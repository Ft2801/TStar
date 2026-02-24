#include "Registration.h"

#include "../astrometry/TriangleMatcher.h"
#include "Statistics.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <iterator>
#include <numeric>
#include <QFileInfo>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>
#include "../io/FitsWrapper.h"
#ifdef _OPENMP
#include <omp.h>
#endif

#include "../core/ThreadState.h"

namespace Stacking {

//=============================================================================
// CONSTRUCTOR / DESTRUCTOR
//=============================================================================

RegistrationEngine::RegistrationEngine(QObject* parent)
    : QObject(parent)
{
}

RegistrationEngine::~RegistrationEngine() = default;

//=============================================================================
// SEQUENCE REGISTRATION
//=============================================================================

int RegistrationEngine::registerSequence(ImageSequence& sequence, int referenceIndex) {
    m_cancelled = false;
    
    if (sequence.count() < 2) {
        return 0;
    }
    
    // Determine reference image
    if (referenceIndex < 0 || referenceIndex >= sequence.count()) {
        referenceIndex = sequence.findBestReference();
    }
    sequence.setReferenceImage(referenceIndex);
    
    // Load and analyze reference image
    ImageBuffer refBuffer;
    if (!sequence.readImage(referenceIndex, refBuffer)) {
        emit logMessage(tr("Failed to load reference image"), "red");
        return 0;
    }
    
    emit logMessage(tr("Detecting stars in reference image..."), "");
    emit progressChanged(tr("Analyzing reference"), 0.0);
    
    m_referenceStars = detectStars(refBuffer);
    if (static_cast<int>(m_referenceStars.size()) < m_params.minStars) {
        emit logMessage(tr("Not enough stars in reference: %1").arg(m_referenceStars.size()), "red");
        return 0;
    }
    
    
    emit logMessage(tr("Reference: %1 stars detected").arg(m_referenceStars.size()), "green");
    
    // Register each image
    int successCount = 0;
    int totalImages = sequence.count();
    
    // Handle reference image first (sequential)
    {
        auto& img = sequence.image(referenceIndex);
        img.registration.hasRegistration = true;
        img.registration.shiftX = 0;
        img.registration.shiftY = 0;
        img.registration.rotation = 0;
        img.registration.scaleX = 1.0;
        img.registration.scaleY = 1.0;
        successCount++;
        
        // Save reference image as r_...
        QString inPath = sequence.image(referenceIndex).filePath;
        QFileInfo fi(inPath);
        QString outName = "r_" + fi.fileName();
        QString outPath;
        
        if (!m_params.outputDirectory.isEmpty()) {
             QDir d(m_params.outputDirectory);
             if (!d.exists()) d.mkpath(".");
             outPath = d.filePath(outName);
        } else {
             outPath = fi.dir().filePath(outName);
        }
        
        FitsIO::write(outPath, refBuffer);
        emit logMessage(tr("Saved reference: %1").arg(outName), "");
        emit imageRegistered(referenceIndex, true);
    }
    
    // ===== SEQUENTIAL STREAMING REGISTRATION =====
    // Process one image at a time: load → detect → match → warp → save → free.
    // This keeps peak memory at ~2 image buffers (reference + current) regardless
    // of sequence length.  Per-image CPU operations (blur, detection, warp) use
    // the full OMP thread pool internally — no nested parallelism.
    int processed = 0;
    for (int i = 0; i < totalImages; ++i) {
        if (i == referenceIndex) continue;
        if (m_cancelled || !Threading::getThreadRun()) break;

        processed++;
        emit progressChanged(
            tr("Registering image %1/%2...").arg(processed).arg(totalImages - 1),
            static_cast<double>(processed) / (totalImages - 1));

        // --- Load ---
        ImageBuffer imgBuffer;
        if (!sequence.readImage(i, imgBuffer)) {
            emit logMessage(tr("Failed to load image %1").arg(i + 1), "salmon");
            emit imageRegistered(i, false);
            continue;
        }

        // --- Detect + Match (full OMP parallelism inside) ---
        RegistrationResult result = registerImage(imgBuffer, refBuffer);

        auto& seqImg = sequence.image(i);
        if (!result.success) {
            seqImg.registration.hasRegistration = false;
            emit logMessage(tr("Image %1: registration FAILED - %2")
                            .arg(i + 1).arg(result.error), "salmon");
            emit imageRegistered(i, false);
            continue;
        }

        seqImg.registration = result.transform;
        successCount++;

        emit logMessage(
            tr("Image %1: %2 stars detected, %3 matched, shift=(%4, %5), rot=%6 deg")
                .arg(i + 1)
                .arg(result.starsDetected)
                .arg(result.starsMatched)
                .arg(result.transform.shiftX, 0, 'f', 1)
                .arg(result.transform.shiftY, 0, 'f', 1)
                .arg(result.transform.rotation * 180.0 / M_PI, 0, 'f', 2), "");

        // --- Warp (OpenCV uses its own thread pool internally) ---
        cv::Mat H(3, 3, CV_64F);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                H.at<double>(r, c) = result.transform.H[r][c];

        int iw = imgBuffer.width();
        int ih = imgBuffer.height();
        int ich = imgBuffer.channels();
        ImageBuffer warped(iw, ih, ich);

        cv::Mat srcMat(ih, iw, CV_32FC(ich), (void*)imgBuffer.data().data());
        cv::Mat dstMat(ih, iw, CV_32FC(ich), (void*)warped.data().data());

        // INTER_CUBIC: 4×4 bicubic kernel — sharper than bilinear, 4× faster than LANCZOS4.
        // LANCZOS4 (8×8 kernel) is overkill for ~10px shifts and costs ~16× more per pixel.
        cv::warpPerspective(srcMat, dstMat, H, dstMat.size(),
                            cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(0));

        // Zero out border pixels that were extrapolated outside the source frame.
        cv::Mat mask(ih, iw, CV_32FC1, cv::Scalar(1.0f));
        cv::Mat dstMask(ih, iw, CV_32FC1);
        cv::warpPerspective(mask, dstMask, H, dstMat.size(),
                            cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0.0f));

        float* dstData = (float*)dstMat.data;
        const float* mData = (const float*)dstMask.data;
        const int totalPx = iw * ih;

        // Pass 1 — direct mask zeroing (same as before).
        #pragma omp parallel for schedule(static)
        for (int p = 0; p < totalPx; ++p) {
            if (mData[p] < 0.999f) {
                for (int c = 0; c < ich; ++c)
                    dstData[p * ich + c] = 0.0f;
            }
        }

        // Pass 2 — erode the valid zone by R pixels (separable min-filter).
        //
        // warpPerspective(INTER_CUBIC) has a 4×4 kernel (radius = 2). Any output
        // pixel whose source footprint straddles the BORDER_CONSTANT=0 region gets
        // cubic ringing → small non-zero values near the border that are NOT caught
        // by the exact == 0 check in the stacking engine.  Eroding by R=3 ensures
        // every kept pixel's full cubic kernel lies inside the valid (unwarped) area,
        // producing clean, artifact-free coverage boundaries.
        {
            const int R = 3;

            // Binary validity mask: 1 = valid (all channels nonzero), 0 = border.
            std::vector<uint8_t> maskBin(totalPx);
            for (int p = 0; p < totalPx; ++p)
                maskBin[p] = (dstData[p * ich] != 0.0f) ? 1u : 0u;

            // X erosion (per-row sliding window).
            std::vector<uint8_t> maskX(totalPx, 1u);
            #pragma omp parallel for schedule(static)
            for (int row = 0; row < ih; ++row) {
                const uint8_t* src = maskBin.data() + row * iw;
                uint8_t*       dst = maskX.data()   + row * iw;
                for (int col = 0; col < iw; ++col) {
                    if (!src[col]) { dst[col] = 0; continue; }
                    bool bad = false;
                    int lo = std::max(0, col - R), hi = std::min(iw - 1, col + R);
                    for (int nc = lo; nc <= hi && !bad; ++nc)
                        if (!src[nc]) bad = true;
                    dst[col] = bad ? 0u : 1u;
                }
            }

            // Y erosion + apply: zero pixels invalidated by either axis.
            #pragma omp parallel for schedule(static)
            for (int col = 0; col < iw; ++col) {
                for (int row = 0; row < ih; ++row) {
                    // Already invalid after X pass?
                    if (!maskX[row * iw + col]) {
                        for (int c = 0; c < ich; ++c)
                            dstData[(row * iw + col) * ich + c] = 0.0f;
                        continue;
                    }
                    bool bad = false;
                    int lo = std::max(0, row - R), hi = std::min(ih - 1, row + R);
                    for (int nr = lo; nr <= hi && !bad; ++nr)
                        if (!maskX[nr * iw + col]) bad = true;
                    if (bad) {
                        for (int c = 0; c < ich; ++c)
                            dstData[(row * iw + col) * ich + c] = 0.0f;
                    }
                }
            }
        }

        // Input buffer no longer needed — free before I/O to minimise peak RAM.
        imgBuffer = ImageBuffer();

        // --- Save ---
        QString inPath = sequence.image(i).filePath;
        QFileInfo fi(inPath);
        QString outName = "r_" + fi.fileName();
        QString outPath;
        if (!m_params.outputDirectory.isEmpty()) {
            QDir d(m_params.outputDirectory);
            outPath = d.filePath(outName);
        } else {
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

//=============================================================================
// SINGLE IMAGE REGISTRATION
//=============================================================================

RegistrationResult RegistrationEngine::registerImage(const ImageBuffer& image,
                                                      const ImageBuffer& reference) {
    RegistrationResult result;
    
    // Detect stars in target image
    // emit logMessage(tr("Detecting stars in target image..."), "");
    std::vector<DetectedStar> targetStars = detectStars(image);
    // emit logMessage(tr("Target: %1 stars detected").arg(targetStars.size()), "");
    
    if (static_cast<int>(targetStars.size()) < m_params.minStars) {
        result.error = tr("Not enough stars: %1").arg(targetStars.size());
        return result;
    }
    
        // Use cached reference stars if available
    std::vector<DetectedStar>* refStars = &m_referenceStars;
    std::vector<DetectedStar> tempRefStars;
    
    if (refStars->empty()) {
        // emit logMessage(tr("Detecting stars in reference (uncached)..."), "");
        tempRefStars = detectStars(reference);
        refStars = &tempRefStars;
        
        // Cache them if we are the reference
        if (&reference != &image) {
            // Logic nuance: m_referenceStars should be set by registerSequence.
            // If registerImage is called standalone, we use the transient tempRefStars.
            // This is the intended behavior for standalone checks.
        }
    }
    
    // Match stars
    // (No emit here — may be called from parallel region)
    result.starsDetected = static_cast<int>(targetStars.size());
    int matched = matchStars(*refStars, targetStars, result.transform);
    result.starsMatched = matched;
    
    if (matched < m_params.minMatches) {
        result.error = tr("Not enough matches: %1").arg(matched);
        return result;
    }
    
    result.success = true;
    result.transform.hasRegistration = true;
    result.quality = static_cast<double>(matched) / targetStars.size();
    
    return result;
}

// Gaussian Blur Helper (Separable, OMP optimized)
static void applyGaussianBlur(const std::vector<float>& src, std::vector<float>& dst, 
                            int width, int height, float sigma) {
    dst.resize(src.size());
    std::vector<float> temp(src.size());
    
    // Kernel generation (approx 6*sigma wide = 3*sigma radius)
    int kRadius = std::ceil(3.0f * sigma);
    if (kRadius < 1) kRadius = 1;
    int kSize = 2 * kRadius + 1;
    std::vector<float> kernel(kSize);
    float sum = 0.0f;
    float sigma2 = 2.0f * sigma * sigma;
    for (int i = -kRadius; i <= kRadius; ++i) {
        float v = std::exp(-(i * i) / sigma2);
        kernel[i + kRadius] = v;
        sum += v;
    }
    for (float& v : kernel) v /= sum;
    
    // Horizontal Pass
    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float val = 0.0f;
            for (int k = -kRadius; k <= kRadius; ++k) {
                int px = std::clamp(x + k, 0, width - 1);
                val += src[y * width + px] * kernel[k + kRadius];
            }
            temp[y * width + x] = val;
        }
    }
    
    // Vertical Pass (iterate y-outer for better cache locality)
    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float val = 0.0f;
            for (int k = -kRadius; k <= kRadius; ++k) {
                int py = std::clamp(y + k, 0, height - 1);
                val += temp[py * width + x] * kernel[k + kRadius];
            }
            dst[y * width + x] = val;
        }
    }
}

