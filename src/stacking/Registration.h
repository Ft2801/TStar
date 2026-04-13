#ifndef REGISTRATION_H
#define REGISTRATION_H

/**
 * @file Registration.h
 * @brief Star-based image registration (alignment) system.
 *
 * Detects stars using a background-mesh threshold, matches them via
 * triangle-asterism hashing, and computes an affine (or full homography)
 * transformation that maps each frame onto the reference.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "../ImageBuffer.h"
#include "StackingTypes.h"
#include "StackingSequence.h"

#include <QObject>
#include <QThread>
#include <vector>
#include <memory>

struct MatchStar;   // Forward declaration (defined in TriangleMatcher.h)

namespace Stacking {

// ============================================================================
// DetectedStar
// ============================================================================

/**
 * @brief Properties of a single detected star.
 */
struct DetectedStar {
    float x         = 0.0f;   ///< Subpixel X coordinate (centroid)
    float y         = 0.0f;   ///< Subpixel Y coordinate (centroid)
    float flux      = 0.0f;   ///< Integrated flux above background
    float peak      = 0.0f;   ///< Peak pixel value above background
    float fwhm      = 0.0f;   ///< Full width at half maximum (pixels)
    float roundness = 0.0f;   ///< Minor/major axis ratio (1 = circular)
    float snr       = 0.0f;   ///< Signal-to-noise ratio

    /** Sort by flux descending (brightest first). */
    bool operator<(const DetectedStar& other) const {
        return flux > other.flux;
    }
};

// ============================================================================
// RegistrationParams
// ============================================================================

/**
 * @brief Tuneable parameters for the registration pipeline.
 */
struct RegistrationParams {

    // -- Star detection ------------------------------------------------------
    float detectionThreshold = 4.0f;   ///< Detection threshold (sigma above background)
    int   minStars           = 20;     ///< Minimum stars required for a valid frame
    int   maxStars           = 2000;   ///< Maximum stars retained after sorting by flux
    float minFWHM            = 1.0f;   ///< Minimum FWHM filter (pixels)
    float maxFWHM            = 20.0f;  ///< Maximum FWHM filter (pixels)
    float minRoundness       = 0.3f;   ///< Minimum roundness (b/a ratio)

    // -- Star matching -------------------------------------------------------
    float matchTolerance     = 0.002f; ///< Triangle-asterism matching tolerance
    int   minMatches         = 4;      ///< Minimum number of matched pairs

    // -- Transform model -----------------------------------------------------
    bool  allowRotation      = true;
    bool  allowScale         = false;
    bool  highPrecision      = true;   ///< Enable subpixel centroid refinement

    // -- Drizzle (optional) --------------------------------------------------
    bool  drizzle            = false;
    float drizzleScale       = 2.0f;
    float drizzleDropSize    = 0.9f;

    // -- Output --------------------------------------------------------------
    QString outputDirectory;           ///< Optional directory for registered files
};

// ============================================================================
// RegistrationResult
// ============================================================================

/**
 * @brief Outcome of registering a single image against the reference.
 */
struct RegistrationResult {
    bool             success       = false;
    RegistrationData transform;
    int              starsDetected = 0;
    int              starsMatched  = 0;
    double           quality       = 0.0;
    QString          error;
};

// ============================================================================
// RegistrationEngine
// ============================================================================

/**
 * @brief Core registration engine.
 *
 * Orchestrates the full pipeline:
 *   1. Star detection (background mesh + Gaussian smoothing + local maxima)
 *   2. Triangle-asterism matching (via TriangleMatcher)
 *   3. Iterative affine refinement with sigma-clipped least squares
 *   4. Final RANSAC affine fit (OpenCV estimateAffine2D)
 *   5. Perspective warp and border cleanup
 *   6. FITS output of the registered frame
 *
 * Thread safety: a single engine instance must not be called concurrently.
 * Internal OMP parallelism is used within each per-image step.
 */
class RegistrationEngine : public QObject {
    Q_OBJECT

public:
    explicit RegistrationEngine(QObject* parent = nullptr);
    ~RegistrationEngine() override;

