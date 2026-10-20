/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

// =============================================================================
// StatisticalStretch.cpp
//
// Implementation of the StatisticalStretch utility class. Provides robust
// statistical analysis (median, MAD-based noise estimation), MTF parameter
// solving, HDR highlight compression via cubic Hermite curves, high-range
// rescaling with percentile ceilings, and piecewise linear curves adjustment.
//
// All pixel buffers are assumed to contain float32 values in [0, 1] stored
// in interleaved channel order.
// =============================================================================

#include "StatisticalStretch.h"

#include <algorithm>
#include <numeric>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif


// =============================================================================
// Robust Statistics
// =============================================================================

float StatisticalStretch::robustSigmaLowerHalf(const std::vector<float>& data,
                                               int stride,
                                               int offset,
                                               int channels,
                                               int maxSamples)
{
    const size_t limit      = data.size();
    const int    totalStride = stride * channels;
    const size_t estimatedSize = limit / totalStride + 1;

    // Adaptively increase the stride if the dataset exceeds the sample limit.
    int actualStride = totalStride;
    if (estimatedSize > static_cast<size_t>(maxSamples))
    {
        actualStride = static_cast<int>(limit / maxSamples);
        actualStride = std::max(actualStride, totalStride);
    }

    std::vector<float> sample;
    sample.reserve(std::min(estimatedSize, static_cast<size_t>(maxSamples)));

    for (size_t i = offset; i < limit; i += actualStride)
        sample.push_back(data[i]);

    if (sample.size() < 16)
        return 0.0f;

    // Compute the median of the sample.
    const size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    const float median = sample[mid];

    // Collect values from the lower half of the distribution (below the median).
    std::vector<float> lowerHalf;
    lowerHalf.reserve(sample.size() / 2 + 1);
    for (float v : sample)
    {
        if (v <= median)
            lowerHalf.push_back(v);
    }

    float mad = 0.0f;

    if (lowerHalf.size() < 16)
    {
        // Insufficient lower-half samples: fall back to full-sample MAD.
        for (size_t i = 0; i < sample.size(); ++i)
            sample[i] = std::abs(sample[i] - median);

        const size_t madMid = sample.size() / 2;
        std::nth_element(sample.begin(), sample.begin() + madMid, sample.end());
        mad = sample[madMid];
    }
    else
    {
        // Compute the median of the lower half, then its MAD.
        const size_t loMid = lowerHalf.size() / 2;
        std::nth_element(lowerHalf.begin(), lowerHalf.begin() + loMid, lowerHalf.end());
        const float medLower = lowerHalf[loMid];

        for (size_t i = 0; i < lowerHalf.size(); ++i)
            lowerHalf[i] = std::abs(lowerHalf[i] - medLower);

        std::nth_element(lowerHalf.begin(), lowerHalf.begin() + loMid, lowerHalf.end());
        mad = lowerHalf[loMid];
    }

    // Scale MAD to a sigma-equivalent using the normal distribution constant.
    return 1.4826f * mad;
}


StatisticalStretch::ChannelStats StatisticalStretch::computeStats(
    const std::vector<float>& data,
    int   stride,
    int   offset,
    int   channels,
    float sigma,
    bool  noBlackClip)
{
    ChannelStats stats;

    const size_t limit       = data.size();
    const int    totalStride = stride * channels;

    float minVal = 1e30f;

    std::vector<float> sample;
    sample.reserve(limit / totalStride + 100);

    for (size_t i = offset; i < limit; i += totalStride)
    {
        const float v = data[i];
        sample.push_back(v);
        if (v < minVal)
            minVal = v;
    }

    if (sample.empty())
        return stats;

    // Compute the channel median via nth_element (O(N), no full sort needed).
    const size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    const float median = sample[mid];

    stats.median   = median;
    stats.minValue = minVal;

    if (noBlackClip)
    {
        // When black clipping is disabled, use the data minimum as the floor.
        stats.blackpoint = minVal;
    }
    else
    {
        // Derive the black point from the robust noise estimate:
        //   blackpoint = median - sigma * noise, clamped to [minVal, 0.99]
        const float noise = robustSigmaLowerHalf(data, stride, offset, channels);
        stats.noise = noise;

        float bp = median - sigma * noise;
        bp = std::max(minVal, bp);
        bp = std::min(bp, 0.99f);
        stats.blackpoint = bp;
    }

    stats.denominator = std::max(1.0f - stats.blackpoint, 1e-12f);

    return stats;
}


