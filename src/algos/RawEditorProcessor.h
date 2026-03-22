#ifndef RAWEDITORPROCESSOR_H
#define RAWEDITORPROCESSOR_H

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

// Forward declarations
class ImageBuffer;

namespace RawEditor {

// ─── Curve Point ──────────────────────────────────────────────────────────────
struct CurvePoint {
    float x = 0.0f;
    float y = 0.0f;
    
    bool operator==(const CurvePoint& other) const {
        return x == other.x && y == other.y;
    }
};

// ─── HSL Color Adjustment (per color range) ───────────────────────────────────
struct HslAdjustment {
    float hue        = 0.0f;   // -1 to +1
    float saturation = 0.0f;   // -1 to +1
    float luminance  = 0.0f;   // -1 to +1
    
    bool operator==(const HslAdjustment& other) const {
        return hue == other.hue && saturation == other.saturation && luminance == other.luminance;
    }
};

// ─── HSL Range Definition ─────────────────────────────────────────────────────
struct HslRange {
    float center;
    float width;
};

// 8 HSL color ranges: Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta
static constexpr std::array<HslRange, 8> HSL_RANGES = {{
    {358.0f, 35.0f},   // Red
    { 25.0f, 45.0f},   // Orange
    { 60.0f, 40.0f},   // Yellow
    {115.0f, 90.0f},   // Green
    {180.0f, 60.0f},   // Aqua
    {225.0f, 60.0f},   // Blue
    {280.0f, 55.0f},   // Purple
    {330.0f, 50.0f}    // Magenta
}};

// ─── Color Grade Settings ─────────────────────────────────────────────────────
struct ColorGradeSettings {
    float hue        = 0.0f;   // 0–360
    float saturation = 0.0f;   // 0–1
    float luminance  = 0.0f;   // -1 to +1
    
    bool operator==(const ColorGradeSettings& other) const {
        return hue == other.hue && saturation == other.saturation && luminance == other.luminance;
    }
};

// ─── All Parameters ──────────────────────────────────────────────────────────
struct Params {
    // Basic
    float exposure   = 0.0f;   // -5 to +5
    float brightness = 0.0f;   // -1 to +1
    float contrast   = 0.0f;   // -1 to +1
    float highlights = 0.0f;   // -1 to +1
    float shadows    = 0.0f;   // -1 to +1
    float whites     = 0.0f;   // -1 to +1
    float blacks     = 0.0f;   // -1 to +1

    // Color
    float temperature = 0.0f;  // -1 to +1
    float tint        = 0.0f;  // -1 to +1
    float saturation  = 0.0f;  // -1 to +1
    float vibrance    = 0.0f;  // -1 to +1

    // Color Calibration (per-channel hue and saturation adjustments)
    float redHue      = 0.0f;  // -1 to +1 
    float greenHue    = 0.0f;  // -1 to +1
    float blueHue     = 0.0f;  // -1 to +1
    float redSat      = 0.0f;  // -1 to +1 
    float greenSat    = 0.0f;  // -1 to +1
    float blueSat     = 0.0f;  // -1 to +1
    float shadowsTint = 0.0f;  // -1 to +1 (shadow color tint)

    // HSL (8 color ranges)
    std::array<HslAdjustment, 8> hsl = {};

    // Color Grading (three-way)
    ColorGradeSettings colorGradingShadows;
    ColorGradeSettings colorGradingMidtones;
    ColorGradeSettings colorGradingHighlights;
    float colorGradingBlending = 1.0f;  // 0–1
    float colorGradingBalance  = 0.0f;  // -1 to +1

    // Detail
    float sharpness          = 0.0f;  // 0 to +1
    float clarity            = 0.0f;  // -1 to +1
    float structure          = 0.0f;  // -1 to +1
    float dehaze             = 0.0f;  // -1 to +1
    float lumaNR             = 0.0f;  // 0 to +1 (unused placeholder for future)
    float colorNR            = 0.0f;  // 0 to +1 (unused placeholder for future)

