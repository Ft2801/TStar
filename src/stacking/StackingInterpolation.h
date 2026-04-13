/**
 * @file StackingInterpolation.h
 * @brief Pixel interpolation utilities for sub-pixel image resampling.
 *
 * Provides inline cubic and bicubic interpolation routines used during
 * image registration and stacking to sample pixel values at fractional
 * coordinates.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#ifndef STACKING_INTERPOLATION_H
#define STACKING_INTERPOLATION_H

#include <cmath>
#include <algorithm>

namespace Stacking {

/**
 * @brief Evaluate a 1-D cubic polynomial through four equally-spaced samples.
 *
 * Uses the Catmull-Rom formulation:
 *   p(x) = p*x^3 + q*x^2 + r*x + s
 * where the coefficients are derived from the four input values.
 *
 * @param v0 Sample at position -1
 * @param v1 Sample at position  0 (left of interpolation interval)
 * @param v2 Sample at position +1 (right of interpolation interval)
 * @param v3 Sample at position +2
 * @param x  Fractional position within [0, 1)
 * @return   Interpolated value
 */
inline float cubic(float v0, float v1, float v2, float v3, float x)
{
    const float p = (v3 - v2) - (v0 - v1);
    const float q = (v0 - v1) - p;
    const float r = v2 - v0;
    const float s = v1;

    return p * x * x * x + q * x * x + r * x + s;
}

/**
 * @brief Sample a pixel value from a multi-channel image using bicubic interpolation.
 *
 * Performs separable bicubic interpolation over a 4x4 neighbourhood.
 * When the requested position falls within one pixel of the image border,
 * the function falls back to nearest-neighbour sampling to avoid
 * out-of-bounds access.
 *
 * @param data     Pointer to interleaved image data (channels are contiguous per pixel)
 * @param width    Image width in pixels
 * @param height   Image height in pixels
 * @param x        Horizontal coordinate (may be fractional)
 * @param y        Vertical coordinate (may be fractional)
 * @param channel  Zero-based channel index to sample
 * @param channels Total number of interleaved channels
 * @return         Interpolated pixel value for the requested channel
 */
inline float interpolateBicubic(const float* data, int width, int height,
                                double x, double y, int channel, int channels)
{
    /* Integer and fractional decomposition of the coordinates. */
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));

    const float dx = static_cast<float>(x - ix);
    const float dy = static_cast<float>(y - iy);

    /*
     * Border guard: the 4x4 kernel requires one extra pixel on each side.
     * If we are too close to the edge, fall back to nearest-neighbour.
     */
    if (ix < 1 || ix >= width - 2 || iy < 1 || iy >= height - 2) {
        const int cx = std::min(std::max(0, static_cast<int>(std::round(x))), width  - 1);
        const int cy = std::min(std::max(0, static_cast<int>(std::round(y))), height - 1);
        return data[(cy * width + cx) * channels + channel];
    }

    /*
     * Gather the 4x4 pixel neighbourhood, interpolate each row horizontally
     * with the cubic kernel, then interpolate the four results vertically.
     */
    float rows[4];
    for (int j = -1; j <= 2; ++j) {
        const int ry = iy + j;

        const float p0 = data[((ry * width) + (ix - 1)) * channels + channel];
        const float p1 = data[((ry * width) + (ix    )) * channels + channel];
        const float p2 = data[((ry * width) + (ix + 1)) * channels + channel];
        const float p3 = data[((ry * width) + (ix + 2)) * channels + channel];

        rows[j + 1] = cubic(p0, p1, p2, p3, dx);
    }

    return cubic(rows[0], rows[1], rows[2], rows[3], dy);
}

} // namespace Stacking

#endif // STACKING_INTERPOLATION_H