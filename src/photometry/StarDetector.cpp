/**
 * @file StarDetector.cpp
 *
 * Automated star detection pipeline with PSF fitting.
 *
 * Processing stages:
 *   1.  Gaussian blur (sigma = KERNEL_SIZE)
 *   2.  Sigma-clipped background estimation -> detection threshold
 *   3.  Local maximum search in (2r+1)^2 neighbourhood with tie-breaking
 *   4.  Neighbourhood density check (mono >= 3, colour >= 8)
 *   5.  Sub-pixel centroid refinement via 1st-derivative zero-crossing
 *   6.  2nd-derivative width estimates (Sr, Sc)
 *   7.  Adaptive box radius: R = clamp(ceil(s_factor * max(Sr,Sc)), r, MAX_BOX)
 *   8.  Duplicate removal within match radius
 *   9.  Per-candidate PSF fit (Gaussian or Moffat) via PsfFitter
 *  10.  Quality rejection criteria (FWHM, roundness, RMSE, Moffat beta)
 *  11.  Final sort by integrated flux (descending)
 *
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
 */

#include "StarDetector.h"
#include "PsfFitter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

#include <gsl/gsl_statistics.h>
#include <gsl/gsl_sort.h>

// ===========================================================================
// Compile-time constants
// ===========================================================================
static constexpr double KERNEL_SIZE          = 2.0;       // Gaussian blur sigma (pixels)
static constexpr double DENSITY_THRESHOLD    = 0.001;     // Density threshold for s_factor
static constexpr double SAT_THRESHOLD        = 0.7;       // Fraction of dynamic range for saturation
static constexpr double SAT_DETECTION_RANGE  = 0.1;       // Fractional range for saturation plateau
static constexpr int    MAX_BOX_RADIUS       = 200;       // Maximum PSF fitting box half-size
static constexpr double MAX_RADIUS_RATIO_DUP = 0.2;       // Duplicate match radius as fraction of R
static constexpr double _2_SQRT_2_LOG2       = 2.35482004503;  // 2 * sqrt(2 * ln(2)) -- Gaussian FWHM/sigma
static constexpr double _SQRT_EXP1           = 1.6487212707;   // sqrt(e)

// ===========================================================================
// Background noise estimation helpers
// ===========================================================================

/** Number of sigma-clip iterations for per-row noise estimation. */
static constexpr int    FN_NITER      = 3;

/** Sigma multiplier for clipping in noise estimation. */
static constexpr double FN_SIGMA_CLIP = 5.0;

/**
 * Compute the mean and standard deviation of a float array.
 *
 * Uses a single-pass algorithm.  The population (biased) standard deviation
 * is returned, which is adequate for the subsequent sigma-clip loop.
 */
static void fnMeanSigma(const float* arr, int n, double* mean, double* sigma)
{
    double sum = 0.0, sum2 = 0.0;
    int ngood = 0;

    for (int i = 0; i < n; ++i) {
        double v = arr[i];
        sum  += v;
        sum2 += v * v;
        ++ngood;
    }

    if (ngood < 1) {
        *mean  = 0.0;
        *sigma = 0.0;
        return;
    }

    double m = sum / ngood;
    *mean  = m;
    *sigma = (ngood > 1) ? std::sqrt(std::max(0.0, sum2 / ngood - m * m)) : 0.0;
}

// ===========================================================================
// Construction / destruction
// ===========================================================================

StarDetector::StarDetector()  = default;
StarDetector::~StarDetector() = default;

// ===========================================================================
// Separable Gaussian blur
// ===========================================================================

/**
 * Apply a separable Gaussian blur to a single channel of a multi-channel image.
 *
 * The kernel radius is chosen as ceil(3 * sigma) to capture > 99.7% of the
 * Gaussian weight.  Edge pixels are handled by clamping indices.
 *
 * @param src     Input image data (interleaved channels).
 * @param dst     Output buffer (single-channel, w * h floats).
 * @param w, h    Image dimensions.
 * @param ch      Number of interleaved channels.
 * @param channel Index of the channel to blur.
 * @param sigma   Gaussian standard deviation (pixels).
 */