    // Chromatic Aberration
    float caRedCyan    = 0.0f; // -0.01 to +0.01
    float caBlueYellow = 0.0f; // -0.01 to +0.01

    // Effects — Vignette
    float vignetteAmount    = 0.0f;  // -1 to +1
    float vignetteMidpoint  = 0.5f;  // 0 to 1
    float vignetteRoundness = 0.0f;  // 0 to 1
    float vignetteFeather   = 0.5f;  // 0 to 1

    // Effects — Grain
    float grainAmount    = 0.0f;  // 0 to 1
    float grainSize      = 1.0f;  // 0.1 to 5
    float grainRoughness = 0.5f;  // 0 to 1

    // Curves (up to 16 control points each)
    std::vector<CurvePoint> lumaCurve  = { {0.0f, 0.0f}, {255.0f, 255.0f} };
    std::vector<CurvePoint> redCurve   = { {0.0f, 0.0f}, {255.0f, 255.0f} };
    std::vector<CurvePoint> greenCurve = { {0.0f, 0.0f}, {255.0f, 255.0f} };
    std::vector<CurvePoint> blueCurve  = { {0.0f, 0.0f}, {255.0f, 255.0f} };

    // Check if all values are at default (no-op)
    bool isIdentity() const;
    
    // Comparison operator for undo/redo history tracking
    bool operator==(const Params& other) const {
        return exposure == other.exposure &&
               brightness == other.brightness &&
               contrast == other.contrast &&
               highlights == other.highlights &&
               shadows == other.shadows &&
               whites == other.whites &&
               blacks == other.blacks &&
               temperature == other.temperature &&
               tint == other.tint &&
               saturation == other.saturation &&
               vibrance == other.vibrance &&
               redHue == other.redHue &&
               greenHue == other.greenHue &&
               blueHue == other.blueHue &&
               redSat == other.redSat &&
               greenSat == other.greenSat &&
               blueSat == other.blueSat &&
               shadowsTint == other.shadowsTint &&
               hsl == other.hsl &&
               colorGradingShadows == other.colorGradingShadows &&
               colorGradingMidtones == other.colorGradingMidtones &&
               colorGradingHighlights == other.colorGradingHighlights &&
               colorGradingBlending == other.colorGradingBlending &&
               colorGradingBalance == other.colorGradingBalance &&
               sharpness == other.sharpness &&
               clarity == other.clarity &&
               structure == other.structure &&
               dehaze == other.dehaze &&
               lumaNR == other.lumaNR &&
               colorNR == other.colorNR &&
               caRedCyan == other.caRedCyan &&
               caBlueYellow == other.caBlueYellow &&
               vignetteAmount == other.vignetteAmount &&
               vignetteMidpoint == other.vignetteMidpoint &&
               vignetteRoundness == other.vignetteRoundness &&
               vignetteFeather == other.vignetteFeather &&
               grainAmount == other.grainAmount &&
               grainSize == other.grainSize &&
               grainRoughness == other.grainRoughness &&
               lumaCurve == other.lumaCurve &&
               redCurve == other.redCurve &&
               greenCurve == other.greenCurve &&
               blueCurve == other.blueCurve;
    }
    
    bool operator!=(const Params& other) const {
        return !(*this == other);
    }
};

// ─── Processor Class ──────────────────────────────────────────────────────────
class Processor {
public:
    // Apply all adjustments to an ImageBuffer in-place.
    // The buffer must have valid float data (0–1 range, interleaved RGB or mono).
    static void apply(ImageBuffer& buffer, const Params& params);

    // Apply all adjustments and return preview data at reduced resolution.
    // Returns an RGB float vector (interleaved) at the given maxDim.
    static std::vector<float> applyPreview(const ImageBuffer& buffer,
                                           const Params& params,
                                           int maxDim = 800);

