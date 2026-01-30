
#ifndef STACKING_REJECTION_ALGORITHMS_H
#define STACKING_REJECTION_ALGORITHMS_H

#include "StackingTypes.h"
#include "Statistics.h"
#include <vector>

namespace Stacking {

/**
 * @brief Result of applying rejection to a pixel stack
 */
struct RejectionResult {
    int keptCount = 0;       ///< Number of pixels kept after rejection
    int lowRejected = 0;     ///< Number of low (dark) rejections
    int highRejected = 0;    ///< Number of high (bright) rejections
    
    int totalRejected() const { return lowRejected + highRejected; }
};

/**
 * @brief Pixel rejection algorithms for stacking
 * 
 * Implements various algorithms to identify and reject outlier pixels
 * from a stack before computing the final value. Each algorithm has
 * different characteristics suitable for different scenarios.
 */
class RejectionAlgorithms {
public:
    /**
     * @brief Apply rejection algorithm to a pixel stack
     * 
     * @param stack Stack of pixel values from all images (modified in place)
     * @param type Type of rejection algorithm to use
     * @param sigmaLow Low rejection threshold (sigma or percentile)
     * @param sigmaHigh High rejection threshold (sigma or percentile)
     * @param rejected Output: rejection status for each pixel
     *                 0 = kept, -1 = rejected low, +1 = rejected high
     * @param weights Optional: weights per pixel (for weighted stacking)
     * @param drizzleWeights Optional: drizzle weights per pixel
     * @return RejectionResult containing rejection statistics
     */
    static RejectionResult apply(
        std::vector<float>& stack,
        Rejection type,
        float sigmaLow,
        float sigmaHigh,
        std::vector<int>& rejected,
        const std::vector<float>* weights = nullptr,
        const std::vector<float>* drizzleWeights = nullptr,
        std::vector<float>* scratch = nullptr
    );
    
    /**
     * @brief Percentile clipping rejection
     * 
     * Rejects pixels based on their percentage deviation from median.
     * Simple and fast but can be less accurate.
     * 
     * @param stack Pixel stack (not modified)
     * @param pLow Low percentile threshold (0-1)
     * @param pHigh High percentile threshold (0-1)
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult percentileClipping(
        const std::vector<float>& stack,
        float pLow, float pHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr
    );
    
    /**
     * @brief Sigma clipping rejection
     * 
     * Classic sigma clipping: rejects pixels more than N sigma from median.
     * Iterative until no more rejections.
     * 
     * @param stack Pixel stack (modified for efficiency)
     * @param sigmaLow Low sigma threshold
     * @param sigmaHigh High sigma threshold
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult sigmaClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr,
        const std::vector<float>* weights = nullptr
    );
    
    /**
     * @brief MAD (Median Absolute Deviation) clipping
     * 
     * Like sigma clipping but uses MAD instead of standard deviation.
     * More robust against outliers.
     * 
     * @param stack Pixel stack (modified for efficiency)
     * @param sigmaLow Low threshold (in MAD units)
     * @param sigmaHigh High threshold (in MAD units)
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult madClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr
    );
    
    /**
     * @brief Sigma-median clipping
     * 
     * Instead of removing outliers, replaces them with median.
     * Useful when you want to keep all pixel positions.
     * 
     * @param stack Pixel stack (values are modified to median)
     * @param sigmaLow Low sigma threshold
     * @param sigmaHigh High sigma threshold
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult sigmaMedianClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr
    );
    
    /**
     * @brief Winsorized sigma clipping
     * 
     * More robust version of sigma clipping that first winsorizes
     * (clips extreme values) before computing sigma.
     * 
     * @param stack Pixel stack (modified for efficiency)
     * @param sigmaLow Low sigma threshold
     * @param sigmaHigh High sigma threshold
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult winsorizedClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr,
        const std::vector<float>* weights = nullptr
    );
    
    /**
     * @brief Linear fit clipping
     * 
     * Fits a line through sorted pixel values and rejects pixels
     * that deviate too much from the fit. Good for gradients.
     * 
     * @param stack Pixel stack (will be sorted)
     * @param sigmaLow Low sigma threshold
     * @param sigmaHigh High sigma threshold
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult linearFitClipping(
        std::vector<float>& stack,
        float sigmaLow, float sigmaHigh,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr
    );
    
    /**
     * @brief Generalized ESD Test (GESDT)
     * 
     * Statistical test for detecting multiple outliers. More rigorous
     * but slower than simple clipping methods.
     * 
     * @param stack Pixel stack (modified for efficiency)
     * @param outliersFraction Expected maximum fraction of outliers (0-1)
     * @param significance Significance level for test (e.g., 0.05)
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult gesdtClipping(
        std::vector<float>& stack,
        float outliersFraction, float significance,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr
    );
    
    /**
     * @brief Pre-compute critical values for GESDT
     * 
     * The GESDT algorithm needs critical values from the t-distribution.
     * Pre-computing them speeds up processing many pixels.
     * 
     * @param n Sample size
     * @param maxOutliers Maximum number of outliers to test for
     * @param significance Significance level
     * @return Vector of critical values
     */
    static std::vector<float> computeGesdtCriticalValues(
        int n, int maxOutliers, float significance
    );