void StarDetector::gaussianBlur(const float* src, float* dst,
                                int w, int h, int ch, int channel, float sigma)
{
    // Build 1-D Gaussian kernel
    int kr    = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
    int ksize = 2 * kr + 1;
    std::vector<float> kern(ksize);

    float sumk = 0.0f;
    for (int k = 0; k < ksize; ++k) {
        float x = static_cast<float>(k - kr);
        kern[k] = std::exp(-x * x / (2.0f * sigma * sigma));
        sumk += kern[k];
    }
    for (float& v : kern) v /= sumk;

    // Temporary buffer for the intermediate (horizontal-pass) result
    std::vector<float> tmp(w * h, 0.0f);

    // Horizontal pass: extract the target channel and convolve
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float val = 0.0f;
            for (int k = 0; k < ksize; ++k) {
                int nx = std::clamp(x + k - kr, 0, w - 1);
                val += src[(y * w + nx) * ch + channel] * kern[k];
            }
            tmp[y * w + x] = val;
        }
    }

    // Vertical pass
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float val = 0.0f;
            for (int k = 0; k < ksize; ++k) {
                int ny = std::clamp(y + k - kr, 0, h - 1);
                val += tmp[ny * w + x] * kern[k];
            }
            dst[y * w + x] = val;
        }
    }
}

// ===========================================================================
// Background statistics
// ===========================================================================

/**
 * Estimate background level, noise, and peak value for a single channel.
 *
 * The method combines two techniques:
 *   - A subsampled pass (step = 8) to obtain the median and peak value.
 *   - A per-row consecutive-difference method with iterative sigma clipping
 *     to robustly estimate the background noise.  The final noise value is
 *     1/sqrt(2) times the median of per-row standard deviations, which
 *     converts the difference-based sigma back to the underlying pixel noise.
 *
 * If the per-row method yields no valid rows, a fallback MAD estimator is
 * used (bgnoise = 1.4826 * MAD).
 */
StarDetector::BgStats StarDetector::computeBackground(
    const float* data, int w, int h, int ch, int channel) const
{
    BgStats out{};

    // --- Subsampled median and peak ---
    const int step = 8;
    std::vector<double> samp;
    samp.reserve(static_cast<size_t>((w / step) + 1) * ((h / step) + 1));
    double maxval = 0.0;

    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            double v = static_cast<double>(data[(y * w + x) * ch + channel]);
            if (v > 0.0 && !std::isnan(v)) {
                samp.push_back(v);
                if (v > maxval) maxval = v;
            }
        }
    }

    out.max = maxval;
    if (samp.empty()) return out;

    gsl_sort(samp.data(), 1, samp.size());
    out.median = gsl_stats_median_from_sorted_data(samp.data(), 1, samp.size());

    // --- Per-row noise estimation via consecutive differences ---
    std::vector<double> rowNoise;
    rowNoise.reserve(h);

    std::vector<float> diffs;
    diffs.reserve(w);

    for (int y = 0; y < h; ++y) {
        diffs.clear();
        float prev    = 0.0f;
        bool  hasPrev = false;

        for (int x = 0; x < w; ++x) {
            float v = data[(y * w + x) * ch + channel];
            if (v == 0.0f || std::isnan(v)) {
                hasPrev = false;
                continue;
            }
            if (hasPrev) diffs.push_back(prev - v);
            prev    = v;
            hasPrev = true;
        }

        if (static_cast<int>(diffs.size()) < 2) continue;

        // Iterative sigma clipping on the difference values
        double mean = 0.0, stdev = 0.0;
        fnMeanSigma(diffs.data(), static_cast<int>(diffs.size()), &mean, &stdev);

        for (int iter = 0; iter < FN_NITER && stdev > 0.0; ++iter) {
            int kk = 0;
            double limit = FN_SIGMA_CLIP * stdev;
            for (int i = 0; i < static_cast<int>(diffs.size()); ++i) {
                if (std::fabs(diffs[i] - static_cast<float>(mean)) < limit)
                    diffs[kk++] = diffs[i];
            }
            if (kk == static_cast<int>(diffs.size())) break;
            diffs.resize(kk);
            if (kk < 2) { stdev = 0.0; break; }
            fnMeanSigma(diffs.data(), kk, &mean, &stdev);
        }

        if (stdev > 0.0)
            rowNoise.push_back(stdev);
    }

    // Compute final noise estimate
    if (rowNoise.empty()) {
        // Fallback: MAD-based noise estimator
        std::vector<double> adev(samp.size());
        for (size_t i = 0; i < samp.size(); ++i)
            adev[i] = std::fabs(samp[i] - out.median);
        gsl_sort(adev.data(), 1, adev.size());
        double mad = gsl_stats_median_from_sorted_data(adev.data(), 1, adev.size());
        out.bgnoise = 1.4826 * mad;
    } else {
        std::sort(rowNoise.begin(), rowNoise.end());
        size_t nr = rowNoise.size();
        double medNoise = (nr % 2 == 1)
            ? rowNoise[nr / 2]
            : 0.5 * (rowNoise[(nr - 1) / 2] + rowNoise[nr / 2]);
        out.bgnoise = 0.70710678 * medNoise;  // 1/sqrt(2) correction for difference-based sigma
    }

    return out;
}