// Robust Global Statistics (Median + MAD/Sigma)
static void computeGlobalStats(const std::vector<float>& data, float& median, float& sigma) {
    if (data.empty()) {
        median = 0; sigma = 0; return;
    }
    
    std::vector<float> sample;
    sample.reserve(data.size() / 100 + 1000);
    int step = 100;
    for(size_t i=0; i<data.size(); i+=step) {
        sample.push_back(data[i]);
    }
    
    if (sample.empty()) return;
    
    size_t n = sample.size();
    size_t mid = n / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    median = sample[mid];
    
    // Sigma via MAD
    std::vector<float> diffs;
    diffs.reserve(n);
    for(float v : sample) diffs.push_back(std::abs(v - median));
    
    std::nth_element(diffs.begin(), diffs.begin() + mid, diffs.end());
    float mad = diffs[mid];
    
    // Normal distribution approximation
    sigma = mad * 1.4826f;
}

struct BlockStats {
    float median;
    float sigma;    // MAD-based sigma
};

class BackgroundMesh {
public:
    int cols, rows;
    int meshSize;
    int width, height;
    std::vector<BlockStats> blocks;

    void compute(const std::vector<float>& data, int w, int h, int boxSize = 64) {
        width = w;
        height = h;
        meshSize = boxSize;
        cols = (w + boxSize - 1) / boxSize;
        rows = (h + boxSize - 1) / boxSize;
        blocks.resize(cols * rows);

        #pragma omp parallel
        {
            // Thread-local buffer to avoid reallocations
            std::vector<float> samples;
            samples.reserve(boxSize * boxSize);
            
            #pragma omp for
            for (int i = 0; i < cols * rows; ++i) {
                samples.clear();
                
                int by = i / cols;
                int bx = i % cols;
                
                int x0 = bx * boxSize;
                int y0 = by * boxSize;
                int x1 = std::min(x0 + boxSize, width);
                int y1 = std::min(y0 + boxSize, height);
                
                for (int y = y0; y < y1; ++y) {
                    const float* row = data.data() + y * width;
                    for (int x = x0; x < x1; ++x) {
                         // Accept all finite values
                         if (std::isfinite(row[x])) samples.push_back(row[x]);
                    }
                }
                
                if (samples.empty()) {
                    blocks[i] = {0.0f, 0.0f}; // Fallback
                    continue;
                }
                
                // Compute Stats (Hoare's Selection Algorithm - O(N))
                size_t n = samples.size();
                size_t mid = n / 2;
                std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
                float median = samples[mid];
                
                // MAD
                std::vector<float> diffs;
                diffs.reserve(n);
                for(float v : samples) diffs.push_back(std::abs(v - median));
                std::nth_element(diffs.begin(), diffs.begin() + mid, diffs.end());
                float mad = diffs[mid];
                
                blocks[i] = {median, mad * 1.4826f};
            }
        }
    }
    
