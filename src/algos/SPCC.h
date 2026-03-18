#pragma once
/*
 * SPCC.h  —  Spectrophotometric Color Calibration
 *
 * - Uses Gaia XP-sampled spectral data
 * - Online catalog queries via VizieR mirrors
 * - JSON-based sensor/filter profiles
 * - Siegel's Repeated Median regression for robustness
 */

#ifndef SPCC_H
#define SPCC_H

#include <vector>
#include <array>
#include <QString>
#include <QStringList>
#include <cstring>
#include "ImageBuffer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constants 
// ─────────────────────────────────────────────────────────────────────────────

// Gaia DR3 XP_SAMPLED wavelength grid: 336-1020nm at 2nm spacing = 343 points
static constexpr int XPSAMPLED_LEN = 343;
static constexpr double XPSAMPLED_MIN_WL = 336.0;
static constexpr double XPSAMPLED_MAX_WL = 1020.0;
static constexpr double XPSAMPLED_STEP_WL = 2.0;

// Data type enumerations 
enum SPCCType {
    MONO_SENSOR = 1,
    OSC_SENSOR,
    MONO_FILTER,
    OSC_FILTER,
    OSC_LPF,
    WB_REF
};

enum SPCCChannel {
    SPCC_RED   = 1 << 0,  // Bit 0
    SPCC_GREEN = 1 << 1,  // Bit 1
    SPCC_BLUE  = 1 << 2,  // Bit 2
    SPCC_CLEAR = 0x07,    // All channels
    SPCC_INVIS = 1 << 7   // Invisible/combined
};

enum CMFType {
    CMF_1931_2DEG = 0,
    CMF_1964_10DEG = 1
};

// ─────────────────────────────────────────────────────────────────────────────
// XPSampled: Gaia XP-sampled spectral data container
// ─────────────────────────────────────────────────────────────────────────────
struct XPSampled {
    const double *x = nullptr;          // Wavelength array (shared, don't free)
    double y[XPSAMPLED_LEN] = {};       // Spectral intensity at each wavelength
    
    // Initialize from wavelength grid
    static const double* wavelengthGrid() {
        static double wl[XPSAMPLED_LEN];
        static bool initialized = false;
        if (!initialized) {
            for (int i = 0; i < XPSAMPLED_LEN; ++i) {
                wl[i] = XPSAMPLED_MIN_WL + i * XPSAMPLED_STEP_WL;
            }
            initialized = true;
        }
        return wl;
    }
    
