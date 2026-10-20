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

#ifndef CHANNEL_OPS_H
#define CHANNEL_OPS_H

// ============================================================================
// ChannelOps.h
// Channel-level image operations for astronomical image processing.
// Includes: channel extraction/combination, luminance computation,
// debayering, continuum subtraction, multiscale decomposition,
// narrowband normalization, and NB-to-RGB star color synthesis.
// ============================================================================

#include "../ImageBuffer.h"

#include <vector>
#include <array>
#include <string>

// ============================================================================
// Continuum Subtraction - Data Structures
// ============================================================================

/**
 * @brief Learned calibration recipe from a starry NB+continuum pass.
 *        Can be reapplied to a corresponding starless pair for consistency.
 */
struct ContinuumSubtractRecipe {
    float pedestal[3]   = {0, 0, 0};   // Background neutralization pedestal (R, G, B)
    float rnormGain     = 1.0f;        // Red-to-green normalization gain
    float rnormOffset   = 0.0f;        // Red-to-green normalization offset
    float wbA[3]        = {1, 1, 1};   // Per-channel white-balance affine gain
    float wbB[3]        = {0, 0, 0};   // Per-channel white-balance affine offset
    float Q             = 0.8f;        // Q factor used for subtraction
    float greenMedian   = 0.0f;        // Green channel median after white balance
    int   starCount     = 0;           // Number of stars used for white balance
    bool  valid         = false;       // True if recipe was successfully learned
};

/**
 * @brief Parameters for the full continuum subtraction pipeline.
 */
struct ContinuumSubtractParams {
    float qFactor       = 0.80f;       // Scale of broadband subtraction (0.1 - 2.0)
    float starThreshold = 5.0f;        // Sigma threshold for star detection (WB step)
    bool  outputLinear  = true;        // If true, output is linear; otherwise stretched
    float targetMedian  = 0.25f;       // Target median for non-linear stretch
    float curvesBoost   = 0.50f;       // Curves boost for non-linear finalization
};

// ============================================================================
// ChannelOps class
// ============================================================================

class ChannelOps {
public:

    // ========================================================================
    // Channel extraction and combination
    // ========================================================================

    /**
     * @brief Extract R, G, B channels from an RGB image into separate mono buffers.
     * @param src  Source RGB image (must have >= 3 channels).
     * @return Vector of 3 mono ImageBuffers, or empty on failure.
     */
    static std::vector<ImageBuffer> extractChannels(const ImageBuffer& src);

    /**
     * @brief Combine 3 mono ImageBuffers into a single RGB ImageBuffer.
     * @return Combined RGB buffer, or invalid buffer if dimensions mismatch.
     */
    static ImageBuffer combineChannels(const ImageBuffer& r,
                                       const ImageBuffer& g,
                                       const ImageBuffer& b);

    // ========================================================================
    // Luminance computation
    // ========================================================================

    /**
     * @brief Method for computing luminance from multi-channel data.
     */
    enum class LumaMethod {
        REC709,   // ITU-R BT.709
        REC601,   // ITU-R BT.601
        REC2020,  // ITU-R BT.2020
        AVERAGE,  // Equal weights (1/3 each)
        MAX,      // Maximum of all channels
        MEDIAN,   // Median of all channels
        SNR,      // Inverse-variance weighting based on noise
        CUSTOM    // User-provided weights
    };

    /**
     * @brief Color space used for luminance recombination.
     */
    enum class ColorSpaceMode {
        HSL,    // Hue-Saturation-Lightness (default)
        HSV,    // Hue-Saturation-Value
        CIELAB  // CIE L*a*b*
    };

    /**
     * @brief Compute a single-channel luminance image from a multi-channel source.
     * @param src               Source image.
     * @param method            Luminance weighting method.
     * @param customWeights     Custom per-channel weights (used with CUSTOM method).
     * @param customNoiseSigma  Pre-computed noise sigmas (used with SNR method).
     * @return Mono luminance image.
     */
    static ImageBuffer computeLuminance(
        const ImageBuffer& src,
        LumaMethod method = LumaMethod::REC709,
        const std::vector<float>& customWeights = {},
        const std::vector<float>& customNoiseSigma = {});

