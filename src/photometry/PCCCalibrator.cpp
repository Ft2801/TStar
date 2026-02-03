#include "PCCCalibrator.h"
#include <cmath>
#include <algorithm>
#include <QString>
#include <QStringList>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_statistics.h>
#include "../core/RobustStatistics.h"


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PCCCalibrator::PCCCalibrator() {
    // Identity WCS default
    m_crval1 = 0; m_crval2 = 0; m_crpix1 = 0; m_crpix2 = 0;
    m_cd11 = 1; m_cd12 = 0; m_cd21 = 0; m_cd22 = 1;
}

void PCCCalibrator::setWCS(double crval1, double crval2, double crpix1, double crpix2, double cd1_1, double cd1_2, double cd2_1, double cd2_2) {
    m_crval1 = crval1; m_crval2 = crval2;
    m_crpix1 = crpix1; m_crpix2 = crpix2;
    m_cd11 = cd1_1; m_cd12 = cd1_2;
    m_cd21 = cd2_1; m_cd22 = cd2_2;
    // Base linear WCS
}

void PCCCalibrator::setSIP(int a_order, int b_order, int ap_order, int bp_order, const std::map<std::string, double>& coeffs) {
    m_sipOrderA = a_order;
    m_sipOrderB = b_order;
    m_sipOrderAP = ap_order;
    m_sipOrderBP = bp_order;
    
    m_sipA.clear(); m_sipB.clear();
    m_sipAP.clear(); m_sipBP.clear();
    
    // Parse keys like "A_2_1"
    for(const auto& [key, val] : coeffs) {
        int i=0, j=0;
        char type[10];
        // Scanf is simple but dangerous, manual parsing is safer
        QString k = QString::fromStdString(key);
        QStringList parts = k.split('_');
        if (parts.size() >= 3) {
            bool ok1, ok2;
            i = parts[1].toInt(&ok1);
            j = parts[2].toInt(&ok2);
            if (ok1 && ok2) {
                if (parts[0] == "A") m_sipA[{i,j}] = val;
                else if (parts[0] == "B") m_sipB[{i,j}] = val;
                else if (parts[0] == "AP") m_sipAP[{i,j}] = val;
                else if (parts[0] == "BP") m_sipBP[{i,j}] = val;
            }
        }
    }
    
    m_useSip = (!m_sipA.empty() || !m_sipB.empty() || !m_sipAP.empty() || !m_sipBP.empty());
}

double PCCCalibrator::calculateSIP(double u, double v, const std::map<std::pair<int,int>, double>& coeffs, int order) const {
    double sum = 0.0;
    // Naive power implementation (can be optimized with Horner's or precomputed powers)
    for (const auto& [p, val] : coeffs) {
        // Optimization: Handle simple cases
        double term = val;
        if (p.first == 1) term *= u;
        else if (p.first > 1) term *= std::pow(u, p.first);
        
        if (p.second == 1) term *= v;
        else if (p.second > 1) term *= std::pow(v, p.second);
        
        sum += term;
    }
    return sum;
}

// Helper for Radians/Degrees
static constexpr double DEG2RAD = M_PI / 180.0;
static constexpr double RAD2DEG = 180.0 / M_PI;

static float BV_to_T(float bv) {
    return 4600.0f * (1.0f / (0.92f * bv + 1.7f) + 1.0f / (0.92f * bv + 0.62f));
}

// Standard Implementation: Kim cubic spline for Planckian locus temp to CIE xyY
// From https://en.wikipedia.org/wiki/Planckian_locus
static void temp_to_xyY(double t, double& x, double& y) {
    // Calculate x
    if (t < 1667.0) {
        x = 0.0;
    } else if (t < 4000.0) {
        x = (-0.2661239e9 / (t * t * t)) - (0.2343589e6 / (t * t)) + (0.8776956e3 / t) + 0.179910;
    } else if (t < 25000.0) {
        x = (-3.0258469e9 / (t * t * t)) + (2.1070379e6 / (t * t)) + (0.2226347e3 / t) + 0.240390;
    } else {
        x = 0.0;
    }
    
    // Calculate y
    if (t < 1667.0) {
        y = 0.0;
    } else if (t < 2222.0) {
        y = (-1.1063814 * x * x * x) - (1.34811020 * x * x) + (2.18555832 * x) - 0.20219683;
    } else if (t < 4000.0) {
        y = (-0.9549476 * x * x * x) - (1.37418593 * x * x) + (2.09137015 * x) - 0.16748867;
    } else if (t < 25000.0) {
        y = (3.0817580 * x * x * x) - (5.87338670 * x * x) + (3.75112997 * x) - 0.37001483;
    } else {
        y = 0.0;
    }
}

