#ifndef XTRANSDEMOSAIC_H
#define XTRANSDEMOSAIC_H

#include "../ImageBuffer.h"
#include <vector>

namespace Preprocessing {

class XTransDemosaic {
public:
    enum class Algorithm {
        Markesteijn,
        VNG
    };

    /**
     * @brief Demosaic an X-Trans image
     * @param input Single-channel CFA image
     * @param output Three-channel RGB image
     * @param algo Algorithm to use
     * @return Success
     */
    static bool demosaic(const ImageBuffer& input, ImageBuffer& output, Algorithm algo = Algorithm::Markesteijn);
    
private:
    static void interpolateMarkesteijn(const ImageBuffer& input, ImageBuffer& output);
    static void interpolateVNG(const ImageBuffer& input, ImageBuffer& output);
    
    // 6x6 Pattern Helper
    static int getPixelType(int x, int y); // 0=G, 1=R, 2=B
};

} // namespace Preprocessing

#endif // XTRANSDEMOSAIC_H
