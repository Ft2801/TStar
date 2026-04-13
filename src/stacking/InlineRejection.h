#ifndef INLINE_REJECTION_H
#define INLINE_REJECTION_H

/**
 * @file InlineRejection.h
 * @brief Header-only pixel-rejection algorithms for image stacking.
 *
 * All functions are declared inline so that the hot inner loops of the
 * stacking engine can be inlined by the compiler without a function-call
 * overhead per pixel.
 *
 * Supported algorithms:
 *   - Percentile clipping
 *   - Iterative sigma clipping (standard deviation)
 *   - Iterative MAD clipping (median absolute deviation)
 *   - Sigma-median replacement
 *   - Winsorized sigma clipping
 *   - Linear-fit clipping
 *   - Generalized ESD Test (GESDT / Grubbs)
 *   - Tukey biweight estimator
 *   - Modified Z-score
 *
 * Each algorithm operates on a StackDataBlock that holds pre-allocated
 * per-pixel scratch buffers.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include <cmath>
#include <cstring>
#include <algorithm>
#include "StackDataBlock.h"
#include "StackingTypes.h"

namespace Stacking {
namespace InlineRejection {

/** Maximum number of outlier iterations for the GESDT algorithm. */
constexpr int MAX_GESDT_OUTLIERS = 25;

// ============================================================================
// Utility: Inverse t-distribution approximation
// ============================================================================

/**
 * @brief Rough inverse of the Student-t CDF.
 *
 * Used internally by computeGESDTCriticalValue().  Adequate for the
 * significance levels typically encountered in pixel rejection.
 *
 * @param p  Probability level.
 * @param df Degrees of freedom (must be > 0).
 * @return Approximate t quantile.
 */
inline double t_inv(double p, double df)
{
    if (df <= 0) return 3.0;
    double x = std::sqrt(df * (std::pow(p, -2.0 / df) - 1.0));
    return x;
}

/**
 * @brief Compute the GESDT critical value for sample size n.
 *
 * @param n     Sample size.
 * @param alpha Significance level.
 * @return Grubbs critical value.
 */
inline float computeGESDTCriticalValue(int n, float alpha)
{
    if (n <= 2) return 3.0f;

    double p    = 1.0 - static_cast<double>(alpha) / (2.0 * n);
    double df   = static_cast<double>(n) - 2.0;
    double t    = t_inv(p, df);
    double crit = (t * (n - 1)) / std::sqrt((df + t * t) * n);
    return static_cast<float>(crit);
}

// ============================================================================
// Fast statistics helpers
// ============================================================================

/**
 * @brief O(n) average-case median using nth_element.
 *
 * Partially reorders the input array.  For even-length arrays the
 * average of the two middle values is returned.
 *
 * @param data Array of values (modified in place).
 * @param n    Number of elements.
 * @return Median value.
 */
inline float quickMedianFloat(float* data, int n)
{
    if (n <= 0) return 0.0f;
    if (n == 1) return data[0];
    if (n == 2) return (data[0] + data[1]) * 0.5f;

    int mid = n / 2;
    std::nth_element(data, data + mid, data + n);

    if (n % 2 == 0) {
        float maxLower = *std::max_element(data, data + mid);
        return (maxLower + data[mid]) * 0.5f;
    }
    return data[mid];
}

/**
 * @brief Sample standard deviation and (optionally) mean.
 *
 * @param data    Input array (unmodified).
 * @param n       Number of elements.
 * @param outMean If non-null, receives the arithmetic mean.
 * @return Bessel-corrected standard deviation.
 */
inline float statsFloatSd(const float* data, int n, float* outMean = nullptr)
{
    if (n <= 1) {
        if (outMean) *outMean = (n == 1) ? data[0] : 0.0f;
        return 0.0f;
    }

    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += data[i];
    float mean = static_cast<float>(sum / n);

    double sumSq = 0.0;
    for (int i = 0; i < n; ++i) {
        float diff = data[i] - mean;
        sumSq += diff * diff;
    }

    if (outMean) *outMean = mean;
    return std::sqrt(static_cast<float>(sumSq / (n - 1)));
}

