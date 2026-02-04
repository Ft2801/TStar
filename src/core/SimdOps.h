#ifndef SIMDOPS_H
#define SIMDOPS_H

#include <cstddef>
#include <cstdint>

namespace SimdOps {

    /**
     * @brief Structure to hold STF parameters for RGB channels.
     * All arrays are strict [R, G, B].
     */
    struct STFParams {
        float shadow[3];    // c0
        float midtones[3];  // m
        float invRange[3];  // 1.0 / (highlight - shadow)
    };

    /**
     * @brief Apply Gain to an interleaved RGB float buffer (in-place).
     * @param data Pointer to float RGB buffer.
     * @param numPixels Number of pixels (total floats = numPixels * 3).
     * @param r Red gain.
     * @param g Green gain.
     * @param b Blue gain.
     */
    void applyGainRGB(float* data, size_t numPixels, float r, float g, float b);

    /**
     * @brief Apply STF (MTF stretch) to a row of pixels and convert to 8-bit RGB.
     * Handles 24-bit HD stretch logic with AVX2 optimization.
     * 
     * @param src Pointer to source float RGB row.
     * @param dst Pointer to destination uchar RGB row (QImage scanline).
     * @param numPixels Number of pixels in the row.
     * @param params STF parameters.
     * @param inverted If true, invert the result (1.0 - val).
     */
    void applySTF_Row(const float* src, uint8_t* dst, size_t numPixels, const STFParams& params, bool inverted);

}

#endif // SIMDOPS_H
