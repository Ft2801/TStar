#pragma once
/*
 * SPCC.h  —  Spectrophotometric Color Calibration
 *
 * Algorithm overview:
 *   1. Load all SEDs, filters and sensor QE curves from tstar_data.fits (FITS binary tables,
 *      CTYPE = "SED" | "FILTER" | "SENSOR").
 *   2. Build common wavelength grid (3000–11000 Å, 1 Å step) matching the Python grid.
 *   3. Interpolate each curve onto that grid; combine as T_sys = T_filter * T_QE * T_LP1 * T_LP2.
 *   4. Detect stars with aperture photometry (background-subtracted annulus method).
 *   5. For every matched SIMBAD star: integrate its Pickles SED against T_sys_R/G/B to get
 *      expected flux ratios (S_R/S_G, S_B/S_G).
 *   6. Gaia XP fallback: for stars lacking a Pickles match, use cached Gaia XP spectra
 *      integrated against the same throughput curves.
 *   7. Fit measured vs. expected ratios using three competing models (slope-only, affine,
 *      quadratic); pick the model with the lowest RMS fractional residual.
 *   8. Apply per-pixel polynomial color mapping: R' = poly_R(R/G)*G, B' = poly_B(B/G)*G,
 *      anchored around the median pivot of each channel.
 *   9. Optionally remove a chromatic gradient (poly2 / poly3 / RBF surface fit in
 *      differential-magnitude space, clamped to ±0.05 mag peak).
 */

#ifndef SPCC_H
#define SPCC_H

#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <QString>
#include <QStringList>
#include <cstring>
#include <atomic>
#include "ImageBuffer.h"

// Forward declarations
struct CatalogStar;

// ─────────────────────────────────────────────────────────────────────────────
// Wavelength grid constants
// Matches Python: wl_grid = np.arange(3000, 11001)  [Angstroms, 1 Å step]
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int    WL_GRID_MIN_AA   = 3000;    ///< Grid start (Angstrom)
static constexpr int    WL_GRID_MAX_AA   = 11000;   ///< Grid end   (Angstrom)
static constexpr int    WL_GRID_LEN      = 8001;    ///< (11000 - 3000) + 1

// Gaia XP sampled grid kept for XP-data convolution (336–1020 nm, 2 nm step)
static constexpr int    XPSAMPLED_LEN    = 343;
static constexpr double XPSAMPLED_MIN_NM = 336.0;
static constexpr double XPSAMPLED_MAX_NM = 1020.0;
static constexpr double XPSAMPLED_STEP_NM = 2.0;

