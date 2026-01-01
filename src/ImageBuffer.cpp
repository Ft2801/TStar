#include "ImageBuffer.h"
#include "io/SimpleTiffWriter.h"
#include "io/SimpleTiffReader.h"
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <omp.h>
#include <QDebug>
#include "core/RobustStatistics.h"
#include <QBuffer>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDataStream> 
#include <stack>
#include <cmath> 
#include "io/XISFReader.h" 
#include "io/XISFWriter.h" 
#include <opencv2/opencv.hpp>

ImageBuffer::ImageBuffer() {}
ImageBuffer::~ImageBuffer() {}

// ...
void ImageBuffer::setMask(const MaskLayer& mask) {
    if (mask.isValid() && mask.width == m_width && mask.height == m_height) {
        m_mask = mask;
        m_hasMask = true;
    }
}

const MaskLayer* ImageBuffer::getMask() const {
    return m_hasMask ? &m_mask : nullptr;
}

bool ImageBuffer::hasMask() const {
    return m_hasMask && m_mask.isValid();
}

void ImageBuffer::removeMask() {
    m_hasMask = false;
    m_mask = MaskLayer();
}

void ImageBuffer::invertMask() {
    if (hasMask() && !m_mask.data.empty()) {
        #pragma omp parallel for
        for (long long i = 0; i < (long long)m_mask.data.size(); ++i) {
            m_mask.data[i] = 1.0f - m_mask.data[i];
        }
        m_mask.inverted = false;
    }
}

void ImageBuffer::setData(int width, int height, int channels, const std::vector<float>& data) {
    m_width = width;
    m_height = height;
    m_channels = channels;
    m_data = data;
    if (m_data.empty() && width > 0 && height > 0 && channels > 0) {
        m_data.resize(static_cast<size_t>(width) * height * channels, 0.0f);
    }
}

void ImageBuffer::resize(int width, int height, int channels) {
    m_width = width;
    m_height = height;
    m_channels = channels;
    m_data.assign(static_cast<size_t>(width) * height * channels, 0.0f);
}

void ImageBuffer::applyWhiteBalance(float r, float g, float b) {
    if (m_channels < 3) return; // Only for color images

    ImageBuffer original;
    if (hasMask()) original = *this;

    long totalPixels = static_cast<long>(m_width) * m_height;
    
    #pragma omp parallel for
    for (long i = 0; i < totalPixels; ++i) {
        size_t idx = i * m_channels;
        m_data[idx] *= r;
        m_data[idx + 1] *= g;
        m_data[idx + 2] *= b;
    }

    if (hasMask()) {
        blendResult(original);
    }
}

bool ImageBuffer::loadStandard(const QString& filePath) {
    QImage img(filePath);
    if (img.isNull()) return false;

    img = img.convertToFormat(QImage::Format_RGB888);
    
    m_width = img.width();
    m_height = img.height();
    m_channels = 3;
    m_data.resize(m_width * m_height * 3);

    for (int y = 0; y < m_height; ++y) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < m_width; ++x) {
            m_data[(y * m_width + x) * 3 + 0] = static_cast<float>(line[x*3+0]) / 255.0f;
            m_data[(y * m_width + x) * 3 + 1] = static_cast<float>(line[x*3+1]) / 255.0f;
            m_data[(y * m_width + x) * 3 + 2] = static_cast<float>(line[x*3+2]) / 255.0f;
        }
    }
    return true;
}

bool ImageBuffer::loadTiff32(const QString& filePath, QString* errorMsg, QString* debugInfo) {
    // Use OpenCV for fast and reliable TIFF loading (uses libtiff internally)
    std::string stdPath = filePath.toStdString();
    
    // IMREAD_UNCHANGED preserves bit depth and channel count
    cv::Mat img = cv::imread(stdPath, cv::IMREAD_UNCHANGED);
    
    if (img.empty()) {
        // Fallback to SimpleTiffReader (handles 32-bit unsigned properly)
        int w, h, c;
        std::vector<float> data;
        if (SimpleTiffReader::readFloat32(filePath, w, h, c, data, errorMsg, debugInfo)) {
            setData(w, h, c, data);
            return true;
        }
        return false;
    }
    
    int w = img.cols;
    int h = img.rows;
    int ch = img.channels();
    
    // Force to 3 channels if grayscale (for ImageBuffer compatibility)
    if (ch == 1) {
        cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        ch = 3;
    } else if (ch == 4) {
        // Drop alpha channel
        cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);
        ch = 3;
    }
    
    // Convert to float32 and normalize to [0,1]
    cv::Mat floatMat;
    double scale = 1.0;
    
    switch (img.depth()) {
        case CV_8U:  scale = 1.0 / 255.0; break;
        case CV_16U: scale = 1.0 / 65535.0; break;
        case CV_32S: 
            // 32-bit signed - but TIFF might actually be unsigned, try SimpleTiffReader
            {
                int tw, th, tc;
                std::vector<float> tdata;
                if (SimpleTiffReader::readFloat32(filePath, tw, th, tc, tdata, errorMsg, debugInfo)) {
                    setData(tw, th, tc, tdata);
                    return true;
                }
            }
            scale = 1.0 / 2147483647.0; 
            break;
        case CV_32F: scale = 1.0; break; // Already float
        case CV_64F: scale = 1.0; break; // Will be converted
        default:
            if (errorMsg) *errorMsg = QObject::tr("Unsupported TIFF bit depth.");
            return false;
    }
    
    img.convertTo(floatMat, CV_32FC(ch), scale);
    
    // Copy to our data structure (BGR -> RGB and row-major interleaved)
    std::vector<float> data(w * h * ch);
    
    for (int y = 0; y < h; ++y) {
        const float* row = floatMat.ptr<float>(y);
        for (int x = 0; x < w; ++x) {
            int srcIdx = x * ch;
            int dstIdx = (y * w + x) * ch;
            // BGR to RGB swap
            data[dstIdx + 0] = row[srcIdx + 2]; // R
            data[dstIdx + 1] = row[srcIdx + 1]; // G
            data[dstIdx + 2] = row[srcIdx + 0]; // B
        }
    }
    
    setData(w, h, ch, data);
    
    if (debugInfo) {
        *debugInfo = QString("Loaded via OpenCV: %1x%2, %3ch, depth=%4").arg(w).arg(h).arg(ch).arg(img.depth());
    }
    
    return true;
}

bool ImageBuffer::loadXISF(const QString& filePath, QString* errorMsg) {
    return XISFReader::read(filePath, *this, errorMsg);
}

bool ImageBuffer::saveXISF(const QString& filePath, BitDepth depth, QString* errorMsg) {
    return XISFWriter::write(filePath, *this, static_cast<int>(depth), errorMsg);
}

// ------ Advanced Display Logic ------

// Constants for LUT
static const int LUT_SIZE = 65536;

// Statistics Helper
struct ChStats { float median; float mad; };

// MTF helper: y = (m-1)x / ((2m-1)x - m)
static float mtf_func(float m, float x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    if (m <= 0) return 0;
    if (m >= 1) return x;
    return ((m - 1.0f) * x) / ((2.0f * m - 1.0f) * x - m);
}

// Histogram-based Stats (O(N) instead of O(N log N))
// Used for AutoStretch speed optimization (industry parity)
static ChStats computeStats(const std::vector<float>& data, int width, int height, int channels, int channelIndex) {
    const int HIST_SIZE = 65536;
    const float MAD_NORM = 1.4826f; // Standard Normalization Factor for MAD
    std::vector<int> hist(HIST_SIZE, 0);
    
    long totalPixels = static_cast<long>(width) * height;
    if (totalPixels == 0) return {0.0f, 0.0f};
    
    // Subsampling strategy
    int step = 1;
    if (totalPixels > 4000000) { // > 4MP
        step = static_cast<int>(std::sqrt(static_cast<double>(totalPixels) / 4000000.0));
        if (step < 1) step = 1;
    }

    long count = 0;
    
    // 1. Build Histogram
    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
             size_t idx = (static_cast<size_t>(y) * width + x) * channels + channelIndex;
             if (idx < data.size()) {
                 float v = data[idx];
                 v = std::max(0.0f, std::min(1.0f, v));
                 int iVal = static_cast<int>(v * (HIST_SIZE - 1) + 0.5f);
                 hist[iVal]++;
                 count++;
             }
        }
    }
    
    if (count == 0) return {0.0f, 0.0f};

    // 2. Find Median
    long medianIdx = -1;
    long currentSum = 0;
    long medianLevel = count / 2;
    
    for (int i = 0; i < HIST_SIZE; ++i) {
        currentSum += hist[i];
        if (currentSum >= medianLevel) {
            medianIdx = i;
            break;
        }
    }
    
    float median = (float)medianIdx / (HIST_SIZE - 1);
    
    // 3. Find MAD (Median Absolute Deviation)
    std::vector<int> madHist(HIST_SIZE, 0);
    for (int i = 0; i < HIST_SIZE; ++i) {
        if (hist[i] > 0) {
            int dev = std::abs(i - (int)medianIdx);
            madHist[dev] += hist[i];
        }
    }
    
    // Find Median of MAD Hist
    currentSum = 0;
    long madIdx = -1;
    for (int i = 0; i < HIST_SIZE; ++i) {
        currentSum += madHist[i];
        if (currentSum >= medianLevel) {
            madIdx = i;
            break;
        }
    }
    
    float rawMad = (float)madIdx / (HIST_SIZE - 1);
    float mad = rawMad * MAD_NORM; // Apply normalization for Gaussian consistency
    
    return {median, mad};
}

// ====== Standard MTF Function ======
// Added safety guards for edge cases
static float standardMTF(float x, float m, float lo, float hi) {
    if (x <= lo) return 0.f;
    if (x >= hi) return 1.f;
    if (hi <= lo) return 0.5f; // Safety: avoid division by zero
    
    float xp = (x - lo) / (hi - lo);
    
    // Safety: handle m = 0.5 case (causes 2m-1 = 0)
    float denom = ((2.f * m - 1.f) * xp) - m;
    if (std::fabs(denom) < 1e-9f) return 0.5f;
    
    float result = ((m - 1.f) * xp) / denom;
    
    // Safety: clamp result to valid range
    if (std::isnan(result) || std::isinf(result)) return 0.5f;
    return std::clamp(result, 0.f, 1.f);
}

// Standard mtf_params equivalent
struct StandardSTFParams {
    float shadows;   // lo (black point)
    float midtones;  // m (midtone balance, 0-1)
    float highlights; // hi (white point)
};

// Computes standard AutoStretch params for a single channel
static StandardSTFParams computeStandardSTF(const std::vector<float>& data, int w, int h, int ch, int channelIdx) {
    const float AS_DEFAULT_SHADOWS_CLIPPING = -2.80f;
    const float AS_DEFAULT_TARGET_BACKGROUND = 0.25f;
    
    StandardSTFParams result;
    result.highlights = 1.0f;
    result.shadows = 0.0f;
    result.midtones = 0.25f; // Default fallback (neutral)
    
    if (data.empty() || ch <= 0) return result;
    
    // Extract channel data
    std::vector<float> chData;
    chData.reserve(data.size() / ch);
    for (size_t i = channelIdx; i < data.size(); i += ch) {
        chData.push_back(data[i]);
    }
    if (chData.size() < 2) return result;  // Need at least 2 samples for meaningful stats
    
    // Compute median and MAD
    std::vector<float> sorted = chData;
    std::sort(sorted.begin(), sorted.end());
    float median = sorted[sorted.size() / 2];
    
    // Safe Median Logic
    if (median < 1e-6f) {
        // Fallback to Mean if Median is zero (common in star masks)
        double sum = 0;
        for(float v : chData) sum += v;
        median = (float)(sum / chData.size());
        
        // If still zero, force a small epsilon
        if (median < 1e-6f) median = 0.0001f;
    }
    
    std::vector<float> deviations(sorted.size());
    for (size_t i = 0; i < sorted.size(); ++i) {
        deviations[i] = std::fabs(sorted[i] - median);
    }
    std::sort(deviations.begin(), deviations.end());
    float mad = deviations[deviations.size() / 2];
    
    // Guard against MAD = 0 (Standard does this)
    if (mad < 1e-9f) mad = 0.001f;
    
    // Standard MAD_NORM = 1.4826
    float sigma = 1.4826f * mad;
    
    // shadows = median + clipping * sigma (clipping is negative, so this subtracts)
    float c0 = median + AS_DEFAULT_SHADOWS_CLIPPING * sigma;
    if (c0 < 0.f) c0 = 0.f;
    
    // m2 = median - c0 (the "distance" from shadow to median)
    float m2 = median - c0;
    
    result.shadows = c0;
    result.highlights = 1.0f;
    
    // Standard formula for midtones: MTF(m2, target_bg, 0, 1)
    float target = AS_DEFAULT_TARGET_BACKGROUND;
    if (m2 <= 1e-9f || m2 >= 1.f) {
        // If m2 is super small, it means shadow ~ median. 
        // We need extreme stretch. 
        result.midtones = 0.001f; // Force strong stretch
    } else {
        float xp = m2;
        float denom = ((2.f * target - 1.f) * xp) - target;
        if (std::fabs(denom) < 1e-9f) {
            result.midtones = 0.25f;
        } else {
            result.midtones = ((target - 1.f) * xp) / denom;
        }
        // Clamp to valid range
        if (std::isnan(result.midtones) || std::isinf(result.midtones)) {
            result.midtones = 0.25f;
        } else {
            result.midtones = std::clamp(result.midtones, 0.00001f, 0.99999f); // Allow stronger stretch than 0.001
        }
    }
    
    return result;
}

