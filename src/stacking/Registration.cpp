#include "Registration.h"

#include "../astrometry/TriangleMatcher.h"
#include "Statistics.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <iterator>
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
    
    for (int i = 0; i < totalImages; ++i) {
        if (m_cancelled || !Threading::getThreadRun()) {
            break;
        }
        if (i == referenceIndex) {
            // Reference is already aligned
            auto& img = sequence.image(i);
            img.registration.hasRegistration = true;
            img.registration.shiftX = 0;
            img.registration.shiftY = 0;
            img.registration.rotation = 0;
            img.registration.scaleX = 1.0;
            img.registration.scaleY = 1.0;
            successCount++;
            
            // Save reference image as r_...
            QString inPath = sequence.image(i).filePath;
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

            emit imageRegistered(i, true);
            continue;
        }
        
        emit progressChanged(tr("Registering image %1/%2").arg(i + 1).arg(totalImages),
                            static_cast<double>(i) / totalImages);
        
        ImageBuffer imgBuffer;
        if (!sequence.readImage(i, imgBuffer)) {
            emit logMessage(tr("Failed to load image %1").arg(i + 1), "salmon");
            emit imageRegistered(i, false);
            continue;
        }
        
        RegistrationResult result = registerImage(imgBuffer, refBuffer);
        
        auto& seqImg = sequence.image(i);
        if (result.success) {
            seqImg.registration = result.transform;
            successCount++;
            emit logMessage(tr("Image %1: shift=(%2, %3), rot=%4 deg, %5 matched")
                           .arg(i + 1)
                            .arg(result.transform.shiftX, 0, 'f', 1)
                            .arg(result.transform.shiftY, 0, 'f', 1)
                            .arg(result.transform.rotation * 180.0 / M_PI, 0, 'f', 2)
                            .arg(result.starsMatched), "");

            // WARP AND SAVE
            cv::Mat H(3, 3, CV_64F);
            for(int r=0; r<3; r++)
                for(int c=0; c<3; c++)
                    H.at<double>(r, c) = result.transform.H[r][c];

            int w = imgBuffer.width();
            int h = imgBuffer.height();
            int ch = imgBuffer.channels();
            ImageBuffer warped(w, h, ch);
            
            // Warp interleaved data (OpenCV handles multi-channel CV_32F mats)
            cv::Mat src(h, w, CV_32FC(ch), (void*)imgBuffer.data().data());
            cv::Mat dst(h, w, CV_32FC(ch), (void*)warped.data().data());
            
            cv::warpPerspective(src, dst, H, dst.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
            
            // Masking Pass: Reject interpolation ramp
            cv::Mat mask(h, w, CV_32FC1, cv::Scalar(1.0f));
            cv::Mat dstMask(h, w, CV_32FC1);
            cv::warpPerspective(mask, dstMask, H, dst.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0.0f));
            
            // Apply Mask
            float* dstData = (float*)dst.data;
            float* mData = (float*)dstMask.data;
            size_t totalPixels = (size_t)w * h;
            
            // Check bounds strictly. If mask < 0.999, it means the pixel was touched by the void.
            // We set it to 0.0f (STACKING_NO_DATA) so rejection excludes it.
            #pragma omp parallel for
            for (int p = 0; p < (int)totalPixels; ++p) {
                if (mData[p] < 0.999f) {
                    for (int c = 0; c < ch; ++c) {
                        dstData[p * ch + c] = 0.0f;
                    }
                }
            }

            QString inPath = sequence.image(i).filePath;
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
            
            FitsIO::write(outPath, warped);
            emit logMessage(tr("Saved: %1").arg(outName), "");
        } else {
            seqImg.registration.hasRegistration = false;
            emit logMessage(tr("Image %1: registration failed - %2")
                           .arg(i + 1).arg(result.error), "salmon");
        }
        
        emit imageRegistered(i, result.success);
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
            // Logic nuance: m_referenceStars should be set by registerSequence
            // If registerImage is called standalone, we might want to cache?
            // For now just use temp.
        }
    }
    
    // Match stars
    emit logMessage(tr("Matching stars (%1 vs %2)...").arg(refStars->size()).arg(targetStars.size()), "");
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
    
    // Kernel generation (approx 4*sigma)
    int kRadius = std::ceil(2.0f * sigma);
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
    
    // Vertical Pass
    #pragma omp parallel for
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
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
                    
                    // d1 = first derivatives
                    float vm10 = smoothLum[y * width + (x - 1)];
                    float vp10 = smoothLum[y * width + (x + 1)];
                    float v0m1 = smoothLum[(y - 1) * width + x];
                    float v0p1 = smoothLum[(y + 1) * width + x];
                    
                    float d1x_l = val - vm10;
                    float d1y_u = val - v0m1;
                    
                    float denomX = vp10 + vm10 - 2.0f * val;
                    float denomY = v0p1 + v0m1 - 2.0f * val;
                    
                    float dx = 0.0f;
                    float dy = 0.0f;
                    
                    // Only refine if valid peak (curvature is negative)
                    if (std::abs(denomX) > 1e-5f) {
                         dx = -0.5f - (d1x_l / denomX); 
                    }
                    
                    if (std::abs(denomY) > 1e-5f) {
                        dy = -0.5f - (d1y_u / denomY);
                    }
                    
                    // Clamp offsets
                    if (std::abs(dx) > 1.0f || std::abs(dy) > 1.0f) continue;
                    
                    DetectedStar s;
                    s.x = x + dx;
                    s.y = y + dy;
                    s.peak = val; // Smoothed peak
                    s.flux = val - bg; // Local flux above background
                    
                    if (denomX < 0) {
                        float sx = std::sqrt(val / -denomX);
                        s.fwhm = 2.355f * sx;
                    } else s.fwhm = 2.0f;
                    
                    s.roundness = 1.0f; // Dummy
                    
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
    
    // Sort by Flux (Peak in this case, close enough)
    std::sort(stars.begin(), stars.end(), [](const DetectedStar& a, const DetectedStar& b){
        return a.peak > b.peak;
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

int RegistrationEngine::matchStars(const std::vector<DetectedStar>& stars1,
                                   const std::vector<DetectedStar>& stars2,
                                   RegistrationData& transform) {
    
    if (stars1.size() < 5 || stars2.size() < 5) {
        return 0; // Not enough stars to solve
    }

    TriangleMatcher matcher;
    // Limit to 50 stars for robust pattern matching without cubic explosion
    // This solves the hang when using 100+ stars.
    matcher.setMaxStars(std::min(m_params.maxStars > 0 ? m_params.maxStars : 40, 50));
    
    std::vector<MatchStar> mStars1, mStars2;
    convertToMatchStars(stars1, mStars1);
    convertToMatchStars(stars2, mStars2);
    
    GenericTrans gTrans;
    // solve(image, reference) -> aligns image to reference
    bool ok = matcher.solve(mStars2, mStars1, gTrans);
    
    if (ok) {
        transform.hasRegistration = true;
        
        // Map Affine Transform to Homography Matrix (Target -> Reference)
        transform.H[0][0] = gTrans.x10; transform.H[0][1] = gTrans.x01; transform.H[0][2] = gTrans.x00;
        transform.H[1][0] = gTrans.y10; transform.H[1][1] = gTrans.y01; transform.H[1][2] = gTrans.y00;
        transform.H[2][0] = 0.0;        transform.H[2][1] = 0.0;        transform.H[2][2] = 1.0;
        
        transform.shiftX = gTrans.x00;
        transform.shiftY = gTrans.y00;
        
        // Extract basic parameters
        transform.scaleX = std::sqrt(gTrans.x10 * gTrans.x10 + gTrans.y10 * gTrans.y10);
        transform.scaleY = std::sqrt(gTrans.x01 * gTrans.x01 + gTrans.y01 * gTrans.y01);
        transform.rotation = std::atan2(gTrans.y10, gTrans.x10);
        
        return gTrans.nr;
    }
    
    return 0;
    

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
