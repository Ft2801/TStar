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
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
 */

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
