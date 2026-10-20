/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
 */

// ============================================================================
// Registration.cpp
// Star-based image registration engine for astronomical image stacking.
//
// Pipeline per image:
//   1. Extract luminance channel (Rec.709 coefficients for RGB)
//   2. Compute adaptive background mesh (64x64 blocks, median + MAD)
//   3. Gaussian blur (sigma=2.0) for stable peak detection
//   4. Detect local maxima above per-block threshold (kSigma above background)
//   5. Refine star positions via weighted centroid (first moment, radius=5px)
//   6. Filter by FWHM and roundness
//   7. Match stars: Triangle matching (20 brightest) -> iterative affine
//      recalculation with sigma clipping -> OpenCV RANSAC final fit
//   8. Warp image with perspective transform (INTER_CUBIC)
//   9. Erode valid zone by R=3px to eliminate cubic ringing artifacts
//  10. Save registered frame as r_<filename>
// ============================================================================

#include "Registration.h"

#include "../astrometry/TriangleMatcher.h"
#include "Statistics.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>

#include <QDir>
#include <QFileInfo>

#include <opencv2/calib3d.hpp>
#include <opencv2/opencv.hpp>

#include "../core/ThreadState.h"
#include "../io/FitsWrapper.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace Stacking {

// ============================================================================
// Internal helpers (file-local)
// ============================================================================

namespace {

// ----------------------------------------------------------------------------
// Separable Gaussian blur, OMP-parallelised horizontal + vertical passes.
// Kernel radius = ceil(3 * sigma), normalised to unit sum.
// ----------------------------------------------------------------------------
void applyGaussianBlur(const std::vector<float>& src,
                       std::vector<float>&       dst,
                       int                       width,
                       int                       height,
                       float                     sigma)
{
    dst.resize(src.size());
    std::vector<float> temp(src.size());

    int kRadius = static_cast<int>(std::ceil(3.0f * sigma));
    if (kRadius < 1) kRadius = 1;
    const int kSize = 2 * kRadius + 1;

    // Build normalised Gaussian kernel
    std::vector<float> kernel(kSize);
    const float sigma2 = 2.0f * sigma * sigma;
    float kernelSum = 0.0f;
    for (int i = -kRadius; i <= kRadius; ++i)
    {
        float v = std::exp(-(i * i) / sigma2);
        kernel[i + kRadius] = v;
        kernelSum += v;
    }
    for (float& v : kernel) v /= kernelSum;

    // Horizontal pass
    #pragma omp parallel for
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float val = 0.0f;
            for (int k = -kRadius; k <= kRadius; ++k)
            {
                const int px = std::clamp(x + k, 0, width - 1);
                val += src[y * width + px] * kernel[k + kRadius];
            }
            temp[y * width + x] = val;
        }
    }

    // Vertical pass (y-outer for cache locality)
    #pragma omp parallel for
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float val = 0.0f;
            for (int k = -kRadius; k <= kRadius; ++k)
            {
                const int py = std::clamp(y + k, 0, height - 1);
                val += temp[py * width + x] * kernel[k + kRadius];
            }
            dst[y * width + x] = val;
        }
    }
}

// ----------------------------------------------------------------------------
// Robust global statistics via subsampled median + MAD-based sigma.
// Samples every 100th pixel to keep cost O(N/100 log N/100).
// ----------------------------------------------------------------------------
void computeGlobalStats(const std::vector<float>& data,
                        float&                    median,
                        float&                    sigma)
{
    if (data.empty())
    {
        median = 0.0f;
        sigma  = 0.0f;
        return;
    }

    // Subsample for speed
    std::vector<float> sample;
    sample.reserve(data.size() / 100 + 1000);
    for (size_t i = 0; i < data.size(); i += 100)
        sample.push_back(data[i]);

    if (sample.empty()) return;

    const size_t n   = sample.size();
    const size_t mid = n / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    median = sample[mid];

    // MAD -> sigma (normal approximation: sigma = 1.4826 * MAD)
    std::vector<float> diffs;
    diffs.reserve(n);
    for (float v : sample) diffs.push_back(std::abs(v - median));
    std::nth_element(diffs.begin(), diffs.begin() + mid, diffs.end());
    sigma = diffs[mid] * 1.4826f;
}

// ----------------------------------------------------------------------------
// Per-block statistics used for adaptive background estimation.
// ----------------------------------------------------------------------------
struct BlockStats
{
    float median = 0.0f;
    float sigma  = 0.0f;  // MAD-based sigma
};

// ----------------------------------------------------------------------------
// Adaptive background mesh: divides the image into boxSize x boxSize blocks,
// computes median and MAD-sigma for each, then provides bilinear interpolation
// of background and noise level at any pixel coordinate.
// ----------------------------------------------------------------------------
class BackgroundMesh
{
public:
    int cols     = 0;
    int rows     = 0;
    int meshSize = 0;
    int width    = 0;
    int height   = 0;

    std::vector<BlockStats> blocks;

