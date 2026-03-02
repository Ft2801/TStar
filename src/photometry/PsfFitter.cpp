/*
 * PSF fitting module (Gaussian + Moffat) using GSL nonlinear least squares.
 *
 *   - Gaussian 2-D model with 7 parameters: B, A, x0, y0, SX, fc, alpha
 *   - Moffat  2-D model with 8 parameters: B, A, x0, y0, SX, fc, alpha, fbeta
 *
 * Parameter encoding (bounded via trigonometric transforms, same as reference):
 *   SX    = |fit[4]|                         (sigma^2 or Ro^2, positive)
 *   r     = 0.5 * (cos(fit[5]) + 1)          (roundness ∈ [0,1])
 *   SY    = r^2 * SX
 *   alpha = fit[6]                            (rotation angle, radians)
 *   beta  = BETA_MAX * 0.5 * (cos(fit[7])+1) (Moffat beta ∈ [0, BETA_MAX])
 */

#include "PsfFitter.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cassert>

#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_statistics_double.h>

// ─── Iteration limits ────────────────────────────────────────────────────────
static constexpr int MAX_ITER_ANGLE = 20;
static constexpr double XTOL = 1e-3;
static constexpr double GTOL = 1e-3;
static constexpr double FTOL = 1e-3;
static constexpr int MIN_HALF_RADIUS    = 1;
static constexpr int MIN_LARGE_SAMPLING = 3;

// SQR helper
template <typename T> static inline T SQR(T x) { return x * x; }

// ─── FWHM / sigma conversions ─────────────────────────
double PsfFitter::fwhm_from_s(double s, double beta, PsfProfile profile) {
    if (profile == PsfProfile::Gaussian)
        return s * PSF_2_SQRT_2_LOG2;
    return 2.0 * s * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
}

double PsfFitter::s_from_fwhm(double fwhm, double beta, PsfProfile profile) {
    if (profile == PsfProfile::Gaussian)
        return SQR(fwhm) * PSF_INV_4_LOG2;
    return SQR(fwhm) * 0.25 / (std::pow(2.0, 1.0 / beta) - 1.0);
}

// ─── Approximate magnitude ────────────────────────────────────────────────────
double PsfFitter::getMag(const double* data, size_t NbRows, size_t NbCols, double B) {
    double intensity = 0.0;
    for (size_t i = 0; i < NbRows * NbCols; i++)
        intensity += data[i] - B;
    if (intensity <= 0.0)
        return 99.0;
    return -2.5 * std::log10(intensity);
}

