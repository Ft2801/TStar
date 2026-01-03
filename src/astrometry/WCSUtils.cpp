#include "WCSUtils.h"
#include <cmath>
#include <QDebug>

// Constants
static const double DEG_TO_RAD = M_PI / 180.0;
static const double RAD_TO_DEG = 180.0 / M_PI;

void WCSUtils::cdeltPcToCD(double cdelt1, double cdelt2,
                           double pc1_1, double pc1_2, double pc2_1, double pc2_2,
                           double& cd1_1, double& cd1_2, double& cd2_1, double& cd2_2) {
    cd1_1 = pc1_1 * cdelt1;
    cd1_2 = pc1_2 * cdelt1;
    cd2_1 = pc2_1 * cdelt2;
    cd2_2 = pc2_2 * cdelt2;
}

void WCSUtils::cdToCdeltCrota(double cd1_1, double cd1_2, double cd2_1, double cd2_2,
                              double& cdelt1, double& cdelt2, double& crota2) {
    // Calculate scales
    cdelt1 = -std::sqrt(cd1_1 * cd1_1 + cd2_1 * cd2_1);  // Usually negative for RA
    cdelt2 = std::sqrt(cd1_2 * cd1_2 + cd2_2 * cd2_2);
    
    // Need to determine actual sign from CD matrix structure
    double det = cd1_1 * cd2_2 - cd1_2 * cd2_1;
    if (det > 0) {
        cdelt1 = -cdelt1;  // Flip sign for proper handedness
    }
    
    // Calculate rotation (average of two methods for robustness)
    double rota1 = std::atan2(cd2_1, cd1_1) * RAD_TO_DEG;
    double rota2 = std::atan2(-cd1_2, cd2_2) * RAD_TO_DEG;
    crota2 = (rota1 + rota2) / 2.0;
    
    // Normalize to 0-360
    while (crota2 < 0) crota2 += 360.0;
    while (crota2 >= 360.0) crota2 -= 360.0;
}

bool WCSUtils::hasValidWCS(const Metadata& meta) {
    bool hasCoords = (meta.ra != 0 || meta.dec != 0);
    double det = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    bool hasMatrix = std::abs(det) > 1e-20;
    return hasCoords && hasMatrix;
}

double WCSUtils::pixelScale(const Metadata& meta) {
    if (!hasValidWCS(meta)) return 0.0;
    
    // Average of scales in both directions
    double scale1 = std::sqrt(meta.cd1_1 * meta.cd1_1 + meta.cd2_1 * meta.cd2_1);
    double scale2 = std::sqrt(meta.cd1_2 * meta.cd1_2 + meta.cd2_2 * meta.cd2_2);
    double avgScale = (scale1 + scale2) / 2.0;
    
    // Convert from degrees to arcseconds
    return avgScale * 3600.0;
}

double WCSUtils::imageRotation(const Metadata& meta) {
    if (!hasValidWCS(meta)) return 0.0;
    
    // Calculate rotation from CD matrix
    double crota = std::atan2(meta.cd2_1, meta.cd1_1) * RAD_TO_DEG;
    
    // Normalize to 0-360
    while (crota < 0) crota += 360.0;
    while (crota >= 360.0) crota -= 360.0;
    
    return crota;
}

void WCSUtils::tanProject(double ra, double dec, double ra0, double dec0,
                          double& xi, double& eta) {
    // Gnomonic (TAN) projection: world to native plane
    double ra_rad = ra * DEG_TO_RAD;
    double dec_rad = dec * DEG_TO_RAD;
    double ra0_rad = ra0 * DEG_TO_RAD;
    double dec0_rad = dec0 * DEG_TO_RAD;
    
    double cosD = std::cos(dec_rad);
    double sinD = std::sin(dec_rad);
    double cosD0 = std::cos(dec0_rad);
    double sinD0 = std::sin(dec0_rad);
    double cosDR = std::cos(ra_rad - ra0_rad);
    double sinDR = std::sin(ra_rad - ra0_rad);
    
    double denom = sinD * sinD0 + cosD * cosD0 * cosDR;
    
    if (std::abs(denom) < 1e-10) {
        // Point is near or beyond 90 degrees from tangent point
        xi = 0;
        eta = 0;
        return;
    }
    
    xi = (cosD * sinDR) / denom;
    eta = (sinD * cosD0 - cosD * sinD0 * cosDR) / denom;
    
    // Convert from radians to degrees
    xi *= RAD_TO_DEG;
    eta *= RAD_TO_DEG;
}