QImage ImageBuffer::getDisplayImage(DisplayMode mode, bool linked, const std::vector<std::vector<float>>* overrideLUT, int maxWidth, int maxHeight, bool showMask, bool inverted, bool falseColor) const {
    if (m_data.empty()) return QImage();

    // 1. Generate LUTs for each channel
    std::vector<std::vector<float>> luts(3, std::vector<float>(LUT_SIZE));
    
    if (overrideLUT && overrideLUT->size() == 3 && !overrideLUT->at(0).empty()) {
        luts = *overrideLUT;
    } else if (mode == Display_AutoStretch) {
        std::vector<ChStats> stats(m_channels);
        for (int c = 0; c < m_channels; ++c) stats[c] = computeStats(m_data, m_width, m_height, m_channels, c);
        const float targetBG = 0.25f;
        const float shadowClip = -2.8f; 
        
        if (linked && m_channels == 3) {
            float avgMed = (stats[0].median + stats[1].median + stats[2].median) / 3.0f;
            float avgMad = (stats[0].mad + stats[1].mad + stats[2].mad) / 3.0f;
            
            float shadow = std::max(0.0f, avgMed + shadowClip * avgMad);
            // If the image is uniform (mad=0) and bright (median=1), shadow becomes 1.0.
            // We must ensure shadow < median to have a valid stretch.
            if (shadow >= avgMed) shadow = std::max(0.0f, avgMed - 0.001f);
            
            float mid = avgMed - shadow; 
            if (mid <= 0) mid = 0.5f; 
            
            float m = mtf_func(targetBG, mid);
            for (int c = 0; c < 3; ++c) {
                for (int i = 0; i < LUT_SIZE; ++i) {
                    float x = (float)i / (LUT_SIZE - 1);
                    float normX = (x - shadow) / (1.0f - shadow + 1e-9f);
                    luts[c][i] = mtf_func(m, normX);
                }
            }
        } else {
            for (int c = 0; c < m_channels; ++c) {
                float shadow = std::max(0.0f, stats[c].median + shadowClip * stats[c].mad);
                if (shadow >= stats[c].median) shadow = std::max(0.0f, stats[c].median - 0.001f);
                
                float mid = stats[c].median - shadow;
                if (mid <= 0) mid = 0.5f;

                float m = mtf_func(targetBG, mid);
                for (int i = 0; i < LUT_SIZE; ++i) {
                    float x = (float)i / (LUT_SIZE - 1);
                    float normX = (x - shadow) / (1.0f - shadow + 1e-9f);
                    luts[c][i] = mtf_func(m, normX);
                }
            }
        }
    } else if (mode == Display_ArcSinh) {
        float strength = 100.0f;
        for (int c = 0; c < m_channels; ++c) {
            for (int i = 0; i < LUT_SIZE; ++i) luts[c][i] = std::asinh((float)i/(LUT_SIZE-1) * strength) / std::asinh(strength);
        }
    } else if (mode == Display_Sqrt) {
        for (int c = 0; c < m_channels; ++c) {
            for (int i = 0; i < LUT_SIZE; ++i) luts[c][i] = std::sqrt((float)i/(LUT_SIZE-1));
        } 
     } else if (mode == Display_Log) {
         const float scale = 65535.0f / 10.0f;
         const float norm = 1.0f / std::log(scale);
         const float min_val = 10.0f / 65535.0f;
         
         for (int c = 0; c < m_channels; ++c) {
            for (int i = 0; i < LUT_SIZE; ++i) {
                float x = (float)i / (LUT_SIZE - 1);
                if (x < min_val) {
                    luts[c][i] = 0.0f;
                } else {
                    // Result is log(x * scale) * norm. 
                    // Since x >= min_val (1/scale), x*scale >= 1, so log >= 0.
                    luts[c][i] = std::log(x * scale) * norm;
                }
            }
        }
    } else if (mode == Display_Histogram) {
        // Histogram Equalization logic
        // 1. Compute Histograms
        std::vector<std::vector<int>> hists(m_channels, std::vector<int>(LUT_SIZE, 0));
        
        // Fast subsample histogram
        int skip = 1;
        long total = m_data.size();
        if (total > 2000000) skip = 4;
        
        for (long i = 0; i < total; i += skip * m_channels) {
             for (int c = 0; c < m_channels; ++c) {
                  float v = m_data[i + c];
                  int idx = std::clamp((int)(v * (LUT_SIZE - 1)), 0, LUT_SIZE - 1);
                  hists[c][idx]++;
             }
        }
        
        // 2. Build CDF and LUT
        for (int c = 0; c < m_channels; ++c) {
             long cdf = 0;
             long minCdf = 0;
             long N = 0;
             for(int count : hists[c]) N += count;
             
             // Find min non-zero CDF for proper scaling
             for(int i=0; i<LUT_SIZE; ++i) {
                 if (hists[c][i] > 0) {
                     minCdf = hists[c][i]; // Approx
                     break;
                 }
             }

             for (int i = 0; i < LUT_SIZE; ++i) {
                  cdf += hists[c][i];
                  // Regular HE formula: (cdf - min) / (total - min)
                  float val = 0.0f;
                  if (N > minCdf) {
                      val = (float)(cdf - minCdf) / (float)(N - minCdf);
                  } else {
                      val = (float)i / (LUT_SIZE - 1); // Fallback
                  }
                  luts[c][i] = std::clamp(val, 0.0f, 1.0f);
             }
        }
    } else {
        for (int c = 0; c < m_channels; ++c) {
             for (int i = 0; i < LUT_SIZE; ++i) luts[c][i] = (float)i / (LUT_SIZE - 1);
        }
    }

    // 2. Generate Image (with Downsampling)
    int outW = m_width;
    int outH = m_height;
    int stepX = 1;
    int stepY = 1;
    
    if (maxWidth > 0 && m_width > maxWidth) {
        stepX = m_width / maxWidth;
    }
    if (maxHeight > 0 && m_height > maxHeight) {
        stepY = m_height / maxHeight;
    }
    
    // Use the larger step for BOTH to preserve Aspect Ratio
    int step = std::max(stepX, stepY);
    if (step < 1) step = 1;
    
    stepX = step;
    stepY = step;
    
    outW = m_width / stepX;
    outH = m_height / stepY;


    // Force RGB888 if False Color is enabled, otherwise use Grayscale8 for mono
    QImage::Format fmt = (m_channels == 1 && !falseColor) ? QImage::Format_Grayscale8 : QImage::Format_RGB888;
    QImage result(outW, outH, fmt);

    // False Color Helper: HSV to RGB (H: 0-360, S: 0-1, V: 0-1)
    auto hsvToRgb = [](float h, float s, float v, uchar& r, uchar& g, uchar& b) {
        if (s <= 0.0f) {
            r = g = b = static_cast<uchar>(v * 255.0f);
            return;
        }
        float hh = h;
        if (hh >= 360.0f) hh = 0.0f;
        hh /= 60.0f;
        int i = static_cast<int>(hh);
        float ff = hh - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - (s * ff));
        float t = v * (1.0f - (s * (1.0f - ff)));
        float rr, gg, bb;
        switch (i) {
            case 0: rr = v; gg = t; bb = p; break;
            case 1: rr = q; gg = v; bb = p; break;
            case 2: rr = p; gg = v; bb = t; break;
            case 3: rr = p; gg = q; bb = v; break;
            case 4: rr = t; gg = p; bb = v; break;
            default: rr = v; gg = p; bb = q; break;
        }
        r = static_cast<uchar>(rr * 255.0f);
        g = static_cast<uchar>(gg * 255.0f);
        b = static_cast<uchar>(bb * 255.0f);
    };

    // Scanline processing
    #pragma omp parallel for
    for (int y = 0; y < outH; ++y) {
        uchar* dest = result.scanLine(y);
        int srcY = y * stepY;
        if (srcY >= m_height) srcY = m_height - 1;
        
        int srcIdxBase = srcY * m_width * m_channels;
        
        for (int x = 0; x < outW; ++x) {
            int srcX = x * stepX;
            if (srcX >= m_width) srcX = m_width - 1;
            
            float r_out, g_out, b_out;

            // Mask Variables
            float maskAlpha = 0.0f;
            bool applyMaskBlend = (m_hasMask && overrideLUT != nullptr);
            if (applyMaskBlend) {
                int maskX = x * stepX;
                int maskY = y * stepY;
                maskAlpha = m_mask.pixel(maskX, maskY);
                if (m_mask.inverted) maskAlpha = 1.0f - maskAlpha;
                if (m_mask.mode == "protect") maskAlpha = 1.0f - maskAlpha;
                maskAlpha *= m_mask.opacity;
            }

            if (m_channels == 1) {
                size_t idx = srcIdxBase + srcX;
                if (idx >= m_data.size()) continue;
                float v = m_data[idx];
                int iVal = static_cast<int>(std::max(0.0f, std::min(1.0f, v)) * (LUT_SIZE - 1));
                float out = luts[0][iVal];
                
                // Blend with original if mask active on preview
                if (applyMaskBlend) {
                    out = out * maskAlpha + v * (1.0f - maskAlpha);
                }

                if (inverted) out = 1.0f - out;
                r_out = g_out = b_out = out;
            } else {
                int base = srcIdxBase + srcX * m_channels;
                if (base + 2 >= static_cast<int>(m_data.size())) continue;
                float r = m_data[base+0];
                float g = m_data[base+1];
                float b = m_data[base+2];
                
                int ir = static_cast<int>(std::max(0.0f, std::min(1.0f, r)) * (LUT_SIZE - 1));
                int ig = static_cast<int>(std::max(0.0f, std::min(1.0f, g)) * (LUT_SIZE - 1));
                int ib = static_cast<int>(std::max(0.0f, std::min(1.0f, b)) * (LUT_SIZE - 1));
                
                r_out = luts[0][ir];
                g_out = luts[1][ig];
                b_out = luts[2][ib];
                
                // Blend with original
                if (applyMaskBlend) {
                    r_out = r_out * maskAlpha + r * (1.0f - maskAlpha);
                    g_out = g_out * maskAlpha + g * (1.0f - maskAlpha);
                    b_out = b_out * maskAlpha + b * (1.0f - maskAlpha);
                }

                if (inverted) {
                    r_out = 1.0f - r_out;
                    g_out = 1.0f - g_out;
                    b_out = 1.0f - b_out;
                }
            }

            if (falseColor) {
                // Determine intensity for heatmap
                float intensity = (m_channels == 3) ? (0.2989f * r_out + 0.5870f * g_out + 0.1140f * b_out) : r_out;
                intensity = std::clamp(intensity, 0.0f, 1.0f);
                // Map intensity to Hue: 300 (Purple) -> 0 (Red)
                float hue = (1.0f - intensity) * 300.0f;
                uchar r8, g8, b8;
                hsvToRgb(hue, 1.0f, 1.0f, r8, g8, b8);
                dest[x*3+0] = r8;
                dest[x*3+1] = g8;
                dest[x*3+2] = b8;
            } else {
                if (m_channels == 1) {
                    dest[x] = static_cast<uchar>(std::clamp(r_out, 0.0f, 1.0f) * 255.0f);
                } else {
                    dest[x*3+0] = static_cast<uchar>(std::clamp(r_out, 0.0f, 1.0f) * 255.0f);
                    dest[x*3+1] = static_cast<uchar>(std::clamp(g_out, 0.0f, 1.0f) * 255.0f);
                    dest[x*3+2] = static_cast<uchar>(std::clamp(b_out, 0.0f, 1.0f) * 255.0f);
                }
            }

            // Apply Mask Overlay (Red Tint)
            if (showMask && m_hasMask) {
                // Since this is downsampled, we need to map x,y to mask coords
                int maskX = x * stepX;
                int maskY = y * stepY;
                
                // For better quality, maybe average? simpler: nearest neighbor
                float mVal = m_mask.pixel(maskX, maskY);
                if (m_mask.inverted) mVal = 1.0f - mVal;
                
                // Treat mVal as transparency of the redness
                if (mVal > 0) {
                     float overlayAlpha = 0.5f * mVal; // Max 50% opacity
                     
                     int off = (fmt == QImage::Format_Grayscale8) ? x : x*3;
                     int r = (fmt == QImage::Format_Grayscale8) ? dest[x] : dest[x*3+0];
                     int g = (fmt == QImage::Format_Grayscale8) ? dest[x] : dest[x*3+1];
                     int b = (fmt == QImage::Format_Grayscale8) ? dest[x] : dest[x*3+2];
                     
                     // If it was grayscale, we must convert this pixel area or use a better approach.
                     // But for mask overlay, we usually want color.
                     // The QImage format handles the storage.
                     
                     if (fmt == QImage::Format_Grayscale8) {
                         // We can't easily tint red in a grayscale image. 
                         // For simplicity, just make it brighter? 
                         // No, let's assume if showMask is on, we might want RGB.
                         // However, getDisplayImage is called often. 
                         // Let's just do a simple highlight for grayscale.
                         int val = dest[x];
                         dest[x] = std::clamp(static_cast<int>(val * (1.0f + overlayAlpha)), 0, 255);
                     } else {
                         // Red tint: R + alpha, G/B reduced
                         // Simple alpha blending with Red (255, 0, 0)
                         float r_f = r * (1.0f - overlayAlpha) + 255.0f * overlayAlpha;
                         float g_f = g * (1.0f - overlayAlpha);
                         float b_f = b * (1.0f - overlayAlpha);
                         
                         dest[x*3+0] = static_cast<uchar>(std::clamp(r_f, 0.0f, 255.0f));
                         dest[x*3+1] = static_cast<uchar>(std::clamp(g_f, 0.0f, 255.0f));
                         dest[x*3+2] = static_cast<uchar>(std::clamp(b_f, 0.0f, 255.0f));
                     }
                }
            }
        }
    }
    
    return result;
}


