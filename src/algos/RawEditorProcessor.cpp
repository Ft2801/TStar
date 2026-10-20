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
 * This file is inspired by or incorporates code/design from RapidRAW
 * (hosted at https://github.com/CyberTimon/RapidRAW), which is
 * licensed under AGPL-3.0.
 * Inspired by the work of Timon Käch.
 * Modified by Ft2801 in 2026.
 * -------------------------------------------------------------------
 */

#include "RawEditorProcessor.h"
#include "../ImageBuffer.h"

#include <cmath>
#include <algorithm>
#include <numeric>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace RawEditor {

// ============================================================================
// Constants
// ============================================================================

// Rec. 709 luminance coefficients used throughout the pipeline.
static constexpr float LUMA_R = 0.2126f;
static constexpr float LUMA_G = 0.7152f;
static constexpr float LUMA_B = 0.0722f;

// ============================================================================
// Math and Colour Space Utilities
// ============================================================================

float Processor::getLuma(float r, float g, float b) {
    return r * LUMA_R + g * LUMA_G + b * LUMA_B;
}

// Smooth Hermite interpolation (equivalent to GLSL smoothstep).
float Processor::smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Convert linear RGB to HSV. Hue is in [0, 360), saturation and value in [0, 1].
void Processor::rgbToHsv(float r, float g, float b,
                          float& h, float& s, float& v)
{
    float cMax  = std::max({r, g, b});
    float cMin  = std::min({r, g, b});
    float delta = cMax - cMin;

    h = 0.0f;
    if (delta > 0.0f) {
        if      (cMax == r) h = 60.0f * std::fmod((g - b) / delta, 6.0f);
        else if (cMax == g) h = 60.0f * (((b - r) / delta) + 2.0f);
        else                h = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;

    s = (cMax > 0.0f) ? (delta / cMax) : 0.0f;
    v = cMax;
}

// Convert HSV back to linear RGB. Hue is in [0, 360).
void Processor::hsvToRgb(float h, float s, float v,
                          float& r, float& g, float& b)
{
    float C  = v * s;
    float X  = C * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m  = v - C;
    float rp, gp, bp;

    if      (h < 60.0f)  { rp = C; gp = X; bp = 0; }
    else if (h < 120.0f) { rp = X; gp = C; bp = 0; }
    else if (h < 180.0f) { rp = 0; gp = C; bp = X; }
    else if (h < 240.0f) { rp = 0; gp = X; bp = C; }
    else if (h < 300.0f) { rp = X; gp = 0; bp = C; }
    else                  { rp = C; gp = 0; bp = X; }

    r = rp + m;
    g = gp + m;
    b = bp + m;
}

// Deterministic hash noise based on a well-known GLSL hash pattern.
float Processor::hashNoise(float x, float y) {
    float p3x = std::fmod(std::fabs(x * 0.1031f), 1.0f);
    float p3y = std::fmod(std::fabs(y * 0.1031f), 1.0f);
    float p3z = std::fmod(std::fabs(x * 0.1031f), 1.0f);  // .xyx swizzle

    float dot = p3x * (p3y + 33.33f)
              + p3y * (p3z + 33.33f)
              + p3z * (p3x + 33.33f);

    p3x = std::fmod(std::fabs(p3x + dot), 1.0f);
    p3y = std::fmod(std::fabs(p3y + dot), 1.0f);
    p3z = std::fmod(std::fabs(p3z + dot), 1.0f);

    return std::fmod(std::fabs((p3x + p3y) * p3z), 1.0f);
}

// Quintic-interpolated gradient noise for smooth, tileable film grain.
float Processor::gradientNoise(float px, float py) {
    float ix = std::floor(px);
    float iy = std::floor(py);
    float fx = px - ix;
    float fy = py - iy;

    // Quintic smoothing kernel: 6t^5 - 15t^4 + 10t^3
    float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

    auto h = [](float ax, float ay) -> float {
        return std::fmod(std::fabs(std::sin(ax * 12.9898f + ay * 78.233f) * 43758.5453f), 1.0f);
    };

    float n00 = h(ix,     iy);
    float n10 = h(ix + 1, iy);
    float n01 = h(ix,     iy + 1);
    float n11 = h(ix + 1, iy + 1);

    float nx0 = n00 + ux * (n10 - n00);
    float nx1 = n01 + ux * (n11 - n01);

    return (nx0 + uy * (nx1 - nx0)) * 2.0f - 1.0f;
}

// ============================================================================
// Params Utilities
// ============================================================================

// Returns true only when every parameter is at its default (no-op) value.
bool Params::isIdentity() const {
    if (exposure != 0.0f  || brightness != 0.0f || contrast != 0.0f)   return false;
    if (highlights != 0.0f || shadows != 0.0f || whites != 0.0f || blacks != 0.0f) return false;
    if (temperature != 0.0f || tint != 0.0f || saturation != 0.0f || vibrance != 0.0f) return false;
    if (sharpness != 0.0f || clarity != 0.0f || structure != 0.0f || dehaze != 0.0f) return false;
    if (caRedCyan != 0.0f || caBlueYellow != 0.0f) return false;
    if (redHue != 0.0f || greenHue != 0.0f || blueHue != 0.0f) return false;
    if (redSat != 0.0f || greenSat != 0.0f || blueSat != 0.0f) return false;
    if (shadowsTint != 0.0f) return false;
    if (vignetteAmount != 0.0f || grainAmount != 0.0f) return false;

    for (const auto& h : hsl) {
        if (h.hue != 0.0f || h.saturation != 0.0f || h.luminance != 0.0f) return false;
    }

    if (colorGradingShadows.saturation    != 0.0f || colorGradingShadows.luminance    != 0.0f) return false;
    if (colorGradingMidtones.saturation   != 0.0f || colorGradingMidtones.luminance   != 0.0f) return false;
    if (colorGradingHighlights.saturation != 0.0f || colorGradingHighlights.luminance != 0.0f) return false;

    if (!Processor::isDefaultCurve(lumaCurve))  return false;
    if (!Processor::isDefaultCurve(redCurve))   return false;
    if (!Processor::isDefaultCurve(greenCurve)) return false;
    if (!Processor::isDefaultCurve(blueCurve))  return false;

    return true;
}

// A curve is the identity when it contains exactly two points at (0,0) and (255,255).
bool Processor::isDefaultCurve(const std::vector<CurvePoint>& points) {
    if (points.size() != 2) return false;
    return (std::fabs(points[0].x)          < 0.1f
         && std::fabs(points[0].y)          < 0.1f
         && std::fabs(points[1].x - 255.0f) < 0.1f
         && std::fabs(points[1].y - 255.0f) < 0.1f);
}

// ============================================================================
// Blur Helper
// ============================================================================

// Compute a Gaussian-approximated luminance map using three successive passes
// of a separable box blur. Three box passes converge rapidly to a Gaussian
// response and are O(N) per pass, making them suitable for large images.
std::vector<float> Processor::computeBlurredLuma(const std::vector<float>& data,
                                                   int w, int h, int channels,
                                                   int radius)
{
    // Extract luminance from each pixel.
    std::vector<float> luma(static_cast<size_t>(w) * h);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < w * h; ++i) {
        size_t idx = static_cast<size_t>(i) * channels;
        luma[i] = (channels >= 3)
                  ? getLuma(data[idx], data[idx + 1], data[idx + 2])
                  : data[idx];
    }

    std::vector<float> temp(luma.size());

    // Horizontal box blur pass: sliding-window sum, O(N) per row.
    auto boxBlurH = [&](const std::vector<float>& src, std::vector<float>& dst) {
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; ++y) {
            float sum   = 0.0f;
            int   count = 0;

            // Initialise the window covering [0, radius].
            for (int x = 0; x <= radius && x < w; ++x) {
                sum += src[y * w + x];
                count++;
            }

            for (int x = 0; x < w; ++x) {
                dst[y * w + x] = sum / count;

                // Advance window: add leading edge, remove trailing edge.
                int right = x + radius + 1;
                if (right < w) { sum += src[y * w + right]; count++; }

                int left = x - radius;
                if (left >= 0) { sum -= src[y * w + left]; count--; }
            }
        }
    };

    // Vertical box blur pass: sliding-window sum, O(N) per column.
    auto boxBlurV = [&](const std::vector<float>& src, std::vector<float>& dst) {
        #pragma omp parallel for schedule(static)
        for (int x = 0; x < w; ++x) {
            float sum   = 0.0f;
            int   count = 0;

            for (int y = 0; y <= radius && y < h; ++y) {
                sum += src[y * w + x];
                count++;
            }

            for (int y = 0; y < h; ++y) {
                dst[y * w + x] = sum / count;

                int bottom = y + radius + 1;
                if (bottom < h) { sum += src[bottom * w + x]; count++; }

                int top = y - radius;
                if (top >= 0) { sum -= src[top * w + x]; count--; }
            }
        }
    };

    // Three H+V passes approximate a Gaussian kernel.
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurH(luma, temp);
        boxBlurV(temp, luma);
    }

    return luma;
}

