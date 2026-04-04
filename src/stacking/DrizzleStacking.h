#ifndef DRIZZLE_STACKING_H
#define DRIZZLE_STACKING_H

/**
 * @file DrizzleStacking.h
 * @brief Drizzle integration, mosaic feathering, and auxiliary rejection classes.
 *
 * Provides the core drizzle algorithm (variable-pixel linear reconstruction),
 * mosaic feathering via distance-transform masks, and two standalone rejection
 * algorithms (Linear-Fit and Generalized ESD Test) used outside the main
 * inline rejection path.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "../ImageBuffer.h"
#include "StackingTypes.h"
#include <vector>

namespace Stacking {

// ============================================================================
// DrizzleStacking
// ============================================================================

/**
 * @brief Variable-pixel linear reconstruction (drizzle) stacking engine.
 *
 * Implements the Fruchter & Hook (2002) drizzle algorithm with extensions
 * for configurable drop size, output scale factor, and optional convolution
 * kernels (Gaussian, Lanczos).  Both a full polygon-clipping path and a
 * fast point-kernel shortcut are provided.
 *
 * Usage (stateful API):
 *   1. initialize(...)
 *   2. addImage(...)   -- call once per frame
 *   3. resolve(output) -- divide accumulators and write result
 */
class DrizzleStacking {
public:

    // -- Parameter structures ------------------------------------------------

    /**
     * @brief Configuration for a drizzle pass.
     */
    struct DrizzleParams {
        double dropSize     = 0.5;   ///< Pixel shrink factor (0.3 - 1.0)
        double scaleFactor  = 2.0;   ///< Output upscale factor
        bool   useWeightMaps = true; ///< Use per-pixel weight maps
        int    kernelType   = 0;     ///< 0 = point, 1 = Gaussian, 2 = Lanczos
        bool   fastMode     = false; ///< Use optimised 1x point-kernel path
    };

    /**
     * @brief Per-pixel contribution weight map produced by computeWeightMap().
     */
    struct DrizzleWeight {
        std::vector<float> weight;   ///< Per-pixel weight values
        int width  = 0;
        int height = 0;
    };

    // -- Static helpers ------------------------------------------------------

    /**
     * @brief Compute the drizzle weight map for a single input image.
     *
     * The map records, for every output pixel, the fractional area of the
     * (possibly shrunk) input pixel that overlaps it.
     *
     * @param input        Source image.
     * @param reg          Registration transform for this frame.
     * @param outputWidth  Width  of the output grid (scaled).
     * @param outputHeight Height of the output grid (scaled).
     * @param params       Drizzle parameters.
     * @return DrizzleWeight map.
     */
    static DrizzleWeight computeWeightMap(const ImageBuffer& input,
                                          const RegistrationData& reg,
                                          int outputWidth, int outputHeight,
                                          const DrizzleParams& params);

    /**
     * @brief Divide accumulated values by accumulated weights to produce
     *        the final stacked image.
     *
     * @param accum       Planar accumulator [channels * W * H].
     * @param weightAccum Weight accumulator  [W * H].
     * @param output      Pre-allocated output buffer (interleaved layout).
     */
    static void finalizeStack(const std::vector<double>& accum,
                              const std::vector<double>& weightAccum,
                              ImageBuffer& output);

    /**
     * @brief Simple 2x nearest-neighbour upscale (drizzle preparation).
     */
    static ImageBuffer upscale2x(const ImageBuffer& input);

    /**
     * @brief Scale the translation component of a homography by a factor.
     *
     * @param reg    Original registration data.
     * @param factor Scale multiplier applied to H[0][2] and H[1][2].
     * @return Scaled registration data.
     */
    static RegistrationData scaleRegistration(const RegistrationData& reg,
                                              double factor);

    // -- Instance methods (polygon-clipping drizzle) -------------------------

    /**
     * @brief Drizzle a single frame onto the accumulator arrays.
     *
     * Transforms each input pixel into a quadrilateral in output space,
     * optionally shrinks it by dropSize, clips against each overlapping
     * output pixel, and accumulates the weighted flux.
     *
     * @param input        Source image.
     * @param reg          Registration transform.
     * @param accum        Planar accumulator (modified in place).
     * @param weightAccum  Weight accumulator (modified in place).
     * @param outputWidth  Width  of output grid.
     * @param outputHeight Height of output grid.
     * @param params       Drizzle parameters.
     * @param offsetX      X-offset applied to output coordinates.
     * @param offsetY      Y-offset applied to output coordinates.
     */
    void drizzleFrame(const ImageBuffer& input,
                      const RegistrationData& reg,
                      std::vector<double>& accum,
                      std::vector<double>& weightAccum,
                      int outputWidth, int outputHeight,
                      const DrizzleParams& params,
                      double offsetX = 0.0, double offsetY = 0.0,
                      const std::vector<float>& weights = {},
                      const float* rejectionMap = nullptr);