/**
 * @brief Median absolute deviation (MAD), unscaled.
 *
 * @param data    Input values (unmodified).
 * @param n       Number of elements.
 * @param median  Pre-computed median of data[].
 * @param scratch Scratch buffer of at least n floats.
 * @return Raw MAD (not multiplied by 1.4826).
 */
inline float statsFloatMad(float* data, int n, float median, float* scratch)
{
    if (n <= 0) return 0.0f;

    for (int i = 0; i < n; ++i)
        scratch[i] = std::abs(data[i] - median);

    return quickMedianFloat(scratch, n);
}

// ============================================================================
// Individual clipping predicates
// ============================================================================

/**
 * @brief Percentile clipping: reject if pixel deviates from the median
 *        by more than a fraction of the median itself.
 *
 * @return -1 (low reject), 0 (keep), or +1 (high reject).
 */
inline int percentileClipping(float pixel, float pLow, float pHigh,
                               float median, int rej[2])
{
    if (median - pixel > median * pLow) {
        rej[0]++;
        return -1;
    } else if (pixel - median > median * pHigh) {
        rej[1]++;
        return 1;
    }
    return 0;
}

/**
 * @brief Standard sigma clipping against the median.
 *
 * @return -1 (low reject), 0 (keep), or +1 (high reject).
 */
inline int sigmaClipping(float pixel, float sigma, float sigmaLow,
                          float sigmaHigh, float median, int rej[2])
{
    if (median - pixel > sigma * sigmaLow) {
        rej[0]++;
        return -1;
    } else if (pixel - median > sigma * sigmaHigh) {
        rej[1]++;
        return 1;
    }
    return 0;
}

/**
 * @brief Linear-fit clipping: reject if pixel deviates from a * i + b
 *        by more than sigma * threshold.
 *
 * @return -1 (low reject), 0 (keep), or +1 (high reject).
 */
inline int lineClipping(float pixel, float sigmaLow, float sigmaHigh,
                         float sigma, int i, float a, float b, int rej[2])
{
    if (a * i + b - pixel > sigma * sigmaLow) {
        rej[0]++;
        return -1;
    } else if (pixel - a * i - b > sigma * sigmaHigh) {
        rej[1]++;
        return 1;
    }
    return 0;
}

// ============================================================================
// Main single-channel rejection dispatcher
// ============================================================================

/**
 * @brief Apply the selected rejection algorithm to a single pixel stack.
 *
 * Operates on the scratch arrays inside the StackDataBlock.  Invalid
 * (zero) pixels are removed before rejection begins.  The surviving
 * pixels are compacted to the front of the stack array.
 *
 * @param data            Per-pixel scratch block.
 * @param nbFrames        Original number of frames.
 * @param rejectionType   Algorithm selector.
 * @param sigLow          Low  rejection parameter (meaning varies by algorithm).
 * @param sigHigh         High rejection parameter.
 * @param criticalValues  Pre-computed GESDT critical values (may be null).
 * @param crej            Two-element counter: [0] = low rejects, [1] = high.
 * @return Number of surviving pixels.
 */
