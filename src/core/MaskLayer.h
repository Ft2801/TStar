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
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

#ifndef MASKLAYER_H
#define MASKLAYER_H

// ============================================================================
// MaskLayer.h
// Data structure representing a single mask layer for selective processing.
// Stores normalized float data [0.0, 1.0] with blending and visibility state.
// ============================================================================

#include <vector>
#include <QString>

/**
 * @brief A single mask layer with pixel data and compositing properties.
 *
 * Masks are used to selectively apply processing operations to regions
 * of an image. Multiple layers can be composed via the MaskManager.
 */
struct MaskLayer {
    std::vector<float> data;        ///< Normalized mask values [0.0, 1.0]
    int     width    = 0;
    int     height   = 0;
    QString id;                     ///< Unique identifier
    QString name;                   ///< Human-readable display name
    QString mode     = "replace";   ///< Compositing mode ("replace", "multiply", etc.)
    bool    inverted = false;       ///< Whether the mask is logically inverted
    bool    visible  = true;        ///< Whether the mask is active in composition
    float   opacity  = 1.0f;        ///< Opacity factor [0.0, 1.0]

    /** Check whether the mask contains valid, properly-sized data. */
    bool isValid() const
    {
        return !data.empty()
            && width > 0
            && height > 0
            && data.size() == static_cast<size_t>(width * height);
    }

    /** Safely access a pixel value with bounds checking. */
    float pixel(int x, int y) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return 0.0f;
        return data[y * width + x];
    }
};

#endif // MASKLAYER_H