    /**
     * @brief Fast drizzle for 1x point kernel (nearest-neighbour scatter).
     *
     * Each input pixel centre is transformed and deposited into the single
     * nearest output pixel with unit weight.  No polygon clipping is needed.
     *
     * @param offsetX      X-offset applied to output coordinates.
     * @param offsetY      Y-offset applied to output coordinates.
     */
    void fastDrizzleFrame(const ImageBuffer& input,
                          const RegistrationData& reg,
                          std::vector<double>& accum,
                          std::vector<double>& weightAccum,
                          int outputWidth, int outputHeight,
                          const DrizzleParams& params,
                          double offsetX = 0.0, double offsetY = 0.0,
                          const std::vector<float>& weights = {},
                          const float* rejectionMap = nullptr);

    // -- Kernel management ---------------------------------------------------

    /**
     * @brief Build the separable kernel look-up table.
     *
     * @param type  Kernel type (Point, Gaussian, Lanczos).
     * @param param Kernel parameter: sigma for Gaussian, order for Lanczos.
     */
    void initKernel(DrizzleKernelType type, double param = 0.0);

    // -- Stateful convenience API --------------------------------------------

    /**
     * @brief Allocate accumulators and configure output dimensions.
     */
    void initialize(int inputWidth, int inputHeight, int channels,
                    const DrizzleParams& params,
                    double offsetX = 0.0, double offsetY = 0.0);

    /**
     * @brief Drizzle one image into the internal accumulators.
     *
     * @param img          Source image.
     * @param reg          Registration transform.
     * @param weights      Optional per-frame weight vector (unused for now).
     * @param rejectionMap Optional per-pixel rejection mask   (unused for now).
     */
    void addImage(const ImageBuffer& img,
                  const RegistrationData& reg,
                  const std::vector<float>& weights = {},
                  const float* rejectionMap = nullptr);

    /**
     * @brief Finalize the stack and write the result.
     *
     * @param output Receives the final drizzled image.
     * @return true on success.
     */
    bool resolve(ImageBuffer& output);

    int outputWidth()  const { return m_outWidth;  }
    int outputHeight() const { return m_outHeight; }

private:

    // -- Kernel LUT ----------------------------------------------------------

    std::vector<float>   m_kernelLUT;
    static const int     LUT_SIZE = 4096;
    float                m_lutScale     = 1.0f;
    DrizzleKernelType    m_currentKernel = DrizzleKernelType::Point;

    /**
     * @brief Evaluate the separable kernel at offset (dx, dy).
     * @return K(dx) * K(dy), looked up from the precomputed LUT.
     */
    float getKernelWeight(double dx, double dy) const;

    // -- Stateful accumulator state ------------------------------------------

    int                  m_outWidth   = 0;
    int                  m_outHeight  = 0;
    int                  m_channels   = 0;
    double               m_offsetX    = 0.0;
    double               m_offsetY    = 0.0;
    DrizzleParams        m_params;
    std::vector<double>  m_accum;        ///< channels * W * H
    std::vector<double>  m_weightAccum;  ///< W * H

    // -- Polygon geometry helpers --------------------------------------------

    struct Point { double x, y; };
    typedef std::vector<Point> Polygon;

    /** @brief Signed area of a simple polygon (Shoelace formula). */
    static double  computePolygonArea(const Polygon& p);

    /** @brief Sutherland-Hodgman clip of subject against an axis-aligned rectangle. */
    static Polygon clipPolygon(const Polygon& subject,
                               double xMin, double yMin,
                               double xMax, double yMax);

    /** @brief Shrink a polygon towards its centroid by the given factor. */
    static Polygon shrinkPolygon(const Polygon& p, double factor);
};

// ============================================================================
// MosaicFeathering
// ============================================================================

/**
 * @brief Edge-feathering for mosaic blending.
 *
 * Produces a smooth [0, 1] weight mask whose value ramps from 0 at
 * the boundary of non-zero content to 1 deep inside.  The mask is
 * computed at reduced resolution for speed and bilinearly upscaled.
 */
class MosaicFeathering {
public:

    /**
     * @brief Feathering configuration.
     */
    struct FeatherParams {
        double maskScale  = 0.1;   ///< Downscale factor for distance computation
        int    rampWidth  = 50;    ///< Edge ramp width in pixels
        bool   smoothRamp = true;  ///< Apply smooth polynomial ramp function
    };

