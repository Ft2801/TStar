#include "StatisticalStretch.h"
#include <algorithm>
#include <numeric>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

// ============================================================================
// Robust Statistics
// ============================================================================

float StatisticalStretch::robustSigmaLowerHalf(const std::vector<float>& data,
                                                int stride, int offset, int channels,
                                                int maxSamples) {
    // Sample data
    std::vector<float> sample;
    size_t limit = data.size();
    int totalStride = stride * channels;
    size_t estimatedSize = limit / totalStride + 1;
    
    // Limit samples for performance
    int actualStride = totalStride;
    if (estimatedSize > static_cast<size_t>(maxSamples)) {
        actualStride = static_cast<int>(limit / maxSamples);
        actualStride = std::max(actualStride, totalStride);
    }
    
    sample.reserve(std::min(estimatedSize, static_cast<size_t>(maxSamples)));
    
    for (size_t i = offset; i < limit; i += actualStride) {
        sample.push_back(data[i]);
    }
    
    if (sample.size() < 16) return 0.0f;
    
    // Compute median
    size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    float median = sample[mid];
    
    // Extract lower half (values <= median)
    std::vector<float> lowerHalf;
    lowerHalf.reserve(sample.size() / 2 + 1);
    for (float v : sample) {
        if (v <= median) lowerHalf.push_back(v);
    }
    
    float mad;
    if (lowerHalf.size() < 16) {
        // Not enough values, use full sample MAD
        for (size_t i = 0; i < sample.size(); ++i) {
            sample[i] = std::abs(sample[i] - median);
        }
        size_t madMid = sample.size() / 2;
        std::nth_element(sample.begin(), sample.begin() + madMid, sample.end());
        mad = sample[madMid];
    } else {
        // Compute MAD of lower half
        size_t loMid = lowerHalf.size() / 2;
        std::nth_element(lowerHalf.begin(), lowerHalf.begin() + loMid, lowerHalf.end());
        float medLower = lowerHalf[loMid];
        
        for (size_t i = 0; i < lowerHalf.size(); ++i) {
            lowerHalf[i] = std::abs(lowerHalf[i] - medLower);
        }
        std::nth_element(lowerHalf.begin(), lowerHalf.begin() + loMid, lowerHalf.end());
        mad = lowerHalf[loMid];
    }
    
    // Convert MAD to sigma equivalent (normal distribution)
    return 1.4826f * mad;
}

StatisticalStretch::ChannelStats StatisticalStretch::computeStats(
        const std::vector<float>& data,
        int stride, int offset, int channels,
        float sigma, bool noBlackClip) {
    
    ChannelStats stats;
    
    // Sample data
    std::vector<float> sample;
    size_t limit = data.size();
    int totalStride = stride * channels;
    
    float minVal = 1e30f;
    
    sample.reserve(limit / totalStride + 100);
    
    for (size_t i = offset; i < limit; i += totalStride) {
        float v = data[i];
        sample.push_back(v);
        if (v < minVal) minVal = v;
    }
    
    if (sample.empty()) return stats;
    
    // Compute median
    size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    float median = sample[mid];
    
    stats.median = median;
    stats.minValue = minVal;
    
    if (noBlackClip) {
        // Use minimum value as black point
        stats.blackpoint = minVal;
    } else {
        // Compute robust noise estimate
        float noise = robustSigmaLowerHalf(data, stride, offset, channels);
        stats.noise = noise;
        
        // Black point = median - sigma * noise
        float bp = median - sigma * noise;
        bp = std::max(minVal, bp);
        bp = std::min(bp, 0.99f);
        stats.blackpoint = bp;
    }
    
    stats.denominator = std::max(1.0f - stats.blackpoint, 1e-12f);
    
    return stats;
}

// ============================================================================
// MTF and Stretch Formula
// ============================================================================

float StatisticalStretch::computeMTFParameter(float currentMedian, float targetMedian) {
    float cb = std::clamp(currentMedian, 1e-6f, 1.0f - 1e-6f);
    float tb = std::clamp(targetMedian, 1e-6f, 1.0f - 1e-6f);
    
    float den = cb * (2.0f * tb - 1.0f) - tb;
    if (std::abs(den) < 1e-12f) den = 1e-12f;
    
    float m = (cb * (tb - 1.0f)) / den;
    return std::clamp(m, 1e-6f, 1.0f - 1e-6f);
}

// ============================================================================
// HDR Highlight Compression
// ============================================================================