// ============================================================================
// Individual Adjustment Functions
// ============================================================================

// Exposure: multiply all channels by 2^exposure (linear EV shift).
void Processor::applyLinearExposure(float& r, float& g, float& b, float exposure) {
    if (exposure == 0.0f) return;
    float mult = std::pow(2.0f, exposure);
    r *= mult; g *= mult; b *= mult;
}

// Brightness: filmic rational-curve midtone adjustment.
// Combines a direct power-of-two scale with a rational sigmoid to avoid
// the clipping artefacts produced by a simple exposure multiplication.
void Processor::applyFilmicExposure(float& r, float& g, float& b, float brightness) {
    if (brightness == 0.0f) return;

    constexpr float RATIONAL_CURVE_MIX = 0.95f;
    constexpr float MIDTONE_STRENGTH   = 1.2f;

    float origLuma = getLuma(r, g, b);
    if (std::fabs(origLuma) < 0.00001f) return;

    float directAdj   = brightness * (1.0f - RATIONAL_CURVE_MIX);
    float rationalAdj = brightness * RATIONAL_CURVE_MIX;

    float scale = std::pow(2.0f, directAdj);
    float k     = std::pow(2.0f, -rationalAdj * MIDTONE_STRENGTH);

    // Apply rational curve in the fractional part of luminance only.
    float lumaAbs      = std::fabs(origLuma);
    float lumaFloor    = std::floor(lumaAbs);
    float lumaFract    = lumaAbs - lumaFloor;
    float shapedFract  = lumaFract / (lumaFract + (1.0f - lumaFract) * k);
    float shapedLuma   = lumaFloor + shapedFract;

    float sign    = (origLuma >= 0.0f) ? 1.0f : -1.0f;
    float newLuma = sign * shapedLuma * scale;

    float totalLumaScale = newLuma / origLuma;
    float chromaScale    = std::pow(std::fabs(totalLumaScale), 0.8f);
    if (totalLumaScale < 0.0f) chromaScale = -chromaScale;

    r = newLuma + (r - origLuma) * chromaScale;
    g = newLuma + (g - origLuma) * chromaScale;
    b = newLuma + (b - origLuma) * chromaScale;
}

// -------------------------------------------------------------------------
// getShadowMult (internal helper)
// Computes a multiplicative factor for shadow lifting or crushing.
// The influence falls off smoothly above a luminance threshold.
// -------------------------------------------------------------------------
static float getShadowMult(float luma, float sh) {
    if (sh == 0.0f) return 1.0f;

    float safeLuma = std::max(luma, 0.0001f);
    constexpr float limit = 0.38f;

    if (safeLuma < limit) {
        float x      = safeLuma / limit;
        float mask   = std::pow(1.0f - x, 1.35f);
        float factor = std::clamp(std::pow(2.0f, sh * 1.25f), 0.2f, 4.5f);
        return (1.0f - mask) + mask * factor;
    }
    return 1.0f;
}

