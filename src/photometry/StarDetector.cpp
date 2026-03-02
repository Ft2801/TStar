/*
 * StarDetector.cpp 
 *
 * Pipeline (peaker → minimize_candidates equivalent):
 *   1.  Gaussian blur (σ = KERNEL_SIZE = 2.0)
 *   2.  Sigma-clipped background → threshold = bg + σ * 5 * bgnoise
 *   3.  Local max in (2r+1)² box with tie-breaking
 *   4.  Neighbourhood density check (≥3 mono / ≥8 colour)
 *   5.  1st-derivative sub-pixel centre
 *   6.  2nd-derivative width estimates Sr, Sc
 *   7.  Box radius R = clamp(ceil(s_factor · max(Sr,Sc)), r, MAX_BOX)
 *   8.  Duplicate removal
 *   9.  PSF fit (PsfFitter::fit)
 *  10.  Rejection criteria
 *  11.  Sort by magnitude / flux
 */

#include "StarDetector.h"
#include "PsfFitter.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_sort.h>

// ── Constants ────────────────────────────────────
static constexpr double KERNEL_SIZE          = 2.0;
static constexpr double DENSITY_THRESHOLD    = 0.001;
static constexpr double SAT_THRESHOLD        = 0.7;
static constexpr double SAT_DETECTION_RANGE  = 0.1;
static constexpr int    MAX_BOX_RADIUS       = 200;
static constexpr double MAX_RADIUS_RATIO_DUP = 0.2;
static constexpr double _2_SQRT_2_LOG2       = 2.35482004503; // Gaussian FWHM = sx * this
static constexpr double _SQRT_EXP1           = 1.6487212707;  // sqrt(e)

// ─────────────────────────────────────────────────────────────────────────────
StarDetector::StarDetector()  = default;
StarDetector::~StarDetector() = default;

// ─── Gaussian blur ────────────────────────────────────────────────────────────
void StarDetector::gaussianBlur(const float* src, float* dst,
                                 int w, int h, int ch, int channel, float sigma)
{
    int kr    = std::max(1, (int)std::ceil(sigma * 3.0f));
    int ksize = 2 * kr + 1;
    std::vector<float> kern(ksize);
    float sumk = 0.0f;
    for (int k = 0; k < ksize; ++k) {
        float x = (float)(k - kr);
        kern[k] = std::exp(-x * x / (2.0f * sigma * sigma));
        sumk += kern[k];
    }
    for (float& v : kern) v /= sumk;

    std::vector<float> tmp(w * h, 0.0f);

    // Horizontal pass: extract single channel and blur
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

// ─── Background statistics ────────────────────────────────────────────────────
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
    if (ngood < 1) { *mean = 0.0; *sigma = 0.0; return; }
    double m = sum / ngood;
    *mean  = m;
    *sigma = (ngood > 1) ? std::sqrt(std::max(0.0, sum2 / ngood - m * m)) : 0.0;
}

static constexpr int    FN_NITER       = 3;
static constexpr double FN_SIGMA_CLIP  = 5.0;