    void compute(const std::vector<float>& data,
                 int                       w,
                 int                       h,
                 int                       boxSize = 64)
    {
        width    = w;
        height   = h;
        meshSize = boxSize;
        cols     = (w + boxSize - 1) / boxSize;
        rows     = (h + boxSize - 1) / boxSize;
        blocks.resize(cols * rows);

        #pragma omp parallel
        {
            // Thread-local sample buffer to avoid repeated allocations
            std::vector<float> samples;
            samples.reserve(boxSize * boxSize);

            #pragma omp for
            for (int i = 0; i < cols * rows; ++i)
            {
                samples.clear();

                const int bx = i % cols;
                const int by = i / cols;
                const int x0 = bx * boxSize;
                const int y0 = by * boxSize;
                const int x1 = std::min(x0 + boxSize, width);
                const int y1 = std::min(y0 + boxSize, height);

                for (int y = y0; y < y1; ++y)
                {
                    const float* row = data.data() + y * width;
                    for (int x = x0; x < x1; ++x)
                    {
                        if (std::isfinite(row[x]))
                            samples.push_back(row[x]);
                    }
                }

                if (samples.empty())
                {
                    blocks[i] = {0.0f, 0.0f};
                    continue;
                }

                // Median via nth_element (Hoare, O(N) average)
                const size_t n   = samples.size();
                const size_t mid = n / 2;
                std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
                const float med = samples[mid];

                // MAD
                std::vector<float> diffs;
                diffs.reserve(n);
                for (float v : samples) diffs.push_back(std::abs(v - med));
                std::nth_element(diffs.begin(), diffs.begin() + mid, diffs.end());

                blocks[i] = {med, diffs[mid] * 1.4826f};
            }
        }
    }

    // Bilinear interpolation of background level and noise sigma at pixel (x, y)
    inline void getStats(int x, int y, float& bg, float& sigma) const
    {
        // Map pixel center to fractional block coordinates
        const float fx = static_cast<float>(x) / meshSize - 0.5f;
        const float fy = static_cast<float>(y) / meshSize - 0.5f;

        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));

        const float wx = fx - static_cast<float>(x0);
        const float wy = fy - static_cast<float>(y0);

        int x1 = x0 + 1;
        int y1 = y0 + 1;

        x0 = std::clamp(x0, 0, cols - 1);
        x1 = std::clamp(x1, 0, cols - 1);
        y0 = std::clamp(y0, 0, rows - 1);
        y1 = std::clamp(y1, 0, rows - 1);

        const BlockStats& b00 = blocks[y0 * cols + x0];
        const BlockStats& b10 = blocks[y0 * cols + x1];
        const BlockStats& b01 = blocks[y1 * cols + x0];
        const BlockStats& b11 = blocks[y1 * cols + x1];

        // Bilinear interpolation for median (background)
        const float bTop = b00.median * (1.0f - wx) + b10.median * wx;
        const float bBot = b01.median * (1.0f - wx) + b11.median * wx;
        bg = bTop * (1.0f - wy) + bBot * wy;

        // Bilinear interpolation for sigma (noise)
        const float sTop = b00.sigma * (1.0f - wx) + b10.sigma * wx;
        const float sBot = b01.sigma * (1.0f - wx) + b11.sigma * wx;
        sigma = sTop * (1.0f - wy) + sBot * wy;

        if (sigma < 1e-9f) sigma = 1e-5f;
    }
};

} // anonymous namespace


// ============================================================================
// RegistrationEngine
// ============================================================================

RegistrationEngine::RegistrationEngine(QObject* parent)
    : QObject(parent)
{
}

RegistrationEngine::~RegistrationEngine() = default;


// ============================================================================
// Sequence registration
// Processes images sequentially to keep peak RAM at ~2 image buffers.
// Per-image CPU work (blur, detection, warp) uses the full OMP thread pool.
// ============================================================================

