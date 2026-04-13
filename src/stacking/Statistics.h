/**
 * @file Statistics.h
 * @brief Optimised statistical functions for image stacking.
 *
 * Provides mean, standard deviation, median, MAD, noise estimation,
 * percentile computation, histogram-based median, IKSS and biweight
 * estimators, linear regression, weighted mean, and in-place
 * quickselect / quicksort utilities.
 *
 * All functions operate on single-precision floating-point data.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#ifndef STACKING_STATISTICS_H
#define STACKING_STATISTICS_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstdint>

namespace Stacking {

/**
 * @brief Collection of static statistical utility functions.
 *
 * Designed for the numerical needs of stacking, rejection algorithms,
 * and normalization.  Thread safety: all functions are re-entrant;
 * some use OpenMP for large inputs.
 */
class Statistics {
public:

    // ========================================================================
    // Central tendency
    // ========================================================================

    /**
     * @brief Arithmetic mean of a float array.
     * @param data Pointer to the first element.
     * @param size Number of elements.
     * @return Mean value, or 0 if @p size is zero.
     */
    static double mean(const float* data, size_t size);

    /** @overload */
    static double mean(const std::vector<float>& data);

    // ========================================================================
    // Dispersion
    // ========================================================================

    /**
     * @brief Compute mean and sample standard deviation in a single pass pair.
     *
     * Uses a numerically stable two-pass algorithm.
     *
     * @param data     Input array.
     * @param size     Number of elements.
     * @param outMean  [out] Computed mean.
     * @param outStdDev [out] Computed sample standard deviation.
     */
    static void meanAndStdDev(const float* data, size_t size,
                              double& outMean, double& outStdDev);

    /**
     * @brief Sample standard deviation.
     * @param data            Input array.
     * @param size            Number of elements.
     * @param precomputedMean Optional pre-computed mean (nullptr to compute internally).
     * @return Standard deviation, or 0 if @p size <= 1.
     */
    static double stdDev(const float* data, size_t size,
                         const double* precomputedMean = nullptr);

    /** @overload */
    static double stdDev(const std::vector<float>& data,
                         const double* precomputedMean = nullptr);

    // ========================================================================
    // Median
    // ========================================================================

    /**
     * @brief In-place median via partial sorting (modifies the input array).
     * @param data Pointer to data (will be partially re-ordered).
     * @param size Number of elements.
     * @return Median value.
     */
    static float quickMedian(float* data, size_t size);

    /** @overload */
    static float quickMedian(std::vector<float>& data);

    /**
     * @brief Non-destructive median (creates an internal copy).
     * @param data Pointer to immutable data.
     * @param size Number of elements.
     * @return Median value.
     */
    static float median(const float* data, size_t size);

    /** @overload */
    static float median(const std::vector<float>& data);

    // ========================================================================
    // MAD (Median Absolute Deviation)
    // ========================================================================

    /**
     * @brief Compute the MAD of an array.
     * @param data   Input array.
     * @param size   Number of elements.
     * @param median Pre-computed median (pass 0 to compute internally).
     * @return MAD value.
     */
    static double mad(const float* data, size_t size, float median = 0.0f);

    /** @overload */
    static double mad(const std::vector<float>& data, float median = 0.0f);

    // ========================================================================
    // Noise estimation
    // ========================================================================

    /**
     * @brief Estimate the noise level of a 2-D image.
     *
     * Applies row-wise first-order differences with iterative sigma
     * clipping, then takes the median of per-row noise estimates scaled
     * by 1 / sqrt(2).
     *
     * @param data   Pointer to the luminance (or single-channel) image data.
     * @param width  Image width in pixels.
     * @param height Image height in pixels.
     * @return Estimated noise sigma.
     */
    static double computeNoise(const float* data, int width, int height);

    // ========================================================================
    // Percentile
    // ========================================================================

    /**
     * @brief Compute a percentile value (partially sorts the input).
     * @param data       Input array (modified).
     * @param size       Number of elements.
     * @param percentile Desired percentile in [0, 100].
     * @return Interpolated value at the requested percentile.
     */
    static float percentile(float* data, size_t size, double percentile);

    // ========================================================================
    // Min / Max
    // ========================================================================

    /** @brief Minimum value in the array. */
    static float minimum(const float* data, size_t size);

    /** @brief Maximum value in the array. */
    static float maximum(const float* data, size_t size);

