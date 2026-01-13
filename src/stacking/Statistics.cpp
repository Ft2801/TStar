/**
 * @file Statistics.cpp
 * @brief Implementation of statistical functions for stacking
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "Statistics.h"
#include <cstring>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace Stacking {

//=============================================================================
// MEAN
//=============================================================================

double Statistics::mean(const float* data, size_t size) {
    if (size == 0) return 0.0;
    
    double sum = 0.0;
    #ifdef _OPENMP
    #pragma omp parallel for reduction(+:sum) if(size > 10000)
    #endif
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    return sum / static_cast<double>(size);
}

double Statistics::mean(const std::vector<float>& data) {
    return mean(data.data(), data.size());
}

//=============================================================================
// MEAN AND STANDARD DEVIATION
//=============================================================================

void Statistics::meanAndStdDev(const float* data, size_t size,
                                double& outMean, double& outStdDev) {
    if (size == 0) {
        outMean = 0.0;
        outStdDev = 0.0;
        return;
    }
    
    // Two-pass algorithm for numerical stability
    double sum = 0.0;
    #ifdef _OPENMP
    #pragma omp parallel for reduction(+:sum) if(size > 10000)
    #endif
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    outMean = sum / static_cast<double>(size);
    
    double sumSq = 0.0;
    #ifdef _OPENMP
    #pragma omp parallel for reduction(+:sumSq) if(size > 10000)
    #endif
    for (size_t i = 0; i < size; ++i) {
        double diff = data[i] - outMean;
        sumSq += diff * diff;
    }
    
    outStdDev = std::sqrt(sumSq / static_cast<double>(size - 1));
}

double Statistics::stdDev(const float* data, size_t size, const double* precomputedMean) {
    if (size <= 1) return 0.0;
    
    double m = precomputedMean ? *precomputedMean : mean(data, size);
    
    double sumSq = 0.0;
    #ifdef _OPENMP
    #pragma omp parallel for reduction(+:sumSq) if(size > 10000)
    #endif
    for (size_t i = 0; i < size; ++i) {
        double diff = data[i] - m;
        sumSq += diff * diff;
    }
    
    return std::sqrt(sumSq / static_cast<double>(size - 1));
}

double Statistics::stdDev(const std::vector<float>& data, const double* precomputedMean) {
    return stdDev(data.data(), data.size(), precomputedMean);
}

//=============================================================================
// MEDIAN
//=============================================================================

float Statistics::quickMedian(float* data, size_t size) {
    if (size == 0) return 0.0f;
    if (size == 1) return data[0];
    if (size == 2) return (data[0] + data[1]) * 0.5f;
    
    size_t mid = size / 2;
    quickSelect(data, size, mid);
    
    if (size % 2 == 0) {
        // Even: average of two middle elements
        // Need to find max of lower half
        float maxLower = data[0];
        for (size_t i = 1; i < mid; ++i) {
            if (data[i] > maxLower) maxLower = data[i];
        }
        return (maxLower + data[mid]) * 0.5f;
    }
    
    return data[mid];
}

float Statistics::quickMedian(std::vector<float>& data) {
    return quickMedian(data.data(), data.size());
}

float Statistics::median(const float* data, size_t size) {
    if (size == 0) return 0.0f;
    
    // Make a copy to avoid modifying input
    std::vector<float> copy(data, data + size);
    return quickMedian(copy);
}

float Statistics::median(const std::vector<float>& data) {
    return median(data.data(), data.size());
}

//=============================================================================
// MAD (Median Absolute Deviation)
//=============================================================================

double Statistics::mad(const float* data, size_t size, float med) {
    if (size == 0) return 0.0;
    
    // Compute median if not provided
    if (med == 0.0f) {
        med = median(data, size);
    }
    
    // Compute absolute deviations
    std::vector<float> deviations(size);
    #ifdef _OPENMP
    #pragma omp parallel for if(size > 10000)
    #endif
    for (int64_t i = 0; i < (int64_t)size; ++i) {
        deviations[i] = std::abs(data[i] - med);
    }
    
    // MAD is median of absolute deviations
    return quickMedian(deviations);
}

double Statistics::mad(const std::vector<float>& data, float med) {
    return mad(data.data(), data.size(), med);
}

//=============================================================================
// NOISE ESTIMATION
//=============================================================================

double Statistics::computeNoise(const float* data, int width, int height) {
    if (width < 3 || height < 2) return 0.0;
    
    // Siril's FnNoise1 implementation:
    // noise = 1.0 / sqrt(2) * rms of (flux[i] - flux[i-1])
    // using iterative sigma clipping (3 iters, 5.0 sigma)
    
    int step = 1;
    if (height > 1000) step = height / 500; // Sample ~500 rows for speed
    
    std::vector<double> rowNoises;
    rowNoises.reserve(height / step);

    const int NITER = 3;
    const double SIGMA_CLIP = 5.0;

    for (int y = 0; y < height; y += step) {
        const float* row = data + y * width;
        std::vector<float> diffs;
        diffs.reserve(width - 1);

        // 1st order differences
        for (int x = 0; x < width - 1; ++x) {
            if (row[x] != 0.0f && row[x+1] != 0.0f) {
                diffs.push_back(row[x] - row[x+1]);
            }
        }

        if (diffs.size() < 2) continue;

        // Iterative sigma clipping
        double mean = 0.0;
        double stdev = 0.0;
        
        // Initial pass
        double sum = 0.0;
        for(float v : diffs) sum += v;
        mean = sum / diffs.size();
        
        double sumSq = 0.0;
        for(float v : diffs) sumSq += (v - mean) * (v - mean);
        stdev = std::sqrt(sumSq / (diffs.size() - 1));

        if (stdev > 0.0) {
            for (int iter = 0; iter < NITER; iter++) {
                size_t keptCount = 0;
                double newSum = 0.0;
                
                // Filter outliers
                for (size_t i = 0; i < diffs.size(); ++i) {
                    if (std::abs(diffs[i] - mean) < SIGMA_CLIP * stdev) {
                        if (keptCount < i) diffs[keptCount] = diffs[i];
                        newSum += diffs[keptCount];
                        keptCount++;
                    }
                }

                if (keptCount == diffs.size()) break;
                if (keptCount < 2) break;

                diffs.resize(keptCount);
                mean = newSum / keptCount;
                
                double newSumSq = 0.0;
                for(float v : diffs) newSumSq += (v - mean) * (v - mean);
                stdev = std::sqrt(newSumSq / (keptCount - 1));
            }
        }

        rowNoises.push_back(stdev);
    }

    if (rowNoises.empty()) return 0.0;

    // Siril takes the median of row standard deviations
    std::sort(rowNoises.begin(), rowNoises.end());
    double medianStdev = rowNoises[rowNoises.size() / 2];

    return 0.70710678118 * medianStdev; // 1/sqrt(2)
}

//=============================================================================
// PERCENTILE
//=============================================================================

float Statistics::percentile(float* data, size_t size, double pct) {
    if (size == 0) return 0.0f;
    if (pct <= 0.0) return minimum(data, size);
    if (pct >= 100.0) return maximum(data, size);
    
    double idx = (pct / 100.0) * (size - 1);
    size_t lower = static_cast<size_t>(idx);
    size_t upper = lower + 1;
    
    if (upper >= size) {
        quickSelect(data, size, lower);
        return data[lower];
    }
    
    // Get both required elements
    quickSelect(data, size, lower);
    float lowerVal = data[lower];
    
    // Find minimum of upper portion
    float upperVal = data[upper];
    for (size_t i = upper + 1; i < size; ++i) {
        if (data[i] < upperVal) upperVal = data[i];
    }
    
    // Linear interpolation
    double frac = idx - lower;
    return lowerVal + static_cast<float>(frac) * (upperVal - lowerVal);
}

//=============================================================================
// MIN/MAX
//=============================================================================

float Statistics::minimum(const float* data, size_t size) {
    if (size == 0) return 0.0f;
    
    float minVal = data[0];
    for (size_t i = 1; i < size; ++i) {
        if (data[i] < minVal) minVal = data[i];
    }
    return minVal;
}

float Statistics::maximum(const float* data, size_t size) {
    if (size == 0) return 0.0f;
    
    float maxVal = data[0];
    for (size_t i = 1; i < size; ++i) {
        if (data[i] > maxVal) maxVal = data[i];
    }
    return maxVal;
}

void Statistics::minMax(const float* data, size_t size,
                        float& outMin, float& outMax) {
    if (size == 0) {
        outMin = outMax = 0.0f;
        return;
    }
    
    outMin = outMax = data[0];
    #ifdef _OPENMP
    #pragma omp parallel for reduction(min:outMin) reduction(max:outMax) if(size > 10000)
    #endif
    for (size_t i = 1; i < size; ++i) {
        if (data[i] < outMin) outMin = data[i];
        if (data[i] > outMax) outMax = data[i];
    }
}

//=============================================================================
// HISTOGRAM MEDIAN (for large arrays)
//=============================================================================

float Statistics::histogramMedian(const float* data, size_t size, int numBins) {
    if (size == 0) return 0.0f;
    
    // Find range
    float minVal, maxVal;
    minMax(data, size, minVal, maxVal);
    
    if (minVal == maxVal) return minVal;
    
    // Build histogram
    std::vector<int> histogram(numBins, 0);
    float scale = static_cast<float>(numBins - 1) / (maxVal - minVal);
    
    for (size_t i = 0; i < size; ++i) {
        int bin = static_cast<int>((data[i] - minVal) * scale);
        bin = std::max(0, std::min(bin, numBins - 1));
        histogram[bin]++;
    }
    
    // Find median bin
    size_t target = size / 2;
    size_t count = 0;
    int medianBin = 0;
    
    for (int i = 0; i < numBins; ++i) {
        count += histogram[i];
        if (count >= target) {
            medianBin = i;
            break;
        }
    }
    
    // Convert back to value
    return minVal + static_cast<float>(medianBin) / scale;
}

//=============================================================================
// IKSS ESTIMATOR
//=============================================================================

void Statistics::ikssEstimator(const float* data, size_t size,
                               float med, float madVal,
                               double& outLocation, double& outScale) {
    
    if (size == 0) {
        outLocation = 0.0;
        outScale = 0.0;
        return;
    }
    
    // Initial estimates
    outLocation = med;
    outScale = 1.4826 * madVal;  // Consistent estimator for Gaussian
    
    const int maxIter = 10; // Reduced from 100 as robust stats usually converge fast
    const double tolerance = 1e-4;
    
    for (int iter = 0; iter < maxIter; ++iter) {
        double sumX = 0.0;
        double sumW = 0.0;
        
        // Compute weighted mean using Huber weights
        #ifdef _OPENMP
        #pragma omp parallel for reduction(+:sumX,sumW) if(size > 10000)
        #endif
        for (int64_t i = 0; i < (int64_t)size; ++i) {
            double z = std::abs(data[i] - outLocation) / outScale;
            double w = (z < 1.5) ? 1.0 : 1.5 / z;
            sumX += w * data[i];
            sumW += w;
        }
        
        if (sumW == 0.0) break;
        double newLocation = sumX / sumW;
        
        // Compute new scale
        double sumSq = 0.0;
        #ifdef _OPENMP
        #pragma omp parallel for reduction(+:sumSq) if(size > 10000)
        #endif
        for (int64_t i = 0; i < (int64_t)size; ++i) {
            double z = std::abs(data[i] - newLocation) / outScale;
            double w = (z < 1.5) ? 1.0 : 1.5 / z;
            double diff = data[i] - newLocation;
            sumSq += w * diff * diff;
        }
        
        double newScale = std::sqrt(sumSq / sumW);
        if (newScale < 1e-10) newScale = 1e-10;
        
        // Check convergence
        if (std::abs(newLocation - outLocation) < tolerance * outScale &&
            std::abs(newScale - outScale) < tolerance * outScale) {
            outLocation = newLocation;
            outScale = newScale;
            break;
        }
        
        outLocation = newLocation;
        outScale = newScale;
    }
}

//=============================================================================
// LINEAR FIT
//=============================================================================

void Statistics::linearFit(const float* x, const float* y, size_t size,
                           float& outSlope, float& outIntercept) {
    // Simple linear regression: y = ax + b
    // Using least squares method
    
    if (size < 2) {
        outSlope = 0.0f;
        outIntercept = (size == 1) ? y[0] : 0.0f;
        return;
    }
    
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
    
    for (size_t i = 0; i < size; ++i) {
        sumX += x[i];
        sumY += y[i];
        sumXY += x[i] * y[i];
        sumX2 += x[i] * x[i];
    }
    
    double n = static_cast<double>(size);
    double denom = n * sumX2 - sumX * sumX;
    
    if (std::abs(denom) < 1e-10) {
        // Vertical line or single point
        outSlope = 0.0f;
        outIntercept = static_cast<float>(sumY / n);
        return;
    }
    
    outSlope = static_cast<float>((n * sumXY - sumX * sumY) / denom);
    outIntercept = static_cast<float>((sumY - outSlope * sumX) / n);
}

//=============================================================================
// WEIGHTED MEAN
//=============================================================================

double Statistics::weightedMean(const float* data, const float* weights, size_t size) {
    if (size == 0) return 0.0;
    
    double sumWX = 0.0;
    double sumW = 0.0;
    
    for (size_t i = 0; i < size; ++i) {
        sumWX += weights[i] * data[i];
        sumW += weights[i];
    }
    
    return (sumW > 0) ? sumWX / sumW : 0.0;
}

//=============================================================================
// QUICKSELECT AND QUICKSORT
//=============================================================================

void Statistics::quickSelect(float* data, size_t size, size_t n) {
    if (size <= 1 || n >= size) return;
    std::nth_element(data, data + n, data + size);
}

float Statistics::quickSelectImpl(float* data, size_t left, size_t right, size_t k) {
    while (left < right) {
        // Choose pivot as median of three
        size_t mid = left + (right - left) / 2;
        
        // Sort left, mid, right
        if (data[mid] < data[left]) std::swap(data[left], data[mid]);
        if (data[right] < data[left]) std::swap(data[left], data[right]);
        if (data[right] < data[mid]) std::swap(data[mid], data[right]);
        
        // Use middle as pivot
        std::swap(data[mid], data[right - 1]);
        size_t pivotIndex = partition(data, left, right - 1, right - 1);
        
        if (k == pivotIndex) {
            return data[k];
        } else if (k < pivotIndex) {
            right = pivotIndex - 1;
        } else {
            left = pivotIndex + 1;
        }
    }
    
    return data[left];
}

size_t Statistics::partition(float* data, size_t left, size_t right, size_t pivotIndex) {
    float pivot = data[pivotIndex];
    std::swap(data[pivotIndex], data[right]);
    
    size_t storeIndex = left;
    for (size_t i = left; i < right; ++i) {
        if (data[i] < pivot) {
            std::swap(data[i], data[storeIndex]);
            storeIndex++;
        }
    }
    
    std::swap(data[storeIndex], data[right]);
    return storeIndex;
}

void Statistics::quickSort(float* data, size_t size) {
    if (size <= 1) return;
    quickSortImpl(data, 0, size - 1);
}

void Statistics::quickSortImpl(float* data, size_t left, size_t right) {
    if (left >= right) return;
    
    // Use insertion sort for small arrays
    if (right - left < 16) {
        for (size_t i = left + 1; i <= right; ++i) {
            float key = data[i];
            size_t j = i;
            while (j > left && data[j - 1] > key) {
                data[j] = data[j - 1];
                --j;
            }
            data[j] = key;
        }
        return;
    }
    
    // Choose pivot as median of three
    size_t mid = left + (right - left) / 2;
    if (data[mid] < data[left]) std::swap(data[left], data[mid]);
    if (data[right] < data[left]) std::swap(data[left], data[right]);
    if (data[right] < data[mid]) std::swap(data[mid], data[right]);
    
    std::swap(data[mid], data[right - 1]);
    size_t pivotIndex = partition(data, left + 1, right - 1, right - 1);
    
    if (pivotIndex > left) {
        quickSortImpl(data, left, pivotIndex - 1);
    }
    if (pivotIndex < right) {
        quickSortImpl(data, pivotIndex + 1, right);
    }
}

} // namespace Stacking