StarDetector::BgStats StarDetector::computeBackground(
        const float* data, int w, int h, int ch, int channel) const
{
    BgStats out{};

    // ── MAX and median background via subsampled pass ──
    const int step = 8;
    std::vector<double> samp;
    samp.reserve(((w / step) + 1) * ((h / step) + 1));
    double maxval = 0.0;

    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            double v = (double)data[(y * w + x) * ch + channel];
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

    // ── BGNOISE ──
    // For each row: collect consecutive differences, sigma-clip, record stdev.
    // bgnoise = 1/sqrt(2) * median of per-row stdevs
    std::vector<double> rowNoise;
    rowNoise.reserve(h);

    std::vector<float> diffs;
    diffs.reserve(w);

    for (int y = 0; y < h; ++y) {
        diffs.clear();
        float prev = 0.0f;
        bool hasPrev = false;

        for (int x = 0; x < w; ++x) {
            float v = data[(y * w + x) * ch + channel];
            if (v == 0.0f || std::isnan(v)) { hasPrev = false; continue; }
            if (hasPrev) diffs.push_back(prev - v);
            prev    = v;
            hasPrev = true;
        }
        if ((int)diffs.size() < 2) continue;

        double mean = 0.0, stdev = 0.0;
        fnMeanSigma(diffs.data(), (int)diffs.size(), &mean, &stdev);

        for (int iter = 0; iter < FN_NITER && stdev > 0.0; ++iter) {
            int kk = 0;
            double limit = FN_SIGMA_CLIP * stdev;
            for (int i = 0; i < (int)diffs.size(); ++i) {
                if (std::fabs(diffs[i] - (float)mean) < limit)
                    diffs[kk++] = diffs[i];
            }
            if (kk == (int)diffs.size()) break;
            diffs.resize(kk);
            if (kk < 2) { stdev = 0.0; break; }
            fnMeanSigma(diffs.data(), kk, &mean, &stdev);
        }

        if (stdev > 0.0)
            rowNoise.push_back(stdev);
    }

    if (rowNoise.empty()) {
        // Fallback: MAD estimation if FnNoise1 yields nothing
        std::vector<double> adev(samp.size());
        for (size_t i = 0; i < samp.size(); ++i) adev[i] = std::fabs(samp[i] - out.median);
        gsl_sort(adev.data(), 1, adev.size());
        double mad = gsl_stats_median_from_sorted_data(adev.data(), 1, adev.size());
        out.bgnoise = 1.4826 * mad;
    } else {
        std::sort(rowNoise.begin(), rowNoise.end());
        size_t nr = rowNoise.size();
        double medNoise = (nr % 2 == 1)
            ? rowNoise[nr / 2]
            : 0.5 * (rowNoise[(nr - 1) / 2] + rowNoise[nr / 2]);
        out.bgnoise = 0.70710678 * medNoise;  // 1/sqrt(2) * row-median stdev
    }

    return out;
}

// ─── Rejection criteria ────────────────────────────────────────────────────────
StarDetector::RejectReason StarDetector::rejectStar(
        const PsfStar* psf,
        double sx, double sy,
        int /*R*/, bool isColor,
        double dynrange, double sigma) const
{
    if (std::isnan(psf->fwhmx) || std::isnan(psf->fwhmy)) return RejectReason::NoFwhm;
    if (std::isnan(psf->x0)   || std::isnan(psf->y0))    return RejectReason::NoPos;
    if (std::isnan(psf->mag))                              return RejectReason::NoMag;

    if (isColor && (psf->fwhmx <= 1.0 || psf->fwhmy <= 1.0))
        return RejectReason::FwhmTooSmall;
    if (psf->fwhmx <= 0.0 || psf->fwhmy <= 0.0)
        return RejectReason::FwhmNeg;

    double r = psf->fwhmy / psf->fwhmx;
    if (r < m_params.roundness || r > m_params.maxRoundness)
        return RejectReason::RoundnessBelow;

    // FWHM too large relative to estimated width (gets looser for larger stars)
    double maxs   = std::max(sx, sy);
    double kernel = std::max(maxs / KERNEL_SIZE, 1.001);
    double fwhmLimitFactor = _2_SQRT_2_LOG2 * (1.0 + 0.5 * std::log(kernel));
    if (psf->fwhmx > maxs * fwhmLimitFactor)
        return RejectReason::FwhmTooLarge;

    // RMSE criterion (skip for saturated stars — A > dynrange)
    if (!m_params.relaxChecks) {
        if ((psf->rmse * sigma / psf->A) > 0.2 && !(psf->A > dynrange))
            return RejectReason::RmseTooLarge;
    }

    // Amplitude range
    if (m_params.minAmplitude > 0.0 && psf->A < m_params.minAmplitude)
        return RejectReason::AmplitudeOutOfRange;
    if (m_params.maxAmplitude > 0.0 && psf->A > m_params.maxAmplitude)
        return RejectReason::AmplitudeOutOfRange;

    // Moffat β
    if (m_params.profile == PsfProfile::Moffat && psf->beta < m_params.minBeta)
        return RejectReason::MoffatBetaTooSmall;

    return RejectReason::OK;
}