// ─── Initial parameter estimation ────────────────────────────────────────────
bool PsfFitter::initParams(const double* data, size_t NbRows, size_t NbCols,
                            double bg, bool fromPeaker,
                            double* A_out, double* x0_out, double* y0_out,
                            double* fwhmX_out, double* fwhmY_out, double* angle_out)
{
    // Build a background-subtracted copy
    std::vector<double> m(NbRows * NbCols);
    if (fromPeaker) {
        for (size_t i = 0; i < NbRows * NbCols; i++)
            m[i] = data[i] - bg;
    } else {
        // Smooth with a 3×3 median-like op (simplified: just subtract bg)
        for (size_t i = 0; i < NbRows * NbCols; i++)
            m[i] = data[i] - bg;
    }

    double max_val;
    size_t ci = NbRows / 2, cj = NbCols / 2;
    if (!fromPeaker) {
        max_val = *std::max_element(m.begin(), m.end());
        for (size_t r = 0; r < NbRows && ci == NbRows/2; r++)
            for (size_t c = 0; c < NbCols; c++)
                if (m[r * NbCols + c] == max_val) { ci = r; cj = c; break; }
    } else {
        max_val = m[ci * NbCols + cj];
    }
    if (max_val <= 0.0) return false;
    *A_out = max_val;

    double halfA = max_val * 0.5;
    int yc = (int)ci, xc = (int)cj;

    // Walk from centre to find half-amplitude extents
    int ii1 = yc + MIN_HALF_RADIUS, ii2 = yc - MIN_HALF_RADIUS;
    int jj1 = xc + MIN_HALF_RADIUS, jj2 = xc - MIN_HALF_RADIUS;
    while (ii1 < (int)NbRows-1 && m[(ii1+1)*NbCols + xc] > halfA) ii1++;
    while (ii2 > 0            && m[(ii2-1)*NbCols + xc] > halfA) ii2--;
    while (jj1 < (int)NbCols-1 && m[yc*NbCols + jj1+1] > halfA) jj1++;
    while (jj2 > 0             && m[yc*NbCols + jj2-1] > halfA) jj2--;

    double FWHMx = jj1 - jj2;
    double FWHMy = ii1 - ii2;

    if (std::min(FWHMx, FWHMy) < MIN_LARGE_SAMPLING) {
        // Small star: simple init
        *x0_out    = (jj1 + jj2 + 1) / 2.0;
        *y0_out    = (ii1 + ii2 + 1) / 2.0;
        *fwhmX_out = std::max(FWHMx, FWHMy);
        *fwhmY_out = std::min(FWHMx, FWHMy);
        *angle_out = 0.0;
        return true;
    }

    // Larger star: use moment analysis (inertia matrix → SVD → angle + FWHM)
    // Scan rows down from yc then up from yc-1, collecting trace pixels above halfA
    double S = 0, Sx = 0, Sy = 0, Ixx = 0, Iyy = 0, Ixy = 0;

    auto scanRow = [&](int y, int &cr, int &cl) {
        // Expand or contract right cursor
        if (cr < (int)NbCols-1 && m[y*NbCols+cr] > halfA) {
            while (cr < (int)NbCols-1 && m[y*NbCols+cr+1] > halfA) cr++;
        } else {
            cr--;
            while (cr > 0 && cr > cl && m[y*NbCols+cr-1] <= halfA) cr--;
        }
        // Expand or contract left cursor
        if (cl >= 0 && m[y*NbCols+cl] > halfA) {
            while (cl > 1 && m[y*NbCols+cl-1] > halfA) cl--;
        } else {
            if (cl < cr) {
                cl++;
                while (cl < cr && m[y*NbCols+cl+1] <= halfA) cl++;
            }
        }
        if (cr == cl && m[y*NbCols+cr] <= halfA) return false;
        for (int x = cl; x <= cr; x++) {
            S   += 1.0;
            Sx  += (double)x;
            Sy  += (double)y;
            Ixx += (double)(y * y);
            Iyy += (double)(x * x);
            Ixy += (double)(x * y);
        }
        return true;
    };

    int cr = jj1, cl = jj2;
    for (int y = yc; y < (int)NbRows; y++) {
        if (!scanRow(y, cr, cl)) break;
    }
    cr = jj1; cl = jj2;
    for (int y = yc - 1; y >= 0; y--) {
        if (!scanRow(y, cr, cl)) break;
    }

    if (S < 1.0) {
        *x0_out    = xc + 0.5;
        *y0_out    = yc + 0.5;
        *fwhmX_out = FWHMx;
        *fwhmY_out = FWHMy;
        *angle_out = 0.0;
        return true;
    }

    double x0 = Sx / S, y0 = Sy / S;
    Ixx -= S * y0 * y0;
    Iyy -= S * x0 * x0;
    Ixy -= S * x0 * y0;

    // SVD-like decomposition for the 2×2 inertia matrix
    double Su00 = Ixx * Ixx + Ixy * Ixy;
    double Su01 = (Ixx + Iyy) * Ixy;
    double Su11 = Iyy * Iyy + Ixy * Ixy;
    double ang  = 90.0 + 0.5 * std::atan2(2.0 * Su01, Su00 - Su11) * 180.0 / M_PI;
    double SUsum = Su00 + Su11;
    double SUdif = std::sqrt(SQR(Su00 - Su11) + 4.0 * SQR(Su01));
    double Ival1 = std::sqrt((SUsum + SUdif) * 0.5);
    double Ival2 = std::sqrt((SUsum - SUdif) * 0.5);
    double r     = (Ival1 > 0) ? std::sqrt(Ival2 / Ival1) : 1.0;
    double FWHM  = 2.0 * std::sqrt(S / M_PI / std::max(r, 1e-6));

    *x0_out    = x0  + 0.5;
    *y0_out    = y0  + 0.5;
    *fwhmX_out = FWHM;
    *fwhmY_out = FWHM * r;
    *angle_out = ang;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// GSL residual / Jacobian functions
// ─────────────────────────────────────────────────────────────────────────────

// ── Gaussian ──────────────────────────────────────────────────────────────────
static int gaussianF(const gsl_vector *x, void *data, gsl_vector *f) {
    PsfFitData *d = (PsfFitData*)data;
    size_t NbRows = d->NbRows, NbCols = d->NbCols, k = 0;
    double B     = gsl_vector_get(x, 0);
    double A     = gsl_vector_get(x, 1);
    double x0    = gsl_vector_get(x, 2);
    double y0    = gsl_vector_get(x, 3);
    double SX    = std::fabs(gsl_vector_get(x, 4));
    double r     = 0.5 * (std::cos(gsl_vector_get(x, 5)) + 1.0);
    double SY    = SQR(r) * SX;
    double alpha = gsl_vector_get(x, 6);
    double ca    = std::cos(alpha), sa = std::sin(alpha);
    double sumres = 0.0;
    for (size_t i = 0; i < NbRows; i++) {
        for (size_t j = 0; j < NbCols; j++) {
            if (!d->mask[i * NbCols + j]) continue;
            double tmpx = ca*(j+0.5-x0) - sa*(i+0.5-y0);
            double tmpy = sa*(j+0.5-x0) + ca*(i+0.5-y0);
            double tmpc = std::exp(-(SQR(tmpx)/SX + SQR(tmpy)/SY));
            double res  = B + A*tmpc - d->y[k];
            gsl_vector_set(f, k, res);
            sumres += res * res;
            k++;
        }
    }
    d->rmse = std::sqrt(sumres / d->n);
    return GSL_SUCCESS;
}

static int gaussianDF(const gsl_vector *x, void *data, gsl_matrix *J) {
    PsfFitData *d = (PsfFitData*)data;
    size_t NbRows = d->NbRows, NbCols = d->NbCols, k = 0;
    double A     = gsl_vector_get(x, 1);
    double x0    = gsl_vector_get(x, 2);
    double y0    = gsl_vector_get(x, 3);
    double SX    = std::fabs(gsl_vector_get(x, 4));
    double fc    = gsl_vector_get(x, 5);
    double r     = 0.5 * (std::cos(fc) + 1.0);
    double sc    = std::sin(fc);
    double SY    = SQR(r) * SX;
    double alpha = gsl_vector_get(x, 6);
    double ca    = std::cos(alpha), sa = std::sin(alpha);
    for (size_t i = 0; i < NbRows; i++) {
        for (size_t j = 0; j < NbCols; j++) {
            if (!d->mask[i * NbCols + j]) continue;
            double tmpx = ca*(j+0.5-x0) - sa*(i+0.5-y0);
            double tmpy = sa*(j+0.5-x0) + ca*(i+0.5-y0);
            double tmpc = std::exp(-(SQR(tmpx)/SX + SQR(tmpy)/SY));
            gsl_matrix_set(J, k, 0, 1.0);
            gsl_matrix_set(J, k, 1, tmpc);
            gsl_matrix_set(J, k, 2,  2*A*tmpc*(  tmpx/SX*ca + tmpy/SY*sa));
            gsl_matrix_set(J, k, 3,  2*A*tmpc*( -tmpx/SX*sa + tmpy/SY*ca));
            gsl_matrix_set(J, k, 4,  A*tmpc*(SQR(tmpx/SX) + SQR(tmpy/SX/r)));
            gsl_matrix_set(J, k, 5, -A*tmpc*sc*SQR(tmpy)/SY/r);
            gsl_matrix_set(J, k, 6,  2*A*tmpc*tmpx*tmpy*(1.0/SX - 1.0/SY));
            k++;
        }
    }
    return GSL_SUCCESS;
}

// ── Moffat ───────────────────────────────────────────────────────────────────
static int moffatF(const gsl_vector *x, void *data, gsl_vector *f) {
    PsfFitData *d = (PsfFitData*)data;
    size_t NbRows = d->NbRows, NbCols = d->NbCols, k = 0;
    double B     = gsl_vector_get(x, 0);
    double A     = gsl_vector_get(x, 1);
    double x0    = gsl_vector_get(x, 2);
    double y0    = gsl_vector_get(x, 3);
    double SX    = std::fabs(gsl_vector_get(x, 4));
    double r     = 0.5 * (std::cos(gsl_vector_get(x, 5)) + 1.0);
    double SY    = SQR(r) * SX;
    double alpha = gsl_vector_get(x, 6);
    double beta  = PSF_MOFFAT_BETA_MAX * 0.5 * (std::cos(gsl_vector_get(x, 7)) + 1.0);
    double ca    = std::cos(alpha), sa = std::sin(alpha);
    double sumres = 0.0;
    for (size_t i = 0; i < NbRows; i++) {
        for (size_t j = 0; j < NbCols; j++) {
            if (!d->mask[i * NbCols + j]) continue;
            double tmpx = ca*(j+0.5-x0) - sa*(i+0.5-y0);
            double tmpy = sa*(j+0.5-x0) + ca*(i+0.5-y0);
            double tmpa = std::pow(1.0 + SQR(tmpx)/SX + SQR(tmpy)/SY, -beta);
            double res  = B + A*tmpa - d->y[k];
            gsl_vector_set(f, k, res);
            sumres += res * res;
            k++;
        }
    }
    d->rmse = std::sqrt(sumres / d->n);
    return GSL_SUCCESS;
}

static int moffatDF(const gsl_vector *x, void *data, gsl_matrix *J) {
    PsfFitData *d = (PsfFitData*)data;
    size_t NbRows = d->NbRows, NbCols = d->NbCols, k = 0;
    double A     = gsl_vector_get(x, 1);
    double x0    = gsl_vector_get(x, 2);
    double y0    = gsl_vector_get(x, 3);
    double SX    = std::fabs(gsl_vector_get(x, 4));
    double fc    = gsl_vector_get(x, 5);
    double r     = 0.5 * (std::cos(fc) + 1.0);
    double sc    = std::sin(fc);
    double SY    = SQR(r) * SX;
    double alpha = gsl_vector_get(x, 6);
    double ca    = std::cos(alpha), sa = std::sin(alpha);
    double fb    = gsl_vector_get(x, 7);
    double beta  = PSF_MOFFAT_BETA_MAX * 0.5 * (std::cos(fb) + 1.0);
    double sfb   = std::sin(fb);
    for (size_t i = 0; i < NbRows; i++) {
        for (size_t j = 0; j < NbCols; j++) {
            if (!d->mask[i * NbCols + j]) continue;
            double tmpx = ca*(j+0.5-x0) - sa*(i+0.5-y0);
            double tmpy = sa*(j+0.5-x0) + ca*(i+0.5-y0);
            double tmpa = 1.0 + SQR(tmpx)/SX + SQR(tmpy)/SY;
            double tmpb = std::pow(tmpa, -beta);
            double tmpc = A * beta * std::pow(tmpa, -beta - 1.0);
            gsl_matrix_set(J, k, 0, 1.0);
            gsl_matrix_set(J, k, 1, tmpb);
            gsl_matrix_set(J, k, 2,  2*tmpc*(  tmpx/SX*ca + tmpy/SY*sa));
            gsl_matrix_set(J, k, 3,  2*tmpc*( -tmpx/SX*sa + tmpy/SY*ca));
            gsl_matrix_set(J, k, 4,  tmpc*(SQR(tmpx/SX) + SQR(tmpy/SX/r)));
            gsl_matrix_set(J, k, 5, -tmpc*sc*SQR(tmpy)/SY/r);
            gsl_matrix_set(J, k, 6,  2*tmpc*tmpx*tmpy*(1.0/SX - 1.0/SY));
            gsl_matrix_set(J, k, 7,  0.5*A*PSF_MOFFAT_BETA_MAX*sfb*std::log(tmpa)*tmpb);
            k++;
        }
    }
    return GSL_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// PsfFitter::fit — main entry point
// ─────────────────────────────────────────────────────────────────────────────
PsfStar* PsfFitter::fit(const double* data,
                         size_t NbRows, size_t NbCols,
                         double background, double sat,
                         int convergence, bool fromPeaker,
                         PsfProfile profile, PsfError* error)
{
    if (error) *error = PsfError::OK;
    const size_t p = (profile == PsfProfile::Gaussian) ? 7 : 8;

    // Build integer mask: 1 = valid pixel (below saturation), 0 = excluded
    size_t n = 0;
    std::vector<int> mask(NbRows * NbCols, 0);
    for (size_t i = 0; i < NbRows * NbCols; i++) {
        if (data[i] < sat) { mask[i] = 1; n++; }
    }
    if (n <= p) {
        if (error) *error = PsfError::WindowTooSmall;
        return nullptr;
    }

    // Collect observed values for valid (unmasked) pixels
    std::vector<double> y(n);
    { size_t k = 0; for (size_t i = 0; i < NbRows * NbCols; i++) if (mask[i]) y[k++] = data[i]; }

    // ── Initial parameter estimation ─────────────────────────────────────────
    double A_init, x0_init, y0_init, fwhmX, fwhmY, angle_deg;
    if (!initParams(data, NbRows, NbCols, background, fromPeaker,
                    &A_init, &x0_init, &y0_init, &fwhmX, &fwhmY, &angle_deg)) {
        if (error) *error = PsfError::Alloc;
        return nullptr;
    }

    // Roundness init (keep away from boundary 1.0 exactly)
    double roundness = (fwhmX > 0) ? fwhmY / fwhmX : 1.0;
    if (roundness >= 1.0) { roundness = 0.9; angle_deg = 0.0; }
    roundness = std::max(std::min(roundness, 0.9999), 1e-4);

    double a_init   = angle_deg * M_PI / 180.0;
    double fr       = std::acos(2.0 * roundness - 1.0);
    double beta_init = (profile == PsfProfile::Gaussian) ? -1.0 : 2.0;
    double fbeta_init = (profile == PsfProfile::Gaussian) ? 0.0
                        : std::acos(2.0 * beta_init / PSF_MOFFAT_BETA_MAX - 1.0);

    double SX_init  = s_from_fwhm(fwhmX, beta_init, profile);
    if (SX_init <= 0.0) SX_init = 1.0;

    double x_init[8] = {
        background, A_init,
        x0_init, y0_init,
        SX_init, fr,
        a_init,  fbeta_init
    };
    gsl_vector_view xv = gsl_vector_view_array(x_init, p);

    // Configure fit data and GSL solver (Levenberg-Marquardt trust-region)
    PsfFitData fitdata;
    fitdata.n      = n;
    fitdata.y      = y.data();
    fitdata.NbRows = NbRows;
    fitdata.NbCols = NbCols;
    fitdata.rmse   = 0.0;
    fitdata.mask   = mask.data();

    gsl_multifit_nlinear_fdf fdf;
    fdf.f   = (profile == PsfProfile::Gaussian) ? gaussianF  : moffatF;
    fdf.df  = (profile == PsfProfile::Gaussian) ? gaussianDF : moffatDF;
    fdf.fvv = nullptr;
    fdf.n   = n;
    fdf.p   = p;
    fdf.params = &fitdata;

    gsl_multifit_nlinear_parameters fdf_params = gsl_multifit_nlinear_default_parameters();
    fdf_params.trs = gsl_multifit_nlinear_trs_lm;

    const gsl_multifit_nlinear_type *T = gsl_multifit_nlinear_trust;
    gsl_multifit_nlinear_workspace *work = gsl_multifit_nlinear_alloc(T, &fdf_params, n, p);
    if (!work) {
        if (error) *error = PsfError::Alloc;
        return nullptr;
    }

    gsl_multifit_nlinear_init(&xv.vector, &fdf, work);

    int max_iter = MAX_ITER_ANGLE
                   * (n < NbRows * NbCols ? 3 : 1)
                   * convergence
                   * (profile == PsfProfile::Gaussian ? 1 : 2);

    int info = 0;
    int status = gsl_multifit_nlinear_driver(max_iter, XTOL, GTOL, FTOL,
                                              nullptr, nullptr, &info, work);

    // ── Extract results ───────────────────────────────────────────────────────
    gsl_matrix *covar = gsl_matrix_alloc(p, p);
    if (covar) {
        gsl_matrix *Jac = gsl_multifit_nlinear_jac(work);
        gsl_multifit_nlinear_covar(Jac, 0.0, covar);
    }

#define FIT(i) gsl_vector_get(work->x, i)
#define ERR(i) (covar ? std::sqrt(gsl_matrix_get(covar, i, i)) : 0.0)

    PsfStar* psf = new PsfStar();
    psf->profile = profile;
    psf->B   = FIT(0);
    psf->A   = FIT(1);
    psf->x0  = FIT(2);
    psf->y0  = FIT(3);
    psf->beta = (profile == PsfProfile::Gaussian)
                ? -1.0
                : PSF_MOFFAT_BETA_MAX * 0.5 * (std::cos(FIT(7)) + 1.0);
    double r_fit = 0.5 * (std::cos(FIT(5)) + 1.0);
    psf->sx = (profile == PsfProfile::Gaussian)
              ? std::sqrt(std::fabs(FIT(4)) * 0.5)   // Gaussian: sigma
              : std::sqrt(std::fabs(FIT(4)));          // Moffat:   Ro
    psf->sy     = psf->sx * r_fit;
    psf->fwhmx  = fwhm_from_s(psf->sx, psf->beta, profile);
    psf->fwhmy  = fwhm_from_s(psf->sy, psf->beta, profile);
    psf->angle  = -FIT(6) * 180.0 / M_PI;
    psf->rmse   = fitdata.rmse;
    psf->mag    = getMag(data, NbRows, NbCols, psf->B);

    // Normalise angle to [-90, 90]
    if (std::fabs(psf->angle) > 10000.0) {
        delete psf;
        if (covar) gsl_matrix_free(covar);
        gsl_multifit_nlinear_free(work);
        if (error) *error = PsfError::Diverged;
        return nullptr;
    }
    while (std::fabs(psf->angle) > 90.0) {
        if (psf->angle > 0) psf->angle -= 180.0;
        else                psf->angle += 180.0;
    }

    // Propagate uncertainties
    if (covar) {
        psf->B_err    = (std::fabs(FIT(0)) > 0) ? ERR(0) / std::fabs(FIT(0)) : 0.0;
        psf->A_err    = (std::fabs(FIT(1)) > 0) ? ERR(1) / std::fabs(FIT(1)) : 0.0;
        psf->x_err    = (std::fabs(FIT(2)) > 0) ? ERR(2) / std::fabs(FIT(2)) : 0.0;
        psf->y_err    = (std::fabs(FIT(3)) > 0) ? ERR(3) / std::fabs(FIT(3)) : 0.0;
        psf->sx_err   = (std::fabs(FIT(4)) > 0) ? ERR(4) / std::fabs(FIT(4)) : 0.0;
        psf->sy_err   = (std::fabs(FIT(5)) > 0) ? ERR(5) / std::fabs(FIT(5)) : 0.0;
        psf->ang_err  = (std::fabs(FIT(6)) > 0) ? ERR(6) / std::fabs(FIT(6)) : 0.0;
        psf->beta_err = (profile != PsfProfile::Gaussian && std::fabs(FIT(7)) > 0)
                        ? ERR(7) / std::fabs(FIT(7)) : 0.0;
        gsl_matrix_free(covar);
    }

    if (status != GSL_SUCCESS && error)
        *error = PsfError::Diverged;

    // Sanity check
    if (!std::isfinite(psf->fwhmx) || !std::isfinite(psf->fwhmy) ||
        psf->fwhmx <= 0.0 || psf->fwhmy <= 0.0) {
        delete psf;
        gsl_multifit_nlinear_free(work);
        if (error) *error = PsfError::Diverged;
        return nullptr;
    }

    gsl_multifit_nlinear_free(work);
    return psf;
#undef FIT
#undef ERR
}

// ─── Convenience wrapper accepting std::vector ────────────────────────────────
PsfStar* PsfFitter::fitMatrix(const std::vector<double>& data,
                               size_t NbRows, size_t NbCols,
                               double background, double sat,
                               int convergence, bool fromPeaker,
                               PsfProfile profile, PsfError* error)
{
    if (data.size() < NbRows * NbCols) {
        if (error) *error = PsfError::InvalidImage;
        return nullptr;
    }
    return fit(data.data(), NbRows, NbCols, background, sat,
               convergence, fromPeaker, profile, error);
}
