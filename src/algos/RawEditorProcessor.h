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

#ifndef RAWEDITORPROCESSOR_H
#define RAWEDITORPROCESSOR_H

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

class ImageBuffer;

namespace RawEditor {

// ----------------------------------------------------------------------------
// CurvePoint
// A single control point on a tone curve, with coordinates in [0, 255].
// ----------------------------------------------------------------------------
struct CurvePoint {
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const CurvePoint& other) const {
        return x == other.x && y == other.y;
    }
};

// ----------------------------------------------------------------------------
// HslAdjustment
// Per-color-range adjustments applied by the HSL panel.
// All values are normalised to [-1, +1].
// ----------------------------------------------------------------------------
struct HslAdjustment {
    float hue        = 0.0f;   // Hue rotation:        -1 to +1
    float saturation = 0.0f;   // Saturation scaling:  -1 to +1
    float luminance  = 0.0f;   // Luminance offset:    -1 to +1

    bool operator==(const HslAdjustment& other) const {
        return hue == other.hue
            && saturation == other.saturation
            && luminance  == other.luminance;
    }
};

// ----------------------------------------------------------------------------
// HslRange
// Centre hue and angular width (degrees) of one of the eight HSL panel bands.
// ----------------------------------------------------------------------------
struct HslRange {
    float center;
    float width;
};

// Eight HSL colour bands covering the full hue circle.
static constexpr std::array<HslRange, 8> HSL_RANGES = {{
    {358.0f,  35.0f},   // Red
    { 25.0f,  45.0f},   // Orange
    { 60.0f,  40.0f},   // Yellow
    {115.0f,  90.0f},   // Green
    {180.0f,  60.0f},   // Aqua
    {225.0f,  60.0f},   // Blue
    {280.0f,  55.0f},   // Purple
    {330.0f,  50.0f},   // Magenta
}};

// ----------------------------------------------------------------------------
// ColorGradeSettings
// Settings for one tonal region (shadows / midtones / highlights) of the
// three-way colour grading panel.
// ----------------------------------------------------------------------------
struct ColorGradeSettings {
    float hue        = 0.0f;   // Target hue:       0 to 360
    float saturation = 0.0f;   // Colour intensity: 0 to 1
    float luminance  = 0.0f;   // Brightness offset: -1 to +1

    bool operator==(const ColorGradeSettings& other) const {
        return hue == other.hue
            && saturation == other.saturation
            && luminance  == other.luminance;
    }
};

// ----------------------------------------------------------------------------
// Params
// Complete set of adjustment parameters for the raw editor pipeline.
// Default values represent the identity transform (no change to the image).
// ----------------------------------------------------------------------------
struct Params {

    // --- Tone ---
    float exposure   = 0.0f;   // Linear EV shift:         -5 to +5
    float brightness = 0.0f;   // Filmic brightness:        -1 to +1
    float contrast   = 0.0f;   // S-curve contrast:         -1 to +1
    float highlights = 0.0f;   // Highlight recovery/boost: -1 to +1
    float shadows    = 0.0f;   // Shadow lift/crush:        -1 to +1
    float whites     = 0.0f;   // White point shift:        -1 to +1
    float blacks     = 0.0f;   // Black point shift:        -1 to +1

    // --- Colour ---
    float temperature = 0.0f;  // White balance temperature: -1 to +1
    float tint        = 0.0f;  // White balance tint:        -1 to +1
    float saturation  = 0.0f;  // Global saturation:         -1 to +1
    float vibrance    = 0.0f;  // Skin-aware saturation:     -1 to +1

    // --- Colour Calibration (per-channel hue and saturation) ---
    float redHue    = 0.0f;    // Red channel hue shift:      -1 to +1
    float greenHue  = 0.0f;    // Green channel hue shift:    -1 to +1
    float blueHue   = 0.0f;    // Blue channel hue shift:     -1 to +1
    float redSat    = 0.0f;    // Red channel saturation:     -1 to +1
    float greenSat  = 0.0f;    // Green channel saturation:   -1 to +1
    float blueSat   = 0.0f;    // Blue channel saturation:    -1 to +1
    float shadowsTint = 0.0f;  // Shadow colour tint:         -1 to +1

    // --- HSL Panel (eight colour ranges) ---
    std::array<HslAdjustment, 8> hsl = {};

    // --- Three-Way Colour Grading ---
    ColorGradeSettings colorGradingShadows;
    ColorGradeSettings colorGradingMidtones;
    ColorGradeSettings colorGradingHighlights;
    float colorGradingBlending = 1.0f;  // Zone feather blending:  0 to 1
    float colorGradingBalance  = 0.0f;  // Shadow/highlight balance: -1 to +1