    /**
     * @brief Replace the luminance component of an RGB image while preserving
     *        hue and saturation, using the specified color space conversion.
     * @param target   RGB image to modify (in-place).
     * @param sourceL  New luminance image (mono).
     * @param csMode   Color space for the conversion.
     * @param blend    Blend factor: 0.0 = no change, 1.0 = full replacement.
     * @return true on success, false on dimension or channel mismatch.
     */
    static bool recombineLuminance(
        ImageBuffer& target,
        const ImageBuffer& sourceL,
        ColorSpaceMode csMode = ColorSpaceMode::HSL,
        float blend = 1.0f);

    /**
     * @brief Estimate per-channel noise sigma using the MAD estimator.
     * @param src  Source image.
     * @return Vector of sigma values, one per channel.
     */
    static std::vector<float> estimateNoiseSigma(const ImageBuffer& src);

    /**
     * @brief Remove pedestal by subtracting the per-channel minimum value.
     */
    static void removePedestal(ImageBuffer& img);

    // ========================================================================
    // Debayering
    // ========================================================================

    /**
     * @brief Debayer (demosaic) a single-channel Bayer mosaic image to RGB.
     * @param mosaic   Mono Bayer mosaic image.
     * @param pattern  Bayer pattern: "RGGB", "BGGR", "GRBG", or "GBRG".
     * @param method   Interpolation method: "edge" (edge-aware) or "bilinear".
     * @return Debayered RGB image.
     */
    static ImageBuffer debayer(const ImageBuffer& mosaic,
                               const std::string& pattern,
                               const std::string& method = "edge");

    /**
     * @brief Compute a quality score for debayer pattern detection.
     *        Lower score indicates a better (more correct) pattern.
     */
    static float computeDebayerScore(const ImageBuffer& rgb);

    // ========================================================================
    // Continuum Subtraction - Full Pipeline
    // ========================================================================

    /**
     * @brief Simple legacy continuum subtraction.
     *        result = NB - Q * (continuum - median(continuum))
     */
    static ImageBuffer continuumSubtract(
        const ImageBuffer& narrowband,
        const ImageBuffer& continuum,
        float qFactor = 0.8f);

    /**
     * @brief Full continuum subtraction pipeline:
     *        BG neutralization -> red-to-green normalization ->
     *        star-based WB -> linear subtraction -> optional stretch.
     * @param recipe  If non-null, the learned parameters are stored for reuse.
     */
    static ImageBuffer continuumSubtractFull(
        const ImageBuffer& narrowband,
        const ImageBuffer& continuum,
        const ContinuumSubtractParams& params,
        ContinuumSubtractRecipe* recipe = nullptr);

    /**
     * @brief Apply a previously learned recipe to a starless NB+continuum pair.
     */
    static ImageBuffer continuumSubtractWithRecipe(
        const ImageBuffer& narrowband,
        const ImageBuffer& continuum,
        const ContinuumSubtractRecipe& recipe,
        bool outputLinear = true,
        float targetMedian = 0.25f,
        float curvesBoost = 0.50f);

    // ---- Sub-steps (exposed for advanced callers) ----

    /**
     * @brief Assemble NB + continuum into an RGB composite: R=NB, G=Cont, B=Cont.
     *        Both inputs are converted to mono first if multi-channel.
     */
    static void assembleNBContRGB(const ImageBuffer& nb,
                                  const ImageBuffer& cont,
                                  std::vector<float>& rgbOut,
                                  int& w, int& h);

    /**
     * @brief Compute background pedestal via a walking dark-box algorithm.
     *        Uses 200 random boxes of 25x25 pixels, 25 walk iterations.
     */
    static void computeBackgroundPedestal(const std::vector<float>& rgb,
                                          int w, int h,
                                          float pedestal[3]);

