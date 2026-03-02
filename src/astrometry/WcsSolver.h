#ifndef WCSSOLVER_H
#define WCSSOLVER_H

#include "TriangleMatcher.h"
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class WcsSolver {
public:
    // ARCSEC to DEGREES conversion
    static constexpr double ARCSEC_TO_DEG = 1.0 / 3600.0;
    
    // ==========================================================================
    // computeWcs — Compute WCS from converged solution
    //
    // After the convergence loop in matchCatalog:
    //   - raConverged, decConverged = the converged projection center
    //   - trans = the final affine transform from centered pixel coords
    //             to tangent plane coords in arcsec
    //
    // The WCS parameters are:
    //   CRPIX = image center (FITS convention, 1-indexed)
    //   CRVAL = converged (RA, Dec)  ← this is the projection center itself
    //   CD    = linear coefficients of trans (arcsec → degrees)
    //
    // IMPORTANT: After convergence, trans.x00 ≈ 0 and trans.y00 ≈ 0,
    // meaning the tangent plane origin coincides with the image center.
    // Therefore CRVAL = (raConverged, decConverged) directly.
    // No de-projection needed — the convergence loop already did that.
    // ==========================================================================
    static bool computeWcs(const GenericTrans& trans, 
                           double raConverged, double decConverged,
                           int imageWidth, int imageHeight,
                           double& crpix1, double& crpix2,
                           double& crval1, double& crval2,
                           double cd[2][2]) 
    {
        // CRPIX: image center in FITS convention (1-indexed)
        crpix1 = imageWidth * 0.5 + 0.5;
        crpix2 = imageHeight * 0.5 + 0.5;
        
        // CD matrix: convert transform coefficients from arcsec/px to deg/px
        //
        // COORDINATE CONVENTION NOTE:
        // The plate solver uses centered pixel coords where:
        //   ms.x = buffer_x - W/2           (same direction as FITS X)
        //   ms.y = H/2 - buffer_y            (increases northward, same as η)
        //
        // The TRANS maps (ms.x, ms.y) → tangent plane (ξ, η) in arcsec.
        // Both ms.y and η increase northward, so the Y-column mapping is direct.
        // The CD matrix maps (Δpix_x, Δpix_y) → (Δra, Δdec) in degrees.
        //
        // FITS pixel Y and ms.y are related by:  ms.y = H/2 - pix_y
        // But the TRANS was fitted with ms.y on the A-side and η on the B-side —
        // both axes increase in the same direction (northward) — so no sign flip
        // is needed when converting TRANS terms to the CD matrix.
        //
        //   CD1_1 = trans.x10 / 3600
        //   CD1_2 = trans.x01 / 3600
        //   CD2_1 = trans.y10 / 3600
        //   CD2_2 = trans.y01 / 3600
        cd[0][0] = trans.x10 * ARCSEC_TO_DEG;  // CD1_1
        cd[0][1] = trans.x01 * ARCSEC_TO_DEG;  // CD1_2
        cd[1][0] = trans.y10 * ARCSEC_TO_DEG;  // CD2_1
        cd[1][1] = trans.y01 * ARCSEC_TO_DEG;  // CD2_2
        
        // Verify non-singular
        double det = cd[0][0] * cd[1][1] - cd[0][1] * cd[1][0];
        if (std::abs(det) < 1e-20) {
            std::cerr << "WcsSolver: Singular CD matrix (det=" << det << ")" << std::endl;
            return false;
        }
        
        // CRVAL: the converged projection center
        // After convergence, trans.x00 ≈ 0, trans.y00 ≈ 0
        // so no additional de-projection is needed
        crval1 = raConverged;
        crval2 = decConverged;
        
        return true;
    }
};

#endif // WCSSOLVER_H
