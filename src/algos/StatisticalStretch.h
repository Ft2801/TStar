#ifndef STATISTICALSTRETCH_H
#define STATISTICALSTRETCH_H

#include <vector>
#include <array>
#include <cmath>


class StatisticalStretch {
public:
    struct ChannelStats {
        float median = 0.5f;
        float blackpoint = 0.0f;
        float denominator = 1.0f;
        float minValue = 0.0f;
        float noise = 0.0f;  // Robust noise estimate
    };
    
    static float robustSigmaLowerHalf(const std::vector<float>& data, 
                                      int stride = 1, int offset = 0, int channels = 1,
                                      int maxSamples = 400000);
    
    static ChannelStats computeStats(const std::vector<float>& data,
                                     int stride, int offset, int channels,
                                     float sigma, bool noBlackClip);
    
    static inline float mtf(float x, float m) {
        float term1 = (m - 1.0f) * x;
        float term2 = (2.0f * m - 1.0f) * x - m;
        if (std::abs(term2) < 1e-12f) return 0.0f;
        return term1 / term2;
    }
    
    static inline float stretchFormula(float x, float medRescaled, float targetMedian) {
        float num = (medRescaled - 1.0f) * targetMedian * x;
        float den = medRescaled * (targetMedian + x - 1.0f) - targetMedian * x;
        if (std::abs(den) < 1e-12f) den = 1e-12f;
        return num / den;
    }
    
    static float computeMTFParameter(float currentMedian, float targetMedian);
    
    static void hdrCompressHighlights(std::vector<float>& data, 
                                      float amount, float knee);
    
    static void hdrCompressColorLuminance(std::vector<float>& data,
                                          int width, int height,
                                          float amount, float knee, int lumaMode);

    static void highRangeRescale(std::vector<float>& data,
                                 int width, int height, int channels,
                                 float targetMedian,
                                 float pedestal, float softCeilPct, float hardCeilPct,
                                 float floorSigma, float softclipThreshold);
    
    static void applyCurvesAdjustment(std::vector<float>& data,
                                      float targetMedian, float curvesBoost);
    
    static inline float computeLuminance(float r, float g, float b, int mode = 0) {
        switch (mode) {
            case 1: // Rec601
                return 0.2990f * r + 0.5870f * g + 0.1140f * b;
            case 2: // Rec2020
                return 0.2627f * r + 0.6780f * g + 0.0593f * b;
            default: // Rec709
                return 0.2126f * r + 0.7152f * g + 0.0722f * b;
        }
    }
    
    static std::array<float, 3> getLumaWeights(int mode);
};

#endif // STATISTICALSTRETCH_H