    // Bilinear Interpolation for smooth background
    inline void getStats(int x, int y, float& bg, float& sigma) const {
        // Center coordinates of the blocks
        float fx = (float)x / meshSize - 0.5f;
        float fy = (float)y / meshSize - 0.5f;
        
        int x0 = (int)std::floor(fx);
        int y0 = (int)std::floor(fy);
        
        float wx = fx - x0;
        float wy = fy - y0;
        
        // Clamp indices
        int x1 = x0 + 1;
        int y1 = y0 + 1;
        
        x0 = std::clamp(x0, 0, cols - 1);
        y0 = std::clamp(y0, 0, rows - 1);
        x1 = std::clamp(x1, 0, cols - 1);
        y1 = std::clamp(y1, 0, rows - 1);
        
        const BlockStats& b00 = blocks[y0 * cols + x0];
        const BlockStats& b10 = blocks[y0 * cols + x1];
        const BlockStats& b01 = blocks[y1 * cols + x0];
        const BlockStats& b11 = blocks[y1 * cols + x1];
        
        // Interpolate Median (Background)
        float b_top = b00.median * (1.0f - wx) + b10.median * wx;
        float b_bot = b01.median * (1.0f - wx) + b11.median * wx;
        bg = b_top * (1.0f - wy) + b_bot * wy;
        
        // Interpolate Sigma (Noise)
        float s_top = b00.sigma * (1.0f - wx) + b10.sigma * wx;
        float s_bot = b01.sigma * (1.0f - wx) + b11.sigma * wx;
        sigma = s_top * (1.0f - wy) + s_bot * wy;
        
        if (sigma < 1e-9f) sigma = 1e-5f;
    }
};