inline int applyRejection(
    StackDataBlock& data,
    int nbFrames,
    Rejection rejectionType,
    float sigLow,
    float sigHigh,
    const float* criticalValues,
    int crej[2])
{
    int N = nbFrames;
    float median = 0.0f;
    int pixel, output, changed;
    int firstloop = 1;
    int kept = 0, removed = 0;

    float* stack    = data.stack;
    float* w_stack  = data.w_stack;
    int*   rejected = data.rejected;
    float* o_stack  = data.o_stack;

    // Preserve the original (unsorted) order for weighted-mean computation
    std::memcpy(o_stack, stack, N * sizeof(float));

    // Remove zero (invalid) pixels from the working stack
    for (int frame = 0; frame < N; ++frame) {
        if (stack[frame] != 0.0f) {
            if (frame != kept) stack[kept] = stack[frame];
            kept++;
        }
    }

    if (kept <= 1) return kept;
    removed = N - kept;
    N = kept;

    if (rejectionType == Rejection::None) return N;

    // Pre-compute median for algorithms that require it
    switch (rejectionType) {
        case Rejection::Percentile:
        case Rejection::Sigma:
        case Rejection::MAD:
            median = quickMedianFloat(stack, N);
            if (median == 0.0f) return 0;
            break;
        default:
            break;
    }

    // ----- Dispatch to the appropriate rejection algorithm -----

    switch (rejectionType) {

    // ----------------------------------------------------------------
    case Rejection::Percentile:
    {
        for (int frame = 0; frame < N; ++frame)
            rejected[frame] = percentileClipping(stack[frame], sigLow, sigHigh,
                                                  median, crej);
        // Compact surviving pixels
        for (pixel = 0, output = 0; pixel < N; ++pixel) {
            if (!rejected[pixel]) {
                if (pixel != output) stack[output] = stack[pixel];
                output++;
            }
        }
        N = output;
        break;
    }

    // ----------------------------------------------------------------
    case Rejection::Sigma:
    case Rejection::MAD:
    {
        do {
            float var;
            if (rejectionType == Rejection::Sigma) {
                var = statsFloatSd(stack, N, nullptr);
            } else {
                float* scratch = w_stack ? w_stack : o_stack;
                var = statsFloatMad(stack, N, median, scratch);
            }

            if (!firstloop)
                median = quickMedianFloat(stack, N);
            else
                firstloop = 0;

            int r = 0;
            for (int frame = 0; frame < N; ++frame) {
                if (N - r <= 4) {
                    rejected[frame] = 0;
                } else {
                    rejected[frame] = sigmaClipping(stack[frame], var, sigLow,
                                                     sigHigh, median, crej);
                    if (rejected[frame]) r++;
                }
            }

            for (pixel = 0, output = 0; pixel < N; ++pixel) {
                if (!rejected[pixel]) {
                    if (pixel != output) stack[output] = stack[pixel];
                    output++;
                }
            }
            changed = (N != output);
            N = output;

        } while (changed && N > 3);
        break;
    }

    // ----------------------------------------------------------------
    case Rejection::SigmaMedian:
    {
        // Replaces outliers with the median instead of removing them
        do {
            float sigma = statsFloatSd(stack, N, nullptr);
            float med   = quickMedianFloat(stack, N);
            int n = 0;
            for (int frame = 0; frame < N; ++frame) {
                if (sigmaClipping(stack[frame], sigma, sigLow, sigHigh, med, crej)) {
                    stack[frame] = med;
                    n++;
                }
            }
            changed = (n > 0);
        } while (changed);
        break;
    }

    // ----------------------------------------------------------------
    case Rejection::Winsorized:
    {
        if (!w_stack) break;

        do {
            float sigma = statsFloatSd(stack, N, nullptr);
            float med   = quickMedianFloat(stack, N);
            std::memcpy(w_stack, stack, N * sizeof(float));

            // Iteratively winsorise until sigma converges
            float sigma0;
            do {
                float m0 = med - 1.5f * sigma;
                float m1 = med + 1.5f * sigma;
                for (int jj = 0; jj < N; ++jj)
                    w_stack[jj] = std::min(m1, std::max(m0, w_stack[jj]));

                sigma0 = sigma;
                sigma  = 1.134f * statsFloatSd(w_stack, N, nullptr);
            } while (std::abs(sigma - sigma0) > sigma0 * 0.0005f);

            int r = 0;
            for (int frame = 0; frame < N; ++frame) {
                if (N - r <= 4) {
                    rejected[frame] = 0;
                } else {
                    rejected[frame] = sigmaClipping(stack[frame], sigma, sigLow,
                                                     sigHigh, med, crej);
                    if (rejected[frame]) r++;
                }
            }

            for (pixel = 0, output = 0; pixel < N; ++pixel) {
                if (!rejected[pixel]) {
                    stack[output] = stack[pixel];
                    output++;
                }
            }
            changed = (N != output);
            N = output;

        } while (changed && N > 3);
        break;
    }

    // ----------------------------------------------------------------
    case Rejection::LinearFit:
    {
        if (!data.xf || !data.yf) break;

        do {
            std::sort(stack, stack + N);
            for (int frame = 0; frame < N; ++frame)
                data.yf[frame] = stack[frame];

            // Least-squares line fit
            float m_x = data.m_x;
            float m_y = 0.0f;
            for (int j = 0; j < N; ++j) m_y += data.yf[j];
            m_y /= N;

            float ssxy = 0.0f;
            for (int j = 0; j < N; ++j)
                ssxy += (j - m_x) * (data.yf[j] - m_y);

            float a = ssxy * data.m_dx2;
            float b = m_y - a * m_x;

            // Mean absolute residual as the sigma estimate
            float sigma = 0.0f;
            for (int frame = 0; frame < N; ++frame)
                sigma += std::abs(stack[frame] - (a * frame + b));
            sigma /= N;

            int r = 0;
            for (int frame = 0; frame < N; ++frame) {
                if (N - r <= 4) {
                    rejected[frame] = 0;
                } else {
                    rejected[frame] = lineClipping(stack[frame], sigLow, sigHigh,
                                                    sigma, frame, a, b, crej);
                    if (rejected[frame]) r++;
                }
            }

            for (pixel = 0, output = 0; pixel < N; ++pixel) {
                if (!rejected[pixel]) {
                    if (pixel != output) stack[output] = stack[pixel];
                    output++;
                }
            }
            changed = (N != output);
            N = output;

        } while (changed && N > 3);
        break;
    }

    // ----------------------------------------------------------------
    case Rejection::GESDT:
    {
        if (!w_stack || !criticalValues) break;

        std::sort(stack, stack + N);
        median = stack[N / 2];

        int maxOutliers = static_cast<int>(nbFrames * sigLow);
        if (removed >= maxOutliers) return kept;
        maxOutliers -= removed;

        std::memcpy(w_stack, stack, N * sizeof(float));
        std::memset(rejected, 0, N * sizeof(int));

        // Iterative Grubbs test
        for (int iter = 0, size = N; iter < maxOutliers && size > 3;
             ++iter, --size)
        {
            float mean;
            float sd = statsFloatSd(w_stack, size, &mean);

            float maxDev = std::abs(mean - w_stack[0]);
            int   maxIdx = 0;
            float dev2   = std::abs(w_stack[size - 1] - mean);
            if (dev2 > maxDev) { maxDev = dev2; maxIdx = size - 1; }

            float Gstat = maxDev / sd;

            if (Gstat > criticalValues[iter + removed]) {
                if (w_stack[maxIdx] >= median) crej[1]++;
                else                           crej[0]++;

                for (int k = maxIdx; k < size - 1; ++k)
                    w_stack[k] = w_stack[k + 1];
            } else {
                break;
            }
        }

        // Keep only values within the surviving range
        float pmin = w_stack[0];
        float pmax = w_stack[N - crej[0] - crej[1] - 1];
        for (pixel = 0, output = 0; pixel < N; ++pixel) {
            if (stack[pixel] >= pmin && stack[pixel] <= pmax) {
                if (pixel != output) stack[output] = stack[pixel];
                output++;
            }
        }
        N = output;
        break;
    }

    // ----------------------------------------------------------------
    case Rejection::Biweight:
    {
        if (!w_stack) break;

        float C = sigHigh > 0 ? sigHigh : 6.0f;   // Tuning constant

        // Initial robust location and scale from median and MAD
        float* scratch = w_stack;
        std::memcpy(scratch, stack, N * sizeof(float));
        float med = quickMedianFloat(scratch, N);

        for (int i = 0; i < N; ++i)
            scratch[i] = std::abs(stack[i] - med);
        float mad = quickMedianFloat(scratch, N);

        if (mad < 1e-9f) return N;   // All values are identical

        double center = med;
        double scale  = mad;
        const int MAX_ITER = 5;

        // Iterative refinement of the biweight centre
        for (int iter = 0; iter < MAX_ITER; ++iter) {
            double num = 0.0, den = 0.0;
            for (int i = 0; i < N; ++i) {
                double u = (stack[i] - center) / (C * scale);
                if (std::abs(u) < 1.0) {
                    double w = (1.0 - u * u);
                    w = w * w;
                    num += (stack[i] - center) * w;
                    den += w;
                }
            }
            if (den > 1e-9) {
                double shift = num / den;
                center += shift;
                if (std::abs(shift) <= 1e-5 * scale) break;
            }
        }

        // Reject pixels outside C * scale from the converged centre
        double limit = C * scale;
        for (int i = 0; i < N; ++i) {
            double dist = std::abs(stack[i] - center);
            if (dist >= limit) {
                rejected[i] = (stack[i] < center) ? -1 : 1;
                if (stack[i] < center) crej[0]++;
                else                   crej[1]++;
            }
        }

        for (pixel = 0, output = 0; pixel < N; ++pixel) {
            if (!rejected[pixel]) {
                if (pixel != output) stack[output] = stack[pixel];
                output++;
            }
        }
        N = output;
        break;
    }

    // ----------------------------------------------------------------
    case Rejection::ModifiedZScore:
    {
        if (!w_stack) break;

        float threshold = sigHigh > 0 ? sigHigh : 3.5f;

        float* scratch = w_stack;
        std::memcpy(scratch, stack, N * sizeof(float));
        float med = quickMedianFloat(scratch, N);

        for (int i = 0; i < N; ++i)
            scratch[i] = std::abs(stack[i] - med);
        float mad = quickMedianFloat(scratch, N);

        if (mad < 1e-9f) return N;

        // Modified Z-score limit: threshold * MAD / 0.6745
        // where 0.6745 = 1 / 1.4826 converts MAD to sigma-equivalent
        float limit = threshold * (mad / 0.6745f);

        for (int i = 0; i < N; ++i) {
            float dev = std::abs(stack[i] - med);
            if (dev > limit) {
                rejected[i] = (stack[i] < med) ? -1 : 1;
                if (stack[i] < med) crej[0]++;
                else                crej[1]++;
            }
        }

        for (pixel = 0, output = 0; pixel < N; ++pixel) {
            if (!rejected[pixel]) {
                if (pixel != output) stack[output] = stack[pixel];
                output++;
            }
        }
        N = output;
        break;
    }

    // ----------------------------------------------------------------
    default:
    case Rejection::None:
        break;
    }

    return N;
}

