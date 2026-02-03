#include "RobustStatistics.h"
#include <algorithm>
#include <cmath>
#include <cassert>
#include <omp.h>
#include <iostream>

namespace RobustStatistics {

    // Helper Macros
    #define LIM(v, min, max) ((v) < (min) ? (min) : ((v) > (max) ? (max) : (v)))

    // --- PORT START: Standard Robust Stats ---

    // 1. Qn0 Scale Estimator
    // Implementation: Naive O(N^2) pairwise differences.
    // Justification: In stacking contexts, N is typically small (number of subs, e.g., 20-300).
    // For N < 1000, the O(N^2) overhead is negligible compared to file I/O and registration.
    // Optimization to O(N log N) is possible but not urgent given typical usage profiles.
    static float Qn0(const std::vector<float>& sorted_data) {
        size_t n = sorted_data.size();
        if (n < 2) return 0.0f;
        
        size_t wsize = n * (n - 1) / 2;
        std::vector<float> work;
        work.reserve(wsize);
        
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                work.push_back(std::fabs(sorted_data[i] - sorted_data[j]));
            }
        }
        
        // Find k-th order statistic
        // Calculate k-th order statistic using standard formula: ((n/2 + 1) * n/2) / 2
        size_t n_2 = n / 2;
        size_t k = ((n_2 + 1) * n_2) / 2;
        
        if (work.empty()) return 0.0f;
        if (k >= work.size()) k = work.size() - 1;
        
        std::nth_element(work.begin(), work.begin() + k, work.end());
        return work[k];
    }
    
    // 2. Standard Trimmed Mean (Fallback)
    static float standardTrimmedMean(const std::vector<float>& sorted_data, float trim) {
        size_t n = sorted_data.size();
        if (n == 0) return 0.0f;
        size_t trimCount = (size_t)(n * trim);
        if (trimCount * 2 >= n) return sorted_data[n/2]; // Fallback median
        
        double sum = 0;
        size_t count = 0;
        for (size_t i = trimCount; i < n - trimCount; ++i) {
            sum += sorted_data[i];
            count++;
        }
        return (count > 0) ? (float)(sum / count) : 0.0f;
    }

    // 3. Standard Robust Mean
    // Uses Qn for rejection, then updates mean
    float standardRobustMean(std::vector<float>& data) {
        if (data.empty()) return 0.0f;
        
        // 1. Sort
        std::sort(data.begin(), data.end());
        
        // 2. Initial Median & Scale
        float med = getMedian(data);
        float qn = Qn0(data);
        
        if (qn < 0) return -1.0f; 
        
        // Standard uses constant 2.2219 (Consistency for Qn normal)
        // Threshold is 3 * sx where sx = 2.2219 * qn
        float sx = 2.2219f * qn;
        float threshold = 3.0f * sx;
        
        // 3. Filter Inliers
        std::vector<float> inliers;
        inliers.reserve(data.size());
        double sum = 0;
        
        for (float v : data) {
            if (std::fabs(v - med) <= threshold) {
                inliers.push_back(v);
                sum += v;
            }
        }
        
        // 4. Final Calculation
        if (inliers.size() < 5) {
            // Not enough inliers? Use Trimmed Mean of original data (30% trim)
            return standardTrimmedMean(data, 0.3f);
        } else {
            return (float)(sum / inliers.size());
        }
    }
    
    // --- PORT END ---

    void findMinMaxPercentile(const float* data, size_t size, float minPrct, float* minOut, float maxPrct, float* maxOut, int threads)
    {
        assert(minPrct <= maxPrct);

        if (size == 0) {
            if (minOut) *minOut = 0.0f;
            if (maxOut) *maxOut = 0.0f;
            return;
        }

        size_t numThreads = 1;
    #ifdef _OPENMP
        // Heuristic from RT to reduce thread overhead for small data
        if (threads > 1) {
            const size_t maxThreads = threads;
            while (size > numThreads * numThreads * 16384 && numThreads < maxThreads) {
                ++numThreads;
            }
        }
    #endif

        // We need min and max value of data to calculate the scale factor for the histogram
        float minVal = data[0];
        float maxVal = data[0];
    #ifdef _OPENMP
        #pragma omp parallel for reduction(min:minVal) reduction(max:maxVal) num_threads(numThreads)
    #endif
        for (size_t i = 1; i < size; ++i) {
            minVal = std::min(minVal, data[i]);
            maxVal = std::max(maxVal, data[i]);
        }

        if (std::fabs(maxVal - minVal) == 0.f) { 
            if (minOut) *minOut = minVal;
            if (maxOut) *maxOut = minVal;
            return;
        }

        // Calibration for histogram bucket size
        const unsigned int histoSize = std::min<size_t>(65536, size);
        const float scale = (histoSize - 1) / (maxVal - minVal);

        std::vector<uint32_t> histo(histoSize, 0);

        if (numThreads == 1) {
            for (size_t i = 0; i < size; ++i) {
                histo[static_cast<uint16_t>(scale * (data[i] - minVal))]++;
            }
        } else {
    #ifdef _OPENMP
            #pragma omp parallel num_threads(numThreads)
    #endif
            {
                std::vector<uint32_t> histothr(histoSize, 0);

    #ifdef _OPENMP
                #pragma omp for nowait
    #endif
                for (size_t i = 0; i < size; ++i) {
                    histothr[static_cast<uint16_t>(scale * (data[i] - minVal))]++;
                }

    #ifdef _OPENMP
                #pragma omp critical
    #endif
                {
                    for (size_t i = 0; i < histoSize; ++i) {
                        histo[i] += histothr[i];
                    }
                }
            }
        }

        size_t k = 0;
        size_t count = 0;

        // Min Percentile
        if (minOut) {
            const float threshmin = minPrct * size;
            while (count < threshmin && k < histoSize) {
                count += histo[k++];
            }
            if (k > 0) {
                const size_t count_ = count - histo[k - 1];
                const float c0 = count - threshmin;
                const float c1 = threshmin - count_;
                *minOut = (c1 * k + c0 * (k - 1)) / (c0 + c1);
            } else {
                *minOut = (float)k;
            }
            *minOut /= scale;
            *minOut += minVal;
            *minOut = LIM(*minOut, minVal, maxVal);
        }
        
        // Calculate Max Percentile, continuing accumulation from previous state if applicable
        
        if (maxOut) {
            const float threshmax = maxPrct * size;
            while (count < threshmax && k < histoSize) {
                count += histo[k++];
            }
             if (k > 0) {
                const size_t count_ = count - histo[k - 1];
                const float c0 = count - threshmax;
                const float c1 = threshmax - count_;
                *maxOut = (c1 * k + c0 * (k - 1)) / (c0 + c1);
            } else {
                *maxOut = (float)k;
            }
            *maxOut /= scale;
            *maxOut += minVal;
            *maxOut = LIM(*maxOut, minVal, maxVal);
        }
    }
    // --- PORT END ---

    float getMedian(const std::vector<float>& data) {
        if (data.empty()) return 0.0f;
        float med;
        // Use 4 threads default? Or max?
        findMinMaxPercentile(data.data(), data.size(), 0.5f, &med, 0.5f, nullptr, omp_get_max_threads());
        return med;
    }
    
    float getMAD(const std::vector<float>& data, float median) {
        if (data.empty()) return 0.0f;
        
        // We need abs diffs.
        std::vector<float> diffs(data.size());
        #pragma omp parallel for
        for (size_t i = 0; i < data.size(); ++i) {
            diffs[i] = std::fabs(data[i] - median);
        }
        
        float mad;
        findMinMaxPercentile(diffs.data(), diffs.size(), 0.5f, &mad, 0.5f, nullptr, omp_get_max_threads());
        return mad;
    }

    // --- PORT START: Repeated Median Fit (Siegel's) ---
    static double getMedianD(std::vector<double>& v) {
        if (v.empty()) return 0.0;
        size_t n = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + n, v.end());
        if (v.size() % 2 != 0) {
            return v[n];
        } else {
            // Even: average of n and n-1
            double val1 = v[n];
            // We need the element before it
            std::nth_element(v.begin(), v.begin() + n - 1, v.end()); 
            return (val1 + v[n-1]) * 0.5;
        }
    }

    bool repeatedMedianFit(const std::vector<double>& x, const std::vector<double>& y, 
                          double& slope, double& intercept, double& sigma) 
    {
        size_t n = x.size();
        if (n < 2) return false;

        std::vector<double> pointMedians(n);

        // 1. Calculate median slope for each point i with all other points j
        for (size_t i = 0; i < n; ++i) {
            std::vector<double> slopes;
            slopes.reserve(n - 1);
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                // Avoid division by zero
                if (std::abs(x[j] - x[i]) > 1e-9) {
                    slopes.push_back((y[j] - y[i]) / (x[j] - x[i]));
                }
            }
            if (!slopes.empty()) {
                pointMedians[i] = getMedianD(slopes);
            } else {
                pointMedians[i] = 0.0; 
            }
        }

        // 2. Slope is median of point medians
        std::vector<double> pointMediansCopy = pointMedians; // Copy for median calc
        slope = getMedianD(pointMediansCopy);

        // 3. Intercepts
        std::vector<double> intercepts(n);
        for (size_t i = 0; i < n; ++i) {
            intercepts[i] = y[i] - slope * x[i];
        }
        intercept = getMedianD(intercepts);

        // 4. Residuals & Sigma
        std::vector<double> residuals(n);
        double sumSq = 0;
        for (size_t i = 0; i < n; ++i) {
            double res = y[i] - (intercept + slope * x[i]);
            residuals[i] = std::abs(res);
            sumSq += res * res;
        }

        // Calculate MAD of residuals
        double mad = getMedianD(residuals); // residuals is modified inplace but it's local
        double madSigma = mad * 1.4826;

        // Inliers check (Standard uses 3.0 * madSigma as cutoff)
        int inliers = 0;
        double robustSumSq = 0;
        for (size_t i = 0; i < n; ++i) {
            // Recompute residual as 'residuals' vector was shuffled/sorted
            double res = y[i] - (intercept + slope * x[i]);
            if (std::abs(res) <= 3.0 * madSigma) {
                inliers++;
                robustSumSq += res * res;
            }
        }
        
        if (inliers > 2) {
            sigma = std::sqrt(robustSumSq / inliers);
        } else {
            sigma = std::sqrt(sumSq / n);
        }

        return true;
    }
    // --- PORT END ---
}