std::vector<DetectedStar> RegistrationEngine::detectStars(const ImageBuffer& image) {
    std::vector<DetectedStar> stars;
    
    // 1. Extract Luminance
    // emit logMessage(tr("Extracting luminance..."), "");
    std::vector<float> lum = extractLuminance(image);
    int width = image.width();
    int height = image.height();
    
    if (m_cancelled) return stars;

    // 2. Compute Background Mesh (SEP-like)
    BackgroundMesh bgMesh;
    bgMesh.compute(lum, width, height, 64); // 64x64 blocks

    // 3. Gaussian Blur (for peak finding)
    std::vector<float> smoothLum;
    applyGaussianBlur(lum, smoothLum, width, height, 2.0f);
    
    // 4. Find Peaks in Smoothed Image
    
    // Helper to store candidates temporarily
    struct PeakCand {
        int x, y;
        float val;
    };
    
    // Thread-local storage
    #ifdef _OPENMP
    int maxThreads = omp_get_max_threads();
    #else
    int maxThreads = 1;
    #endif
    
    std::vector<std::vector<DetectedStar>> threadStars(maxThreads);
    
    int r = 2;
    float kSigma = m_params.detectionThreshold > 0.1f ? m_params.detectionThreshold : 3.0f; // Lower default for local thresholding

    #pragma omp parallel
    {
        #ifdef _OPENMP
        int tid = omp_get_thread_num();
        #else
        int tid = 0;
        #endif
        
        threadStars[tid].reserve(2000);
        
        #pragma omp for
        for (int y = r; y < height - r; ++y) {
            
            for (int x = r; x < width - r; ++x) {
                // Get local background statistics
                float bg, sigma;
                bgMesh.getStats(x, y, bg, sigma);
                
                float thresholdVal = bg + kSigma * sigma;
                float val = smoothLum[y * width + x];
                
                if (val <= thresholdVal) continue;
                
                // Local Maxima Check
                bool isMax = true;
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        if (smoothLum[(y + dy) * width + (x + dx)] > val) {
                            isMax = false;
                            break;
                        }
                    }
                    if (!isMax) break;
                }
                
                if (isMax) {
                    
                    // Use ORIGINAL (unsmoothed) luminance for subpixel refinement
                    // via weighted centroid (first moment) - much more accurate
                    float bg, localSigma;
                    bgMesh.getStats(x, y, bg, localSigma);
                    
                    const int refineR = 5;
                    // Boundary check for refinement radius
                    if (x < refineR || x >= width - refineR || y < refineR || y >= height - refineR) continue;
                    
                    double sumWX = 0, sumWY = 0, sumW = 0;
                    double sumWXX = 0, sumWYY = 0;
                    float peakVal = 0.0f;
                    
                    for (int ry = -refineR; ry <= refineR; ++ry) {
                        for (int rx = -refineR; rx <= refineR; ++rx) {
                            float v = lum[(y + ry) * width + (x + rx)] - bg;
                            if (v <= 0.0f) continue;
                            sumWX += rx * v;
                            sumWY += ry * v;
                            sumW += v;
                            sumWXX += rx * rx * v;
                            sumWYY += ry * ry * v;
                            if (v > peakVal) peakVal = v;
                        }
                    }
                    
                    if (sumW <= 0 || peakVal <= 0) continue;
                    
                    float dx = static_cast<float>(sumWX / sumW);
                    float dy = static_cast<float>(sumWY / sumW);
                    
                    // Reject if centroid is too far from peak
                    if (std::abs(dx) > 2.0f || std::abs(dy) > 2.0f) continue;
                    
                    // Compute FWHM from second moment (variance)
                    double varX = sumWXX / sumW - dx * dx;
                    double varY = sumWYY / sumW - dy * dy;
                    if (varX < 0) varX = 0;
                    if (varY < 0) varY = 0;
                    
                    float sigmaX = std::sqrt(static_cast<float>(varX));
                    float sigmaY = std::sqrt(static_cast<float>(varY));
                    
                    // Roundness = minor/major axis ratio
                    float roundness = 0.0f;
                    if (sigmaX > 1e-6f && sigmaY > 1e-6f) {
                        roundness = std::min(sigmaX, sigmaY) / std::max(sigmaX, sigmaY);
                    }
                    
                    if (roundness < m_params.minRoundness) continue;

                    DetectedStar s;
                    s.x = x + dx;
                    s.y = y + dy;
                    s.peak = peakVal;
                    s.flux = static_cast<float>(sumW); // Total flux above background
                    s.fwhm = 2.355f * (sigmaX + sigmaY) / 2.0f; // Gaussian FWHM
                    s.roundness = roundness;
                    s.snr = peakVal / localSigma;
                    
                    // Quality Check
                    if (s.fwhm < m_params.minFWHM || s.fwhm > m_params.maxFWHM) continue;
                    
                    threadStars[tid].push_back(s);
                }
            }
        }
    }
    
    // 5. Merge and Sort
    for(const auto& t : threadStars) {
        stars.insert(stars.end(), t.begin(), t.end());
    }
    
    // Sort by total flux (proper brightness measure)
    std::sort(stars.begin(), stars.end(), [](const DetectedStar& a, const DetectedStar& b){
        return a.flux > b.flux;
    });

    
    // 6. Limit
    int mMax = (m_params.maxStars > 0) ? m_params.maxStars : 2000;
    if (stars.size() > (size_t)mMax) {
        stars.resize(mMax);
    }
    
    if (stars.empty()) {
        // emit logMessage(tr("No stars detected"), "red");
    } else {
        // emit logMessage(tr("Detected %1 stars").arg(stars.size()), "green");
    }
    
    return stars;
}