// =============================================================================
// MTF Parameter Solver
// =============================================================================

float StatisticalStretch::computeMTFParameter(float currentMedian, float targetMedian)
{
    // Solve for m such that MTF(currentMedian, m) = targetMedian.
    // Derived by inverting the rational MTF formula.
    const float cb = std::clamp(currentMedian, 1e-6f, 1.0f - 1e-6f);
    const float tb = std::clamp(targetMedian,  1e-6f, 1.0f - 1e-6f);

    float den = cb * (2.0f * tb - 1.0f) - tb;
    if (std::abs(den) < 1e-12f)
        den = 1e-12f;

    const float m = (cb * (tb - 1.0f)) / den;
    return std::clamp(m, 1e-6f, 1.0f - 1e-6f);
}


// =============================================================================
// HDR Highlight Compression
// =============================================================================

void StatisticalStretch::hdrCompressHighlights(std::vector<float>& data,
                                               float amount,
                                               float knee)
{
    if (amount <= 0.0f)
        return;

    const float a  = std::clamp(amount, 0.0f, 1.0f);
    const float k  = std::clamp(knee,   0.0f, 0.99f);

    // Determine the end slope of the Hermite curve.
    // a = 0 -> m1 = 1 (identity slope), a = 1 -> m1 = 5 (strong compression).
    const float m1 = std::clamp(1.0f + 4.0f * a, 1.0f, 5.0f);

    const size_t total = data.size();

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(total); ++i)
    {
        float y = data[i];

        if (y > k)
        {
            // Normalize the above-knee portion to t in [0, 1].
            float t = std::clamp((y - k) / (1.0f - k), 0.0f, 1.0f);

            const float t2 = t * t;
            const float t3 = t2 * t;

            // Cubic Hermite basis: p0 = 0, p1 = 1, m0 = 1 (unit slope at knee),
            // m1 = controlled end slope for compression.
            const float h10 = t3 - 2.0f * t2 + t;        // Tangent at p0 coefficient
            const float h01 = -2.0f * t3 + 3.0f * t2;    // p1 coefficient
            const float h11 = t3 - t2;                    // Tangent at p1 coefficient

            float f = h10 * 1.0f + h01 * 1.0f + h11 * m1;
            f = std::clamp(f, 0.0f, 1.0f);

            data[i] = k + (1.0f - k) * f;
        }

        data[i] = std::clamp(data[i], 0.0f, 1.0f);
    }
}


void StatisticalStretch::hdrCompressColorLuminance(std::vector<float>& data,
                                                   int   width,
                                                   int   height,
                                                   float amount,
                                                   float knee,
                                                   int   lumaMode)
{
    if (amount <= 0.0f)
        return;

    const auto weights     = getLumaWeights(lumaMode);
    const long pixelCount  = static_cast<long>(width) * height;
    const float a          = std::clamp(amount, 0.0f, 1.0f);
    const float m1         = 1.0f + 4.0f * a;

    #pragma omp parallel for
    for (long i = 0; i < pixelCount; ++i)
    {
        const size_t idx = i * 3;
        const float  r   = data[idx];
        const float  g   = data[idx + 1];
        const float  b   = data[idx + 2];

        const float L = weights[0] * r + weights[1] * g + weights[2] * b;

        if (L > knee && L > 1e-10f)
        {
            // Compress the luminance above the knee using the Hermite curve.
            float t = std::clamp((L - knee) / (1.0f - knee), 0.0f, 1.0f);

            const float t2  = t * t;
            const float t3  = t2 * t;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            float f = std::clamp(h10 * 1.0f + h01 * 1.0f + h11 * m1, 0.0f, 1.0f);

            const float Lc    = knee + (1.0f - knee) * f;
            const float scale = Lc / L;

            // Scale all three channels by the luminance compression ratio
            // to preserve the original hue and saturation.
            data[idx]     = std::clamp(r * scale, 0.0f, 1.0f);
            data[idx + 1] = std::clamp(g * scale, 0.0f, 1.0f);
            data[idx + 2] = std::clamp(b * scale, 0.0f, 1.0f);
        }
    }
}


