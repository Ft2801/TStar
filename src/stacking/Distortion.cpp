/**
 * @file Distortion.cpp
 * @brief Implementation of SIP distortion corrections and cross-frame mapping.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "Distortion.h"
#include <cmath>

namespace Stacking {

// =============================================================================
// SIP Polynomial Evaluation
// =============================================================================

double Distortion::computePoly(double u, double v, int order,
                               const std::vector<std::vector<double>>& coeffs)
{
    double res = 0.0;

    for (int p = 0; p <= order; ++p) {
        for (int q = 0; q <= order; ++q) {
            if (p + q > order) continue;
            if (p < static_cast<int>(coeffs.size()) &&
                q < static_cast<int>(coeffs[p].size()))
            {
                double val = coeffs[p][q];
                if (val != 0.0) {
                    double up = (p == 0) ? 1.0 : std::pow(u, p);
                    double vq = (q == 0) ? 1.0 : std::pow(v, q);
                    res += val * up * vq;
                }
            }
        }
    }

    return res;
}

// =============================================================================
// Forward Distortion: Pixel -> Linear
// =============================================================================

QPointF Distortion::applyForward(const QPointF& p, const RegistrationData& reg)
{
    if (!reg.hasDistortion || reg.sipOrder <= 0) return p;

    double u  = p.x();
    double v  = p.y();
    double dx = computePoly(u, v, reg.sipOrder, reg.sipA);
    double dy = computePoly(u, v, reg.sipOrder, reg.sipB);

    return QPointF(u + dx, v + dy);
}

// =============================================================================
// Reverse Distortion: Linear -> Pixel
// =============================================================================

QPointF Distortion::applyReverse(const QPointF& p, const RegistrationData& reg)
{
    if (!reg.hasDistortion || reg.sipOrder <= 0) return p;

    double u  = p.x();
    double v  = p.y();
    double dx = computePoly(u, v, reg.sipOrder, reg.sipAP);
    double dy = computePoly(u, v, reg.sipOrder, reg.sipBP);

    return QPointF(u + dx, v + dy);
}

// =============================================================================
// Full Ref-to-Source Coordinate Transform
// =============================================================================

QPointF Distortion::transformRefToSrc(const QPointF& outP,
                                      const RegistrationData& refReg,
                                      const RegistrationData& srcReg)
{
    // Step 1: Reference pixel -> reference linear (forward SIP on reference).
    QPointF refLin = applyForward(outP, refReg);

    // Step 2: Reference linear -> source linear (inverse homography).
    //
    // The homography H maps source(linear) -> reference(linear), so we
    // need to invert it: source(linear) = H^{-1} * reference(linear).

    const double h00 = srcReg.H[0][0], h01 = srcReg.H[0][1], h02 = srcReg.H[0][2];
    const double h10 = srcReg.H[1][0], h11 = srcReg.H[1][1], h12 = srcReg.H[1][2];
    const double h20 = srcReg.H[2][0], h21 = srcReg.H[2][1], h22 = srcReg.H[2][2];

    double det = h00 * (h11 * h22 - h12 * h21)
               - h01 * (h10 * h22 - h12 * h20)
               + h02 * (h10 * h21 - h11 * h20);

    if (std::abs(det) < 1e-9) {
        // Singular matrix -- fall back to reverse SIP only.
        return applyReverse(refLin, srcReg);
    }

    double invDet = 1.0 / det;

    // Compute inverse matrix elements (cofactor expansion, unrolled).
    double i00 = (h11 * h22 - h12 * h21) * invDet;
    double i01 = (h02 * h21 - h01 * h22) * invDet;
    double i02 = (h01 * h12 - h02 * h11) * invDet;

    double i10 = (h12 * h20 - h10 * h22) * invDet;
    double i11 = (h00 * h22 - h02 * h20) * invDet;
    double i12 = (h02 * h10 - h00 * h12) * invDet;

    double i20 = (h10 * h21 - h11 * h20) * invDet;
    double i21 = (h01 * h20 - h00 * h21) * invDet;
    double i22 = (h00 * h11 - h01 * h10) * invDet;

    // Project the reference-linear point through the inverse homography.
    double rx = refLin.x();
    double ry = refLin.y();

    double sx = i00 * rx + i01 * ry + i02;
    double sy = i10 * rx + i11 * ry + i12;
    double sw = i20 * rx + i21 * ry + i22;

    if (std::abs(sw) > 1e-9) {
        sx /= sw;
        sy /= sw;
    }

    // Step 3: Source linear -> source pixel (reverse SIP on source).
    return applyReverse(QPointF(sx, sy), srcReg);
}

// =============================================================================
// Convenience: Forward SIP to output doubles
// =============================================================================

void Distortion::correctDistortion(double x, double y,
                                   const RegistrationData& reg,
                                   double& outX, double& outY)
{
    if (!reg.hasDistortion) {
        outX = x;
        outY = y;
        return;
    }

    outX = x + computePoly(x, y, reg.sipOrder, reg.sipA);
    outY = y + computePoly(x, y, reg.sipOrder, reg.sipB);
}

} // namespace Stacking