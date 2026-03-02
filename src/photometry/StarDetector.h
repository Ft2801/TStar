/*
 * StarDetector
 *
 * Pipeline
 *   1. Gaussian blur (σ = KERNEL_SIZE = 2.0)
 *   2. Sigma-clipped background stats → threshold = bg + σ * 5 * bgnoise
 *   3. Local max search in (2r+1)² box with tie-breaking
 *   4. Neighbourhood density check (mono: ≥3, colour: ≥8)
 *   5. Sub-pixel centre from 1st-derivative zero crossing
 *   6. 2nd-derivative analysis → width estimates Sr, Sc
 *   7. Box radius  R = clamp(ceil(s_factor * max(Sr,Sc)), r, MAX_BOX_RADIUS)
 *   8. Duplicate removal within matchradius = max(1, R * MAX_RADIUS_RATIO_DUP)
 *   9. Per-candidate PSF fit (Gaussian or Moffat) via PsfFitter
 *  10. Rejection criteria (FWHM, roundness, RMSE, Moffat β)
 *  11. Sort by magnitude
 */
#ifndef STARDETECTOR_H
#define STARDETECTOR_H

#include "../ImageBuffer.h"
#include "PsfFitter.h"
#include <vector>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// DetectedStar — result of StarDetector::detect()
// ─────────────────────────────────────────────────────────────────────────────
struct DetectedStar {
    double x;           // sub-pixel centroid X (image coords, 0-based)
    double y;           // sub-pixel centroid Y
    double flux;        // integrated flux above background
    double peak;        // peak value (smoothed)
    double background;  // local background level
    float  fwhm;        // FWHM major axis (pixels), 0 if PSF fit not performed
    float  fwhmx;       // PSF FWHM along X (pixels)
    float  fwhmy;       // PSF FWHM along Y (pixels)
    float  angle;       // PSF rotation angle (degrees)
    float  rmse;        // PSF fit RMSE
    float  beta;        // Moffat β (-1 for Gaussian)
    bool   saturated;   // true if star has saturated pixels
    int    R;           // box half-size used for PSF fit
    std::shared_ptr<PsfStar> psf; // PSF fit result (may be null)
};

// ─────────────────────────────────────────────────────────────────────────────
// StarFinderParams — algorithm tuning knobs
// ─────────────────────────────────────────────────────────────────────────────
struct StarFinderParams {
    double      sigma          = 1.0;            // detection threshold multiplier
    double      roundness      = 0.5;            // minimum fwhmy/fwhmx
    double      maxRoundness   = 1.5;            // maximum fwhmy/fwhmx
    double      minBeta        = 1.5;            // minimum Moffat β
    int         radius         = 3;              // local max search half-size
    bool        relaxChecks    = false;          // allow RMSE_TOO_LARGE stars
    PsfProfile  profile        = PsfProfile::Gaussian;
    int         convergence    = 1;              // passed to PsfFitter::fit()
    double      minAmplitude   = 0.0;            // 0 = disabled
    double      maxAmplitude   = 0.0;            // 0 = disabled
};

// ─────────────────────────────────────────────────────────────────────────────
// StarDetector
// ─────────────────────────────────────────────────────────────────────────────
class StarDetector {
public:
    StarDetector();
    ~StarDetector();

    // ── Configuration helpers (keep old API) ──
    void setThresholdSigma(float sigma)  { m_params.sigma = sigma; }
    void setMinFWHM(float fwhm)          { m_minFWHM = fwhm; }
    void setMaxStars(int max)            { m_maxStars = max; }
    void setParams(const StarFinderParams& p) { m_params = p; }
    const StarFinderParams& params() const    { return m_params; }

    // ── Main entry point ──
    std::vector<DetectedStar> detect(const ImageBuffer& image, int channel = 0);

private:
    StarFinderParams m_params;
    float  m_minFWHM  = 0.5f;
    int    m_maxStars = 2000;

    // Background statistics (sigma-clipped)
    struct BgStats {
        double median  = 0.0;
        double bgnoise = 0.0;  // robust noise estimate (1.4826 * MAD of residuals)
        double max     = 0.0;
    };
    BgStats computeBackground(const float* data, int w, int h, int ch, int channel) const;

    // Separable Gaussian blur (σ = KERNEL_SIZE = 2.0)
    static void gaussianBlur(const float* src, float* dst,
                             int w, int h, int ch, int channel, float sigma);

    // Rejection criteria
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