// ===========================================================================
// Star rejection criteria
// ===========================================================================

/**
 * Evaluate whether a fitted PSF star should be accepted or rejected.
 *
 * The rejection criteria test for:
 *   - Invalid (NaN) fit parameters
 *   - FWHM too small for colour images (likely cosmic ray or noise spike)
 *   - Non-positive FWHM
 *   - Roundness outside the allowed range
 *   - FWHM exceeding the expected size based on the pre-fit width estimate
 *   - Excessive RMSE relative to amplitude (unless the star is saturated)
 *   - Amplitude outside optional user-specified bounds
 *   - Moffat beta below the minimum threshold (Moffat profile only)
 *
 * @return RejectReason::OK if the star passes all checks.
 */
StarDetector::RejectReason StarDetector::rejectStar(
    const PsfStar* psf,
    double sx, double sy,
    int /*R*/, bool isColor,
    double dynrange, double sigma) const
{
    // Validity checks on fitted parameters
    if (std::isnan(psf->fwhmx) || std::isnan(psf->fwhmy)) return RejectReason::NoFwhm;
    if (std::isnan(psf->x0)   || std::isnan(psf->y0))     return RejectReason::NoPos;
    if (std::isnan(psf->mag))                               return RejectReason::NoMag;

    // FWHM lower bounds
    if (isColor && (psf->fwhmx <= 1.0 || psf->fwhmy <= 1.0))
        return RejectReason::FwhmTooSmall;
    if (psf->fwhmx <= 0.0 || psf->fwhmy <= 0.0)
        return RejectReason::FwhmNeg;

    // Roundness (aspect ratio) constraint
    double r = psf->fwhmy / psf->fwhmx;
    if (r < m_params.roundness || r > m_params.maxRoundness)
        return RejectReason::RoundnessBelow;

    // FWHM upper bound: adaptive limit that grows with the pre-fit width estimate
    double maxs            = std::max(sx, sy);
    double kernel          = std::max(maxs / KERNEL_SIZE, 1.001);
    double fwhmLimitFactor = _2_SQRT_2_LOG2 * (1.0 + 0.5 * std::log(kernel));
    if (psf->fwhmx > maxs * fwhmLimitFactor)
        return RejectReason::FwhmTooLarge;

    // RMSE criterion (bypassed for saturated stars where A > dynrange)
    if (!m_params.relaxChecks) {
        if ((psf->rmse * sigma / psf->A) > 0.2 && !(psf->A > dynrange))
            return RejectReason::RmseTooLarge;
    }

    // Optional amplitude bounds
    if (m_params.minAmplitude > 0.0 && psf->A < m_params.minAmplitude)
        return RejectReason::AmplitudeOutOfRange;
    if (m_params.maxAmplitude > 0.0 && psf->A > m_params.maxAmplitude)
        return RejectReason::AmplitudeOutOfRange;

    // Moffat beta constraint
    if (m_params.profile == PsfProfile::Moffat && psf->beta < m_params.minBeta)
        return RejectReason::MoffatBetaTooSmall;

    return RejectReason::OK;
}