// Tonal adjustments: whites, blacks, shadows and contrast.
// A pre-computed spatially blurred luminance (blurredLuma) is used for
// shadow recovery to suppress halo artefacts around bright edges.
void Processor::applyTonalAdjustments(float& r, float& g, float& b,
                                       float blurredLuma,
                                       float contrast, float shadows,
                                       float whites,   float blacks)
{
    // --- White point ---
    // Shifts the white point: positive values compress highlights.
    if (whites != 0.0f) {
        float whiteLevel = 1.0f - whites * 0.25f;
        float wMult      = 1.0f / std::max(whiteLevel, 0.01f);
        r *= wMult; g *= wMult; b *= wMult;
        blurredLuma *= wMult;
    }

    // --- Black point ---
    // Positive values clip the shadows; negative values lift the floor.
    if (blacks != 0.0f) {
        float blackPointShift = -blacks * 0.12f;

        auto applyBlackPoint = [blackPointShift](float val) {
            if (blackPointShift > 0.0f) {
                // Compress: move blacks upward.
                return std::max(0.0f, (val - blackPointShift) / (1.0f - blackPointShift));
            } else {
                // Lift: offset the floor downward.
                return val * (1.0f + blackPointShift) - blackPointShift;
            }
        };

        r = applyBlackPoint(r);
        g = applyBlackPoint(g);
        b = applyBlackPoint(b);
        blurredLuma = applyBlackPoint(blurredLuma);
    }

    float pixelLuma      = getLuma(std::max(r, 0.0f), std::max(g, 0.0f), std::max(b, 0.0f));
    float safePixelLuma  = std::max(pixelLuma,  0.0001f);
    float safeBlurredLuma = std::max(blurredLuma, 0.0001f);

    // Halo protection mask: suppress shadow boost at edges between light and
    // dark regions by measuring the perceptual difference between the pixel
    // luminance and the locally blurred luminance.
    float percPixel   = std::sqrt(safePixelLuma);
    float percBlurred = std::sqrt(safeBlurredLuma);
    float edgeDiff    = std::fabs(percPixel - percBlurred);
    float haloProt    = smoothstep(0.05f, 0.25f, edgeDiff);

    // --- Shadows ---
    if (shadows != 0.0f) {
        float spatialMult = getShadowMult(safeBlurredLuma, shadows);
        float pixelMult   = getShadowMult(safePixelLuma,   shadows);
        float finalMult   = spatialMult + haloProt * (pixelMult - spatialMult);
        r *= finalMult; g *= finalMult; b *= finalMult;
    }

    // --- Contrast ---
    // Applies a symmetrical S-curve in gamma space to preserve perceptual linearity.
    if (contrast != 0.0f) {
        float safeR = std::max(r, 0.0f);
        float safeG = std::max(g, 0.0f);
        float safeB = std::max(b, 0.0f);

        constexpr float gamma = 2.2f;
        float pR = std::clamp(std::pow(safeR, 1.0f / gamma), 0.0f, 1.0f);
        float pG = std::clamp(std::pow(safeG, 1.0f / gamma), 0.0f, 1.0f);
        float pB = std::clamp(std::pow(safeB, 1.0f / gamma), 0.0f, 1.0f);

        float strength = std::pow(2.0f, contrast * 1.25f);

        auto curveChannel = [strength](float p) -> float {
            if (p < 0.5f) return 0.5f * std::pow(2.0f * p,        strength);
            else          return 1.0f - 0.5f * std::pow(2.0f * (1.0f - p), strength);
        };

        float cR = std::pow(curveChannel(pR), gamma);
        float cG = std::pow(curveChannel(pG), gamma);
        float cB = std::pow(curveChannel(pB), gamma);

        // Blend back to linear above white to avoid clipping over-bright regions.
        float mR = smoothstep(1.0f, 1.01f, safeR);
        float mG = smoothstep(1.0f, 1.01f, safeG);
        float mB = smoothstep(1.0f, 1.01f, safeB);

        r = cR + mR * (r - cR);
        g = cG + mG * (g - cG);
        b = cB + mB * (b - cB);
    }
}

// Highlight recovery (negative values) or boost (positive values).
// Uses a gamma-based power curve for the in-range case and a rational
// compressor for values that exceed 1.0.
void Processor::applyHighlightsAdjustment(float& r, float& g, float& b,
                                           float blurredLuma, float highlights)
{
    (void)blurredLuma;
    if (highlights == 0.0f) return;

    float pixelLuma     = getLuma(std::max(r, 0.0f), std::max(g, 0.0f), std::max(b, 0.0f));
    float safePixelLuma = std::max(pixelLuma, 0.0001f);

    // Mask based on tanh-compressed luminance so only bright pixels are affected.
    float maskInput = std::tanh(safePixelLuma * 1.5f);
    float hlMask    = smoothstep(0.3f, 0.95f, maskInput);
    if (hlMask < 0.001f) return;

    float luma = pixelLuma;
    float adjR, adjG, adjB;

    if (highlights < 0.0f) {
        // Recovery: compress highlights toward lower values.
        float newLuma;
        if (luma <= 1.0f) {
            float gamma = 1.0f - highlights * 1.75f;
            newLuma = std::pow(luma, gamma);
        } else {
            // Rational compressor for out-of-range luminance values.
            float excess    = luma - 1.0f;
            float compStr   = -highlights * 6.0f;
            float compExcess = excess / (1.0f + excess * compStr);
            newLuma = 1.0f + compExcess;
        }

        float ratio = newLuma / std::max(luma, 0.0001f);
        adjR = r * ratio; adjG = g * ratio; adjB = b * ratio;

        // Progressively desaturate extreme highlights to match natural rolloff.
        float desatAmt = smoothstep(1.0f, 10.0f, luma);
        adjR += desatAmt * (newLuma - adjR);
        adjG += desatAmt * (newLuma - adjG);
        adjB += desatAmt * (newLuma - adjB);

    } else {
        // Boost: linearly amplify highlights.
        float factor = std::pow(2.0f, highlights * 1.75f);
        adjR = r * factor; adjG = g * factor; adjB = b * factor;
    }

    // Blend the adjustment in through the luminance mask.
    r = r + hlMask * (adjR - r);
    g = g + hlMask * (adjG - g);
    b = b + hlMask * (adjB - b);
}

// White balance via independent temperature (blue-orange axis) and
// tint (green-magenta axis) channel multipliers.
void Processor::applyWhiteBalance(float& r, float& g, float& b,
                                   float temp, float tnt)
{
    if (temp == 0.0f && tnt == 0.0f) return;

    float tR  = 1.0f + temp *  0.20f;
    float tG  = 1.0f + temp *  0.05f;
    float tB  = 1.0f - temp *  0.20f;
    float tnR = 1.0f + tnt  *  0.25f;
    float tnG = 1.0f - tnt  *  0.25f;
    float tnB = 1.0f + tnt  *  0.25f;

    r *= tR * tnR;
    g *= tG * tnG;
    b *= tB * tnB;
}