    /**
     * @brief Apply a per-channel additive pedestal to RGB data.
     */
    static void applyPedestal(std::vector<float>& rgb, int w, int h,
                              const float pedestal[3]);

    /**
     * @brief Normalize the red channel to match green channel statistics
     *        using MAD and median matching.
     */
    static void normalizeRedToGreen(std::vector<float>& rgb, int w, int h,
                                    float& gain, float& offset);

    /**
     * @brief Star-based white balance: detect stars, measure per-channel flux,
     *        compute affine correction to neutralize stellar colors.
     * @return Number of stars used for calibration.
     */
    static int starBasedWhiteBalance(std::vector<float>& rgb, int w, int h,
                                     float threshold,
                                     float wbA[3], float wbB[3]);

    /**
     * @brief Linear continuum subtraction on processed RGB data.
     *        result[i] = R[i] - Q * (G[i] - greenMedian), output is mono [0,1].
     */
    static void linearContinuumSubtract(const std::vector<float>& rgb,
                                        int w, int h,
                                        float Q, float greenMedian,
                                        std::vector<float>& result);

    /**
     * @brief Non-linear finalization: statistical stretch -> pedestal
     *        subtraction -> curves adjustment.
     */
    static void nonLinearFinalize(std::vector<float>& data, int w, int h,
                                  float targetMedian = 0.25f,
                                  float curvesBoost = 0.50f);

    // ========================================================================
    // Multiscale Decomposition
    // ========================================================================

    /**
     * @brief Per-layer configuration for multiscale processing.
     */
    struct LayerCfg {
        bool  enabled  = true;
        float biasGain = 1.0f;   // 1.0 = unchanged, >1 boosts, <1 reduces
        float thr      = 0.0f;   // Soft threshold in sigma units
        float amount   = 0.0f;   // Blend toward thresholded version (0..1)
        float denoise  = 0.0f;   // Multiscale noise reduction strength (0..1)
    };

    /**
     * @brief Decompose an image into Gaussian pyramid detail layers + residual.
     * @param img        Interleaved float [0,1] image data.
     * @param w, h, ch   Image dimensions and channel count.
     * @param layers     Number of detail layers to extract.
     * @param baseSigma  Base Gaussian sigma (doubled per layer).
     * @param details    Output detail layers (wavelet-like differences).
     * @param residual   Output coarsest residual.
     */
    static void multiscaleDecompose(const std::vector<float>& img,
                                    int w, int h, int ch,
                                    int layers, float baseSigma,
                                    std::vector<std::vector<float>>& details,
                                    std::vector<float>& residual);

    /**
     * @brief Reconstruct an image from detail layers + residual.
     */
    static std::vector<float> multiscaleReconstruct(
        const std::vector<std::vector<float>>& details,
        const std::vector<float>& residual,
        int pixelCount);

    /**
     * @brief Apply soft thresholding: sign(x) * max(0, |x| - t).
     */
    static void softThreshold(std::vector<float>& data, float t);

    /**
     * @brief Compute a robust noise estimate (MAD-based sigma) for a data vector.
     */
    static float robustSigma(const std::vector<float>& data);

    /**
     * @brief Apply per-layer operations: denoise, threshold, and gain.
     * @param mode  0 = sigma-based thresholding, 1 = linear (gain only).
     */
    static void applyLayerOps(std::vector<float>& layer,
                              const LayerCfg& cfg,
                              float sigma, int layerIndex,
                              int mode = 0);

    // ========================================================================
    // Narrowband Normalization
    // ========================================================================

