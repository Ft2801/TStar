#pragma once
#ifndef PSFFITTER_H
#define PSFFITTER_H

#include <cmath>
#include <vector>
#include <cstddef>

// ─── Constants ───────────────────────
#define PSF_2_SQRT_2_LOG2   2.35482004503          // 2*sqrt(2*ln2)
#define PSF_INV_4_LOG2      0.360673760222241       // 1/(4*ln2)
#define PSF_MOFFAT_BETA_MAX 10.0                   // upper bound for Moffat beta

// ─── Profile selector ────────────────────────────────────────────────────────
enum class PsfProfile {
    Gaussian,        // 7-parameter fit: B A x0 y0 SX fc alpha
    Moffat           // 8-parameter fit: B A x0 y0 SX fc alpha fbeta (free beta)
};

// ─── PSF fitting error codes ─────────────────────────────────────────────────
enum class PsfError {
    OK              = 0,
    Alloc           = 3,
    Unsupported     = 4,
    Diverged        = 5,
    OutOfWindow     = 6,
    InnerTooSmall   = 7,
    ApertureTooSmall= 8,
    TooFewBgPix     = 9,
    MeanFailed      = 10,
    InvalidStdErr   = 11,
    InvalidPixVal   = 12,
    WindowTooSmall  = 13,
    InvalidImage    = 14,
    FluxRatio       = 15
};

// ─── Result struct ───────────────────────────────
struct PsfStar {
    PsfProfile profile = PsfProfile::Gaussian;

    double B   = 0.0;               // background level
    double A   = 0.0;               // peak amplitude
    double x0  = 0.0;               // centroid X  (in local box coordinates)
    double y0  = 0.0;               // centroid Y  (in local box coordinates)
    double sx  = 0.0;               // sigma_x (Gaussian) or Ro_x (Moffat)
    double sy  = 0.0;               // sigma_y (Gaussian) or Ro_y (Moffat)
    double fwhmx = 0.0;             // FWHM in X (pixels)
    double fwhmy = 0.0;             // FWHM in Y (pixels)
    double angle = 0.0;             // rotation angle (degrees)
    double rmse  = 0.0;             // RMS fit residual
    double beta  = -1.0;            // Moffat beta (-1 = Gaussian)
    double mag   = 0.0;             // instrumental magnitude estimate

    // Absolute uncertainties from covariance
    double B_err = 0.0, A_err = 0.0;
    double x_err = 0.0, y_err = 0.0;
    double sx_err = 0.0, sy_err = 0.0;
    double ang_err = 0.0, beta_err = 0.0;

    // Position in full-image space (set by caller)
    double xpos = 0.0;
    double ypos = 0.0;

    bool has_saturated = false;
    int  R = 0;                     // box half-radius used
    int  layer = 0;
};

// ─── Internal fit data passed to GSL ─────────────────────────────────────────
struct PsfFitData {
    size_t n;          // number of unmasked pixels
    double *y;         // observed pixel values (length n)
    size_t NbRows;     // box height
    size_t NbCols;     // box width
    double rmse;       // updated by residual function
    int    *mask;      // mask[row*NbCols+col]: 1 = valid pixel, 0 = excluded (saturated)
};

// ─── Main PSF fitter ─────────────────────────────────────────────────────────
class PsfFitter {
public:
    PsfFitter() = default;
    ~PsfFitter() = default;

    // -------------------------------------------------------------------------
    // fit() – fit a 2-D PSF to a data matrix stored row-major (row 0 = top).
    //
    //  data      : row-major array of NbRows * NbCols pixel values
    //  NbRows    : box height
    //  NbCols    : box width
    //  background: pre-estimated background value
    //  sat       : saturation level (pixels >= sat are excluded from fit)
    //  convergence: scaling factor for max iterations (1 = default, 2 = relaxed)
    //  fromPeaker: if true the centre pixel is taken as the maximum (faster init)
    //  profile   : Gaussian or Moffat
    //  error     : output error code (optional)
    // -------------------------------------------------------------------------
    static PsfStar* fit(const double* data,
                        size_t NbRows, size_t NbCols,
                        double background, double sat,
                        int convergence    = 1,
                        bool fromPeaker    = true,
                        PsfProfile profile = PsfProfile::Gaussian,
                        PsfError*  error   = nullptr);

    // Simple wrapper that returns nullptr on failure
    static PsfStar* fitMatrix(const std::vector<double>& data,
                              size_t NbRows, size_t NbCols,
                              double background, double sat,
                              int convergence = 1,
                              bool fromPeaker = true,
                              PsfProfile profile = PsfProfile::Gaussian,
                              PsfError*  error   = nullptr);

    // Free a heap-allocated PsfStar
    static void free(PsfStar* s) { delete s; }

    // FWHM helpers
    static double fwhm_from_s(double s, double beta, PsfProfile profile);
    static double s_from_fwhm(double fwhm, double beta, PsfProfile profile);

private:
    // ── Init: estimate initial parameter vector from data matrix ─────────────
    // Returns { A, x0, y0, FWHMx, FWHMy, angle_deg }  (6 elements)
    static bool initParams(const double* data, size_t NbRows, size_t NbCols,
                           double bg, bool fromPeaker,
                           double* A_out, double* x0_out, double* y0_out,
                           double* fwhmX_out, double* fwhmY_out, double* angle_out);

    // ── Approximate magnitude from summed flux ───────────────────────────────
    static double getMag(const double* data, size_t NbRows, size_t NbCols, double B);
};

#endif // PSFFITTER_H