// Per-channel hue rotation, per-channel saturation scaling and a
// luminance-weighted shadow colour tint.
void Processor::applyColorCalibration(float& r, float& g, float& b,
                                       float redHue,   float greenHue, float blueHue,
                                       float redSat,   float greenSat, float blueSat,
                                       float shadowsTint)
{
    if (redHue == 0.0f && greenHue == 0.0f && blueHue == 0.0f
     && redSat == 0.0f && greenSat == 0.0f && blueSat == 0.0f
     && shadowsTint == 0.0f) return;

    float origR = r, origG = g, origB = b;

    // Hue shifts are implemented as cross-channel colour mixing:
    // positive shifts move the hue toward the next primary (R->Y, G->C, B->M);
    // negative shifts move it toward the previous primary.

    if (redHue != 0.0f) {
        float amount = std::clamp(std::abs(redHue), 0.0f, 1.0f);
        float sign   = (redHue < 0.0f) ? -1.0f : 1.0f;
        r = origR * (1.0f - amount * 0.30f) + origG * amount * 0.30f * sign;
        g = origG * (1.0f + amount * 0.15f * sign);
        b = origB * (1.0f - amount * 0.15f * sign);
        origR = r; origG = g; origB = b;
    }

    if (greenHue != 0.0f) {
        float amount = std::clamp(std::abs(greenHue), 0.0f, 1.0f);
        float sign   = (greenHue < 0.0f) ? -1.0f : 1.0f;
        r = origR * (1.0f - amount * 0.15f * sign);
        g = origG * (1.0f - amount * 0.30f) + origB * amount * 0.30f * sign;
        b = origB * (1.0f + amount * 0.15f * sign);
        origR = r; origG = g; origB = b;
    }

    if (blueHue != 0.0f) {
        float amount = std::clamp(std::abs(blueHue), 0.0f, 1.0f);
        float sign   = (blueHue < 0.0f) ? -1.0f : 1.0f;
        r = origR * (1.0f + amount * 0.15f * sign);
        g = origG * (1.0f - amount * 0.15f * sign);
        b = origB * (1.0f - amount * 0.30f) + origR * amount * 0.30f * sign;
    }

    // Per-channel saturation: scale each channel's deviation from luminance.
    float luma = getLuma(r, g, b);
    if (redSat   != 0.0f) r = luma + (r - luma) * (1.0f + redSat);
    if (greenSat != 0.0f) g = luma + (g - luma) * (1.0f + greenSat);
    if (blueSat  != 0.0f) b = luma + (b - luma) * (1.0f + blueSat);

    // Shadow colour tint: adds a warm or cool cast only to dark regions.
    if (shadowsTint != 0.0f) {
        float pixLuma    = getLuma(r, g, b);
        float shadowMask = smoothstep(0.3f, 0.0f, pixLuma);  // Fades above 0.3
        float tintR =  shadowsTint * 0.15f;
        float tintB = -shadowsTint * 0.15f;
        r += tintR * shadowMask;
        b += tintB * shadowMask;
    }
}

// Global saturation and vibrance adjustment.
// Saturation operates uniformly; vibrance preferentially boosts less saturated
// pixels while protecting skin-tone hues from over-saturation.
void Processor::applyCreativeColor(float& r, float& g, float& b,
                                    float sat, float vib)
{
    float luma = getLuma(r, g, b);

    if (sat != 0.0f) {
        r = luma + (r - luma) * (1.0f + sat);
        g = luma + (g - luma) * (1.0f + sat);
        b = luma + (b - luma) * (1.0f + sat);
    }

    if (vib == 0.0f) return;

    float cMax  = std::max({r, g, b});
    float cMin  = std::min({r, g, b});
    float delta = cMax - cMin;
    if (delta < 0.02f) return;  // Skip nearly achromatic pixels.

    float currentSat = delta / std::max(cMax, 0.001f);
    luma = getLuma(r, g, b);

    if (vib > 0.0f) {
        // Vibrance boost: weight inversely by current saturation.
        float satMask = 1.0f - smoothstep(0.4f, 0.9f, currentSat);

        // Compute HSV to check for skin-tone proximity.
        float h2, s2, v2;
        rgbToHsv(r, g, b, h2, s2, v2);
        float skinCenter  = 25.0f;
        float hueDist     = std::min(std::fabs(h2 - skinCenter),
                                     360.0f - std::fabs(h2 - skinCenter));
        float isSkin      = smoothstep(35.0f, 10.0f, hueDist);
        float skinDampen  = 1.0f - isSkin * 0.6f;
        float amount      = vib * satMask * skinDampen * 3.0f;

        r = luma + (r - luma) * (1.0f + amount);
        g = luma + (g - luma) * (1.0f + amount);
        b = luma + (b - luma) * (1.0f + amount);

    } else {
        // Vibrance reduction: weight toward already-desaturated pixels.
        float desatMask = 1.0f - smoothstep(0.2f, 0.8f, currentSat);
        float amount    = vib * desatMask;
        r = luma + (r - luma) * (1.0f + amount);
        g = luma + (g - luma) * (1.0f + amount);
        b = luma + (b - luma) * (1.0f + amount);
    }
}

// ============================================================================
// HSL Panel
// ============================================================================

// Gaussian-shaped influence function for a single HSL band.
// Returns the influence weight of hue 'hue' on the band centred at 'center'
// with angular half-width 'width / 2', wrapping at 360 degrees.
static float getRawHslInfluence(float hue, float center, float width) {
    float dist      = std::min(std::fabs(hue - center), 360.0f - std::fabs(hue - center));
    constexpr float sharpness = 1.5f;
    float falloff   = dist / (width * 0.5f);
    return std::exp(-sharpness * falloff * falloff);
}

// Apply the eight-band HSL panel to a single pixel.
// Achromatic pixels are skipped. Influence weights across all bands are
// normalised so adjustments blend smoothly at band boundaries.
void Processor::applyHSLPanel(float& r, float& g, float& b,
                               const std::array<HslAdjustment, 8>& hsl)
{
    // Skip pixels with negligible chroma.
    if (std::fabs(r - g) < 0.001f && std::fabs(g - b) < 0.001f) return;

    float h, s, v;
    rgbToHsv(r, g, b, h, s, v);
    float origLuma = getLuma(r, g, b);

    float satMask  = smoothstep(0.05f, 0.20f, s);
    float lumWeight = smoothstep(0.0f, 1.0f, s);
    if (satMask < 0.001f && lumWeight < 0.001f) return;

    // Compute and normalise per-band influence weights.
    float rawInfluences[8];
    float total = 0.0f;

    for (int i = 0; i < 8; ++i) {
        rawInfluences[i] = getRawHslInfluence(h, HSL_RANGES[i].center, HSL_RANGES[i].width);
        total += rawInfluences[i];
    }
    if (total < 0.0001f) return;

    float totalHueShift = 0.0f;
    float totalSatMult  = 0.0f;
    float totalLumAdj   = 0.0f;

    for (int i = 0; i < 8; ++i) {
        float norm       = rawInfluences[i] / total;
        float hueSatInfl = norm * satMask;
        float lumaInfl   = norm * lumWeight;

        totalHueShift += hsl[i].hue        * 2.0f * hueSatInfl;
        totalSatMult  += hsl[i].saturation * hueSatInfl;
        totalLumAdj   += hsl[i].luminance  * lumaInfl;
    }

    // If the result would be achromatic, apply only the luminance adjustment.
    if (s * (1.0f + totalSatMult) < 0.0001f) {
        float finalLuma = origLuma * (1.0f + totalLumAdj);
        r = g = b = finalLuma;
        return;
    }

    float newH = std::fmod(h + totalHueShift + 360.0f, 360.0f);
    float newS = std::clamp(s * (1.0f + totalSatMult), 0.0f, 1.0f);
    hsvToRgb(newH, newS, v, r, g, b);

    // Restore luminance after hue/saturation changes using a ratio scale.
    float newLuma    = getLuma(r, g, b);
    float targetLuma = origLuma * (1.0f + totalLumAdj);

    if (newLuma > 0.0001f) {
        float ratio = targetLuma / newLuma;
        r *= ratio; g *= ratio; b *= ratio;
    } else {
        r = g = b = std::max(0.0f, targetLuma);
    }
}