    // --- Detail ---
    float sharpness = 0.0f;   // Local contrast (fine detail):  0 to +1
    float clarity   = 0.0f;   // Local contrast (midtone):      -1 to +1
    float structure = 0.0f;   // Local contrast (broad):        -1 to +1
    float dehaze    = 0.0f;   // Atmospheric haze removal:      -1 to +1
    float lumaNR    = 0.0f;   // Luminance noise reduction (reserved)
    float colorNR   = 0.0f;   // Colour noise reduction (reserved)

    // --- Chromatic Aberration ---
    float caRedCyan    = 0.0f;  // Red/cyan fringe correction:  -0.01 to +0.01
    float caBlueYellow = 0.0f;  // Blue/yellow fringe correction: -0.01 to +0.01

    // --- Vignette ---
    float vignetteAmount    = 0.0f;  // Vignette strength:   -1 to +1
    float vignetteMidpoint  = 0.5f;  // Transition midpoint:  0 to 1
    float vignetteRoundness = 0.0f;  // Shape roundness:      0 to 1
    float vignetteFeather   = 0.5f;  // Edge softness:        0 to 1

    // --- Grain ---
    float grainAmount    = 0.0f;  // Grain intensity:   0 to 1
    float grainSize      = 1.0f;  // Grain scale:       0.1 to 5
    float grainRoughness = 0.5f;  // Grain texture mix: 0 to 1

    // --- Tone Curves (up to 16 control points each, coordinates in [0, 255]) ---
    std::vector<CurvePoint> lumaCurve  = { {0.0f, 0.0f}, {255.0f, 255.0f} };
    std::vector<CurvePoint> redCurve   = { {0.0f, 0.0f}, {255.0f, 255.0f} };
    std::vector<CurvePoint> greenCurve = { {0.0f, 0.0f}, {255.0f, 255.0f} };
    std::vector<CurvePoint> blueCurve  = { {0.0f, 0.0f}, {255.0f, 255.0f} };

    // Returns true when all parameters are at their default values (identity transform).
    bool isIdentity() const;

    // Equality comparison used by the undo/redo history system.
    bool operator==(const Params& other) const {
        return exposure            == other.exposure
            && brightness         == other.brightness
            && contrast           == other.contrast
            && highlights         == other.highlights
            && shadows            == other.shadows
            && whites             == other.whites
            && blacks             == other.blacks
            && temperature        == other.temperature
            && tint               == other.tint
            && saturation         == other.saturation
            && vibrance           == other.vibrance
            && redHue             == other.redHue
            && greenHue           == other.greenHue
            && blueHue            == other.blueHue
            && redSat             == other.redSat
            && greenSat           == other.greenSat
            && blueSat            == other.blueSat
            && shadowsTint        == other.shadowsTint
            && hsl                == other.hsl
            && colorGradingShadows    == other.colorGradingShadows
            && colorGradingMidtones   == other.colorGradingMidtones
            && colorGradingHighlights == other.colorGradingHighlights
            && colorGradingBlending   == other.colorGradingBlending
            && colorGradingBalance    == other.colorGradingBalance
            && sharpness          == other.sharpness
            && clarity            == other.clarity
            && structure          == other.structure
            && dehaze             == other.dehaze
            && lumaNR             == other.lumaNR
            && colorNR            == other.colorNR
            && caRedCyan          == other.caRedCyan
            && caBlueYellow       == other.caBlueYellow
            && vignetteAmount     == other.vignetteAmount
            && vignetteMidpoint   == other.vignetteMidpoint
            && vignetteRoundness  == other.vignetteRoundness
            && vignetteFeather    == other.vignetteFeather
            && grainAmount        == other.grainAmount
            && grainSize          == other.grainSize
            && grainRoughness     == other.grainRoughness
            && lumaCurve          == other.lumaCurve
            && redCurve           == other.redCurve
            && greenCurve         == other.greenCurve
            && blueCurve          == other.blueCurve;
    }

    bool operator!=(const Params& other) const {
        return !(*this == other);
    }
};

// ----------------------------------------------------------------------------
// Processor
// Stateless collection of static image processing functions.
// All pixel-level operations work in linear float RGB space unless noted.
// The main entry point is apply(), which executes the full adjustment pipeline.
// ----------------------------------------------------------------------------
class Processor {
public:

    // Apply all adjustments defined in params to buffer in-place.
    // The buffer must contain float data in [0, 1] range (interleaved RGB or mono).
    static void apply(ImageBuffer& buffer, const Params& params);

    // Apply all adjustments and return a downsampled float RGB preview vector.
    // maxDim constrains the longer edge of the preview.
    static std::vector<float> applyPreview(const ImageBuffer& buffer,
                                           const Params& params,
                                           int maxDim = 800);

    // Returns true if the curve is the default identity (two points: (0,0) and (255,255)).
    static bool isDefaultCurve(const std::vector<CurvePoint>& points);

private:

    // -------------------------------------------------------------------------
    // Per-pixel adjustment functions
    // All operate on a single pixel's (r, g, b) triplet in linear space.
    // -------------------------------------------------------------------------

    // Exposure: multiply by 2^exposure (linear EV shift).
    static void applyLinearExposure(float& r, float& g, float& b, float exposure);

    // Brightness: filmic rational-curve midtone adjustment with chroma preservation.
    static void applyFilmicExposure(float& r, float& g, float& b, float brightness);

    // Tonal range adjustments: contrast (S-curve), shadow lift, white/black points.
    // Requires a pre-computed blurred luminance value for spatial context.
    static void applyTonalAdjustments(float& r, float& g, float& b,
                                       float blurredLuma,
                                       float contrast, float shadows,
                                       float whites,   float blacks);

    // Highlight recovery (negative) or boost (positive) using gamma-based mapping.
    static void applyHighlightsAdjustment(float& r, float& g, float& b,
                                           float blurredLuma, float highlights);

    // White balance via per-channel temperature and tint multipliers.
    static void applyWhiteBalance(float& r, float& g, float& b,
                                   float temp, float tint);

    // Per-channel hue rotation and saturation scaling, plus shadow colour tint.
    static void applyColorCalibration(float& r, float& g, float& b,
                                       float redHue,  float greenHue, float blueHue,
                                       float redSat,  float greenSat, float blueSat,
                                       float shadowsTint);

    // Global saturation and vibrance (vibrance includes skin-tone protection).
    static void applyCreativeColor(float& r, float& g, float& b,
                                    float sat, float vib);

    // Eight-band HSL panel: per-range hue, saturation and luminance adjustments.
    static void applyHSLPanel(float& r, float& g, float& b,
                               const std::array<HslAdjustment, 8>& hsl);

    // Three-way colour grading: independent tint and luminance per tonal zone.
    static void applyColorGrading(float& r, float& g, float& b,
                                   const ColorGradeSettings& shadows,
                                   const ColorGradeSettings& midtones,
                                   const ColorGradeSettings& highlights,
                                   float blending, float balance);

    // Atmospheric haze removal (dark channel prior) or haze addition.
    static void applyDehaze(float& r, float& g, float& b, float amount);

    // Local contrast for sharpness, clarity and structure tools.
    // mode 0 = sharpness (edge-damped), mode 1 = clarity/structure (log-ratio).
    static void applyLocalContrast(float& r, float& g, float& b,
                                    float blurredR, float blurredG, float blurredB,
                                    float amount, int mode, bool isRaw);

    // Evaluate a cubic Hermite spline tone curve at value val (in [0, 1]).
    static float applyCurve(float val, const std::vector<CurvePoint>& points);

    // Apply the luma, red, green and blue curves with luminance-preserving blending.
    static void applyAllCurves(float& r, float& g, float& b,
                                const std::vector<CurvePoint>& luma,
                                const std::vector<CurvePoint>& red,
                                const std::vector<CurvePoint>& green,
                                const std::vector<CurvePoint>& blue);

    // Radial vignette: darkens or brightens the image corners.
    static void applyVignette(float& r, float& g, float& b,
                               int x, int y, int w, int h,
                               float amount, float midpoint,
                               float roundness, float feather);

    // Procedural film grain using two-octave gradient noise.
    static void applyGrain(float& r, float& g, float& b,
                            int x, int y,
                            float amount, float size, float roughness,
                            float scale);

    // Chromatic aberration correction: radial red and blue channel shift.
    // Operates on the full pixel array rather than a single pixel.
    static void applyChromaticAberration(std::vector<float>& data,
                                          int w, int h, int channels,
                                          float caRC, float caBY);

    // -------------------------------------------------------------------------
    // Colour space and math helpers
    // -------------------------------------------------------------------------

    // Rec. 709 luminance: 0.2126*R + 0.7152*G + 0.0722*B
    static float getLuma(float r, float g, float b);

    static void  rgbToHsv(float r, float g, float b, float& h, float& s, float& v);
    static void  hsvToRgb(float h, float s, float v, float& r, float& g, float& b);

    // Smooth Hermite interpolation, equivalent to GLSL smoothstep().
    static float smoothstep(float edge0, float edge1, float x);

    // Hash-based pseudo-random value (deterministic per coordinate).
    static float hashNoise(float x, float y);

    // Quintic-interpolated gradient noise (Perlin-style).
    static float gradientNoise(float px, float py);

    // -------------------------------------------------------------------------
    // Blur helper
    // -------------------------------------------------------------------------

    // Compute a Gaussian-approximated blur of the luminance channel using
    // three passes of a separable box blur. Used as spatial context for tonal
    // and local contrast operations.
    static std::vector<float> computeBlurredLuma(const std::vector<float>& data,
                                                  int w, int h, int channels,
                                                  int radius);
};

} // namespace RawEditor

#endif // RAWEDITORPROCESSOR_H