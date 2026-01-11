#ifndef STACKING_INTERPOLATION_H
#define STACKING_INTERPOLATION_H

#include <cmath>
#include <algorithm>

namespace Stacking {

inline float cubic(float v0, float v1, float v2, float v3, float x) {
    float p = (v3 - v2) - (v0 - v1);
    float q = (v0 - v1) - p;
    float r = v2 - v0;
    float s = v1;
    return p * x * x * x + q * x * x + r * x + s;
}

/**
 * @brief Get pixel value with bicubic interpolation
 * @param data Image data pointer
 * @param width Image width
 * @param height Image height
 * @param x X coordinate (can be fractional)
 * @param y Y coordinate (can be fractional)
 * @param channel Channel index
 * @param channels Total channels
 * @return Interpolated pixel value
 */
inline float interpolateBicubic(const float* data, int width, int height, 
                              double x, double y, int channel, int channels) 
{
    // Integer part
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    
    // Fractional part
    float dx = static_cast<float>(x - ix);
    float dy = static_cast<float>(y - iy);
    
    // Clamp to valid range (with 1px margin for 4x4 kernel)
    // If close to edge, fallback to bilinear or clamp coordinate
    if (ix < 1 || ix >= width - 2 || iy < 1 || iy >= height - 2) {
        // Simple clamp and nearest neighbor for border safety
        int cx = std::min(std::max(0, static_cast<int>(std::round(x))), width - 1);
        int cy = std::min(std::max(0, static_cast<int>(std::round(y))), height - 1);
        return data[(cy * width + cx) * channels + channel];
    }
    
    // Gather 4x4 neighborhood
    float rows[4];
    for (int j = -1; j <= 2; ++j) {
        int ry = iy + j;
        float p0 = data[((ry * width) + (ix - 1)) * channels + channel];
        float p1 = data[((ry * width) + (ix)) * channels + channel];
        float p2 = data[((ry * width) + (ix + 1)) * channels + channel];
        float p3 = data[((ry * width) + (ix + 2)) * channels + channel];
        
        rows[j + 1] = cubic(p0, p1, p2, p3, dx);
    }
    
    return cubic(rows[0], rows[1], rows[2], rows[3], dy);
}

} // namespace Stacking

#endif // STACKING_INTERPOLATION_H