// ============================================================================
// Colour Grading
// ============================================================================

// Three-way colour grading: independent hue tint and luminance offset for
// shadows, midtones and highlights. Zone boundaries are controlled by the
// balance parameter; feathering is controlled by blending.
void Processor::applyColorGrading(float& r, float& g, float& b,
                                   const ColorGradeSettings& shadows,
                                   const ColorGradeSettings& midtones,
                                   const ColorGradeSettings& highlights,
                                   float blending, float balance)
{
    float luma = getLuma(std::max(r, 0.0f), std::max(g, 0.0f), std::max(b, 0.0f));

    // Compute zone crossover thresholds shifted by the balance parameter.
    constexpr float baseShadowCrossover = 0.1f;
    constexpr float baseHighCrossover   = 0.5f;
    constexpr float balanceRange        = 0.5f;

    float shadowCrossover = baseShadowCrossover + std::max(0.0f, -balance) * balanceRange;
    float highCrossover   = baseHighCrossover   - std::max(0.0f,  balance) * balanceRange;
    float feather         = 0.2f * blending;

    float finalShadowCrossover = std::min(shadowCrossover, highCrossover - 0.01f);

    float shadowMask = 1.0f - smoothstep(finalShadowCrossover - feather,
                                          finalShadowCrossover + feather, luma);
    float highMask   = smoothstep(highCrossover - feather,
                                  highCrossover + feather, luma);
    float midMask    = std::max(0.0f, 1.0f - shadowMask - highMask);

    // Apply a hue-based colour tint and a luminance offset to one zone.
    auto applyTint = [&](const ColorGradeSettings& grade, float mask,
                          float satStr, float lumStr)
    {
        if (grade.saturation > 0.001f) {
            float tR, tG, tB;
            hsvToRgb(grade.hue, 1.0f, 1.0f, tR, tG, tB);
            r += (tR - 0.5f) * grade.saturation * mask * satStr;
            g += (tG - 0.5f) * grade.saturation * mask * satStr;
            b += (tB - 0.5f) * grade.saturation * mask * satStr;
        }
        r += grade.luminance * mask * lumStr;
        g += grade.luminance * mask * lumStr;
        b += grade.luminance * mask * lumStr;
    };

    applyTint(shadows,    shadowMask, 0.3f, 0.5f);
    applyTint(midtones,   midMask,    0.6f, 0.8f);
    applyTint(highlights, highMask,   0.8f, 1.0f);
}

// ============================================================================
// Dehaze
// ============================================================================

// Atmospheric haze removal using a simplified dark channel prior.
// Positive amount removes haze; negative amount adds it.
void Processor::applyDehaze(float& r, float& g, float& b, float amount) {
    if (amount == 0.0f) return;

    // Atmospheric light estimate (slightly below pure white to match natural haze).
    constexpr float aR = 0.95f, aG = 0.97f, aB = 1.0f;

    if (amount > 0.0f) {
        // Estimate transmission from the darkest channel and recover the scene radiance.
        float dark     = std::min({r, g, b});
        float transEst = 1.0f - dark;
        float t        = std::max(1.0f - amount * transEst, 0.1f);

        float recR = (r - aR) / t + aR;
        float recG = (g - aG) / t + aG;
        float recB = (b - aB) / t + aB;

        r = r + amount * (recR - r);
        g = g + amount * (recG - g);
        b = b + amount * (recB - b);

        // Mild contrast and saturation boost to compensate for the flat haze look.
        r = 0.5f + (r - 0.5f) * (1.0f + amount * 0.15f);
        g = 0.5f + (g - 0.5f) * (1.0f + amount * 0.15f);
        b = 0.5f + (b - 0.5f) * (1.0f + amount * 0.15f);

        float luma = getLuma(r, g, b);
        r = luma + (r - luma) * (1.0f + amount * 0.1f);
        g = luma + (g - luma) * (1.0f + amount * 0.1f);
        b = luma + (b - luma) * (1.0f + amount * 0.1f);

    } else {
        // Add haze: blend toward the atmospheric light estimate.
        float aa = std::fabs(amount) * 0.7f;
        r = r + aa * (aR - r);
        g = g + aa * (aG - g);
        b = b + aa * (aB - b);
    }
}

// ============================================================================
// Local Contrast
// ============================================================================

// Local contrast enhancement or reduction (used for sharpness, clarity and structure).
// Computes the ratio between the pixel luminance and a spatially blurred version
// and uses that ratio to either accentuate or soften local detail.
//
// mode 0 (sharpness): edge-magnitude damping reduces haloing.
// mode 1 (clarity / structure): full log-ratio boost for broader detail.
void Processor::applyLocalContrast(float& r, float& g, float& b,
                                    float blurredR, float blurredG, float blurredB,
                                    float amount, int mode, bool isRaw)
{
    if (amount == 0.0f) return;

    float centerLuma       = getLuma(r, g, b);
    float shadowThreshold  = isRaw ? 0.1f : 0.03f;

    // Restrict effect to midtones to avoid shadow noise amplification and
    // highlight clipping.
    float shadowProt = smoothstep(0.0f, shadowThreshold, centerLuma);
    float hlProt     = 1.0f - smoothstep(0.9f, 1.0f, centerLuma);
    float midMask    = shadowProt * hlProt;
    if (midMask < 0.001f) return;

    float blurredLuma    = getLuma(blurredR, blurredG, blurredB);
    float safeCenterLuma = std::max(centerLuma,  0.0001f);
    float safeBlurredLuma = std::max(blurredLuma, 0.0001f);

    float finalR, finalG, finalB;

    if (amount < 0.0f) {
        // Soften: blend the pixel toward its blurred neighbourhood.
        float proj = safeBlurredLuma / safeCenterLuma;
        float bR   = r * proj, bG = g * proj, bB = b * proj;
        float ba   = -amount;
        if (mode == 0) ba *= 0.5f;

        finalR = r + ba * (bR - r);
        finalG = g + ba * (bG - g);
        finalB = b + ba * (bB - b);

    } else {
        // Enhance: amplify deviations from the local average.
        float logRatio  = std::log2(safeCenterLuma / safeBlurredLuma);
        float effAmount = amount;

        if (mode == 0) {
            // Sharpness: damp the effect at strong edges to prevent haloing.
            float edgeMag  = std::fabs(logRatio);
            float normEdge = std::clamp(edgeMag / 3.0f, 0.0f, 1.0f);
            float edgeDamp = 1.0f - std::sqrt(normEdge);
            effAmount = amount * edgeDamp * 0.8f;
        }

        float contrastFactor = std::pow(2.0f, logRatio * effAmount);
        finalR = r * contrastFactor;
        finalG = g * contrastFactor;
        finalB = b * contrastFactor;
    }

    r = r + midMask * (finalR - r);
    g = g + midMask * (finalG - g);
    b = b + midMask * (finalB - b);
}

