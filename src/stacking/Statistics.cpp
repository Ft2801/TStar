/**
 * @file Statistics.cpp
 * @brief Implementation of the Statistics utility class.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "Statistics.h"

#include <cstring>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace Stacking {

// ============================================================================
// Mean
// ============================================================================

double Statistics::mean(const float* data, size_t size)
{
    if (size == 0) {
        return 0.0;
    }

    double sum = 0.0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:sum) if(size > 10000)
#endif
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    return sum / static_cast<double>(size);
}

double Statistics::mean(const std::vector<float>& data)
{
    return mean(data.data(), data.size());
}

// ============================================================================
// Mean and standard deviation (two-pass for numerical stability)
// ============================================================================

void Statistics::meanAndStdDev(const float* data, size_t size,
                               double& outMean, double& outStdDev)
{
    if (size == 0) {
        outMean   = 0.0;
        outStdDev = 0.0;
        return;
    }

    /* Pass 1: compute the mean. */
    double sum = 0.0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:sum) if(size > 10000)
#endif
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }
    outMean = sum / static_cast<double>(size);

    /* Pass 2: compute the sum of squared deviations. */
    double sumSq = 0.0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:sumSq) if(size > 10000)
#endif
    for (size_t i = 0; i < size; ++i) {
        const double diff = data[i] - outMean;
        sumSq += diff * diff;
    }

    outStdDev = std::sqrt(sumSq / static_cast<double>(size - 1));
}

double Statistics::stdDev(const float* data, size_t size,
                          const double* precomputedMean)
{
    if (size <= 1) {
        return 0.0;
    }

    const double m = precomputedMean ? *precomputedMean : mean(data, size);

    double sumSq = 0.0;
#ifdef _OPENMP
    #pragma omp parallel for reduction(+:sumSq) if(size > 10000)
#endif
    for (size_t i = 0; i < size; ++i) {
        const double diff = data[i] - m;
        sumSq += diff * diff;
    }

    return std::sqrt(sumSq / static_cast<double>(size - 1));
}

double Statistics::stdDev(const std::vector<float>& data,
                          const double* precomputedMean)
{
    return stdDev(data.data(), data.size(), precomputedMean);
}

// ============================================================================
// Median
// ============================================================================

float Statistics::quickMedian(float* data, size_t size)
{
    if (size == 0) return 0.0f;
    if (size == 1) return data[0];
    if (size == 2) return (data[0] + data[1]) * 0.5f;

    const size_t mid = size / 2;
    quickSelect(data, size, mid);

    if (size % 2 == 0) {
        /* For even-length arrays the median is the average of the two central values.
         * After quickSelect(mid), all elements before mid are <= data[mid],
         * so the lower-middle value is the maximum of the left partition. */
        float maxLower = data[0];
        for (size_t i = 1; i < mid; ++i) {
            if (data[i] > maxLower) {
                maxLower = data[i];
            }
        }
        return (maxLower + data[mid]) * 0.5f;
    }

    return data[mid];
}

float Statistics::quickMedian(std::vector<float>& data)
{
    return quickMedian(data.data(), data.size());
}

float Statistics::median(const float* data, size_t size)
{
    if (size == 0) return 0.0f;

    /* Work on a copy to keep the caller's data intact. */
    std::vector<float> copy(data, data + size);
    return quickMedian(copy);
}

float Statistics::median(const std::vector<float>& data)
{
    return median(data.data(), data.size());
}

// ============================================================================
// MAD (Median Absolute Deviation)
// ============================================================================

double Statistics::mad(const float* data, size_t size, float med)
{
    if (size == 0) return 0.0;

    /* Compute the median if the caller did not supply one. */
    if (med == 0.0f) {
        med = median(data, size);
    }

    /* Build the absolute-deviation array. */
    std::vector<float> deviations(size);
#ifdef _OPENMP
    #pragma omp parallel for if(size > 10000)
#endif
    for (int64_t i = 0; i < static_cast<int64_t>(size); ++i) {
        deviations[i] = std::abs(data[i] - med);
    }

    /* The MAD is the median of the absolute deviations. */
    return quickMedian(deviations);
}

double Statistics::mad(const std::vector<float>& data, float med)
{
    return mad(data.data(), data.size(), med);
}

// ============================================================================
// Noise estimation
// ============================================================================

