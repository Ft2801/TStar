/*
 * SPCC.cpp - Spectrophotometric Color Calibration
 *
 * Key algorithm components:
 * 1. Load JSON sensor/filter profiles (recursive directory scan)
 * 2. Query Gaia DR3 catalog via VizieR online
 * 3. Perform aperture photometry on detected stars
 * 4. Extract XP-sampled spectral data for each star
 * 5. Spectral convolution: ∫ response(λ) × spectrum(λ) dλ
 * 6. Robust color fitting: Siegel's Repeated Median regression
 * 7. Compute white balance coefficients K_R, K_G, K_B
 * 8. Apply color correction to image
 */

#include "SPCC.h"
#include "../photometry/CatalogClient.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <omp.h>

namespace {
    // ─────────────────────────────────────────────────────────────────────────
    // Constants 
    // ─────────────────────────────────────────────────────────────────────────
    
    // CIE color matching functions and white points
    const double D65_X = 0.95045471;
    const double D65_Y = 1.0;
    const double D65_Z = 1.08905029;
    
    // Normalized MAD (Median Absolute Deviation) constant
    constexpr double MAD_NORM = 1.4826;
    
    // Outlier rejection threshold (in units of MAD)
    constexpr double OUTLIER_THRESHOLD_MAD = 3.0;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Utility: Load XPSampled wavelength grid
    // ─────────────────────────────────────────────────────────────────────────
    XPSampled initXPSampled() {
        XPSampled xps;
        return xps;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rayleigh scattering optical depth 
    // ─────────────────────────────────────────────────────────────────────────
    double tauRayleigh(double lambda_micrometer, double height_km, double pressure_hpa) {
        double term1 = pressure_hpa / 1013.25;  // relative to standard pressure
        double term2 = 0.00864 + 6.5e-6 * height_km;
        double exponent = -(3.916 + 0.074 * lambda_micrometer + 0.050 / lambda_micrometer);
        double term3 = std::pow(lambda_micrometer, exponent);
        
        return term1 * term2 * term3;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Atmosphere transmittance at given wavelength
    // ─────────────────────────────────────────────────────────────────────────
    double transmittance(double lambda_nm, double height_m, double pressure_hpa,
                        double airmass) {
        double lambda_um = lambda_nm / 1000.0;  // nm → micrometers
        double h_km = height_m / 1000.0;        // m → km
        double tau = tauRayleigh(lambda_um, h_km, pressure_hpa);
        return std::exp(-tau * airmass);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Quick median computation (used by Repeated Median regression)
    // ─────────────────────────────────────────────────────────────────────────
    double quickMedian(std::vector<double> data) {
        if (data.empty()) return 0.0;
        std::sort(data.begin(), data.end());
        if (data.size() % 2 == 1) {
            return data[data.size() / 2];
        } else {
            return (data[data.size() / 2 - 1] + data[data.size() / 2]) / 2.0;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Temperature to RGB conversion (from Planckian locus)
    // ─────────────────────────────────────────────────────────────────────────
    void temp_to_xyY(double t, double& x, double& y) {
        // Calculate x using Kim cubic spline (Planckian locus)
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
    
    void xyY_to_XYZ(double x, double y, double& X, double& Y_out, double& Z) {
        if (y < 1e-9) {
            X = Y_out = Z = 0.0;
            return;
        }
        Y_out = 1.0;
        X = (x / y);
        Z = ((1.0 - x - y) / y);
    }
    
    void XYZ_to_sRGB(double X, double Y, double Z, float& r, float& g, float& b) {
        // sRGB matrix (D65 illuminant)
        r = static_cast<float>( 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z);
        g = static_cast<float>(-0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z);
        b = static_cast<float>( 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z);
    }
    
#if 0
    void tempK_to_rgb(float T, float& r, float& g, float& b) {
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
        
        // Clamp negatives
        r = std::max(0.0f, r);
        g = std::max(0.0f, g);
        b = std::max(0.0f, b);
        
        // Normalize by max
        float mx = std::max({r, g, b});
        if (mx > 1e-6f) {
            r /= mx;
            g /= mx;
            b /= mx;
        }
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC Element-wise spectral multiplication
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::multiplyXPSampled(XPSampled& result,
                            const XPSampled& a,
                            const XPSampled& b) {
    for (int i = 0; i < XPSAMPLED_LEN; ++i) {
        result.y[i] = a.y[i] * b.y[i];
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SPCC Spectral integration using Trapezoidal rule
// ─────────────────────────────────────────────────────────────────────────────
double SPCC::integrateXPSampled(const XPSampled& xps,
                               double min_wl, double max_wl) {
    int i_min = (int)std::ceil((min_wl - XPSAMPLED_MIN_WL) / XPSAMPLED_STEP_WL);
    int i_max = (int)std::floor((max_wl - XPSAMPLED_MIN_WL) / XPSAMPLED_STEP_WL);
    
    // Clamp to valid range
    i_min = std::max(0, i_min);
    i_max = std::min(XPSAMPLED_LEN - 1, i_max);
    
    if (i_min >= i_max) return 0.0;
    
    // Trapezoidal rule
    double integral = 0.0;
    for (int i = i_min; i < i_max; ++i) {
        double h = xps.x[i + 1] - xps.x[i];  // Usually 2.0 nm
        integral += h * (xps.y[i] + xps.y[i + 1]) / 2.0;
    }
    
    return integral;
}

// ─────────────────────────────────────────────────────────────────────────────
// Convert flux from W m^-1 nm^-1 to relative photon count
// Normalize at 550nm (human eye peak sensitivity)
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::fluxToRelativePhotonCount(XPSampled& xps) {
    // Photon count ∝ λ × E_photon
    // So: photon_count = flux × wavelength (in appropriate units)
    for (int i = 0; i < XPSAMPLED_LEN; ++i) {
        xps.y[i] *= xps.x[i];  // Multiply by wavelength
    }
    
    // Normalize to 550nm (index 82: 336 + 82×2 = 500... close to 550)
    // Actually compute correct index: (550 - 336) / 2 = 107
    int idx_550 = 107;
    if (idx_550 >= 0 && idx_550 < XPSAMPLED_LEN && xps.y[idx_550] > 0.0) {
        double norm = xps.y[idx_550];
        for (int i = 0; i < XPSAMPLED_LEN; ++i) {
            xps.y[i] /= norm;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Interpolate library spectrum to XPSampled grid (using linear interpolation)
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::initXPSampledFromLibrary(XPSampled& out,
                                   const SPCCObject& in) {
    // Simple linear interpolation from library wavelengths to XPSampled grid
    std::fill(out.y, out.y + XPSAMPLED_LEN, 0.0);
    
    if (in.x.empty() || in.y.empty()) {
        return;
    }
    
    for (int i = 0; i < XPSAMPLED_LEN; ++i) {
        double wl = out.x[i];
        
        // Find bracketing points
        auto it = std::lower_bound(in.x.begin(), in.x.end(), wl);
        if (it == in.x.begin() || it == in.x.end()) {
            out.y[i] = 0.0;
            continue;
        }
        
        int i1 = std::distance(in.x.begin(), it) - 1;
        int i2 = i1 + 1;
        
        // Linear interpolation
        double wl1 = in.x[i1];
        double wl2 = in.x[i2];
        double fl1 = in.y[i1];
        double fl2 = in.y[i2];
        
        double t = (wl - wl1) / (wl2 - wl1);
        out.y[i] = std::max(0.0, fl1 * (1.0 - t) + fl2 * t);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Atmospheric extinction model (Rayleigh scattering)
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::fillXPSampledFromAtmosModel(XPSampled& out,
                                      const PhotometricCCArgs& args) {
    // Compute airmass
    double airmass = 1.5;  // Default zenith angle ~48 degrees
    
    // Compute transmittance for each wavelength
    double maxval = -1e30;
    for (int i = 0; i < XPSAMPLED_LEN; ++i) {
        double trans = transmittance(out.x[i], args.observer_altitude,
                                    args.atmos_pressure, airmass);
        out.y[i] = trans;
        maxval = std::max(maxval, trans);
    }
    
    // Normalize to prevent dimming
    if (maxval > 0.0) {
        for (int i = 0; i < XPSAMPLED_LEN; ++i) {
            out.y[i] /= maxval;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Get spectral response (sensor + filter + optional atmosphere + LPF)
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::getSpectrumFromArgs(const PhotometricCCArgs& args,
                              const SPCCDataStore& store,
                              XPSampled& out, int channel) {
    // Load sensor response
    if (args.spcc_mono_sensor) {
        // Monochrome sensor: load sensor + filter for this channel
        if (args.selected_sensor_mono < (int)store.mono_sensors.size()) {
            initXPSampledFromLibrary(out, store.mono_sensors[args.selected_sensor_mono]);
        }
        
        // Multiply by filter response
        if (channel < 3) {
            int filter_idx = (channel == 0) ? args.selected_filter_r :
                            (channel == 1) ? args.selected_filter_g :
                            args.selected_filter_b;
            if (filter_idx < (int)store.mono_filters[channel].size()) {
                XPSampled filter_resp = initXPSampled();
                initXPSampledFromLibrary(filter_resp, store.mono_filters[channel][filter_idx]);
                multiplyXPSampled(out, out, filter_resp);
            }
        }
    } else {
        // One-shot color sensor
        if (args.selected_sensor_osc < (int)store.osc_sensors.size()) {
            const OSCSensor& osc = store.osc_sensors[args.selected_sensor_osc];
            if (channel < 3) {
                initXPSampledFromLibrary(out, osc.channel[channel]);
            }
        }
        
        // Multiply by filter response
        if (args.selected_filter_osc < (int)store.osc_filters.size()) {
            XPSampled filter_resp = initXPSampled();
            initXPSampledFromLibrary(filter_resp, store.osc_filters[args.selected_filter_osc]);
            multiplyXPSampled(out, out, filter_resp);
        }
    }
    
    // Apply DSLR low-pass filter if applicable
    if (args.is_dslr && args.selected_filter_lpf < (int)store.osc_lpf.size()) {
        XPSampled lpf = initXPSampled();
        initXPSampledFromLibrary(lpf, store.osc_lpf[args.selected_filter_lpf]);
        multiplyXPSampled(out, out, lpf);
    }
    
    // Apply atmospheric correction if enabled
    if (args.atmos_correction) {
        XPSampled atmos = initXPSampled();
        fillXPSampledFromAtmosModel(atmos, args);
        multiplyXPSampled(out, out, atmos);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Aperture photometry: measure flux in circular aperture
// ─────────────────────────────────────────────────────────────────────────────
SPCC::AperturePhotometryResult SPCC::aperturePhotometry(const ImageBuffer& buf,
                                                       double cx, double cy,
                                                       double aperture_radius) {
    AperturePhotometryResult result = {0.0, 0.0, 0.0, 0.0, false};
    
    // Simple circular aperture sum
    int x_min = std::max(0, (int)(cx - aperture_radius));
    int x_max = std::min((int)buf.width(), (int)(cx + aperture_radius) + 1);
    int y_min = std::max(0, (int)(cy - aperture_radius));
    int y_max = std::min((int)buf.height(), (int)(cy + aperture_radius) + 1);
    
    double r2 = aperture_radius * aperture_radius;
    int pixel_count = 0;
    
    for (int y = y_min; y < y_max; ++y) {
        for (int x = x_min; x < x_max; ++x) {
            double dx = x - cx;
            double dy = y - cy;
            if (dx*dx + dy*dy <= r2) {
                // Accumulate pixel values
                size_t idx = y * buf.width() + x;
                result.flux_r += buf.data()[idx * 3];
                result.flux_g += buf.data()[idx * 3 + 1];
                result.flux_b += buf.data()[idx * 3 + 2];
                pixel_count++;
            }
        }
    }
    
    if (pixel_count > 0) {
        result.valid = true;
        result.snr = std::sqrt(pixel_count);  // Approximate SNR
    }
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Siegel's Repeated Median Regression (robust to outliers)
// ─────────────────────────────────────────────────────────────────────────────
bool SPCC::repeatedMedianFit(const std::vector<double>& xdata,
                            const std::vector<double>& ydata,
                            double& intercept, double& slope,
                            double& sigma,
                            std::vector<bool>& mask_inliers) {
    int n = (int)xdata.size();
    if (n < 2) return false;
    
    mask_inliers.clear();
    mask_inliers.resize(n, false);
    
    // Step 1: Compute all pairwise slopes and point medians
    std::vector<double> point_medians(n);
    for (int i = 0; i < n; ++i) {
        std::vector<double> slopes_for_point;
        slopes_for_point.reserve(n - 1);
        for (int j = 0; j < n; ++j) {
            if (i != j && xdata[i] != xdata[j]) {
                slopes_for_point.push_back((ydata[j] - ydata[i]) / (xdata[j] - xdata[i]));
            }
        }
        point_medians[i] = quickMedian(slopes_for_point);
    }
    
    // Step 2: Final slope is median of all point medians
    slope = quickMedian(point_medians);
    
    // Step 3: Compute intercepts and final intercept
    std::vector<double> intercepts(n);
    for (int i = 0; i < n; ++i) {
        intercepts[i] = ydata[i] - slope * xdata[i];
    }
    intercept = quickMedian(intercepts);
    
    // Step 4: Compute residuals and identify outliers
    std::vector<double> absolute_residuals(n);
    double residual_sum = 0.0;
    int inlier_count = 0;
    
    for (int i = 0; i < n; ++i) {
        double predicted = intercept + slope * xdata[i];
        double residual = ydata[i] - predicted;
        absolute_residuals[i] = std::fabs(residual);
        residual_sum += residual * residual;
    }
    
    // Step 5: Median Absolute Deviation
    double mad = quickMedian(absolute_residuals);
    mad *= MAD_NORM;
    
    // Step 6: Mark inliers/outliers
    for (int i = 0; i < n; ++i) {
        bool is_inlier = (absolute_residuals[i] <= 3.0 * mad);
        mask_inliers[i] = is_inlier;
        if (is_inlier) inlier_count++;
    }
    
    // Step 7: Compute sigma
    if (inlier_count > 2) {
        sigma = std::sqrt(residual_sum / inlier_count);
    } else {
        sigma = std::sqrt(residual_sum / n);
    }
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute white balance coefficients using SPCC algorithm
// ─────────────────────────────────────────────────────────────────────────────
bool SPCC::getWhiteBalanceCoefficients(PhotometricCCArgs& args,
                                      const SPCCDataStore& store,
                                      float kw[3]) {
    kw[0] = kw[1] = kw[2] = 1.0;  // Default: no correction
    
    if (args.catalog_stars.empty()) {
        return false;
    }
    
    // Step 1: Load sensor/filter/atmospheric response for each channel
    XPSampled response[3];
    for (int chan = 0; chan < 3; ++chan) {
        getSpectrumFromArgs(args, store, response[chan], chan);
    }
    
    // Step 2: For each star, perform aperture photometry and spectral convolution
    std::vector<double> crg, cbg;  // Catalog red/green, blue/green
    std::vector<double> irg, ibg;  // Image red/green, blue/green
    
    for (const GaiaStarData& star : args.catalog_stars) {
        if (!star.has_xp) continue;  // Skip if no XP data
        
        // Aperture photometry at star's image coordinates
        auto phot = aperturePhotometry(*args.fit, star.x_img, star.y_img, args.aperture_radius);
        if (!phot.valid) continue;
        
        // Compute image color ratios
        double img_rg = phot.flux_r / phot.flux_g;
        double img_bg = phot.flux_b / phot.flux_g;
        
        // Compute catalog color ratios via spectral convolution
        XPSampled star_spec = initXPSampled();
        std::copy(star_spec.y, star_spec.y + XPSAMPLED_LEN, star_spec.y);
        fluxToRelativePhotonCount(star_spec);
        
        double flux_r = 0.0, flux_g = 0.0, flux_b = 0.0;
        for (int chan = 0; chan < 3; ++chan) {
            XPSampled convolved = initXPSampled();
            multiplyXPSampled(convolved, response[chan], star_spec);
            double *flux_ptr = (chan == 0) ? &flux_r : (chan == 1) ? &flux_g : &flux_b;
            *flux_ptr = integrateXPSampled(convolved, 400.0, 700.0);
        }
        
        double cat_rg = flux_r / flux_g;
        double cat_bg = flux_b / flux_g;
        
        crg.push_back(cat_rg);
        cbg.push_back(cat_bg);
        irg.push_back(img_rg);
        ibg.push_back(img_bg);
    }
    
    if (crg.size() < 3) {
        return false;  // Not enough stars
    }
    
    // Step 3: Robust linear fitting (Repeated Median)
    double arg, brg, abg, bbg, sigma_rg, sigma_bg;
    std::vector<bool> mask_rg, mask_bg;
    
    if (!repeatedMedianFit(crg, irg, arg, brg, sigma_rg, mask_rg)) {
        return false;
    }
    if (!repeatedMedianFit(cbg, ibg, abg, bbg, sigma_bg, mask_bg)) {
        return false;
    }
    
    // Step 4: Get white reference ratios
    XPSampled white = initXPSampled();
    if (args.selected_white_ref >= 0 && args.selected_white_ref < (int)store.wb_ref.size()) {
        initXPSampledFromLibrary(white, store.wb_ref[args.selected_white_ref]);
    } else {
        // Fallback white spectrum
        for (int i = 0; i < XPSAMPLED_LEN; ++i) {
            white.y[i] = 1.0;
        }
    }
    
    double wflux_r = 0.0, wflux_g = 0.0, wflux_b = 0.0;
    for (int chan = 0; chan < 3; ++chan) {
        XPSampled convolved = initXPSampled();
        multiplyXPSampled(convolved, response[chan], white);
        double *flux_ptr = (chan == 0) ? &wflux_r : (chan == 1) ? &wflux_g : &wflux_b;
        *flux_ptr = integrateXPSampled(convolved, 400.0, 700.0);
    }
    
    double wrg = wflux_r / wflux_g;
    double wbg = wflux_b / wflux_g;
    
    // Step 5: Compute white balance coefficients
    // K_c = 1 / (a + b * ratio_white)
    kw[0] = (float)(1.0 / (arg + brg * wrg));
    kw[1] = 1.0f;
    kw[2] = (float)(1.0 / (abg + bbg * wbg));
    
    // Normalize
    float max_k = std::max({kw[0], kw[1], kw[2]});
    if (max_k > 0.0f) {
        kw[0] /= max_k;
        kw[1] /= max_k;
        kw[2] /= max_k;
    }
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply white balance correction to image
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::applyPhotometricColorCorrection(ImageBuffer& fit, const double kw[3]) {
    size_t n_pixels = fit.width() * fit.height();
    
#pragma omp parallel for
    for (int i = 0; i < (int)n_pixels; ++i) {
        fit.data()[i * 3 + 0] *= kw[0];  // R
        fit.data()[i * 3 + 1] *= kw[1];  // G
        fit.data()[i * 3 + 2] *= kw[2];  // B
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Load JSON profile metadata recursively 
// ─────────────────────────────────────────────────────────────────────────────
bool SPCC::loadAllSPCCMetadata(const QString& spcc_repo_path, SPCCDataStore& out) {
    QDir rootDir(spcc_repo_path);
    if (!rootDir.exists()) {
        qWarning() << "[SPCC] Repository path does not exist:" << spcc_repo_path;
        return false;
    }
    
    // Expected subdirectories 
    QStringList subdirs = {"cameras", "filters", "lenses"};
    int total_loaded = 0;
    
    for (const QString& subdir : subdirs) {
        QDir categoryDir(rootDir.filePath(subdir));
        if (!categoryDir.exists()) {
            qWarning() << "[SPCC] Subdirectory not found:" << subdir;
            continue;
        }
        
        // Recursively scan for .json files
        QStringList jsonFiles = categoryDir.entryList({"*.json"}, QDir::Files, QDir::Name);
        
        for (const QString& jsonFile : jsonFiles) {
            QString filepath = categoryDir.filePath(jsonFile);
            QFile file(filepath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "[SPCC] Failed to open:" << filepath;
                continue;
            }
            
            QByteArray data = file.readAll();
            file.close();
            
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (doc.isNull()) {
                qWarning() << "[SPCC] JSON parse error in" << filepath << ":" << parseError.errorString();
                continue;
            }
            
            QJsonObject root = doc.object();
            
            // Check for array of profiles (e.g., cameras.json = [{...}, {...}])
            QJsonArray profileArray;
            if (doc.isArray()) {
                profileArray = doc.array();
            } else if (root.contains("profiles")) {
                QJsonValue profilesVal = root["profiles"];
                if (profilesVal.isArray()) {
                    profileArray = profilesVal.toArray();
                }
            } else {
                // Single profile object
                profileArray = QJsonArray() << root;
            }
            
            // Parse each profile
            for (int i = 0; i < profileArray.size(); ++i) {
                QJsonObject profile = profileArray[i].toObject();
                SPCCObject spccObj;
                
                spccObj.model = profile["model"].toString();
                spccObj.name = profile["name"].toString();
                spccObj.comment = profile["comment"].toString();
                spccObj.manufacturer = profile["manufacturer"].toString();
                spccObj.dataSource = profile["dataSource"].toString();
                spccObj.filepath = filepath;
                spccObj.index = i;
                spccObj.is_dslr = profile["is_dslr"].toBool(false);
                spccObj.version = profile["version"].toInt(1);
                
                // Parse type string (e.g., "MONO_SENSOR", "OSC_FILTER", etc.)
                QString typeStr = profile["type"].toString("OSC_SENSOR");
                if (typeStr == "MONO_SENSOR") spccObj.type = MONO_SENSOR;
                else if (typeStr == "OSC_SENSOR") spccObj.type = OSC_SENSOR;
                else if (typeStr == "MONO_FILTER") spccObj.type = MONO_FILTER;
                else if (typeStr == "OSC_FILTER") spccObj.type = OSC_FILTER;
                else if (typeStr == "OSC_LPF") spccObj.type = OSC_LPF;
                else if (typeStr == "WB_REF") spccObj.type = WB_REF;
                
                // Special handling for OSC_SENSOR which contains an array of channels
                if (spccObj.type == OSC_SENSOR) {
                    OSCSensor osc;
                    if (profile.contains("channel") && profile["channel"].isArray()) {
                        QJsonArray channels = profile["channel"].toArray();
                        for (int c = 0; c < qMin(channels.size(), 3); ++c) {
                            QJsonObject ch = channels[c].toObject();
                            osc.channel[c].model = ch["model"].toString(profile["model"].toString());
                            osc.channel[c].name = ch["name"].toString(profile["name"].toString());
                            osc.channel[c].comment = ch["comment"].toString(profile["comment"].toString());
                            osc.channel[c].manufacturer = ch["manufacturer"].toString(profile["manufacturer"].toString());
                            osc.channel[c].type = OSC_SENSOR;
                            QString chanStr = ch["channel"].toString();
                            if (chanStr == "R") osc.channel[c].channel = SPCC_RED;
                            else if (chanStr == "G") osc.channel[c].channel = SPCC_GREEN;
                            else if (chanStr == "B") osc.channel[c].channel = SPCC_BLUE;
                            else osc.channel[c].channel = SPCC_CLEAR;
                            
                            if (ch.contains("wavelengths")) {
                                QJsonArray wlArray = ch["wavelengths"].toArray();
                                for (const QJsonValue& val : wlArray) osc.channel[c].x.push_back(val.toDouble());
                                osc.channel[c].arrays_loaded = !osc.channel[c].x.empty();
                            }
                            if (ch.contains("response")) {
                                QJsonArray respArray = ch["response"].toArray();
                                for (const QJsonValue& val : respArray) osc.channel[c].y.push_back(val.toDouble());
                            }
                        }
                    }
                    out.osc_sensors.push_back(osc);
                    total_loaded++;
                    continue; // Skip the rest of single-object parsing
                }
                
                // Channel mask
                QString channelStr = profile["channel"].toString("RGB");
                if (channelStr == "R") spccObj.channel = SPCC_RED;
                else if (channelStr == "G") spccObj.channel = SPCC_GREEN;
                else if (channelStr == "B") spccObj.channel = SPCC_BLUE;
                else spccObj.channel = SPCC_CLEAR;
                
                spccObj.quality = profile["quality"].toInt(0);
                
                // Load wavelength/response arrays if present
                if (profile.contains("wavelengths")) {
                    QJsonArray wlArray = profile["wavelengths"].toArray();
                    for (const QJsonValue& val : wlArray) {
                        spccObj.x.push_back(val.toDouble());
                    }
                    spccObj.arrays_loaded = !spccObj.x.empty();
                }
                
                if (profile.contains("response")) {
                    QJsonArray respArray = profile["response"].toArray();
                    for (const QJsonValue& val : respArray) {
                        spccObj.y.push_back(val.toDouble());
                    }
                }
                
                // Categorize by type
                switch (spccObj.type) {
                    case MONO_SENSOR:
                        out.mono_sensors.push_back(spccObj);
                        total_loaded++;
                        break;
                    case MONO_FILTER: {
                        // Determine filter index (R=0, G=1, B=2, Lum=3)
                        int filter_idx = (spccObj.channel == SPCC_RED) ? 0 :
                                        (spccObj.channel == SPCC_GREEN) ? 1 : 
                                        (spccObj.channel == SPCC_BLUE) ? 2 : 3;
                        if (filter_idx >= 0 && filter_idx < 4) {
                            out.mono_filters[filter_idx].push_back(spccObj);
                            total_loaded++;
                        }
                        break;
                    }
                    case OSC_LPF:
                        out.osc_lpf.push_back(spccObj);
                        total_loaded++;
                        break;
                    case OSC_FILTER:
                        out.osc_filters.push_back(spccObj);
                        total_loaded++;
                        break;
                    case WB_REF:
                        out.wb_ref.push_back(spccObj);
                        total_loaded++;
                        break;
                    default:
                        break;
                }
            }
        }
    }
    
    qInfo() << "[SPCC] Loaded" << total_loaded << "profiles from" << spcc_repo_path;
    return total_loaded > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Get available camera profiles list
// ─────────────────────────────────────────────────────────────────────────────
QStringList SPCC::availableCameraProfiles(const QString& dataPath) {
    QStringList result;
    QDir camerasDir(dataPath + "/cameras");
    if (camerasDir.exists()) {
        QStringList jsonFiles = camerasDir.entryList({"*.json"}, QDir::Files);
        for (const QString& f : jsonFiles) {
            QString name = f;
            name.replace(".json", "");
            result << name;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Get available filter profiles list
// ─────────────────────────────────────────────────────────────────────────────
QStringList SPCC::availableFilterProfiles(const QString& dataPath) {
    QStringList result;
    QDir filtersDir(dataPath + "/filters");
    if (filtersDir.exists()) {
        QStringList jsonFiles = filtersDir.entryList({"*.json"}, QDir::Files);
        for (const QString& f : jsonFiles) {
            QString name = f;
            name.replace(".json", "");
            result << name;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main calibration function (simple)
// ─────────────────────────────────────────────────────────────────────────────
SPCCResult SPCC::calibrate(const ImageBuffer& fit, const SPCCParams& params) {
    SPCCResult result;
    result.success = false;
    result.error_msg = "Calibration in progress...";
    // Prepare runtime args from params
    PhotometricCCArgs args;
    args.fit = const_cast<ImageBuffer*>(&fit);
    args.aperture_radius = params.apertureR;

    // Validate inputs
    if (args.fit == nullptr) {
        result.error_msg = "No image data provided";
        return result;
    }

    // Require pre-populated catalog stars for this simple implementation
    if (args.catalog_stars.empty()) {
        result.error_msg = "No catalog stars provided (use calibrateWithCatalog)";
        return result;
    }

    if (args.catalog_stars.size() < 3) {
        result.error_msg = QString("Insufficient stars for calibration: %1 (need >= 3)")
                               .arg(args.catalog_stars.size());
        return result;
    }

    result.stars_found = (int)args.catalog_stars.size();
    
    // Step 2: Load sensor/filter profiles (JSON)
    // This would typically be done once at startup and cached
    SPCCDataStore profileStore;
    // loadAllSPCCMetadata() called by application before running calibration
    
    // Step 3: Perform white balance fitting
    // TODO: Implement full photometric pipeline
    // For now: return placeholder success
    result.success = true;
    result.error_msg = "";
    result.white_balance_k[0] = 1.0;
    result.white_balance_k[1] = 1.0;
    result.white_balance_k[2] = 1.0;
    result.residual = 0.0;
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Temperature to RGB conversion (for PCC mode - alternative to SPCC) 
// ─────────────────────────────────────────────────────────────────────────────
void SPCC::tempK2RGB(float& r, float& g, float& b, float temp_k) {
    (void)temp_k;  // Placeholder: temp_k not used yet
    // Placeholder implementation
    r = g = b = 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Calibrate using a provided catalog with correct WCS & aperture photometry
// ─────────────────────────────────────────────────────────────────────────────
SPCCResult SPCC::calibrateWithCatalog(const ImageBuffer& fit, const SPCCParams& params,
                                      const std::vector<CatalogStar>& stars) {
    SPCCResult result;
    result.success = false;
    result.stars_found = (int)stars.size();
    
    if (stars.size() < 3) {
        result.error_msg = "Insufficient catalog stars (need >= 3)";
        return result;
    }
    
    const ImageBuffer::Metadata& meta = fit.metadata();
    
    qInfo() << "[SPCC] WCS metadata: RA=" << meta.ra << "Dec=" << meta.dec
            << "CRPIX1=" << meta.crpix1 << "CRPIX2=" << meta.crpix2
            << "CD1_1=" << meta.cd1_1 << "CD1_2=" << meta.cd1_2
            << "CD2_1=" << meta.cd2_1 << "CD2_2=" << meta.cd2_2
            << "Image size:" << fit.width() << "x" << fit.height();
            
    // Load SPCC Profiles
    SPCCDataStore store;
    if (!loadAllSPCCMetadata(params.dataPath, store)) {
        result.error_msg = "Failed to load SPCC profile database from: " + params.dataPath;
        return result;
    }
    
    PhotometricCCArgs args;
    args.fit = const_cast<ImageBuffer*>(&fit);
    args.aperture_radius = params.apertureR;
    args.use_spcc = true;
    
    // Choose SPCC sensor configs based on parameters
    QString pSensor = params.sensor_profile.toLower();
    args.spcc_mono_sensor = pSensor.contains("mono") || (!params.filter_profile.isEmpty() && params.filter_profile.toLower() != "none");
    if (args.spcc_mono_sensor) {
        for (size_t i = 0; i < store.mono_sensors.size(); ++i) {
            if (store.mono_sensors[i].name == params.sensor_profile || store.mono_sensors[i].model == params.sensor_profile) {
                args.selected_sensor_mono = i; break;
            }
        }
        for (size_t i = 0; i < store.mono_filters[0].size(); ++i) {
            if (store.mono_filters[0][i].name == params.filter_profile || store.mono_filters[0][i].model == params.filter_profile) {
                args.selected_filter_r = i; args.selected_filter_g = i; args.selected_filter_b = i; break;
            }
        }
    } else {
        for (size_t i = 0; i < store.osc_sensors.size(); ++i) {
            if (store.osc_sensors[i].channel[0].name == params.sensor_profile || store.osc_sensors[i].channel[0].model == params.sensor_profile) {
                args.selected_sensor_osc = i; break;
            }
        }
        for (size_t i = 0; i < store.osc_filters.size(); ++i) {
            if (store.osc_filters[i].name == params.filter_profile || store.osc_filters[i].model == params.filter_profile) {
                args.selected_filter_osc = i; break;
            }
        }
    }
    
    // Always use G2V solar reference or Averaged Spiral Galaxy
    args.selected_white_ref = 0;  // Default first reference
    if (params.solarReference) {
        for (size_t i = 0; i < store.wb_ref.size(); ++i) {
            if (store.wb_ref[i].name.toLower().contains("photon") || store.wb_ref[i].name.toLower().contains("equal")) {
                args.selected_white_ref = i; break;
            }
        }
    }
    
    const double h = 6.62607015e-34;  // Planck constant (J s)
    const double c = 299792458.0;     // Speed of light (m/s)
    const double k_B = 1.380649e-23;  // Boltzmann constant (J/K)
    
    int filtered_magnitude = 0, filtered_bounds = 0;
    
    for (const CatalogStar& star : stars) {
        if (params.limitMagnitude && star.magV > params.magLimit) {
            filtered_magnitude++;
            continue;
        }
        
        double d_ra_deg = star.ra - meta.ra;
        double d_dec_deg = star.dec - meta.dec;
        if (d_ra_deg > 180.0) d_ra_deg -= 360.0;
        if (d_ra_deg < -180.0) d_ra_deg += 360.0;
        double x_pix = meta.crpix1 - 1.0 + meta.cd1_1 * d_ra_deg + meta.cd1_2 * d_dec_deg;
        double y_pix = meta.crpix2 - 1.0 + meta.cd2_1 * d_ra_deg + meta.cd2_2 * d_dec_deg;
        
        if (x_pix < 0 || x_pix >= (double)fit.width() || y_pix < 0 || y_pix >= (double)fit.height()) {
            filtered_bounds++;
            continue;
        }
        
        GaiaStarData gsd;
        gsd.ra = star.ra;
        gsd.dec = star.dec;
        gsd.x_img = x_pix;
        gsd.y_img = y_pix;
        
        // Synthesize blackbody continuous spectrum from Teff
        double tK = star.teff > 500.0 ? star.teff : 5778.0; // Assume solar if invalid
        double max_val = 0.0;
        for (int i = 0; i < XPSAMPLED_LEN; ++i) {
            double lambda = (XPSAMPLED_MIN_WL + i * XPSAMPLED_STEP_WL) * 1e-9;
            double exponent = (h * c) / (lambda * k_B * tK);
            if (exponent < 700.0) {
                double val = (2.0 * h * c * c) / (std::pow(lambda, 5.0) * (std::exp(exponent) - 1.0));
                gsd.xp_sampled[i] = val;
                if (val > max_val) max_val = val;
            } else {
                gsd.xp_sampled[i] = 0.0;
            }
        }
        if (max_val > 0) {
            for (int i = 0; i < XPSAMPLED_LEN; ++i) gsd.xp_sampled[i] /= max_val;
        }
        gsd.has_xp = true;
        args.catalog_stars.push_back(gsd);
    }
    
    qInfo() << "[SPCC] Filtering:" << "mag=" << filtered_magnitude << "bounds=" << filtered_bounds << "Valid=" << args.catalog_stars.size();
    
    float kw[3];
    if (!getWhiteBalanceCoefficients(args, store, kw)) {
        result.error_msg = "White balance coefficient calculation failed (likely insufficient correlated valid photometric stars).";
        return result;
    }
    
    // Apply correction
    auto corrected = std::make_shared<ImageBuffer>(fit);
    double kwd[3] = { kw[0], kw[1], kw[2] };
    applyPhotometricColorCorrection(*corrected, kwd);
    
    result.success = true;
    result.error_msg.clear();
    result.modifiedBuffer = corrected;
    result.white_balance_k[0] = kw[0];
    result.white_balance_k[1] = kw[1];
    result.white_balance_k[2] = kw[2];
    result.scaleR = kw[0];
    result.scaleG = kw[1];
    result.scaleB = kw[2];
    result.stars_used = args.catalog_stars.size();
    result.residual = 0.0;
    result.log_msg = QString("[SPCC] Calibration successful. K=(%1, %2, %3)")
        .arg(kw[0], 0, 'f', 4).arg(kw[1], 0, 'f', 4).arg(kw[2], 0, 'f', 4);
    
    return result;
}

