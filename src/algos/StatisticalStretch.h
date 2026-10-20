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
// StatisticalStretch.h
//
// Provides statistical analysis and stretch utilities for astronomical image
// processing. Implements robust noise estimation, MTF (Midtone Transfer
// Function) computation, HDR highlight compression, high-range rescaling,
// and curves adjustment. All methods are static and operate on flat
// float32 pixel buffers in the [0, 1] range.
// =============================================================================

#ifndef STATISTICALSTRETCH_H
#define STATISTICALSTRETCH_H

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>


class StatisticalStretch
{
public:

    // -------------------------------------------------------------------------
    // ChannelStats
    //
    // Holds per-channel statistical values computed from a sampled pixel buffer.
    // Used to determine black point and stretch denominator for each channel.
    // -------------------------------------------------------------------------
    struct ChannelStats
    {
        float median      = 0.5f;   // Median pixel value of the channel sample
        float blackpoint  = 0.0f;   // Computed black clipping point
        float denominator = 1.0f;   // (1.0 - blackpoint), used for rescaling
        float minValue    = 0.0f;   // Minimum sampled value
        float noise       = 0.0f;   // Robust noise estimate (sigma equivalent)
    };


    // -------------------------------------------------------------------------
    // robustSigmaLowerHalf
    //
    // Estimates the noise level of a channel using the lower half of the sample
    // distribution. Computes the Median Absolute Deviation (MAD) of values
    // below the median and converts it to a sigma-equivalent using the
    // normal distribution consistency constant (1.4826).
    //
    // Parameters:
    //   data        - Flat interleaved pixel buffer
    //   stride      - Channel stride (typically 1 for per-channel access)
    //   offset      - Starting offset within the buffer (channel index)
    //   channels    - Total number of channels (interleave step)
    //   maxSamples  - Maximum number of samples to draw for performance
    //
    // Returns: Sigma-equivalent noise estimate
    // -------------------------------------------------------------------------
    static float robustSigmaLowerHalf(const std::vector<float>& data,
                                      int stride     = 1,
                                      int offset     = 0,
                                      int channels   = 1,
                                      int maxSamples = 400000);


    // -------------------------------------------------------------------------
    // computeStats
    //
    // Computes the statistical profile of a single channel within an interleaved
    // pixel buffer. Determines the median, minimum value, noise estimate, and
    // black clipping point. The black point is derived from the median and the
    // robust noise estimate scaled by the sigma parameter.
    //
    // Parameters:
    //   data        - Flat interleaved pixel buffer
    //   stride      - Step between successive pixels of the same channel
    //   offset      - Channel offset within each pixel
    //   channels    - Total channel count (interleave factor)
    //   sigma       - Sigma multiplier for black point computation
    //   noBlackClip - If true, uses the minimum value as the black point
    //                 instead of the sigma-clipped estimate
    //
    // Returns: Populated ChannelStats struct
    // -------------------------------------------------------------------------
    static ChannelStats computeStats(const std::vector<float>& data,
                                     int   stride,
                                     int   offset,
                                     int   channels,
                                     float sigma,
                                     bool  noBlackClip);


    // -------------------------------------------------------------------------
    // mtf (inline)
    //
    // Applies the Midtone Transfer Function to a single value x with midtone
    // parameter m. The MTF is a rational curve commonly used in astrophotography
    // for non-linear stretching. Defined as:
    //
    //   MTF(x, m) = (m - 1) * x / ((2m - 1) * x - m)
    //
    // Returns 0 if the denominator is below the numerical stability threshold.
    // -------------------------------------------------------------------------
    static inline float mtf(float x, float m)
    {
        const float term1 = (m - 1.0f) * x;
        const float term2 = (2.0f * m - 1.0f) * x - m;

        if (std::abs(term2) < 1e-12f)
            return 0.0f;

        return term1 / term2;
    }


    // -------------------------------------------------------------------------
    // stretchFormula (inline)
    //
    // Applies the statistical stretch rational formula to a single pixel value.
    // The formula maps a pre-rescaled value x using the rescaled median and
    // target median. Inputs are clamped and the result is protected against
    // division by zero and non-finite output.
    //
    // Parameters:
    //   x             - Input pixel value (>= 0)
    //   medRescaled   - Median of the channel after black point rescaling
    //   targetMedian  - Desired output median for the stretched result
    //
    // Returns: Stretched pixel value clamped to [0, 1]
    // -------------------------------------------------------------------------
    static inline float stretchFormula(float x, float medRescaled, float targetMedian)
    {
        x           = std::max(0.0f, x);
        medRescaled = std::clamp(medRescaled, 1e-6f, 1.0f - 1e-6f);

        const float num = (medRescaled - 1.0f) * targetMedian * x;
        const float den = medRescaled * (targetMedian + x - 1.0f) - targetMedian * x;

        if (std::abs(den) < 1e-6f)
            return std::clamp(x, 0.0f, 1.0f);

        const float result = num / den;

        if (!std::isfinite(result))
            return std::clamp(x, 0.0f, 1.0f);

        return std::clamp(result, 0.0f, 1.0f);
    }


    // -------------------------------------------------------------------------
    // computeMTFParameter
    //
    // Solves for the MTF midtone parameter m such that MTF(currentMedian, m)
    // maps to targetMedian. Used to determine the correct MTF curve to apply
    // after black point rescaling.
    //
    // Parameters:
    //   currentMedian - The median of the rescaled channel
    //   targetMedian  - The desired output median
    //
    // Returns: MTF midtone parameter m, clamped to (1e-6, 1 - 1e-6)
    // -------------------------------------------------------------------------
    static float computeMTFParameter(float currentMedian, float targetMedian);