double Statistics::computeNoise(const float* data, int width, int height)
{
    if (width < 3 || height < 2) {
        return 0.0;
    }

    /*
     * Estimate noise as (1 / sqrt(2)) * median(per-row sigma of first-order
     * differences), with iterative sigma clipping to suppress real structure.
     */

    /* Sub-sample rows when the image is tall to keep computation fast. */
    int step = 1;
    if (height > 1000) {
        step = height / 500;
    }

    std::vector<double> rowNoises;
    rowNoises.reserve(height / step);

    constexpr int    NITER      = 3;
    constexpr double SIGMA_CLIP = 5.0;

    for (int y = 0; y < height; y += step) {
        const float* row = data + y * width;

        /* Compute first-order differences, ignoring zero-valued pixels. */
        std::vector<float> diffs;
        diffs.reserve(width - 1);
        for (int x = 0; x < width - 1; ++x) {
            if (row[x] != 0.0f && row[x + 1] != 0.0f) {
                diffs.push_back(row[x] - row[x + 1]);
            }
        }
        if (diffs.size() < 2) {
            continue;
        }

        /* Initial mean and standard deviation. */
        double rowMean = 0.0;
        for (float v : diffs) {
            rowMean += v;
        }
        rowMean /= diffs.size();

        double rowSumSq = 0.0;
        for (float v : diffs) {
            rowSumSq += (v - rowMean) * (v - rowMean);
        }
        double rowStdev = std::sqrt(rowSumSq / (diffs.size() - 1));

        /* Iterative sigma clipping. */
        if (rowStdev > 0.0) {
            for (int iter = 0; iter < NITER; ++iter) {
                size_t keptCount = 0;
                double newSum    = 0.0;

                for (size_t j = 0; j < diffs.size(); ++j) {
                    if (std::abs(diffs[j] - rowMean) < SIGMA_CLIP * rowStdev) {
                        if (keptCount < j) {
                            diffs[keptCount] = diffs[j];
                        }
                        newSum += diffs[keptCount];
                        ++keptCount;
                    }
                }
                if (keptCount == diffs.size() || keptCount < 2) {
                    break;
                }

                diffs.resize(keptCount);
                rowMean = newSum / keptCount;

                double newSumSq = 0.0;
                for (float v : diffs) {
                    newSumSq += (v - rowMean) * (v - rowMean);
                }
                rowStdev = std::sqrt(newSumSq / (keptCount - 1));
            }
        }

        rowNoises.push_back(rowStdev);
    }

    if (rowNoises.empty()) {
        return 0.0;
    }

    std::sort(rowNoises.begin(), rowNoises.end());
    const double medianStdev = rowNoises[rowNoises.size() / 2];

    /* Scale by 1/sqrt(2) to convert difference-noise to per-pixel noise. */
    return 0.70710678118 * medianStdev;
}

// ============================================================================
// Percentile
// ============================================================================

float Statistics::percentile(float* data, size_t size, double pct)
{
    if (size == 0)       return 0.0f;
    if (pct <= 0.0)      return minimum(data, size);
    if (pct >= 100.0)    return maximum(data, size);

    const double idx   = (pct / 100.0) * (size - 1);
    const size_t lower = static_cast<size_t>(idx);
    const size_t upper = lower + 1;

    if (upper >= size) {
        quickSelect(data, size, lower);
        return data[lower];
    }

    /* Partition to place the lower-rank element. */
    quickSelect(data, size, lower);
    const float lowerVal = data[lower];

    /* The upper element is the minimum of everything above 'lower'. */
    float upperVal = data[upper];
    for (size_t i = upper + 1; i < size; ++i) {
        if (data[i] < upperVal) {
            upperVal = data[i];
        }
    }

    /* Linearly interpolate between the two bracketing values. */
    const double frac = idx - lower;
    return lowerVal + static_cast<float>(frac) * (upperVal - lowerVal);
}

// ============================================================================
// Min / Max
// ============================================================================

float Statistics::minimum(const float* data, size_t size)
{
    if (size == 0) return 0.0f;

    float minVal = data[0];
    for (size_t i = 1; i < size; ++i) {
        if (data[i] < minVal) {
            minVal = data[i];
        }
    }
    return minVal;
}

float Statistics::maximum(const float* data, size_t size)
{
    if (size == 0) return 0.0f;

    float maxVal = data[0];
    for (size_t i = 1; i < size; ++i) {
        if (data[i] > maxVal) {
            maxVal = data[i];
        }
    }
    return maxVal;
}