// CIE xyY to XYZ (Y=1 assumed for white point)
static void xyY_to_XYZ(double x, double y, double& X, double& Y_out, double& Z) {
    if (y < 1e-9) {
        X = Y_out = Z = 0.0;
        return;
    }
    Y_out = 1.0;
    X = (x / y);
    Z = ((1.0 - x - y) / y);
}

// XYZ to linear sRGB (D65 illuminant)
// Using standard sRGB matrix
static void XYZ_to_sRGB(double X, double Y, double Z, float& r, float& g, float& b) {
    // sRGB matrix (D65 white point)
    r = static_cast<float>( 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z);
    g = static_cast<float>(-0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z);
    b = static_cast<float>( 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z);
}

// Standard Implementation: Temperature to linear RGB using Planckian locus
// This replaces the inaccurate Helland approximation
static void temp_to_rgb(float T, float& r, float& g, float& b) {
    double x = 0, y = 0;
    double X = 0, Y = 0, Z = 0;
    
    temp_to_xyY(static_cast<double>(T), x, y);
    
    if (x == 0 && y == 0) {
        // Invalid temperature, return neutral
        r = g = b = 1.0f;
        return;
    }
    
    xyY_to_XYZ(x, y, X, Y, Z);
    XYZ_to_sRGB(X, Y, Z, r, g, b);
    
    // Clamp negatives (can happen with very blue/red temps outside gamut)
    r = std::max(0.0f, r);
    g = std::max(0.0f, g);
    b = std::max(0.0f, b);
    
    // Normalize by max (Standard does this)
    float mx = std::max({r, g, b});
    if (mx > 1e-6f) {
        r /= mx;
        g /= mx;
        b /= mx;
    }
}

// Robust Mean implementation (Reference Implementation) - UNUSED
/*
static float robust_mean(std::vector<float>& data) {
    if (data.empty()) return 0.0f;
    std::sort(data.begin(), data.end());
    
    // Simple 2-sigma clipping (or interquartile mean)
    // For simplicity: use median if N is small, or mid-80% mean
    size_t n = data.size();
    if (n < 5) return data[n/2];
    
    // Trim 20% from each side
    size_t low = n * 0.2;
    size_t high = n - low;
    double sum = 0;
    for (size_t i = low; i < high; ++i) sum += data[i];
    return (float)(sum / (high - low));
}
*/

