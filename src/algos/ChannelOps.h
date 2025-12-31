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

private:
    static float getLumaWeightR(LumaMethod method);
    static float getLumaWeightG(LumaMethod method);
    static float getLumaWeightB(LumaMethod method);
};

#endif // CHANNEL_OPS_H