int RegistrationEngine::registerSequence(ImageSequence& sequence,
                                         int             referenceIndex)
{
    m_cancelled = false;

    if (sequence.count() < 2)
        return 0;

    // Resolve reference image
    if (referenceIndex < 0 || referenceIndex >= sequence.count())
        referenceIndex = sequence.findBestReference();
    sequence.setReferenceImage(referenceIndex);

    // Load reference buffer
    ImageBuffer refBuffer;
    if (!sequence.readImage(referenceIndex, refBuffer))
    {
        emit logMessage(tr("Failed to load reference image"), "red");
        return 0;
    }

    emit logMessage(tr("Detecting stars in reference image..."), "");
    emit progressChanged(tr("Analyzing reference"), 0.0);

    // Detect stars in reference (cached for all subsequent comparisons)
    m_referenceStars = detectStars(refBuffer);
    if (static_cast<int>(m_referenceStars.size()) < m_params.minStars)
    {
        emit logMessage(
            tr("Not enough stars in reference: %1").arg(m_referenceStars.size()), "red");
        return 0;
    }

    emit logMessage(
        tr("Reference: %1 stars detected").arg(m_referenceStars.size()), "green");

    int successCount  = 0;
    const int total   = sequence.count();

    // -------------------------------------------------------------------------
    // Handle reference image (identity transform, save immediately)
    // -------------------------------------------------------------------------
    {
        auto& refImg = sequence.image(referenceIndex);
        refImg.registration.hasRegistration = true;
        refImg.registration.shiftX          = 0.0;
        refImg.registration.shiftY          = 0.0;
        refImg.registration.rotation        = 0.0;
        refImg.registration.scaleX          = 1.0;
        refImg.registration.scaleY          = 1.0;
        ++successCount;

        const QString inPath  = sequence.image(referenceIndex).filePath;
        const QFileInfo fi(inPath);
        const QString outName = "r_" + fi.fileName();
        QString outPath;

        if (!m_params.outputDirectory.isEmpty())
        {
            QDir d(m_params.outputDirectory);
            if (!d.exists()) d.mkpath(".");
            outPath = d.filePath(outName);
        }
        else
        {
            outPath = fi.dir().filePath(outName);
        }

        FitsIO::write(outPath, refBuffer);
        emit logMessage(tr("Saved reference: %1").arg(outName), "");
        emit imageRegistered(referenceIndex, true);
    }

    // -------------------------------------------------------------------------
    // Sequential streaming: load -> detect -> match -> warp -> save -> free
    // -------------------------------------------------------------------------
    int processed = 0;
    for (int i = 0; i < total; ++i)
    {
        if (i == referenceIndex) continue;
        if (m_cancelled || !Threading::getThreadRun()) break;

        ++processed;
        emit progressChanged(
            tr("Registering image %1/%2...").arg(processed).arg(total - 1),
            static_cast<double>(processed) / (total - 1));

        // --- Load ---
        ImageBuffer imgBuffer;
        if (!sequence.readImage(i, imgBuffer))
        {
            emit logMessage(tr("Failed to load image %1").arg(i + 1), "salmon");
            emit imageRegistered(i, false);
            continue;
        }

        // --- Detect + match (full OMP parallelism inside) ---
        RegistrationResult result = registerImage(imgBuffer, refBuffer);

        auto& seqImg = sequence.image(i);
        if (!result.success)
        {
            seqImg.registration.hasRegistration = false;
            emit logMessage(
                tr("Image %1: registration FAILED - %2")
                    .arg(i + 1).arg(result.error), "salmon");
            emit imageRegistered(i, false);
            continue;
        }

        seqImg.registration = result.transform;
        ++successCount;

        emit logMessage(
            tr("Image %1: %2 stars detected, %3 matched, "
               "shift=(%4, %5), rot=%6 deg")
                .arg(i + 1)
                .arg(result.starsDetected)
                .arg(result.starsMatched)
                .arg(result.transform.shiftX,  0, 'f', 1)
                .arg(result.transform.shiftY,  0, 'f', 1)
                .arg(result.transform.rotation * 180.0 / M_PI, 0, 'f', 2), "");

        // --- Warp (OpenCV uses its own internal thread pool) ---
        cv::Mat H(3, 3, CV_64F);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                H.at<double>(r, c) = result.transform.H[r][c];

        const int iw  = imgBuffer.width();
        const int ih  = imgBuffer.height();
        const int ich = imgBuffer.channels();

        ImageBuffer warped(iw, ih, ich);

        cv::Mat srcMat(ih, iw, CV_32FC(ich),
                       static_cast<void*>(imgBuffer.data().data()));
        cv::Mat dstMat(ih, iw, CV_32FC(ich),
                       static_cast<void*>(warped.data().data()));

        // INTER_CUBIC: 4x4 bicubic kernel. Sharper than bilinear and
        // approximately 4x faster than LANCZOS4, which is overkill for
        // the sub-10px shifts typical in image stacking.
        cv::warpPerspective(srcMat, dstMat, H, dstMat.size(),
                            cv::INTER_CUBIC,
                            cv::BORDER_CONSTANT, cv::Scalar(0));

        // Build validity mask by warping an all-ones image with the same
        // transform and zeroing pixels whose source was outside the frame.
        cv::Mat srcMask(ih, iw, CV_32FC1, cv::Scalar(1.0f));
        cv::Mat dstMask(ih, iw, CV_32FC1);
        cv::warpPerspective(srcMask, dstMask, H, dstMat.size(),
                            cv::INTER_LINEAR,
                            cv::BORDER_CONSTANT, cv::Scalar(0.0f));

        float*       dstData = reinterpret_cast<float*>(dstMat.data);
        const float* mData   = reinterpret_cast<const float*>(dstMask.data);
        const int    totalPx = iw * ih;

        // Pass 1: zero pixels whose validity mask fell below 1 (border pixels)
        #pragma omp parallel for schedule(static)
        for (int p = 0; p < totalPx; ++p)
        {
            if (mData[p] < 0.999f)
            {
                for (int c = 0; c < ich; ++c)
                    dstData[p * ich + c] = 0.0f;
            }
        }

        // Pass 2: separable erosion by R=3 pixels.
        //
        // warpPerspective(INTER_CUBIC) has a 4x4 kernel (radius=2). Any
        // output pixel whose source footprint straddles the BORDER_CONSTANT=0
        // region can exhibit cubic ringing -- small non-zero values near the
        // border that are invisible to an exact == 0 check. Eroding by R=3
        // guarantees that every retained pixel's full cubic support lies
        // entirely within the valid (unwarped) area, producing clean,
        // artifact-free coverage boundaries.
        {
            const int R = 3;

            // Binary validity map: 1 = all channels non-zero, 0 = border
            std::vector<uint8_t> maskBin(totalPx);
            for (int p = 0; p < totalPx; ++p)
                maskBin[p] = (dstData[p * ich] != 0.0f) ? 1u : 0u;

            // Horizontal erosion (per-row sliding window)
            std::vector<uint8_t> maskX(totalPx, 1u);
            #pragma omp parallel for schedule(static)
            for (int row = 0; row < ih; ++row)
            {
                const uint8_t* src = maskBin.data() + row * iw;
                uint8_t*       dst = maskX.data()   + row * iw;

                for (int col = 0; col < iw; ++col)
                {
                    if (!src[col]) { dst[col] = 0u; continue; }

                    bool bad = false;
                    const int lo = std::max(0,      col - R);
                    const int hi = std::min(iw - 1, col + R);
                    for (int nc = lo; nc <= hi && !bad; ++nc)
                        if (!src[nc]) bad = true;

                    dst[col] = bad ? 0u : 1u;
                }
            }

            // Vertical erosion + apply: zero pixels invalidated by either axis
            #pragma omp parallel for schedule(static)
            for (int col = 0; col < iw; ++col)
            {
                for (int row = 0; row < ih; ++row)
                {
                    // Already invalidated by horizontal pass
                    if (!maskX[row * iw + col])
                    {
                        for (int c = 0; c < ich; ++c)
                            dstData[(row * iw + col) * ich + c] = 0.0f;
                        continue;
                    }

                    bool bad = false;
                    const int lo = std::max(0,      row - R);
                    const int hi = std::min(ih - 1, row + R);
                    for (int nr = lo; nr <= hi && !bad; ++nr)
                        if (!maskX[nr * iw + col]) bad = true;

                    if (bad)
                    {
                        for (int c = 0; c < ich; ++c)
                            dstData[(row * iw + col) * ich + c] = 0.0f;
                    }
                }
            }
        }

        // Preserve FITS WCS metadata from the source frame
        warped.setMetadata(imgBuffer.metadata());

        // Release input buffer before I/O to minimise peak RAM
        imgBuffer = ImageBuffer();

        // --- Save ---
        const QString inPath  = sequence.image(i).filePath;
        const QFileInfo fi(inPath);
        const QString outName = "r_" + fi.fileName();
        QString outPath;

        if (!m_params.outputDirectory.isEmpty())
        {
            QDir d(m_params.outputDirectory);
            outPath = d.filePath(outName);
        }
        else
        {
            outPath = fi.dir().filePath(outName);
        }

        FitsIO::write(outPath, warped);
        emit logMessage(tr("Saved: %1").arg(outName), "");
        emit imageRegistered(i, true);
    }

    emit progressChanged(tr("Registration complete"), 1.0);
    emit finished(successCount);

    return successCount;
}


