#ifndef CHANNEL_OPS_H
#define CHANNEL_OPS_H

#include "../ImageBuffer.h"
#include <vector>

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
    
    // Continuum subtraction: result = nb - Q * (continuum - median(continuum))
    static ImageBuffer continuumSubtract(const ImageBuffer& narrowband, const ImageBuffer& continuum, float qFactor = 0.8f);

private:
    static float getLumaWeightR(LumaMethod method, const std::vector<float>& customWeights = {});
    static float getLumaWeightG(LumaMethod method, const std::vector<float>& customWeights = {});
    static float getLumaWeightB(LumaMethod method, const std::vector<float>& customWeights = {});
};

#endif // CHANNEL_OPS_H