// ... (previous includes)
#include <fitsio.h>
#include <numeric>

// ------ Saving Logic ------
bool ImageBuffer::save(const QString& filePath, const QString& format, BitDepth depth, QString* errorMsg) {
    if (m_data.empty()) return false;

    // XISF Support
    if (format.compare("xisf", Qt::CaseInsensitive) == 0) {
        return XISFWriter::write(filePath, *this, depth, errorMsg);
    }

    // FITS uses CFITSIO
    if (format.compare("fits", Qt::CaseInsensitive) == 0 || format.compare("fit", Qt::CaseInsensitive) == 0) {
        fitsfile* fptr;
        int status = 0;
        
        // Overwrite by prefixing "!" (CFITSIO magic)
        QString outName = "!" + filePath;
        
        if (fits_create_file(&fptr, outName.toUtf8().constData(), &status)) {
            if (errorMsg) *errorMsg = "CFITSIO Create File Error: " + QString::number(status);
            return false;
        }

        // Determine BITPIX
        int bitpix = FLOAT_IMG; // Default -32
        if (depth == Depth_32Int) bitpix = LONG_IMG; // 32
        else if (depth == Depth_16Int) bitpix = SHORT_IMG; // 16
        else if (depth == Depth_8Int) bitpix = BYTE_IMG; // 8
        
        long naxes[3] = { (long)m_width, (long)m_height, (long)m_channels };
        int naxis = (m_channels > 1) ? 3 : 2;

        if (fits_create_img(fptr, bitpix, naxis, naxes, &status)) {
            if (errorMsg) *errorMsg = "CFITSIO Create Image Error: " + QString::number(status);
            fits_close_file(fptr, &status);
            return false;
        }

        // Prepare data to write
        long nelements = m_width * m_height * m_channels;
        
        if (depth == Depth_32Float) {
             // Write standard float data interleaved?
             // CFITSIO for 3D is usually planar [width, height, channels].
             // Our m_data is interleaved [pixel, pixel...].
             // WE must de-interleave for FITS writing if NAXIS3=3.
             
             std::vector<float> planarData(nelements);
             if (m_channels == 1) {
                 planarData = m_data; // Copy
             } else {
                 long planeSize = m_width * m_height;
                 for (int i = 0; i < planeSize; ++i) {
                     planarData[i] = m_data[i*3 + 0];             // R (Plane 1)
                     planarData[i + planeSize] = m_data[i*3 + 1]; // G (Plane 2)
                     planarData[i + 2*planeSize] = m_data[i*3 + 2];// B (Plane 3)
                 }
             }

             if (fits_write_img(fptr, TFLOAT, 1, nelements, planarData.data(), &status)) {
                 if (errorMsg) *errorMsg = "CFITSIO Write Error: " + QString::number(status);
                 fits_close_file(fptr, &status);
                 return false;
             }

        } else {
            // Integer conversion
            double bscale = 1.0;
            double bzero = 0.0;
            
            if (depth == Depth_16Int) {
                // UInt16: [0, 65535] -> [-32768, 32767]
                bzero = 32768.0;
                bscale = 1.0;
                fits_write_key(fptr, TDOUBLE, "BZERO", &bzero, "offset for unsigned integers", &status);
                fits_write_key(fptr, TDOUBLE, "BSCALE", &bscale, "scaling", &status);
            } else if (depth == Depth_32Int) {
                // UInt32: [0, 4294967295] -> [-2147483648, 2147483647]
                // BITPIX=32 is signed 32-bit.
                // We use standard BZERO = 2147483648 to represent unsigned 32-bit.
                bzero = 2147483648.0;
                bscale = 1.0;
                fits_write_key(fptr, TDOUBLE, "BZERO", &bzero, "offset for unsigned integers", &status);
                fits_write_key(fptr, TDOUBLE, "BSCALE", &bscale, "scaling", &status);
            } else {
                // 8-bit is unsigned natively (BYTE_IMG)
            }

            // Convert and Deshuffle
            std::vector<float> planarData(nelements);
            // MaxVal depends on target range
            // UInt32 max = 4294967295.0
            float maxVal = (depth == Depth_16Int) ? 65535.0f : ((depth == Depth_32Int) ? 4294967295.0f : 255.0f);
            
             if (m_channels == 1) {
                 for(size_t i=0; i<nelements; ++i) planarData[i] = m_data[i] * maxVal;
             } else {
                 long planeSize = m_width * m_height;
                 for (int i = 0; i < planeSize; ++i) {
                     planarData[i] = m_data[i*3 + 0] * maxVal;
                     planarData[i + planeSize] = m_data[i*3 + 1] * maxVal;
                     planarData[i + 2*planeSize] = m_data[i*3 + 2] * maxVal;
                 }
             }
             
             int type = TFLOAT;
             // fits_write_img will convert TFLOAT using BZERO/BSCALE
             
             if (fits_write_img(fptr, type, 1, nelements, planarData.data(), &status)) {
                 if (errorMsg) *errorMsg = "CFITSIO Write Error: " + QString::number(status);
                 fits_close_file(fptr, &status);
                 return false;
             }
        }
        
        // --- WRITE METADATA ---
        for (const auto& card : m_meta.rawHeaders) {
            // Skip structural keywords that CFITSIO already wrote or will write
            QString key = card.key.trimmed().toUpper();
            if (key == "SIMPLE" || key == "BITPIX" || key == "NAXIS" || key == "NAXIS1" || 
                key == "NAXIS2" || key == "NAXIS3" || key == "EXTEND" || key == "BZERO" || key == "BSCALE") {
                continue;
            }
            
            // Try to write as key/value/comment
            // Note: CFITSIO handles quotes for strings if we use fits_write_key
            // However, our 'value' is already a string representation.
            // If it starts with quotes, it might be a literal string. 
            // If it's a number, it's a number.
            // Simple approach: fits_write_record with a reconstructed card?
            // Or fits_write_key with TSTRING for now, CFITSIO is smart.
            
            if (key == "HISTORY") {
                fits_write_history(fptr, card.value.toUtf8().constData(), &status);
            } else if (key == "COMMENT") {
                fits_write_comment(fptr, card.value.toUtf8().constData(), &status);
            } else {
                // Heuristic to determine type
                bool isLong;
                long lVal = card.value.toLong(&isLong);
                
                bool isDouble;
                double dVal = card.value.toDouble(&isDouble);
                
                if (isLong) {
                     fits_write_key(fptr, TLONG, key.toUtf8().constData(), &lVal, card.comment.toUtf8().constData(), &status);
                } else if (isDouble) {
                     fits_write_key(fptr, TDOUBLE, key.toUtf8().constData(), &dVal, card.comment.toUtf8().constData(), &status);
                } else {
                     // String
                     // Ensure we don't double-quote if the string already has quotes?
                     // CFITSIO adds quotes for TSTRING.
                     fits_write_key(fptr, TSTRING, key.toUtf8().constData(), 
                                    (void*)card.value.toUtf8().constData(), 
                                    card.comment.toUtf8().constData(), &status);
                }
            }
            
            if (status) status = 0; // Ignore error and continue
        }
        
        // Write WCS explicitly to ensure it overrides any old/invalid WCS in rawHeaders
        if (m_meta.ra != 0 || m_meta.dec != 0) {
            fits_update_key(fptr, TDOUBLE, "CRVAL1", &m_meta.ra, "RA at reference pixel", &status);
            fits_update_key(fptr, TDOUBLE, "CRVAL2", &m_meta.dec, "Dec at reference pixel", &status);
            fits_update_key(fptr, TDOUBLE, "CRPIX1", &m_meta.crpix1, "Reference pixel x", &status);
            fits_update_key(fptr, TDOUBLE, "CRPIX2", &m_meta.crpix2, "Reference pixel y", &status);
            fits_update_key(fptr, TDOUBLE, "CD1_1", &m_meta.cd1_1, "", &status);
            fits_update_key(fptr, TDOUBLE, "CD1_2", &m_meta.cd1_2, "", &status);
            fits_update_key(fptr, TDOUBLE, "CD2_1", &m_meta.cd2_1, "", &status);
            fits_update_key(fptr, TDOUBLE, "CD2_2", &m_meta.cd2_2, "", &status);
            status = 0;
        }

        fits_close_file(fptr, &status);
        return true;

    } else if (format.compare("tiff", Qt::CaseInsensitive) == 0 || format.compare("tif", Qt::CaseInsensitive) == 0) {
        // Use SimpleTiffWriter for full bit depth support
        SimpleTiffWriter::Format fmt = SimpleTiffWriter::Format_uint8;
        if (depth == Depth_16Int) fmt = SimpleTiffWriter::Format_uint16;
        else if (depth == Depth_32Int) fmt = SimpleTiffWriter::Format_uint32;
        else if (depth == Depth_32Float) fmt = SimpleTiffWriter::Format_float32;
        
        // Error msg?
        if (!SimpleTiffWriter::write(filePath, m_width, m_height, m_channels, fmt, m_data, errorMsg)) {
             return false;
        }
        return true;
        
    } else {
        // Standard (JPG/PNG) via QImage
        // Convert to 8-bit RGB
        QImage saveImg = getDisplayImage(Display_Linear); 
        return saveImg.save(filePath, format.toLatin1().constData());
    }
}

// ------ True Stretch Impl ------

// Specific Python Formula port
// num = (med - 1.0) * target_median * r
// den = med * (target_median + r - 1.0) - target_median * r
static float stretch_fn(float r, float med, float target) {
    if (r < 0) r = 0; // Clip input
     // if (med == 1.0) avoid div zero? formula handles it?
    float num = (med - 1.0f) * target * r;
    float den = med * (target + r - 1.0f) - target * r;
    if (std::abs(den) < 1e-12f) den = 1e-12f;
    return num / den;
}

// Curves Interpolation (Linear for now, or simple S-curve?)
// Python uses 6 points. 
// For simplicity in C++, let's implement a look-up or direct linear interp.
static float apply_curve(float val, const std::vector<float>& x, const std::vector<float>& y) {
    if (val <= 0) return 0;
    if (val >= 1) return 1;
    
    // Find segment
    for (size_t i = 0; i < x.size() - 1; ++i) {
        if (val >= x[i] && val <= x[i+1]) {
            float t = (val - x[i]) / (x[i+1] - x[i]);
            return y[i] + t * (y[i+1] - y[i]);
        }
    }
    return val;
}