// ============================================================================
// Single image registration
// ============================================================================

RegistrationResult RegistrationEngine::registerImage(const ImageBuffer& image,
                                                      const ImageBuffer& reference)
{
    RegistrationResult result;

    // Detect stars in target image
    std::vector<DetectedStar> targetStars = detectStars(image);

    if (static_cast<int>(targetStars.size()) < m_params.minStars)
    {
        result.error = tr("Not enough stars: %1").arg(targetStars.size());
        return result;
    }

    // Use cached reference stars when available; detect on demand otherwise
    std::vector<DetectedStar>  tempRefStars;
    std::vector<DetectedStar>* refStars = &m_referenceStars;

    if (refStars->empty())
    {
        tempRefStars = detectStars(reference);
        refStars     = &tempRefStars;
    }

    result.starsDetected = static_cast<int>(targetStars.size());

    const int matched = matchStars(*refStars, targetStars, result.transform);
    result.starsMatched = matched;

    if (matched < m_params.minMatches)
    {
        result.error = tr("Not enough matches: %1").arg(matched);
        return result;
    }

    result.success               = true;
    result.transform.hasRegistration = true;
    result.quality = static_cast<double>(matched) / targetStars.size();

    return result;
}


// ============================================================================
// Luminance extraction
// Uses Rec.709 coefficients for RGB images; identity for monochrome.
// ============================================================================

std::vector<float> RegistrationEngine::extractLuminance(const ImageBuffer& image)
{
    const int width    = image.width();
    const int height   = image.height();
    const int channels = image.channels();

    std::vector<float> lum(width * height);
    const float* data = image.data().data();

    if (channels == 1)
    {
        std::copy(data, data + width * height, lum.begin());
    }
    else
    {
        // Rec.709 luminance: Y = 0.2126 R + 0.7152 G + 0.0722 B
        #pragma omp parallel for
        for (int i = 0; i < width * height; ++i)
        {
            lum[i] = 0.2126f * data[i * channels]
                   + 0.7152f * data[i * channels + 1]
                   + 0.0722f * data[i * channels + 2];
        }
    }

    return lum;
}


// ============================================================================
// Background statistics (delegates to computeGlobalStats)
// Legacy interface retained for API compatibility.
// ============================================================================

void RegistrationEngine::computeBackground(const std::vector<float>& data,
                                            int                       width,
                                            int                       height,
                                            float&                    background,
                                            float&                    rms)
{
    Q_UNUSED(width)
    Q_UNUSED(height)
    computeGlobalStats(data, background, rms);
}


// ============================================================================
// findLocalMaxima -- superseded by BackgroundMesh + smoothed-peak detection.
// Retained as a stub for API compatibility.
// ============================================================================

std::vector<std::pair<int, int>>
RegistrationEngine::findLocalMaxima(const std::vector<float>& data,
                                    int                       width,
                                    int                       height,
                                    float                     threshold)
{
    Q_UNUSED(data)
    Q_UNUSED(width)
    Q_UNUSED(height)
    Q_UNUSED(threshold)
    return {};
}


// ============================================================================
// Star position refinement via weighted centroid (first moment, radius=5px)
// Also computes FWHM from the second moment and roundness from axis ratio.
// ============================================================================