    /**
     * @brief Parameters for narrowband channel normalization and RGB mapping.
     */
    struct NBNParams {
        int   scenario   = 0;       // 0=SHO, 1=HSO, 2=HOS, 3=HOO
        int   mode       = 1;       // 0=linear, 1=non-linear
        int   lightness  = 0;       // 0=off, 1=original, 2=Ha, 3=SII (or OIII for HOO), 4=OIII
        float blackpoint = 0.25f;   // Black point clipping level (0..1)
        float hlrecover  = 1.0f;    // Highlight recovery factor (0.5..2.0)
        float hlreduct   = 1.0f;    // Highlight reduction factor (0.5..2.0)
        float brightness = 1.0f;    // Brightness multiplier (0.5..2.0)
        int   blendmode  = 0;       // HOO only: 0=Screen, 1=Add, 2=LinearDodge, 3=Normal
        float hablend    = 0.6f;    // HOO Ha blend ratio (0..1)
        float oiiiboost  = 1.0f;    // HOO OIII boost (0.5..2)
        float siiboost   = 1.0f;    // SHO-family SII boost (0.5..2)
        float oiiiboost2 = 1.0f;    // SHO-family OIII boost (0.5..2)
        bool  scnr       = true;    // Apply green SCNR after mapping
    };

    /**
     * @brief Normalize narrowband channels and map to RGB.
     * @param ha, oiii, sii  Mono float [0,1] channels (sii may be empty for HOO).
     * @param w, h           Image dimensions.
     * @param params         Normalization and mapping parameters.
     * @return Interleaved RGB float data (w*h*3).
     */
    static std::vector<float> normalizeNarrowband(
        const std::vector<float>& ha,
        const std::vector<float>& oiii,
        const std::vector<float>& sii,
        int w, int h,
        const NBNParams& params);

    // ========================================================================
    // NB -> RGB Stars
    // ========================================================================

    /**
     * @brief Parameters for combining narrowband data into RGB star colors.
     */
    struct NBStarsParams {
        float ratio         = 0.30f;   // Ha:OIII blend ratio (0..1)
        bool  starStretch   = true;    // Enable non-linear star stretch
        float stretchFactor = 5.0f;    // Stretch exponent
        float saturation    = 1.0f;    // Saturation multiplier
        bool  applySCNR     = true;    // Apply green SCNR
    };

    /**
     * @brief Combine narrowband channels into an RGB stars image.
     * @param ha, oiii     Mono float [0,1] channels.
     * @param sii, osc     Optional channels (sii may be empty; osc is interleaved RGB).
     * @param w, h         Image dimensions.
     * @param oscChannels  Number of channels in the OSC data.
     * @param params       Combination parameters.
     * @return Interleaved RGB float data (w*h*3).
     */
    static std::vector<float> combineNBtoRGBStars(
        const std::vector<float>& ha,
        const std::vector<float>& oiii,
        const std::vector<float>& sii,
        const std::vector<float>& osc,
        int w, int h, int oscChannels,
        const NBStarsParams& params);

    /**
     * @brief Subtractive Chromatic Noise Reduction for the green channel.
     *        Applies average neutral protection: G = min(G, (R+B)/2).
     */
    static void applySCNR(std::vector<float>& rgb, int w, int h);

    /**
     * @brief Adjust color saturation of an interleaved RGB image.
     * @param factor  Saturation multiplier (1.0 = no change).
     */
    static void adjustSaturation(std::vector<float>& rgb, int w, int h,
                                 float factor);

private:
    // Luminance weight helpers for standard methods
    static float getLumaWeightR(LumaMethod method,
                                const std::vector<float>& customWeights = {});
    static float getLumaWeightG(LumaMethod method,
                                const std::vector<float>& customWeights = {});
    static float getLumaWeightB(LumaMethod method,
                                const std::vector<float>& customWeights = {});

    // Separable Gaussian blur for multiscale decomposition
    static void gaussianBlur(const std::vector<float>& src,
                             std::vector<float>& dst,
                             int w, int h, int ch, float sigma);

    // Per-channel normalization helper for narrowband processing
    static void normalizeChannel(std::vector<float>& ch, int n,
                                 float blackpoint, int mode);
};

#endif // CHANNEL_OPS_H