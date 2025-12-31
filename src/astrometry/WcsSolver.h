#ifndef WCSSOLVER_H
#define WCSSOLVER_H

#include "TriangleMatcher.h"
#include <cmath>
#include <iostream>

// Computes WCS parameters (CRVAL, CRPIX, CD) from an Affine/Generic Transformation
// The transformation maps (x_img, y_img) -> (Xi, Eta) in Degrees.
class WcsSolver {
public:
    static bool computeWcs(const GenericTrans& trans, double raCenter, double decCenter,
                           double& crpix1, double& crpix2,
                           double cd[2][2]) 
    {
        // 1. CD Matrix
        // The linear part of the transformation
        // Xi  = x10*x + x01*y + x00
        // Eta = y10*x + y01*y + y00
        //
        // WCS Standard (assuming CRPIX=0 initially):
        // Xi  = CD1_1*x + CD1_2*y
        // Eta = CD2_1*x + CD2_2*y
        //
        // So CD matrix is exactly the linear coefficients.
        cd[0][0] = trans.x10;
        cd[0][1] = trans.x01;
        cd[1][0] = trans.y10;
        cd[1][1] = trans.y01;
        
        // 2. CRPIX
        // Reference point is (raCenter, decCenter).
        // At this reference point, Standard Coordinates (Xi, Eta) = (0, 0).
        // We need to find pixel (x, y) such that transform maps it to (0, 0).
        //
        // 0 = x10*x + x01*y + x00
        // 0 = y10*x + y01*y + y00
        //
        // Solve linear system A*x = B
        // | x10 x01 | | x | = | -x00 |
        // | y10 y01 | | y |   | -y00 |
        
        double det = trans.x10 * trans.y01 - trans.x01 * trans.y10;
        if (std::abs(det) < 1e-12) {
            return false; // Singular matrix, cannot invert
        }
        
        double invDet = 1.0 / det;
        
        // Cramer's or Inverse
        // x = ( (-x00)*y01 - x01*(-y00) ) / det
        // y = ( x10*(-y00) - (-x00)*y10 ) / det
        
        double x00 = trans.x00;
        double y00 = trans.y00;
        
        double x_sol = (-x00 * trans.y01 - trans.x01 * (-y00)) * invDet;
        double y_sol = (trans.x10 * (-y00) - (-x00) * trans.y10) * invDet;
        
        crpix1 = x_sol;
        crpix2 = y_sol;
        
        // Adjust for 1-based indexing if FITS requires it (usually yes).
        // But our internal image coordinates are 0-based.
        // TStar/Standard usually keep internal 0-based.
        // We deliver 0-based CRPIX here. The GUI/Output can shift it if writing to FITS.
        
        return true;
    }
};

#endif // WCSSOLVER_H