bool RegistrationEngine::refineStarPosition(const std::vector<float>& data,
                                             int                       width,
                                             int                       height,
                                             int                       cx,
                                             int                       cy,
                                             DetectedStar&             star)
{
    const int radius = 5;

    // Local minimum as background estimate
    float minVal = std::numeric_limits<float>::max();
    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int px = std::clamp(cx + dx, 0, width  - 1);
            const int py = std::clamp(cy + dy, 0, height - 1);
            minVal = std::min(minVal, data[py * width + px]);
        }
    }
    const float background = minVal;

    // First moment (weighted centroid)
    double sumX    = 0.0;
    double sumY    = 0.0;
    double sumW    = 0.0;
    double sumPeak = 0.0;

    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int px = cx + dx;
            const int py = cy + dy;
            if (px < 0 || px >= width || py < 0 || py >= height) continue;

            const float val = data[py * width + px] - background;
            if (val <= 0.0f) continue;

            sumX += dx * val;
            sumY += dy * val;
            sumW += val;
            if (val > sumPeak) sumPeak = val;
        }
    }

    if (sumW <= 0.0) return false;

    const float subX = static_cast<float>(sumX / sumW);
    const float subY = static_cast<float>(sumY / sumW);

    // Reject false peaks whose centroid is too far from the detection pixel
    if (std::abs(subX) > 2.0f || std::abs(subY) > 2.0f) return false;

    star.x    = static_cast<float>(cx) + subX;
    star.y    = static_cast<float>(cy) + subY;
    star.peak = static_cast<float>(sumPeak);
    star.flux = static_cast<float>(sumW);

    // Second moment (variance) for FWHM
    double sumXX = 0.0;
    double sumYY = 0.0;

    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int px = cx + dx;
            const int py = cy + dy;
            if (px < 0 || px >= width || py < 0 || py >= height) continue;

            const float val = data[py * width + px] - background;
            if (val <= 0.0f) continue;

            // Residuals relative to refined centroid
            const double rx = static_cast<double>(dx) - subX;
            const double ry = static_cast<double>(dy) - subY;
            sumXX += rx * rx * val;
            sumYY += ry * ry * val;
        }
    }

    const double sigmaX2 = std::max(0.0, sumXX / sumW);
    const double sigmaY2 = std::max(0.0, sumYY / sumW);

    const float sigmaX = std::sqrt(static_cast<float>(sigmaX2));
    const float sigmaY = std::sqrt(static_cast<float>(sigmaY2));

    // Gaussian FWHM = 2.355 * sigma (mean of both axes)
    star.fwhm = 2.355f * (sigmaX + sigmaY) * 0.5f;

    // Roundness: minor / major axis ratio
    if (sigmaX > 1e-6f && sigmaY > 1e-6f)
        star.roundness = std::min(sigmaX, sigmaY) / std::max(sigmaX, sigmaY);
    else
        star.roundness = 0.0f;

    return true;
}


// ============================================================================
// Star detection
// Full pipeline: luminance -> background mesh -> Gaussian blur ->
// local-maxima with adaptive threshold -> weighted-centroid refinement ->
// quality filters -> sort by flux -> limit count.
// ============================================================================

std::vector<DetectedStar>
RegistrationEngine::detectStars(const ImageBuffer& image)
{
    std::vector<DetectedStar> stars;

    // Extract luminance channel
    std::vector<float> lum = extractLuminance(image);
    const int width        = image.width();
    const int height       = image.height();

    if (m_cancelled) return stars;

    // Adaptive background mesh (64x64 blocks)
    BackgroundMesh bgMesh;
    bgMesh.compute(lum, width, height, 64);

    // Gaussian blur for peak detection (sigma=2 suppresses single-pixel noise)
    std::vector<float> smoothLum;
    applyGaussianBlur(lum, smoothLum, width, height, 2.0f);

    // Detection threshold: kSigma above local background
    const int   r      = 2;
    const float kSigma = (m_params.detectionThreshold > 0.1f)
                             ? m_params.detectionThreshold
                             : 3.0f;

    // Thread-local star lists to avoid locking
#ifdef _OPENMP
    const int maxThreads = omp_get_max_threads();
#else
    const int maxThreads = 1;
#endif
    std::vector<std::vector<DetectedStar>> threadStars(maxThreads);

    #pragma omp parallel
    {
#ifdef _OPENMP
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        threadStars[tid].reserve(2000);

        #pragma omp for
        for (int y = r; y < height - r; ++y)
        {
            for (int x = r; x < width - r; ++x)
            {
                // Local background threshold
                float bg, sigma;
                bgMesh.getStats(x, y, bg, sigma);

                const float threshVal = bg + kSigma * sigma;
                const float val       = smoothLum[y * width + x];

                if (val <= threshVal) continue;

                // Local maximum check within radius r
                bool isMax = true;
                for (int dy = -r; dy <= r && isMax; ++dy)
                {
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        if (dx == 0 && dy == 0) continue;
                        if (smoothLum[(y + dy) * width + (x + dx)] > val)
                        {
                            isMax = false;
                            break;
                        }
                    }
                }
                if (!isMax) continue;

                // Boundary check for refinement window
                const int refineR = 5;
                if (x < refineR || x >= width  - refineR ||
                    y < refineR || y >= height - refineR)
                    continue;

                // Weighted centroid + second moment on unsmoothed luminance
                float localBg, localSigma;
                bgMesh.getStats(x, y, localBg, localSigma);

                double sumWX  = 0.0, sumWY  = 0.0, sumWt = 0.0;
                double sumWXX = 0.0, sumWYY = 0.0;
                float  peakVal = 0.0f;

                for (int ry = -refineR; ry <= refineR; ++ry)
                {
                    for (int rx = -refineR; rx <= refineR; ++rx)
                    {
                        const float v = lum[(y + ry) * width + (x + rx)] - localBg;
                        if (v <= 0.0f) continue;

                        sumWX  += rx * v;
                        sumWY  += ry * v;
                        sumWt  += v;
                        sumWXX += rx * rx * v;
                        sumWYY += ry * ry * v;
                        if (v > peakVal) peakVal = v;
                    }
                }

                if (sumWt <= 0.0 || peakVal <= 0.0f) continue;

                const float dx = static_cast<float>(sumWX / sumWt);
                const float dy = static_cast<float>(sumWY / sumWt);

                // Reject centroids too far from detection pixel (likely noise)
                if (std::abs(dx) > 2.0f || std::abs(dy) > 2.0f) continue;

                // Compute FWHM from second moment (variance = E[x^2] - E[x]^2)
                double varX = sumWXX / sumWt - dx * dx;
                double varY = sumWYY / sumWt - dy * dy;
                if (varX < 0.0) varX = 0.0;
                if (varY < 0.0) varY = 0.0;

                const float sigmaX = std::sqrt(static_cast<float>(varX));
                const float sigmaY = std::sqrt(static_cast<float>(varY));

                float roundness = 0.0f;
                if (sigmaX > 1e-6f && sigmaY > 1e-6f)
                    roundness = std::min(sigmaX, sigmaY) / std::max(sigmaX, sigmaY);

                if (roundness < m_params.minRoundness) continue;

                DetectedStar s;
                s.x         = static_cast<float>(x) + dx;
                s.y         = static_cast<float>(y) + dy;
                s.peak      = peakVal;
                s.flux      = static_cast<float>(sumWt);
                s.fwhm      = 2.355f * (sigmaX + sigmaY) * 0.5f;
                s.roundness = roundness;
                s.snr       = peakVal / localSigma;

                if (s.fwhm < m_params.minFWHM || s.fwhm > m_params.maxFWHM)
                    continue;

                threadStars[tid].push_back(s);
            }
        }
    }

    // Merge thread-local lists
    for (const auto& t : threadStars)
        stars.insert(stars.end(), t.begin(), t.end());

    // Sort by total flux (descending) -- brighter stars rank higher
    std::sort(stars.begin(), stars.end(),
              [](const DetectedStar& a, const DetectedStar& b)
              { return a.flux > b.flux; });

    // Limit to maximum allowed count
    const int maxStars = (m_params.maxStars > 0) ? m_params.maxStars : 2000;
    if (static_cast<int>(stars.size()) > maxStars)
        stars.resize(maxStars);

    return stars;
}


