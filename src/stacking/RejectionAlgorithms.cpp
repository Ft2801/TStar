/**
 * @file RejectionAlgorithms.cpp
 * @brief Implementation of pixel rejection algorithms for image stacking.
 *
 * Each algorithm operates on a flat vector of pixel values collected from
 * multiple frames at the same spatial position. The goal is to identify and
 * flag statistical outliers (cosmic rays, hot pixels, satellite trails, etc.)
 * before the final combination step.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "RejectionAlgorithms.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Stacking {

// ============================================================================
// Main Dispatcher
// ============================================================================

RejectionResult RejectionAlgorithms::apply(
    std::vector<float>& stack,
    Rejection type,
    float sigmaLow,
    float sigmaHigh,
    std::vector<int>& rejected,
    [[maybe_unused]] const std::vector<float>* weights,
    [[maybe_unused]] const std::vector<float>* drizzleWeights,
    std::vector<float>* scratch)
{
    const int n = static_cast<int>(stack.size());

    // Prepare the rejection flag array (all pixels initially accepted).
    rejected.assign(n, 0);

    // A minimum of 3 samples is required for any meaningful rejection.
    // With fewer samples, return all pixels as kept.
    if (n < 3) {
        RejectionResult result;
        result.keptCount = n;
        return result;
    }

    // Dispatch to the appropriate algorithm.
    switch (type) {
    case Rejection::None: {
        RejectionResult result;
        result.keptCount = n;
        return result;
    }
    case Rejection::Percentile:
        return percentileClipping(stack, sigmaLow, sigmaHigh, rejected, scratch);

    case Rejection::Sigma:
        return sigmaClipping(stack, sigmaLow, sigmaHigh, rejected, scratch, weights);

    case Rejection::MAD:
        return madClipping(stack, sigmaLow, sigmaHigh, rejected, scratch);

    case Rejection::SigmaMedian:
        return sigmaMedianClipping(stack, sigmaLow, sigmaHigh, rejected, scratch);

    case Rejection::Winsorized:
        return winsorizedClipping(stack, sigmaLow, sigmaHigh, rejected, scratch, weights);

    case Rejection::LinearFit:
        return linearFitClipping(stack, sigmaLow, sigmaHigh, rejected, scratch);

    case Rejection::GESDT:
        return gesdtClipping(stack, sigmaLow, sigmaHigh, rejected, scratch);

    case Rejection::Biweight:
        return biweightClipping(
            stack, (sigmaHigh > 0) ? sigmaHigh : 6.0f, rejected, scratch);

    case Rejection::ModifiedZScore:
        return modifiedZScoreClipping(
            stack, (sigmaHigh > 0) ? sigmaHigh : 3.5f, rejected, scratch);

    default: {
        RejectionResult result;
        result.keptCount = n;
        return result;
    }
    }
}

// ============================================================================
// Percentile Clipping
// ============================================================================

RejectionResult RejectionAlgorithms::percentileClipping(
    const std::vector<float>& stack,
    float pLow, float pHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    // Obtain the median from a working copy of the stack.
    std::vector<float> localBuf;
    std::vector<float>& work = scratch ? *scratch : localBuf;
    work = stack;

    const float median = Statistics::quickMedian(work);

    // Guard against a zero median to avoid division-by-zero artefacts.
    if (median == 0.0f) {
        result.keptCount = n;
        return result;
    }

    // Reject pixels whose fractional deviation from the median exceeds the
    // specified low/high thresholds.
    for (int i = 0; i < n; ++i) {
        const float pixel = stack[i];

        if ((median - pixel) > median * pLow) {
            // Low (dark) outlier.
            rejected[i] = -1;
            result.lowRejected++;
        } else if ((pixel - median) > median * pHigh) {
            // High (bright) outlier.
            rejected[i] = 1;
            result.highRejected++;
        }
    }

    result.keptCount = n - result.lowRejected - result.highRejected;
    return result;
}

// ============================================================================
// Sigma Clipping
// ============================================================================

RejectionResult RejectionAlgorithms::sigmaClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch,
    const std::vector<float>* weights)
{
    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    // Local buffer management: reuse the caller-supplied scratch buffer when
    // available to avoid repeated heap allocations.
    std::vector<float> localBuf;
    std::vector<float>& work = scratch ? *scratch : localBuf;
    if (!scratch) localBuf.reserve(n);

    int  currentN = n;
    bool changed  = true;

    // Iterate until no additional rejections occur or the minimum sample
    // size (3) is reached.
    while (changed && currentN > 3) {
        changed = false;

        float  median = 0.0f;
        double sigma  = 0.0;

        if (weights) {
            // Weighted path: use weighted median and weighted standard deviation.
            median = weightedMedian(stack, *weights, &rejected);
            auto stats = weightedMeanAndStdDev(stack, *weights, &rejected);
            sigma = stats.second;
        } else {
            // Unweighted path: gather valid (non-rejected) pixels.
            work.clear();
            for (int i = 0; i < n; ++i) {
                if (rejected[i] == 0)
                    work.push_back(stack[i]);
            }
            median = Statistics::quickMedian(work.data(), work.size());
            sigma  = Statistics::stdDev(work.data(), work.size(), nullptr);
        }

        // Test each non-rejected pixel against the current sigma bounds.
        for (int i = 0; i < n; ++i) {
            if (rejected[i] != 0) continue;
            if (currentN <= 3) break;

            const float pixel = stack[i];

            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++;
                changed = true;
                currentN--;
            } else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                changed = true;
                currentN--;
            }
        }
    }

    result.keptCount = currentN;
    return result;
}

// ============================================================================
// MAD Clipping
// ============================================================================

RejectionResult RejectionAlgorithms::madClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    Q_UNUSED(scratch);

    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    // Prepare a working buffer for computing MAD statistics. The MAD
    // computation modifies the buffer (partial sort), so a copy is required.
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;

    bool changed  = true;
    int  currentN = n;

    while (changed && currentN > 3) {
        changed = false;

        // Collect valid (non-rejected) pixels.
        work.clear();
        for (int i = 0; i < n; ++i) {
            if (rejected[i] == 0)
                work.push_back(stack[i]);
        }

        // Compute robust location and scale.
        const float  median = Statistics::quickMedian(work.data(), work.size());
        const double mad    = Statistics::mad(work.data(), work.size(), median);

        // Use the raw MAD as the scale estimate (no 1.4826 correction).
        const double sigma = mad;

        // Test each non-rejected pixel.
        for (int i = 0; i < n; ++i) {
            if (rejected[i] != 0) continue;
            if (currentN <= 3) break;

            const float pixel = stack[i];

            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++;
                changed = true;
                currentN--;
            } else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                changed = true;
                currentN--;
            }
        }
    }

    result.keptCount = currentN;
    return result;
}

// ============================================================================
// Sigma-Median Clipping
// ============================================================================

RejectionResult RejectionAlgorithms::sigmaMedianClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;

    // Iterate: detect outliers and replace them with the median until the
    // data stabilises (no more replacements in a full pass).
    int replaced;
    do {
        replaced = 0;

        // Snapshot the current stack for computing statistics.
        work = stack;

        const double sigma  = Statistics::stdDev(work.data(), n, nullptr);
        const float  median = Statistics::quickMedian(work.data(), n);

        for (int i = 0; i < n; ++i) {
            const float pixel = stack[i];
            bool isOutlier = false;

            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++;
                isOutlier = true;
            } else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                isOutlier = true;
            }

            if (isOutlier) {
                // Replace the outlier with the median rather than removing it.
                stack[i] = median;
                replaced++;
            }
        }
    } while (replaced > 0);

    // All positions are preserved; the count equals the original size.
    result.keptCount = n;
    return result;
}

// ============================================================================
// Winsorized Sigma Clipping
// ============================================================================

RejectionResult RejectionAlgorithms::winsorizedClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch,
    const std::vector<float>* weights)
{
    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    // Working copy used for the winsorization (clipping extremes).
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& wStack = scratch ? *scratch : localBuf;
    wStack = stack;

    bool changed  = true;
    int  currentN = n;

    while (changed && currentN > 3) {
        changed = false;

        float  median = 0.0f;
        double sigma  = 0.0;

        if (weights) {
            // Weighted statistics path.
            median = weightedMedian(stack, *weights, &rejected);
            auto stats = weightedMeanAndStdDev(stack, *weights, &rejected);
            sigma = stats.second;
        } else {
            // Unweighted statistics from valid pixels only.
            std::vector<float> validPixels;
            validPixels.reserve(n);
            for (int i = 0; i < n; ++i) {
                if (rejected[i] == 0)
                    validPixels.push_back(stack[i]);
            }
            if (validPixels.size() < 3) break;

            sigma  = Statistics::stdDev(validPixels.data(), validPixels.size(), nullptr);
            median = Statistics::quickMedian(validPixels.data(), validPixels.size());
        }

        // Reset winsorized buffer to original values.
        wStack = stack;

        // Iterative winsorization: clamp values to [median - 1.5*sigma,
        // median + 1.5*sigma] and recompute sigma from the clamped data.
        // Repeat until sigma converges (relative change < 0.05%).
        double sigma0;
        int    wIters = 0;
        do {
            const float m0 = median - 1.5f * static_cast<float>(sigma);
            const float m1 = median + 1.5f * static_cast<float>(sigma);

            // Winsorize all values.
            for (int i = 0; i < n; ++i)
                wStack[i] = std::min(m1, std::max(m0, wStack[i]));

            sigma0 = sigma;

            if (weights) {
                auto wStats = weightedMeanAndStdDev(wStack, *weights, &rejected);
                sigma = 1.134 * wStats.second;
            } else {
                std::vector<float> wValid;
                wValid.reserve(n);
                for (int i = 0; i < n; ++i) {
                    if (rejected[i] == 0)
                        wValid.push_back(wStack[i]);
                }
                sigma = 1.134 * Statistics::stdDev(wValid.data(), wValid.size(), nullptr);
            }
            wIters++;
        } while (std::abs(sigma - sigma0) > sigma0 * 0.0005 && wIters < 10);

        // Apply rejection to original data using the converged sigma.
        for (int i = 0; i < n; ++i) {
            if (rejected[i] != 0) continue;
            if (currentN <= 3) break;

            const float pixel = stack[i];

            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++;
                changed = true;
                currentN--;
            } else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                changed = true;
                currentN--;
            }
        }
    }

    result.keptCount = currentN;
    return result;
}

// ============================================================================
// Linear Fit Clipping
// ============================================================================

RejectionResult RejectionAlgorithms::linearFitClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    Q_UNUSED(scratch);

    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    bool changed  = true;
    int  currentN = n;

    while (changed && currentN > 3) {
        changed = false;

        // Gather valid (non-rejected) pixels paired with their original index
        // and sort by value.  The rank j serves as the x-coordinate for the
        // least-squares fit.
        std::vector<std::pair<float, int>> validPairs;
        validPairs.reserve(currentN);
        for (int i = 0; i < n; ++i) {
            if (rejected[i] == 0)
                validPairs.push_back({stack[i], i});
        }
        std::sort(validPairs.begin(), validPairs.end());

        const int N = static_cast<int>(validPairs.size());

        // ---- OLS regression: y = a*x + b  (x = rank index) ----
        const float m_x = (N - 1) * 0.5f;
        double m_y = 0.0;
        for (int j = 0; j < N; ++j)
            m_y += validPairs[j].first;
        m_y /= N;

        double ssxy = 0.0, ssxx = 0.0;
        for (int j = 0; j < N; ++j) {
            const double dx = j - m_x;
            ssxy += dx * (validPairs[j].first - m_y);
            ssxx += dx * dx;
        }

        const float a = static_cast<float>((ssxx > 0.0) ? ssxy / ssxx : 0.0);
        const float b = static_cast<float>(m_y - a * m_x);

        // ---- Scale estimate: Mean Absolute Error (MAE) ----
        double sigma = 0.0;
        for (int j = 0; j < N; ++j)
            sigma += std::abs(validPairs[j].first - (a * j + b));
        sigma /= N;

        // ---- Rejection pass ----
        for (int j = 0; j < N; ++j) {
            if (currentN <= 3) break;

            const int   origIdx   = validPairs[j].second;
            const float deviation = validPairs[j].first - (a * j + b);

            if (deviation < -sigmaLow * sigma) {
                rejected[origIdx] = -1;
                result.lowRejected++;
                changed = true;
                currentN--;
            } else if (deviation > sigmaHigh * sigma) {
                rejected[origIdx] = 1;
                result.highRejected++;
                changed = true;
                currentN--;
            }
        }
    }

    result.keptCount = currentN;
    return result;
}

// ============================================================================
// GESDT (Generalized Extreme Studentized Deviate Test)
// ============================================================================

RejectionResult RejectionAlgorithms::gesdtClipping(
    std::vector<float>& stack,
    float outliersFraction, float significance,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    // Working copy for destructive Grubbs iterations.
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& wStack = scratch ? *scratch : localBuf;
    wStack = stack;

    // Compute the median for later low/high classification of outliers.
    Statistics::quickSort(wStack.data(), n);
    const float median = wStack[n / 2];

    // Determine the maximum number of outliers to test.
    int maxOutliers = static_cast<int>(n * outliersFraction);
    if (maxOutliers < 1) maxOutliers = 1;

    // Pre-compute critical values for each iteration.
    std::vector<float> criticalValues =
        computeGesdtCriticalValues(n, maxOutliers, significance);

    // Reset working copy to unsorted original data.
    wStack = stack;

    // Track outlier candidates (value, original index).
    std::vector<std::pair<float, int>> outliers;
    outliers.reserve(maxOutliers);

    // Index mapping for swap-remove optimisation (O(1) removal per step).
    std::vector<int> indices(n);
    for (int i = 0; i < n; ++i)
        indices[i] = i;

    int currentSize = n;

    for (int iter = 0; iter < maxOutliers && currentSize > 3; ++iter) {
        float gStat;
        int   maxIndexLocal;

        // Compute the Grubbs statistic on the current (shrinking) dataset.
        grubbsStatistic(wStack, currentSize, gStat, maxIndexLocal);

        // If the statistic does not exceed the critical value, no further
        // outliers can be confirmed and we stop.
        if (!checkGValue(gStat, criticalValues[iter]))
            break;

        // Record the confirmed outlier.
        outliers.push_back({wStack[maxIndexLocal], indices[maxIndexLocal]});

        // Swap-remove: swap the outlier with the last element and shrink.
        if (maxIndexLocal != currentSize - 1) {
            std::swap(wStack[maxIndexLocal],  wStack[currentSize - 1]);
            std::swap(indices[maxIndexLocal], indices[currentSize - 1]);
        }
        currentSize--;
    }

    // Classify confirmed outliers as low or high relative to the median.
    confirmGesdtOutliers(
        outliers, static_cast<int>(outliers.size()), median, rejected, result);

    result.keptCount = n - result.totalRejected();
    return result;
}

// ============================================================================
// Biweight Estimator
// ============================================================================

RejectionResult RejectionAlgorithms::biweightClipping(
    std::vector<float>& stack,
    float tuningConstant,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    // Biweight estimation needs at least 4 samples for meaningful results.
    if (n < 4) {
        result.keptCount = n;
        return result;
    }

    // Step 1: Obtain initial robust location (median) and scale (MAD).
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;
    work = stack;

    float median = Statistics::quickMedian(work.data(), n);

    // Compute the MAD (Median Absolute Deviation).
    for (int i = 0; i < n; ++i)
        work[i] = std::abs(stack[i] - median);
    float mad = Statistics::quickMedian(work.data(), n);

    if (mad < 1e-9f) {
        // All pixel values are effectively identical; nothing to reject.
        result.keptCount = n;
        return result;
    }

    // Step 2: Iteratively refine the biweight center.
    //
    // The biweight location is:
    //   T_bi = M + sum[(x_i - M)(1 - u_i^2)^2] / sum[(1 - u_i^2)^2]
    // where u_i = (x_i - M) / (c * MAD).  Pixels with |u_i| >= 1 receive
    // zero weight.
    double       center    = median;
    double       scale     = mad;
    const int    MAX_ITERS = 5;
    const double C         = (tuningConstant > 0) ? tuningConstant : 6.0;

    for (int iter = 0; iter < MAX_ITERS; ++iter) {
        double num = 0.0;
        double den = 0.0;
        bool   converged = true;

        for (int i = 0; i < n; ++i) {
            const double u = (stack[i] - center) / (C * scale);
            if (std::abs(u) < 1.0) {
                double w = (1.0 - u * u);
                w = w * w;  // (1 - u^2)^2
                num += (stack[i] - center) * w;
                den += w;
            }
        }

        if (den > 1e-9) {
            const double shift = num / den;
            center += shift;
            if (std::abs(shift) > 1e-5 * scale)
                converged = false;
        }

        // Recompute the scale (MAD) for the new location to ensure proper convergence
        for (int i = 0; i < n; ++i)
            work[i] = static_cast<float>(std::abs(stack[i] - center));
        scale = static_cast<double>(Statistics::quickMedian(work.data(), n));
        if (scale < 1e-9) scale = 1e-9; // Protect against zero scale

        if (converged) break;
    }

    // Step 3: Reject pixels that receive zero biweight weight (|u| >= 1).
    const double limit = C * scale;

    for (int i = 0; i < n; ++i) {
        const double dist = stack[i] - center;
        if (std::abs(dist) >= limit) {
            rejected[i] = (dist < 0) ? -1 : 1;
            if (dist < 0) result.lowRejected++;
            else          result.highRejected++;
        }
    }

    result.keptCount = n - result.totalRejected();
    return result;
}

// ============================================================================
// Modified Z-Score
// ============================================================================

RejectionResult RejectionAlgorithms::modifiedZScoreClipping(
    std::vector<float>& stack,
    float threshold,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    const int n = static_cast<int>(stack.size());

    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    // Compute median.
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;
    work = stack;

    const float median = Statistics::quickMedian(work.data(), n);

    // Compute MAD.
    for (int i = 0; i < n; ++i)
        work[i] = std::abs(stack[i] - median);
    const float mad = Statistics::quickMedian(work.data(), n);

    if (mad < 1e-9f) {
        // All values are identical; nothing to reject.
        result.keptCount = n;
        return result;
    }

    // The modified Z-score is  M_i = 0.6745 * (x_i - median) / MAD.
    // Checking |M_i| > threshold is equivalent to checking
    //   |x_i - median| > threshold * (MAD / 0.6745).
    // This is mathematically the same as sigma clipping with a robust scale.
    const float limit = threshold * (mad / 0.6745f);

    for (int i = 0; i < n; ++i) {
        const float dev = stack[i] - median;
        if (std::abs(dev) > limit) {
            rejected[i] = (dev < 0) ? -1 : 1;
            if (dev < 0) result.lowRejected++;
            else         result.highRejected++;
        }
    }

    result.keptCount = n - result.totalRejected();
    return result;
}

// ============================================================================
// GESDT Critical Value Computation
// ============================================================================

std::vector<float> RejectionAlgorithms::computeGesdtCriticalValues(
    int n, int maxOutliers, float significance)
{
    std::vector<float> values(maxOutliers);

    for (int i = 0; i < maxOutliers; ++i) {
        const int ni = n - i;

        // Adjusted significance level (Bonferroni-like correction).
        const double alpha = significance / (2.0 * ni);

        double p = 1.0 - alpha;
        if (p > 0.5) p = 1.0 - p;

        // Rational approximation of the inverse normal CDF (Abramowitz & Stegun).
        double t = std::sqrt(-2.0 * std::log(p));
        const double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
        const double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
        t = t - (c0 + c1 * t + c2 * t * t) /
                (1.0 + d1 * t + d2 * t * t + d3 * t * t * t);

        if (alpha < 0.5 && significance < 0.5) t = -t;
        t = std::abs(t);

        // Critical value for the Grubbs statistic.
        const double numerator = (ni - 1) * t;
        const double denom     = std::sqrt((ni - 2 + t * t) * ni);

        values[i] = static_cast<float>(numerator / denom);
    }

    return values;
}

// ============================================================================
// Helper Functions
// ============================================================================

int RejectionAlgorithms::removeNullPixels(
    std::vector<float>& stack,
    const std::vector<float>* drizzleWeights)
{
    int       kept = 0;
    const int n    = static_cast<int>(stack.size());

    if (drizzleWeights) {
        for (int i = 0; i < n; ++i) {
            if (stack[i] != 0.0f && (*drizzleWeights)[i] != 0.0f) {
                if (i != kept) stack[kept] = stack[i];
                kept++;
            }
        }
    } else {
        for (int i = 0; i < n; ++i) {
            if (stack[i] != 0.0f) {
                if (i != kept) stack[kept] = stack[i];
                kept++;
            }
        }
    }

    stack.resize(kept);
    return kept;
}

int RejectionAlgorithms::compactStack(
    std::vector<float>& stack,
    const std::vector<int>& rejected)
{
    int       kept = 0;
    const int n    = static_cast<int>(stack.size());

    for (int i = 0; i < n; ++i) {
        if (rejected[i] == 0) {
            if (i != kept) stack[kept] = stack[i];
            kept++;
        }
    }

    stack.resize(kept);
    return kept;
}

void RejectionAlgorithms::grubbsStatistic(
    const std::vector<float>& stack, int n,
    float& gStat, int& maxIndex)
{
    double mean, sd;
    Statistics::meanAndStdDev(stack.data(), n, mean, sd);

    // Find the sample with the maximum absolute deviation from the mean.
    float maxDev = -1.0f;
    maxIndex     = -1;

    for (int i = 0; i < n; ++i) {
        const float dev = std::abs(stack[i] - static_cast<float>(mean));
        if (dev > maxDev) {
            maxDev   = dev;
            maxIndex = i;
        }
    }

    gStat = (sd > 1e-9) ? maxDev / static_cast<float>(sd) : 0.0f;
}

bool RejectionAlgorithms::checkGValue(float gStat, float criticalValue)
{
    return gStat > criticalValue;
}

void RejectionAlgorithms::confirmGesdtOutliers(
    const std::vector<std::pair<float, int>>& outliers,
    int numOutliers, float median,
    std::vector<int>& rejected,
    RejectionResult& result)
{
    for (int i = 0; i < numOutliers; ++i) {
        const float value = outliers[i].first;
        const int   idx   = outliers[i].second;

        if (idx < 0 || idx >= static_cast<int>(rejected.size()))
            continue;

        if (value < median) {
            rejected[idx] = -1;
            result.lowRejected++;
        } else {
            rejected[idx] = 1;
            result.highRejected++;
        }
    }
}

// ============================================================================
// Weighted Statistics
// ============================================================================

float RejectionAlgorithms::weightedMedian(
    const std::vector<float>& data,
    const std::vector<float>& weights,
    const std::vector<int>* validMask)
{
    const int n = static_cast<int>(data.size());
    if (n == 0 || n != static_cast<int>(weights.size()))
        return 0.0f;

    // Build (value, weight) pairs for valid samples.
    struct ValWeight { float v; float w; };
    std::vector<ValWeight> pairs;
    pairs.reserve(n);

    double totalWeight = 0.0;

    for (int i = 0; i < n; ++i) {
        if (!validMask || (*validMask)[i] == 0) {
            const float w = weights[i];
            if (w > 0.0f) {
                pairs.push_back({data[i], w});
                totalWeight += w;
            }
        }
    }

    if (pairs.empty()) return 0.0f;

    // Sort by value.
    std::sort(pairs.begin(), pairs.end(),
              [](const ValWeight& a, const ValWeight& b) { return a.v < b.v; });

    // Walk sorted pairs until the cumulative weight reaches 50%.
    const double halfWeight    = totalWeight * 0.5;
    double       currentWeight = 0.0;

    for (const auto& p : pairs) {
        currentWeight += p.w;
        if (currentWeight >= halfWeight)
            return p.v;
    }

    return pairs.back().v;
}

std::pair<double, double> RejectionAlgorithms::weightedMeanAndStdDev(
    const std::vector<float>& data,
    const std::vector<float>& weights,
    const std::vector<int>* validMask)
{
    const int n = static_cast<int>(data.size());
    if (n == 0 || n != static_cast<int>(weights.size()))
        return {0.0, 0.0};

    // Pass 1: Weighted mean.
    double sumW    = 0.0;
    double sumValW = 0.0;

    for (int i = 0; i < n; ++i) {
        if (!validMask || (*validMask)[i] == 0) {
            const float w = weights[i];
            if (w > 0.0f) {
                sumW    += w;
                sumValW += static_cast<double>(data[i]) * w;
            }
        }
    }

    if (sumW == 0.0) return {0.0, 0.0};
    const double mean = sumValW / sumW;

    // Pass 2: Weighted variance with reliability-weight correction.
    double sumSqDiffW = 0.0;
    double sumSqW     = 0.0;
    int    count      = 0;

    for (int i = 0; i < n; ++i) {
        if (!validMask || (*validMask)[i] == 0) {
            const float w = weights[i];
            if (w > 0.0f) {
                const double diff = data[i] - mean;
                sumSqDiffW += diff * diff * w;
                sumSqW     += w * w;
                count++;
            }
        }
    }

    if (count <= 1) return {mean, 0.0};

    // Denominator: V1 - V2/V1 (reliability-weight correction).
    double denom = sumW - (sumSqW / sumW);
    if (denom <= 0.0) denom = sumW;

    const double variance = sumSqDiffW / denom;

    return {mean, std::sqrt(variance)};
}

} // namespace Stacking