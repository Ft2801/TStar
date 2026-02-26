#ifndef CHANNEL_OPS_H
#define CHANNEL_OPS_H

#include "../ImageBuffer.h"
#include <vector>
#include <array>

// ============================================================================
// Continuum Subtraction — Structures
// ============================================================================

// Learned recipe from starry pass, applied to starless pair
struct ContinuumSubtractRecipe {
    float pedestal[3]  = {0, 0, 0};     // Background neutralization pedestal (R,G,B)
    float rnormGain    = 1.0f;          // Red-to-green normalization gain
    float rnormOffset  = 0.0f;          // Red-to-green normalization offset
    float wbA[3]       = {1, 1, 1};     // Per-channel white-balance affine gain
    float wbB[3]       = {0, 0, 0};     // Per-channel white-balance affine offset
    float Q            = 0.8f;          // Q factor used for subtraction
    float greenMedian  = 0.0f;          // Green channel median after WB
    int   starCount    = 0;             // Number of stars used for WB
    bool  valid        = false;         // True if recipe was successfully learned
};

// Parameters for the full continuum subtraction pipeline
struct ContinuumSubtractParams {
    float qFactor       = 0.80f;        // Scale of broadband subtraction (0.1–2.0)
    float starThreshold = 5.0f;         // Sigma threshold for star detection (WB)
    bool  outputLinear  = true;         // If true, output linear; if false, apply stretch+curves
    float targetMedian  = 0.25f;        // Target median for non-linear stretch
    float curvesBoost   = 0.50f;        // Curves boost for non-linear finalization
};

class ChannelOps {
public:
    // Extract RGB channels into 3 separate mono ImageBuffers
    static std::vector<ImageBuffer> extractChannels(const ImageBuffer& src);

    // Combine 3 mono ImageBuffers into one RGB ImageBuffer
    // Returns invalid buffer if inputs are incompatible
    static ImageBuffer combineChannels(const ImageBuffer& r, const ImageBuffer& g, const ImageBuffer& b);


    enum class LumaMethod {
        REC709,
        REC601,
        REC2020,
        AVERAGE,
        MAX,
        MEDIAN,
        SNR,
        CUSTOM
    };

    // Color space used for luminance recombination
    enum class ColorSpaceMode {
        HSL,    // Hue-Saturation-Lightness (default)
        HSV,    // Hue-Saturation-Value
        CIELAB  // CIE L*a*b*
    };

    // Extended computeLuminance with support for Custom weights and SNR
    static ImageBuffer computeLuminance(const ImageBuffer& src, LumaMethod method = LumaMethod::REC709, 
                                        const std::vector<float>& customWeights = {}, 
                                        const std::vector<float>& customNoiseSigma = {});

    // Recombine luminance into target image using color space conversion.
    // Converts each pixel RGB -> color space, replaces lightness/value with sourceL, converts back.
    // This preserves hue and saturation while replacing only the luminance component.
    // target: RGB image to modify
    // sourceL: New luminance (mono)
    // csMode: Color space for the conversion (HSL, HSV, or CIELab)
    // blend: Blend factor 0..1 (0 = no change, 1 = full replacement)
    static bool recombineLuminance(ImageBuffer& target, const ImageBuffer& sourceL, 
                                   ColorSpaceMode csMode = ColorSpaceMode::HSL,
                                   float blend = 1.0f);

    // Helper to estimate noise sigma per channel (for SNR weighting)
    static std::vector<float> estimateNoiseSigma(const ImageBuffer& src);
    
    // Remove pedestal (subtract min per channel)
    static void removePedestal(ImageBuffer& img);

    // Debayer (demosaic) a single-channel Bayer mosaic to RGB
    // pattern: "RGGB", "BGGR", "GRBG", or "GBRG"
    // method: "edge" (edge-aware) or "bilinear"
    static ImageBuffer debayer(const ImageBuffer& mosaic, const std::string& pattern, const std::string& method = "edge");
    
    // Compute score for debayer pattern detection (lower is better)
    static float computeDebayerScore(const ImageBuffer& rgb);
    