// Helper to get stats for a channel (Legacy/TrueStretch version)
struct TrueStretchStats { float median; float bp; float den; };

static TrueStretchStats getTrueStretchStats(const std::vector<float>& data, int stride, float nSig, int offset, int channels) {
    std::vector<float> sample;
    float minVal = 1e30f;
    
    size_t limit = data.size();
    sample.reserve(limit / (stride * channels) + 100);
    
    // Subsample
    double sum = 0, sumSq = 0;
    long count = 0;
    
    for (size_t i = offset; i < limit; i += stride * channels) {
        float v = data[i];
        sample.push_back(v);
        if (v < minVal) minVal = v;
        sum += v;
        sumSq += v*v;
        count++;
    }
    
    if (sample.empty()) return {0.5f, 0.0f, 1.0f};

    size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    float median = sample[mid];
    
    double mean = sum / count;
    double var = (sumSq / count) - (mean * mean);
    double stdDev = std::sqrt(std::max(0.0, var));
    
    float bp = std::max(minVal, median - nSig * (float)stdDev);
    float den = 1.0f - bp;
    if (den < 1e-12f) den = 1e-12f;
    
    return {median, bp, den};
}

void ImageBuffer::performTrueStretch(const StretchParams& params) {
    if (m_data.empty()) return;

    // Mask Support: Copy original if mask is present
    ImageBuffer original;
    if (hasMask()) original = *this;

    // 1. Calculate Stats
    std::vector<TrueStretchStats> stats;
    int stride = (m_width * m_height) / 100000 + 1;
    
    if (params.linked) {
        // Compute combined stats (using Green channel as proxy or luminance? Python uses all pixels)
        // Let's sample all channels interleaved
         TrueStretchStats s = getTrueStretchStats(m_data, stride, 2.7f, 0, 1); // Treat as 1 channel stream
         stats.push_back(s); // [0] = global
    } else {
        for (int c = 0; c < m_channels; ++c) {
            stats.push_back(getTrueStretchStats(m_data, stride, 2.7f, c, m_channels));
        }
    }
    
    // Curves Setup
    std::vector<float> cx, cy;
    bool useCurves = (params.applyCurves && params.curvesBoost > 0);
    if (useCurves) {
        float tm = params.targetMedian;
        float cb = params.curvesBoost;
        float p3x = 0.25f * (1.0f - tm) + tm;
        float p4x = 0.75f * (1.0f - tm) + tm;
        float p3y = std::pow(p3x, (1.0f - cb));
        float p4y = std::pow(std::pow(p4x, (1.0f - cb)), (1.0f - cb));
        
        cx = {0.0f, 0.5f*tm, tm, p3x, p4x, 1.0f};
        cy = {0.0f, 0.5f*tm, tm, p3y, p4y, 1.0f};
    }

    // Precompute MedRescaled
    std::vector<float> medRescaled;
    if (params.linked) {
        float mr = (stats[0].median - stats[0].bp) / stats[0].den;
        medRescaled.push_back(mr);
    } else {
        for (int c = 0; c < m_channels; ++c) {
             float mr = (stats[c].median - stats[c].bp) / stats[c].den;
             medRescaled.push_back(mr);
        }
    }
    
    // Main Loop
    long total = m_data.size();
    int ch = m_channels;
    
    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        int c = i % ch;
        // Linked uses index 0, Unlinked uses index c
        int sIdx = params.linked ? 0 : c;
        
        float bp = stats[sIdx].bp;
        float den = stats[sIdx].den;
        float mr = medRescaled[sIdx];
        
        float v = m_data[i];
        
        // Rescale
        float rescaled = (v - bp) / den;
        
        // Formula
        float out = stretch_fn(rescaled, mr, params.targetMedian);
        
        // Curves
        if (useCurves) {
            out = apply_curve(out, cx, cy);
        }
        
        // Clip
        if (out < 0.0f) out = 0.0f;
        if (out > 1.0f) out = 1.0f;
        
        m_data[i] = out;
    }
    
// Normalize
    if (params.normalize) {
        float mx = -1e30f;
        for (float v : m_data) if (v > mx) mx = v;
        if (mx > 1e-9f) {
             for (long i = 0; i < total; ++i) m_data[i] /= mx;
        }
    }
    
    // Blend back if masked
    if (hasMask()) {
        blendResult(original);
    }
}

// Compute LUT for TrueStretch
std::vector<std::vector<float>> ImageBuffer::computeTrueStretchLUT(const StretchParams& params, int size) const {
    if (m_data.empty()) return {};

    // 1. Calculate Stats (Same as performTrueStretch)
    std::vector<TrueStretchStats> stats;
    int stride = (m_width * m_height) / 100000 + 1;
    
    if (params.linked) {
         TrueStretchStats s = getTrueStretchStats(m_data, stride, 2.7f, 0, 1); 
         stats.push_back(s); 
    } else {
        for (int c = 0; c < m_channels; ++c) {
            stats.push_back(getTrueStretchStats(m_data, stride, 2.7f, c, m_channels));
        }
    }
    
    // Curves Setup
    std::vector<float> cx, cy;
    bool useCurves = (params.applyCurves && params.curvesBoost > 0);
    if (useCurves) {
        float tm = params.targetMedian;
        float cb = params.curvesBoost;
        float p3x = 0.25f * (1.0f - tm) + tm;
        float p4x = 0.75f * (1.0f - tm) + tm;
        float p3y = std::pow(p3x, (1.0f - cb));
        float p4y = std::pow(std::pow(p4x, (1.0f - cb)), (1.0f - cb));
        cx = {0.0f, 0.5f*tm, tm, p3x, p4x, 1.0f};
        cy = {0.0f, 0.5f*tm, tm, p3y, p4y, 1.0f};
    }

    // Precompute MedRescaled
    std::vector<float> medRescaled;
    if (params.linked) {
        float mr = (stats[0].median - stats[0].bp) / stats[0].den;
        medRescaled.push_back(mr);
    } else {
        for (int c = 0; c < m_channels; ++c) {
             float mr = (stats[c].median - stats[c].bp) / stats[c].den;
             medRescaled.push_back(mr);
        }
    }

    // Build LUTs
    std::vector<std::vector<float>> luts(m_channels, std::vector<float>(size));
    
    std::vector<float> maxVals(m_channels, 1.0f);
    if (params.normalize) {
         float globalMax = 0.0f;
         for (int c = 0; c < m_channels; ++c) {
             int sIdx = params.linked ? 0 : c;
             // Input 1.0 through formula
             float bp = stats[sIdx].bp;
             float den = stats[sIdx].den;
             float mr = medRescaled[sIdx];
             float rescaled = (1.0f - bp) / den;
             float out = stretch_fn(rescaled, mr, params.targetMedian);
             if (useCurves) out = apply_curve(out, cx, cy);
             if (out > globalMax) globalMax = out; 
         }
         if (globalMax > 1e-9f) {
             std::fill(maxVals.begin(), maxVals.end(), globalMax);
         }
    }

    #pragma omp parallel for
    for (int i = 0; i < size; ++i) {
        float inVal = (float)i / (size - 1);
        
        for (int c = 0; c < m_channels; ++c) {
            int sIdx = params.linked ? 0 : c;
            float bp = stats[sIdx].bp;
            float den = stats[sIdx].den;
            float mr = medRescaled[sIdx];
            
            float rescaled = (inVal - bp) / den;
            float out = stretch_fn(rescaled, mr, params.targetMedian);
            
            if (useCurves) out = apply_curve(out, cx, cy);
            
            if (out < 0.0f) out = 0.0f;
            
            if (params.normalize) {
                out /= maxVals[c];
            }
            
            // Final clip
            if (out > 1.0f) out = 1.0f;
            
            luts[c][i] = out;
        }
    }
    
    return luts;
}

// ------ GHS Implementation ------
#include "GHSAlgo.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

static inline void rgbblend_func(float* r, float* g, float* b, float sf0, float sf1, float sf2, float tf0, float tf1, float tf2, const bool* do_channel, float m_CB) {
    // maxima
    float sfmax = std::max({sf0, sf1, sf2});
    float tfmax = std::max({tf0, tf1, tf2});

    // difference
    float d = sfmax - tfmax;

    // build masks as 0.0f / 1.0f without branching
    float mask_cond = (tfmax + m_CB * d > 1.0f) ? 1.0f : 0.0f;   // condition tfmax + m_CB*d > 1
    float mask_dnz  = (d != 0.0f)               ? 1.0f : 0.0f;   // d != 0

    // full_mask = mask_cond && mask_dnz  (still 0.0f or 1.0f)
    float full_mask = mask_cond * mask_dnz;

    // construct safe_d: if d!=0 then d else 1.0f  (done branchless)
    float safe_d = mask_dnz * d + (1.0f - mask_dnz) * 1.0f;

    // candidate and limited value
    float candidate = (1.0f - tfmax) / safe_d;       // safe even when original d==0 due to safe_d
    float limited   = std::min(m_CB, candidate);

    // final k = full_mask ? limited : m_CB  (blend)
    float k = full_mask * limited + (1.0f - full_mask) * m_CB;

    // precompute factor
    float one_minus_k = 1.0f - k;

    // channel masks (0 or 1) to decide whether to update each channel
    float mr = do_channel[0] ? 1.0f : 0.0f;
    float mg = do_channel[1] ? 1.0f : 0.0f;
    float mb = do_channel[2] ? 1.0f : 0.0f;

    // blend per-channel without branching:
    // if channel enabled: result = one_minus_k * tf + k * sf
    // else: keep old value (*r, *g, *b)
    *r = mr * (one_minus_k * tf0 + k * sf0) + (1.0f - mr) * (*r);
    *g = mg * (one_minus_k * tf1 + k * sf1) + (1.0f - mg) * (*g);
    *b = mb * (one_minus_k * tf2 + k * sf2) + (1.0f - mb) * (*b);
}

// RGB-HSL helpers from colors.c
static inline void rgb_to_hsl(float r, float g, float b, float& h, float& s, float& l) {
    float v = std::max({r, g, b});
    float m = std::min({r, g, b});
    float vm = v - m;
    l = (m + v) / 2.0f;
    if (vm == 0.0f) {
        h = 0.0f;
        s = 0.0f;
        return;
    }
    s = (l <= 0.5f) ? (vm / (v + m)) : (vm / (2.0f - v - m));
    float r2 = (v - r) / vm;
    float g2 = (v - g) / vm;
    float b2 = (v - b) / vm;
    float hr = (r == v) ? (g == m ? 5.0f + b2 : 1.0f - g2) : 0.0f;
    float hg = (g == v) ? (b == m ? 1.0f + r2 : 3.0f - b2) : 0.0f;
    float hb = (b == v) ? (r == m ? 3.0f + g2 : 5.0f - r2) : 0.0f;
    h = (hr + hg + hb) / 6.0f;
}

