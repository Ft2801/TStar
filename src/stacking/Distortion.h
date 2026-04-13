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
 * Copyright (C) 2024-2026 Siril Team
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