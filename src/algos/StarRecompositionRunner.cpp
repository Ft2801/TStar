#include "StarRecompositionRunner.h"
#include <algorithm>
#include <cmath>

StarRecompositionRunner::StarRecompositionRunner(QObject* parent) : QObject(parent) {}

bool StarRecompositionRunner::run(const ImageBuffer& starless, const ImageBuffer& stars, ImageBuffer& output, const StarRecompositionParams& params, QString* errorMsg) {
    if (starless.width() != stars.width() || starless.height() != stars.height()) {
        if (errorMsg) *errorMsg = "Dimensions mismatch between starless and stars image.";
        return false;
    }

    int w = starless.width();
    int h = starless.height();
    int c = starless.channels();
    int starsC = stars.channels();

    output.resize(w, h, std::max(c, starsC));
    
    // Copy the stars buffer so we can modify it
    ImageBuffer stretchedStars = stars;
    
    // Apply the advanced stretch to the star layer
    stretchedStars.applyGHS(params.ghs);

    size_t numPixels = (size_t)w * h;
    int outC = output.channels();

    const float* sllData = starless.data().data();
    const float* strData = stretchedStars.data().data();
    float* outData = output.data().data();

    // Screen: A + B - A*B
    for (size_t i = 0; i < numPixels; ++i) {
        for (int ch = 0; ch < outC; ++ch) {
            float sll = 0.0f;
            if (c == 3) sll = sllData[i * 3 + ch];
            else if (c == 1) sll = sllData[i];

            float str = 0.0f;
            if (starsC == 3) str = strData[i * 3 + ch];
            else if (starsC == 1) str = strData[i];

            // Screen Blend
            float val = sll + str - (sll * str);
            outData[i * outC + ch] = std::max(0.0f, std::min(1.0f, val));
        }
    }

    return true;
}
