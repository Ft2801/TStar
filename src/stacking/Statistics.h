/**
 * @file Statistics.h
 * @brief Statistical functions for stacking operations
 * 
 * This file provides fast, optimized statistical functions needed
 * for stacking, rejection algorithms, and normalization.
 * 
 * Copyright (C) 2024-2026 TStar Team
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
 * @brief Statistical utility functions
 * 
 * Provides optimized statistical computations needed for stacking.
 * All functions are designed to work with floating-point data.
 */
class Statistics {
public:
    /**
     * @brief Compute mean of data
     * @param data Input data
     * @param size Number of elements
     * @return Mean value
     */
    static double mean(const float* data, size_t size);
    static double mean(const std::vector<float>& data);
    
    /**
     * @brief Compute mean and standard deviation together (more efficient)
     * @param data Input data
     * @param size Number of elements
     * @param outMean Output mean
     * @param outStdDev Output standard deviation
     */
    static void meanAndStdDev(const float* data, size_t size,
                              double& outMean, double& outStdDev);
    
    /**
     * @brief Compute standard deviation
     * @param data Input data
     * @param size Number of elements
     * @param mean Optional pre-computed mean (nullptr to compute)
     * @return Standard deviation
     */
    static double stdDev(const float* data, size_t size, const double* mean = nullptr);
    static double stdDev(const std::vector<float>& data, const double* mean = nullptr);
    
    /**
     * @brief Compute median (modifies input array!)
     * @param data Input data (will be partially sorted)
     * @param size Number of elements
     * @return Median value
     */
    static float quickMedian(float* data, size_t size);
    static float quickMedian(std::vector<float>& data);
    
    /**
     * @brief Compute median without modifying input
     * @param data Input data
     * @param size Number of elements
     * @return Median value
     */
    static float median(const float* data, size_t size);
    static float median(const std::vector<float>& data);
    
    /**
     * @brief Compute Median Absolute Deviation (MAD)
     * @param data Input data
     * @param size Number of elements
     * @param median Pre-computed median (or 0 to compute)
     * @return MAD value
     */
    static double mad(const float* data, size_t size, float median = 0.0f);
    static double mad(const std::vector<float>& data, float median = 0.0f);

    /**
     * @brief Estimate noise in image (using MAD of differences or similar)
     * 
     * Uses efficient estimator suitable for large images.
     * 
     * @param data Image data
     * @param width Image width
     * @param height Image height
     * @return Noise estimate (sigma)
     */
    static double computeNoise(const float* data, int width, int height);
    
    /**
     * @brief Compute percentile value
     * @param data Input data (will be partially sorted)
     * @param size Number of elements
     * @param percentile Percentile (0-100)
     * @return Value at percentile
     */
    static float percentile(float* data, size_t size, double percentile);
    
    /**
     * @brief Compute minimum value
     * @param data Input data
     * @param size Number of elements
     * @return Minimum value
     */
    static float minimum(const float* data, size_t size);
    
    /**
     * @brief Compute maximum value
     * @param data Input data
     * @param size Number of elements
     * @return Maximum value
     */
    static float maximum(const float* data, size_t size);
    
    /**
     * @brief Compute min and max together (more efficient)
     * @param data Input data
     * @param size Number of elements
     * @param outMin Output minimum
     * @param outMax Output maximum
     */
    static void minMax(const float* data, size_t size,
                       float& outMin, float& outMax);
    
    /**
     * @brief Compute histogram-based median (faster for large arrays)
     * @param data Input data
     * @param size Number of elements
     * @param numBins Number of histogram bins
     * @return Approximate median
     */
    static float histogramMedian(const float* data, size_t size, int numBins = 65536);
    
    /**
     * @brief IKSS (Iterative K-Sigma Sigma-clipping) location estimator
     * 
     * More robust than mean for background estimation.
     * 
     * @param data Input data
     * @param size Number of elements
     * @param median Pre-computed median
     * @param mad Pre-computed MAD
     * @param outLocation Output location estimate
     * @param outScale Output scale estimate
     */
    static void ikssEstimator(const float* data, size_t size,
                              float median, float mad,
                              double& outLocation, double& outScale);
    
    /**
     * @brief Simple linear regression
     * @param x X values
     * @param y Y values
     * @param size Number of points
     * @param outSlope Output slope (a)
     * @param outIntercept Output intercept (b)
     */
    static void linearFit(const float* x, const float* y, size_t size,
                          float& outSlope, float& outIntercept);
    
    /**
     * @brief Compute weighted mean
     * @param data Input data
     * @param weights Weights
     * @param size Number of elements
     * @return Weighted mean
     */
    static double weightedMean(const float* data, const float* weights, size_t size);
    
    /**
     * @brief Quick partial sort (nth element)
     * 
     * Places the nth smallest element at position n and partitions
     * the array such that elements before n are smaller.
     * 
     * @param data Input data (modified)
     * @param size Number of elements
     * @param n Position to partition on
     */
    static void quickSelect(float* data, size_t size, size_t n);
    
    /**
     * @brief Full quicksort
     * @param data Input data (modified)
     * @param size Number of elements
     */
    static void quickSort(float* data, size_t size);
    
private:
    /**
     * @brief Partition helper for quickselect/quicksort
     */
    static size_t partition(float* data, size_t left, size_t right, size_t pivotIndex);
    
    /**
     * @brief QuickSelect implementation
     */
    static float quickSelectImpl(float* data, size_t left, size_t right, size_t k);
    
    /**
     * @brief QuickSort implementation
     */
    static void quickSortImpl(float* data, size_t left, size_t right);
};

} // namespace Stacking

#endif // STACKING_STATISTICS_H