// ===========================================================================
// Public detection entry point (ImageBuffer wrapper)
// ===========================================================================

/**
 * Detect stars in an ImageBuffer on the specified channel.
 *
 * Validates the image before delegating to the raw-pointer pipeline.
 */
std::vector<DetectedStar> StarDetector::detect(const ImageBuffer& image, int channel)
{
    if (!image.isValid()) return {};

    const int w  = image.width();
    const int h  = image.height();
    const int ch = image.channels();
    if (w <= 0 || h <= 0 || ch <= 0) return {};

    const std::vector<float>& pixels = image.data();
    const size_t expectedSize =
        static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(ch);
    if (pixels.size() < expectedSize) return {};

    const float* raw = pixels.data();
    if (!raw) return {};

    return detectRaw(raw, w, h, ch, channel);
}

// ===========================================================================
// Core detection pipeline
// ===========================================================================

/**
 * Detect stars from a raw float pixel buffer.
 *
 * Implements the full 11-stage pipeline described in the file header.
 */
std::vector<DetectedStar> StarDetector::detectRaw(
    const float* raw, int w, int h, int ch, int channel)
{
    if (!raw || w <= 0 || h <= 0 || ch <= 0) return {};

    channel = std::clamp(channel, 0, ch - 1);

    // -----------------------------------------------------------------------
    // Stage 1: Gaussian blur
    // -----------------------------------------------------------------------
    std::vector<float> blurred(w * h);
    gaussianBlur(raw, blurred.data(), w, h, ch, channel, static_cast<float>(KERNEL_SIZE));

    // -----------------------------------------------------------------------
    // Stage 2: Background statistics and detection threshold
    // -----------------------------------------------------------------------
    BgStats bg = computeBackground(raw, w, h, ch, channel);
    if (bg.bgnoise < 1e-12) bg.bgnoise = 1e-6;

    double threshold   = bg.median + m_params.sigma * 5.0 * bg.bgnoise;
    double dynrange    = std::min(bg.max, 1.0) - bg.median;
    double minsatlevel = dynrange * SAT_THRESHOLD;
    double satrange2   = dynrange * SAT_DETECTION_RANGE;
    double s_factor    = std::sqrt(-2.0 * std::log(DENSITY_THRESHOLD));  // approx. 3.717
    bool   ismono      = (ch == 1);
    double locthresh   = m_params.sigma * 5.0 * bg.bgnoise;

    // Safe pixel accessor for the blurred image (clamped at edges)
    auto bpix = [&](int y, int x) -> float {
        y = std::clamp(y, 0, h - 1);
        x = std::clamp(x, 0, w - 1);
        return blurred[y * w + x];
    };

    // Internal candidate record for pre-PSF-fit peaks
    struct Cand {
        int   x, y;
        float mag_est;
        float sx, sy;
        float sat;
        int   R;
        bool  hasSaturated;
        bool  isColor;
    };

    const int r       = m_params.radius;
    const int boxsize = (2 * r + 1) * (2 * r + 1);

    std::vector<Cand> candidates;
    candidates.reserve(2048);

    // -----------------------------------------------------------------------
    // Stages 3-8: Candidate detection loop
    // -----------------------------------------------------------------------
    for (int y = r; y < h - r; y++) {
        for (int x = r; x < w - r; x++) {

            float pixel = bpix(y, x);
            if (pixel <= static_cast<float>(threshold)) continue;

            // --- Stage 3: Local maximum test with deterministic tie-breaking ---
            bool bingo = true;
            int  cnt   = 0;
            for (int yy2 = y - r; yy2 <= y + r && bingo; yy2++) {
                for (int xx2 = x - r; xx2 <= x + r; xx2++) {
                    if (xx2 == x && yy2 == y) continue;
                    float nb = bpix(yy2, xx2);
                    if (nb > pixel) { bingo = false; break; }
                    if (nb == pixel) {
                        // Tie-breaking: lower-left pixel wins
                        if ((xx2 <= x && yy2 <= y) || (xx2 > x && yy2 < y)) {
                            bingo = false; break;
                        }
                    }
                    cnt++;
                }
            }
            if (!bingo || cnt < boxsize - 1) continue;

            // Skip ahead: no other local max can exist within r columns
            x += r;
            int xx = x - r, yy = y;

            // --- Stage 4: Neighbourhood density check ---
            int    cnt2    = 0;
            double meanhigh = 0.0, minhigh = 1e30;
            for (int dy2 = -1; dy2 <= 1; dy2++) {
                for (int dx2 = -1; dx2 <= 1; dx2++) {
                    if (dx2 == 0 && dy2 == 0) continue;
                    int nx2 = xx + dx2, ny2 = yy + dy2;
                    if (nx2 < 0 || nx2 >= w || ny2 < 0 || ny2 >= h) continue;
                    float nb = bpix(ny2, nx2);
                    if (nb >= static_cast<float>(threshold)) {
                        if (nb < minhigh) minhigh = static_cast<double>(nb);
                        meanhigh += static_cast<double>(nb);
                        cnt2++;
                    }
                }
            }
            if (cnt2 == 0 || (ismono && cnt2 < 3) || (!ismono && cnt2 < 8)) continue;
            meanhigh /= static_cast<double>(cnt2);
            float pixel0 = bpix(yy, xx);

            // --- Saturation detection and plateau walking ---
            bool  has_sat = false;
            float sat_lv  = 1.0f;

            if (!(meanhigh - bg.median < minsatlevel ||
                  static_cast<double>(pixel0 - static_cast<float>(minhigh)) > satrange2))
            {
                has_sat = true;
                sat_lv  = static_cast<float>(std::min(static_cast<double>(pixel0), 1.0) - satrange2);

                // Walk the edges of the saturated plateau to find its extent
                int i2 = 0, j2 = 0, xr = 0, xl = 0, yu2 = 0, yd2 = 0;

                // Walk right
                while (xx + i2 < w - 1 && bpix(yy, xx + i2) > sat_lv) i2++;
                // Walk down-right
                while (xx + i2 < w - 1 && yy + j2 < h - 1 &&
                       (bpix(yy+j2+1, xx+i2+1) > sat_lv || bpix(yy+j2+1, xx+i2) > sat_lv)) {
                    if (bpix(yy+j2+1, xx+i2+1) > sat_lv) i2++;
                    j2++;
                }
                xr = i2;

                // Walk down-left
                while (xx + i2 > 1 && yy + j2 < h - 1 &&
                       (bpix(yy+j2+1, xx+i2-1) > sat_lv || bpix(yy+j2, xx+i2-1) > sat_lv)) {
                    if (bpix(yy+j2+1, xx+i2-1) > sat_lv) j2++;
                    i2--;
                }
                yd2 = j2;

                // Walk up-left
                while (xx + i2 > 1 && yy + j2 > 1 &&
                       (bpix(yy+j2-1, xx+i2-1) > sat_lv || bpix(yy+j2-1, xx+i2) > sat_lv)) {
                    if (bpix(yy+j2-1, xx+i2-1) > sat_lv) i2--;
                    j2--;
                }
                xl = i2;

                // Walk up-right
                while (xx + i2 < w - 1 && yy + j2 > 1 &&
                       (bpix(yy+j2-1, xx+i2+1) > sat_lv || bpix(yy+j2, xx+i2+1) > sat_lv)) {
                    if (bpix(yy+j2-1, xx+i2+1) > sat_lv) j2--;
                    i2++;
                }
                yu2 = j2;

                // Final right extension check
                while (xx + i2 < w - 1 && yy + j2 < h - 1 &&
                       (bpix(yy+j2+1, xx+i2+1) > sat_lv || bpix(yy+j2+1, xx+i2) > sat_lv)) {
                    if (bpix(yy+j2+1, xx+i2+1) > sat_lv) i2++;
                    j2++;
                }
                if (i2 > xr) xr = i2;

                // Re-centre on the plateau midpoint
                xx += (xr + xl) / 2;
                yy += (yu2 + yd2) / 2;
                x  += xr;  // advance outer loop past the plateau
            }

            // Reject candidates too close to image borders
            if (xx - 2 < 1 || xx + 2 > w - 2 || yy - 2 < 1 || yy + 2 > h - 2) continue;

            // --- Stage 5: Sub-pixel centroid via 1st-derivative zero-crossing ---
            float r0, c0;
            if (!has_sat) {
                float d1rl = bpix(yy, xx)     - bpix(yy, xx - 1);
                float d1rr = bpix(yy, xx + 1) - bpix(yy, xx);
                float d1cu = bpix(yy, xx)     - bpix(yy - 1, xx);
                float d1cd = bpix(yy + 1, xx) - bpix(yy, xx);
                r0 = (std::fabs(d1rr - d1rl) > 1e-10f)
                     ? -0.5f - d1rl / (d1rr - d1rl) : 0.0f;
                c0 = (std::fabs(d1cd - d1cu) > 1e-10f)
                     ? -0.5f - d1cu / (d1cd - d1cu) : 0.0f;
            } else {
                r0 = c0 = -0.5f;
            }

            // --- Stage 6: 2nd-derivative width estimates in four directions ---
            float srl, srr, scu, scd;
            float Arl, Arr, Acu, Acd;

            // Right direction
            {
                int ii = 0;
                if (has_sat) while (xx + ii < w && bpix(yy, xx + ii) > sat_lv) ii++;
                if (xx + ii > w - 3) continue;
                float d2a = bpix(yy, xx+ii+1) + bpix(yy, xx+ii-1) - 2*bpix(yy, xx+ii);
                float d2b = bpix(yy, xx+ii+2) + bpix(yy, xx+ii)   - 2*bpix(yy, xx+ii+1);
                while (d2b < 0 && xx + ii + 2 < w - 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy, xx+ii+2) + bpix(yy, xx+ii) - 2*bpix(yy, xx+ii+1);
                }
                srr = static_cast<float>(ii)
                    - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a / (d2b - d2a) : 0.f) - r0;
                Arr = -(bpix(yy, xx+ii) - bpix(yy, xx+ii-1)) * srr * static_cast<float>(_SQRT_EXP1);
            }

            // Left direction
            {
                int ii = 0;
                if (has_sat) while (xx - ii > 0 && bpix(yy, xx - ii) > sat_lv) ii++;
                if (xx - ii < 2) continue;
                float d2a = bpix(yy, xx-ii-1) + bpix(yy, xx-ii+1) - 2*bpix(yy, xx-ii);
                float d2b = bpix(yy, xx-ii-2) + bpix(yy, xx-ii)   - 2*bpix(yy, xx-ii-1);
                while (d2b < 0 && xx - ii - 2 > 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy, xx-ii-2) + bpix(yy, xx-ii) - 2*bpix(yy, xx-ii-1);
                }
                srl = -(static_cast<float>(ii)
                    - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a / (d2b - d2a) : 0.f)) - r0;
                Arl = (bpix(yy, xx-ii) - bpix(yy, xx-ii+1)) * srl * static_cast<float>(_SQRT_EXP1);
            }

            // Down direction
            {
                int ii = 0;
                if (has_sat) while (yy + ii < h && bpix(yy+ii, xx) > sat_lv) ii++;
                if (yy + ii > h - 3) continue;
                float d2a = bpix(yy+ii+1, xx) + bpix(yy+ii-1, xx) - 2*bpix(yy+ii, xx);
                float d2b = bpix(yy+ii+2, xx) + bpix(yy+ii,   xx) - 2*bpix(yy+ii+1, xx);
                while (d2b < 0 && yy + ii + 2 < h - 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy+ii+2, xx) + bpix(yy+ii, xx) - 2*bpix(yy+ii+1, xx);
                }
                scd = static_cast<float>(ii)
                    - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a / (d2b - d2a) : 0.f) - c0;
                Acd = -(bpix(yy+ii, xx) - bpix(yy+ii-1, xx)) * scd * static_cast<float>(_SQRT_EXP1);
            }

            // Up direction
            {
                int ii = 0;
                if (has_sat) while (yy - ii > 0 && bpix(yy-ii, xx) > sat_lv) ii++;
                if (yy - ii < 2) continue;
                float d2a = bpix(yy-ii-1, xx) + bpix(yy-ii+1, xx) - 2*bpix(yy-ii, xx);
                float d2b = bpix(yy-ii-2, xx) + bpix(yy-ii,   xx) - 2*bpix(yy-ii-1, xx);
                while (d2b < 0 && yy - ii - 2 > 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy-ii-2, xx) + bpix(yy-ii, xx) - 2*bpix(yy-ii-1, xx);
                }
                scu = -(static_cast<float>(ii)
                    - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a / (d2b - d2a) : 0.f)) - c0;
                Acu = (bpix(yy-ii, xx) - bpix(yy-ii+1, xx)) * scu * static_cast<float>(_SQRT_EXP1);
            }

            // Averaged width and amplitude estimates
            float Sr = 0.5f * (-srl + srr);
            float Ar = 0.5f * (Arl + Arr);
            float Sc = 0.5f * (-scu + scd);
            float Ac = 0.5f * (Acu + Acd);

            // Quality checks: amplitude symmetry and width symmetry
            if (!m_params.relaxChecks) {
                if (std::max(Ar, Ac) < static_cast<float>(locthresh)) continue;

                float dA = std::max(std::fabs(Ar), std::fabs(Ac)) /
                           std::min(std::fabs(Ar) + 1e-6f, std::fabs(Ac) + 1e-6f);

                float srl_abs = std::fabs(srl), srr_abs = std::fabs(srr);
                float scu_abs = std::fabs(scu), scd_abs = std::fabs(scd);

                float dSr = (std::min(srl_abs, srr_abs) > 1e-6f)
                    ? std::max(srl_abs, srr_abs) / std::min(srl_abs, srr_abs) : 999.f;
                float dSc = (std::min(scu_abs, scd_abs) > 1e-6f)
                    ? std::max(scu_abs, scd_abs) / std::min(scu_abs, scd_abs) : 999.f;

                if (dA > 2.f || dSr > 2.f || dSc > 2.f) continue;
            }

            // --- Stage 7: Adaptive box radius ---
            int Rr = static_cast<int>(std::ceil(s_factor * static_cast<double>(Sr)));
            int Rc = static_cast<int>(std::ceil(s_factor * static_cast<double>(Sc)));
            int Rm = std::max(Rr, Rc);
            Rm     = std::min(Rm, MAX_BOX_RADIUS);
            int R  = std::max(Rm, r);
            R = std::min({R, xx, yy, w - xx - 1, h - yy - 1});
            if (R <= 0) continue;

            // --- Stage 8: Duplicate removal ---
            int matchr = std::max(1, static_cast<int>(static_cast<double>(R) * MAX_RADIUS_RATIO_DUP));
            bool isdup = false;
            for (int ci = static_cast<int>(candidates.size()) - 1; ci >= 0; ci--) {
                if (std::abs(xx - candidates[ci].x) + std::abs(yy - candidates[ci].y) <= matchr) {
                    isdup = true;
                    break;
                }
                if (yy - candidates[ci].y > R) break;
            }
            if (isdup) continue;

            // Store accepted candidate
            Cand c;
            c.x            = xx;
            c.y            = yy;
            c.mag_est      = static_cast<float>(meanhigh);
            c.sx           = Sr;
            c.sy           = Sc;
            c.sat          = has_sat ? sat_lv : 1.0f;
            c.R            = R;
            c.hasSaturated = has_sat;
            c.isColor      = !ismono;
            candidates.push_back(c);
        }
    }

    // -----------------------------------------------------------------------
    // Stage 9: Sort candidates by estimated brightness (descending)
    // -----------------------------------------------------------------------
    std::sort(candidates.begin(), candidates.end(),
              [](const Cand& a, const Cand& b) { return a.mag_est > b.mag_est; });

    // -----------------------------------------------------------------------
    // Stage 10: PSF fitting and quality rejection
    // -----------------------------------------------------------------------
    std::vector<DetectedStar> stars;
    stars.reserve(std::min(static_cast<int>(candidates.size()), m_maxStars));

    int limit     = (m_maxStars > 0) ? m_maxStars : static_cast<int>(candidates.size());
    int toProcess = std::min(static_cast<int>(candidates.size()), limit + limit / 4);

    for (int ci = 0; ci < toProcess; ci++) {
        if (static_cast<int>(stars.size()) >= limit) break;
        const Cand& cand = candidates[ci];

        int R    = cand.R;
        int bx0  = cand.x - R;
        int by0  = cand.y - R;
        int boxw = 2 * R + 1;
        int boxh = 2 * R + 1;

        // Extract the fitting box from the raw image
        std::vector<double> box(static_cast<size_t>(boxw) * boxh);
        for (int jj = 0; jj < boxh; jj++) {
            for (int ii = 0; ii < boxw; ii++) {
                int ix = std::clamp(bx0 + ii, 0, w - 1);
                int iy = std::clamp(by0 + jj, 0, h - 1);
                box[static_cast<size_t>(jj) * boxw + ii] =
                    static_cast<double>(raw[(iy * w + ix) * ch + channel]);
            }
        }

        // Perform PSF fit
        PsfError err = PsfError::OK;
        PsfStar* psf = PsfFitter::fit(
            box.data(),
            static_cast<size_t>(boxh), static_cast<size_t>(boxw),
            bg.median, static_cast<double>(cand.sat),
            m_params.convergence,
            /*fromPeaker=*/true,
            m_params.profile, &err);

        if (!psf) continue;

        // Apply rejection criteria
        RejectReason rej = rejectStar(
            psf,
            static_cast<double>(cand.sx), static_cast<double>(cand.sy),
            R, cand.isColor, dynrange, m_params.sigma);

        bool accepted = (rej == RejectReason::OK) ||
                        (m_params.relaxChecks && rej == RejectReason::RmseTooLarge);
        if (!accepted) { delete psf; continue; }

        // Flag saturated stars
        psf->has_saturated = (psf->A > dynrange);

        // Enforce minimum FWHM
        if (psf->fwhmx < static_cast<double>(m_minFWHM) &&
            psf->fwhmy < static_cast<double>(m_minFWHM))
        {
            delete psf;
            continue;
        }

        // Populate the output record
        DetectedStar ds;
        ds.x          = bx0 + psf->x0;
        ds.y          = by0 + psf->y0;
        ds.flux       = (psf->mag < 90.0) ? std::pow(10.0, -0.4 * psf->mag) : 0.0;
        ds.peak       = static_cast<double>(bpix(cand.y, cand.x));
        ds.background = bg.median;
        ds.fwhm       = static_cast<float>(psf->fwhmx);
        ds.fwhmx      = static_cast<float>(psf->fwhmx);
        ds.fwhmy      = static_cast<float>(psf->fwhmy);
        ds.angle      = static_cast<float>(psf->angle);
        ds.rmse       = static_cast<float>(psf->rmse);
        ds.beta       = static_cast<float>(psf->beta);
        ds.saturated  = cand.hasSaturated || psf->has_saturated;
        ds.R          = R;
        ds.psf        = std::shared_ptr<PsfStar>(psf);
        stars.push_back(std::move(ds));
    }

    // -----------------------------------------------------------------------
    // Stage 11: Final sort by flux (descending) and truncation
    // -----------------------------------------------------------------------
    std::sort(stars.begin(), stars.end(),
              [](const DetectedStar& a, const DetectedStar& b) { return a.flux > b.flux; });

    if (static_cast<int>(stars.size()) > m_maxStars)
        stars.resize(static_cast<size_t>(m_maxStars));

    return stars;
}