// ============================================================================
// Tone Curves
// ============================================================================

// Evaluate a cubic Hermite segment between two control points at parameter t.
// Tangents m1 and m2 are the derivatives at p1 and p2, scaled to the segment width.
static float interpolateCubicHermite(float x,
                                      const CurvePoint& p1, const CurvePoint& p2,
                                      float m1, float m2)
{
    float dx = p2.x - p1.x;
    if (dx <= 0.0f) return p1.y;

    float t  = (x - p1.x) / dx;
    float t2 = t * t;
    float t3 = t2 * t;

    float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 =         t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 =         t3 -        t2;

    return h00 * p1.y + h10 * m1 * dx + h01 * p2.y + h11 * m2 * dx;
}

// Evaluate a piecewise cubic Hermite spline curve at the given value.
// Tangents are computed using the Fritsch-Carlson monotone method to prevent
// oscillation between control points. The input and output are in [0, 1].
float Processor::applyCurve(float val, const std::vector<CurvePoint>& points) {
    int count = static_cast<int>(points.size());
    if (count < 2) return val;

    float x = val * 255.0f;

    if (x <= points[0].x)           return points[0].y           / 255.0f;
    if (x >= points[count - 1].x)   return points[count - 1].y   / 255.0f;

    for (int i = 0; i < count - 1; ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];

        if (x <= p2.x) {
            int i0 = std::max(0,         i - 1);
            int i3 = std::min(count - 1, i + 2);
            const auto& p0 = points[i0];
            const auto& p3 = points[i3];

            float deltaBefore  = (p1.y - p0.y) / std::max(0.001f, p1.x - p0.x);
            float deltaCurrent = (p2.y - p1.y) / std::max(0.001f, p2.x - p1.x);
            float deltaAfter   = (p3.y - p2.y) / std::max(0.001f, p3.x - p2.x);

            float tangentP1, tangentP2;

            if (i == 0) {
                tangentP1 = deltaCurrent;
            } else {
                tangentP1 = (deltaBefore * deltaCurrent <= 0.0f)
                            ? 0.0f
                            : (deltaBefore + deltaCurrent) / 2.0f;
            }

            if (i + 1 == count - 1) {
                tangentP2 = deltaCurrent;
            } else {
                tangentP2 = (deltaCurrent * deltaAfter <= 0.0f)
                            ? 0.0f
                            : (deltaCurrent + deltaAfter) / 2.0f;
            }

            // Monotonicity constraint: prevent overshooting.
            if (deltaCurrent != 0.0f) {
                float alpha = tangentP1 / deltaCurrent;
                float beta  = tangentP2 / deltaCurrent;
                if (alpha * alpha + beta * beta > 9.0f) {
                    float tau = 3.0f / std::sqrt(alpha * alpha + beta * beta);
                    tangentP1 *= tau;
                    tangentP2 *= tau;
                }
            }

            float resultY = interpolateCubicHermite(x, p1, p2, tangentP1, tangentP2);
            return std::clamp(resultY / 255.0f, 0.0f, 1.0f);
        }
    }

    return points[count - 1].y / 255.0f;
}

// Apply the luma, red, green and blue curves with luminance-preserving blending.
// When any per-channel curve is active, the luma curve adjusts the overall
// brightness target and the channel curves modulate colour without shifting it.
void Processor::applyAllCurves(float& r, float& g, float& b,
                                const std::vector<CurvePoint>& luma,
                                const std::vector<CurvePoint>& red,
                                const std::vector<CurvePoint>& green,
                                const std::vector<CurvePoint>& blue)
{
    bool redDef   = isDefaultCurve(red);
    bool greenDef = isDefaultCurve(green);
    bool blueDef  = isDefaultCurve(blue);
    bool rgbActive = !redDef || !greenDef || !blueDef;

    if (rgbActive) {
        float gR = applyCurve(r, red);
        float gG = applyCurve(g, green);
        float gB = applyCurve(b, blue);

        float lumaInitial = getLuma(r, g, b);
        float lumaTarget  = applyCurve(lumaInitial, luma);
        float lumaGraded  = getLuma(gR, gG, gB);

        if (lumaGraded > 0.001f) {
            float ratio = lumaTarget / lumaGraded;
            r = gR * ratio; g = gG * ratio; b = gB * ratio;
        } else {
            r = g = b = lumaTarget;
        }

        // Prevent super-whites introduced by the channel curves.
        float maxComp = std::max({r, g, b});
        if (maxComp > 1.0f) { r /= maxComp; g /= maxComp; b /= maxComp; }

    } else {
        // Only the luma curve is active; apply it equally to all channels.
        r = applyCurve(r, luma);
        g = applyCurve(g, luma);
        b = applyCurve(b, luma);
    }
}

// ============================================================================
// Vignette
// ============================================================================

// Radial vignette that darkens (negative amount) or brightens (positive amount)
// the corners of the image. The shape is controlled by roundness and the
// transition by midpoint and feather.
void Processor::applyVignette(float& r, float& g, float& b,
                               int x, int y, int w, int h,
                               float amount, float midpoint,
                               float roundness, float feather)
{
    if (amount == 0.0f) return;

    float aspect = static_cast<float>(h) / w;
    float uvX    = (static_cast<float>(x) / w - 0.5f) * 2.0f;
    float uvY    = (static_cast<float>(y) / h - 0.5f) * 2.0f;

    float vRound = 1.0f - roundness;
    float uvRx   = (uvX >= 0 ? 1.0f : -1.0f) * std::pow(std::fabs(uvX), vRound);
    float uvRy   = (uvY >= 0 ? 1.0f : -1.0f) * std::pow(std::fabs(uvY), vRound);

    float d       = std::sqrt(uvRx * uvRx + uvRy * uvRy * aspect * aspect) * 0.5f;
    float vFeather = feather * 0.5f;
    float mask    = smoothstep(midpoint - vFeather, midpoint + vFeather, d);

    if (amount < 0.0f) {
        float mult = 1.0f + amount * mask;
        r *= mult; g *= mult; b *= mult;
    } else {
        r = r + amount * mask * (1.0f - r);
        g = g + amount * mask * (1.0f - g);
        b = b + amount * mask * (1.0f - b);
    }
}

// ============================================================================
// Grain
// ============================================================================

