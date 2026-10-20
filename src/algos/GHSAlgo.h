#ifndef GHSALGO_H
#define GHSALGO_H

/**
 * @file GHSAlgo.h
 * @brief Generalized Hyperbolic Stretch (GHS) algorithm.
 *
 * Implements the parametric stretch function described by Mike Cranfield,
 * supporting multiple stretch types (Payne normal/inverse, asinh,
 * inverse asinh) with precomputed coefficients for efficient per-pixel
 * evaluation.
 *
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

#include <cmath>
#include <algorithm>
#include <vector>

namespace GHSAlgo {

/**
 * @brief Supported stretch function types.
 */
enum StretchType {
    STRETCH_LINEAR       = 0,   ///< Identity (no stretch).
    STRETCH_PAYNE_NORMAL = 1,   ///< Payne forward stretch.
    STRETCH_PAYNE_INVERSE= 2,   ///< Payne inverse stretch.
    STRETCH_ASINH        = 3,   ///< Inverse hyperbolic sine stretch.
    STRETCH_INVASINH     = 4    ///< Inverse of the asinh stretch.
};

/**
 * @brief Precomputed coefficients for efficient per-pixel stretch evaluation.
 *
 * These coefficients are computed once by @ref setup() and reused for
 * every pixel in the buffer.  The naming follows the mathematical
 * formulation of the GHS piecewise transfer function.
 */
struct GHSComputeParams {
    float qlp, q0, qwp, q1, q;
    float b1, a2, b2, c2, d2, a3, b3, c3, d3, a4, b4;
    float e2, e3;                   ///< Exponents for power-law terms.
    float a1, LPT, SPT, HPT;       ///< Additional piecewise boundaries.
};

/**
 * @brief User-facing stretch parameters.
 */
struct GHSParams {
    float       D, B, SP, LP, HP, BP;
    StretchType type;
};

/**
 * @brief Precompute the piecewise coefficients for a given parameter set.
 *
 * Must be called once before any calls to @ref compute().
 *
 * @param[out] c           Destination for precomputed coefficients.
 * @param      B           Stretch shape parameter.
 * @param      D           Stretch intensity (0 = no stretch).
 * @param      LP          Local (shadow) protection point.
 * @param      SP          Symmetry (midtone) point.
 * @param      HP          Highlight protection point.
 * @param      stretchtype One of the @ref StretchType values.
 */
void setup(GHSComputeParams& c, float B, float D, float LP,
           float SP, float HP, int stretchtype);

/**
 * @brief Evaluate the stretch function for a single pixel value.
 *
 * @param in     Input pixel value (typically in [0, 1]).
 * @param params User-facing stretch parameters.
 * @param c      Precomputed coefficients from @ref setup().
 * @return Stretched pixel value.
 */
float compute(float in, const GHSParams& params, const GHSComputeParams& c);

/**
 * @brief Apply the GHS stretch to an entire data buffer in-place.
 *
 * Internally calls @ref setup() once and then @ref compute() per pixel.
 * Output values are clamped to [0, 1].
 *
 * @param data   Pixel buffer (modified in-place).
 * @param params Stretch parameters.
 */
void applyToBuffer(std::vector<float>& data, const GHSParams& params);

} // namespace GHSAlgo

#endif // GHSALGO_H