static inline void hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b) {
    h = std::fmod(h, 1.0f);
    if (h < 0) h += 1.0f;
    float v = (l <= 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
    if (v <= 0.0f) {
        r = g = b = 0.0f;
        return;
    }
    float m = l + l - v;
    float sv = (v - m) / v;
    float h6 = h * 6.0f;
    int sextant = static_cast<int>(h6);
    float fract = h6 - sextant;
    float vsf = v * sv * fract;
    float mid1 = m + vsf;
    float mid2 = v - vsf;
    r = (sextant == 0 || sextant == 5) ? v : (sextant == 2 || sextant == 3) ? m : (sextant == 4) ? mid1 : mid2;
    g = (sextant == 1 || sextant == 2) ? v : (sextant == 4 || sextant == 5) ? m : (sextant == 0) ? mid1 : mid2;
    b = (sextant == 3 || sextant == 4) ? v : (sextant == 0 || sextant == 1) ? m : (sextant == 2) ? mid1 : mid2;
}

void ImageBuffer::applyGHS(const GHSParams& params) {
    if (m_data.empty()) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    // 1. Setup Algorithm Parameters
    GHSAlgo::GHSParams algoParams;
    algoParams.D = (float)params.D;
    algoParams.B = (float)params.B;
    algoParams.SP = (float)params.SP;
    algoParams.LP = (float)params.LP;
    algoParams.HP = (float)params.HP;
    algoParams.BP = (float)params.BP;
    
    // Fix Enum Mapping
    switch(params.mode) {
        case GHS_GeneralizedHyperbolic: algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL; break;
        case GHS_InverseGeneralizedHyperbolic: algoParams.type = GHSAlgo::STRETCH_PAYNE_INVERSE; break;
        case GHS_Linear: algoParams.type = GHSAlgo::STRETCH_LINEAR; break;
        case GHS_ArcSinh: algoParams.type = GHSAlgo::STRETCH_ASINH; break;
        case GHS_InverseArcSinh: algoParams.type = GHSAlgo::STRETCH_INVASINH; break;
        default: algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL; break;
    }

    if (algoParams.type != GHSAlgo::STRETCH_LINEAR && algoParams.BP > 0.0f) {
        float den = 1.0f - algoParams.BP;
        if (den > 1e-6f) { // Avoid div by zero
            float invDen = 1.0f / den;
            algoParams.SP = (algoParams.SP - algoParams.BP) * invDen;
            algoParams.LP = (algoParams.LP - algoParams.BP) * invDen;
            algoParams.HP = (algoParams.HP - algoParams.BP) * invDen;
        }
    }

    GHSAlgo::GHSComputeParams cp;
    GHSAlgo::setup(cp, algoParams.B, algoParams.D, algoParams.LP, algoParams.SP, algoParams.HP, algoParams.type);

    long n_pixels = (long)m_width * m_height;
    
    // Determine Color Model Factors
    float factor_red = 0.2126f;
    float factor_green = 0.7152f;
    float factor_blue = 0.0722f;
    
    int active_count = 0;
    for(int c=0; c<3; ++c) if(params.channels[c]) active_count++;
    
    if (params.colorMode == GHS_EvenWeightedLuminance) {
        float f = (active_count > 0) ? (1.0f / active_count) : 0.0f;
        factor_red = factor_green = factor_blue = f;
    } else if (params.colorMode == GHS_WeightedLuminance && m_channels == 3) {
         if (active_count < 3) {
             float f = (active_count > 0) ? (1.0f / active_count) : 0.0f;
             factor_red = factor_green = factor_blue = f;
         }
    }

    bool active_channels[3] = {params.channels[0], params.channels[1], params.channels[2]};
    float m_CB = 1.0f; 

    float local_global_max = -FLT_MAX;
    
    #pragma omp parallel for reduction(max:local_global_max)
    for (long i = 0; i < n_pixels; ++i) {
        if (m_channels < 3 || params.colorMode == GHS_Independent) {
            // Mono or Independent Logic
            for (int c = 0; c < m_channels; ++c) {
                if (m_channels == 3 && !params.channels[c]) continue; 
                float v = m_data[i*m_channels + c];
                // Clip input to [0,1] as GHS logic expects it.
                float civ = std::max(0.0f, std::min(1.0f, v));
                // Call compute directly for Float precision
                m_data[i*m_channels + c] = (civ == 0.0f) ? 0.0f : GHSAlgo::compute(civ, algoParams, cp);
            }
        } 
        else if (m_channels == 3 && params.colorMode == GHS_Saturation) {
            // Saturation Model
            size_t idx = i * 3;
            float r = m_data[idx+0];
            float g = m_data[idx+1];
            float b = m_data[idx+2];
            float h, s, l;
            rgb_to_hsl(r, g, b, h, s, l);
            
            float cs = std::max(0.0f, std::min(1.0f, s));
            float new_s = (cs == 0.0f) ? 0.0f : GHSAlgo::compute(cs, algoParams, cp);
            
            hsl_to_rgb(h, new_s, l, r, g, b);
            m_data[idx+0] = r;
            m_data[idx+1] = g;
            m_data[idx+2] = b;
        }
        else if (m_channels == 3 && (params.colorMode == GHS_WeightedLuminance || params.colorMode == GHS_EvenWeightedLuminance)) {
            // Luminance-based Modes
            size_t idx = i * 3;
            float f[3] = { m_data[idx], m_data[idx+1], m_data[idx+2] };
            float f_clipped[3];
            for(int c=0; c<3; ++c) f_clipped[c] = std::max(0.0f, std::min(1.0f, f[c]));

            float sf[3];
            float tf[3] = {0,0,0}; 

            float fbar = (active_channels[0] ? factor_red * f_clipped[0] : 0) + 
                         (active_channels[1] ? factor_green * f_clipped[1] : 0) + 
                         (active_channels[2] ? factor_blue * f_clipped[2] : 0);
            
            // Direct call for sfbar
            float sfbar = (fbar == 0.0f) ? 0.0f : GHSAlgo::compute(std::min(1.0f, std::max(0.0f, fbar)), algoParams, cp);
            float stretch_factor = (fbar == 0.0f) ? 0.0f : sfbar / fbar;
            
            for(int c=0; c<3; ++c) sf[c] = f[c] * stretch_factor;
            
            if (params.clipMode == GHS_ClipRGBBlend) {
                for(int c=0; c<3; ++c) {
                    float xc = std::max(0.0f, std::min(1.0f, f[c]));
                    tf[c] = active_channels[c] ? ((xc == 0.0f) ? 0.0f : GHSAlgo::compute(xc, algoParams, cp)) : 0.0f;
                }
            }
            
            // Apply Clip Modes
            if (params.clipMode == GHS_ClipRGBBlend) {
                 rgbblend_func(&f[0], &f[1], &f[2], sf[0], sf[1], sf[2], tf[0], tf[1], tf[2], active_channels, m_CB);
                 for(int c=0; c<3; ++c) m_data[idx+c] = active_channels[c] ? f[c] : m_data[idx+c];
            } else if (params.clipMode == GHS_Rescale) {
                float maxval = std::max({sf[0], sf[1], sf[2]});
                if (maxval > 1.0f) {
                    float invmax = 1.0f / maxval;
                    sf[0] *= invmax; sf[1] *= invmax; sf[2] *= invmax;
                }
                for(int c=0; c<3; ++c) m_data[idx+c] = active_channels[c] ? sf[c] : m_data[idx+c];
            } else if (params.clipMode == GHS_RescaleGlobal) {
                 float maxval = std::max({sf[0], sf[1], sf[2]});
                 if (maxval > local_global_max) local_global_max = maxval;
                 for(int c=0; c<3; ++c) m_data[idx+c] = active_channels[c] ? sf[c] : m_data[idx+c];
            } else {
                for(int c=0; c<3; ++c) {
                    m_data[idx+c] = active_channels[c] ? std::max(0.0f, std::min(1.0f, sf[c])) : m_data[idx+c];
                }
            }
        }
    }
    
    // Pass 2 for Global Rescale
    if (params.colorMode != GHS_Independent && params.clipMode == GHS_RescaleGlobal && m_channels == 3) {
        if (local_global_max > 0.0f) { // If max is 0 (black image), do nothing
            float invMax = 1.0f / local_global_max;
            #pragma omp parallel for
            for (long i = 0; i < n_pixels; ++i) {
                 for (int c=0; c<3; ++c) {
                     if (active_channels[c]) m_data[i*3+c] *= invMax;
                 }
            }
        }
    }

    if (hasMask()) {
        blendResult(original);
    }
}


std::vector<float> ImageBuffer::computeGHSLUT(const GHSParams& params, int size) const {
    std::vector<float> lut(size);
    
    GHSAlgo::GHSParams algoParams;
    algoParams.D = (float)params.D;
    algoParams.B = (float)params.B;
    algoParams.SP = (float)params.SP;
    algoParams.LP = (float)params.LP;
    algoParams.HP = (float)params.HP;
    algoParams.BP = (float)params.BP;
    
    if (params.mode == GHS_GeneralizedHyperbolic) algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL;
    else if (params.mode == GHS_InverseGeneralizedHyperbolic) algoParams.type = GHSAlgo::STRETCH_PAYNE_INVERSE;
    else if (params.mode == GHS_ArcSinh) algoParams.type = GHSAlgo::STRETCH_ASINH;
    else if (params.mode == GHS_InverseArcSinh) algoParams.type = GHSAlgo::STRETCH_INVASINH;
    else algoParams.type = GHSAlgo::STRETCH_LINEAR;

    // Fix for Siril Compatibility (LUT for Preview)
    if (algoParams.type != GHSAlgo::STRETCH_LINEAR && algoParams.BP > 0.0f) {
        float den = 1.0f - algoParams.BP;
        if (den > 1e-6f) {
            float invDen = 1.0f / den;
            algoParams.SP = (algoParams.SP - algoParams.BP) * invDen;
            algoParams.LP = (algoParams.LP - algoParams.BP) * invDen;
            algoParams.HP = (algoParams.HP - algoParams.BP) * invDen;
        }
    }

    GHSAlgo::GHSComputeParams cp;
    GHSAlgo::setup(cp, algoParams.B, algoParams.D, algoParams.LP, algoParams.SP, algoParams.HP, algoParams.type);

    for (int i = 0; i < size; ++i) {
        float in = (float)i / (size - 1);
        lut[i] = GHSAlgo::compute(in, algoParams, cp);
    }
    
    return lut;
}

void ImageBuffer::blendResult(const ImageBuffer& original, bool inverseMask) {
    if (!hasMask() || m_mask.data.empty()) return;
    if (m_data.size() != original.m_data.size()) return;

    size_t total = m_data.size();
    int ch = m_channels;

    #pragma omp parallel for
    for (long long i = 0; i < (long long)total; ++i) {
         size_t pixelIdx = i / ch;
         if (pixelIdx >= m_mask.data.size()) continue;

         float alpha = m_mask.data[pixelIdx];
         if (m_mask.inverted) alpha = 1.0f - alpha;
         if (inverseMask) alpha = 1.0f - alpha;

         // Mode "protect" means white protects (alpha=0 effect)
         if (m_mask.mode == "protect") alpha = 1.0f - alpha;
         
         alpha *= m_mask.opacity;

         // Result = Processed * alpha + Original * (1 - alpha)
         m_data[i] = m_data[i] * alpha + original.m_data[i] * (1.0f - alpha);
    }
}

// Geometric Ops
void ImageBuffer::crop(int x, int y, int w, int h) {
    if (m_data.empty()) return;

    int oldW = m_width;
    int oldH = m_height;
    
    // Bounds check
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > m_width) w = m_width - x;
    if (y + h > m_height) h = m_height - y;
    
    if (w <= 0 || h <= 0) return;
    
    std::vector<float> newData(w * h * m_channels);
    
    for (int ry = 0; ry < h; ++ry) {
        int srcY = y + ry;
        int srcIdxStart = (srcY * m_width + x) * m_channels;
        int destIdxStart = (ry * w) * m_channels;
        int copySize = w * m_channels;
        
        for (int k = 0; k < copySize; ++k) {
             newData[destIdxStart + k] = m_data[srcIdxStart + k];
        }
    }
    
    m_width = w;
    m_height = h;
    m_data = newData;

    // Update WCS
    m_meta.crpix1 -= x;
    m_meta.crpix2 -= y;

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.crop(x, y, w, h);
        m_mask.data = maskBuf.data();
        m_mask.width = w;
        m_mask.height = h;
    }
}