    // -------------------------------------------------------------------------
    // hdrCompressHighlights
    //
    // Applies a cubic Hermite highlight compression curve to all values above
    // the specified knee threshold. Values below the knee are left unchanged.
    // The compression strength is controlled by the amount parameter, which
    // determines the slope of the curve at the upper end.
    //
    // Operates on mono or multi-channel flat buffers (per-element).
    //
    // Parameters:
    //   data   - Flat pixel buffer (modified in place)
    //   amount - Compression strength in [0, 1]; 0 = identity, 1 = maximum
    //   knee   - Threshold above which compression begins, in [0, 0.99]
    // -------------------------------------------------------------------------
    static void hdrCompressHighlights(std::vector<float>& data,
                                      float amount,
                                      float knee);


    // -------------------------------------------------------------------------
    // hdrCompressColorLuminance
    //
    // Applies HDR highlight compression to an RGB image using luminance-guided
    // scaling. For each pixel whose luminance exceeds the knee threshold, the
    // luminance is compressed using the cubic Hermite curve and all three
    // channels are scaled proportionally to preserve color ratios.
    //
    // Parameters:
    //   data     - Flat interleaved RGB pixel buffer (modified in place)
    //   width    - Image width in pixels
    //   height   - Image height in pixels
    //   amount   - Compression strength in [0, 1]
    //   knee     - Luminance threshold above which compression begins
    //   lumaMode - Luminance weighting standard: 0 = Rec.709, 1 = Rec.601,
    //              2 = Rec.2020
    // -------------------------------------------------------------------------
    static void hdrCompressColorLuminance(std::vector<float>& data,
                                          int   width,
                                          int   height,
                                          float amount,
                                          float knee,
                                          int   lumaMode);


    // -------------------------------------------------------------------------
    // highRangeRescale
    //
    // Performs a full high-dynamic-range rescale pipeline on a pixel buffer:
    //   1. Computes a robust floor value from the median and noise estimate.
    //   2. Determines soft and hard ceiling percentiles from the luminance.
    //   3. Scales the data to map [floor, softCeil] to [pedestal, ~0.98].
    //   4. Optionally applies an MTF correction to reach the target median.
    //   5. Applies a final soft-clip pass to the highlights.
    //
    // Parameters:
    //   data               - Flat interleaved pixel buffer (modified in place)
    //   width              - Image width in pixels
    //   height             - Image height in pixels
    //   channels           - Channel count (1 = mono, 3 = RGB)
    //   targetMedian       - Desired output median; 0 to skip MTF correction
    //   pedestal           - Minimum output floor value, clamped to [0, 0.05]
    //   softCeilPct        - Percentile for the soft ceiling (e.g., 99.0)
    //   hardCeilPct        - Percentile for the hard ceiling (e.g., 99.9)
    //   floorSigma         - Sigma multiplier for the floor noise computation
    //   softclipThreshold  - Knee value for the final soft-clip pass
    // -------------------------------------------------------------------------
    static void highRangeRescale(std::vector<float>& data,
                                 int   width,
                                 int   height,
                                 int   channels,
                                 float targetMedian,
                                 float pedestal,
                                 float softCeilPct,
                                 float hardCeilPct,
                                 float floorSigma,
                                 float softclipThreshold);


    // -------------------------------------------------------------------------
    // applyCurvesAdjustment
    //
    // Applies a piecewise linear curves boost to the pixel buffer, computed
    // from the target median and a curves boost factor. The curve lifts
    // midtones and highlights above the median to increase contrast and
    // perceived brightness in the upper tonal range.
    //
    // Parameters:
    //   data         - Flat pixel buffer (modified in place)
    //   targetMedian - Median value around which the curve is anchored
    //   curvesBoost  - Boost strength in [0, 1]; 0 = identity
    // -------------------------------------------------------------------------
    static void applyCurvesAdjustment(std::vector<float>& data,
                                      float targetMedian,
                                      float curvesBoost);


    // -------------------------------------------------------------------------
    // computeLuminance (inline)
    //
    // Computes the perceptual luminance of an RGB triplet using the specified
    // color standard weighting coefficients.
    //
    // Parameters:
    //   r, g, b  - Linear RGB channel values
    //   mode     - 0 = Rec.709 (default), 1 = Rec.601, 2 = Rec.2020
    //
    // Returns: Scalar luminance value
    // -------------------------------------------------------------------------
    static inline float computeLuminance(float r, float g, float b, int mode = 0)
    {
        switch (mode)
        {
            case 1:  return 0.2990f * r + 0.5870f * g + 0.1140f * b;  // Rec.601
            case 2:  return 0.2627f * r + 0.6780f * g + 0.0593f * b;  // Rec.2020
            default: return 0.2126f * r + 0.7152f * g + 0.0722f * b;  // Rec.709
        }
    }


    // -------------------------------------------------------------------------
    // getLumaWeights
    //
    // Returns the RGB luminance weighting coefficients for the specified
    // color standard as a three-element array {wR, wG, wB}.
    //
    // Parameters:
    //   mode - 0 = Rec.709, 1 = Rec.601, 2 = Rec.2020
    // -------------------------------------------------------------------------
    static std::array<float, 3> getLumaWeights(int mode);
};


#endif // STATISTICALSTRETCH_H