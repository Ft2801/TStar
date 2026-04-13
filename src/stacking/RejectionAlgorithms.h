#ifndef STACKING_REJECTION_ALGORITHMS_H
#define STACKING_REJECTION_ALGORITHMS_H

/**
 * @file RejectionAlgorithms.h
 * @brief Pixel rejection algorithms for astronomical image stacking.
 *
 * Provides a suite of statistical outlier rejection methods used to identify
 * and exclude anomalous pixel values (e.g., cosmic rays, hot pixels, satellite
 * trails) from a pixel stack before computing the final combined value.
 *
 * Supported algorithms:
 *   - Percentile Clipping
 *   - Sigma Clipping (iterative, optionally weighted)
 *   - MAD (Median Absolute Deviation) Clipping
 *   - Sigma-Median Clipping (replacement-based)
 *   - Winsorized Sigma Clipping
 *   - Linear Fit Clipping
 *   - Generalized ESD Test (GESDT)
 *   - Biweight Estimator
 *   - Modified Z-Score
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "StackingTypes.h"
#include "Statistics.h"

#include <vector>

namespace Stacking {

// ============================================================================
// Rejection Result
// ============================================================================

/**
 * @brief Aggregated statistics produced by a rejection pass.
 */
struct RejectionResult {
    int keptCount    = 0;   ///< Number of pixels retained after rejection.
    int lowRejected  = 0;   ///< Number of pixels rejected on the low (dark) side.
    int highRejected = 0;   ///< Number of pixels rejected on the high (bright) side.

    /** @brief Total number of rejected pixels (low + high). */
    int totalRejected() const { return lowRejected + highRejected; }
};

// ============================================================================
// Rejection Algorithms
// ============================================================================

/**
 * @brief Static class implementing pixel rejection algorithms for stacking.
 *
 * All public methods are static and operate on a pixel stack represented as a
 * vector of float values.  An accompanying rejection-flag vector (same length)
 * records the per-pixel decision: 0 = kept, -1 = rejected low, +1 = rejected
 * high.
 */
class RejectionAlgorithms {
public:
    // ---- Main dispatcher ---------------------------------------------------

    /**
     * @brief Apply the selected rejection algorithm to a pixel stack.
     *
     * @param[in,out] stack          Pixel values from all frames at one position.
     * @param[in]     type           Which rejection algorithm to use.
     * @param[in]     sigmaLow       Low-side threshold (sigma multiplier or percentile).
     * @param[in]     sigmaHigh      High-side threshold (sigma multiplier or percentile).
     * @param[out]    rejected       Per-pixel rejection flags (resized internally).
     * @param[in]     weights        Optional per-pixel weights (weighted stacking).
     * @param[in]     drizzleWeights Optional drizzle weights.
     * @param[in,out] scratch        Optional scratch buffer to reduce allocations.
     * @return RejectionResult with rejection statistics.
     */
    static RejectionResult apply(
        std::vector<float>& stack,
        Rejection type,
        float sigmaLow,
        float sigmaHigh,
        std::vector<int>& rejected,
        const std::vector<float>* weights        = nullptr,
        const std::vector<float>* drizzleWeights = nullptr,
        std::vector<float>* scratch              = nullptr);

    // ---- Individual algorithms ---------------------------------------------

    /**
     * @brief Percentile clipping.
     *
     * Rejects pixels whose fractional deviation from the median exceeds
     * the given low/high thresholds.  Simple, fast, but less adaptive.
     *
     * @param[in]  stack    Pixel stack (not modified).
     * @param[in]  pLow     Low percentile threshold (0-1).
     * @param[in]  pHigh    High percentile threshold (0-1).
     * @param[out] rejected Rejection flags.
     * @param[in,out] scratch Optional scratch buffer.
     * @return Rejection statistics.
     */
    static RejectionResult percentileClipping(
        const std::vector<float>& stack,
        float pLow, float pHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr);