    // ========================================================================
    // Continuum Subtraction — Full Pipeline
    // ========================================================================

    // Simple legacy fallback: result = nb - Q * (continuum - median(continuum))
    static ImageBuffer continuumSubtract(const ImageBuffer& narrowband, const ImageBuffer& continuum, float qFactor = 0.8f);

    // Full pipeline: BG neutralization → red-to-green normalization →
    //                star-based WB → linear subtraction → optional stretch
    // If recipe != nullptr, the learned parameters are stored for reuse on starless.
    static ImageBuffer continuumSubtractFull(
        const ImageBuffer& narrowband,
        const ImageBuffer& continuum,
        const ContinuumSubtractParams& params,
        ContinuumSubtractRecipe* recipe = nullptr);

    // Apply a previously learned recipe to a starless NB+continuum pair.
    static ImageBuffer continuumSubtractWithRecipe(
        const ImageBuffer& narrowband,
        const ImageBuffer& continuum,
        const ContinuumSubtractRecipe& recipe,
        bool outputLinear = true,
        float targetMedian = 0.25f,
        float curvesBoost  = 0.50f);

    // ---- Sub-steps (exposed for advanced callers) ----

    // Assemble NB (mono) + continuum (mono) into an RGB composite: R=NB, G=Cont, B=Cont
    // Both inputs are converted to mono first if multi-channel.
    static void assembleNBContRGB(const ImageBuffer& nb, const ImageBuffer& cont,
                                  std::vector<float>& rgbOut, int& w, int& h);

    // Background neutralization via walking dark-box algorithm
    // (200 random boxes, 25 iterations, walk toward darkest regions)
    static void computeBackgroundPedestal(const std::vector<float>& rgb, int w, int h,
                                          float pedestal[3]);
    static void applyPedestal(std::vector<float>& rgb, int w, int h,
                              const float pedestal[3]);

    // Normalize red channel to match green channel statistics (MAD & median)
    static void normalizeRedToGreen(std::vector<float>& rgb, int w, int h,
                                    float& gain, float& offset);

    // Star-based white balance: detect stars, measure per-channel flux,
    // compute affine correction to neutralize stellar colors.
    // Returns number of stars used. wbA[3] and wbB[3] are the affine params.
    static int starBasedWhiteBalance(std::vector<float>& rgb, int w, int h,
                                     float threshold,
                                     float wbA[3], float wbB[3]);

    // Linear continuum subtraction on processed RGB data.
    // result = R - Q * (G - greenMedian), output is mono float [0,1].
    static void linearContinuumSubtract(const std::vector<float>& rgb, int w, int h,
                                        float Q, float greenMedian,
                                        std::vector<float>& result);

    // Non-linear finalization: statistical stretch → pedestal subtract → curves.
    static void nonLinearFinalize(std::vector<float>& data, int w, int h,
                                  float targetMedian = 0.25f, float curvesBoost = 0.50f);

    // ========================================================================
    // Multiscale Decomposition
    // ========================================================================

    // Per-layer configuration
    struct LayerCfg {
        bool  enabled   = true;
        float biasGain  = 1.0f;   // 1.0 = unchanged, >1 boosts, <1 reduces
        float thr       = 0.0f;   // soft threshold in σ units
        float amount    = 0.0f;   // blend toward thresholded (0..1)
        float denoise   = 0.0f;   // multiscale NR strength (0..1)
    };

    // Decompose image into Gaussian pyramid detail layers + residual.
    // Input: float [0,1] interleaved, any channel count.
    // Returns detail layers (wavelet-like difference layers) and residual (coarsest).
    static void multiscaleDecompose(const std::vector<float>& img, int w, int h, int ch,
                                    int layers, float baseSigma,
                                    std::vector<std::vector<float>>& details,
                                    std::vector<float>& residual);

    // Reconstruct image from detail layers + residual.
    static std::vector<float> multiscaleReconstruct(const std::vector<std::vector<float>>& details,
                                                    const std::vector<float>& residual,
                                                    int pixelCount);