std::vector<float> RegistrationEngine::extractLuminance(const ImageBuffer& image) {
    int width = image.width();
    int height = image.height();
    int channels = image.channels();
    
    std::vector<float> lum(width * height);
    const float* data = image.data().data();
    
    if (channels == 1) {
        std::copy(data, data + width * height, lum.begin());
    } else {
        // Rec.709 luminance
        #pragma omp parallel for
        for (int i = 0; i < width * height; ++i) {
            lum[i] = 0.2126f * data[i * channels] +
                     0.7152f * data[i * channels + 1] +
                     0.0722f * data[i * channels + 2];
        }
    }
    
    return lum;
}

// Replaced by BackgroundMesh logic
// Replaced by Global Stats logic
void RegistrationEngine::computeBackground(const std::vector<float>& data,
                                           int width, int height,
                                           float& background, float& rms) {
   // Use global statistics
   Q_UNUSED(width); Q_UNUSED(height);
   computeGlobalStats(data, background, rms);
}

// Unused legacy helper
std::vector<std::pair<int, int>> RegistrationEngine::findLocalMaxima(
    const std::vector<float>& data, int width, int height, float threshold) {
    Q_UNUSED(data); Q_UNUSED(width); Q_UNUSED(height); Q_UNUSED(threshold);
    return {};
}

