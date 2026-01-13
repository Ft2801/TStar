#ifndef DRIZZLE_STACKING_H
#define DRIZZLE_STACKING_H

#include "../ImageBuffer.h"
#include "StackingTypes.h"
#include <vector>

namespace Stacking {

class DrizzleStacking {
public:
    struct DrizzleParams {
        double dropSize = 0.5;       // Pixel shrink factor (0.3-1.0, default 0.5)
        double scaleFactor = 2.0;    // Output upscale factor
        bool useWeightMaps = true;   // Use per-pixel weight maps
        int kernelType = 0;          // 0=point, 1=Gaussian, 2=Lanczos
    };
    
    struct DrizzleWeight {
        std::vector<float> weight;   // Per-pixel contribution weights
        int width, height;
    };
    
    /**
     * @brief Compute drizzle weight map for an image
     * 
     * Weight map accounts for:
     * - Drop size (smaller drops = less coverage per pixel)
     * - Pixel position (sub-pixel registration)
     * - Bad pixel masks
     */
    static DrizzleWeight computeWeightMap(const ImageBuffer& input,
                                          const RegistrationData& reg,
                                          int outputWidth, int outputHeight,
                                          const DrizzleParams& params);
    
    /**
     * @brief Drizzle a single frame onto accumulator
     * 
     * @param input Input image
     * @param reg Registration data
     * @param accum Accumulator (sum of weighted pixels)
     * @param weightAccum Weight accumulator
     * @param params Drizzle parameters
     */
    void drizzleFrame(const ImageBuffer& input,
                             const RegistrationData& reg,
                             std::vector<double>& accum,
                             std::vector<double>& weightAccum,
                             int outputWidth, int outputHeight,
                             const DrizzleParams& params);
    
    /**
     * @brief Finalize drizzle stack (divide by weights)
     */
    static void finalizeStack(const std::vector<double>& accum,
                              const std::vector<double>& weightAccum,
                              ImageBuffer& output);
    
    /**
     * @brief 2x upscale using nearest-neighbor (simple drizzle prep)
     */
    static ImageBuffer upscale2x(const ImageBuffer& input);
    
    /**
     * @brief Scale homography for upscaled coordinates
     */
    static RegistrationData scaleRegistration(const RegistrationData& reg, double factor);

    // Kernel Initialization
    void initKernel(DrizzleKernelType type, double param = 0.0); // param is sigma for Gauss or a for Lanczos
    
    // Stateful API for convenience
    void initialize(int inputWidth, int inputHeight, int channels, const DrizzleParams& params); 
    void addImage(const ImageBuffer& img, const RegistrationData& reg, const std::vector<float>& weights = {}, const float* rejectionMap = nullptr);
    bool resolve(ImageBuffer& output);
    
    int outputWidth() const { return m_outWidth; }
    int outputHeight() const { return m_outHeight; }

private:
    std::vector<float> m_kernelLUT;
    static const int LUT_SIZE = 4096;
    float m_lutScale = 1.0f; 
    DrizzleKernelType m_currentKernel = DrizzleKernelType::Point;
    
    // Stateful members
    int m_outWidth = 0;
    int m_outHeight = 0;
    int m_channels = 0;
    DrizzleParams m_params;
    std::vector<double> m_accum;       // 3 * W * H
    std::vector<double> m_weightAccum; // W * H
    
    float getKernelWeight(double dx, double dy) const;
    
    // Rest of class...
private:
    struct Point { double x, y; };
    typedef std::vector<Point> Polygon;
    
    // Sutherland-Hodgman Polygon Clipping
    static double computePolygonArea(const Polygon& p);
    static Polygon clipPolygon(const Polygon& subject, double xMin, double yMin, double xMax, double yMax);
    static Polygon shrinkPolygon(const Polygon& p, double factor);
};

class MosaicFeathering {
public:
    struct FeatherParams {
        double maskScale = 0.1;      // Downscale factor for mask computation
        int rampWidth = 50;          // Edge ramp width in pixels
        bool smoothRamp = true;      // Use smooth polynomial ramp
    };
    
    static std::vector<float> computeFeatherMask(const ImageBuffer& input, const FeatherParams& params);
       
    static inline float smoothRamp(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * t * (6.0f * t * t - 15.0f * t + 10.0f);
    }
    
    /**
     * @brief Blend two images using feather masks
     */
    static void blendImages(const ImageBuffer& imgA, const std::vector<float>& maskA,
                           const ImageBuffer& imgB, const std::vector<float>& maskB,
                           ImageBuffer& output);
    
    /**
     * @brief Compute downscaled distance mask (opencv-like)
     * 
     * @param binary Input binary mask (0/255)
     * @param width, height Mask dimensions
     * @param scaleX, scaleY Output scale factors
     * @param output Downscaled float mask
     */
    static void computeDistanceMask(const std::vector<uint8_t>& binary,
                                    int width, int height,
                                    int outWidth, int outHeight,
                                    std::vector<float>& output);

private:
    // LUT for smooth ramp (1001 entries for 0.000 - 1.000)
    static std::vector<float> s_rampLUT;
    static void initRampLUT();
};

class LinearFitRejection {
public:

    static int reject(float* stack, int N, float sigLow, float sigHigh,
                      int* rejected, int& lowReject, int& highReject);
    
private:
    static void fitLine(const float* x, const float* y, int N,
                        float& intercept, float& slope);
};

class GESDTRejection {
public:
    struct ESDOutlier {
        bool isOutlier;
        float value;
        int originalIndex;
    };
    
    /**
     * @brief Apply GESDT rejection
     * 
     * @param stack Sorted pixel values
     * @param N Number of values
     * @param maxOutliers Maximum number of outliers to detect
     * @param criticalValues Precomputed critical values for each iteration
     * @param rejected Output: rejection status
     * @return Number of remaining pixels
     */
    static int reject(float* stack, int N, int maxOutliers,
                      const float* criticalValues,
                      int* rejected, int& lowReject, int& highReject);
    
    /**
     * @brief Precompute critical values for GESDT
     * 
     * @param N Sample size
     * @param alpha Significance level (e.g., 0.05)
     * @param maxOutliers Maximum outliers to detect
     * @param output Critical values array
     */
    static void computeCriticalValues(int N, double alpha, int maxOutliers,
                                      std::vector<float>& output);
    
private:
    /**
     * @brief Compute Grubbs' test statistic
     */
    static float grubbsStat(const float* data, int N, int& maxIndex);
};

} // namespace Stacking

#endif // DRIZZLE_STACKING_H
