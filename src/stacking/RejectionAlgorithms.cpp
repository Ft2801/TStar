
#include "RejectionAlgorithms.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Stacking {

//=============================================================================
// MAIN DISPATCHER
//=============================================================================

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
    int n = static_cast<int>(stack.size());
    
    // Initialize rejection array
    rejected.assign(n, 0);
    
    // NOTE: We do NOT call removeNullPixels here anymore.
    // That function destructively resizes the stack, breaking the 1:1
    // correspondence between stack indices and the rejected array.
    // The caller (processMeanBlock) should handle null/invalid pixels.
    
    // Check minimum requirements
    if (n < 3) {
        RejectionResult result;
        result.keptCount = n;
        return result;
    }
    
    // Apply appropriate rejection algorithm
    switch (type) {
        case Rejection::None:
            {
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
            return biweightClipping(stack, sigmaHigh > 0 ? sigmaHigh : 6.0f, rejected, scratch);
            
        case Rejection::ModifiedZScore:
            // Use sigmaHigh as threshold (default 3.5)
            return modifiedZScoreClipping(stack, sigmaHigh > 0 ? sigmaHigh : 3.5f, rejected, scratch);
            
        default:
            {
                RejectionResult result;
                result.keptCount = n;
                return result;
            }
    }
}

//=============================================================================
// PERCENTILE CLIPPING
//=============================================================================

RejectionResult RejectionAlgorithms::percentileClipping(
    const std::vector<float>& stack,
    float pLow, float pHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }
    
    // Compute median
    // Use scratch for sorting
    std::vector<float> localBuf;
    std::vector<float>& work = scratch ? *scratch : localBuf;
    if (scratch) work = stack; else work = stack; // Copy
    
    float median = Statistics::quickMedian(work);
    
    if (median == 0.0f) {
        result.keptCount = n;
        return result;
    }
    
    // Apply percentile rejection
    for (int i = 0; i < n; ++i) {
        float pixel = stack[i];
        
        // Low rejection: (median - pixel) / median > pLow
        if ((median - pixel) > median * pLow) {
            rejected[i] = -1;
            result.lowRejected++;
        }
        // High rejection: (pixel - median) / median > pHigh
        else if ((pixel - median) > median * pHigh) {
            rejected[i] = 1;
            result.highRejected++;
        }
    }
    
    result.keptCount = n - result.lowRejected - result.highRejected;
    return result;
}

//=============================================================================
// SIGMA CLIPPING
//=============================================================================

RejectionResult RejectionAlgorithms::sigmaClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch,
    const std::vector<float>* weights)
{
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    // Local buffer management (avoids Allocation if scratch provided)
    std::vector<float> localBuf;
    std::vector<float>& work = scratch ? *scratch : localBuf;
    if (!scratch) localBuf.reserve(n);

    // Initial pass: all valid
    int currentN = n;
    
    // Iterative sigma clipping
    bool changed = true;
    int totalRejected = 0;
    
    while (changed && currentN > 3) {
        changed = false;
        
        float median = 0.0f;
        double sigma = 0.0f;

        if (weights) {
            median = weightedMedian(stack, *weights, &rejected);
            auto stats = weightedMeanAndStdDev(stack, *weights, &rejected);
            sigma = stats.second;
        } else {
            // 1. Gather valid pixels into 'work'
            work.clear();
            for (int i = 0; i < n; ++i) {
                if (rejected[i] == 0) {
                    work.push_back(stack[i]);
                }
            }
            
            // 2. Compute statistics
            median = Statistics::quickMedian(work.data(), work.size());
            sigma = Statistics::stdDev(work.data(), work.size(), nullptr);
        }
        
        // 3. Check each pixel in ORIGINAL stack
        for (int i = 0; i < n; ++i) {
            if (rejected[i] != 0) continue;  // Already rejected
            
            if (currentN <= 3) {
                 break;
            }
            
            float pixel = stack[i];
            
            // Low rejection
            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++;
                totalRejected++;
                changed = true;
                currentN--; 
            }
            // High rejection
            else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                totalRejected++;
                changed = true;
                currentN--;
            }
        }
    }
    
    result.keptCount = currentN;
    return result;
}