void StatisticalStretch::hdrCompressHighlights(std::vector<float>& data,
                                               float amount, float knee) {
    if (amount <= 0.0f) return;
    
    float a = std::clamp(amount, 0.0f, 1.0f);
    float k = std::clamp(knee, 0.0f, 0.99f);
    
    // End slope: a=0 -> m1=1 (identity), a=1 -> m1=5 (strong compression)
    float m1 = 1.0f + 4.0f * a;
    m1 = std::clamp(m1, 1.0f, 5.0f);
    
    size_t total = data.size();
    
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(total); ++i) {
        float y = data[i];
        if (y > k) {
            // Normalize to t in [0,1]
            float t = (y - k) / (1.0f - k);
            t = std::clamp(t, 0.0f, 1.0f);
            
            // Cubic Hermite: p0=0, p1=1, m0=1, m1=m1
            float t2 = t * t;
            float t3 = t2 * t;
            
            float h10 = t3 - 2.0f * t2 + t;   // m0 coefficient
            float h01 = -2.0f * t3 + 3.0f * t2; // p1 coefficient
            float h11 = t3 - t2;              // m1 coefficient
            
            float f = h10 * 1.0f + h01 * 1.0f + h11 * m1;
            f = std::clamp(f, 0.0f, 1.0f);
            
            data[i] = k + (1.0f - k) * f;
        }
        data[i] = std::clamp(data[i], 0.0f, 1.0f);
    }
}

void StatisticalStretch::hdrCompressColorLuminance(std::vector<float>& data,
                                                   int width, int height,
                                                   float amount, float knee, int lumaMode) {
    if (amount <= 0.0f) return;
    
    auto weights = getLumaWeights(lumaMode);
    long pixelCount = static_cast<long>(width) * height;
    
    #pragma omp parallel for
    for (long i = 0; i < pixelCount; ++i) {
        size_t idx = i * 3;
        float r = data[idx];
        float g = data[idx + 1];
        float b = data[idx + 2];
        
        // Compute current luminance
        float L = weights[0] * r + weights[1] * g + weights[2] * b;
        
        if (L > knee && L > 1e-10f) {
            // Compress luminance
            float t = (L - knee) / (1.0f - knee);
            t = std::clamp(t, 0.0f, 1.0f);
            
            float a = std::clamp(amount, 0.0f, 1.0f);
            float m1 = 1.0f + 4.0f * a;
            
            float t2 = t * t;
            float t3 = t2 * t;
            float h10 = t3 - 2.0f * t2 + t;
            float h01 = -2.0f * t3 + 3.0f * t2;
            float h11 = t3 - t2;
            float f = h10 * 1.0f + h01 * 1.0f + h11 * m1;
            f = std::clamp(f, 0.0f, 1.0f);
            
            float Lc = knee + (1.0f - knee) * f;
            
            // Scale colors by ratio of compressed/original luminance
            float scale = Lc / L;
            data[idx] = std::clamp(r * scale, 0.0f, 1.0f);
            data[idx + 1] = std::clamp(g * scale, 0.0f, 1.0f);
            data[idx + 2] = std::clamp(b * scale, 0.0f, 1.0f);
        }
    }
}

// ============================================================================
// High-Range Rescaling
// ============================================================================