// Procedural film grain using two octaves of gradient noise.
// The grain is masked so it avoids pure blacks and pure whites.
void Processor::applyGrain(float& r, float& g, float& b,
                            int x, int y,
                            float amount, float size, float roughness,
                            float scale)
{
    if (amount <= 0.0f) return;

    float luma     = std::max(0.0f, getLuma(r, g, b));
    float lumaMask = smoothstep(0.0f, 0.15f, luma) * (1.0f - smoothstep(0.6f, 1.0f, luma));

    float freq = (1.0f / std::max(size, 0.1f)) / scale;

    float baseCoordX  = x * freq;
    float baseCoordY  = y * freq;
    float roughCoordX = x * freq * 0.6f + 5.2f;
    float roughCoordY = y * freq * 0.6f + 1.3f;

    float noiseBase  = gradientNoise(baseCoordX,  baseCoordY);
    float noiseRough = gradientNoise(roughCoordX, roughCoordY);

    // Blend the two noise octaves by the roughness parameter.
    float noise    = noiseBase + roughness * (noiseRough - noiseBase);
    float grainVal = noise * amount * 0.5f * lumaMask;

    r += grainVal; g += grainVal; b += grainVal;
}

// ============================================================================
// Chromatic Aberration
// ============================================================================

// Correct (or exaggerate) chromatic aberration by shifting the red and blue
// channels radially from the image centre. The green channel is not moved.
// The correction is applied to the full pixel array rather than per-pixel
// because it requires sampling from neighbouring positions.
void Processor::applyChromaticAberration(std::vector<float>& data,
                                          int w, int h, int channels,
                                          float caRC, float caBY)
{
    if (channels < 3) return;
    if (std::fabs(caRC) < 0.000001f && std::fabs(caBY) < 0.000001f) return;

    // Work from a copy so shifted samples reference the unmodified image.
    const std::vector<float> orig = data;

    float cx = w / 2.0f;
    float cy = h / 2.0f;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx   = x - cx;
            float dy   = y - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 0.001f) continue;

            float dirX = dx / dist;
            float dirY = dy / dist;

            // Red channel: shift controlled by caRC.
            float redShiftX = dirX * dist * caRC;
            float redShiftY = dirY * dist * caRC;
            int rxc = std::clamp(static_cast<int>(std::round(x - redShiftX)), 0, w - 1);
            int ryc = std::clamp(static_cast<int>(std::round(y - redShiftY)), 0, h - 1);

            // Blue channel: shift controlled by caBY.
            float blueShiftX = dirX * dist * caBY;
            float blueShiftY = dirY * dist * caBY;
            int bxc = std::clamp(static_cast<int>(std::round(x - blueShiftX)), 0, w - 1);
            int byc = std::clamp(static_cast<int>(std::round(y - blueShiftY)), 0, h - 1);

            size_t idx = (static_cast<size_t>(y) * w + x) * channels;

            data[idx + 0] = orig[(static_cast<size_t>(ryc) * w + rxc) * channels + 0];
            data[idx + 1] = orig[(static_cast<size_t>(y)   * w + x)   * channels + 1];
            data[idx + 2] = orig[(static_cast<size_t>(byc) * w + bxc) * channels + 2];
        }
    }
}

// ============================================================================
// Main Processing Pipeline
// ============================================================================

