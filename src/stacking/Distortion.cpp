#include "Distortion.h"
#include <cmath>

namespace Stacking {

//-----------------------------------------------------------------------------
// Compute Polynomial: sum(A_ij * x^i * y^j)
//-----------------------------------------------------------------------------
double Distortion::computePoly(double u, double v, int order, const std::vector<std::vector<double>>& coeffs) {
    double res = 0.0;
    
    // SIP format usually: A_pq * u^p * v^q with p+q <= order
    // coeffs storage: we need a consistent flattened or 2D way.
    // StackingTypes defines vector<vector<double>>. 
    // Let's assume coeffs[p][q] exists.
    
    // Optimization: Precompute powers of u and v?
    // For small order (3-5), direct calc is okay.
    
    for (int p = 0; p <= order; ++p) {
        for (int q = 0; q <= order; ++q) {
            if (p + q > order) continue;
            // Check bounds
            if (p < (int)coeffs.size() && q < (int)coeffs[p].size()) {
                double val = coeffs[p][q];
                if (val != 0.0) {
                     res += val * std::pow(u, p) * std::pow(v, q);
                }
            }
        }
    }
    return res;
}

//-----------------------------------------------------------------------------
// Apply Forward (Pixel -> Linear/Intermediate)
// x_lin = x + A(x,y)
// y_lin = y + B(x,y)
//-----------------------------------------------------------------------------
QPointF Distortion::applyForward(const QPointF& p, const RegistrationData& reg) {
    if (!reg.hasDistortion || reg.sipOrder <= 0) return p;

    double u = p.x(); 
    double v = p.y();
    
    double dx = computePoly(u, v, reg.sipOrder, reg.sipA);
    double dy = computePoly(u, v, reg.sipOrder, reg.sipB);
    
    return QPointF(u + dx, v + dy);
}

//-----------------------------------------------------------------------------
// Apply Reverse (Linear/Intermediate -> Pixel)
// x_pix = x_lin + AP(x_lin, y_lin)
//-----------------------------------------------------------------------------
QPointF Distortion::applyReverse(const QPointF& p, const RegistrationData& reg) {
    if (!reg.hasDistortion || reg.sipOrder <= 0) return p;
    
    // Check if AP/BP exist (Inverse SIP)
    // If not, we might need to solve iteratively (Newton-Raphson).
    // For now assume if hasDistortion is true, we have matrices.
    
    double u = p.x();
    double v = p.y();
    
    double dx = computePoly(u, v, reg.sipOrder, reg.sipAP);
    double dy = computePoly(u, v, reg.sipOrder, reg.sipBP);
    
    return QPointF(u + dx, v + dy);
}

//-----------------------------------------------------------------------------
// Transform Output (Ref) Pixel to Input (Src) Pixel
//-----------------------------------------------------------------------------
QPointF Distortion::transformRefToSrc(const QPointF& outP, const RegistrationData& refReg, const RegistrationData& srcReg) {
    // 1. Ref Pixel -> Ref Linear (Forward SIP on Ref)
    //    If Ref has no distortion (often true for master reference), this is identity.
    QPointF refLin = applyForward(outP, refReg);
    
    // 2. Ref Linear -> Src Linear (Inverse Homography)
    // We need to transform refLin back to srcLin using the INVERSE of the src->ref homography matrix H.
    // H maps Src(linear) -> Ref(linear).
    // So Src(linear) = H_inv * Ref(linear).

    // Invert the 3x3 Homography Matrix srcReg.H
    // H = | h00 h01 h02 |
    //     | h10 h11 h12 |
    //     | h20 h21 h22 |
    
    // Determinant
    double h00 = srcReg.H[0][0], h01 = srcReg.H[0][1], h02 = srcReg.H[0][2];
    double h10 = srcReg.H[1][0], h11 = srcReg.H[1][1], h12 = srcReg.H[1][2];
    double h20 = srcReg.H[2][0], h21 = srcReg.H[2][1], h22 = srcReg.H[2][2];
    
    double det = h00 * (h11 * h22 - h12 * h21) -
                 h01 * (h10 * h22 - h12 * h20) +
                 h02 * (h10 * h21 - h11 * h20);
                 
    if (std::abs(det) < 1e-9) {
        // Singular matrix, cannot invert. Return input as fallback.
        return applyReverse(refLin, srcReg);
    }
    
    double invDet = 1.0 / det;
    
    // Inverse Matrix Elements (Unrolled)
    double i00 = (h11 * h22 - h12 * h21) * invDet;
    double i01 = (h02 * h21 - h01 * h22) * invDet;
    double i02 = (h01 * h12 - h02 * h11) * invDet;
    
    double i10 = (h12 * h20 - h10 * h22) * invDet;
    double i11 = (h00 * h22 - h02 * h20) * invDet;
    double i12 = (h02 * h10 - h00 * h12) * invDet;
    
    double i20 = (h10 * h21 - h11 * h20) * invDet;
    double i21 = (h01 * h20 - h00 * h21) * invDet;
    double i22 = (h00 * h11 - h01 * h10) * invDet;
    
    // Project Point
    // P_src = H_inv * P_ref
    double rx = refLin.x();
    double ry = refLin.y();
    double w = 1.0; 
    
    double sx = i00 * rx + i01 * ry + i02 * w;
    double sy = i10 * rx + i11 * ry + i12 * w;
    double sw = i20 * rx + i21 * ry + i22 * w;
    
    if (std::abs(sw) > 1e-9) {
        sx /= sw;
        sy /= sw;
    }
    
    QPointF srcLin(sx, sy);

    // 3. Src Linear -> Src Pixel (Reverse SIP on Src)
    return applyReverse(srcLin, srcReg);
}

void Distortion::correctDistortion(double x, double y, const RegistrationData& reg, double& outX, double& outY) {
    if (!reg.hasDistortion) {
        outX = x; outY = y;
        return;
    }
    // Assume forward correction
    outX = x + computePoly(x, y, reg.sipOrder, reg.sipA);
    outY = y + computePoly(x, y, reg.sipOrder, reg.sipB);
}

} // namespace Stacking
