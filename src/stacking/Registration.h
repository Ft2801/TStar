/**
 * @file Registration.h
 * @brief Image registration (alignment) system
 * 
 * Implements star-based image registration for aligning
 * astronomical images before stacking.
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef REGISTRATION_H
#define REGISTRATION_H

#include "../ImageBuffer.h"
#include "StackingTypes.h"
#include "StackingSequence.h"
#include <QObject>
#include <QThread>
#include <vector>
#include <memory>

// Forward declaration
struct MatchStar;

namespace Stacking {

/**
 * @brief Star detection result
 */
struct DetectedStar {
    float x = 0.0f;         ///< X coordinate (subpixel)
    float y = 0.0f;         ///< Y coordinate (subpixel)
    float flux = 0.0f;      ///< Total flux
    float peak = 0.0f;      ///< Peak value
    float fwhm = 0.0f;      ///< Full width at half maximum
    float roundness = 0.0f; ///< b/a ratio (1 = circular)
    float snr = 0.0f;       ///< Signal-to-noise ratio
    
    bool operator<(const DetectedStar& other) const {
        return flux > other.flux;  // Sort by flux descending
    }
};



/**
 * @brief Registration parameters
 */
struct RegistrationParams {
    // Star detection
    float detectionThreshold = 4.0f;  ///< Detection sigma
    int minStars = 20;                ///< Minimum stars required
    int maxStars = 2000;              ///< Maximum stars to use
    float minFWHM = 1.0f;             ///< Minimum FWHM filter
    float maxFWHM = 20.0f;            ///< Maximum FWHM filter
    float minRoundness = 0.3f;        ///< Minimum roundness
    
    // Matching
    float matchTolerance = 0.002f;    ///< Triangle matching tolerance
    int minMatches = 4;               ///< Minimum triangle matches
    
    // Transform type
    bool allowRotation = true;
    bool allowScale = false;
    bool highPrecision = true;        ///< Use subpixel refinement
    
    // Drizzle
    bool drizzle = false;
    float drizzleScale = 2.0f;
    float drizzleDropSize = 0.9f;
    
    // Output
    QString outputDirectory;          ///< Optional output directory
};

/**
 * @brief Result of registration for one image
 */
struct RegistrationResult {
    bool success = false;
    RegistrationData transform;
    int starsDetected = 0;
    int starsMatched = 0;
    double quality = 0.0;
    QString error;
};

/**
 * @brief Image registration engine
 */
class RegistrationEngine : public QObject {
    Q_OBJECT
    
public:
    explicit RegistrationEngine(QObject* parent = nullptr);
    ~RegistrationEngine() override;
    
    /**
     * @brief Set registration parameters
     */
    void setParams(const RegistrationParams& params) { m_params = params; }
    const RegistrationParams& params() const { return m_params; }
    
    /**
     * @brief Register a sequence of images
     * @param sequence Image sequence to register
     * @param referenceIndex Index of reference image
     * @return Number of successfully registered images
     */
    int registerSequence(ImageSequence& sequence, int referenceIndex = -1);
    
    /**
     * @brief Register a single image against reference
     * @param image Image to register
     * @param reference Reference image
     * @return Registration result
     */
    RegistrationResult registerImage(const ImageBuffer& image, 
                                      const ImageBuffer& reference);
    
    /**
     * @brief Detect stars in an image
     * @param image Input image
     * @return Vector of detected stars
     */
    std::vector<DetectedStar> detectStars(const ImageBuffer& image);
    
    /**
     * @brief Match stars between two images
     * @param stars1 Stars from first image
     * @param stars2 Stars from second image
     * @param transform Output transformation
     * @return Number of matched stars
     */
    int matchStars(const std::vector<DetectedStar>& stars1,
                   const std::vector<DetectedStar>& stars2,
                   RegistrationData& transform);
    
public slots:
    /**
     * @brief Request cancellation
     */
    void cancel() { m_cancelled = true; }
    
public:
    bool isCancelled() const { return m_cancelled; }
    
    /**
     * @brief Set callbacks
     */
    void setProgressCallback(ProgressCallback cb) { m_progressCallback = cb; }
    void setLogCallback(LogCallback cb) { m_logCallback = cb; }
    
signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageRegistered(int index, bool success);
    void finished(int successCount);
    
private:
    /**
     * @brief Extract luminance channel
     */
    std::vector<float> extractLuminance(const ImageBuffer& image);
    
    /**
     * @brief Compute background statistics
     */
    void computeBackground(const std::vector<float>& data, int width, int height,
                          float& background, float& rms);
    
    /**
     * @brief Find local maxima (potential stars)
     */
    std::vector<std::pair<int, int>> findLocalMaxima(
        const std::vector<float>& data, int width, int height,
        float threshold);
    
    /**
     * @brief Refine star position using centroid
     */
    bool refineStarPosition(const std::vector<float>& data, int width, int height,
                           int cx, int cy, DetectedStar& star);
    
    /**
     * @brief Compute conversion from DetectedStar to MatchStar
     */
    bool convertToMatchStars(const std::vector<DetectedStar>& src, 
                             std::vector<struct MatchStar>& dst);
    
    RegistrationParams m_params;
    ProgressCallback m_progressCallback;
    LogCallback m_logCallback;
    std::vector<DetectedStar> m_referenceStars;
    bool m_cancelled = false;
};

/**
 * @brief Worker thread for registration
 */
class RegistrationWorker : public QThread {
    Q_OBJECT
    
public:
    RegistrationWorker(ImageSequence* sequence, 
                       const RegistrationParams& params,
                       int referenceIndex = -1,
                       QObject* parent = nullptr);
    
    void run() override;
    void requestCancel() { m_engine.cancel(); }
    
signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageRegistered(int index, bool success);
    void finished(int successCount);
    
private:
    ImageSequence* m_sequence;
    RegistrationParams m_params;
    int m_referenceIndex;
    RegistrationEngine m_engine;
};

} // namespace Stacking

#endif // REGISTRATION_H