bool RegistrationEngine::refineStarPosition(const std::vector<float>& data,
                                            int width, int height,
                                            int cx, int cy,
                                            DetectedStar& star) {
    const int radius = 5;
    
    // Get local stats from immediate area for background subtraction
    float minVal = std::numeric_limits<float>::max();
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
             int px = std::clamp(cx+dx, 0, width-1);
             int py = std::clamp(cy+dy, 0, height-1);
             float v = data[py*width+px];
             if(v < minVal) minVal = v;
        }
    }
    float background = minVal;
    
    // Compute weighted centroid (First moment)
    double sumX = 0, sumY = 0, sumW = 0;
    double sumPeak = 0;
    
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = cx + dx;
            int py = cy + dy;
            
            if (px < 0 || px >= width || py < 0 || py >= height) continue;
            
            float val = data[py * width + px] - background;
            if (val <= 0) continue;
            
            sumX += dx * val;
            sumY += dy * val;
            sumW += val;
            
            if (val > sumPeak) sumPeak = val;
        }
    }
    
    if (sumW <= 0) return false;
    
    float dx = sumX / sumW;
    float dy = sumY / sumW;
    
    // Check iteratively if centroid is too far (false peak)
    if (std::abs(dx) > 2.0 || std::abs(dy) > 2.0) return false;

    star.x = cx + dx;
    star.y = cy + dy;
    star.peak = sumPeak;
    star.flux = sumW;
    
    // Compute FWHM (Second moment)
    // Var(x) = E[x^2] - E[x]^2
    double sumXX = 0, sumYY = 0;
    
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = cx + dx;
            int py = cy + dy;
            
            if (px < 0 || px >= width || py < 0 || py >= height) continue;
            
            float val = data[py * width + px] - background;
            if (val <= 0) continue;
            
            // Re-center on centroid
            double rx = dx - star.x + cx; // (cx+dx) - star.x
            double ry = dy - star.y + cy;
            
            sumXX += rx * rx * val;
            sumYY += ry * ry * val;
        }
    }
    
    double sigmaX2 = sumXX / sumW;
    double sigmaY2 = sumYY / sumW;
    
    if(sigmaX2 < 0) sigmaX2 = 0;
    if(sigmaY2 < 0) sigmaY2 = 0;
    
    float sigmaX = std::sqrt(sigmaX2);
    float sigmaY = std::sqrt(sigmaY2);
    
    // Gaussian FWHM = 2.355 * sigma
    star.fwhm = 2.355f * (sigmaX + sigmaY) / 2.0f;
    
    // Roundness
    if (sigmaX > 1e-6 && sigmaY > 1e-6) {
        star.roundness = std::min(sigmaX, sigmaY) / std::max(sigmaX, sigmaY);
    } else {
        star.roundness = 0.0f;
    }

    return true;
}

//=============================================================================
// STAR MATCHING via TRIANGLE MATCHER
//=============================================================================

bool RegistrationEngine::convertToMatchStars(const std::vector<DetectedStar>& src, 
                                             std::vector<MatchStar>& dst) {
    dst.clear();
    dst.reserve(src.size());
    
    for (size_t i = 0; i < src.size(); ++i) {
        MatchStar s;
        s.id = static_cast<int>(i); // Keep track of original index
        s.index = static_cast<int>(i);
        s.x = src[i].x;
        s.y = src[i].y;
        
        // TriangleMatcher sorts by mag ascending (smaller is brighter).
        // DetectedStar has flux (larger is brighter).
        // Convert flux to instrumental magnitude: m = -2.5 * log10(flux)
        // Or simply negate flux as a proxy for sorting.
        // Let's use -flux to avoid log cost, as order is preserved (descending flux -> ascending -flux)
        s.mag = -src[i].flux; 
        
        s.match_id = -1;
        dst.push_back(s);
    }
    return true;
}