void ImageBuffer::rotate(float angleDegrees) {
    if (m_data.empty()) return;
    if (std::abs(angleDegrees) < 0.1f) return;
    
    // Convert to radians
    float theta = -angleDegrees * 3.14159265f / 180.0f; // Negative to match typical image coord rotation
    float cosT = std::cos(theta);
    float sinT = std::sin(theta);
    
    // New dimensions (bounding box)
    // Corners: (0,0), (w,0), (0,h), (w,h)
    float x0 = 0, y0 = 0;
    float x1 = m_width, y1 = 0;
    float x2 = 0, y2 = m_height;
    float x3 = m_width, y3 = m_height;
    
    auto rotX = [&](float x, float y) { return x*cosT - y*sinT; };
    auto rotY = [&](float x, float y) { return x*sinT + y*cosT; };
    
    float rx0 = rotX(x0,y0), ry0 = rotY(x0,y0);
    float rx1 = rotX(x1,y1), ry1 = rotY(x1,y1);
    float rx2 = rotX(x2,y2), ry2 = rotY(x2,y2);
    float rx3 = rotX(x3,y3), ry3 = rotY(x3,y3);
    
    float minX = std::min({rx0, rx1, rx2, rx3});
    float maxX = std::max({rx0, rx1, rx2, rx3});
    float minY = std::min({ry0, ry1, ry2, ry3});
    float maxY = std::max({ry0, ry1, ry2, ry3});
    
    int newW = static_cast<int>(std::ceil(maxX - minX));
    int newH = static_cast<int>(std::ceil(maxY - minY));
    
    std::vector<float> newData(newW * newH * m_channels, 0.0f);
    
    float centerX = m_width / 2.0f;
    float centerY = m_height / 2.0f;
    float newCenterX = newW / 2.0f;
    float newCenterY = newH / 2.0f;
    
    // Updated to use blendResult
    ImageBuffer original;
    if (hasMask()) original = *this;

    #pragma omp parallel for
    for (int y = 0; y < newH; ++y) {
        for (int x = 0; x < newW; ++x) {
            // Inverse mapping
            float dx = x - newCenterX;
            float dy = y - newCenterY;
            
            // Rotate back by -theta (so +theta in matrix) to find src
            float srcX = dx * std::cos(-theta) - dy * std::sin(-theta) + centerX;
            float srcY = dx * std::sin(-theta) + dy * std::cos(-theta) + centerY;
            
            // Bilinear Interpolation
            if (srcX >= 0 && srcX < m_width - 1 && srcY >= 0 && srcY < m_height - 1) {
                int px = static_cast<int>(srcX);
                int py = static_cast<int>(srcY);
                float fx = srcX - px;
                float fy = srcY - py;
                
                int idx00 = (py * m_width + px) * m_channels;
                int idx01 = ((py) * m_width + (px+1)) * m_channels;
                int idx10 = ((py+1) * m_width + px) * m_channels;
                int idx11 = ((py+1) * m_width + (px+1)) * m_channels;
                
                for (int c = 0; c < m_channels; ++c) {
                    float v00 = m_data[idx00 + c];
                    float v01 = m_data[idx01 + c]; // Bug: idx01 might be out of row if px=w-1? Handled by m_width-1 check
                    float v10 = m_data[idx10 + c];
                    float v11 = m_data[idx11 + c];
                    
                    float top = v00 * (1 - fx) + v01 * fx;
                    float bot = v10 * (1 - fx) + v11 * fx;
                    float val = top * (1 - fy) + bot * fy;
                    
                    newData[(y * newW + x) * m_channels + c] = val;
                }
            }
        }
    }
    
    m_width = newW;
    m_height = newH;
    m_data = newData;

    // Apply geometry transform to MASK if present
    if (hasMask()) {
        // We reuse the rotate logic but on the mask data.
        // It's a single channel float image.
        // Easiest way: create a temp ImageBuffer for mask, rotate it, put back.
        ImageBuffer maskBuf;
        maskBuf.setData(original.width(), original.height(), 1, m_mask.data);
        maskBuf.rotate(angleDegrees);
        m_mask.data = maskBuf.data();
        m_mask.width = maskBuf.width();
        m_mask.height = maskBuf.height();
        // No blending for geometry! We just moved the pixels.
    }

    // Update WCS for Rotation
    // Transformation: P_dest = Rot(theta) * (P_src - C_src) + C_dest
    // theta is POSITIVE form of angle (since theta in code was inverted)
    // Wait, theta in code is -angleDegrees * rad.
    // Let's use own theta for WCS to be clear.
    float wcsTheta = -theta; // This is the rotation angle applied to image
    float c = std::cos(wcsTheta);
    float s = std::sin(wcsTheta);
    
    // CD_new = CD_old * Rot(-wcsTheta)
    // Rot(-wcsTheta) = [[c, s], [-s, c]]
    double ncd1_1 = m_meta.cd1_1 * c + m_meta.cd1_2 * -s;
    double ncd1_2 = m_meta.cd1_1 * s + m_meta.cd1_2 * c;
    double ncd2_1 = m_meta.cd2_1 * c + m_meta.cd2_2 * -s;
    double ncd2_2 = m_meta.cd2_1 * s + m_meta.cd2_2 * c;
    
    m_meta.cd1_1 = ncd1_1; m_meta.cd1_2 = ncd1_2;
    m_meta.cd2_1 = ncd2_1; m_meta.cd2_2 = ncd2_2;
    
    // CRPIX_new = Rot(wcsTheta) * (CRPIX_old - C_src) + C_dest
    // Rot(wcsTheta) = [[c, -s], [s, c]]
    double ox = m_meta.crpix1 - centerX;
    double oy = m_meta.crpix2 - centerY;
    
    m_meta.crpix1 = (ox * c - oy * s) + newCenterX;
    m_meta.crpix2 = (ox * s + oy * c) + newCenterY;
}

void ImageBuffer::applySCNR(float amount, int method) {
    if (m_data.empty() || m_channels < 3) return; // Only works on Color images

    ImageBuffer original;
    if (hasMask()) original = *this;

    long total = static_cast<long>(m_width) * m_height;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long idx = i * m_channels;
        float r = m_data[idx + 0];
        float g = m_data[idx + 1];
        float b = m_data[idx + 2];
        
        float ref = 0.0f;
        switch (method) {
            case 0: // Average Neutral
                ref = (r + b) / 2.0f;
                break;
            case 1: // Maximum Neutral
                ref = std::max(r, b);
                break;
            case 2: // Minimum Neutral
                ref = std::min(r, b);
                break;
            default:
                ref = (r + b) / 2.0f;
        }
        
        // SCNR Logic: If Green > Ref, reduce it.
        // mask = max(0, g - ref)
        // g_new = g - amount * mask
        
        float mask = std::max(0.0f, g - ref);
        float g_new = g - amount * mask;
        
        m_data[idx + 1] = g_new;
    }

    if (hasMask()) {
        blendResult(original);
    }
}

void ImageBuffer::cropRotated(float cx, float cy, float w, float h, float angleDegrees) {
    if (m_data.empty()) return;
    if (w <= 1 || h <= 1) return;

    // Output size is fixed to w, h
    int outW = static_cast<int>(w);
    int outH = static_cast<int>(h);
    
    // Updated to use blendResult
    ImageBuffer original;
    if (hasMask()) original = *this;
    
    std::vector<float> newData(outW * outH * m_channels);
    
    float theta = angleDegrees * 3.14159265f / 180.0f; // Radians (Positive for visual CW match)
    float cosT = std::cos(theta);
    float sinT = std::sin(theta);
    
    float halfW = w / 2.0f;
    float halfH = h / 2.0f;
    
    // Center of source: cx, cy
    
    #pragma omp parallel for
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            // Coord relative to center of new image
            float dx = x - halfW;
            float dy = y - halfH;
            
            // Rotate back to align with source axes
            float srcDX = dx * cosT - dy * sinT;
            float srcDY = dx * sinT + dy * cosT;
            
            // Add source center
            float srcX = cx + srcDX;
            float srcY = cy + srcDY;
            
            // Bilinear Interp
            if (srcX >= 0 && srcX < m_width - 1 && srcY >= 0 && srcY < m_height - 1) {
                int px = static_cast<int>(srcX);
                int py = static_cast<int>(srcY);
                float fx = srcX - px;
                float fy = srcY - py;
                
                int idx00 = (py * m_width + px) * m_channels;
                int idx01 = ((py) * m_width + (px+1)) * m_channels;
                int idx10 = ((py+1) * m_width + px) * m_channels;
                int idx11 = ((py+1) * m_width + (px+1)) * m_channels;
                
                for (int c = 0; c < m_channels; ++c) {
                    float v00 = m_data[idx00 + c];
                    float v01 = m_data[idx01 + c];
                    float v10 = m_data[idx10 + c];
                    float v11 = m_data[idx11 + c];
                    
                    float top = v00 * (1 - fx) + v01 * fx;
                    float bot = v10 * (1 - fx) + v11 * fx;
                    float val = top * (1 - fy) + bot * fy;
                    
                    newData[(y * outW + x) * m_channels + c] = val;
                }
            } else {
                // Background color (Black)
                for (int c = 0; c < m_channels; ++c) {
                    newData[(y * outW + x) * m_channels + c] = 0.0f;
                }
            }
        }
    }
    
    m_width = outW;
    m_height = outH;
    m_data = newData;

    // Update WCS for CropRotated
    // Code uses theta = angleDegrees * rad (positive)
    // Mapping matches: P_dest corresponds to Rot(-theta) P_src ..
    // Let's use Same Logic as Rotate:
    // P_dest = Rot(theta) * (P_src - C_src) + C_dest ?? 
    // In code: src = Rot(-theta) * dest + ...
    // => dest = Rot(theta) * (src - ...)
    // So Rotation is theta.
    
    float c = std::cos(theta);
    float s = std::sin(theta);
    
    // CD_new = CD_old * Rot(-theta)
    // Rot(-theta) = [[c, s], [-s, c]]
    double ncd1_1 = m_meta.cd1_1 * c + m_meta.cd1_2 * -s;
    double ncd1_2 = m_meta.cd1_1 * s + m_meta.cd1_2 * c;
    double ncd2_1 = m_meta.cd2_1 * c + m_meta.cd2_2 * -s;
    double ncd2_2 = m_meta.cd2_1 * s + m_meta.cd2_2 * c;
    
    m_meta.cd1_1 = ncd1_1; m_meta.cd1_2 = ncd1_2;
    m_meta.cd2_1 = ncd2_1; m_meta.cd2_2 = ncd2_2;
    
    // CRPIX_new = Rot(theta) * (CRPIX_old - C_src) + C_dest
    // C_src = (cx, cy)
    // C_dest = (w/2, h/2)
    // Rot(theta) = [[c, -s], [s, c]]
    double ox = m_meta.crpix1 - cx;
    double oy = m_meta.crpix2 - cy;
    
    m_meta.crpix1 = (ox * c - oy * s) + halfW;
    m_meta.crpix2 = (ox * s + oy * c) + halfH;
    
    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(original.width(), original.height(), 1, m_mask.data);
        maskBuf.cropRotated(cx, cy, w, h, angleDegrees);
        m_mask.data = maskBuf.data();
        m_mask.width = maskBuf.width();
        m_mask.height = maskBuf.height();
    }
}

float ImageBuffer::getPixelValue(int x, int y, int c) const {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height || c < 0 || c >= m_channels) return 0.0f;
    return m_data[(static_cast<size_t>(y) * m_width + x) * m_channels + c];
}

float ImageBuffer::getPixelFlat(size_t index, int c) const {
    if (m_data.empty()) return 0.0f;
    if (m_channels == 1) return m_data[index];
    return m_data[index * m_channels + c];
}

float ImageBuffer::getChannelMedian(int channelIndex) const {
    if (m_data.empty()) return 0.0f;
    
    // Create view of channel data
    std::vector<float> chData;
    chData.reserve(m_data.size() / m_channels);
    int ch = m_channels;
    for (size_t i = channelIndex; i < m_data.size(); i += ch) {
        chData.push_back(m_data[i]);
    }
    
    return RobustStatistics::getMedian(chData);
}

float ImageBuffer::getAreaMean(int x, int y, int w, int h, int c) const {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > m_width) w = m_width - x;
    if (y + h > m_height) h = m_height - y;
    if (w <= 0 || h <= 0) return 0.0f;
    
    double sum = 0.0;
    long count = static_cast<long>(w) * h;
    
    // Flattened loop for simpler OpenMP reduction
    #pragma omp parallel for reduction(+:sum)
    for (int iy = y; iy < y + h; ++iy) {
        for (int ix = x; ix < x + w; ++ix) {
             size_t idx = (static_cast<size_t>(iy) * m_width + ix) * m_channels + c;
             sum += m_data[idx];
        }
    }
    return (count > 0) ? (float)(sum / count) : 0.0f;
}

void ImageBuffer::computeClippingStats(long& lowClip, long& highClip) const {
    lowClip = 0;
    highClip = 0;
    
    long tempLow = 0;
    long tempHigh = 0;
    size_t n = m_data.size();
    
    #pragma omp parallel for reduction(+:tempLow, tempHigh)
    for (size_t i = 0; i < n; ++i) {
        float v = m_data[i];
        if (v <= 0.0f) tempLow++;
        else if (v >= 1.0f) tempHigh++;
    }
    
    lowClip = tempLow;
    highClip = tempHigh;
}

std::vector<std::vector<int>> ImageBuffer::computeHistogram(int bins) const {
    if (m_data.empty() || bins <= 0) return {};
    
    int numThreads = omp_get_max_threads();
    if (numThreads < 1) numThreads = 1;
    
    std::vector<std::vector<std::vector<int>>> localHists(numThreads, 
        std::vector<std::vector<int>>(m_channels, std::vector<int>(bins, 0)));
    
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        #pragma omp for
        for (long long i = 0; i < (long long)m_data.size(); ++i) { // Process all pixels
            int c = i % m_channels;
            float v = m_data[i];
            
            // Fast clamp
            if (v < 0.0f) v = 0.0f;
            else if (v > 1.0f) v = 1.0f;
            
            int b = static_cast<int>(v * (bins - 1) + 0.5f);
            localHists[tid][c][b]++;
        }
    }
    
    std::vector<std::vector<int>> hist(m_channels, std::vector<int>(bins, 0));
    for (int t = 0; t < numThreads; ++t) {
        for (int c = 0; c < m_channels; ++c) {
            for (int b = 0; b < bins; ++b) {
                hist[c][b] += localHists[t][c][b];
            }
        }
    }
    return hist;
}