    /**
     * @brief Biweight Estimator Rejection
     * 
     * Iteratively computes a robust center (biweight location) and scale (biweight midvariance).
     * Rejects pixels that have zero weight in the final iteration.
     * 
     * @param stack Pixel stack
     * @param tuningConstant Tuning constant (typically 6.0 or 9.0)
     * @param rejected Output rejection flags
     * @return Rejection statistics
     */
    static RejectionResult biweightClipping(
        std::vector<float>& stack,
        float tuningConstant,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr
    );
    
    /**
     * @brief Modified Z-Score Rejection
     * 
     * Uses Median and MAD to calculate robust Z-Scores.
     * Rejects pixels with |Z| > threshold.
     * 
     * @param stack Pixel stack
     * @param threshold Rejection threshold (default ~ 3.5)
     * @param rejected Output rejection flags
     */
    static RejectionResult modifiedZScoreClipping(
        std::vector<float>& stack,
        float threshold,
        std::vector<int>& rejected,
        std::vector<float>* scratch = nullptr
    );

    // =========================================================================
    // ROBUST STATS WITH WEIGHTS
    // =========================================================================

    /**
     * @brief Compute Weighted Median
     * 
     * @param data Pixel values
     * @param weights Weights for each pixel
     * @param validMask Optional mask (0 = invalid)
     * @return Weighted Median
     */
    static float weightedMedian(const std::vector<float>& data, 
                                const std::vector<float>& weights,
                                const std::vector<int>* validMask = nullptr);

    /**
     * @brief Compute Weighted Mean and Standard Deviation
     * 
     * @param data Pixel values
     * @param weights Weights for each pixel
     * @param validMask Optional mask
     * @return std::pair<mean, stdDev>
     */
    static std::pair<double, double> weightedMeanAndStdDev(
                                const std::vector<float>& data,
                                const std::vector<float>& weights,
                                const std::vector<int>* validMask = nullptr);
    
private:
    /**
     * @brief Remove null (zero) pixels from stack
     * @return Number of non-null pixels
     */
    static int removeNullPixels(std::vector<float>& stack, 
                                const std::vector<float>* drizzleWeights);
    
    /**
     * @brief Compact rejected pixels out of stack
     * @return Number of remaining pixels
     */
    static int compactStack(std::vector<float>& stack, 
                           const std::vector<int>& rejected);
    
    /**
     * @brief Compute Grubbs statistic for GESDT
     */
    static void grubbsStatistic(const std::vector<float>& stack, int n,
                                float& gStat, int& maxIndex);
    
    /**
     * @brief Check if G value exceeds critical threshold
     */
    static bool checkGValue(float gStat, float criticalValue);
    
    /**
     * @brief Confirm GESDT outliers and classify as low/high
     */
    static void confirmGesdtOutliers(
        const std::vector<std::pair<float, int>>& outliers,
        int numOutliers, float median,
        std::vector<int>& rejected,
        RejectionResult& result
    );
};

} // namespace Stacking

#endif // STACKING_REJECTION_ALGORITHMS_H