    // Soft threshold: sign(x) * max(0, |x| - t)
    static void softThreshold(std::vector<float>& data, float t);

    // Robust sigma estimate (MAD-based) for a detail layer
    static float robustSigma(const std::vector<float>& data);

    // Apply per-layer operations: denoise, threshold, gain
    // mode: 0 = μ-σ Thresholding, 1 = Linear (only gain applied)
    static void applyLayerOps(std::vector<float>& layer, const LayerCfg& cfg,
                              float sigma, int layerIndex, int mode = 0);

    // ========================================================================
    // Narrowband Normalization
    // ========================================================================

    struct NBNParams {
        int   scenario   = 0;     // 0=SHO, 1=HSO, 2=HOS, 3=HOO
        int   mode       = 1;     // 0=linear, 1=non-linear
        int   lightness  = 0;     // 0=off, 1=original, 2=Ha, 3=SII(or OIII for HOO), 4=OIII
        float blackpoint = 0.25f; // 0..1
        float hlrecover  = 1.0f;  // 0.5..2.0
        float hlreduct   = 1.0f;  // 0.5..2.0
        float brightness = 1.0f;  // 0.5..2.0
        int   blendmode  = 0;     // HOO only: 0=Screen, 1=Add, 2=LinearDodge, 3=Normal
        float hablend    = 0.6f;  // HOO Ha blend ratio 0..1
        float oiiiboost  = 1.0f;  // HOO OIII boost 0.5..2
        float siiboost   = 1.0f;  // SHO-family SII boost 0.5..2
        float oiiiboost2 = 1.0f;  // SHO-family OIII boost 0.5..2
        bool  scnr       = true;  // apply green SCNR
    };

    // Normalize narrowband channels to RGB.
    // ha, oiii, sii: mono float32 [0,1] same dimensions (w*h). sii may be empty for HOO.
    // Output: interleaved RGB float32 (w*h*3).
    static std::vector<float> normalizeNarrowband(const std::vector<float>& ha,
                                                   const std::vector<float>& oiii,
                                                   const std::vector<float>& sii,
                                                   int w, int h,
                                                   const NBNParams& params);

    // ========================================================================
    // NB → RGB Stars
    // ========================================================================

    struct NBStarsParams {
        float ratio         = 0.30f;  // Ha:OIII blend ratio (0..1)
        bool  starStretch   = true;   // enable star stretch
        float stretchFactor = 5.0f;   // stretch exponent
        float saturation    = 1.0f;   // saturation multiplier
        bool  applySCNR     = true;   // green SCNR
    };

    // Combine narrowband channels to RGB stars image.
    // ha, oiii: mono float32 [0,1]. sii, osc may be empty.
    // osc is interleaved RGB if present.
    // Output: interleaved RGB float32 (w*h*3).
    static std::vector<float> combineNBtoRGBStars(const std::vector<float>& ha,
                                                   const std::vector<float>& oiii,
                                                   const std::vector<float>& sii,
                                                   const std::vector<float>& osc,
                                                   int w, int h, int oscChannels,
                                                   const NBStarsParams& params);

    // SCNR (Subtractive Chromatic Noise Reduction) for green channel
    static void applySCNR(std::vector<float>& rgb, int w, int h);

    // Adjust saturation of RGB image
    static void adjustSaturation(std::vector<float>& rgb, int w, int h, float factor);

private:
    static float getLumaWeightR(LumaMethod method, const std::vector<float>& customWeights = {});
    static float getLumaWeightG(LumaMethod method, const std::vector<float>& customWeights = {});
    static float getLumaWeightB(LumaMethod method, const std::vector<float>& customWeights = {});

    // Gaussian blur helper for multiscale decomposition
    static void gaussianBlur(const std::vector<float>& src, std::vector<float>& dst,
                             int w, int h, int ch, float sigma);

    // Channel normalization helper for NBN
    static void normalizeChannel(std::vector<float>& ch, int n, float blackpoint, int mode);
};

#endif // CHANNEL_OPS_H