    XPSampled() {
        x = wavelengthGrid();
        std::fill(y, y + XPSAMPLED_LEN, 0.0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCCObject: Sensor/Filter profile (JSON-loaded)
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCObject {
    QString model;              ///< Device model name
    QString name;               ///< Friendly display name
    QString filepath;           ///< JSON file path (for lazy loading)
    QString comment;            ///< Optional description
    bool is_dslr = false;       ///< DSLR flag
    int index = 0;              ///< Index in JSON array
    int type = 0;               ///< SPCCType enum value
    int quality = 0;            ///< Data quality marker
    int channel = SPCC_CLEAR;   ///< Channel mask
    QString manufacturer;
    QString dataSource;
    int version = 0;
    
    bool arrays_loaded = false;     ///< Lazy loading flag
    std::vector<double> x;          ///< Wavelength array (lazy-loaded)
    std::vector<double> y;          ///< Response array (lazy-loaded)
    
    ~SPCCObject() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// OSCSensor: Three RGB channels for one-shot-color camera
// ─────────────────────────────────────────────────────────────────────────────
struct OSCSensor {
    SPCCObject channel[3];  // R, G, B channels
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCCDataStore: Complete profile database (loaded from JSON files)
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCDataStore {
    std::vector<SPCCObject> mono_sensors;
    std::vector<OSCSensor>  osc_sensors;
    std::vector<SPCCObject> mono_filters[4];  // R, G, B, Luminance
    std::vector<SPCCObject> osc_lpf;          // One-shot color low-pass filters
    std::vector<SPCCObject> osc_filters;      // One-shot color filters
    std::vector<SPCCObject> wb_ref;           // White references
};

// ─────────────────────────────────────────────────────────────────────────────
// Gaia Catalog Item
// ─────────────────────────────────────────────────────────────────────────────
struct GaiaStarData {
    double ra, dec;                 ///< Celestial coordinates (degrees)
    double pmra, pmdec;             ///< Proper motion (mas/yr)
    float gmag;                     ///< G magnitude
    float bp_mag, rp_mag;           ///< Bluepass and Redpass magnitudes
    float teff = 0.0f;              ///< Effective temperature (if available)
    uint64_t gaia_source_id = 0;
    std::array<double, XPSAMPLED_LEN> xp_sampled = {}; ///< XP spectral data
    bool has_xp = false;            ///< Whether xp_sampled is available
    
    // Image coordinates (computed via WCS)
    double x_img = 0.0, y_img = 0.0;  ///< Pixel coordinates in image
    
    double bv() const {
        // Convert BP-RP to B-V using Riello et al. (2021) formula
        double bprp = bp_mag - rp_mag;
        return 0.3930 + 0.4750 * bprp - 0.0548 * bprp * bprp;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Photometric Color Calibration Arguments 
// ─────────────────────────────────────────────────────────────────────────────
struct PhotometricCCArgs {
    // Image data
    ImageBuffer *fit = nullptr;
    
    // Star detection
    float fwhm = 5.0f;
    float t0 = 3.0f, t1 = 15.0f;   // Background tolerance in sigma
    int nb_stars = 0;
    
    // Background extraction
    bool bg_auto = true;
    double bg_mean[3] = {0.0, 0.0, 0.0};
    
    // Catalog/WCS
    std::vector<GaiaStarData> catalog_stars;
    bool has_wcs = false;
    
    // SPCC mode
    bool use_spcc = true;
    bool spcc_mono_sensor = false;  ///< true=mono+filters, false=OSC
    bool is_dslr = false;
    
    // Profile selection
    int selected_sensor_osc = 0;        ///< OSC sensor index
    int selected_sensor_mono = 0;       ///< Mono sensor index
    int selected_filter_osc = 0;        ///< OSC filter index
    int selected_filter_r = 0, selected_filter_g = 0, selected_filter_b = 0;  ///< Mono filters
    int selected_filter_lpf = 0;        ///< LPF index
    int selected_white_ref = 0;         ///< White reference index
    
    // Narrowband mode
    bool nb_mode = false;
    double nb_center[3] = {0.0, 0.0, 0.0};      ///< Narrowband center wavelengths (nm)
    double nb_bandwidth[3] = {10.0, 10.0, 10.0}; ///< Narrowband widths (nm)
    
    // Atmospheric correction
    bool atmos_correction = false;
    double observer_altitude = 0.0;  ///< meters
    double atmos_pressure = 1013.25; ///< hPa
    
    // Star magnitude limits
    bool magnitude_limit = true;
    double mag_limit = 13.5;
    
    // Aptertyre photometry
    double aperture_radius = 4.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCC Calibration Parameters
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCParams {
    // Profile selection (JSON files)
    QString sensor_profile;
    QString filter_profile;
    QString cameraProfile;              // Alias for sensor_profile (for SPCCDialog compatibility)
    QString filterProfile;              // Alias for filter_profile
    
    // Detection parameters
    int fwhm = 5;
    float t0 = 3.0f, t1 = 15.0f;
    
    // Calibration options
    bool use_online_catalog = true;
    bool useFullMatrix = false;
    bool solarReference = false;
    bool neutralBackground = false;
    
    // Photometry parameters
    double minSNR = 3.0;
    int maxStars = 50;
    double apertureR = 4.0;
    
    // Magnitude filtering
    bool limitMagnitude = true;
    double magLimit = 13.5;
    
    // Data path
    QString dataPath;
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCC Result
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCResult {
    bool success = false;
    QString error_msg;
    QString log_msg;
    
    int stars_used = 0;
    int stars_found = 0;
    
    double white_balance_k[3] = {1.0, 1.0, 1.0};  // K_R, K_G, K_B
    double scaleR = 1.0, scaleG = 1.0, scaleB = 1.0;
    double corrMatrix[3][3] = {{1,0,0}, {0,1,0}, {0,0,1}};
    double residual = 0.0;
    
    // Modified image buffer (if applicable)
    std::shared_ptr<ImageBuffer> modifiedBuffer = nullptr;
    
    // Diagnostic data
    struct DiagStar {
        double x_img, y_img;
        double flux_r, flux_g, flux_b;
        double predicted_rg, predicted_bg;
        double measured_rg, measured_bg;
        double bv;
        bool is_inlier = false;
    };
    std::vector<DiagStar> diagnostics;
};

// ─────────────────────────────────────────────────────────────────────────────
// Main SPCC Class
// ─────────────────────────────────────────────────────────────────────────────
class SPCC {
public:
    // ── JSON metadata loading ─────────────────────────────────────────────
    /// Recursively scan directory for sensor/filter profiles (JSON files)
    static bool loadAllSPCCMetadata(const QString& spcc_repo_path, SPCCDataStore& out);
    
    /// Get list of available camera profiles
    static QStringList availableCameraProfiles(const QString& dataPath);
    
    /// Get list of available filter profiles
    static QStringList availableFilterProfiles(const QString& dataPath);
    
    // ── Spectral analysis ─────────────────────────────────────────────────
    static SPCCResult calibrate(const ImageBuffer& fit, const SPCCParams& params);
    
    /// Calibrate using online Gaia DR3 catalog
    static SPCCResult calibrateWithCatalog(const ImageBuffer& fit, const SPCCParams& params,
                                           const std::vector<CatalogStar>& stars);
    
    // ── Color calibration ─────────────────────────────────────────────────
    static void applyPhotometricColorCorrection(ImageBuffer& fit, const double kw[3]);
    
    // ── Helper functions ──────────────────────────────────────────────────
    
    /// Get spectrum from args (load sensor + filter + atmospheric)
    static void getSpectrumFromArgs(const PhotometricCCArgs& args,
                                    const SPCCDataStore& store,
                                    XPSampled& out, int channel);
    
    /// Temperature to RGB (for PCC mode, alternative to SPCC)
    static void tempK2RGB(float& r, float& g, float& b, float temp_k);
    
private:
    // ── Spectral operations ───────────────────────────────────────────────
    
    /// Element-wise multiplication: result = a × b
    static void multiplyXPSampled(XPSampled& result,
                                  const XPSampled& a,
                                  const XPSampled& b);
    
    /// Integrate spectrum over wavelength range
    static double integrateXPSampled(const XPSampled& xps,
                                    double min_wl, double max_wl);
    
    /// Convert flux units: W m^-2 nm^-1 → relative photon count
    static void fluxToRelativePhotonCount(XPSampled& xps);
    
    /// Load spectral data from library to XPSampled grid (interpolate)
    static void initXPSampledFromLibrary(XPSampled& out,
                                        const SPCCObject& in);
    
    /// Atmospheric extinction model
    static void fillXPSampledFromAtmosModel(XPSampled& out,
                                           const PhotometricCCArgs& args);
    
    // ── Fitting ───────────────────────────────────────────────────────────
    
    /// Siegel's Repeated Median regression (robust to outliers)
    static bool repeatedMedianFit(const std::vector<double>& xdata,
                                  const std::vector<double>& ydata,
                                  double& intercept, double& slope,
                                  double& sigma,
                                  std::vector<bool>& mask_inliers);
    
    /// Compute white balance coefficients from star color measurements
    static bool getWhiteBalanceCoefficients(PhotometricCCArgs& args,
                                           const SPCCDataStore& store,
                                           float kw[3]);
    
    // ── Photometry ─────────────────────────────────────────────────────────
    
    struct AperturePhotometryResult {
        double flux_r, flux_g, flux_b;
        double snr;
        bool valid;
    };
    
    static AperturePhotometryResult aperturePhotometry(const ImageBuffer& buf,
                                                      double cx, double cy,
                                                      double aperture_radius);
};

#endif // SPCC_H