//=============================================================================
// MAD CLIPPING
//=============================================================================

RejectionResult RejectionAlgorithms::madClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    Q_UNUSED(scratch);
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }
    
    // Iterative MAD clipping
    bool changed = true;
    int totalRejected = 0;
    int currentN = n;
    
    // Local scratch for stats (needed because MAD implementation likely modifies buffer or we need sorted copy)
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;

    while (changed && currentN > 3) {
        changed = false;
        
        // Gather valid
        work.clear();
        for (int i=0; i<n; ++i) {
            if (rejected[i] == 0) work.push_back(stack[i]);
        }
        
        // Compute statistics
        float median = Statistics::quickMedian(work.data(), work.size());
        double mad = Statistics::mad(work.data(), work.size(), median);
        
        // Raw MAD — do NOT scale by 1.4826.
        double sigma = mad;
        
        // Check each pixel
        for (int i = 0; i < n; ++i) {
            if (rejected[i] != 0) continue;
            
            if (currentN <= 3) break;
            
            float pixel = stack[i];
            
            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++;
                totalRejected++;
                changed = true;
                currentN--;
            }
            else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                totalRejected++;
                changed = true;
                currentN--;
            }
        }
    }
    
    result.keptCount = currentN;
    return result;
}

//=============================================================================
// SIGMA-MEDIAN CLIPPING
//=============================================================================

RejectionResult RejectionAlgorithms::sigmaMedianClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }
    
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;

    // Iterate until no more replacements
    int replaced;
    do {
        replaced = 0;
        
        // Copy current stack to work for stats (since we might modify stack in place?)
        // Wait, SigmaMedian modifies STACK in place.
        // But we need stats from current stack.
        work = stack; 
        
        double sigma = Statistics::stdDev(work.data(), n, nullptr);
        float median = Statistics::quickMedian(work.data(), n);
        
        for (int i = 0; i < n; ++i) {
            float pixel = stack[i];
            
            bool isOutlier = false;
            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++; // Warning: accumulated over iterations? Logic might be flawed in original too.
                isOutlier = true;
            }
            else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                isOutlier = true;
            }
            
            if (isOutlier) {
                // Replace with median instead of removing
                stack[i] = median;
                replaced++;
            }
        }
    } while (replaced > 0);
    
    // All pixels kept (but modified)
    result.keptCount = n;
    return result;
}

//=============================================================================
// WINSORIZED CLIPPING
//=============================================================================

RejectionResult RejectionAlgorithms::winsorizedClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch,
    const std::vector<float>* weights)
{
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }
    
    // Working copy for winsorization
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& wStack = scratch ? *scratch : localBuf;
    
    wStack = stack;
    
    bool changed = true;
    int totalRejected = 0;
    int currentN = n;
    
    while (changed && currentN > 3) {
        changed = false;

        float median = 0.0f;
        double sigma = 0.0f;

        if (weights) {
             // Weighted Stats
             median = weightedMedian(stack, *weights, &rejected);
             auto stats = weightedMeanAndStdDev(stack, *weights, &rejected);
             sigma = stats.second;
        } else {
             // Standard Stats
             std::vector<float> validPixels; 
             validPixels.reserve(n);
             for(int i=0; i<n; ++i) if(rejected[i]==0) validPixels.push_back(stack[i]);
             
             if (validPixels.size() < 3) break;
             
             sigma = Statistics::stdDev(validPixels.data(), validPixels.size(), nullptr);
             median = Statistics::quickMedian(validPixels.data(), validPixels.size());
        }
        
        // Copy stack to wbuffer
        wStack = stack; 
        
        // Iterative winsorization
        double sigma0;
        int wIters = 0;
        do {
            float m0 = median - 1.5f * static_cast<float>(sigma);
            float m1 = median + 1.5f * static_cast<float>(sigma);
            
            // Clip values to [m0, m1]
            // Winsorize ALL pixels based on current center/scale
            for (int i = 0; i < n; ++i) {
                 wStack[i] = std::min(m1, std::max(m0, wStack[i]));
            }
            
            sigma0 = sigma;
            
            if (weights) {
                 // Weighted Sigma of Winsorized Data
                 // Note: We use original pixel weights? Yes.
                 // Treat rejected as invalid? winsorization usually operates on whole set, but here we process iteratively.
                 // Let's assume we treat currently rejected as invalid for stats calc.
                 auto wStats = weightedMeanAndStdDev(wStack, *weights, &rejected);
                 sigma = 1.134 * wStats.second;
            } else {
                std::vector<float> wValid;
                wValid.reserve(n);
                for(int i=0; i<n; ++i) if(rejected[i]==0) wValid.push_back(wStack[i]);
                sigma = 1.134 * Statistics::stdDev(wValid.data(), wValid.size(), nullptr);
            }
            
            wIters++;
            
        } while (std::abs(sigma - sigma0) > sigma0 * 0.0005 && wIters < 10);
        
        // Use final sigma for rejection on original data
        for (int i = 0; i < n; ++i) {
            if (rejected[i] != 0) continue;
            
            if (currentN <= 3) break;
            
            float pixel = stack[i];
            
            if ((median - pixel) > sigma * sigmaLow) {
                rejected[i] = -1;
                result.lowRejected++;
                totalRejected++;
                changed = true;
                currentN--;
            }
            else if ((pixel - median) > sigma * sigmaHigh) {
                rejected[i] = 1;
                result.highRejected++;
                totalRejected++;
                changed = true;
                currentN--;
            }
        }
    }
    
    result.keptCount = currentN;
    return result;
}