int RegistrationEngine::matchStars(const std::vector<DetectedStar>& refStars,
                                   const std::vector<DetectedStar>& targetStars,
                                   RegistrationData& regData) {
    if (refStars.size() < 5 || targetStars.size() < 5)
        return 0;

    // === Phase 1: Triangle matching (AT_MATCH_NBRIGHT=20) → initial rough affine ===
    TriangleMatcher matcher;
    matcher.setMaxStars(20);  // AT_MATCH_NBRIGHT = 20

    std::vector<MatchStar> mRef, mTarget;
    convertToMatchStars(refStars, mRef);
    convertToMatchStars(targetStars, mTarget);

    GenericTrans gTrans;
    // solve(imgStars=target, catStars=ref) → transform target→ref
    if (!matcher.solve(mTarget, mRef, gTrans))
        return 0;

    // Affine: ref = A * [1, target_x, target_y]^T
    // A[0] = {x00, x10, x01}  →  ref_x = x00 + x10*tx + x01*ty
    // A[1] = {y00, y10, y01}  →  ref_y = y00 + y10*tx + y01*ty
    double A[2][3] = {
        {gTrans.x00, gTrans.x10, gTrans.x01},
        {gTrans.y00, gTrans.y10, gTrans.y01}
    };

    const int    nRef    = (int)mRef.size();
    const int    nTarget = (int)mTarget.size();
    const double kRadius2   = 5.0 * 5.0;  // AT_MATCH_RADIUS = 5.0 px
    const int    kMaxIter   = 5;           // AT_MATCH_MAXITER
    const double kHaltSig   = 0.1;        // AT_MATCH_HALTSIGMA
    const double kNSig      = 10.0;       // AT_MATCH_NSIGMA
    const double kPctile    = 0.35;       // AT_MATCH_PERCENTILE

    // Inline 3x3 determinant (via row pointers, avoids array-parameter lambda issues)
    auto det3x3 = [](const double* r0, const double* r1, const double* r2) -> double {
        return r0[0]*(r1[1]*r2[2]-r2[1]*r1[2])
              -r0[1]*(r1[0]*r2[2]-r1[2]*r2[0])
              +r0[2]*(r1[0]*r2[1]-r1[1]*r2[0]);
    };

    // Refit affine A[][] from matched pairs using iterative sigma clipping (atRecalcTrans).
    // Does NOT modify the pairs vector — just updates A[][].
    // Returns number of inliers.
    auto recalcAffine = [&](const std::vector<std::pair<int,int>>& allPairs) -> int {
        int n = (int)allPairs.size();
        if (n < 6) return 0;
        std::vector<bool> active(n, true);
        for (int iter = 0; iter < kMaxIter; ++iter) {
            int nAct = 0;
            for (bool b : active) if (b) ++nAct;
            if (nAct < 6) return 0;

            // Build normal equations: A*x = b (for each row of affine separately)
            double sumx=0,sumy=0,sumx2=0,sumy2=0,sumxy=0;
            double sumxp=0,sumxpx=0,sumxpy=0,sumyp=0,sumypx=0,sumypy=0;
            for (int i = 0; i < n; ++i) {
                if (!active[i]) continue;
                double sx = mTarget[allPairs[i].first].x;
                double sy = mTarget[allPairs[i].first].y;
                double rx = mRef[allPairs[i].second].x;
                double ry = mRef[allPairs[i].second].y;
                sumx+=sx; sumy+=sy; sumx2+=sx*sx; sumy2+=sy*sy; sumxy+=sx*sy;
                sumxp+=rx; sumxpx+=rx*sx; sumxpy+=rx*sy;
                sumyp+=ry; sumypx+=ry*sx; sumypy+=ry*sy;
            }
            double Nd = (double)nAct;
            double M[3][3] = {{Nd,sumx,sumy},{sumx,sumx2,sumxy},{sumy,sumxy,sumy2}};
            double det = det3x3(M[0], M[1], M[2]);
            if (std::abs(det) < 1e-9) return 0;
            // Solve for x params via Cramer's rule
            {
                double M0[3][3]={{sumxp, M[0][1],M[0][2]},{sumxpx,M[1][1],M[1][2]},{sumxpy,M[2][1],M[2][2]}};
                double M1[3][3]={{M[0][0],sumxp, M[0][2]},{M[1][0],sumxpx,M[1][2]},{M[2][0],sumxpy,M[2][2]}};
                double M2[3][3]={{M[0][0],M[0][1],sumxp },{M[1][0],M[1][1],sumxpx},{M[2][0],M[2][1],sumxpy}};
                A[0][0]=det3x3(M0[0],M0[1],M0[2])/det; A[0][1]=det3x3(M1[0],M1[1],M1[2])/det; A[0][2]=det3x3(M2[0],M2[1],M2[2])/det;
            }
            // Solve for y params via Cramer's rule
            {
                double M0[3][3]={{sumyp, M[0][1],M[0][2]},{sumypx,M[1][1],M[1][2]},{sumypy,M[2][1],M[2][2]}};
                double M1[3][3]={{M[0][0],sumyp, M[0][2]},{M[1][0],sumypx,M[1][2]},{M[2][0],sumypy,M[2][2]}};
                double M2[3][3]={{M[0][0],M[0][1],sumyp },{M[1][0],M[1][1],sumypx},{M[2][0],M[2][1],sumypy}};
                A[1][0]=det3x3(M0[0],M0[1],M0[2])/det; A[1][1]=det3x3(M1[0],M1[1],M1[2])/det; A[1][2]=det3x3(M2[0],M2[1],M2[2])/det;
            }
            // Compute residuals and estimate sigma from PERCENTILE
            std::vector<double> res2(n, 1e18);
            for (int i = 0; i < n; ++i) {
                if (!active[i]) continue;
                double sx = mTarget[allPairs[i].first].x;
                double sy = mTarget[allPairs[i].first].y;
                double rx = mRef[allPairs[i].second].x;
                double ry = mRef[allPairs[i].second].y;
                double px = A[0][0]+A[0][1]*sx+A[0][2]*sy;
                double py = A[1][0]+A[1][1]*sx+A[1][2]*sy;
                res2[i] = (px-rx)*(px-rx)+(py-ry)*(py-ry);
            }
            std::vector<double> sortedRes;
            sortedRes.reserve(nAct);
            for (int i = 0; i < n; ++i) if (active[i]) sortedRes.push_back(res2[i]);
            std::sort(sortedRes.begin(), sortedRes.end());
            double sigma2 = sortedRes[std::max(0, (int)(sortedRes.size() * kPctile))];
            if (sigma2 < kHaltSig * kHaltSig) break;  // Converged
            double threshold = kNSig * sigma2;
            int removed = 0;
            for (int i = 0; i < n; ++i) {
                if (active[i] && res2[i] > threshold) { active[i] = false; ++removed; }
            }
            if (removed == 0) break;
        }
        int inliers = 0;
        for (bool b : active) if (b) ++inliers;
        return inliers;
    };

    // === Phase 2+3: 2 rounds of proximity matching + affine recalculation (atMatchLists + atRecalcTrans) ===
    // Sort ref stars by X for O(n log n) proximity search instead of O(n²).
    std::vector<int> refByX(nRef);
    std::iota(refByX.begin(), refByX.end(), 0);
    std::sort(refByX.begin(), refByX.end(), [&](int a, int b){ return mRef[a].x < mRef[b].x; });
    std::vector<double> refXSorted(nRef);
    for (int j = 0; j < nRef; ++j) refXSorted[j] = mRef[refByX[j]].x;

    std::vector<std::pair<int,int>> matchedPairs;
    const double kRadius = std::sqrt(kRadius2);  // 5.0 px
    for (int round = 0; round < 2; ++round) {
        matchedPairs.clear();
        std::vector<bool> refUsed(nRef, false);
        for (int i = 0; i < nTarget; ++i) {
            // Apply current affine to this target star
            double tx = A[0][0] + A[0][1]*mTarget[i].x + A[0][2]*mTarget[i].y;
            double ty = A[1][0] + A[1][1]*mTarget[i].x + A[1][2]*mTarget[i].y;
            // Binary search in sorted ref-X to find candidates within 5px in X
            int lo = (int)(std::lower_bound(refXSorted.begin(), refXSorted.end(), tx - kRadius) - refXSorted.begin());
            int hi = (int)(std::upper_bound(refXSorted.begin(), refXSorted.end(), tx + kRadius) - refXSorted.begin());
            double minD2 = kRadius2;
            int bestJ = -1;
            for (int ki = lo; ki < hi; ++ki) {
                int j = refByX[ki];
                if (refUsed[j]) continue;
                double dy = ty - mRef[j].y;
                if (dy * dy >= kRadius2) continue;  // fast Y reject
                double dx = tx - mRef[j].x;
                double d2 = dx*dx + dy*dy;
                if (d2 < minD2) { minD2 = d2; bestJ = j; }
            }
            if (bestJ >= 0) {
                matchedPairs.push_back({i, bestJ});
                refUsed[bestJ] = true;
            }
        }
        if ((int)matchedPairs.size() < 6) break;
        recalcAffine(matchedPairs);  // Refit A[][] from all matched pairs
    }

    int nMatched = (int)matchedPairs.size();
    if (nMatched < 6) return 0;

    // === Phase 4: OpenCV RANSAC final affine fit (cvCalculH → estimateAffine2D, threshold=3px) ===
    std::vector<cv::Point2f> srcPts, dstPts;
    srcPts.reserve(nMatched); dstPts.reserve(nMatched);
    for (auto& [ti, ri] : matchedPairs) {
        srcPts.emplace_back((float)mTarget[ti].x, (float)mTarget[ti].y);
        dstPts.emplace_back((float)mRef[ri].x,    (float)mRef[ri].y);
    }
    cv::Mat affM = cv::estimateAffine2D(srcPts, dstPts, cv::noArray(), cv::RANSAC, 3.0);
    if (affM.empty() || cv::countNonZero(affM) < 1)
        return 0;

    // Build 3×3 homography from 2×3 affine matrix
    regData.hasRegistration = true;
    regData.H[0][0] = affM.at<double>(0,0); regData.H[0][1] = affM.at<double>(0,1); regData.H[0][2] = affM.at<double>(0,2);
    regData.H[1][0] = affM.at<double>(1,0); regData.H[1][1] = affM.at<double>(1,1); regData.H[1][2] = affM.at<double>(1,2);
    regData.H[2][0] = 0.0;                  regData.H[2][1] = 0.0;                  regData.H[2][2] = 1.0;

    regData.shiftX   = affM.at<double>(0,2);
    regData.shiftY   = affM.at<double>(1,2);
    regData.scaleX   = std::sqrt(regData.H[0][0]*regData.H[0][0] + regData.H[1][0]*regData.H[1][0]);
    regData.scaleY   = std::sqrt(regData.H[0][1]*regData.H[0][1] + regData.H[1][1]*regData.H[1][1]);
    regData.rotation = std::atan2(regData.H[1][0], regData.H[0][0]);

    return nMatched;
}

//=============================================================================
// WORKER THREAD
//=============================================================================

RegistrationWorker::RegistrationWorker(ImageSequence* sequence,
                                       const RegistrationParams& params,
                                       int referenceIndex,
                                       QObject* parent)
    : QThread(parent)
    , m_sequence(sequence)
    , m_params(params)
    , m_referenceIndex(referenceIndex)
{
    m_engine.setParams(params);
    
    connect(&m_engine, &RegistrationEngine::progressChanged,
            this, &RegistrationWorker::progressChanged);
    connect(&m_engine, &RegistrationEngine::logMessage,
            this, &RegistrationWorker::logMessage);
    connect(&m_engine, &RegistrationEngine::imageRegistered,
            this, &RegistrationWorker::imageRegistered);
}

void RegistrationWorker::run() {
    int count = m_engine.registerSequence(*m_sequence, m_referenceIndex);
    emit finished(count);
}

} // namespace Stacking