void ImageBuffer::rotate90() {
    if (m_data.empty()) return;
    
    int oldW = m_width;
    int oldH = m_height;
    int ch = m_channels;
    std::vector<float> newData(oldW * oldH * ch);
    
    #pragma omp parallel for
    for (int y = 0; y < oldH; ++y) {
        for (int x = 0; x < oldW; ++x) {
            int newX = oldH - 1 - y;
            int newY = x;
            int oldIdx = (y * oldW + x) * ch;
            int newIdx = (newY * oldH + newX) * ch;
            for (int c = 0; c < ch; ++c) {
                newData[newIdx + c] = m_data[oldIdx + c];
            }
        }
    }
    
    m_width = oldH;
    m_height = oldW;
    m_data = std::move(newData);

    // Update WCS for Rotate 90 CW
    // x' = H - 1 - y  => y = H - 1 - x'
    // y' = x          => x = y'
    // Matrix M = [[0, 1], [-1, 0]]
    // CD_new = CD_old * M
    double ncd1_1 = m_meta.cd1_1 * 0.0 + m_meta.cd1_2 * -1.0;
    double ncd1_2 = m_meta.cd1_1 * 1.0 + m_meta.cd1_2 * 0.0;
    double ncd2_1 = m_meta.cd2_1 * 0.0 + m_meta.cd2_2 * -1.0;
    double ncd2_2 = m_meta.cd2_1 * 1.0 + m_meta.cd2_2 * 0.0;
    
    m_meta.cd1_1 = ncd1_1; m_meta.cd1_2 = ncd1_2;
    m_meta.cd2_1 = ncd2_1; m_meta.cd2_2 = ncd2_2;
    
    // CRPIX Update: x' = H - 1 - y, y' = x
    double nx = oldH - 1 - m_meta.crpix2;
    double ny = m_meta.crpix1;
    m_meta.crpix1 = nx;
    m_meta.crpix2 = ny;

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.rotate90();
        m_mask.data = maskBuf.data();
        m_mask.width = maskBuf.width();
        m_mask.height = maskBuf.height();
    }
}

void ImageBuffer::rotate180() {
    if (m_data.empty()) return;
    
    int h = m_height;
    int w = m_width;
    int ch = m_channels;
    
    #pragma omp parallel for
    for (int y = 0; y < h / 2; ++y) {
        int y2 = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            int x2 = w - 1 - x;
            int idx1 = (y * w + x) * ch;
            int idx2 = (y2 * w + x2) * ch;
            for (int c = 0; c < ch; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }
    
    // Handle middle row if height is odd
    if (h % 2 != 0) {
        int y = h / 2;
        for (int x = 0; x < w / 2; ++x) {
            int x2 = w - 1 - x;
            int idx1 = (y * w + x) * ch;
            int idx2 = (y * w + x2) * ch;
            for (int c = 0; c < ch; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }
    
    // Update WCS for Rotate 180
    // Matrix M = [[-1, 0], [0, -1]]
    m_meta.cd1_1 *= -1.0; m_meta.cd1_2 *= -1.0;
    m_meta.cd2_1 *= -1.0; m_meta.cd2_2 *= -1.0;
    
    m_meta.crpix1 = w - 1 - m_meta.crpix1;
    m_meta.crpix2 = h - 1 - m_meta.crpix2;

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(w, h, 1, m_mask.data);
        maskBuf.rotate180();
        m_mask.data = maskBuf.data();
        m_mask.width = w;
        m_mask.height = h;
    }
}

void ImageBuffer::rotate270() {
    if (m_data.empty()) return;
    
    int oldW = m_width;
    int oldH = m_height;
    int ch = m_channels;
    std::vector<float> newData(oldW * oldH * ch);
    
    #pragma omp parallel for
    for (int y = 0; y < oldH; ++y) {
        for (int x = 0; x < oldW; ++x) {
            int newX = y;
            int newY = oldW - 1 - x;
            int oldIdx = (y * oldW + x) * ch;
            int newIdx = (newY * oldH + newX) * ch;
            for (int c = 0; c < ch; ++c) {
                newData[newIdx + c] = m_data[oldIdx + c];
            }
        }
    }
    
    m_width = oldH;
    m_height = oldW;
    m_data = std::move(newData);
    
    // Update WCS for Rotate 270 CW (90 CCW)
    // x' = y
    // y' = W - 1 - x
    // Matrix M = [[0, -1], [1, 0]]
    double ncd1_1 = m_meta.cd1_1 * 0.0 + m_meta.cd1_2 * 1.0;
    double ncd1_2 = m_meta.cd1_1 * -1.0 + m_meta.cd1_2 * 0.0;
    double ncd2_1 = m_meta.cd2_1 * 0.0 + m_meta.cd2_2 * 1.0;
    double ncd2_2 = m_meta.cd2_1 * -1.0 + m_meta.cd2_2 * 0.0;
    
    m_meta.cd1_1 = ncd1_1; m_meta.cd1_2 = ncd1_2;
    m_meta.cd2_1 = ncd2_1; m_meta.cd2_2 = ncd2_2;
    
    double nx = m_meta.crpix2;
    double ny = oldW - 1 - m_meta.crpix1;
    m_meta.crpix1 = nx;
    m_meta.crpix2 = ny;

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.rotate270();
        m_mask.data = maskBuf.data();
        m_mask.width = maskBuf.width();
        m_mask.height = maskBuf.height();
    }
}


void ImageBuffer::mirrorX() {
    if (m_data.empty()) return;
    
    #pragma omp parallel for
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width / 2; ++x) {
            int x2 = m_width - 1 - x;
            
            size_t idx1 = (y * m_width + x) * m_channels;
            size_t idx2 = (y * m_width + x2) * m_channels;
            
            for (int c = 0; c < m_channels; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }
    
    // Update WCS for Mirror X
    // Matrix M = [[-1, 0], [0, 1]]
    m_meta.cd1_1 *= -1.0; 
    m_meta.cd2_1 *= -1.0; 
    
    m_meta.crpix1 = m_width - 1 - m_meta.crpix1;

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(m_width, m_height, 1, m_mask.data);
        maskBuf.mirrorX();
        m_mask.data = maskBuf.data();
    }
}



void ImageBuffer::mirrorY() {
    if (m_data.empty()) return;
    
    int h = m_height;
    int w = m_width;
    int ch = m_channels;
    
    #pragma omp parallel for
    for (int y = 0; y < h / 2; ++y) {
        int y2 = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            int idx1 = (y * w + x) * ch;
            int idx2 = (y2 * w + x) * ch;
            for (int c = 0; c < ch; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }
    
    // Update WCS for Mirror Y
    // Matrix M = [[1, 0], [0, -1]]
    m_meta.cd1_2 *= -1.0;
    m_meta.cd2_2 *= -1.0;
    
    m_meta.crpix2 = m_height - 1 - m_meta.crpix2;

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(m_width, m_height, 1, m_mask.data);
        maskBuf.mirrorY();
        m_mask.data = maskBuf.data();
    }
}

void ImageBuffer::multiply(float factor) {
    if (m_data.empty()) return;
    #pragma omp parallel for
    for (size_t i = 0; i < m_data.size(); ++i) {
        m_data[i] = std::max(0.0f, std::min(1.0f, m_data[i] * factor));
    }
}

void ImageBuffer::subtract(float r, float g, float b) {
    if (m_data.empty()) return;
    
    int ch = m_channels;
    long total = static_cast<long>(m_width) * m_height;
    
    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        size_t idx = i * ch;
        if (ch == 1) {
            m_data[idx] = std::max(0.0f, m_data[idx] - r); // Use r for mono
        } else {
            m_data[idx + 0] = std::max(0.0f, m_data[idx + 0] - r);
            m_data[idx + 1] = std::max(0.0f, m_data[idx + 1] - g);
            m_data[idx + 2] = std::max(0.0f, m_data[idx + 2] - b);
        }
    }
}

float ImageBuffer::getChannelMAD(int channelIndex, float median) const {
    if (m_data.empty()) return 0.0f;
    
    // Extract channel data to pass to RobustStatistics
    // (We could optimize RobustStatistics to take stride, but copying is acceptable for O(N))
    std::vector<float> chData;
    chData.reserve(m_data.size() / m_channels);
    int ch = m_channels;
    for (size_t i = channelIndex; i < m_data.size(); i += ch) {
        chData.push_back(m_data[i]);
    }
    
    return RobustStatistics::getMAD(chData, median);
}

float ImageBuffer::getRobustMedian(int channelIndex, float t0, float t1) const {
    if (m_data.empty()) return 0.0f;
    
    std::vector<float> chData;
    chData.reserve(m_data.size() / m_channels);
    int ch = m_channels;
    for (size_t i = channelIndex; i < m_data.size(); i += ch) {
        chData.push_back(m_data[i]);
    }
    
    float med = RobustStatistics::getMedian(chData);
    float mad = RobustStatistics::getMAD(chData, med);
    
    float sigma = 1.4826f * mad;
    float lower = med + t0 * sigma; 
    float upper = med + t1 * sigma;

    // Filter
    std::vector<float> valid;
    valid.reserve(chData.size());
    for (float v : chData) {
        if (v >= lower && v <= upper) valid.push_back(v);
    }
    
    return RobustStatistics::getMedian(valid);
}

void ImageBuffer::applyPCC(float kr, float kg, float kb, float br, float bg, float bb, float bg_mean) {
    if (m_data.empty() || m_channels < 3) return;
    
    ImageBuffer original;
    if (hasMask()) original = *this;

    // Standard Formula:
    // P' = (P - Bg) * K + Bg_Mean
    //    = P*K - Bg*K + Bg_Mean
    //    = P*K + (Bg_Mean - Bg*K)
    
    float offsetR = bg_mean - br * kr;
    float offsetG = bg_mean - bg * kg;
    float offsetB = bg_mean - bb * kb;
    
    long total = static_cast<long>(m_width) * m_height;
    
    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long idx = i * 3;
        
        float r = m_data[idx + 0] * kr + offsetR;
        float g = m_data[idx + 1] * kg + offsetG;
        float b = m_data[idx + 2] * kb + offsetB;
        
        // Do NOT clamp to [0,1]. Preserving dynamic range (including negatives) is crucial 
        // for subsequent processing (like Background Extraction or AutoStretch).
        m_data[idx + 0] = std::clamp(r, 0.0f, 1.0f);
        m_data[idx + 1] = std::clamp(g, 0.0f, 1.0f);
        m_data[idx + 2] = std::clamp(b, 0.0f, 1.0f);
    }

    if (hasMask()) {
        blendResult(original);
    }
}

void ImageBuffer::applySpline(const SplineData& spline, const bool channels[3]) {
    if (m_data.empty()) return;
    if (spline.n < 2) return;
    
    ImageBuffer original;
    if (hasMask()) original = *this;

    int ch = m_channels;
    long total = static_cast<long>(m_width) * m_height;
    
    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long idx = i * ch;
        for (int c = 0; c < ch; ++c) {
             bool apply = false;
             if (ch == 3) {
                 apply = channels[c];
             } else if (ch == 1) {
                 apply = channels[0]; 
             } else {
                 apply = (c < 3) ? channels[c] : true;
             }
             
             if (!apply) continue;
             
             float v = m_data[idx + c];
             float out = CubicSpline::interpolate(v, spline);
             m_data[idx + c] = out;
        }
    }
    
    if (hasMask()) {
        blendResult(original);
    }
}



// ------ Wavelet & Star Extraction Implementations ------

// B3 Kernel: [1, 4, 6, 4, 1] / 16
static const std::vector<float> KERNEL_B3 = { 1.0f/16.0f, 4.0f/16.0f, 6.0f/16.0f, 4.0f/16.0f, 1.0f/16.0f };