// Apply the full adjustment pipeline defined by params to buffer in-place.
// The pipeline order is:
//   1. Chromatic aberration (whole-image pass, before per-pixel loop)
//   2. Linear exposure
//   3. White balance
//   4. Filmic brightness
//   5. Dehaze
//   6. Tonal adjustments (whites, blacks, shadows, contrast)
//   7. Highlights
//   8. Colour calibration
//   9. HSL panel
//  10. Colour grading
//  11. Saturation and vibrance
//  12. Local contrast (sharpness, clarity, structure)
//  13. Linear-to-sRGB conversion
//  14. Tone curves
//  15. Vignette
//  16. Grain
//  17. sRGB-to-linear conversion
//  18. Output clamp
void Processor::apply(ImageBuffer& buffer, const Params& params) {
    if (params.isIdentity()) return;

    ImageBuffer::WriteLock lock(&buffer);  // Thread-safe write lock.

    const int    w  = buffer.width();
    const int    h  = buffer.height();
    const int    ch = buffer.channels();
    auto&        data = buffer.data();

    // -------------------------------------------------------------------------
    // Pre-compute blurred luminance maps required by various operations.
    // Different operations use different blur radii scaled to the image size.
    // -------------------------------------------------------------------------
    const bool needsBlur = (params.contrast   != 0.0f || params.shadows    != 0.0f
                         || params.blacks      != 0.0f || params.highlights != 0.0f
                         || params.sharpness   != 0.0f || params.clarity    != 0.0f
                         || params.structure   != 0.0f);

    constexpr float REF_DIM = 1080.0f;
    float scale = std::max(0.1f,
                           std::min(static_cast<float>(w), static_cast<float>(h)) / REF_DIM);

    std::vector<float> tonalBlurred;
    std::vector<float> sharpnessBlurred;
    std::vector<float> clarityBlurred;
    std::vector<float> structureBlurred;

    if (needsBlur) {
        int tonalRadius = std::max(2, static_cast<int>(20 * scale));
        tonalBlurred = computeBlurredLuma(data, w, h, ch, tonalRadius);

        if (params.sharpness != 0.0f) {
            int sharpRadius = std::max(1, static_cast<int>(3 * scale));
            sharpnessBlurred = computeBlurredLuma(data, w, h, ch, sharpRadius);
        }
        if (params.clarity != 0.0f) {
            int clarRadius = std::max(2, static_cast<int>(30 * scale));
            clarityBlurred = computeBlurredLuma(data, w, h, ch, clarRadius);
        }
        if (params.structure != 0.0f) {
            int structRadius = std::max(1, static_cast<int>(5 * scale));
            structureBlurred = computeBlurredLuma(data, w, h, ch, structRadius);
        }
    }

    // --- Step 1: Chromatic aberration (full-image pass before per-pixel loop) ---
    applyChromaticAberration(data, w, h, ch, params.caRedCyan, params.caBlueYellow);

    // Pre-check which optional operations are needed to avoid per-pixel branching.
    bool needsHSL = false;
    for (const auto& hslBand : params.hsl) {
        if (hslBand.hue != 0.0f || hslBand.saturation != 0.0f || hslBand.luminance != 0.0f) {
            needsHSL = true;
            break;
        }
    }

    bool needsCG = (params.colorGradingShadows.saturation    != 0.0f
                 || params.colorGradingShadows.luminance      != 0.0f
                 || params.colorGradingMidtones.saturation    != 0.0f
                 || params.colorGradingMidtones.luminance     != 0.0f
                 || params.colorGradingHighlights.saturation  != 0.0f
                 || params.colorGradingHighlights.luminance   != 0.0f);

    bool needsCurves = !isDefaultCurve(params.lumaCurve)
                    || !isDefaultCurve(params.redCurve)
                    || !isDefaultCurve(params.greenCurve)
                    || !isDefaultCurve(params.blueCurve);

    // -------------------------------------------------------------------------
    // Step 2: Per-pixel processing loop
    // -------------------------------------------------------------------------
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {

            size_t idx = (static_cast<size_t>(y) * w + x) * ch;
            float r, g, b;

            if (ch >= 3) {
                r = data[idx]; g = data[idx + 1]; b = data[idx + 2];
            } else {
                r = g = b = data[idx];
            }

            // 2a. Linear exposure.
            applyLinearExposure(r, g, b, params.exposure);

            // 2b. White balance.
            applyWhiteBalance(r, g, b, params.temperature, params.tint);

            // 2c. Filmic brightness.
            applyFilmicExposure(r, g, b, params.brightness);

            // 2d. Dehaze.
            applyDehaze(r, g, b, params.dehaze);

            // 2e. Tonal adjustments (whites, blacks, shadows, contrast).
            if (params.contrast != 0.0f || params.shadows != 0.0f
             || params.whites   != 0.0f || params.blacks  != 0.0f) {
                float blurred = tonalBlurred.empty()
                                ? getLuma(r, g, b)
                                : tonalBlurred[y * w + x];
                applyTonalAdjustments(r, g, b, blurred,
                                       params.contrast, params.shadows,
                                       params.whites,   params.blacks);
            }

            // 2f. Highlights.
            if (params.highlights != 0.0f) {
                float blurred = tonalBlurred.empty()
                                ? getLuma(r, g, b)
                                : tonalBlurred[y * w + x];
                applyHighlightsAdjustment(r, g, b, blurred, params.highlights);
            }

            // 2g. Colour calibration.
            applyColorCalibration(r, g, b,
                                   params.redHue,  params.greenHue, params.blueHue,
                                   params.redSat,  params.greenSat, params.blueSat,
                                   params.shadowsTint);

            // 2h. HSL panel.
            if (needsHSL) {
                applyHSLPanel(r, g, b, params.hsl);
            }

            // 2i. Colour grading.
            if (needsCG) {
                applyColorGrading(r, g, b,
                                   params.colorGradingShadows,
                                   params.colorGradingMidtones,
                                   params.colorGradingHighlights,
                                   params.colorGradingBlending,
                                   params.colorGradingBalance);
            }

            // 2j. Saturation and vibrance.
            applyCreativeColor(r, g, b, params.saturation, params.vibrance);

            // 2k. Local contrast (sharpness, clarity, structure).
            if (params.sharpness != 0.0f && !sharpnessBlurred.empty()) {
                float bl = sharpnessBlurred[y * w + x];
                applyLocalContrast(r, g, b, bl, bl, bl,
                                    params.sharpness * 2.5f, 0, true);
            }
            if (params.clarity != 0.0f && !clarityBlurred.empty()) {
                float bl = clarityBlurred[y * w + x];
                applyLocalContrast(r, g, b, bl, bl, bl,
                                    params.clarity * 0.5f, 1, true);
            }
            if (params.structure != 0.0f && !structureBlurred.empty()) {
                float bl = structureBlurred[y * w + x];
                applyLocalContrast(r, g, b, bl, bl, bl,
                                    params.structure * 0.5f, 1, true);
            }

            // 2l. Convert to sRGB for curve and effects operations.
            auto linearToSrgb = [](float c) -> float {
                c = std::clamp(c, 0.0f, 1.0f);
                return (c <= 0.0031308f)
                       ? c * 12.92f
                       : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            };
            r = linearToSrgb(r);
            g = linearToSrgb(g);
            b = linearToSrgb(b);

            // 2m. Tone curves.
            if (needsCurves) {
                applyAllCurves(r, g, b,
                                params.lumaCurve, params.redCurve,
                                params.greenCurve, params.blueCurve);
            }

            // 2n. Vignette.
            applyVignette(r, g, b, x, y, w, h,
                           params.vignetteAmount,    params.vignetteMidpoint,
                           params.vignetteRoundness, params.vignetteFeather);

            // 2o. Grain.
            applyGrain(r, g, b, x, y,
                        params.grainAmount, params.grainSize,
                        params.grainRoughness, scale);

            // 2p. Convert back to linear.
            auto srgbToLinear = [](float c) -> float {
                c = std::clamp(c, 0.0f, 1.0f);
                return (c <= 0.04045f)
                       ? c / 12.92f
                       : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
            r = srgbToLinear(r);
            g = srgbToLinear(g);
            b = srgbToLinear(b);

            // 2q. Clamp to [0, 1] and write back.
            r = std::clamp(r, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b, 0.0f, 1.0f);

            if (ch >= 3) {
                data[idx]     = r;
                data[idx + 1] = g;
                data[idx + 2] = b;
            } else {
                data[idx] = getLuma(r, g, b);
            }
        }
    }
}

// ============================================================================
// Preview
// ============================================================================

// Downsample the buffer to at most maxDim pixels on the long edge, apply the
// full adjustment pipeline, and return the result as an interleaved RGB float vector.
std::vector<float> Processor::applyPreview(const ImageBuffer& buffer,
                                            const Params& params,
                                            int maxDim)
{
    const int w  = buffer.width();
    const int h  = buffer.height();
    const int ch = buffer.channels();

    // Compute uniform scale factor to fit within maxDim.
    float scaleFactor = 1.0f;
    if (w > maxDim || h > maxDim) {
        scaleFactor = static_cast<float>(maxDim) / std::max(w, h);
    }

    const int pw = std::max(1, static_cast<int>(w * scaleFactor));
    const int ph = std::max(1, static_cast<int>(h * scaleFactor));

    const auto& srcData = buffer.data();
    std::vector<float> preview(static_cast<size_t>(pw) * ph * 3);

    // Nearest-neighbour downsample.
    #pragma omp parallel for schedule(static)
    for (int py = 0; py < ph; ++py) {
        for (int px = 0; px < pw; ++px) {
            int sx = std::clamp(static_cast<int>(px / scaleFactor), 0, w - 1);
            int sy = std::clamp(static_cast<int>(py / scaleFactor), 0, h - 1);

            size_t srcIdx = (static_cast<size_t>(sy) * w + sx) * ch;
            size_t dstIdx = (static_cast<size_t>(py) * pw + px) * 3;

            if (ch >= 3) {
                preview[dstIdx]     = srcData[srcIdx];
                preview[dstIdx + 1] = srcData[srcIdx + 1];
                preview[dstIdx + 2] = srcData[srcIdx + 2];
            } else {
                preview[dstIdx] = preview[dstIdx + 1] = preview[dstIdx + 2] = srcData[srcIdx];
            }
        }
    }

    // Route through a temporary ImageBuffer so apply() can operate on it.
    ImageBuffer previewBuf(pw, ph, 3);
    previewBuf.data() = std::move(preview);
    apply(previewBuf, params);

    return std::move(previewBuf.data());
}

} // namespace RawEditor