// ============================================================================
// Weighted mean computation
// ============================================================================

/**
 * @brief Compute the (optionally weighted) mean of the surviving pixels.
 *
 * When external weights or per-pixel masks are provided, the function
 * uses the original (unsorted) stack to maintain correspondence between
 * pixel values and their per-frame weights.  Only values that fall
 * within the min/max range of the surviving (post-rejection) stack are
 * included.
 *
 * @param data        Scratch block containing stack and o_stack arrays.
 * @param keptPixels  Number of surviving pixels after rejection.
 * @param nbFrames    Original number of frames.
 * @param weights     Optional per-channel per-frame weight array.
 * @param maskStack   Optional per-frame pixel-mask array.
 * @param layer       Colour layer index (-1 for mono).
 * @return Weighted mean of the surviving pixels.
 */
inline float computeWeightedMean(
    StackDataBlock& data,
    int keptPixels,
    int nbFrames,
    const double* weights,
    const float* maskStack,
    int layer)
{
    if (keptPixels <= 0) return 0.0f;

    float* stack = (layer >= 0 && layer < 3) ? data.stackRGB[layer] : data.stack;

    // Fast path: unweighted arithmetic mean
    if (!weights && !maskStack) {
        double sum = 0.0;
        for (int k = 0; k < keptPixels; ++k) sum += stack[k];
        return static_cast<float>(sum / keptPixels);
    }

    // Weighted path: use the original unsorted stack for weight correspondence
    float* o_stack_ptr = (layer >= 0 && layer < 3)
                          ? data.o_stackRGB[layer]
                          : data.o_stack;

    // Determine the value range of the surviving pixels
    float pmin = stack[0], pmax = stack[0];
    for (int k = 1; k < keptPixels; ++k) {
        if (stack[k] < pmin) pmin = stack[k];
        if (stack[k] > pmax) pmax = stack[k];
    }

    double sum  = 0.0;
    double norm = 0.0;

    const double* pweights = weights
        ? (weights + (layer >= 0 ? layer : 0) * nbFrames)
        : nullptr;

    for (int frame = 0; frame < nbFrames; ++frame) {
        float val = o_stack_ptr[frame];
        bool isKept = (val >= pmin && val <= pmax && val != 0.0f);

        if (isKept) {
            double w = 1.0;
            if (pweights)  w *= pweights[frame];
            if (maskStack) w *= maskStack[frame];
            sum  += val * w;
            norm += w;
        }
    }

    if (norm == 0.0) {
        // Fallback: simple mean of kept pixels
        sum = 0.0;
        int count = 0;
        for (int frame = 0; frame < nbFrames; ++frame) {
            float val = o_stack_ptr[frame];
            if (val >= pmin && val <= pmax && val != 0.0f) {
                sum += val;
                count++;
            }
        }
        return (count > 0) ? static_cast<float>(sum / count) : 0.0f;
    }

    return static_cast<float>(sum / norm);
}

