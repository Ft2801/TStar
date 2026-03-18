#pragma once
/*
 * Deconvolution.h  —  Image deconvolution for astrophotography
 *
 * Implements three industry-standard algorithms:
 *
 *   1. Richardson-Lucy (RL)
 *      Classic iterative maximum-likelihood estimator (Poisson noise model).
 *      Fast, straightforward, suitable for stars and compact objects.
 *
 *   2. Blind Richardson-Lucy with Total Variation regularisation (RLTV)
 *      Regularised RL with anisotropic TV prior to suppress ringing.
 *      Preferred for extended nebulosity.
 *
 *   3. Wiener / MMSE deconvolution
 *      Single-step frequency-domain estimator (FFT-based).
 *      Fastest, stable, but less aggressive than iterative methods.
 *
 * PSF input: the caller supplies a normalised float kernel (obtained from
 * PsfFitter or a synthetic Gaussian/Airy), or requests a synthetic one.
 *
 * All algorithms:
 *   - operate on normalised [0,1] float data
 *   - support 1-channel (mono) or 3-channel (RGB) ImageBuffer
 *   - are multi-threaded via OpenMP (plane-level parallelism + loop-level)
 *   - use OpenCV DFT for frequency-domain steps
 *
 *  Author: TStar project
 */

#ifndef DECONVOLUTION_H
#define DECONVOLUTION_H

#include <vector>
#include <functional>
#include <QString>
#include "ImageBuffer.h"
#include <opencv2/core.hpp>

// ─── PSF source ──────────────────────────────────────────────────────────────
enum class PSFSource {
    Gaussian,     ///< synthetic isotropic Gaussian
    Moffat,       ///< synthetic Moffat profile
    Disk,         ///< synthetic Disk profile
    Airy,         ///< synthetic Airy profile
    Custom        ///< user-supplied kernel image
};

// ─── Algorithm selection ──────────────────────────────────────────────────────
enum class DeconvAlgorithm {
    RichardsonLucy,   ///< standard RL (no regularisation)
    RLTV,             ///< RL + Total Variation regulariser (recommended)
    Wiener            ///< single-step Wiener / MMSE
};

// ─── Mask to protect bright stars from ringing ───────────────────────────────
struct DeconvStarMask {
    bool   useMask    = true;
    double threshold  = 0.85;   ///< pixel value threshold for star mask
    double radius     = 2.0;    ///< dilation radius (pixels)
    double blend      = 0.5;    ///< blend factor: 0=original, 1=deconvolved in mask region
};

// ─── Deconvolution parameters ─────────────────────────────────────────────────
struct DeconvParams {
    // ── Algorithm ───────────────────────────────────────────────────────
    DeconvAlgorithm algo      = DeconvAlgorithm::RLTV;

    // ── PSF definition ──────────────────────────────────────────────────
    PSFSource   psfSource     = PSFSource::Gaussian;
    double      psfFWHM       = 2.0;     ///< FWHM in pixels (Gaussian / Moffat)
    double      psfBeta       = 4.5;     ///< Moffat beta parameter
    double      psfAngle      = 0.0;     ///< PSF orientation angle (degrees)
    double      psfRoundness  = 1.0;     ///< minor/major axis ratio [0,1]
    cv::Mat     customKernel;            ///< 32F normalised kernel (if Custom)
    
    // ─── Airy parameters ──────────────────────────────────────────────────
    double      airyWavelength = 550.0;  ///< Wavelength in nm
    double      airyAperture   = 200.0;  ///< Telescope aperture in mm
    double      airyFocalLen   = 1000.0; ///< Telescope focal length in mm
    double      airyPixelSize  = 3.76;   ///< Sensor pixel size in um
    double      airyObstruction= 0.0;    ///< Central obstruction ratio [0,1]

    // ── Iterations (RL / RLTV) ──────────────────────────────────────────
    int         maxIter       = 100;
    double      convergenceTol= 1e-4;    ///< stop if ‖u_{k+1}−u_k‖/‖u_k‖ < tol

    // ── RLTV regularisation ─────────────────────────────────────────────
    double      tvRegWeight   = 0.01;    ///< λ_TV (higher = smoother, less ringing)
    double      tvEps         = 1e-4;    ///< Huber epsilon for TV sub-gradient

    // ── Wiener ──────────────────────────────────────────────────────────
    double      wienerK       = 0.001;   ///< noise-to-signal power ratio

    // ── Star protection ─────────────────────────────────────────────────
    DeconvStarMask starMask;

    // ── Border handling ─────────────────────────────────────────────────
    int         borderPad     = 32;      ///< mirror-padding to reduce wrap artefacts

    // ── PSF kernel size ─────────────────────────────────────────────────
    int         kernelSize    = 0;       ///< 0 = auto (4×FWHM rounded to odd)

    // Optional progress callback (0..100, status text)
    std::function<void(int, const QString&)> progressCallback;
};

// ─── Result ──────────────────────────────────────────────────────────────────
struct DeconvResult {
    bool    success     = false;
    int     iterations  = 0;
    double  finalChange = 0.0;   ///< relative change at last iteration
    QString errorMsg;
};

// ─── Main class ──────────────────────────────────────────────────────────────
class Deconvolution {
public:
    /// Apply deconvolution to an ImageBuffer (all channels).
    static DeconvResult apply(ImageBuffer& buf, const DeconvParams& p);

    /// Build a PSF kernel (float32, normalised, size×size)
    static cv::Mat buildPSF(const DeconvParams& p, int kernelSize);

    /// Auto-estimate PSF FWHM from the image (delegates to StarDetector + PsfFitter)
    static double estimateFWHM(const ImageBuffer& buf);

    /// Build a binary star-protection mask from a plane
    static cv::Mat buildStarMask(const cv::Mat& plane, const DeconvStarMask& m);

private:
    // ── Per-plane algorithms ──────────────────────────────────────────────
    static DeconvResult applyRL(cv::Mat& plane,
                                const cv::Mat& psf,
                                const cv::Mat& starMask,
                                const DeconvParams& p);

    static DeconvResult applyRLTV(cv::Mat& plane,
                                   const cv::Mat& psf,
                                   const cv::Mat& starMask,
                                   const DeconvParams& p);

    static DeconvResult applyWiener(cv::Mat& plane,
                                     const cv::Mat& psf,
                                     const DeconvParams& p);

    // ── Helper: convolve via DFT ──────────────────────────────────────────
    static cv::Mat convolveFFT(const cv::Mat& src, const cv::Mat& psf);

    // ── Total Variation sub-gradient ─────────────────────────────────────
    static cv::Mat tvGradient(const cv::Mat& u, double eps);

    // ── Frequency-domain Wiener filter ───────────────────────────────────
    static cv::Mat wienerFilter(const cv::Mat& src, const cv::Mat& psf, double K);
};

#endif // DECONVOLUTION_H