//=============================================================================
// LINEAR FIT CLIPPING
//=============================================================================

RejectionResult RejectionAlgorithms::linearFitClipping(
    std::vector<float>& stack,
    float sigmaLow, float sigmaHigh,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    Q_UNUSED(scratch);
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }

    bool changed = true;
    int currentN = n;
    
    while (changed && currentN > 3) {
        changed = false;
        
        // Gather valid points — sort by value (rank-based x)
        std::vector<std::pair<float,int>> validPairs;  // (value, original_index)
        validPairs.reserve(currentN);
        for(int i=0; i<n; ++i) {
            if(rejected[i]==0) validPairs.push_back({stack[i], i});
        }
        std::sort(validPairs.begin(), validPairs.end());
        
        int N = static_cast<int>(validPairs.size());
        // x[j] = j (rank of sorted pixel)
        // Pre-compute means for OLS
        float m_x = (N - 1) * 0.5f;
        float m_y = 0.0f;
        for (int j = 0; j < N; ++j) m_y += validPairs[j].first;
        m_y /= N;
        
        float ssxy = 0.0f, ssxx = 0.0f;
        for (int j = 0; j < N; ++j) {
            float dx = j - m_x;
            ssxy += dx * (validPairs[j].first - m_y);
            ssxx += dx * dx;
        }
        float a = (ssxx > 0.0f) ? ssxy / ssxx : 0.0f;
        float b = m_y - a * m_x;
        
        // Sigma = MAE (Mean Absolute Error)
        float sigma = 0.0f;
        for (int j = 0; j < N; ++j)
            sigma += std::abs(validPairs[j].first - (a * j + b));
        sigma /= N;
        
        // Check each pixel at its rank position j vs predicted a*j + b
        for (int j = 0; j < N; ++j) {
            if (currentN <= 3) break;
            
            int origIdx = validPairs[j].second;
            float deviation = validPairs[j].first - (a * j + b);
            
            if (deviation < -sigmaLow * sigma) {
                rejected[origIdx] = -1;
                result.lowRejected++;
                changed = true;
                currentN--;
            }
            else if (deviation > sigmaHigh * sigma) {
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

//=============================================================================
// GESDT (Generalized Extreme Studentized Deviate Test)
//=============================================================================

RejectionResult RejectionAlgorithms::gesdtClipping(
    std::vector<float>& stack,
    float outliersFraction, float significance,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }
    
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& wStack = scratch ? *scratch : localBuf;
    wStack = stack;

    // Sort for median computation
    Statistics::quickSort(wStack.data(), n);
    float median = wStack[n / 2];
    
    // Maximum number of outliers to test
    int maxOutliers = static_cast<int>(n * outliersFraction);
    if (maxOutliers < 1) maxOutliers = 1;
    
    // Pre-compute critical values
    std::vector<float> criticalValues = computeGesdtCriticalValues(n, maxOutliers, significance);
    
    // Reset wStack
    wStack = stack;
    
    // Store outlier candidates
    std::vector<std::pair<float, int>> outliers;  // (value, original index position tracking)
    outliers.reserve(maxOutliers);
    
    int currentSize = n;
    
    // Simplifying: GESDT is destructive. We track original indices to map back to the 'rejected' array.
    // Optimization: Use swap-remove (O(1)) instead of erase (O(N)) since Grubbs statistic is order-independent.
    // Let's adapt:
    
    // We need to track original indices.
    std::vector<int> indices(n);
    for(int i=0; i<n; ++i) indices[i] = i;
    
    for (int iter = 0; iter < maxOutliers && currentSize > 3; ++iter) {
        float gStat;
        int maxIndexLocal; // Index in wStack
        
        // Compute Grubbs statistic on current wStack
        grubbsStatistic(wStack, currentSize, gStat, maxIndexLocal);
        
        // Check against critical value
        bool isOutlier = checkGValue(gStat, criticalValues[iter]);
        
        if (!isOutlier) {
            // Grubbs statistic does not exceed critical value — stop
            break;
        }
        
        // Store confirmed outlier
        float value = wStack[maxIndexLocal];
        int originalIdx = indices[maxIndexLocal];
        outliers.push_back({value, originalIdx});
        
        // Optimization: Swap with end and decrement size instead of erasing
        // This keeps the loop O(K*N) instead of O(K*N^2)
        if (maxIndexLocal != currentSize - 1) {
            std::swap(wStack[maxIndexLocal], wStack[currentSize - 1]);
            std::swap(indices[maxIndexLocal], indices[currentSize - 1]);
        }
        // "Remove" logically
        currentSize--;
    }
    
    // Confirm outliers
    confirmGesdtOutliers(outliers, static_cast<int>(outliers.size()), median, rejected, result);
    
    result.keptCount = n - result.totalRejected();
    return result;
}

//=============================================================================
// BIWEIGHT ESTIMATOR
//=============================================================================

RejectionResult RejectionAlgorithms::biweightClipping(
    std::vector<float>& stack,
    float tuningConstant,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    // Biweight requires decent sample size
    if (n < 4) {
        result.keptCount = n;
        return result;
    }
    
    // 1. Initial robust location/scale (Median & MAD)
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;
    work = stack; // Copy
    
    float median = Statistics::quickMedian(work.data(), n);
    
    // Compute MAD
    for(int i=0; i<n; ++i) work[i] = std::abs(stack[i] - median);
    float mad = Statistics::quickMedian(work.data(), n);
    
    if (mad < 1e-9f) {
        // All pixels identical?
        result.keptCount = n;
        return result;
    }
    
    // 2. Iterate to refine Location (Center) and Scale
    // Usually 1-2 iterations are enough for convergence on well-behaved data
    double center = median;
    double scale = mad;
    const int MAX_ITERS = 5;
    const double C = (tuningConstant > 0) ? tuningConstant : 6.0;
    
    for(int iter=0; iter<MAX_ITERS; ++iter) {
        // Update Center
        // T_bi = M + [ sum( (x-M)(1-u^2)^2 ) / sum( (1-u^2)^2 ) ]
        // where u = (x-M)/(c*MAD)
        
        bool converged = true;
        
        double num = 0.0;
        double den = 0.0;
        
        for(int i=0; i<n; ++i) {
             double u = (stack[i] - center) / (C * scale);
             if (std::abs(u) < 1.0) {
                 double w = (1.0 - u*u);
                 w = w * w; // (1-u^2)^2
                 num += (stack[i] - center) * w;
                 den += w;
             }
        }
        
        if (den > 1e-9) {
            double shift = num / den;
            center += shift;
            if (std::abs(shift) > 1e-5 * scale) converged = false;
        }
        
        if (converged) break;
    }
    
    // 3. Reject pixels with zero weight (u >= 1)
    
    double limit = C * scale; 
    
    for(int i=0; i<n; ++i) {
        double dist = stack[i] - center;
        if (std::abs(dist) >= limit) {
             // Rejection
             rejected[i] = (dist < 0) ? -1 : 1;
             if (dist < 0) result.lowRejected++;
             else result.highRejected++;
        }
    }
    
    result.keptCount = n - result.totalRejected();
    return result;
}

//=============================================================================
// MODIFIED Z-SCORE
//=============================================================================

RejectionResult RejectionAlgorithms::modifiedZScoreClipping(
    std::vector<float>& stack,
    float threshold,
    std::vector<int>& rejected,
    std::vector<float>* scratch)
{
    RejectionResult result;
    int n = static_cast<int>(stack.size());
    
    if (n < 3) {
        result.keptCount = n;
        return result;
    }
    
    // Median
    std::vector<float> localBuf;
    if (!scratch) localBuf.resize(n);
    std::vector<float>& work = scratch ? *scratch : localBuf;
    work = stack; 
    
    float median = Statistics::quickMedian(work.data(), n);
    
    // MAD
    for(int i=0; i<n; ++i) work[i] = std::abs(stack[i] - median);
    float mad = Statistics::quickMedian(work.data(), n);
    
    if (mad < 1e-9f) {
        // All identical
        result.keptCount = n;
        return result;
    }
    
    // Standard Z-Score factor for consistency with normal distribution
    // M_i = 0.6745 * (deviations) / MAD
    // But usually Modified Z-Score check is:
    // M_i > Threshold (e.g. 3.5)
    
    // Factor 0.6745 makes MAD comparable to Sigma (Sigma = 1.4826 * MAD = MAD / 0.6745)
    // So M_i = (x - med) / (MAD / 0.6745) = (x - med) / Sigma_est
    // So checking M_i > Threshold is same as Sigma Clipping with robust sigma.
    
    float limit = threshold * (mad / 0.6745f);
    
    for (int i = 0; i < n; ++i) {
        float dev = stack[i] - median;
        if (std::abs(dev) > limit) {
             rejected[i] = (dev < 0) ? -1 : 1;
             if (dev < 0) result.lowRejected++;
             else result.highRejected++;
        }
    }
    
    result.keptCount = n - result.totalRejected();
    return result;
}

std::vector<float> RejectionAlgorithms::computeGesdtCriticalValues(
    int n, int maxOutliers, float significance)
{
    std::vector<float> values(maxOutliers);
    
    for (int i = 0; i < maxOutliers; ++i) {
        int ni = n - i;
        double alpha = significance / (2.0 * ni);
        
        double p = 1.0 - alpha;
        if (p > 0.5) p = 1.0 - p;
        
        double t = std::sqrt(-2.0 * std::log(p));
        double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
        double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
        t = t - (c0 + c1*t + c2*t*t) / (1 + d1*t + d2*t*t + d3*t*t*t);
        
        if (alpha < 0.5 && significance < 0.5) t = -t;
        t = std::abs(t);
        
        double numerator = (ni - 1) * t;
        double denom = std::sqrt((ni - 2 + t*t) * ni);
        
        values[i] = static_cast<float>(numerator / denom);
    }
    
    return values;
}

//=============================================================================
// HELPER FUNCTIONS
//=============================================================================

int RejectionAlgorithms::removeNullPixels(
    std::vector<float>& stack,
    const std::vector<float>* drizzleWeights)
{
    int kept = 0;
    int n = static_cast<int>(stack.size());
    
    if (drizzleWeights) {
        for (int i = 0; i < n; ++i) {
            if (stack[i] != 0.0f && (*drizzleWeights)[i] != 0.0f) {
                if (i != kept) {
                    stack[kept] = stack[i];
                }
                kept++;
            }
        }
    } else {
        for (int i = 0; i < n; ++i) {
            if (stack[i] != 0.0f) {
                if (i != kept) {
                    stack[kept] = stack[i];
                }
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
    // NO-OP or deprecated?
    // Modern logic avoids compacting to preserve indices.
    // If called, we compact.
    
    int kept = 0;
    int n = static_cast<int>(stack.size());
    
    for (int i = 0; i < n; ++i) {
        if (rejected[i] == 0) {
            if (i != kept) {
                stack[kept] = stack[i];
            }
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
    
    // Find max deviation in unsorted array
    float maxDev = -1.0f;
    maxIndex = -1;
    
    for(int i=0; i<n; ++i) {
        float dev = std::abs(stack[i] - static_cast<float>(mean));
        if (dev > maxDev) {
            maxDev = dev;
            maxIndex = i;
        }
    }
    
    if (sd > 1e-9)
        gStat = maxDev / static_cast<float>(sd);
    else
        gStat = 0.0f;
}

bool RejectionAlgorithms::checkGValue(float gStat, float criticalValue) {
    return gStat > criticalValue;
}

void RejectionAlgorithms::confirmGesdtOutliers(
    const std::vector<std::pair<float, int>>& outliers,
    int numOutliers, float median,
    std::vector<int>& rejected,
    RejectionResult& result)
{
    // All entries in outliers are confirmed (non-outlier entries are no longer pushed)
    for (int i = 0; i < numOutliers; ++i) {
        float value = outliers[i].first;
        int idx = outliers[i].second;
        
        if (idx < 0 || idx >= static_cast<int>(rejected.size())) continue;
        
        if (value < median) {
            rejected[idx] = -1;
            result.lowRejected++;
        } else {
            rejected[idx] = 1;
            result.highRejected++;
        }
    }
}

//=============================================================================
// WEIGHTED STATISTICS
//=============================================================================

float RejectionAlgorithms::weightedMedian(const std::vector<float>& data, 
                             const std::vector<float>& weights,
                             const std::vector<int>* validMask)
{
    int n = static_cast<int>(data.size());
    if (n == 0 || n != (int)weights.size()) return 0.0f;
    
    // Create pairs of (value, weight)
    struct ValWeight { float v; float w; };
    std::vector<ValWeight> pairs;
    pairs.reserve(n);
    
    double totalWeight = 0.0;
    
    for(int i=0; i<n; ++i) {
        if (!validMask || (*validMask)[i] == 0) {
            float w = weights[i];
            if (w > 0.0f) {
                pairs.push_back({data[i], w});
                totalWeight += w;
            }
        }
    }
    
    if (pairs.empty()) return 0.0f;
    
    // Sort by value
    std::sort(pairs.begin(), pairs.end(), [](const ValWeight& a, const ValWeight& b) {
        return a.v < b.v;
    });
    
    double halfWeight = totalWeight * 0.5;
    double currentWeight = 0.0;
    
    for(const auto& p : pairs) {
        currentWeight += p.w;
        if (currentWeight >= halfWeight) {
            return p.v; 
        }
    }
    
    return pairs.back().v;
}

std::pair<double, double> RejectionAlgorithms::weightedMeanAndStdDev(
                            const std::vector<float>& data,
                            const std::vector<float>& weights,
                            const std::vector<int>* validMask)
{
    int n = static_cast<int>(data.size());
    if (n == 0 || n != (int)weights.size()) return {0.0, 0.0};
    
    double sumW = 0.0;
    double sumValW = 0.0;
    
    // 1. Weighted Mean
    for(int i=0; i<n; ++i) {
        if (!validMask || (*validMask)[i] == 0) {
            float w = weights[i];
            float v = data[i];
            if (w > 0.0f) {
                sumW += w;
                sumValW += (double)v * w;
            }
        }
    }
    
    if (sumW == 0.0) return {0.0, 0.0};
    double mean = sumValW / sumW;
    
    // 2. Weighted Variance
    double sumSqDiffW = 0.0;
    double sumSqW = 0.0; // Sum of squared weights
    int count = 0;
    
    for(int i=0; i<n; ++i) {
        if (!validMask || (*validMask)[i] == 0) {
            float w = weights[i];
            float v = data[i];
            if (w > 0.0f) {
                double diff = v - mean;
                sumSqDiffW += diff * diff * w;
                sumSqW += w * w;
                count++;
            }
        }
    }
    
    if (count <= 1) return {mean, 0.0};
    
    // Weighted Sample Variance correction
    // Denom = SumW - (SumSqW / SumW)
    double denom = sumW - (sumSqW / sumW);
    if (denom <= 0.0) denom = sumW; 
    
    double variance = sumSqDiffW / denom;
    
    return {mean, std::sqrt(variance)};
}

} // namespace Stacking