void PCCCalibrator::pixelToWorld(double x, double y, double& ra, double& dec) {
    // Standard FITS TAN (Gnomonic) Projection
    // 1. Pixel to Intermediate World Coordinates (deg)
    double u = x - m_crpix1;
    double v = y - m_crpix2;
    
    if (m_useSip) {
        double f_uv = calculateSIP(u, v, m_sipA, m_sipOrderA);
        double g_uv = calculateSIP(u, v, m_sipB, m_sipOrderB);
        u += f_uv;
        v += g_uv;
    }
    
    double xi = m_cd11 * u + m_cd12 * v;
    double eta = m_cd21 * u + m_cd22 * v;
    
    // 2. Intermediate to Spherical
    double xi_rad = xi * DEG2RAD;
    double eta_rad = eta * DEG2RAD;
    
    double crval1_rad = m_crval1 * DEG2RAD;
    double crval2_rad = m_crval2 * DEG2RAD;
    
    double rho = std::sqrt(xi_rad * xi_rad + eta_rad * eta_rad);
    if (rho == 0.0) {
        ra = m_crval1;
        dec = m_crval2;
        return;
    }
    
    double c = std::atan(rho); // Since radius on tangent plane is rho (projection plane dist = 1)
    
    double sin_dec = std::cos(c) * std::sin(crval2_rad) + (eta_rad * std::sin(c) * std::cos(crval2_rad)) / rho;
    double dec_rad = std::asin(std::clamp(sin_dec, -1.0, 1.0));
    
    double y_term = rho * std::cos(crval2_rad) * std::cos(c) - eta_rad * std::sin(crval2_rad) * std::sin(c);
    double x_term = xi_rad * std::sin(c);
    
    double ra_diff = std::atan2(x_term, y_term);
    double ra_rad = crval1_rad + ra_diff;
    
    ra = ra_rad * RAD2DEG;
    dec = dec_rad * RAD2DEG;
    
    // Normalize RA
    while (ra < 0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
}

PCCResult PCCCalibrator::calibrate(const std::vector<DetectedStar>& starsR,
                                   const std::vector<DetectedStar>& starsG,
                                   const std::vector<DetectedStar>& starsB,
                                   const std::vector<CatalogStar>& catalog,
                                   [[maybe_unused]] int width, [[maybe_unused]] int height) 
{
    // Standard approach: compute per-channel factors K = (1/flux) * catalog_rgb
    // Then use robust mean and normalize
    std::vector<float> kR_list, kG_list, kB_list;
    // RELAXED TOLERANCE: 0.005 deg (~18 arcsec) to ensure we find matches even with slight distortion
    double matchRadiusDeg = 0.005; 

    // Iterate GREEN stars (Reference Channel for star matching)
    for (const auto& sg : starsG) {
        if (sg.saturated) continue;
        
        // Find matching R and B within 2 pixels (relaxed to 2.5 for chromatic aberration?)
        const DetectedStar *sr = nullptr, *sb = nullptr;
        for (const auto& s : starsR) { 
            if (!s.saturated && std::abs(s.x - sg.x) < 2.5 && std::abs(s.y - sg.y) < 2.5) { 
                sr = &s; break; 
            } 
        }
        for (const auto& s : starsB) { 
            if (!s.saturated && std::abs(s.x - sg.x) < 2.5 && std::abs(s.y - sg.y) < 2.5) { 
                sb = &s; break; 
            } 
        }
        
        if (sr && sb && sr->flux > 0 && sg.flux > 0 && sb->flux > 0) {
            // Project Image (x,y) to World (RA,Dec)
            double ra, dec;
            pixelToWorld(sg.x, sg.y, ra, dec);
            
            for (const auto& cat : catalog) {
                // Pre-filter with larger box to be safe
                if (std::abs(cat.dec - dec) > matchRadiusDeg) continue;
                // Handle RA wrap-around check roughly
                double dRA = std::abs(cat.ra - ra);
                if (dRA > 180.0) dRA = 360.0 - dRA;
                if (dRA > matchRadiusDeg) continue; 
                
                double dist = std::sqrt(std::pow(dRA, 2) + std::pow(cat.dec - dec, 2));
                if (dist < matchRadiusDeg) {
                    // Get catalog RGB from B-V temperature
                    float Tr, Tg, Tb;
                    temp_to_rgb(BV_to_T(cat.B_V), Tr, Tg, Tb);
                    
                    if (Tr > 0 && Tg > 0 && Tb > 0) {
                        // Standard formula: K_channel = (1/flux) * catalog_color
                        // This is: K = catalog_expected / measured_flux
                        float kR = (1.0f / static_cast<float>(sr->flux)) * Tr;
                        float kG = (1.0f / static_cast<float>(sg.flux)) * Tg;
                        float kB = (1.0f / static_cast<float>(sb->flux)) * Tb;
                        
                        if (!std::isnan(kR) && !std::isnan(kG) && !std::isnan(kB) &&
                            !std::isinf(kR) && !std::isinf(kG) && !std::isinf(kB)) {
                            kR_list.push_back(kR);
                            kG_list.push_back(kG);
                            kB_list.push_back(kB);
                        }
                    }
                    break; // Matched ONE catalog star
                }
            }
        }
    }

    // Need minimal samples
    size_t minStars = std::min({kR_list.size(), kG_list.size(), kB_list.size()});
    if (minStars < 10) return {false, 1.0, 1.0, 1.0};

    // Calculate Robust Mean of Factors (Standard uses standardRobustMean)
    float kr = RobustStatistics::standardRobustMean(kR_list);
    float kg = RobustStatistics::standardRobustMean(kG_list);
    float kb = RobustStatistics::standardRobustMean(kB_list);

    // Safety check
    if (kr <= 0 || kg <= 0 || kb <= 0) return {false, 1.0, 1.0, 1.0};

    // Normalize so the highest factor is 1.0 (Standard normalization)
    float maxK = std::max({kr, kg, kb});
    
    PCCResult res;
    res.valid = true;
    res.R_factor = kr / maxK;
    res.G_factor = kg / maxK;
    res.B_factor = kb / maxK;
    
    return res;
}

// Inverse WCS: World (RA,Dec) to Pixel
void PCCCalibrator::worldToPixel(double ra, double dec, double& x, double& y) {
    // Inverse gnomonic (TAN) projection
    double ra_rad = ra * DEG2RAD;
    double dec_rad = dec * DEG2RAD;
    double crval1_rad = m_crval1 * DEG2RAD;
    double crval2_rad = m_crval2 * DEG2RAD;
    
    double cos_dec = std::cos(dec_rad);
    double sin_dec = std::sin(dec_rad);
    double cos_dec0 = std::cos(crval2_rad);
    double sin_dec0 = std::sin(crval2_rad);
    double delta_ra = ra_rad - crval1_rad;
    double cos_dra = std::cos(delta_ra);
    
    double denom = sin_dec * sin_dec0 + cos_dec * cos_dec0 * cos_dra;
    if (std::abs(denom) < 1e-10) {
        x = y = -1;
        return;
    }
    
    double xi = (cos_dec * std::sin(delta_ra)) / denom;
    double eta = (sin_dec * cos_dec0 - cos_dec * sin_dec0 * cos_dra) / denom;
    
    // Convert from radians to degrees
    xi *= RAD2DEG;
    eta *= RAD2DEG;
    
    // Apply inverse CD matrix
    double det = m_cd11 * m_cd22 - m_cd12 * m_cd21;
    if (std::abs(det) < 1e-15) {
        x = y = -1;
        return;
    }
    
    double u = (m_cd22 * xi - m_cd12 * eta) / det;
    double v = (-m_cd21 * xi + m_cd11 * eta) / det;
    
    // Apply Inverse SIP (AP/BP) if available
    if (m_useSip && (!m_sipAP.empty() || !m_sipBP.empty())) {
        double f_uv = calculateSIP(u, v, m_sipAP, m_sipOrderAP);
        double g_uv = calculateSIP(u, v, m_sipBP, m_sipOrderBP);
        u += f_uv;
        v += g_uv;
    } else if (m_useSip) {
        // We have forward SIP but no inverse. Iterative solution required?
        // For now, assume AP/BP are usually provided with A/B in XISF.
        // If not, the match will be approximate (linear guess), which might still work if distortion is small.
    }
    
    x = u + m_crpix1;
    y = v + m_crpix2;
}

// NEW: Standard PCC using aperture photometry
#include "AperturePhotometry.h"

PCCResult PCCCalibrator::calibrateWithAperture(const ImageBuffer& image,
                                                const std::vector<CatalogStar>& catalogStars) {
    if (image.channels() < 3 || catalogStars.empty()) {
        return {false, 1.0, 1.0, 1.0};
    }
    
    const float* data = image.data().data();
    int width = image.width();
    int height = image.height();
    int channels = image.channels();
    
    // Configure aperture photometry (Standard defaults)
    AperturePhotometry photometry;
    ApertureConfig config;
    config.aperture = 10.0; // Standard default fixed aperture
    config.inner = 20.0;    // Standard default inner radius
    config.outer = 30.0;    // Standard default outer radius
    config.auto_aperture_factor = 4.0; 
    config.force_radius = true; // Standard defaults to fixed radius
    config.minval = 0.0;
    config.maxval = 1.0;
    photometry.setConfig(config);
    
    // Generic Approach: Robust Linear Fit on Color Ratios
    // We collect pairs of (Catalog Ratio, Image Ratio) for R/G and B/G
    std::vector<double> CatRG, ImgRG;
    std::vector<double> CatBG, ImgBG;

    for (const auto& cat : catalogStars) {
        // Convert catalog RA,Dec to pixel coordinates
        double px, py;
        worldToPixel(cat.ra, cat.dec, px, py);
        
        // Skip stars outside image
        if (px < 15 || py < 15 || px >= width - 15 || py >= height - 15) continue;
        
        int starX = static_cast<int>(px);
        int starY = static_cast<int>(py);
        
        // Measure photometry in each channel
        PhotometryResult photR = photometry.measureStar(data, width, height, channels, 0, starX, starY);
        PhotometryResult photG = photometry.measureStar(data, width, height, channels, 1, starX, starY);
        PhotometryResult photB = photometry.measureStar(data, width, height, channels, 2, starX, starY);
        
        // Only use valid measurements
        if (!photR.valid || !photG.valid || !photB.valid) continue;
        if (photR.flux <= 0 || photG.flux <= 0 || photB.flux <= 0) continue;
        
        // Get theoretical RGB
        float Tr, Tg, Tb;
        double temp;

        if (cat.teff > 1000.0) {
            temp = cat.teff;
        } else {
             float bv = std::clamp(static_cast<float>(cat.B_V), -0.4f, 2.0f);
             temp = 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62));
        }

        temp_to_rgb(static_cast<float>(temp), Tr, Tg, Tb);
        
        if (Tr <= 0 || Tg <= 0 || Tb <= 0) continue;
        
        // Calculate Ratios
        double img_rg = photR.flux / photG.flux;
        double img_bg = photB.flux / photG.flux;
        
        double cat_rg = Tr / Tg;
        double cat_bg = Tb / Tg;
        
        if (!std::isnan(img_rg) && !std::isnan(cat_rg)) {
            CatRG.push_back(cat_rg);
            ImgRG.push_back(img_rg);
        }
        if (!std::isnan(img_bg) && !std::isnan(cat_bg)) {
            CatBG.push_back(cat_bg);
            ImgBG.push_back(img_bg);
        }
    }
    
    // Need minimal samples
    size_t minStars = std::min(CatRG.size(), CatBG.size());
    if (minStars < 5) return {false, 1.0, 1.0, 1.0};
    
    // Robust Linear Fit: ImageRatio = Slope * CatRatio + Intercept
    double slopeRG=0, iceptRG=0, sigmaRG=0;
    double slopeBG=0, iceptBG=0, sigmaBG=0;
    
    bool fitRG = RobustStatistics::repeatedMedianFit(CatRG, ImgRG, slopeRG, iceptRG, sigmaRG);
    bool fitBG = RobustStatistics::repeatedMedianFit(CatBG, ImgBG, slopeBG, iceptBG, sigmaBG);
    
    if (!fitRG || !fitBG) return {false, 1.0, 1.0, 1.0};
    
    // Standard White Reference (G2V assumes WhiteRatio = 1.0)
    // KR = 1.0 / (Intercept + Slope * 1.0)
    // Map Catalog colors (D65 based) to Image colors.
    // Factor K = 1.0 / ImageRatio_White.
    
    double Wr = 1.0;
    double Wb = 1.0;
    
    double pred_rg = iceptRG + slopeRG * Wr;
    double pred_bg = iceptBG + slopeBG * Wb;
    
    if (pred_rg <= 0 || pred_bg <= 0) return {false, 1.0, 1.0, 1.0};
    
    float kr = static_cast<float>(1.0 / pred_rg);
    float kg = 1.0f;
    float kb = static_cast<float>(1.0 / pred_bg);
    
    // Normalize
    float maxK = std::max({kr, kg, kb});
    
    PCCResult res;
    res.valid = true;
    res.R_factor = kr / maxK;
    res.G_factor = kg / maxK;
    res.B_factor = kb / maxK;
    
    // Store Distribution Data for Plotting
    res.CatRG = CatRG;
    res.ImgRG = ImgRG;
    res.CatBG = CatBG;
    res.ImgBG = ImgBG;
    res.slopeRG = slopeRG;
    res.iceptRG = iceptRG;
    res.slopeBG = slopeBG;
    res.iceptBG = iceptBG;
    
    return res;

}
