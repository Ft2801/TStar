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
        // REC2020, // Can add later
        AVERAGE,
        MAX
    };

    static ImageBuffer computeLuminance(const ImageBuffer& src, LumaMethod method = LumaMethod::REC709);

    // Debayer (demosaic) a single-channel Bayer mosaic to RGB
    // pattern: "RGGB", "BGGR", "GRBG", or "GBRG"
    // method: "edge" (edge-aware) or "bilinear"
    static ImageBuffer debayer(const ImageBuffer& mosaic, const std::string& pattern, const std::string& method = "edge");
    
    // Compute score for debayer pattern detection (lower is better)
    static float computeDebayerScore(const ImageBuffer& rgb);
    
    // Continuum subtraction: result = nb - Q * (continuum - median(continuum))
    static ImageBuffer continuumSubtract(const ImageBuffer& narrowband, const ImageBuffer& continuum, float qFactor = 0.8f);

private:
    static float getLumaWeightR(LumaMethod method);
    static float getLumaWeightG(LumaMethod method);
    static float getLumaWeightB(LumaMethod method);
};

#endif // CHANNEL_OPS_H