// ─── detect() — main pipeline ─────────────────────────────────────────────────
std::vector<DetectedStar> StarDetector::detect(const ImageBuffer& image, int channel)
{
    const int   w   = image.width();
    const int   h   = image.height();
    const int   ch  = image.channels();
    const float* raw = image.data().data();

    // Clamp channel index
    channel = std::clamp(channel, 0, ch - 1);

    // ── 1. Gaussian blur ──
    std::vector<float> blurred(w * h);
    gaussianBlur(raw, blurred.data(), w, h, ch, channel, (float)KERNEL_SIZE);

    // ── 2. Background stats → threshold ──
    BgStats bg = computeBackground(raw, w, h, ch, channel);
    if (bg.bgnoise < 1e-12) bg.bgnoise = 1e-6;

    double threshold   = bg.median + m_params.sigma * 5.0 * bg.bgnoise;
    double dynrange    = std::min(bg.max, 1.0) - bg.median;
    double minsatlevel = dynrange * SAT_THRESHOLD;
    double satrange2   = dynrange * SAT_DETECTION_RANGE;
    double s_factor    = std::sqrt(-2.0 * std::log(DENSITY_THRESHOLD)); // ≈ 3.717
    bool   ismono      = (ch == 1);
    double locthresh   = m_params.sigma * 5.0 * bg.bgnoise;

    // Safe pixel accessor for blurred image (clamps edges)
    auto bpix = [&](int y, int x) -> float {
        y = std::clamp(y, 0, h - 1);
        x = std::clamp(x, 0, w - 1);
        return blurred[y * w + x];
    };

    // Internal candidate struct
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

    // ── 3–8. Candidate detection loop ──
    for (int y = r; y < h - r; y++) {
        for (int x = r; x < w - r; x++) {

            float pixel = bpix(y, x);
            if (pixel <= (float)threshold) continue;

            // ── 3. Local max in (2r+1)² box with tie-breaking ──
            bool bingo = true;
            int  cnt   = 0;
            for (int yy2 = y - r; yy2 <= y + r && bingo; yy2++) {
                for (int xx2 = x - r; xx2 <= x + r; xx2++) {
                    if (xx2 == x && yy2 == y) continue;
                    float nb = bpix(yy2, xx2);
                    if (nb > pixel) { bingo = false; break; }
                    if (nb == pixel) {
                        if ((xx2 <= x && yy2 <= y) || (xx2 > x && yy2 < y)) {
                            bingo = false; break;
                        }
                    }
                    cnt++;
                }
            }
            if (!bingo || cnt < boxsize - 1) continue;

            // Advance x by r (no other local max within r columns)
            x += r;
            int xx = x - r, yy = y;

            // ── 4. Neighbourhood density check ──
            int    cnt2    = 0;
            double meanhigh = 0.0, minhigh = 1e30;
            for (int dy2 = -1; dy2 <= 1; dy2++) {
                for (int dx2 = -1; dx2 <= 1; dx2++) {
                    if (dx2 == 0 && dy2 == 0) continue;
                    int nx2 = xx + dx2, ny2 = yy + dy2;
                    if (nx2 < 0 || nx2 >= w || ny2 < 0 || ny2 >= h) continue;
                    float nb = bpix(ny2, nx2);
                    if (nb >= (float)threshold) {
                        if (nb < minhigh) minhigh = (double)nb;
                        meanhigh += (double)nb;
                        cnt2++;
                    }
                }
            }
            if (cnt2 == 0 || (ismono && cnt2 < 3) || (!ismono && cnt2 < 8)) continue;
            meanhigh /= (double)cnt2;
            float pixel0 = bpix(yy, xx);

            // ── Saturation detection ──
            bool  has_sat  = false;
            float sat_lv   = 1.0f;

            if (!(meanhigh - bg.median < minsatlevel || (double)(pixel0 - (float)minhigh) > satrange2)) {
                has_sat = true;
                sat_lv  = (float)(std::min((double)pixel0, 1.0) - satrange2);

                // Edge-walk the plateau
                int i2 = 0, j2 = 0, xr = 0, xl = 0, yu2 = 0, yd2 = 0;
                while (xx + i2 < w - 1 && bpix(yy, xx + i2) > sat_lv) i2++;
                while (xx + i2 < w - 1 && yy + j2 < h - 1 &&
                       (bpix(yy+j2+1, xx+i2+1) > sat_lv || bpix(yy+j2+1, xx+i2) > sat_lv)) {
                    if (bpix(yy+j2+1, xx+i2+1) > sat_lv) i2++;
                    j2++;
                }
                xr = i2;
                while (xx + i2 > 1 && yy + j2 < h - 1 &&
                       (bpix(yy+j2+1, xx+i2-1) > sat_lv || bpix(yy+j2, xx+i2-1) > sat_lv)) {
                    if (bpix(yy+j2+1, xx+i2-1) > sat_lv) j2++;
                    i2--;
                }
                yd2 = j2;
                while (xx + i2 > 1 && yy + j2 > 1 &&
                       (bpix(yy+j2-1, xx+i2-1) > sat_lv || bpix(yy+j2-1, xx+i2) > sat_lv)) {
                    if (bpix(yy+j2-1, xx+i2-1) > sat_lv) i2--;
                    j2--;
                }
                xl = i2;
                while (xx + i2 < w - 1 && yy + j2 > 1 &&
                       (bpix(yy+j2-1, xx+i2+1) > sat_lv || bpix(yy+j2, xx+i2+1) > sat_lv)) {
                    if (bpix(yy+j2-1, xx+i2+1) > sat_lv) j2--;
                    i2++;
                }
                yu2 = j2;
                while (xx + i2 < w - 1 && yy + j2 < h - 1 &&
                       (bpix(yy+j2+1, xx+i2+1) > sat_lv || bpix(yy+j2+1, xx+i2) > sat_lv)) {
                    if (bpix(yy+j2+1, xx+i2+1) > sat_lv) i2++;
                    j2++;
                }
                if (i2 > xr) xr = i2;
                xx += (xr + xl) / 2;
                yy += (yu2 + yd2) / 2;
                x  += xr; // advance outer loop
            }

            // Check proximity to border
            if (xx - 2 < 1 || xx + 2 > w - 2 || yy - 2 < 1 || yy + 2 > h - 2) continue;

            // ── 5. Sub-pixel centre (1st derivative) ──
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

            // ── 6. 2nd derivative width estimates ──
            float srl, srr, scu, scd;
            float Arl, Arr, Acu, Acd;
            {
                // Right
                int ii = 0;
                if (has_sat) while (xx + ii < w     && bpix(yy, xx + ii) > sat_lv) ii++;
                if (xx + ii > w - 3) continue;
                float d2a = bpix(yy, xx+ii+1) + bpix(yy, xx+ii-1) - 2*bpix(yy, xx+ii);
                float d2b = bpix(yy, xx+ii+2) + bpix(yy, xx+ii)   - 2*bpix(yy, xx+ii+1);
                while (d2b < 0 && xx + ii + 2 < w - 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy,xx+ii+2)+bpix(yy,xx+ii)-2*bpix(yy,xx+ii+1);
                }
                srr  = (float)ii - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a/(d2b-d2a) : 0.f) - r0;
                Arr  = -(bpix(yy,xx+ii)-bpix(yy,xx+ii-1)) * srr * (float)_SQRT_EXP1;
            }
            {
                // Left
                int ii = 0;
                if (has_sat) while (xx - ii > 0 && bpix(yy, xx - ii) > sat_lv) ii++;
                if (xx - ii < 2) continue;
                float d2a = bpix(yy,xx-ii-1)+bpix(yy,xx-ii+1)-2*bpix(yy,xx-ii);
                float d2b = bpix(yy,xx-ii-2)+bpix(yy,xx-ii)  -2*bpix(yy,xx-ii-1);
                while (d2b < 0 && xx - ii - 2 > 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy,xx-ii-2)+bpix(yy,xx-ii)-2*bpix(yy,xx-ii-1);
                }
                srl  = -((float)ii - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a/(d2b-d2a) : 0.f)) - r0;
                Arl  =  (bpix(yy,xx-ii)-bpix(yy,xx-ii+1)) * srl * (float)_SQRT_EXP1;
            }
            {
                // Down
                int ii = 0;
                if (has_sat) while (yy + ii < h     && bpix(yy+ii, xx) > sat_lv) ii++;
                if (yy + ii > h - 3) continue;
                float d2a = bpix(yy+ii+1,xx)+bpix(yy+ii-1,xx)-2*bpix(yy+ii,xx);
                float d2b = bpix(yy+ii+2,xx)+bpix(yy+ii,  xx)-2*bpix(yy+ii+1,xx);
                while (d2b < 0 && yy + ii + 2 < h - 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy+ii+2,xx)+bpix(yy+ii,xx)-2*bpix(yy+ii+1,xx);
                }
                scd  = (float)ii - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a/(d2b-d2a) : 0.f) - c0;
                Acd  = -(bpix(yy+ii,xx)-bpix(yy+ii-1,xx)) * scd * (float)_SQRT_EXP1;
            }
            {
                // Up
                int ii = 0;
                if (has_sat) while (yy - ii > 0 && bpix(yy-ii, xx) > sat_lv) ii++;
                if (yy - ii < 2) continue;
                float d2a = bpix(yy-ii-1,xx)+bpix(yy-ii+1,xx)-2*bpix(yy-ii,xx);
                float d2b = bpix(yy-ii-2,xx)+bpix(yy-ii,  xx)-2*bpix(yy-ii-1,xx);
                while (d2b < 0 && yy - ii - 2 > 1) {
                    ii++; d2a = d2b;
                    d2b = bpix(yy-ii-2,xx)+bpix(yy-ii,xx)-2*bpix(yy-ii-1,xx);
                }
                scu  = -((float)ii - ((std::fabs(d2b - d2a) > 1e-10f) ? d2a/(d2b-d2a) : 0.f)) - c0;
                Acu  =  (bpix(yy-ii,xx)-bpix(yy-ii+1,xx)) * scu * (float)_SQRT_EXP1;
            }

            float Sr = 0.5f * (-srl + srr);
            float Ar = 0.5f * (Arl + Arr);
            float Sc = 0.5f * (-scu + scd);
            float Ac = 0.5f * (Acu + Acd);

            // Quality check
            if (!m_params.relaxChecks) {
                if (std::max(Ar, Ac) < (float)locthresh) continue;
                float minAr = std::min(std::fabs(Ar), 1e-6f);
                float minAc = std::min(std::fabs(Ac), 1e-6f);
                float dA = std::max(std::fabs(Ar), std::fabs(Ac)) /
                           std::min(std::fabs(Ar) + 1e-6f, std::fabs(Ac) + 1e-6f);
                float srl_abs = std::fabs(srl), srr_abs = std::fabs(srr);
                float scu_abs = std::fabs(scu), scd_abs = std::fabs(scd);
                float dSr = (std::min(srl_abs, srr_abs) > 1e-6f)
                            ? std::max(srl_abs, srr_abs) / std::min(srl_abs, srr_abs) : 999.f;
                float dSc = (std::min(scu_abs, scd_abs) > 1e-6f)
                            ? std::max(scu_abs, scd_abs) / std::min(scu_abs, scd_abs) : 999.f;
                if (dA > 2.f || dSr > 2.f || dSc > 2.f) continue;
                (void)minAr; (void)minAc;
            }

            // ── 7. Box radius ──
            int Rr = (int)std::ceil(s_factor * (double)Sr);
            int Rc = (int)std::ceil(s_factor * (double)Sc);
            int Rm = std::max(Rr, Rc);
            Rm     = std::min(Rm, MAX_BOX_RADIUS);
            int R  = std::max(Rm, r);
            // clamp to image boundaries
            R = std::min({R, xx, yy, w - xx - 1, h - yy - 1});
            if (R <= 0) continue;

            // ── 8. Duplicate removal ──
            int matchr = std::max(1, (int)((double)R * MAX_RADIUS_RATIO_DUP));
            bool isdup = false;
            for (int ci = (int)candidates.size() - 1; ci >= 0; ci--) {
                if (std::abs(xx - candidates[ci].x) + std::abs(yy - candidates[ci].y) <= matchr) {
                    isdup = true; break;
                }
                if (yy - candidates[ci].y > R) break;
            }
            if (isdup) continue;

            Cand c;
            c.x            = xx;
            c.y            = yy;
            c.mag_est      = (float)meanhigh;
            c.sx           = Sr;
            c.sy           = Sc;
            c.sat          = has_sat ? sat_lv : 1.0f;
            c.R            = R;
            c.hasSaturated = has_sat;
            c.isColor      = !ismono;
            candidates.push_back(c);
        }
    }

    // ── 9. Sort by estimated brightness (descending) ──
    std::sort(candidates.begin(), candidates.end(), [](const Cand& a, const Cand& b){
        return a.mag_est > b.mag_est;
    });

    // ── 10. PSF fit + rejection ──
    std::vector<DetectedStar> stars;
    stars.reserve(std::min((int)candidates.size(), m_maxStars));

    int limit     = (m_maxStars > 0) ? m_maxStars : (int)candidates.size();
    int toProcess = std::min((int)candidates.size(), limit + limit / 4);

    for (int ci = 0; ci < toProcess; ci++) {
        if ((int)stars.size() >= limit) break;
        const Cand& cand = candidates[ci];

        int R    = cand.R;
        int bx0  = cand.x - R;
        int by0  = cand.y - R;
        int boxw = 2 * R + 1;
        int boxh = 2 * R + 1;

        // Extract box pixels from raw image
        std::vector<double> box((size_t)boxw * boxh);
        for (int jj = 0; jj < boxh; jj++) {
            for (int ii = 0; ii < boxw; ii++) {
                int ix = std::clamp(bx0 + ii, 0, w - 1);
                int iy = std::clamp(by0 + jj, 0, h - 1);
                box[(size_t)jj * boxw + ii] = (double)raw[(iy * w + ix) * ch + channel];
            }
        }

        PsfError err = PsfError::OK;
        PsfStar* psf = PsfFitter::fit(box.data(), (size_t)boxh, (size_t)boxw,
                                       bg.median, (double)cand.sat,
                                       m_params.convergence,
                                       /*fromPeaker=*/true,
                                       m_params.profile, &err);
        if (!psf) continue;

        // Rejection
        RejectReason rej = rejectStar(psf, (double)cand.sx, (double)cand.sy,
                                       R, cand.isColor, dynrange, m_params.sigma);
        bool accepted = (rej == RejectReason::OK)
                     || (m_params.relaxChecks && rej == RejectReason::RmseTooLarge);
        if (!accepted) { delete psf; continue; }

        // Mark saturated if amplitude exceeds dynamic range 
        psf->has_saturated = (psf->A > dynrange);

        // Minimum FWHM
        if (psf->fwhmx < (double)m_minFWHM && psf->fwhmy < (double)m_minFWHM) {
            delete psf; continue;
        }

        DetectedStar ds;
        ds.x          = bx0 + psf->x0;  // absolute image X
        ds.y          = by0 + psf->y0;  // absolute image Y
        // Convert magnitude back to relative flux (instrumental)
        ds.flux       = (psf->mag < 90.0) ? std::pow(10.0, -0.4 * psf->mag) : 0.0;
        ds.peak       = (double)bpix(cand.y, cand.x);
        ds.background = bg.median;
        ds.fwhm       = (float)psf->fwhmx;
        ds.fwhmx      = (float)psf->fwhmx;
        ds.fwhmy      = (float)psf->fwhmy;
        ds.angle      = (float)psf->angle;
        ds.rmse       = (float)psf->rmse;
        ds.beta       = (float)psf->beta;
        ds.saturated  = cand.hasSaturated || psf->has_saturated;
        ds.R          = R;
        ds.psf        = std::shared_ptr<PsfStar>(psf);
        stars.push_back(std::move(ds));
    }

    // ── 11. Final sort by flux (descending) ──
    std::sort(stars.begin(), stars.end(), [](const DetectedStar& a, const DetectedStar& b){
        return a.flux > b.flux;
    });
    if ((int)stars.size() > m_maxStars) stars.resize((size_t)m_maxStars);

    return stars;
}