// ============================================================================
// DetectedStar -> MatchStar conversion
// Converts flux to a proxy magnitude (negated flux) so that TriangleMatcher's
// ascending-magnitude sort corresponds to descending brightness.
// ============================================================================

bool RegistrationEngine::convertToMatchStars(const std::vector<DetectedStar>& src,
                                              std::vector<MatchStar>&          dst)
{
    dst.clear();
    dst.reserve(src.size());

    for (size_t i = 0; i < src.size(); ++i)
    {
        MatchStar s;
        s.id       = static_cast<int>(i);
        s.index    = static_cast<int>(i);
        s.x        = src[i].x;
        s.y        = src[i].y;
        s.mag      = -src[i].flux;   // Negate: larger flux -> smaller (brighter) mag proxy
        s.match_id = -1;
        dst.push_back(s);
    }

    return true;
}


// ============================================================================
// Star matching pipeline
//
// Phase 1: Triangle matching on the 20 brightest stars (TriangleMatcher)
//          -> initial rough affine A[2][3].
//
// Phase 2+3: Two rounds of O(N log N) proximity matching (binary search on
//            sorted ref-X) + iterative affine recalculation with sigma clipping
//            (atMatchLists + atRecalcTrans analogue).
//
// Phase 4: OpenCV RANSAC estimateAffine2D (threshold=3px) -> final 3x3
//          homography stored in RegistrationData.
// ============================================================================