void StatisticalStretch::highRangeRescale(std::vector<float>& data,
                                          int width, int height, int channels,
                                          float targetMedian,
                                          float pedestal, float softCeilPct, float hardCeilPct,
                                          float floorSigma, float softclipThreshold) {
    long pixelCount = static_cast<long>(width) * height;
    
    // Compute luminance for stats (mono or RGB)
    std::vector<float> luminance(pixelCount);
    
    if (channels == 1) {
        for (long i = 0; i < pixelCount; ++i) {
            luminance[i] = data[i];
        }
    } else {
        auto weights = getLumaWeights(0);  // Rec709
        for (long i = 0; i < pixelCount; ++i) {
            size_t idx = i * channels;
            luminance[i] = weights[0] * data[idx] + weights[1] * data[idx + 1] + weights[2] * data[idx + 2];
        }
    }
    
    // Robust floor calculation
    std::vector<float> sample(luminance);
    size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    float median = sample[mid];
    
    float noise = robustSigmaLowerHalf(luminance, 1, 0, 1);
    float globalFloor = median - floorSigma * noise;
    float minVal = *std::min_element(luminance.begin(), luminance.end());
    globalFloor = std::max(globalFloor, minVal);
    
    // Percentile ceilings (subsample for speed)
    int stride = std::max(1, static_cast<int>(luminance.size()) / 500000);
    std::vector<float> samplePerc;
    for (size_t i = 0; i < luminance.size(); i += stride) {
        samplePerc.push_back(luminance[i]);
    }
    std::sort(samplePerc.begin(), samplePerc.end());
    
    size_t softIdx = static_cast<size_t>(softCeilPct / 100.0f * samplePerc.size());
    size_t hardIdx = static_cast<size_t>(hardCeilPct / 100.0f * samplePerc.size());
    softIdx = std::min(softIdx, samplePerc.size() - 1);
    hardIdx = std::min(hardIdx, samplePerc.size() - 1);
    
    float softCeil = samplePerc[softIdx];
    float hardCeil = samplePerc[hardIdx];
    
    if (softCeil <= globalFloor) softCeil = globalFloor + 1e-6f;
    if (hardCeil <= softCeil) hardCeil = softCeil + 1e-6f;
    
    float ped = std::clamp(pedestal, 0.0f, 0.05f);
    
    // Compute scale
    float scaleContrast = (0.98f - ped) / (softCeil - globalFloor + 1e-12f);
    float scaleSafety = (1.0f - ped) / (hardCeil - globalFloor + 1e-12f);
    float s = std::min(scaleContrast, scaleSafety);
    
    // Apply rescale
    size_t total = data.size();
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(total); ++i) {
        float y = (data[i] - globalFloor) * s + ped;
        data[i] = std::clamp(y, 0.0f, 1.0f);
    }
    
    // Recompute median and apply MTF
    if (targetMedian > 0.0f && targetMedian < 1.0f) {
        // Recompute current median
        std::vector<float> afterSample;
        for (size_t i = 0; i < data.size(); i += stride * channels) {
            afterSample.push_back(data[i]);
        }
        size_t afterMid = afterSample.size() / 2;
        std::nth_element(afterSample.begin(), afterSample.begin() + afterMid, afterSample.end());
        float currentMed = afterSample[afterMid];
        
        if (currentMed > 0.0f && currentMed < 1.0f && std::abs(currentMed - targetMedian) > 1e-3f) {
            float m = computeMTFParameter(currentMed, targetMedian);
            
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(total); ++i) {
                data[i] = std::clamp(mtf(data[i], m), 0.0f, 1.0f);
            }
        }
    }
    
    // Final soft clip
    hdrCompressHighlights(data, 1.0f, softclipThreshold);
}

// ============================================================================
// Curves Adjustment
// ============================================================================

void StatisticalStretch::applyCurvesAdjustment(std::vector<float>& data,
                                               float targetMedian, float curvesBoost) {
    if (curvesBoost <= 0.0f) return;
    
    float tm = std::clamp(targetMedian, 0.01f, 0.99f);
    float cb = std::clamp(curvesBoost, 0.0f, 1.0f);
    
    float p3x = 0.25f * (1.0f - tm) + tm;
    float p4x = 0.75f * (1.0f - tm) + tm;
    float p3y = std::pow(p3x, 1.0f - cb);
    float p4y = std::pow(std::pow(p4x, 1.0f - cb), 1.0f - cb);
    
    std::vector<float> cx = {0.0f, 0.5f * tm, tm, p3x, p4x, 1.0f};
    std::vector<float> cy = {0.0f, 0.5f * tm, tm, p3y, p4y, 1.0f};
    
    size_t total = data.size();
    
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(total); ++i) {
        float x = std::clamp(data[i], 0.0f, 1.0f);
        
        // Piecewise linear interpolation
        for (size_t j = 0; j < cx.size() - 1; ++j) {
            if (x >= cx[j] && x <= cx[j + 1]) {
                float t = (x - cx[j]) / (cx[j + 1] - cx[j] + 1e-12f);
                data[i] = cy[j] + t * (cy[j + 1] - cy[j]);
                break;
            }
        }
        data[i] = std::clamp(data[i], 0.0f, 1.0f);
    }
}

// ============================================================================
// Utility
// ============================================================================

std::array<float, 3> StatisticalStretch::getLumaWeights(int mode) {
    switch (mode) {
        case 1: // Rec601
            return {0.2990f, 0.5870f, 0.1140f};
        case 2: // Rec2020
            return {0.2627f, 0.6780f, 0.0593f};
        default: // Rec709
            return {0.2126f, 0.7152f, 0.0722f};
    }
}
