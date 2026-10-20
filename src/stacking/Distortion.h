#ifndef DISTORTION_H
#define DISTORTION_H

/**
 * @file Distortion.h
 * @brief SIP (Simple Imaging Polynomial) distortion handling.
 *
 * Implements forward and reverse SIP transformations for correcting
 * optical distortion in astronomical images, as well as the full
 * coordinate pipeline: pixel -> linearized -> homography -> linearized -> pixel
 * for mapping between reference and source frames.
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

#include "StackingTypes.h"
#include <vector>
#include <QPointF>

namespace Stacking {

class Distortion {
public:

    /**
     * @brief Apply forward SIP distortion (pixel -> intermediate/linear).
     *
     * x' = x + A(x, y),  y' = y + B(x, y)
     *
     * @param p    Input pixel coordinates.
     * @param reg  Registration data containing SIP A and B polynomials.
     * @return Linearized (distortion-corrected) coordinates.
     */
    static QPointF applyForward(const QPointF& p, const RegistrationData& reg);

    /**
     * @brief Apply reverse SIP distortion (intermediate/linear -> pixel).
     *
     * x = x' + AP(x', y'),  y = y' + BP(x', y')
     *
     * @param p    Linearized coordinates.
     * @param reg  Registration data containing SIP AP and BP polynomials.
     * @return Pixel coordinates in the distorted frame.
     */
    static QPointF applyReverse(const QPointF& p, const RegistrationData& reg);

    /**
     * @brief Evaluate a 2D SIP polynomial: sum(c[p][q] * u^p * v^q).
     *
     * @param u       First coordinate.
     * @param v       Second coordinate.
     * @param order   Maximum polynomial order (p + q <= order).
     * @param coeffs  2D coefficient array indexed as coeffs[p][q].
     * @return Polynomial value.
     */
    static double computePoly(double u, double v, int order,
                              const std::vector<std::vector<double>>& coeffs);

    /**
     * @brief Full coordinate transform from output (reference) pixel to input (source) pixel.
     *
     * Pipeline:
     *   1. Reference pixel -> forward SIP (ref) -> ref linear
     *   2. Ref linear -> inverse homography -> source linear
     *   3. Source linear -> reverse SIP (src) -> source pixel
     *
     * @param outP    Output pixel coordinates in the reference frame.
     * @param refReg  Registration data for the reference image.
     * @param srcReg  Registration data for the source image (contains H, SIP).
     * @return Corresponding pixel coordinates in the source image.
     */
    static QPointF transformRefToSrc(const QPointF& outP,
                                     const RegistrationData& refReg,
                                     const RegistrationData& srcReg);

    /**
     * @brief Apply forward SIP correction to a coordinate pair.
     *
     * Convenience wrapper that writes to output doubles instead of QPointF.
     */
    static void correctDistortion(double x, double y,
                                  const RegistrationData& reg,
                                  double& outX, double& outY);
};

} // namespace Stacking

#endif // DISTORTION_H