int RegistrationEngine::matchStars(const std::vector<DetectedStar>& refStars,
                                    const std::vector<DetectedStar>& targetStars,
                                    RegistrationData&                regData)
{
    if (refStars.size() < 5 || targetStars.size() < 5)
        return 0;

    // -------------------------------------------------------------------------
    // Phase 1: Triangle matching (AT_MATCH_NBRIGHT = 20)
    // -------------------------------------------------------------------------
    TriangleMatcher matcher;
    matcher.setMaxStars(20);

    std::vector<MatchStar> mRef, mTarget;
    convertToMatchStars(refStars,    mRef);
    convertToMatchStars(targetStars, mTarget);

    GenericTrans              gTrans;
    std::vector<MatchStar>    regMatchedA, regMatchedB;

    // solve(imgStars=target, catStars=ref): transform target -> ref
    // Scale range 0.9..1.1 appropriate for image-to-image registration
    if (!matcher.solve(mTarget, mRef, gTrans,
                       regMatchedA, regMatchedB, 0.9, 1.1))
        return 0;

    // Initial affine: ref = A * [1, tx, ty]^T
    double A[2][3] = {
        { gTrans.x00, gTrans.x10, gTrans.x01 },
        { gTrans.y00, gTrans.y10, gTrans.y01 }
    };

    const int    nRef     = static_cast<int>(mRef.size());
    const int    nTarget  = static_cast<int>(mTarget.size());
    const double kRadius  = 5.0;                 // AT_MATCH_RADIUS
    const double kRadius2 = kRadius * kRadius;
    const int    kMaxIter = 5;                   // AT_MATCH_MAXITER
    const double kHaltSig = 0.1;                 // AT_MATCH_HALTSIGMA
    const double kNSig    = 10.0;                // AT_MATCH_NSIGMA
    const double kPctile  = 0.35;                // AT_MATCH_PERCENTILE

    // Inline 3x3 determinant (row pointers to avoid lambda capture issues)
    auto det3x3 = [](const double* r0,
                     const double* r1,
                     const double* r2) -> double
    {
        return  r0[0] * (r1[1]*r2[2] - r2[1]*r1[2])
              - r0[1] * (r1[0]*r2[2] - r1[2]*r2[0])
              + r0[2] * (r1[0]*r2[1] - r1[1]*r2[0]);
    };

    // -------------------------------------------------------------------------
    // Iterative affine refit with sigma-clipping (atRecalcTrans analogue).
    // Updates A[][] in place; does NOT modify the pairs vector.
    // Returns the number of inlier pairs.
    // -------------------------------------------------------------------------
    auto recalcAffine = [&](const std::vector<std::pair<int,int>>& allPairs) -> int
    {
        const int n = static_cast<int>(allPairs.size());
        if (n < 6) return 0;

        std::vector<bool> active(n, true);

        for (int iter = 0; iter < kMaxIter; ++iter)
        {
            int nAct = 0;
            for (bool b : active) if (b) ++nAct;
            if (nAct < 6) return 0;

            // Accumulate normal-equation sums
            double sumx  = 0, sumy  = 0, sumx2 = 0, sumy2 = 0, sumxy = 0;
            double sumxp = 0, sumxpx = 0, sumxpy = 0;
            double sumyp = 0, sumypx = 0, sumypy = 0;

            for (int i = 0; i < n; ++i)
            {
                if (!active[i]) continue;
                const double sx = mTarget[allPairs[i].first ].x;
                const double sy = mTarget[allPairs[i].first ].y;
                const double rx = mRef   [allPairs[i].second].x;
                const double ry = mRef   [allPairs[i].second].y;

                sumx  += sx;  sumy  += sy;
                sumx2 += sx*sx; sumy2 += sy*sy; sumxy += sx*sy;
                sumxp += rx;  sumxpx += rx*sx;  sumxpy += rx*sy;
                sumyp += ry;  sumypx += ry*sx;  sumypy += ry*sy;
            }

            const double Nd  = static_cast<double>(nAct);
            double M[3][3]   = {
                { Nd,   sumx, sumy  },
                { sumx, sumx2, sumxy },
                { sumy, sumxy, sumy2 }
            };
            const double det = det3x3(M[0], M[1], M[2]);
            if (std::abs(det) < 1e-9) return 0;

            // Solve x-row via Cramer's rule
            {
                double M0[3][3] = {
                    {sumxp,  M[0][1], M[0][2]},
                    {sumxpx, M[1][1], M[1][2]},
                    {sumxpy, M[2][1], M[2][2]}
                };
                double M1[3][3] = {
                    {M[0][0], sumxp,  M[0][2]},
                    {M[1][0], sumxpx, M[1][2]},
                    {M[2][0], sumxpy, M[2][2]}
                };
                double M2[3][3] = {
                    {M[0][0], M[0][1], sumxp },
                    {M[1][0], M[1][1], sumxpx},
                    {M[2][0], M[2][1], sumxpy}
                };
                A[0][0] = det3x3(M0[0],M0[1],M0[2]) / det;
                A[0][1] = det3x3(M1[0],M1[1],M1[2]) / det;
                A[0][2] = det3x3(M2[0],M2[1],M2[2]) / det;
            }

            // Solve y-row via Cramer's rule
            {
                double M0[3][3] = {
                    {sumyp,  M[0][1], M[0][2]},
                    {sumypx, M[1][1], M[1][2]},
                    {sumypy, M[2][1], M[2][2]}
                };
                double M1[3][3] = {
                    {M[0][0], sumyp,  M[0][2]},
                    {M[1][0], sumypx, M[1][2]},
                    {M[2][0], sumypy, M[2][2]}
                };
                double M2[3][3] = {
                    {M[0][0], M[0][1], sumyp },
                    {M[1][0], M[1][1], sumypx},
                    {M[2][0], M[2][1], sumypy}
                };
                A[1][0] = det3x3(M0[0],M0[1],M0[2]) / det;
                A[1][1] = det3x3(M1[0],M1[1],M1[2]) / det;
                A[1][2] = det3x3(M2[0],M2[1],M2[2]) / det;
            }

            // Compute squared residuals for all active pairs
            std::vector<double> res2(n, 1e18);
            for (int i = 0; i < n; ++i)
            {
                if (!active[i]) continue;
                const double sx = mTarget[allPairs[i].first ].x;
                const double sy = mTarget[allPairs[i].first ].y;
                const double rx = mRef   [allPairs[i].second].x;
                const double ry = mRef   [allPairs[i].second].y;
                const double px = A[0][0] + A[0][1]*sx + A[0][2]*sy;
                const double py = A[1][0] + A[1][1]*sx + A[1][2]*sy;
                res2[i] = (px - rx)*(px - rx) + (py - ry)*(py - ry);
            }

            // Estimate sigma from the specified percentile of active residuals
            std::vector<double> sortedRes;
            sortedRes.reserve(nAct);
            for (int i = 0; i < n; ++i)
                if (active[i]) sortedRes.push_back(res2[i]);
            std::sort(sortedRes.begin(), sortedRes.end());

            const double sigma2 =
                sortedRes[std::max(0, static_cast<int>(sortedRes.size() * kPctile))];

            if (sigma2 < kHaltSig * kHaltSig) break;  // Converged

            const double threshold = kNSig * sigma2;
            int removed = 0;
            for (int i = 0; i < n; ++i)
            {
                if (active[i] && res2[i] > threshold)
                {
                    active[i] = false;
                    ++removed;
                }
            }
            if (removed == 0) break;
        }

        int inliers = 0;
        for (bool b : active) if (b) ++inliers;
        return inliers;
    };

    // -------------------------------------------------------------------------
    // Phase 2+3: Two rounds of proximity matching + affine recalculation.
    // Reference stars are sorted by X to enable O(N log N) nearest-neighbour
    // search instead of O(N^2).
    // -------------------------------------------------------------------------
    std::vector<int> refByX(nRef);
    std::iota(refByX.begin(), refByX.end(), 0);
    std::sort(refByX.begin(), refByX.end(),
              [&](int a, int b) { return mRef[a].x < mRef[b].x; });

    std::vector<double> refXSorted(nRef);
    for (int j = 0; j < nRef; ++j)
        refXSorted[j] = mRef[refByX[j]].x;

    std::vector<std::pair<int,int>> matchedPairs;

    for (int round = 0; round < 2; ++round)
    {
        matchedPairs.clear();
        std::vector<bool> refUsed(nRef, false);

        for (int i = 0; i < nTarget; ++i)
        {
            // Apply current affine to the target star position
            const double tx = A[0][0] + A[0][1]*mTarget[i].x + A[0][2]*mTarget[i].y;
            const double ty = A[1][0] + A[1][1]*mTarget[i].x + A[1][2]*mTarget[i].y;

            // Binary search: find ref stars within kRadius in X
            const int lo = static_cast<int>(
                std::lower_bound(refXSorted.begin(), refXSorted.end(),
                                 tx - kRadius) - refXSorted.begin());
            const int hi = static_cast<int>(
                std::upper_bound(refXSorted.begin(), refXSorted.end(),
                                 tx + kRadius) - refXSorted.begin());

            double minD2  = kRadius2;
            int    bestJ  = -1;

            for (int ki = lo; ki < hi; ++ki)
            {
                const int j = refByX[ki];
                if (refUsed[j]) continue;

                const double dY = ty - mRef[j].y;
                if (dY * dY >= kRadius2) continue;  // Fast Y reject

                const double dX = tx - mRef[j].x;
                const double d2 = dX*dX + dY*dY;
                if (d2 < minD2) { minD2 = d2; bestJ = j; }
            }

            if (bestJ >= 0)
            {
                matchedPairs.push_back({i, bestJ});
                refUsed[bestJ] = true;
            }
        }

        if (static_cast<int>(matchedPairs.size()) < 6) break;

        recalcAffine(matchedPairs);
    }

    const int nMatched = static_cast<int>(matchedPairs.size());
    if (nMatched < 6) return 0;

    // -------------------------------------------------------------------------
    // Phase 4: OpenCV RANSAC final affine fit (threshold = 3px)
    // -------------------------------------------------------------------------
    std::vector<cv::Point2f> srcPts, dstPts;
    srcPts.reserve(nMatched);
    dstPts.reserve(nMatched);

    for (auto& [ti, ri] : matchedPairs)
    {
        srcPts.emplace_back(static_cast<float>(mTarget[ti].x),
                            static_cast<float>(mTarget[ti].y));
        dstPts.emplace_back(static_cast<float>(mRef[ri].x),
                            static_cast<float>(mRef[ri].y));
    }

    cv::Mat affM = cv::estimateAffine2D(srcPts, dstPts,
                                        cv::noArray(), cv::RANSAC, 3.0);
    if (affM.empty() || cv::countNonZero(affM) < 1)
        return 0;

    // Embed the 2x3 affine result in a 3x3 homography
    regData.hasRegistration = true;

    regData.H[0][0] = affM.at<double>(0, 0);
    regData.H[0][1] = affM.at<double>(0, 1);
    regData.H[0][2] = affM.at<double>(0, 2);
    regData.H[1][0] = affM.at<double>(1, 0);
    regData.H[1][1] = affM.at<double>(1, 1);
    regData.H[1][2] = affM.at<double>(1, 2);
    regData.H[2][0] = 0.0;
    regData.H[2][1] = 0.0;
    regData.H[2][2] = 1.0;

    regData.shiftX   = affM.at<double>(0, 2);
    regData.shiftY   = affM.at<double>(1, 2);
    regData.scaleX   = std::sqrt(regData.H[0][0]*regData.H[0][0]
                                 + regData.H[1][0]*regData.H[1][0]);
    regData.scaleY   = std::sqrt(regData.H[0][1]*regData.H[0][1]
                                 + regData.H[1][1]*regData.H[1][1]);
    regData.rotation = std::atan2(regData.H[1][0], regData.H[0][0]);

    return nMatched;
}


// ============================================================================
// RegistrationWorker
// ============================================================================

RegistrationWorker::RegistrationWorker(ImageSequence*          sequence,
                                        const RegistrationParams& params,
                                        int                       referenceIndex,
                                        QObject*                  parent)
    : QThread(parent)
    , m_sequence(sequence)
    , m_params(params)
    , m_referenceIndex(referenceIndex)
{
    m_engine.setParams(params);

    connect(&m_engine, &RegistrationEngine::progressChanged,
            this,      &RegistrationWorker::progressChanged);
    connect(&m_engine, &RegistrationEngine::logMessage,
            this,      &RegistrationWorker::logMessage);
    connect(&m_engine, &RegistrationEngine::imageRegistered,
            this,      &RegistrationWorker::imageRegistered);
}

void RegistrationWorker::run()
{
    const int count = m_engine.registerSequence(*m_sequence, m_referenceIndex);
    emit finished(count);
}

} // namespace Stacking