// =============================================================================
// High-Range Rescaling
// =============================================================================

void StatisticalStretch::highRangeRescale(std::vector<float>& data,
                                          int   width,
                                          int   height,
                                          int   channels,
                                          float targetMedian,
                                          float pedestal,
                                          float softCeilPct,
                                          float hardCeilPct,
                                          float floorSigma,
                                          float softclipThreshold)
{
    const long pixelCount = static_cast<long>(width) * height;

    // -------------------------------------------------------------------------
    // Step 1: Compute per-pixel luminance for statistical analysis.
    // -------------------------------------------------------------------------
    std::vector<float> luminance(pixelCount);

    if (channels == 1)
    {
        for (long i = 0; i < pixelCount; ++i)
            luminance[i] = data[i];
    }
    else
    {
        // Use Rec.709 weights for luminance extraction.
        const auto weights = getLumaWeights(0);
        for (long i = 0; i < pixelCount; ++i)
        {
            const size_t idx = i * channels;
            luminance[i] = weights[0] * data[idx]
                         + weights[1] * data[idx + 1]
                         + weights[2] * data[idx + 2];
        }
    }

    // -------------------------------------------------------------------------
    // Step 2: Determine the robust floor from median and noise estimate.
    // -------------------------------------------------------------------------
    std::vector<float> sample(luminance);
    const size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    const float median = sample[mid];

    const float noise       = robustSigmaLowerHalf(luminance, 1, 0, 1);
    const float minVal      = *std::min_element(luminance.begin(), luminance.end());
    const float globalFloor = std::max(median - floorSigma * noise, minVal);

    // -------------------------------------------------------------------------
    // Step 3: Compute soft and hard ceiling percentiles from a subsample.
    // -------------------------------------------------------------------------
    const int stride = std::max(1, static_cast<int>(luminance.size()) / 500000);

    std::vector<float> samplePerc;
    for (size_t i = 0; i < luminance.size(); i += stride)
        samplePerc.push_back(luminance[i]);

    std::sort(samplePerc.begin(), samplePerc.end());

    size_t softIdx = std::min(
        static_cast<size_t>(softCeilPct / 100.0f * samplePerc.size()),
        samplePerc.size() - 1);
    size_t hardIdx = std::min(
        static_cast<size_t>(hardCeilPct / 100.0f * samplePerc.size()),
        samplePerc.size() - 1);

    float softCeil = samplePerc[softIdx];
    float hardCeil = samplePerc[hardIdx];

    // Ensure the ceilings are strictly ordered above the floor.
    if (softCeil <= globalFloor) softCeil = globalFloor + 1e-6f;
    if (hardCeil <= softCeil)    hardCeil = softCeil    + 1e-6f;

    const float ped = std::clamp(pedestal, 0.0f, 0.05f);

    // -------------------------------------------------------------------------
    // Step 4: Compute the linear scale factor and apply the rescaling.
    //
    // Two candidate scales are considered:
    //   scaleContrast - maps softCeil to ~0.98 (preserves contrast headroom)
    //   scaleSafety   - maps hardCeil to 1.0 (prevents hard clipping)
    // The smaller of the two is used to satisfy both constraints.
    // -------------------------------------------------------------------------
    const float scaleContrast = (0.98f - ped) / (softCeil - globalFloor + 1e-12f);
    const float scaleSafety   = (1.0f  - ped) / (hardCeil - globalFloor + 1e-12f);
    const float s             = std::min(scaleContrast, scaleSafety);

    const size_t total = data.size();

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(total); ++i)
        data[i] = std::clamp((data[i] - globalFloor) * s + ped, 0.0f, 1.0f);

    // -------------------------------------------------------------------------
    // Step 5: Optionally apply MTF correction to reach the target median.
    // -------------------------------------------------------------------------
    if (targetMedian > 0.0f && targetMedian < 1.0f)
    {
        // Recompute the current median from a subsample of the rescaled data.
        std::vector<float> afterSample;
        for (size_t i = 0; i < data.size(); i += stride * channels)
            afterSample.push_back(data[i]);

        const size_t afterMid = afterSample.size() / 2;
        std::nth_element(afterSample.begin(), afterSample.begin() + afterMid, afterSample.end());
        const float currentMed = afterSample[afterMid];

        if (currentMed > 0.0f && currentMed < 1.0f
            && std::abs(currentMed - targetMedian) > 1e-3f)
        {
            const float m = computeMTFParameter(currentMed, targetMedian);

            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(total); ++i)
                data[i] = std::clamp(mtf(data[i], m), 0.0f, 1.0f);
        }
    }

    // -------------------------------------------------------------------------
    // Step 6: Apply a final soft-clip pass to roll off remaining highlights.
    // -------------------------------------------------------------------------
    hdrCompressHighlights(data, 1.0f, softclipThreshold);
}