// ─────────────────────────────────────────────────────────────────────────────
// Data type enumerations
// ─────────────────────────────────────────────────────────────────────────────
enum SPCCType {
    MONO_SENSOR  = 1,
    OSC_SENSOR   = 2,
    MONO_FILTER  = 3,
    OSC_FILTER   = 4,
    OSC_LPF      = 5,
    WB_REF       = 6    ///< Stellar SED / white reference
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCCObject: one HDU entry from tstar_data.fits
// Arrays (x = wavelength in Angstrom, y = flux or throughput) are loaded
// eagerly when reading the FITS file and stored as std::vector<double>.
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCObject {
    QString name;               ///< EXTNAME value from FITS header
    QString model;              ///< Same as name (kept for API symmetry)
    int     type   = 0;         ///< SPCCType enum value
    bool    arrays_loaded = false;

    std::vector<double> x;     ///< Wavelength array, Angstrom (converted from raw units)
    std::vector<double> y;     ///< Flux [SED] or throughput [FILTER/SENSOR]
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCCDataStore: complete in-memory database loaded from tstar_data.fits
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCDataStore {
    std::vector<SPCCObject> sed_list;       ///< CTYPE = "SED"    (Pickles + galaxy templates)
    std::vector<SPCCObject> filter_list;    ///< CTYPE = "FILTER" (band-pass filters, LP filters)
    std::vector<SPCCObject> sensor_list;    ///< CTYPE = "SENSOR" (QE curves)
};

// ─────────────────────────────────────────────────────────────────────────────
// PicklesMatch: SIMBAD spectral type -> best-matching Pickles SED name
// ─────────────────────────────────────────────────────────────────────────────
struct PicklesMatch {
    QString sedName;      ///< Empty string = no match found
    bool    valid = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// StarRecord: one SIMBAD star with image-plane coordinates, spectral info,
//             optional Gaia source identifier.
// ─────────────────────────────────────────────────────────────────────────────
struct StarRecord {
    double x_img    = 0.0;      ///< Pixel column (SEP centroid)
    double y_img    = 0.0;      ///< Pixel row    (SEP centroid)
    double ra       = 0.0;      ///< ICRS right ascension (degrees)
    double dec      = 0.0;      ///< ICRS declination (degrees)
    double semi_a   = 1.5;      ///< SEP semi-major axis (pixels), used for aperture radius

    QString sp_type;            ///< Raw SIMBAD spectral type string
    QString sp_clean;           ///< Leading letter class, e.g. "G"
    QString pickles_match;      ///< Best-matching Pickles SED EXTNAME

    // Gaia cross-match
    qint64  gaia_source_id = 0;
    double  gaia_bp_rp = std::numeric_limits<double>::quiet_NaN();
    double  gaia_gmag  = std::numeric_limits<double>::quiet_NaN();
};

// ─────────────────────────────────────────────────────────────────────────────
// PhotometryResult: background-subtracted annulus aperture photometry
// Mirrors Python measure_star_rgb_photometry()
// ─────────────────────────────────────────────────────────────────────────────
struct PhotometryResult {
    double R_star = 0.0;    ///< Star-only flux (raw - background * aperture area)
    double G_star = 0.0;
    double B_star = 0.0;
    double R_bg   = 0.0;    ///< Background per pixel estimate
    double G_bg   = 0.0;
    double B_bg   = 0.0;
    bool   valid  = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// EnrichedMatch: output from the main matching loop (one valid star)
// ─────────────────────────────────────────────────────────────────────────────
struct EnrichedMatch {
    double x_img, y_img;
    double R_meas, G_meas, B_meas;   ///< Star-only measured flux per channel
    double S_star_R, S_star_G, S_star_B; ///< Expected synthetic integrals
    double exp_RG, exp_BG;            ///< Expected ratios  (S_R/S_G, S_B/S_G)
    double meas_RG, meas_BG;          ///< Measured ratios  (R_meas/G_meas, …)
    double r_ap, r_in, r_out;
    bool   used_gaia = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// CalibrationModel: which polynomial model was chosen and its coefficients
// coeff[0..2] = a, b, c for: a*x^2 + b*x + c
// ─────────────────────────────────────────────────────────────────────────────
enum ModelKind { MODEL_SLOPE_ONLY = 0, MODEL_AFFINE = 1, MODEL_QUADRATIC = 2 };

struct CalibrationModel {
    ModelKind kind = MODEL_AFFINE;
    double coeff_R[3] = {0.0, 1.0, 0.0};  ///< (a, b, c) for R channel
    double coeff_B[3] = {0.0, 1.0, 0.0};  ///< (a, b, c) for B channel
    double rms_total = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GradientSurface: per-channel polynomial or RBF correction surface
// ─────────────────────────────────────────────────────────────────────────────
struct GradientSurface {
    std::vector<float> R, G, B;   ///< Per-pixel multiplicative scale maps (H*W)
    int width = 0, height = 0;
    bool valid = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCCParams: inputs from the dialog
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCParams {
    // Equipment selections (must match EXTNAME values in tstar_data.fits)
    QString whiteRef;       ///< SED EXTNAME for reference star/galaxy (e.g. "A0V", "G2V")
    QString rFilter;        ///< Filter EXTNAME for R channel ("(None)" = flat 1)
    QString gFilter;        ///< Filter EXTNAME for G channel
    QString bFilter;        ///< Filter EXTNAME for B channel
    QString sensor;         ///< Sensor QE EXTNAME ("(None)" = flat 1)
    QString lpFilter1;      ///< LP/cut filter 1 EXTNAME ("(None)" = flat 1)
    QString lpFilter2;      ///< LP/cut filter 2 EXTNAME ("(None)" = flat 1)

    // Processing options
    QString bgMethod;       ///< Background model: "None"|"Simple"|"Poly2"|"Poly3"|"RBF"
    QString dataPath;       ///< Directory containing tstar_data.fits

    double  sepThreshold    = 5.0;   ///< SEP detection threshold in sigma
    int     maxStars        = 300;   ///< Cap on SEP detections
    bool    gaiaFallback    = true;  ///< Use Gaia XP spectra for unmatched stars
    bool    useFullMatrix   = true;  ///< Estimate full 3x3 or diagonal only
    bool    linearMode      = true;  ///< If true, apply global multipliers. If false, apply non-linear polynomial.

    // Optional gradient extraction
    bool    runGradient     = false;
    QString gradientMethod  = "poly3";   ///< "poly2"|"poly3"|"rbf"

    // Progress reporting: callback(percent 0-100, message)
    std::function<void(int, const QString&)> progressCallback;
    
    // Cancellation flag
    std::atomic<bool>* cancelFlag = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCCResult
// ─────────────────────────────────────────────────────────────────────────────
struct SPCCResult {
    bool    success   = false;
    QString error_msg;
    QString log_msg;

    int     stars_found  = 0;   ///< Stars from catalog input
    int     stars_used   = 0;   ///< Stars that survived photometry + matching
    double  residual     = 0.0; ///< Combined RMS fractional residual

    // Scale factors (evaluated at x=1 for display)
    double scaleR = 1.0, scaleG = 1.0, scaleB = 1.0;

    // 3x3 correction matrix (diagonal if !useFullMatrix)
    double corrMatrix[3][3] = {{1,0,0}, {0,1,0}, {0,0,1}};

    // White balance multipliers kept for compatibility
    double white_balance_k[3] = {1.0, 1.0, 1.0};

    // Modified output image
    std::shared_ptr<ImageBuffer> modifiedBuffer = nullptr;

    // Per-star diagnostics
    struct DiagStar {
        double x_img, y_img;
        double meas_RG, meas_BG;
        double exp_RG,  exp_BG;
        bool   is_inlier = true;
    };
    std::vector<DiagStar> diagnostics;

    CalibrationModel model;
};

// ─────────────────────────────────────────────────────────────────────────────
// XPSampled: Gaia XP-sampled spectral container (336-1020 nm, 2 nm step)
// Used when integrating Gaia XP spectra against system throughput.
// ─────────────────────────────────────────────────────────────────────────────
struct XPSampled {
    double wl[XPSAMPLED_LEN] = {};  ///< Wavelength in nm
    double flux[XPSAMPLED_LEN] = {};

    XPSampled() {
        for (int i = 0; i < XPSAMPLED_LEN; ++i)
            wl[i] = XPSAMPLED_MIN_NM + i * XPSAMPLED_STEP_NM;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SPCC: main namespace class
// All public methods are static; the dialog calls calibrateWithCatalog().
// ─────────────────────────────────────────────────────────────────────────────
class SPCC {
public:
    // ── Database I/O ─────────────────────────────────────────────────────────

    /// Load all binary-table HDUs from tstar_data.fits into an SPCCDataStore.
    /// CTYPE = "SED" -> sed_list, "FILTER" -> filter_list, "SENSOR" -> sensor_list.
    /// Wavelengths are converted to Angstrom if they appear to be in nm.
    static bool loadTStarFits(const QString& dataPath, SPCCDataStore& out);

    /// Load TStar JSON database from a directory (with subdirs mono_filters, etc.)
    static bool loadTStarDatabase(const QString& dbPath, SPCCDataStore& out);

    /// Convenience: SED / filter / sensor name lists for populating combo boxes.
    static QStringList availableSEDs     (const SPCCDataStore& store);
    static QStringList availableFilters  (const SPCCDataStore& store);
    static QStringList availableSensors  (const SPCCDataStore& store);

    // ── Main entry point ─────────────────────────────────────────────────────

    /// Full calibration pipeline.  Called from SPCCDialog via QtConcurrent::run().
    /// @param buf         Input image (RGB float32 in [0,1] or uint16).
    /// @param params      User-chosen equipment and options.
    /// @param starRecords SIMBAD star list with pixel coords and spectral types,
    ///                    produced by the "Fetch Stars" step in the dialog.
    static SPCCResult calibrateWithStarList(const ImageBuffer& buf,
                                            const SPCCParams&  params,
                                            const std::vector<StarRecord>& starRecords);

    /// Legacy entry point kept for compatibility with existing dialog code.
    static SPCCResult calibrateWithCatalog(const ImageBuffer& buf,
                                           const SPCCParams&  params,
                                           const std::vector<CatalogStar>& stars);

    // ── Spectral utilities (public for diagnostics / SaspViewer) ─────────────

    /// Interpolate a raw curve (x_aa, y) onto the common wavelength grid.
    /// @param wl_aa   Wavelengths in Angstrom (input curve, arbitrary sampling).
    /// @param vals    Throughput or flux values corresponding to wl_aa.
    /// @param out     Output array of length WL_GRID_LEN (3000-11000 Å, 1 Å step).
    static void interpolateToGrid(const std::vector<double>& wl_aa,
                                  const std::vector<double>& vals,
                                  double out[WL_GRID_LEN]);

    /// Trapezoidal integral of f[]*g[] on the common grid (1 Å spacing).
    static double trapz(const double f[WL_GRID_LEN], const double g[WL_GRID_LEN]);

    /// Build the total system throughput T_sys = T_filter * T_QE * T_LP1 * T_LP2.
    /// Any component whose name is "(None)" is treated as a flat response of 1.
    static void buildSystemThroughput(const SPCCDataStore& store,
                                      const QString& filterName,
                                      const QString& sensorName,
                                      const QString& lp1Name,
                                      const QString& lp2Name,
                                      const QString& channel,
                                      double T_sys[WL_GRID_LEN]);

    // ── Pickles spectral type matching ────────────────────────────────────────

    /// Map a SIMBAD spectral type string to a ranked list of matching Pickles SED names.
    /// Returns an empty list if no match is found.
    static QStringList picklesMatchForSimbad(const QString& simbadSp,
                                             const QStringList& availableSEDs);

    /// Infer a Pickles spectral type (e.g. "G2V") from a Gaia BP-RP colour index.
    static QString inferTypeFromBpRp(double bp_rp);
    static double  bpRpFromType(const QString& spec);

    // ── Aperture photometry ───────────────────────────────────────────────────

    /// Background-subtracted aperture photometry (mirrors Python measure_star_rgb_photometry).
    /// @param img_float  RGB image, float32, values in [0,1], shape H x W x 3 (row-major).
    /// @param width, height  Image dimensions.
    /// @param cx, cy     Aperture centre (pixel).
    /// @param r          Aperture radius.
    /// @param r_in       Inner annulus radius.
    /// @param r_out      Outer annulus radius.
    static PhotometryResult aperturePhotometry(const float* img_float,
                                               int width, int height,
                                               double cx, double cy,
                                               double r, double r_in, double r_out);

    // ── Calibration model selection ───────────────────────────────────────────

    /// Fit three competing models (slope-only, affine, quadratic) to (meas, expected)
    /// ratio pairs and return the model with the lowest combined RMS fractional residual.
    /// Mirrors the Python model selection block in run_spcc().
    static CalibrationModel fitColorModel(const std::vector<double>& meas_RG,
                                          const std::vector<double>& exp_RG,
                                          const std::vector<double>& meas_BG,
                                          const std::vector<double>& exp_BG);

    // ── Image correction ─────────────────────────────────────────────────────

    /// Apply polynomial color mapping to every pixel.
    /// R' = pivot_R + (R - pivot_R) * poly_R(R/G)
    /// B' = pivot_B + (B - pivot_B) * poly_B(B/G)
    /// Result is clamped to [0,1].
    static void applyColorModel(float* img_float,
                                int width, int height,
                                const CalibrationModel& model,
                                double pivot_R, double pivot_G, double pivot_B);

    // ── Gradient extraction ───────────────────────────────────────────────────

    /// Compute and return a per-channel multiplicative correction surface
    /// using the differential-magnitude approach.  The surface peak is clamped
    /// to max_allowed_mag (default 0.05 mag).
    static GradientSurface computeGradientSurface(
                                const float* img_float,
                                int width, int height,
                                const std::vector<EnrichedMatch>& matches,
                                const double T_sys_R[WL_GRID_LEN],
                                const double T_sys_G[WL_GRID_LEN],
                                const double T_sys_B[WL_GRID_LEN],
                                const QString& method,
                                double max_allowed_mag = 0.05);

    /// Apply a gradient surface to img_float in-place.
    static void applyGradientSurface(float* img_float, int width, int height,
                                     const GradientSurface& surf);

    // ── Misc ──────────────────────────────────────────────────────────────────

    /// Convert wavelengths to Angstrom if they appear to be in nm (median < 2000).
    static bool detectAndConvertToAngstrom(std::vector<double>& wl);

private:
    // ── Internal helpers ─────────────────────────────────────────────────────

    /// Compute RMS fractional residual: sqrt(mean(((pred/exp)-1)^2)).
    static double rmsFrac(const std::vector<double>& pred,
                          const std::vector<double>& exp_vals);

    /// Polynomial evaluation:  a*x^2 + b*x + c.
    static inline double polyEval(const double coeff[3], double x) {
        return coeff[0] * x * x + coeff[1] * x + coeff[2];
    }

    /// Least-squares fit of 2-degree polynomial y = a*x^2 + b*x + c.
    static bool polyFit2(const std::vector<double>& x,
                         const std::vector<double>& y,
                         double& a, double& b, double& c);

    /// Poly2 surface fit (spatial gradient model).
    static std::vector<double> fitPoly2Surface(
                                    const std::vector<std::array<double,2>>& pts,
                                    const std::vector<double>& vals,
                                    int width, int height);

    /// Poly3 surface fit (spatial gradient model).
    static std::vector<double> fitPoly3Surface(
                                    const std::vector<std::array<double,2>>& pts,
                                    const std::vector<double>& vals,
                                    int width, int height);

    /// Trapezoidal integral on a generic 1-D grid.
    static double trapzGeneric(const double* f, const double* x, int n);

    /// Median of a vector (copies, so safe to call with temporary).
    static double vectorMedian(std::vector<double> v);

    /// Lookup curve by name in a list; returns nullptr if not found.
    static const SPCCObject* findCurve(const std::vector<SPCCObject>& list,
                                       const QString& name);
};

#endif // SPCC_H