std::vector<float> ImageBuffer::convolveSepReflect(const std::vector<float>& src, int w, int h, const std::vector<float>& kernel, int scale) {
    if (src.empty() || kernel.empty()) return src;
    
    std::vector<float> tmp(static_cast<size_t>(w) * h);
    std::vector<float> out(static_cast<size_t>(w) * h);
    
    int kSize = (int)kernel.size();
    int center = kSize / 2;
    int step = 1 << scale; // 2^scale spacing
    
    // Horizontal Pass
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        long rowOff = (long)y * w;
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < kSize; ++k) {
                int offset = (k - center) * step;
                int sx = x + offset;
                // Reflect padding
                if (sx < 0) sx = -sx;
                if (sx >= w) sx = 2 * w - 2 - sx;
                if (sx < 0) sx = 0; // fallback len 1
                
                sum += src[rowOff + sx] * kernel[k];
            }
            tmp[rowOff + x] = sum;
        }
    }
    
    // Vertical Pass
    #pragma omp parallel for
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            float sum = 0.0f;
            for (int k = 0; k < kSize; ++k) {
                int offset = (k - center) * step;
                int sy = y + offset;
                // Reflect padding
                if (sy < 0) sy = -sy;
                if (sy >= h) sy = 2 * h - 2 - sy;
                if (sy < 0) sy = 0;
                
                sum += tmp[(long)sy * w + x] * kernel[k];
            }
            out[(long)y * w + x] = sum;
        }
    }
    return out;
}

std::vector<std::vector<float>> ImageBuffer::atrousDecompose(const std::vector<float>& src, int w, int h, int n_scales) {
    std::vector<std::vector<float>> planes;
    if (src.empty()) return planes;
    
    std::vector<float> current = src;
    
    for (int s = 0; s < n_scales; ++s) {
        // Smooth
        std::vector<float> smooth = convolveSepReflect(current, w, h, KERNEL_B3, s);
        
        // Detail = Current - Smooth
        std::vector<float> detail(static_cast<size_t>(w) * h);
        #pragma omp parallel for
        for (size_t i = 0; i < detail.size(); ++i) {
            detail[i] = current[i] - smooth[i];
        }
        planes.push_back(detail);
        
        current = smooth;
    }
    planes.push_back(current); // Residual
    return planes;
}

std::vector<float> ImageBuffer::atrousReconstruct(const std::vector<std::vector<float>>& planes, int w, int h) {
    if (planes.empty()) return {};
    std::vector<float> out(static_cast<size_t>(w) * h, 0.0f);
    
    for (const auto& plane : planes) {
        #pragma omp parallel for
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] += plane[i];
        }
    }
    return out;
}

// Star Extraction Implementation
std::vector<ImageBuffer::DetectedStar> ImageBuffer::extractStars(const std::vector<float>& src, int w, int h, float sigma, int minArea) {
    std::vector<DetectedStar> stars;
    if (src.empty()) return stars;

    // 1. Background Estimation (Simple Sigma Clipping Median from helper)
    ChStats stats = computeStats(src, w, h, 1, 0); 
    float bg = stats.median;
    float rms = stats.mad;
    
    float thresholdVal = bg + sigma * rms;
    
    // 2. Thresholding + Connected Components
    // Visited array
    std::vector<uint8_t> visited(static_cast<size_t>(w) * h, 0);
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            long idx = (long)y * w + x;
            if (visited[idx]) continue;
            
            float v = src[idx];
            if (v > thresholdVal) {
                // Found new component, flood fill
                std::vector<std::pair<int, int>> stack; // Vector as stack
                stack.push_back({x, y});
                visited[idx] = 1;
                
                double sumFlux = 0;
                double sumX = 0, sumY = 0;
                double sumX2 = 0, sumY2 = 0, sumXY = 0;
                float peak = v;
                int count = 0;
                
                while (!stack.empty()) {
                    auto p = stack.back();
                    stack.pop_back();
                    int cx = p.first;
                    int cy = p.second;
                    long cidx = (long)cy * w + cx;
                    
                    float val = src[cidx] - bg; // Subtract background for proper flux weighting
                    if (val < 0) val = 0; 
                    
                    if (src[cidx] > peak) peak = src[cidx];
                    
                    sumFlux += val;
                    sumX += cx * val;
                    sumY += cy * val;
                    sumX2 += cx * cx * val;
                    sumY2 += cy * cy * val;
                    sumXY += cx * cy * val;
                    count++;
                    
                    // Neighbors (4-connectivity)
                    const int dx[] = {1, -1, 0, 0};
                    const int dy[] = {0, 0, 1, -1};
                    
                    for (int k = 0; k < 4; ++k) {
                        int nx = cx + dx[k];
                        int ny = cy + dy[k];
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            long nidx = (long)ny * w + nx;
                            if (!visited[nidx] && src[nidx] > thresholdVal) {
                                visited[nidx] = 1;
                                stack.push_back({nx, ny});
                            }
                        }
                    }
                }
                
                if (count >= minArea && sumFlux > 0) {
                    double mx = sumX / sumFlux;
                    double my = sumY / sumFlux;
                    
                    // Moments for shape
                    double u20 = (sumX2 / sumFlux) - (mx * mx);
                    double u02 = (sumY2 / sumFlux) - (my * my);
                    double u11 = (sumXY / sumFlux) - (mx * my);
                    
                    // Eigenvalues of covariance matrix
                    double delta = std::sqrt(std::abs((u20 - u02)*(u20 - u02) + 4 * u11*u11));
                    double lam1 = (u20 + u02 + delta) / 2.0;
                    double lam2 = (u20 + u02 - delta) / 2.0;
                    
                    float a = (float)std::sqrt(std::max(0.0, lam1));
                    float b = (float)std::sqrt(std::max(0.0, lam2));
                    
                    // Theta
                    float theta = (float)(0.5 * std::atan2(2.0 * u11, u20 - u02));
                    
                    // HFR proxy
                    float hfr = 2.0f * a; 
                    
                    DetectedStar star;
                    star.x = (float)mx;
                    star.y = (float)my;
                    star.flux = (float)sumFlux;
                    star.peak = peak;
                    star.a = a;
                    star.b = b;
                    star.theta = theta;
                    star.hfr = hfr; 
                    
                    stars.push_back(star);
                }
            }
        }
    }
    return stars;
}




// HSL Conversion Helpers for Saturation Tool
static void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxv = std::max({r, g, b});
    float minv = std::min({r, g, b});
    l = (maxv + minv) / 2.0f;
    if (maxv == minv) {
        h = s = 0.0f;
    } else {
        float d = maxv - minv;
        s = l > 0.5f ? d / (2.0f - maxv - minv) : d / (maxv + minv);
        if (maxv == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (maxv == g) h = (b - r) / d + 2.0f;
        else if (maxv == b) h = (r - g) / d + 4.0f;
        h *= 60.0f;
    }
}

static float hueToRgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

static void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.0f) {
        r = g = b = l;
    } else {
        h /= 360.0f;
        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        r = hueToRgb(p, q, h + 1.0f/3.0f);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.0f/3.0f);
    }
}

void ImageBuffer::applySaturation(const SaturationParams& params) {
    if (m_channels != 3 || m_data.empty()) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    #pragma omp parallel for
    for (int i = 0; i < (int)(m_data.size() / 3); ++i) {
        float r = m_data[i * 3 + 0];
        float g = m_data[i * 3 + 1];
        float b = m_data[i * 3 + 2];

        // Cache original values for safe clamping (avoid race condition)
        float origR = r, origG = g, origB = b;

        // HDR-Safe Saturation: Work on chrominance directly
        // Luminance (simple average for color-neutral weighting)
        float lum = (r + g + b) / 3.0f;
        
        // Chrominance (deviation from gray)
        float cr = r - lum;
        float cg = g - lum;
        float cb = b - lum;
        
        // Compute hue from chrominance for selective coloring
        // Using atan2 on a/b style channels (simplified)
        float hue = 0.0f;
        float chroma = std::sqrt(cr*cr + cg*cg + cb*cb);
        
        if (chroma > 1e-7f) {
            // Approximate hue from RGB deviation
            // Red=0, Green=120, Blue=240
            hue = std::atan2(std::sqrt(3.0f) * (cg - cb), 2.0f * cr - cg - cb);
            hue = hue * 180.0f / 3.14159265f; // Convert to degrees
            if (hue < 0.0f) hue += 360.0f;
        }

        // Compute Hue Weighting
        float hueWeight = 1.0f;
        if (params.hueWidth < 359.0f) {
            float d = std::abs(hue - params.hueCenter);
            if (d > 180.0f) d = 360.0f - d;
            
            float halfWidth = params.hueWidth / 2.0f;
            if (d <= halfWidth) hueWeight = 1.0f;
            else if (d >= halfWidth + params.hueSmooth) hueWeight = 0.0f;
            else hueWeight = 1.0f - (d - halfWidth) / params.hueSmooth;
        }

        // Background masking: Reduce effect in dark areas
        // Use luminance clamped to [0,1] for mask calculation
        float lumClamped = std::max(0.0f, std::min(1.0f, lum));
        float mask = std::pow(lumClamped, params.bgFactor);
        
        // Compute effective boost
        float boost = 1.0f + (params.amount - 1.0f) * mask * hueWeight;
        
        // Clamp boost to prevent extreme inversion
        boost = std::max(0.0f, boost);

        // Apply boost to chrominance
        cr *= boost;
        cg *= boost;
        cb *= boost;

        // Reconstruct RGB
        r = lum + cr;
        g = lum + cg;
        b = lum + cb;
        
        // Clamp output to [0, max_original] to prevent runaway values
        // but preserve HDR headroom (use cached originals, not m_data)
        float maxOrig = std::max({origR, origG, origB, 1.0f});
        r = std::max(0.0f, std::min(maxOrig, r));
        g = std::max(0.0f, std::min(maxOrig, g));
        b = std::max(0.0f, std::min(maxOrig, b));

        m_data[i * 3 + 0] = r;
        m_data[i * 3 + 1] = g;
        m_data[i * 3 + 2] = b;
    }
    if (hasMask()) {
        blendResult(original);
    }
    setModified(true);
}

void ImageBuffer::applyArcSinh(float stretchFactor) {
    if (m_data.empty()) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    float norm = 1.0f / std::asinh(stretchFactor);
    
    long total = static_cast<long>(m_width) * m_height;
    int ch = m_channels;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long idx = i * ch;
        for (int c = 0; c < ch; ++c) {
             float v = m_data[idx + c];
             float out = std::asinh(v * stretchFactor) * norm;
             // Clamp? usually not needed if v in [0,1], but good practice
             if (out < 0.0f) out = 0.0f;
             if (out > 1.0f) out = 1.0f;
             m_data[idx + c] = out;
        }
    }

    if (hasMask()) {
        blendResult(original);
    }
}

void ImageBuffer::applyArcSinh(float stretchFactor, float blackPoint, bool humanLuminance) {
    if (m_data.empty()) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    long total = static_cast<long>(m_width) * m_height;
    int channels = m_channels;
    
    // Algorithm constants
    float asinh_beta = std::asinh(stretchFactor);
    float factor_r = humanLuminance ? 0.2126f : 0.3333f;
    float factor_g = humanLuminance ? 0.7152f : 0.3333f;
    float factor_b = humanLuminance ? 0.0722f : 0.3333f;
    float offset = blackPoint;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        if (channels == 3) {
            long idx = i * 3;
            float rv = m_data[idx + 0];
            float gv = m_data[idx + 1];
            float bv = m_data[idx + 2];
            
            // Apply black point
            float rp = std::max(0.0f, (rv - offset) / (1.0f - offset));
            float gp = std::max(0.0f, (gv - offset) / (1.0f - offset));
            float bp = std::max(0.0f, (bv - offset) / (1.0f - offset));
            
            // Compute luminance
            float x = factor_r * rp + factor_g * gp + factor_b * bp;
            float k = (x <= 1e-9f) ? 0.0f : (stretchFactor == 0.0f ? 1.0f : std::asinh(stretchFactor * x) / (x * asinh_beta));
            
            // Apply to each channel
            m_data[idx + 0] = std::min(1.0f, std::max(0.0f, rp * k));
            m_data[idx + 1] = std::min(1.0f, std::max(0.0f, gp * k));
            m_data[idx + 2] = std::min(1.0f, std::max(0.0f, bp * k));
        } else if (channels == 1) {
            float v = m_data[i];
            float xp = std::max(0.0f, (v - offset) / (1.0f - offset));
            float k = (xp <= 1e-9f) ? 0.0f : (stretchFactor == 0.0f ? 1.0f : std::asinh(stretchFactor * xp) / (xp * asinh_beta));
            m_data[i] = std::min(1.0f, std::max(0.0f, xp * k));
        }
    }

    if (hasMask()) {
        blendResult(original);
    }
}