    /**
     * @brief Iterative sigma clipping.
     *
     * Iteratively rejects pixels more than N sigma from the median until
     * convergence.  Optionally weight-aware.
     *
     * @param[in,out] stack     Pixel stack.
     * @param[in]     sigmaLow  Low sigma multiplier.
     * @param[in]     sigmaHigh High sigma multiplier.
     * @param[out]    rejected  Rejection flags.
     * @param[in,out] scratch   Optional scratch buffer.
     * @param[in]     weights   Optional per-pixel weights.
     * @return Rejection statistics.
     */
    static RejectionResult sigmaClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch        = nullptr,
        const std::vector<float>* weights  = nullptr);

    /**
     * @brief MAD (Median Absolute Deviation) clipping.
     *
     * Like sigma clipping but uses the MAD instead of standard deviation,
     * making it more robust against outliers in the scale estimate itself.
     *
     * @param[in,out] stack     Pixel stack.
     * @param[in]     sigmaLow  Low threshold (MAD units).
     * @param[in]     sigmaHigh High threshold (MAD units).
     * @param[out]    rejected  Rejection flags.
     * @param[in,out] scratch   Optional scratch buffer.
     * @return Rejection statistics.
     */
    static RejectionResult madClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr);

    /**
     * @brief Sigma-median clipping (replacement variant).
     *
     * Outliers are replaced with the current median rather than removed,
     * preserving the pixel-position correspondence.  Iterates until no
     * further replacements occur.
     *
     * @param[in,out] stack     Pixel stack (outlier values are overwritten).
     * @param[in]     sigmaLow  Low sigma threshold.
     * @param[in]     sigmaHigh High sigma threshold.
     * @param[out]    rejected  Rejection flags.
     * @param[in,out] scratch   Optional scratch buffer.
     * @return Rejection statistics.
     */
    static RejectionResult sigmaMedianClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr);

    /**
     * @brief Winsorized sigma clipping.
     *
     * Computes the scale estimate on a winsorized copy of the data, yielding
     * a more robust sigma than classical standard deviation.  Optionally
     * weight-aware.
     *
     * @param[in,out] stack     Pixel stack.
     * @param[in]     sigmaLow  Low sigma threshold.
     * @param[in]     sigmaHigh High sigma threshold.
     * @param[out]    rejected  Rejection flags.
     * @param[in,out] scratch   Optional scratch buffer.
     * @param[in]     weights   Optional per-pixel weights.
     * @return Rejection statistics.
     */
    static RejectionResult winsorizedClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch        = nullptr,
        const std::vector<float>* weights  = nullptr);

    /**
     * @brief Linear fit clipping.
     *
     * Fits a line through rank-sorted pixel values via OLS and rejects
     * pixels whose residuals exceed the threshold.  Effective when the
     * expected distribution has a gradient component.
     *
     * @param[in,out] stack     Pixel stack.
     * @param[in]     sigmaLow  Low residual threshold (in MAE units).
     * @param[in]     sigmaHigh High residual threshold (in MAE units).
     * @param[out]    rejected  Rejection flags.
     * @param[in,out] scratch   Optional scratch buffer.
     * @return Rejection statistics.
     */
    static RejectionResult linearFitClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr);

    /**
     * @brief Generalized ESD Test (GESDT).
     *
     * A rigorous statistical test for detecting multiple outliers based on
     * the Grubbs statistic.  Slower than simple clipping but more principled.
     *
     * @param[in,out] stack            Pixel stack.
     * @param[in]     outliersFraction Maximum expected fraction of outliers (0-1).
     * @param[in]     significance     Significance level (e.g. 0.05).
     * @param[out]    rejected         Rejection flags.
     * @param[in,out] scratch          Optional scratch buffer.
     * @return Rejection statistics.
     */
    static RejectionResult gesdtClipping(
        std::vector<float>& stack,
        float outliersFraction, float significance,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr);

    /**
     * @brief Pre-compute critical values for the GESDT algorithm.
     *
     * Uses a rational approximation of the inverse normal CDF to derive
     * critical values from the t-distribution.
     *
     * @param[in] n            Sample size.
     * @param[in] maxOutliers  Maximum number of outliers to test.
     * @param[in] significance Significance level.
     * @return Vector of critical values (one per iteration).
     */
    static std::vector<float> computeGesdtCriticalValues(
        int n, int maxOutliers, float significance);

    /**
     * @brief Biweight estimator rejection.
     *
     * Iteratively refines a robust center (biweight location) and rejects
     * pixels that receive zero weight under the Tukey biweight function.
     *
     * @param[in,out] stack          Pixel stack.
     * @param[in]     tuningConstant Tuning constant c (typically 6.0 or 9.0).
     * @param[out]    rejected       Rejection flags.
     * @param[in,out] scratch        Optional scratch buffer.
     * @return Rejection statistics.
     */
    static RejectionResult biweightClipping(
        std::vector<float>& stack,
        float tuningConstant,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr);

    /**
     * @brief Modified Z-score rejection.
     *
     * Uses the median and MAD to compute robust Z-scores.  Pixels with
     * |Z| > threshold are rejected.  Equivalent to sigma clipping with
     * a MAD-based scale estimate.
     *
     * @param[in,out] stack     Pixel stack.
     * @param[in]     threshold Rejection threshold (typical default ~3.5).
     * @param[out]    rejected  Rejection flags.
     * @param[in,out] scratch   Optional scratch buffer.
     * @return Rejection statistics.
     */
    static RejectionResult modifiedZScoreClipping(
        std::vector<float>& stack,
        float threshold,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr);

    // ---- Weighted statistics -----------------------------------------------

    /**
     * @brief Compute the weighted median of a dataset.
     *
     * @param[in] data      Pixel values.
     * @param[in] weights   Per-pixel weights.
     * @param[in] validMask Optional mask (0 = valid, non-zero = excluded).
     * @return Weighted median value.
     */
    static float weightedMedian(
        const std::vector<float>& data,
        const std::vector<float>& weights,
        const std::vector<int>* validMask = nullptr);

    /**
     * @brief Compute weighted mean and standard deviation.
     *
     * Uses reliability-weight variance correction (V1 - V2/V1 denominator).
     *
     * @param[in] data      Pixel values.
     * @param[in] weights   Per-pixel weights.
     * @param[in] validMask Optional mask (0 = valid, non-zero = excluded).
     * @return Pair of (weighted mean, weighted standard deviation).
     */
    static std::pair<double, double> weightedMeanAndStdDev(
        const std::vector<float>& data,
        const std::vector<float>& weights,
        const std::vector<int>* validMask = nullptr);

