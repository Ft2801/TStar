#ifndef STARDETECTOR_H
#define STARDETECTOR_H

// =============================================================================
// StarDetector.h
//
// Multi-stage star detection pipeline:
//
//   1.  Gaussian blur (sigma = KERNEL_SIZE = 2.0)
//   2.  Sigma-clipped background statistics -> threshold = bg + sigma * 5 * bgnoise
//   3.  Local maximum search in (2r+1)^2 box with tie-breaking
//   4.  Neighbourhood density check (mono: >= 3, colour: >= 8)
//   5.  Sub-pixel centroid from 1st-derivative zero crossing
//   6.  2nd-derivative width estimates (Sr, Sc) for box sizing
//   7.  Box radius R = clamp(ceil(s_factor * max(Sr, Sc)), r, MAX_BOX_RADIUS)
//   8.  Duplicate removal within match_radius = max(1, R * 0.2)
//   9.  Per-candidate PSF fit (Gaussian or Moffat) via PsfFitter
//  10.  Rejection criteria (FWHM, roundness, RMSE, Moffat beta)
//  11.  Sort by flux (descending)
//
// This file is part of TStar, a Professional Astrophotography Processing Suite.
// Copyright (C) 2026 Fabio Tempera (Ft2801)
//
// TStar is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TStar is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TStar.  If not, see <https://www.gnu.org/licenses/>.
//
// -------------------------------------------------------------------
// Prominent Notice of Modification & Original Copyright:
// This file is derived from or incorporates code from Siril (hosted at
// https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
// Modified by Ft2801 in 2026.
// Original copyright (C) 2012-2026 team free-astro.
// -------------------------------------------------------------------
// =============================================================================

#include "../ImageBuffer.h"
#include "PsfFitter.h"

#include <vector>
#include <memory>

// =============================================================================
// DetectedStar - Output record for a single detected star
// =============================================================================

struct DetectedStar {
    double x;           // Sub-pixel centroid X (image coordinates, 0-based)
    double y;           // Sub-pixel centroid Y
    double flux;        // Integrated flux above background (linear)
    double peak;        // Peak pixel value (from smoothed image)
    double background;  // Local background level

    float fwhm;         // FWHM of the major axis (pixels)
    float fwhmx;        // PSF FWHM along X (pixels)
    float fwhmy;        // PSF FWHM along Y (pixels)
    float angle;        // PSF rotation angle (degrees)
    float rmse;         // PSF fit RMS residual
    float beta;         // Moffat beta parameter (-1 for Gaussian)

    bool  saturated;    // True if the star contains saturated pixels
    int   R;            // Box half-radius used for PSF fitting

    std::shared_ptr<PsfStar> psf; // Full PSF fit result (may be null)
};

// =============================================================================
// StarFinderParams - Algorithm tuning parameters
// =============================================================================

struct StarFinderParams {
    double     sigma         = 1.0;   // Detection threshold multiplier
    double     roundness     = 0.5;   // Minimum acceptable fwhmy / fwhmx ratio
    double     maxRoundness  = 1.5;   // Maximum acceptable fwhmy / fwhmx ratio
    double     minBeta       = 1.5;   // Minimum Moffat beta (Moffat profile only)
    int        radius        = 3;     // Local maximum search half-size (pixels)
    bool       relaxChecks   = false; // If true, allow stars that fail RMSE criterion
    PsfProfile profile       = PsfProfile::Gaussian;
    int        convergence   = 1;     // PSF solver iteration scaling factor
    double     minAmplitude  = 0.0;   // Minimum PSF amplitude (0 = disabled)
    double     maxAmplitude  = 0.0;   // Maximum PSF amplitude (0 = disabled)
};

// =============================================================================
// StarDetector - Multi-stage star detection and PSF fitting engine
// =============================================================================

class StarDetector {
public:
    StarDetector();
    ~StarDetector();

    // --- Configuration ---

    void setThresholdSigma(float sigma)         { m_params.sigma = sigma; }
    void setMinFWHM(float fwhm)                 { m_minFWHM = fwhm; }
    void setMaxStars(int max)                   { m_maxStars = max; }
    void setParams(const StarFinderParams& p)   { m_params = p; }
    const StarFinderParams& params() const      { return m_params; }

    // --- Detection entry points ---

    // Detect stars in a single channel of an ImageBuffer.
    std::vector<DetectedStar> detect(const ImageBuffer& image, int channel = 0);

    // Detect stars from a raw float pixel buffer (row-major, interleaved channels).
    std::vector<DetectedStar> detectRaw(const float* raw, int w, int h,
                                        int ch, int channel = 0);

    // Separable 2-D Gaussian blur (applied to a single channel).
    static void gaussianBlur(const float* src, float* dst,
                             int w, int h, int ch, int channel, float sigma);

private:
    StarFinderParams m_params;
    float m_minFWHM  = 0.5f;
    int   m_maxStars = 2000;

    // --- Background statistics (sigma-clipped) ---

    struct BgStats {
        double median  = 0.0;
        double bgnoise = 0.0; // Robust noise estimate (row-difference method or MAD)
        double max     = 0.0;
    };

    BgStats computeBackground(const float* data, int w, int h,
                              int ch, int channel) const;

    // --- Star rejection criteria ---

    enum class RejectReason {
        OK = 0,
        FwhmTooLarge,
        RmseTooLarge,
        CenterOff,
        NoFwhm,
        NoPos,
        NoMag,
        FwhmTooSmall,
        FwhmNeg,
        RoundnessBelow,
        MoffatBetaTooSmall,
        AmplitudeOutOfRange
    };

    RejectReason rejectStar(const PsfStar* psf,
                            double sx, double sy,
                            int R, bool isColor,
                            double dynrange, double sigma) const;
};

#endif // STARDETECTOR_H