    /**
     * @brief Compute a feather mask for the given image.
     *
     * @param input  Source image (non-zero pixels define the content region).
     * @param params Feathering parameters.
     * @return Float mask with the same pixel count as the input.
     */
    static std::vector<float> computeFeatherMask(const ImageBuffer& input,
                                                  const FeatherParams& params);

    /**
     * @brief Quintic smooth-step ramp: 6t^5 - 15t^4 + 10t^3.
     */
    static inline float smoothRamp(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * t * (6.0f * t * t - 15.0f * t + 10.0f);
    }

    /**
     * @brief Weighted blend of two images using their feather masks.
     */
    static void blendImages(const ImageBuffer& imgA,
                            const std::vector<float>& maskA,
                            const ImageBuffer& imgB,
                            const std::vector<float>& maskB,
                            ImageBuffer& output);

    /**
     * @brief Compute a downscaled distance-to-edge mask via OpenCV.
     *
     * @param binary    Input binary mask (0 = void, 255 = content).
     * @param width     Full-resolution width.
     * @param height    Full-resolution height.
     * @param outWidth  Target downscaled width.
     * @param outHeight Target downscaled height.
     * @param output    Receives the normalised [0, 1] distance mask.
     */
    static void computeDistanceMask(const std::vector<uint8_t>& binary,
                                    int width, int height,
                                    int outWidth, int outHeight,
                                    std::vector<float>& output);

private:
    static std::vector<float> s_rampLUT;   ///< 1001-entry smooth-ramp LUT
    static void initRampLUT();
};

// ============================================================================
// LinearFitRejection
// ============================================================================

/**
 * @brief Iterative sigma-clipping rejection with a linear-fit model.
 *
 * After sorting, a least-squares line is fitted to the pixel stack.
 * Pixels deviating more than sigLow/sigHigh sigma from the line are
 * rejected.  The process repeats until convergence or until fewer
 * than 4 pixels remain.
 */
class LinearFitRejection {
public:
    /**
     * @brief Run the linear-fit rejection.
     *
     * @param stack      Pixel values (modified: compacted in place).
     * @param N          Number of input values.
     * @param sigLow     Low  rejection threshold (sigma units).
     * @param sigHigh    High rejection threshold (sigma units).
     * @param rejected   Per-pixel rejection flag (-1 low, 0 kept, +1 high).
     * @param lowReject  Incremented for every low  rejection.
     * @param highReject Incremented for every high rejection.
     * @return Number of remaining (non-rejected) pixels.
     */
    static int reject(float* stack, int N, float sigLow, float sigHigh,
                      int* rejected, int& lowReject, int& highReject);

private:
    /** @brief Ordinary least-squares fit: y = slope * x + intercept. */
    static void fitLine(const float* x, const float* y, int N,
                        float& intercept, float& slope);
};

// ============================================================================
// GESDTRejection
// ============================================================================

/**
 * @brief Generalized Extreme Studentized Deviate Test (GESDT) rejection.
 *
 * Iteratively removes the sample with the largest Grubbs statistic and
 * compares it against precomputed critical values.  Suitable for detecting
 * multiple outliers when the maximum number of outliers is known a priori.
 */
class GESDTRejection {
public:

    /** @brief Record of a single ESD iteration. */
    struct ESDOutlier {
        bool  isOutlier;
        float value;
        int   originalIndex;
    };

    /**
     * @brief Run the GESDT rejection.
     *
     * @param stack          Sorted pixel values (compacted in place).
     * @param N              Number of input values.
     * @param maxOutliers    Maximum outliers to test for.
     * @param criticalValues Precomputed critical values per iteration.
     * @param rejected       Per-pixel rejection status.
     * @param lowReject      Accumulated low  rejection count.
     * @param highReject     Accumulated high rejection count.
     * @return Number of remaining pixels.
     */
    static int reject(float* stack, int N, int maxOutliers,
                      const float* criticalValues,
                      int* rejected, int& lowReject, int& highReject);

    /**
     * @brief Precompute Grubbs critical values for a given sample size.
     *
     * @param N           Sample size.
     * @param alpha       Significance level (e.g. 0.05).
     * @param maxOutliers Maximum number of outlier iterations.
     * @param output      Receives the critical value array.
     */
    static void computeCriticalValues(int N, double alpha, int maxOutliers,
                                      std::vector<float>& output);

private:
    /**
     * @brief Compute the Grubbs test statistic for sorted data.
     *
     * Checks only the extreme values (first and last element) against
     * the sample mean.
     *
     * @param data     Sorted array.
     * @param N        Array length.
     * @param maxIndex Receives the index of the most extreme value.
     * @return Grubbs G statistic.
     */
    static float grubbsStat(const float* data, int N, int& maxIndex);
};

} // namespace Stacking

#endif // DRIZZLE_STACKING_H