// =============================================================================
// Curves Adjustment
// =============================================================================

void StatisticalStretch::applyCurvesAdjustment(std::vector<float>& data,
                                               float targetMedian,
                                               float curvesBoost)
{
    if (curvesBoost <= 0.0f)
        return;

    const float tm = std::clamp(targetMedian, 0.01f, 0.99f);
    const float cb = std::clamp(curvesBoost,  0.0f,  1.0f);

    // Construct a six-point piecewise linear curve anchored at the target median.
    // Points above the median are lifted by a power function to boost highlights.
    const float p3x = 0.25f * (1.0f - tm) + tm;
    const float p4x = 0.75f * (1.0f - tm) + tm;
    const float p3y = std::pow(p3x, 1.0f - cb);
    const float p4y = std::pow(std::pow(p4x, 1.0f - cb), 1.0f - cb);

    const std::vector<float> cx = { 0.0f, 0.5f * tm, tm, p3x, p4x, 1.0f };
    const std::vector<float> cy = { 0.0f, 0.5f * tm, tm, p3y, p4y, 1.0f };

    const size_t total = data.size();

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(total); ++i)
    {
        const float x = std::clamp(data[i], 0.0f, 1.0f);

        // Piecewise linear interpolation between the curve control points.
        for (size_t j = 0; j < cx.size() - 1; ++j)
        {
            if (x >= cx[j] && x <= cx[j + 1])
            {
                const float t = (x - cx[j]) / (cx[j + 1] - cx[j] + 1e-12f);
                data[i] = cy[j] + t * (cy[j + 1] - cy[j]);
                break;
            }
        }

        data[i] = std::clamp(data[i], 0.0f, 1.0f);
    }
}


// =============================================================================
// Utility
// =============================================================================

std::array<float, 3> StatisticalStretch::getLumaWeights(int mode)
{
    switch (mode)
    {
        case 1:  return { 0.2990f, 0.5870f, 0.1140f };  // Rec.601
        case 2:  return { 0.2627f, 0.6780f, 0.0593f };  // Rec.2020
        default: return { 0.2126f, 0.7152f, 0.0722f };  // Rec.709
    }
}