    /**
     * @brief Simultaneous minimum and maximum (single pass).
     * @param data   Input array.
     * @param size   Number of elements.
     * @param outMin [out] Minimum value.
     * @param outMax [out] Maximum value.
     */
    static void minMax(const float* data, size_t size,
                       float& outMin, float& outMax);

    // ========================================================================
    // Histogram-based median
    // ========================================================================

    /**
     * @brief Approximate median via histogram binning.
     *
     * Faster than exact median for very large arrays at the cost of
     * bin-width quantisation error.
     *
     * @param data    Input array.
     * @param size    Number of elements.
     * @param numBins Number of histogram bins (default 65536).
     * @return Approximate median value.
     */
    static float histogramMedian(const float* data, size_t size, int numBins = 65536);

    // ========================================================================
    // Robust estimators
    // ========================================================================

    /**
     * @brief IKSS-lite location and scale estimator.
     *
     * Algorithm:
     *  1. Clip values outside +/- 6*MAD from the median.
     *  2. Re-compute the median (location) of the filtered data.
     *  3. Re-compute the MAD of the filtered data.
     *  4. Scale = sqrt(biweight midvariance) * 0.991.
     *
     * More robust than plain MAD for stacking normalisation.
     *
     * @param data        Input array (partially re-ordered internally).
     * @param size        Number of elements.
     * @param median      Pre-computed median.
     * @param mad         Pre-computed MAD.
     * @param outLocation [out] Robust location estimate.
     * @param outScale    [out] Robust scale estimate.
     * @return false if the computation fails (e.g. MAD == 0 after filtering).
     */
    static bool ikssLite(const float* data, size_t size,
                         float median, float mad,
                         double& outLocation, double& outScale);

    /**
     * @brief Biweight midvariance (BWMV).
     *
     *   BWMV = n * sum[ (x_i - med)^2 * (1 - u_i^2)^4 ]
     *             / ( sum[ (1 - u_i^2)(1 - 5*u_i^2) ] )^2
     *
     * where u_i = (x_i - median) / (9 * MAD), clamped so |u_i| < 1.
     *
     * @param data   Input array.
     * @param size   Number of elements.
     * @param mad    MAD of the data about @p median.
     * @param median Median of the data.
     * @return Biweight midvariance estimate.
     */
    static double biweightMidvariance(const float* data, size_t size,
                                      float mad, float median);

    /**
     * @brief Legacy Huber-reweighted IKSS estimator.
     *
     * Iteratively re-weights samples using Huber weights to converge
     * on a robust location and scale.  Retained for backward compatibility;
     * prefer ikssLite() for new code.
     *
     * @param data        Input array.
     * @param size        Number of elements.
     * @param median      Pre-computed median.
     * @param mad         Pre-computed MAD.
     * @param outLocation [out] Location estimate.
     * @param outScale    [out] Scale estimate.
     */
    static void ikssEstimator(const float* data, size_t size,
                              float median, float mad,
                              double& outLocation, double& outScale);

    /**
     * @brief In-place sort (modifies the input array).
     * @param data Pointer to data.
     * @param size Number of elements.
     */
    static void quickSort(float* data, size_t size);

    /** @overload */
    static void quickSort(std::vector<float>& data) { quickSort(data.data(), data.size()); }

    // ========================================================================
    // Linear regression
    // ========================================================================

    /**
     * @brief Ordinary least-squares linear fit:  y = slope * x + intercept.
     * @param x            Independent variable array.
     * @param y            Dependent variable array.
     * @param size         Number of data points.
     * @param outSlope     [out] Fitted slope.
     * @param outIntercept [out] Fitted intercept.
     */
    static void linearFit(const float* x, const float* y, size_t size,
                          float& outSlope, float& outIntercept);

    // ========================================================================
    // Weighted mean
    // ========================================================================

    /**
     * @brief Compute the weighted arithmetic mean.
     * @param data    Values.
     * @param weights Corresponding weights.
     * @param size    Number of elements.
     * @return Weighted mean, or 0 if the total weight is non-positive.
     */
    static double weightedMean(const float* data, const float* weights, size_t size);

private:
    static void quickSelect(float* data, size_t size, size_t n);
    static float quickSelectImpl(float* data, size_t left, size_t right, size_t k);
    static size_t partition(float* data, size_t left, size_t right, size_t pivotIndex);
    static void quickSortImpl(float* data, size_t left, size_t right);
};  // class Statistics

}  // namespace Stacking

#endif  // STACKING_STATISTICS_H 