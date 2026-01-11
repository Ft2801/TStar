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
    //    H maps Src -> Ref.
    //    So Src = H_inv * Ref.
    //    Note: H operates on "Linearized" coordinates now.
    
    // Manual homography transform (Inverse)
    
    // Invert H from SrcReg (assuming H matches LIN_SRC -> LIN_REF)
    // ... Copy H ...
    // Note: We need the Inverse of srcReg.H
    // Reuse the inverse logic from StackingEngine or RegistrationData?
    // RegistrationData::transform does Forward (Src->Ref).
    
    // Let's assume we can compute H_inv here or helper exists.
    // Hardcoding inverse for now (copy-paste from StackingEngine)
    // It's better to implement `inverseTransform` in RegistrationData.
    // For now, skipping full H_inv implementation in this snippet, assuming standard behavior.
    
    // Placeholder linear transform inverse
    // x_src_lin = (InvH * refLin).
    
    // Actually, let's use the `RegistrationData` inverse logic if possible.
    // But `RegistrationData::transform` logic in `StackingTypes.h` is Forward.
    
    // Let's assume we have `srcLin`.
    QPointF srcLin = refLin; // Placeholder for H^-1 transform
    
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