// ============================================================================
// Linked (3-channel) rejection
// ============================================================================

/**
 * @brief Apply rejection with cross-channel synchronisation.
 *
 * Rejection decisions are made independently per channel but linked
 * via a union rule: if any channel flags a frame as an outlier, all
 * three channels discard that frame.  This prevents colour artefacts
 * caused by rejecting a frame in only one channel.
 *
 * The GESDT algorithm uses a global Grubbs statistic across all three
 * channels to decide which frame to remove at each iteration.
 *
 * @param data            Per-pixel scratch block (RGB variant).
 * @param nbFrames        Original number of frames.
 * @param rejectionType   Algorithm selector.
 * @param sigLow          Low  rejection parameter.
 * @param sigHigh         High rejection parameter.
 * @param criticalValues  Pre-computed GESDT critical values (may be null).
 * @param crej            Two-element counter: [0] = low, [1] = high.
 * @return Number of surviving frames (same for all channels).
 */
inline int applyRejectionLinked(
    StackDataBlock& data,
    int nbFrames,
    Rejection rejectionType,
    float sigLow,
    float sigHigh,
    const float* criticalValues,
    int crej[2])
{
    int N = nbFrames;
    (void)criticalValues;   // Suppress unused warning when GESDT is not active

    int pixel, output, changed;
    float median[3] = {0.0f, 0.0f, 0.0f};

    // Per-channel scratch pointers
    float* stack[3]    = { data.stackRGB[0],    data.stackRGB[1],    data.stackRGB[2] };
    int*   rejected[3] = { data.rejectedRGB[0], data.rejectedRGB[1], data.rejectedRGB[2] };
    float* w_stack[3]  = { data.w_stackRGB[0],  data.w_stackRGB[1],  data.w_stackRGB[2] };
    float* o_stack[3]  = { data.o_stackRGB[0],  data.o_stackRGB[1],  data.o_stackRGB[2] };

    if (!stack[0] || !stack[1] || !stack[2]) return 0;

    // Preserve unsorted copies
    for (int c = 0; c < 3; ++c) {
        if (stack[c] && o_stack[c])
            std::memcpy(o_stack[c], stack[c], N * sizeof(float));
    }

    // Synchronised validation: remove frames where any channel is zero
    int kept = 0;
    for (int frame = 0; frame < N; ++frame) {
        bool valid = (stack[0][frame] != 0.0f) &&
                     (stack[1][frame] != 0.0f) &&
                     (stack[2][frame] != 0.0f);
        if (valid) {
            if (frame != kept) {
                for (int c = 0; c < 3; ++c)
                    stack[c][kept] = stack[c][frame];
            }
            kept++;
        }
    }

    if (kept <= 1) return kept;
    N = kept;

    if (rejectionType == Rejection::None) return N;

    // ---- Iterative rejection loop ----
    do {
        // Step 1: Compute per-channel statistics and flag outliers
        for (int c = 0; c < 3; ++c) {
            float* s = stack[c];

            median[c] = quickMedianFloat(s, N);

            // Compute the dispersion estimator
            float sigma = 0.0f;
            if (rejectionType == Rejection::Sigma) {
                sigma = statsFloatSd(s, N, nullptr);

            } else if (rejectionType == Rejection::Winsorized) {
                // Robust Winsorized sigma estimate
                float* w = w_stack[c];
                if (!w) w = o_stack[c];
                if (w) std::memcpy(w, s, N * sizeof(float));

                float currentSigma = statsFloatSd(s, N, nullptr);
                float prevSigma    = 0.0f;
                const int MAX_ITER = 5;

                for (int iter = 0; iter < MAX_ITER; ++iter) {
                    float low  = median[c] - 1.5f * currentSigma;
                    float high = median[c] + 1.5f * currentSigma;

                    for (int k = 0; k < N; ++k) {
                        if      (w[k] < low)  w[k] = low;
                        else if (w[k] > high) w[k] = high;
                    }

                    prevSigma    = currentSigma;
                    currentSigma = statsFloatSd(w, N, nullptr) * 1.134f;

                    if (std::abs(currentSigma - prevSigma) < 1e-5 * prevSigma)
                        break;
                }
                sigma = currentSigma;

            } else if (rejectionType == Rejection::MAD) {
                float* scratch = w_stack[c] ? w_stack[c] : o_stack[c];
                sigma = statsFloatMad(s, N, median[c], scratch);
            }

            // Flag outliers per channel (standard methods only)
            if (rejectionType != Rejection::GESDT) {
                for (int f = 0; f < N; ++f) {
                    rejected[c][f] = 0;
                    if (rejectionType == Rejection::Percentile) {
                        if (median[c] - s[f] > median[c] * sigLow)
                            rejected[c][f] = -1;
                        else if (s[f] - median[c] > median[c] * sigHigh)
                            rejected[c][f] = 1;
                    } else {
                        if (median[c] - s[f] > sigma * sigLow)
                            rejected[c][f] = -1;
                        else if (s[f] - median[c] > sigma * sigHigh)
                            rejected[c][f] = 1;
                    }
                }
            }
        }

        // Step 2: GESDT cross-channel union rejection
        if (rejectionType == Rejection::GESDT && criticalValues) {
            int maxOutliers = static_cast<int>(nbFrames * sigLow);
            for (int c = 0; c < 3; ++c)
                std::memcpy(w_stack[c], stack[c], N * sizeof(float));

            int currentSize = N;
            for (int iter = 0;
                 iter < maxOutliers && currentSize > 3;
                 ++iter, --currentSize)
            {
                float maxG         = 0.0f;
                int   maxIdxGlobal = -1;

                for (int c = 0; c < 3; ++c) {
                    float mean;
                    float sd = statsFloatSd(w_stack[c], currentSize, &mean);
                    if (sd <= 1e-10f) continue;

                    float localMaxDev = std::abs(w_stack[c][0] - mean);
                    int   localIdx    = 0;
                    float devEnd      = std::abs(w_stack[c][currentSize - 1] - mean);
                    if (devEnd > localMaxDev) {
                        localMaxDev = devEnd;
                        localIdx    = currentSize - 1;
                    }
                    float localG = localMaxDev / sd;
                    if (localG > maxG) {
                        maxG         = localG;
                        maxIdxGlobal = localIdx;
                    }
                }

                if (maxIdxGlobal != -1 &&
                    maxG > criticalValues[iter + (nbFrames - N)])
                {
                    crej[1]++;
                    for (int c = 0; c < 3; ++c) {
                        for (int k = maxIdxGlobal; k < currentSize - 1; ++k)
                            w_stack[c][k] = w_stack[c][k + 1];
                    }
                } else {
                    break;
                }
            }

            for (int c = 0; c < 3; ++c)
                std::memcpy(stack[c], w_stack[c], currentSize * sizeof(float));

            N       = currentSize;
            changed = false;   // GESDT completes in a single outer pass

        } else {
            // Step 2 (standard methods): Union rejection across channels
            for (int f = 0; f < N; ++f) {
                bool anyRej = (rejected[0][f] != 0) ||
                              (rejected[1][f] != 0) ||
                              (rejected[2][f] != 0);
                if (anyRej) {
                    int lowVotes = 0, highVotes = 0;
                    for (int c = 0; c < 3; ++c) {
                        if      (rejected[c][f] < 0) lowVotes++;
                        else if (rejected[c][f] > 0) highVotes++;
                    }
                    for (int c = 0; c < 3; ++c) rejected[c][f] = 1;

                    if (lowVotes >= highVotes) crej[0]++;
                    else                       crej[1]++;
                }
            }

            // Step 3: Compact all channels synchronously
            output = 0;
            for (pixel = 0; pixel < N; ++pixel) {
                if (!rejected[0][pixel]) {
                    if (pixel != output) {
                        for (int c = 0; c < 3; ++c)
                            stack[c][output] = stack[c][pixel];
                    }
                    output++;
                }
            }
            changed = (N != output);
            N = output;
        }


    } while (changed && N > 3 &&
             (rejectionType != Rejection::Percentile));

    return N;
}

} // namespace InlineRejection
} // namespace Stacking

#endif // INLINE_REJECTION_H