private:
    // ---- Internal helpers --------------------------------------------------

    /**
     * @brief Remove null (zero-valued) pixels from the stack in-place.
     * @note  Deprecated in modern pipeline; kept for legacy callers.
     * @return Number of non-null pixels remaining.
     */
    static int removeNullPixels(
        std::vector<float>& stack,
        const std::vector<float>* drizzleWeights);

    /**
     * @brief Compact a stack by removing rejected entries in-place.
     * @note  Deprecated; modern logic preserves indices.
     * @return Number of remaining (non-rejected) pixels.
     */
    static int compactStack(
        std::vector<float>& stack,
        const std::vector<int>& rejected);

    /**
     * @brief Compute the Grubbs test statistic for the GESDT algorithm.
     *
     * Finds the sample with the maximum absolute deviation from the mean
     * and returns the ratio deviation / stddev.
     *
     * @param[in]  stack    Pixel values.
     * @param[in]  n        Number of valid elements to consider.
     * @param[out] gStat    Computed Grubbs statistic.
     * @param[out] maxIndex Index of the most extreme sample.
     */
    static void grubbsStatistic(
        const std::vector<float>& stack, int n,
        float& gStat, int& maxIndex);

    /**
     * @brief Test whether the Grubbs statistic exceeds its critical value.
     * @return true if gStat > criticalValue (i.e., sample is an outlier).
     */
    static bool checkGValue(float gStat, float criticalValue);

    /**
     * @brief Classify confirmed GESDT outliers as low or high rejections.
     *
     * @param[in]     outliers    Vector of (value, originalIndex) pairs.
     * @param[in]     numOutliers Number of confirmed outliers.
     * @param[in]     median      Reference median for direction classification.
     * @param[in,out] rejected    Rejection flag array to update.
     * @param[in,out] result      RejectionResult counters to update.
     */
    static void confirmGesdtOutliers(
        const std::vector<std::pair<float, int>>& outliers,
        int numOutliers, float median,
        std::vector<int>& rejected,
        RejectionResult& result);
};

} // namespace Stacking

#endif // STACKING_REJECTION_ALGORITHMS_H