    /** @brief Set detection and matching parameters. */
    void setParams(const RegistrationParams& params) { m_params = params; }

    /** @brief Read-only access to current parameters. */
    const RegistrationParams& params() const { return m_params; }

    /**
     * @brief Register every image in a sequence against the reference.
     *
     * @param sequence       The image sequence (modified: registration data
     *                       is written back into each SequenceImage).
     * @param referenceIndex Index of the reference frame (-1 = auto-select).
     * @return Number of successfully registered images.
     */
    int registerSequence(ImageSequence& sequence, int referenceIndex = -1);

    /**
     * @brief Register a single image against a reference buffer.
     *
     * @param image     Target image.
     * @param reference Reference image.
     * @return Registration result (success flag, transform, star counts).
     */
    RegistrationResult registerImage(const ImageBuffer& image,
                                     const ImageBuffer& reference);

    /**
     * @brief Detect stars in an image.
     *
     * @param image Input image buffer.
     * @return Detected stars sorted by flux (brightest first).
     */
    std::vector<DetectedStar> detectStars(const ImageBuffer& image);

    /**
     * @brief Match two star lists and compute the affine transform.
     *
     * @param stars1    Reference star list.
     * @param stars2    Target star list.
     * @param transform Output: computed registration data.
     * @return Number of matched star pairs.
     */
    int matchStars(const std::vector<DetectedStar>& stars1,
                   const std::vector<DetectedStar>& stars2,
                   RegistrationData& transform);

public slots:
    /** @brief Request cancellation of the current operation. */
    void cancel() { m_cancelled = true; }

public:
    bool isCancelled() const { return m_cancelled; }

    /** @brief Set an optional progress callback. */
    void setProgressCallback(ProgressCallback cb) { m_progressCallback = cb; }

    /** @brief Set an optional log callback. */
    void setLogCallback(LogCallback cb) { m_logCallback = cb; }

signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageRegistered(int index, bool success);
    void finished(int successCount);

private:

    /** @brief Extract a single-channel luminance image. */
    std::vector<float> extractLuminance(const ImageBuffer& image);

    /** @brief Compute global background level and RMS (legacy helper). */
    void computeBackground(const std::vector<float>& data,
                           int width, int height,
                           float& background, float& rms);

    /** @brief Find local maxima above a threshold (legacy stub). */
    std::vector<std::pair<int, int>> findLocalMaxima(
        const std::vector<float>& data,
        int width, int height,
        float threshold);

    /** @brief Refine a star position using weighted centroid (legacy helper). */
    bool refineStarPosition(const std::vector<float>& data,
                            int width, int height,
                            int cx, int cy,
                            DetectedStar& star);

    /** @brief Convert DetectedStar list to MatchStar list for the triangle matcher. */
    bool convertToMatchStars(const std::vector<DetectedStar>& src,
                             std::vector<MatchStar>& dst);

    RegistrationParams           m_params;
    ProgressCallback             m_progressCallback;
    LogCallback                  m_logCallback;
    std::vector<DetectedStar>    m_referenceStars;
    bool                         m_cancelled = false;
};

// ============================================================================
// RegistrationWorker
// ============================================================================

/**
 * @brief QThread wrapper that runs RegistrationEngine::registerSequence()
 *        on a background thread.
 */
class RegistrationWorker : public QThread {
    Q_OBJECT

public:
    RegistrationWorker(ImageSequence* sequence,
                       const RegistrationParams& params,
                       int referenceIndex = -1,
                       QObject* parent = nullptr);

    void run() override;

    /** @brief Request cancellation (thread-safe). */
    void requestCancel() { m_engine.cancel(); }

signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageRegistered(int index, bool success);
    void finished(int successCount);

private:
    ImageSequence*     m_sequence;
    RegistrationParams m_params;
    int                m_referenceIndex;
    RegistrationEngine m_engine;
};

} // namespace Stacking

#endif // REGISTRATION_H