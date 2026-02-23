

#ifndef INLINE_REJECTION_H
#define INLINE_REJECTION_H

#include <cmath>
#include <cstring>
#include <algorithm>
#include "StackDataBlock.h"
#include "StackingTypes.h"

namespace Stacking {

namespace InlineRejection {

constexpr int MAX_GESDT_OUTLIERS = 25;

inline double t_inv(double p, double df) {
    if (df <= 0) return 3.0;
    // Approximation for t-distribution inverse at 0.05 level
    double x = std::sqrt(df * (std::pow(p, -2.0/df) - 1.0));
    return x;
}

/**
 * @brief Compute GESDT critical value for sample size n
 */
inline float computeGESDTCriticalValue(int n, float alpha) {
    if (n <= 2) return 3.0f;
    double p = 1.0 - (double)alpha / (2.0 * n);
    double df = (double)n - 2.0;
    double t = t_inv(p, df);
    double crit = (t * (n - 1)) / std::sqrt((df + t * t) * n);
    return static_cast<float>(crit);
}

//=============================================================================
// FAST STATISTICS
//=============================================================================

/**
 * Quick median using nth_element (O(n) average)
 * Modifies the input array (partial sort)
 */
inline float quickMedianFloat(float* data, int n) {
    if (n <= 0) return 0.0f;
    if (n == 1) return data[0];
    if (n == 2) return (data[0] + data[1]) * 0.5f;
    
    int mid = n / 2;
    std::nth_element(data, data + mid, data + n);
    
    if (n % 2 == 0) {
        // For even n, need max of lower half
        float maxLower = *std::max_element(data, data + mid);
        return (maxLower + data[mid]) * 0.5f;
    }
    return data[mid];
}

inline float statsFloatSd(const float* data, int n, float* outMean = nullptr) {
    if (n <= 1) {
        if (outMean) *outMean = (n == 1) ? data[0] : 0.0f;
        return 0.0f;
    }
    
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        sum += data[i];
    }
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
 * MAD (Median Absolute Deviation) for float array
 */
inline float statsFloatMad(float* data, int n, float median, float* scratch) {
    if (n <= 0) return 0.0f;
    
    // Compute absolute deviations
    for (int i = 0; i < n; ++i) {
        scratch[i] = std::abs(data[i] - median);
    }

    // MAD = median of absolute deviations (raw, NOT scaled by 1.4826).
    return quickMedianFloat(scratch, n);
}

//=============================================================================
// INDIVIDUAL CLIPPING FUNCTIONS (from rejection_float.c)
//=============================================================================

inline int percentileClipping(float pixel, float pLow, float pHigh, 
                               float median, int rej[2]) {
    if (median - pixel > median * pLow) {
        rej[0]++;
        return -1;
    } else if (pixel - median > median * pHigh) {
        rej[1]++;
        return 1;
    }
    return 0;
}

inline int sigmaClipping(float pixel, float sigma, float sigmaLow, 
                          float sigmaHigh, float median, int rej[2]) {
    if (median - pixel > sigma * sigmaLow) {
        rej[0]++;
        return -1;
    } else if (pixel - median > sigma * sigmaHigh) {
        rej[1]++;
        return 1;
    }
    return 0;
}

inline int lineClipping(float pixel, float sigmaLow, float sigmaHigh,
                        float sigma, int i, float a, float b, int rej[2]) {
    if (a * i + b - pixel > sigma * sigmaLow) {
        rej[0]++;
        return -1;
    } else if (pixel - a * i - b > sigma * sigmaHigh) {
        rej[1]++;
        return 1;
    }
    return 0;
}

//=============================================================================
// MAIN REJECTION FUNCTION (from rejection_float.c apply_rejection_float)
//=============================================================================

inline int applyRejection(
    StackDataBlock& data,
    int nbFrames,
    Rejection rejectionType,
    float sigLow,
    float sigHigh,
    const float* criticalValues,
    int crej[2]
) {
    int N = nbFrames;
    float median = 0.0f;
    int pixel, output, changed;
    int firstloop = 1;
    int kept = 0, removed = 0;
    
    float* stack = data.stack;
    float* w_stack = data.w_stack;
    int* rejected = data.rejected;
    float* o_stack = data.o_stack;
    
    // Make a copy of unsorted stack (for weighted mean later)
    std::memcpy(o_stack, stack, N * sizeof(float));
    
    for (int frame = 0; frame < N; frame++) {
        if (stack[frame] != 0.0f) {
            if (frame != kept) {
                stack[kept] = stack[frame];
            }
            kept++;
        }
    }
    
    // Not enough valid pixels
    if (kept <= 1) {
        return kept;
    }
    removed = N - kept;
    N = kept;
    
    // No rejection requested
    if (rejectionType == Rejection::None) {
        return N;
    }
    
    // Prepare median for algorithms that need it
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
    
    // Apply rejection algorithm
    switch (rejectionType) {
        
        case Rejection::Percentile:
            for (int frame = 0; frame < N; frame++) {
                rejected[frame] = percentileClipping(stack[frame], sigLow, sigHigh, median, crej);
            }
            // Compact
            for (pixel = 0, output = 0; pixel < N; pixel++) {
                if (!rejected[pixel]) {
                    if (pixel != output) stack[output] = stack[pixel];
                    output++;
                }
            }
            N = output;
            break;
            
        case Rejection::Sigma:
        case Rejection::MAD:
            do {
                float var;
                if (rejectionType == Rejection::Sigma) {
                    var = statsFloatSd(stack, N, nullptr);
                } else {
                    // MAD needs scratch buffer - reuse w_stack or o_stack
                    float* scratch = w_stack ? w_stack : o_stack;
                    var = statsFloatMad(stack, N, median, scratch);
                }
                
                if (!firstloop) {
                    median = quickMedianFloat(stack, N);
                } else {
                    firstloop = 0;
                }
                
                int r = 0;
                for (int frame = 0; frame < N; frame++) {
                    if (N - r <= 4) {
                        rejected[frame] = 0;
                    } else {
                        rejected[frame] = sigmaClipping(stack[frame], var, sigLow, sigHigh, median, crej);
                        if (rejected[frame]) r++;
                    }
                }
                
                // Compact
                for (pixel = 0, output = 0; pixel < N; pixel++) {
                    if (!rejected[pixel]) {
                        if (pixel != output) stack[output] = stack[pixel];
                        output++;
                    }
                }
                changed = (N != output);
                N = output;
            } while (changed && N > 3);
            break;
            
        case Rejection::SigmaMedian:
            // Sigma median replaces outliers with median instead of removing
            do {
                float sigma = statsFloatSd(stack, N, nullptr);
                float med = quickMedianFloat(stack, N);
                int n = 0;
                for (int frame = 0; frame < N; frame++) {
                    if (sigmaClipping(stack[frame], sigma, sigLow, sigHigh, med, crej)) {
                        stack[frame] = med;
                        n++;
                    }
                }
                changed = (n > 0);
            } while (changed);
            break;
            
        case Rejection::Winsorized:
            if (!w_stack) break;
            do {
                float sigma = statsFloatSd(stack, N, nullptr);
                float med = quickMedianFloat(stack, N);
                std::memcpy(w_stack, stack, N * sizeof(float));
                
                float sigma0;
                do {
                    float m0 = med - 1.5f * sigma;
                    float m1 = med + 1.5f * sigma;
                    for (int jj = 0; jj < N; jj++) {
                        w_stack[jj] = std::min(m1, std::max(m0, w_stack[jj]));
                    }
                    sigma0 = sigma;
                    sigma = 1.134f * statsFloatSd(w_stack, N, nullptr);
                } while (std::abs(sigma - sigma0) > sigma0 * 0.0005f);
                
                int r = 0;
                for (int frame = 0; frame < N; frame++) {
                    if (N - r <= 4) {
                        rejected[frame] = 0;
                    } else {
                        rejected[frame] = sigmaClipping(stack[frame], sigma, sigLow, sigHigh, med, crej);
                        if (rejected[frame]) r++;
                    }
                }
                
                // Compact
                for (pixel = 0, output = 0; pixel < N; pixel++) {
                    if (!rejected[pixel]) {
                        stack[output] = stack[pixel];
                        output++;
                    }
                }
                changed = (N != output);
                N = output;
            } while (changed && N > 3);
            break;
            
        case Rejection::LinearFit:
            if (!data.xf || !data.yf) break;
            do {
                std::sort(stack, stack + N);
                for (int frame = 0; frame < N; frame++) {
                    data.yf[frame] = stack[frame];
                }
                
                // Simple linear fit
                float m_x = data.m_x;
                float m_y = 0.0f;
                for (int j = 0; j < N; ++j) m_y += data.yf[j];
                m_y /= N;
                
                float ssxy = 0.0f;
                for (int j = 0; j < N; ++j) {
                    ssxy += (j - m_x) * (data.yf[j] - m_y);
                }
                float a = ssxy * data.m_dx2;
                float b = m_y - a * m_x;
                
                float sigma = 0.0f;
                for (int frame = 0; frame < N; frame++) {
                    sigma += std::abs(stack[frame] - (a * frame + b));
                }
                sigma /= N;
                
                int r = 0;
                for (int frame = 0; frame < N; frame++) {
                    if (N - r <= 4) {
                        rejected[frame] = 0;
                    } else {
                        rejected[frame] = lineClipping(stack[frame], sigLow, sigHigh, sigma, frame, a, b, crej);
                        if (rejected[frame]) r++;
                    }
                }
                
                // Compact
                for (pixel = 0, output = 0; pixel < N; pixel++) {
                    if (!rejected[pixel]) {
                        if (pixel != output) stack[output] = stack[pixel];
                        output++;
                    }
                }
                changed = (N != output);
                N = output;
            } while (changed && N > 3);
            break;
            
        case Rejection::GESDT:
            // GESDT (Generalized Extreme Studentized Deviate Test)
            // Simplified implementation
            if (!w_stack || !criticalValues) break;
            {
                std::sort(stack, stack + N);
                median = stack[N/2];
                
                int maxOutliers = static_cast<int>(nbFrames * sigLow);
                if (removed >= maxOutliers) return kept;
                maxOutliers -= removed;
                
                std::memcpy(w_stack, stack, N * sizeof(float));
                std::memset(rejected, 0, N * sizeof(int));
                
                // Grubbs test iterations
                for (int iter = 0, size = N; iter < maxOutliers && size > 3; iter++, size--) {
                    float mean;
                    float sd = statsFloatSd(w_stack, size, &mean);
                    
                    float maxDev = std::abs(mean - w_stack[0]);
                    int maxIdx = 0;
                    float dev2 = std::abs(w_stack[size-1] - mean);
                    if (dev2 > maxDev) {
                        maxDev = dev2;
                        maxIdx = size - 1;
                    }
                    
                    float Gstat = maxDev / sd;
                    
                    // Check against critical value
                    if (Gstat > criticalValues[iter + removed]) {
                        // Mark as outlier
                        if (w_stack[maxIdx] >= median) {
                            crej[1]++;
                        } else {
                            crej[0]++;
                        }
                        // Remove element
                        for (int k = maxIdx; k < size - 1; k++) {
                            w_stack[k] = w_stack[k + 1];
                        }
                    } else {
                        break;
                    }
                }
                
                // Compact based on value ranges
                float pmin = w_stack[0], pmax = w_stack[N - crej[0] - crej[1] - 1];
                for (pixel = 0, output = 0; pixel < N; pixel++) {
                    if (stack[pixel] >= pmin && stack[pixel] <= pmax) {
                        if (pixel != output) stack[output] = stack[pixel];
                        output++;
                    }
                }
                N = output;
            }
            break;
            
        case Rejection::Biweight:
            // Biweight estimator with tuning constant (default 6)
            {
                if (!w_stack) break;
                float C = sigHigh > 0 ? sigHigh : 6.0f;
                
                // Initial robust center/scale from median & MAD
                float* scratch = w_stack;
                std::memcpy(scratch, stack, N * sizeof(float));
                float med = quickMedianFloat(scratch, N);
                
                for (int i = 0; i < N; ++i)
                    scratch[i] = std::abs(stack[i] - med);
                float mad = quickMedianFloat(scratch, N);
                
                if (mad < 1e-9f) {
                    // All identical
                    return N;
                }
                
                double center = med;
                double scale = mad;
                const int MAX_ITER = 5;
                
                // Iterate to refine center
                for (int iter = 0; iter < MAX_ITER; ++iter) {
                    double num = 0.0, den = 0.0;
                    for (int i = 0; i < N; ++i) {
                        double u = (stack[i] - center) / (C * scale);
                        if (std::abs(u) < 1.0) {
                            double w = (1.0 - u*u);
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
                
                // Reject pixels with |pixel - center| >= C * scale
                double limit = C * scale;
                for (int i = 0; i < N; ++i) {
                    double dist = std::abs(stack[i] - center);
                    if (dist >= limit) {
                        rejected[i] = (stack[i] < center) ? -1 : 1;
                        if (stack[i] < center) crej[0]++;
                        else crej[1]++;
                    }
                }
                
                // Compact
                for (pixel = 0, output = 0; pixel < N; pixel++) {
                    if (!rejected[pixel]) {
                        if (pixel != output) stack[output] = stack[pixel];
                        output++;
                    }
                }
                N = output;
            }
            break;
            
        case Rejection::ModifiedZScore:
            // Modified Z-Score: M_i = 0.6745 * (x_i - median) / MAD
            // Reject if M_i > threshold (default 3.5)
            {
                if (!w_stack) break;
                float threshold = sigHigh > 0 ? sigHigh : 3.5f;
                
                // Median
                float* scratch = w_stack;
                std::memcpy(scratch, stack, N * sizeof(float));
                float med = quickMedianFloat(scratch, N);
                
                // MAD
                for (int i = 0; i < N; ++i)
                    scratch[i] = std::abs(stack[i] - med);
                float mad = quickMedianFloat(scratch, N);
                
                if (mad < 1e-9f) {
                    return N;
                }
                
                // Modified Z-Score: 0.6745 = 1 / 1.4826 (inverse transformation)
                float limit = threshold * (mad / 0.6745f);
                
                for (int i = 0; i < N; ++i) {
                    float dev = std::abs(stack[i] - med);
                    if (dev > limit) {
                        rejected[i] = (stack[i] < med) ? -1 : 1;
                        if (stack[i] < med) crej[0]++;
                        else crej[1]++;
                    }
                }
                
                // Compact
                for (pixel = 0, output = 0; pixel < N; pixel++) {
                    if (!rejected[pixel]) {
                        if (pixel != output) stack[output] = stack[pixel];
                        output++;
                    }
                }
                N = output;
            }
            break;
            
        default:
        case Rejection::None:
            // No rejection
            break;
    }
    
    return N;
}

inline float computeWeightedMean(
    StackDataBlock& data,
    int keptPixels,
    int nbFrames,
    const double* weights,
    const float* maskStack,
    int layer
) {
    if (keptPixels <= 0) return 0.0f;
    
    float* stack = (layer >= 0 && layer < 3) ? data.stackRGB[layer] : data.stack;
    
    if (!weights && !maskStack) {
        // Simple mean
        double sum = 0.0;
        for (int k = 0; k < keptPixels; ++k) {
            sum += stack[k];
        }
        return static_cast<float>(sum / keptPixels);
    }
    
    // Weighted mean: need to use o_stack to maintain correspondence with weights/masks
    float* o_stack_ptr = (layer >= 0 && layer < 3) ? data.o_stackRGB[layer] : data.o_stack;
    
    // Find min/max of kept pixels to identify which original pixels to include
    float pmin = stack[0], pmax = stack[0];
    for (int k = 1; k < keptPixels; ++k) {
        if (stack[k] < pmin) pmin = stack[k];
        if (stack[k] > pmax) pmax = stack[k];
    }
    
    double sum = 0.0;
    double norm = 0.0;
    
    // Weights are stored as [ch0_img0...ch0_imgN, ch1_img0...ch1_imgN, ...]
    const double* pweights = weights ? (weights + (layer >= 0 ? layer : 0) * nbFrames) : nullptr;
    
    for (int frame = 0; frame < nbFrames; ++frame) {
        float val = o_stack_ptr[frame];
        // Check if value is within kept range (was not rejected)
        bool kept = (val >= pmin && val <= pmax && val != 0.0f);
        
        if (kept) {
            double w = 1.0;
            if (pweights) w *= pweights[frame];
            if (maskStack) w *= maskStack[frame];
            
            sum += val * w;
            norm += w;
        }
    }
    
    if (norm == 0.0) {
        // Fallback to simple mean of kept pixels if weights collapsed
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

//=============================================================================
// LINKED REJECTION (3-CHANNEL PARITY)
//=============================================================================

inline int applyRejectionLinked(
    StackDataBlock& data,
    int nbFrames,
    Rejection rejectionType,
    float sigLow,
    float sigHigh,
    const float* criticalValues,
    int crej[2]
) {
    int N = nbFrames;
    (void)criticalValues; // Suppress unused warning (GESDT not yet linked)
    int pixel, output, changed;
    int firstloop = 1;
    float median[3] = {0,0,0};
    
    // Pointers
    float* stack[3] = { data.stackRGB[0], data.stackRGB[1], data.stackRGB[2] };
    int* rejected[3] = { data.rejectedRGB[0], data.rejectedRGB[1], data.rejectedRGB[2] };
    float* w_stack[3] = { data.w_stackRGB[0], data.w_stackRGB[1], data.w_stackRGB[2] };
    float* o_stack[3] = { data.o_stackRGB[0], data.o_stackRGB[1], data.o_stackRGB[2] };
    
    // Check pointers (safety)
    if (!stack[0] || !stack[1] || !stack[2]) return 0;

    // Copy unsorted stacks
    for(int c=0; c<3; ++c) {
        if(stack[c] && o_stack[c])
            std::memcpy(o_stack[c], stack[c], N * sizeof(float));
    }
    
    // Initial compaction (remove zero/invalid pixels from ALL channels if any is invalid)
    // Synchronized validation
    int kept = 0;
    for (int frame = 0; frame < N; frame++) {
        // Enforce intersection of valid pixels
        bool valid = (stack[0][frame] != 0.0f) && (stack[1][frame] != 0.0f) && (stack[2][frame] != 0.0f);
        if (valid) {
            if (frame != kept) {
                for(int c=0; c<3; ++c) stack[c][kept] = stack[c][frame];
            }
            kept++;
        }
    }
    
    if (kept <= 1) return kept;
    N = kept;
    
    if (rejectionType == Rejection::None) return N;

    // Iterative Rejection Loop
    do {
        // 1. Compute stats & marks per channel
        for(int c=0; c<3; ++c) {
             float* s = stack[c];
             // Compute Median
             if (!firstloop) {
                 median[c] = quickMedianFloat(s, N);
             } else {
                 median[c] = quickMedianFloat(s, N); 
             }
             
             // Compute Sigma/MAD
             float sigma = 0.0f;
             if (rejectionType == Rejection::Sigma) { 
                  sigma = statsFloatSd(s, N, nullptr);
             } 
             else if (rejectionType == Rejection::Winsorized) {
                  // Robust Winsorized Sigma
                  // 1. Copy to temp buffer (w_stack)
                  float* w = w_stack[c];
                  if (!w) w = o_stack[c]; // Fallback (should have w_stack)
                  if (w) std::memcpy(w, s, N * sizeof(float));
                  
                  float currentSigma = statsFloatSd(s, N, nullptr);
                  float prevSigma = 0.0f;
                  const int MAX_ITER = 5;
                  
                  for(int iter=0; iter<MAX_ITER; ++iter) {
                      float low = median[c] - 1.5f * currentSigma;
                      float high = median[c] + 1.5f * currentSigma;
                      
                      // Winsorize (Clamp outliers)
                      for(int k=0; k<N; ++k) {
                          if (w[k] < low) w[k] = low;
                          else if (w[k] > high) w[k] = high;
                      }
                      
                      prevSigma = currentSigma;
                      // Recalculate Sigma on clamped data * 1.134 (Correction factor for normal dist)
                      currentSigma = statsFloatSd(w, N, nullptr) * 1.134f;
                      
                      if (std::abs(currentSigma - prevSigma) < 1e-5 * prevSigma) break;
                  }
                  sigma = currentSigma;
             } 
             else if (rejectionType == Rejection::MAD) {
                 float* scratch = w_stack[c] ? w_stack[c] : o_stack[c];
                 sigma = statsFloatMad(s, N, median[c], scratch);
             }
             
             // Detect Outliers (Standard methods)
             if (rejectionType != Rejection::GESDT) {
                 for(int f=0; f<N; ++f) {
                     rejected[c][f] = 0; 
                     if (rejectionType == Rejection::Percentile) {
                         if (median[c] - s[f] > median[c] * sigLow) rejected[c][f] = -1;
                         else if (s[f] - median[c] > median[c] * sigHigh) rejected[c][f] = 1;
                     } else {
                         if (median[c] - s[f] > sigma * sigLow) rejected[c][f] = -1;
                         else if (s[f] - median[c] > sigma * sigHigh) rejected[c][f] = 1;
                     }
                 }
             }
        }

        // 2. GESDT Specialized Union Rejection
        if (rejectionType == Rejection::GESDT && criticalValues) {
            int maxOutliers = static_cast<int>(nbFrames * sigLow);
            // We need a workspace for Grubbs test across channels
            // Use w_stackRGB as workspace
            for(int c=0; c<3; ++c) std::memcpy(w_stack[c], stack[c], N * sizeof(float));
            
            int currentSize = N;
            for (int iter = 0; iter < maxOutliers && currentSize > 3; iter++, currentSize--) {
                float maxG = 0.0f;
                int maxIdxGlobal = -1;
                // int maxChan = -1;

                for(int c=0; c<3; ++c) {
                    float mean;
                    float sd = statsFloatSd(w_stack[c], currentSize, &mean);
                    if (sd <= 1e-10f) continue;

                    // Find extreme in this channel
                    float localMaxDev = std::abs(w_stack[c][0] - mean);
                    int localIdx = 0;
                    float devEnd = std::abs(w_stack[c][currentSize-1] - mean);
                    if (devEnd > localMaxDev) {
                        localMaxDev = devEnd;
                        localIdx = currentSize-1;
                    }
                    float localG = localMaxDev / sd;
                    if (localG > maxG) {
                        maxG = localG;
                        maxIdxGlobal = localIdx;
                        // maxChan = c;
                    }
                }

                if (maxIdxGlobal != -1 && maxG > criticalValues[iter + (nbFrames - N)]) {
                    // Mark global rejection in ALL channels for this iteration's extreme frame
                    crej[1]++; // Count as high rejection (simplified)
                    // Remove from workspace across all channels
                    for(int c=0; c<3; ++c) {
                        for (int k = maxIdxGlobal; k < currentSize - 1; k++) {
                            w_stack[c][k] = w_stack[c][k + 1];
                        }
                    }
                } else {
                    break;
                }
            }

            // Real compaction for GESDT survivors
            for(int c=0; c<3; ++c) {
                std::memcpy(stack[c], w_stack[c], currentSize * sizeof(float));
            }
            N = currentSize;
            changed = false; // GESDT is done in one outer pass
        } else {
            // 2. Union Rejection (Link Channels for standard methods)
            for(int f=0; f<N; ++f) {
                bool anyRej = (rejected[0][f] != 0) || (rejected[1][f] != 0) || (rejected[2][f] != 0);
                if (anyRej) {
                    rejected[0][f] = 1;
                    rejected[1][f] = 1;
                    rejected[2][f] = 1;
                    crej[0]++; 
                }
            }
            
            // 3. Compact standard rejections
            output = 0;
            for (pixel = 0; pixel < N; pixel++) {
                if (!rejected[0][pixel]) { 
                    if (pixel != output) {
                        for(int c=0; c<3; ++c) stack[c][output] = stack[c][pixel];
                    }
                    output++;
                }
            }
            changed = (N != output);
            N = output;
        }
        
        firstloop = 0;
        
    } while(changed && N > 3 && (rejectionType != Rejection::Percentile));
    
    return N;
}

} // namespace InlineRejection
} // namespace Stacking

#endif // INLINE_REJECTION_H