void WCSUtils::tanDeproject(double xi, double eta, double ra0, double dec0,
                            double& ra, double& dec) {
    // Gnomonic (TAN) deprojection: native plane to world
    double xi_rad = xi * DEG_TO_RAD;
    double eta_rad = eta * DEG_TO_RAD;
    double ra0_rad = ra0 * DEG_TO_RAD;
    double dec0_rad = dec0 * DEG_TO_RAD;
    
    double cosD0 = std::cos(dec0_rad);
    double sinD0 = std::sin(dec0_rad);
    
    double rho = std::sqrt(xi_rad * xi_rad + eta_rad * eta_rad);
    double c = std::atan(rho);
    
    double cosC = std::cos(c);
    double sinC = std::sin(c);
    
    double dec_rad;
    if (rho < 1e-10) {
        dec_rad = dec0_rad;
    } else {
        dec_rad = std::asin(cosC * sinD0 + (eta_rad * sinC * cosD0) / rho);
    }
    
    double ra_rad;
    if (std::abs(cosD0) < 1e-10) {
        // At poles
        ra_rad = ra0_rad + std::atan2(xi_rad, -eta_rad * sinD0 / std::abs(sinD0));
    } else {
        ra_rad = ra0_rad + std::atan2(xi_rad * sinC, rho * cosD0 * cosC - eta_rad * sinD0 * sinC);
    }
    
    ra = ra_rad * RAD_TO_DEG;
    dec = dec_rad * RAD_TO_DEG;
    
    // Normalize RA to 0-360
    while (ra < 0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
}

double WCSUtils::getSIPCoeff(const Metadata& meta, const QString& prefix, int i, int j) {
    QString key = QString("%1%2_%3").arg(prefix).arg(i).arg(j);
    return meta.sipCoeffs.value(key, 0.0);
}

void WCSUtils::applySIP(const Metadata& meta, double u, double v,
                        double& du, double& dv) {
    du = 0.0;
    dv = 0.0;
    
    int orderA = meta.sipOrderA;
    int orderB = meta.sipOrderB;
    
    if (orderA <= 0 && orderB <= 0) return;
    
    // A polynomial: du = sum(A_i_j * u^i * v^j)
    for (int i = 0; i <= orderA; ++i) {
        for (int j = 0; j <= orderA - i; ++j) {
            if (i == 0 && j == 0) continue;  // No constant term
            double coeff = getSIPCoeff(meta, "A_", i, j);
            du += coeff * std::pow(u, i) * std::pow(v, j);
        }
    }
    
    // B polynomial: dv = sum(B_i_j * u^i * v^j)
    for (int i = 0; i <= orderB; ++i) {
        for (int j = 0; j <= orderB - i; ++j) {
            if (i == 0 && j == 0) continue;
            double coeff = getSIPCoeff(meta, "B_", i, j);
            dv += coeff * std::pow(u, i) * std::pow(v, j);
        }
    }
}

void WCSUtils::applySIPInverse(const Metadata& meta, double u, double v,
                               double& u0, double& v0) {
    // If we have inverse coefficients (AP, BP), use them
    if (meta.sipOrderAP > 0 || meta.sipOrderBP > 0) {
        double du = 0.0;
        double dv = 0.0;
        
        int orderAP = meta.sipOrderAP;
        int orderBP = meta.sipOrderBP;
        
        for (int i = 0; i <= orderAP; ++i) {
            for (int j = 0; j <= orderAP - i; ++j) {
                if (i == 0 && j == 0) continue;
                double coeff = getSIPCoeff(meta, "AP_", i, j);
                du += coeff * std::pow(u, i) * std::pow(v, j);
            }
        }
        
        for (int i = 0; i <= orderBP; ++i) {
            for (int j = 0; j <= orderBP - i; ++j) {
                if (i == 0 && j == 0) continue;
                double coeff = getSIPCoeff(meta, "BP_", i, j);
                dv += coeff * std::pow(u, i) * std::pow(v, j);
            }
        }
        
        u0 = u + du;
        v0 = v + dv;
        return;
    }
    
    // Otherwise, use iterative method
    // Start with u0 = u, v0 = v and iterate
    u0 = u;
    v0 = v;
    
    for (int iter = 0; iter < 10; ++iter) {
        double du, dv;
        applySIP(meta, u0, v0, du, dv);
        
        double u_pred = u0 + du;
        double v_pred = v0 + dv;
        
        // Adjust
        u0 = u - du;
        v0 = v - dv;
        
        // Check convergence
        if (std::abs(u_pred - u) < 1e-10 && std::abs(v_pred - v) < 1e-10) {
            break;
        }
    }
}

bool WCSUtils::pixelToWorld(const Metadata& meta, double px, double py,
                            double& ra, double& dec) {
    if (!hasValidWCS(meta)) {
        ra = 0;
        dec = 0;
        return false;
    }
    
    // Relative pixel coordinates (0-indexed to CRPIX)
    double u = px - meta.crpix1 + 1.0;  // FITS is 1-indexed
    double v = py - meta.crpix2 + 1.0;
    
    // Apply SIP distortion if present
    if (meta.sipOrderA > 0 || meta.sipOrderB > 0) {
        double du, dv;
        applySIP(meta, u, v, du, dv);
        u += du;
        v += dv;
    }
    
    // Apply CD matrix to get intermediate world coordinates
    double xi = meta.cd1_1 * u + meta.cd1_2 * v;
    double eta = meta.cd2_1 * u + meta.cd2_2 * v;
    
    // Deproject from native plane to world coordinates
    tanDeproject(xi, eta, meta.ra, meta.dec, ra, dec);
    
    return true;
}

bool WCSUtils::worldToPixel(const Metadata& meta, double ra, double dec,
                            double& px, double& py) {
    if (!hasValidWCS(meta)) {
        px = 0;
        py = 0;
        return false;
    }
    
    // Project to native plane
    double xi, eta;
    tanProject(ra, dec, meta.ra, meta.dec, xi, eta);
    
    // Invert CD matrix: [u, v] = CD^-1 * [xi, eta]
    double det = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    if (std::abs(det) < 1e-20) {
        px = 0;
        py = 0;
        return false;
    }
    
    double u = (meta.cd2_2 * xi - meta.cd1_2 * eta) / det;
    double v = (-meta.cd2_1 * xi + meta.cd1_1 * eta) / det;
    
    // Apply inverse SIP if present
    if (meta.sipOrderA > 0 || meta.sipOrderB > 0 ||
        meta.sipOrderAP > 0 || meta.sipOrderBP > 0) {
        double u0, v0;
        applySIPInverse(meta, u, v, u0, v0);
        u = u0;
        v = v0;
    }
    
    // Convert back to pixel coordinates (0-indexed)
    px = u + meta.crpix1 - 1.0;  // FITS is 1-indexed
    py = v + meta.crpix2 - 1.0;
    
    return true;
}

bool WCSUtils::getFieldCenter(const Metadata& meta, int width, int height,
                              double& ra, double& dec) {
    // Convert center pixel to world coordinates
    return pixelToWorld(meta, width / 2.0, height / 2.0, ra, dec);
}

bool WCSUtils::getFieldOfView(const Metadata& meta, int width, int height,
                              double& fovX, double& fovY) {
    if (!hasValidWCS(meta)) {
        fovX = 0;
        fovY = 0;
        return false;
    }
    
    // Simple calculation using pixel scale
    double scale = pixelScale(meta);  // arcsec/pixel
    fovX = width * scale / 3600.0;   // degrees
    fovY = height * scale / 3600.0;  // degrees
    
    return true;
}