    // Check if a curve is the identity curve (default 2 points at 0,0 and 255,255)
    static bool isDefaultCurve(const std::vector<CurvePoint>& points);

private:
    // ── Individual adjustment functions (operate on a single pixel's RGB) ────
    // All work in linear float RGB space.

    // Exposure: color * 2^exposure
    static void applyLinearExposure(float& r, float& g, float& b, float exposure);

    // Brightness (filmic rational curve)
    static void applyFilmicExposure(float& r, float& g, float& b, float brightness);

    // Tonal: contrast, shadows, whites, blacks  (needs blurred luma)
    static void applyTonalAdjustments(float& r, float& g, float& b,
                                      float blurredLuma,
                                      float contrast, float shadows,
                                      float whites, float blacks);

    // Highlights recovery (gamma-based)
    static void applyHighlightsAdjustment(float& r, float& g, float& b,
                                          float blurredLuma, float highlights);

    // White Balance: temperature + tint multipliers
    static void applyWhiteBalance(float& r, float& g, float& b,
                                  float temp, float tint);

    // Color Calibration: per-channel hue and saturation adjustments
    static void applyColorCalibration(float& r, float& g, float& b,
                                      float redHue, float greenHue, float blueHue,
                                      float redSat, float greenSat, float blueSat,
                                      float shadowsTint);

    // Saturation + Vibrance (with skin protection)
    static void applyCreativeColor(float& r, float& g, float& b,
                                   float sat, float vib);

    // HSL Panel (8 color ranges)
    static void applyHSLPanel(float& r, float& g, float& b,
                              const std::array<HslAdjustment, 8>& hsl);

    // Color Grading (three-way shadow/mid/high)
    static void applyColorGrading(float& r, float& g, float& b,
                                  const ColorGradeSettings& shadows,
                                  const ColorGradeSettings& midtones,
                                  const ColorGradeSettings& highlights,
                                  float blending, float balance);

    // Dehaze (dark channel prior)
    static void applyDehaze(float& r, float& g, float& b, float amount);

    // Local Contrast (clarity / structure / sharpness)
    // Operates on pixel + blurred version
    static void applyLocalContrast(float& r, float& g, float& b,
                                   float blurredR, float blurredG, float blurredB,
                                   float amount, int mode, bool isRaw);

    // Curves (cubic Hermite spline)
    static float applyCurve(float val, const std::vector<CurvePoint>& points);
    static void applyAllCurves(float& r, float& g, float& b,
                               const std::vector<CurvePoint>& luma,
                               const std::vector<CurvePoint>& red,
                               const std::vector<CurvePoint>& green,
                               const std::vector<CurvePoint>& blue);

    // Vignette
    static void applyVignette(float& r, float& g, float& b,
                              int x, int y, int w, int h,
                              float amount, float midpoint,
                              float roundness, float feather);

    // Grain (gradient noise)
    static void applyGrain(float& r, float& g, float& b,
                           int x, int y,
                           float amount, float size, float roughness,
                           float scale);

    // Chromatic Aberration (whole-image pass)
    static void applyChromaticAberration(std::vector<float>& data,
                                         int w, int h, int channels,
                                         float caRC, float caBY);

    // ── Color space helpers ──────────────────────────────────────────────────
    static float getLuma(float r, float g, float b);
    static void  rgbToHsv(float r, float g, float b, float& h, float& s, float& v);
    static void  hsvToRgb(float h, float s, float v, float& r, float& g, float& b);
    static float smoothstep(float edge0, float edge1, float x);
    static float hashNoise(float x, float y);
    static float gradientNoise(float px, float py);

    // ── Blur helpers ─────────────────────────────────────────────────────────
    // Generate Gaussian blur of luminance channel for tonal/local contrast ops
    static std::vector<float> computeBlurredLuma(const std::vector<float>& data,
                                                  int w, int h, int channels,
                                                  int radius);
};

} // namespace RawEditor

#endif // RAWEDITORPROCESSOR_H
