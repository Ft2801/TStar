#ifndef DRIZZLE_MAP_H
#define DRIZZLE_MAP_H

/**
 * @file DrizzleMap.h
 * @brief Coordinate mapping for drizzle integration.
 *
 * Stores the output (x', y') coordinate grid for an input image's pixels.
 * Used to perform high-precision sub-pixel interpolation when mapping
 * shrunk pixels (pixfrac < 1.0) or non-rectilinear transforms.
 */

#include <vector>

namespace Stacking {

/**
 * @brief Coordinate map from input pixel centers to output pixel positions.
 */
struct DrizzleMap {
    std::vector<float> xMap; ///< Output X coordinates [width * height]
    std::vector<float> yMap; ///< Output Y coordinates [width * height]
    int width  = 0;          ///< Input image width
    int height = 0;          ///< Input image height

    bool isValid() const {
        return !xMap.empty() && !yMap.empty() && width > 0 && height > 0;
    }

    float getX(int x, int y) const { return xMap[static_cast<size_t>(y) * width + x]; }
    float getY(int x, int y) const { return yMap[static_cast<size_t>(y) * width + x]; }
};

} // namespace Stacking

#endif // DRIZZLE_MAP_H