void Statistics::minMax(const float* data, size_t size,
                        float& outMin, float& outMax)
{
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

// ============================================================================
// Histogram-based median (approximate, fast for very large arrays)
// ============================================================================

float Statistics::histogramMedian(const float* data, size_t size, int numBins)
{
    if (size == 0) return 0.0f;

    /* Determine the data range. */
    float minVal, maxVal;
    minMax(data, size, minVal, maxVal);

    if (minVal == maxVal) {
        return minVal;
    }

    /* Build the histogram. */
    std::vector<int> histogram(numBins, 0);
    const float scale = static_cast<float>(numBins - 1) / (maxVal - minVal);

    for (size_t i = 0; i < size; ++i) {
        int bin = static_cast<int>((data[i] - minVal) * scale);
        bin = std::max(0, std::min(bin, numBins - 1));
        histogram[bin]++;
    }

    /* Walk the cumulative histogram to find the median bin. */
    const size_t target = size / 2;
    size_t count     = 0;
    int    medianBin = 0;

    for (int i = 0; i < numBins; ++i) {
        count += histogram[i];
        if (count >= target) {
            medianBin = i;
            break;
        }
    }

    /* Convert the bin index back to a value. */
    return minVal + static_cast<float>(medianBin) / scale;
}

// ============================================================================
// IKSS estimator (legacy Huber-reweighted iterative scheme)
// ============================================================================

void Statistics::ikssEstimator(const float* data, size_t size,
                               float med, float madVal,
                               double& outLocation, double& outScale)
{
    if (size == 0) {
        outLocation = 0.0;
        outScale    = 0.0;
        return;
    }

    /* Seed the iteration with the median and the MAD-based scale. */
    outLocation = med;
    outScale    = 1.4826 * madVal;  // Consistent scale estimator for Gaussian.

    constexpr int    maxIter   = 10;
    constexpr double tolerance = 1e-4;

    for (int iter = 0; iter < maxIter; ++iter) {
        double sumX = 0.0;
        double sumW = 0.0;

        /* Weighted mean using Huber weights (cut-off at 1.5 sigma). */
#ifdef _OPENMP
        #pragma omp parallel for reduction(+:sumX,sumW) if(size > 10000)
#endif
        for (int64_t i = 0; i < static_cast<int64_t>(size); ++i) {
            const double z = std::abs(data[i] - outLocation) / outScale;
            const double w = (z < 1.5) ? 1.0 : 1.5 / z;
            sumX += w * data[i];
            sumW += w;
        }
        if (sumW == 0.0) {
            break;
        }
        const double newLocation = sumX / sumW;

        /* Weighted dispersion. */
        double sumSq = 0.0;
#ifdef _OPENMP
        #pragma omp parallel for reduction(+:sumSq) if(size > 10000)
#endif
        for (int64_t i = 0; i < static_cast<int64_t>(size); ++i) {
            const double z    = std::abs(data[i] - newLocation) / outScale;
            const double w    = (z < 1.5) ? 1.0 : 1.5 / z;
            const double diff = data[i] - newLocation;
            sumSq += w * diff * diff;
        }

        double newScale = std::sqrt(sumSq / sumW);
        if (newScale < 1e-10) {
            newScale = 1e-10;
        }

        /* Check for convergence. */
        if (std::abs(newLocation - outLocation) < tolerance * outScale &&
            std::abs(newScale    - outScale)    < tolerance * outScale) {
            outLocation = newLocation;
            outScale    = newScale;
            break;
        }

        outLocation = newLocation;
        outScale    = newScale;
    }
}

// ============================================================================
// Linear fit (ordinary least squares)
// ============================================================================

void Statistics::linearFit(const float* x, const float* y, size_t size,
                           float& outSlope, float& outIntercept)
{
    if (size < 2) {
        outSlope     = 0.0f;
        outIntercept = (size == 1) ? y[0] : 0.0f;
        return;
    }

    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;

    for (size_t i = 0; i < size; ++i) {
        sumX  += x[i];
        sumY  += y[i];
        sumXY += x[i] * y[i];
        sumX2 += x[i] * x[i];
    }

    const double n     = static_cast<double>(size);
    const double denom = n * sumX2 - sumX * sumX;

    if (std::abs(denom) < 1e-10) {
        /* Degenerate case (vertical line or constant x). */
        outSlope     = 0.0f;
        outIntercept = static_cast<float>(sumY / n);
        return;
    }

    outSlope     = static_cast<float>((n * sumXY - sumX * sumY) / denom);
    outIntercept = static_cast<float>((sumY - outSlope * sumX) / n);
}

// ============================================================================
// Weighted mean
// ============================================================================

double Statistics::weightedMean(const float* data, const float* weights, size_t size)
{
    if (size == 0) return 0.0;

    double sumWX = 0.0;
    double sumW  = 0.0;

    for (size_t i = 0; i < size; ++i) {
        sumWX += weights[i] * data[i];
        sumW  += weights[i];
    }

    return (sumW > 0.0) ? sumWX / sumW : 0.0;
}

// ============================================================================
// QuickSelect / QuickSort
// ============================================================================

void Statistics::quickSelect(float* data, size_t size, size_t n)
{
    if (size <= 1 || n >= size) return;
    std::nth_element(data, data + n, data + size);
}

float Statistics::quickSelectImpl(float* data, size_t left, size_t right, size_t k)
{
    while (left < right) {
        const size_t mid = left + (right - left) / 2;

        /* Median-of-three pivot selection. */
        if (data[mid]   < data[left])  std::swap(data[left],  data[mid]);
        if (data[right] < data[left])  std::swap(data[left],  data[right]);
        if (data[right] < data[mid])   std::swap(data[mid],   data[right]);

        std::swap(data[mid], data[right - 1]);
        const size_t pivotIndex = partition(data, left, right - 1, right - 1);

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

size_t Statistics::partition(float* data, size_t left, size_t right, size_t pivotIndex)
{
    const float pivot = data[pivotIndex];
    std::swap(data[pivotIndex], data[right]);

    size_t storeIndex = left;
    for (size_t i = left; i < right; ++i) {
        if (data[i] < pivot) {
            std::swap(data[i], data[storeIndex]);
            ++storeIndex;
        }
    }

    std::swap(data[storeIndex], data[right]);
    return storeIndex;
}

void Statistics::quickSort(float* data, size_t size)
{
    if (size <= 1) return;
    quickSortImpl(data, 0, size - 1);
}

void Statistics::quickSortImpl(float* data, size_t left, size_t right)
{
    if (left >= right) return;

    /* Fall back to insertion sort for small sub-arrays. */
    if (right - left < 16) {
        for (size_t i = left + 1; i <= right; ++i) {
            const float key = data[i];
            size_t j = i;
            while (j > left && data[j - 1] > key) {
                data[j] = data[j - 1];
                --j;
            }
            data[j] = key;
        }
        return;
    }

    /* Median-of-three pivot selection. */
    const size_t mid = left + (right - left) / 2;
    if (data[mid]   < data[left])  std::swap(data[left],  data[mid]);
    if (data[right] < data[left])  std::swap(data[left],  data[right]);
    if (data[right] < data[mid])   std::swap(data[mid],   data[right]);

    std::swap(data[mid], data[right - 1]);
    const size_t pivotIndex = partition(data, left + 1, right - 1, right - 1);

    if (pivotIndex > left) {
        quickSortImpl(data, left, pivotIndex - 1);
    }
    if (pivotIndex < right) {
        quickSortImpl(data, pivotIndex + 1, right);
    }
}

// ============================================================================
// Biweight midvariance
// ============================================================================

double Statistics::biweightMidvariance(const float* data, size_t n,
                                       float mad, float median)
{
    if (mad <= 0.0f || n == 0) {
        return 0.0;
    }

    /*
     * u_i = (x_i - median) / (9 * MAD),  clamped so |u_i| < 1.
     *
     * BWMV = n * sum[ (x_i-m)^2 * (1-u^2)^4 ]
     *          / ( sum[ (1-u^2)(1 - 5*u^2) ] )^2
     */
    const float factor = 1.0f / (9.0f * mad);

    double numerator   = 0.0;
    double denominator = 0.0;

    for (size_t i = 0; i < n; ++i) {
        const float diff = data[i] - median;
        const float yi   = diff * factor;
        const float yi2  = (std::abs(yi) < 1.0f) ? yi * yi : 1.0f;
        const double t   = 1.0 - yi2;

        numerator   += static_cast<double>(diff * t) * static_cast<double>(diff * t) * t * t;
        denominator += t * (1.0 - 5.0 * yi2);
    }

    return (denominator != 0.0)
        ? static_cast<double>(n) * numerator / (denominator * denominator)
        : 0.0;
}

// ============================================================================
// IKSS-lite (one-shot robust estimator)
// ============================================================================

bool Statistics::ikssLite(const float* data, size_t size,
                          float med, float madVal,
                          double& outLocation, double& outScale)
{
    if (size == 0 || madVal <= 0.0f) {
        outLocation = med;
        outScale    = 0.0;
        return false;
    }

    /* Step 1: remove pixels outside +/- 6 MAD from the median. */
    const float xlow  = med - 6.0f * madVal;
    const float xhigh = med + 6.0f * madVal;

    std::vector<float> filtered;
    filtered.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] >= xlow && data[i] <= xhigh) {
            filtered.push_back(data[i]);
        }
    }

    if (filtered.empty()) {
        outLocation = med;
        outScale    = 0.0;
        return false;
    }

    /* Step 2: re-compute the median of the filtered data (new location). */
    const float newMed = quickMedian(filtered);
    outLocation = newMed;

    /* Step 3: re-compute the MAD of the filtered data. */
    const float newMad = static_cast<float>(
        Statistics::mad(filtered.data(), filtered.size(), newMed));

    if (newMad == 0.0f) {
        outScale = 0.0;
        return false;
    }

    /* Step 4: scale = sqrt(biweight midvariance) * 0.991 */
    const double bwmv = biweightMidvariance(filtered.data(), filtered.size(),
                                            newMad, newMed);
    outScale = std::sqrt(bwmv) * 0.991;
    return true;
}

} // namespace Stacking