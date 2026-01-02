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
    
    static bool computeWcs(const GenericTrans& trans, 
                           double raHint, double decHint,
                           int imageWidth, int imageHeight,
                           double& crpix1, double& crpix2,
                           double& crval1, double& crval2,
                           double cd[2][2]) 
    {
        crpix1 = imageWidth * 0.5 + 0.5;
        crpix2 = imageHeight * 0.5 + 0.5;
        
        // Initialize CD Matrix from affine transform
        cd[0][0] = trans.x10 * ARCSEC_TO_DEG;  // CD1_1
        cd[0][1] = trans.x01 * ARCSEC_TO_DEG;  // CD1_2
        cd[1][0] = trans.y10 * ARCSEC_TO_DEG;  // CD2_1
        cd[1][1] = trans.y01 * ARCSEC_TO_DEG;  // CD2_2
        
        // Verify singular
        double det = cd[0][0] * cd[1][1] - cd[0][1] * cd[1][0];
        if (std::abs(det) < 1e-20) {
            std::cerr << "WcsSolver: Singular CD matrix (det=" << det << ")" << std::endl;
            return false;
        }
        
        double crpix1_input = crpix1; // Center X
        double crpix2_input = crpix2; // Center Y
        
        // Calculate standard coords (in arcsec) at center pixel
        double xi_sec = trans.x00 + trans.x10 * crpix1_input + trans.x01 * crpix2_input;
        double eta_sec = trans.y00 + trans.y10 * crpix1_input + trans.y01 * crpix2_input;
        
        // Convert to degrees (standard coordinates on the tangent plane)
        double xi = xi_sec * ARCSEC_TO_DEG;
        double eta = eta_sec * ARCSEC_TO_DEG;
        
        // De-project from tangent plane to RA/DEC
        double ra0 = raHint * (M_PI / 180.0);
        double dec0 = decHint * (M_PI / 180.0);
        
        double xi_rad = xi * (M_PI / 180.0);
        double eta_rad = eta * (M_PI / 180.0);
        
        double rho = std::sqrt(xi_rad * xi_rad + eta_rad * eta_rad);
        double c = std::atan(rho);
        double ra_new = raHint;
        double dec_new = decHint;
        
        if (rho > 1e-10) {
            c = std::atan(rho); // angular distance from center
            double cos_c = std::cos(c);
            double sin_c = std::sin(c);
            
            double sin_dec = std::cos(c) * std::sin(dec0) + (eta_rad * std::sin(c) * std::cos(dec0)) / rho;
            double dec_rad_new = std::asin(sin_dec);
            
            double y_term = rho * std::cos(dec0) * cos_c - eta_rad * std::sin(dec0) * sin_c;
            double x_term = xi_rad * sin_c;
            
            double ra_diff = std::atan2(x_term, y_term);
            double ra_rad_new = ra0 + ra_diff;
            
            ra_new = ra_rad_new * (180.0 / M_PI);
            dec_new = dec_rad_new * (180.0 / M_PI);
            
            // Normalize RA
            while (ra_new < 0) ra_new += 360.0;
            while (ra_new >= 360.0) ra_new -= 360.0;
        }
        
        // Set new CRVAL
        crval1 = ra_new;
        crval2 = dec_new;
        
        return true;
    }
};

#endif // WCSSOLVER_H
