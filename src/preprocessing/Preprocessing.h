
#ifndef PREPROCESSING_H
#define PREPROCESSING_H

#include "PreprocessingTypes.h"
#include "MasterFrames.h"
#include "../ImageBuffer.h"
#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>

namespace Preprocessing {

/**
 * @brief Statistics for calibrated images
 */
struct CalibrationStats {
    int imagesProcessed = 0;
    int imagesFailed = 0;
    double avgBackground = 0.0;
    double avgDarkOptimK = 1.0;  ///< Average dark optimization factor
    int hotPixelsCorrected = 0;
    int coldPixelsCorrected = 0;
};

/**
 * @brief Main preprocessing engine
 * 
 * Handles the complete image calibration pipeline including
 * bias, dark, and flat calibration with optimizations.
 */
class PreprocessingEngine : public QObject {
    Q_OBJECT
    
public:
    explicit PreprocessingEngine(QObject* parent = nullptr);
    ~PreprocessingEngine() override;
    
    //=========================================================================
    // CONFIGURATION
    //=========================================================================
    
    /**
     * @brief Set preprocessing parameters
     */
    void setParams(const PreprocessParams& params);
    const PreprocessParams& params() const { return m_params; }
    
    /**
     * @brief Get/set master frames manager
     */
    MasterFrames& masters() { return m_masters; }
    const MasterFrames& masters() const { return m_masters; }
    
    //=========================================================================
    // SINGLE IMAGE PROCESSING
    //=========================================================================
    
    /**
     * @brief Preprocess a single image
     * @param input Input image
     * @param output Output calibrated image
     * @return true if successful
     */
    bool preprocessImage(const ImageBuffer& input, ImageBuffer& output);
    
    /**
     * @brief Preprocess a single file
     * @param inputPath Input file path
     * @param outputPath Output file path
     * @return true if successful
     */
    bool preprocessFile(const QString& inputPath, const QString& outputPath);
    
    //=========================================================================
    // BATCH PROCESSING
    //=========================================================================
    
    /**
     * @brief Preprocess multiple files
     * @param inputFiles List of input files
     * @param outputDir Output directory
     * @param progress Progress callback
     * @return Number of successfully processed files
     */
    int preprocessBatch(
        const QStringList& inputFiles,
        const QString& outputDir,
        ProgressCallback progress = nullptr
    );
    
    /**
     * @brief Get statistics from last processing
     */
    const CalibrationStats& lastStats() const { return m_stats; }
    
    //=========================================================================
    // CANCELLATION
    //=========================================================================
    
    void requestCancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled.load(); }
    
signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageProcessed(const QString& path, bool success);
    void finished(bool success);
    
private:
    //=========================================================================
    // CALIBRATION STEPS
    //=========================================================================
    
    /**
     * @brief Subtract bias from image
     */
    bool subtractBias(ImageBuffer& image);
    
    /**
     * @brief Subtract dark from image with optional optimization
     * @param image Image to calibrate
     * @param K Output: dark optimization factor used
     * @return true if successful
     */
    bool subtractDark(ImageBuffer& image, double& K);
    
    /**
     * @brief Apply cosmetic correction
     * @param image Image to correct
     * @param hotCorrected Output: number of hot pixels corrected
     * @param coldCorrected Output: number of cold pixels corrected
     */
    void applyCosmeticCorrection(ImageBuffer& image, 
                                  int& hotCorrected, int& coldCorrected);
    
    /**
     * @brief Debayer a CFA image
     */
    bool debayer(ImageBuffer& image);
    
    //=========================================================================
    // DEBAYERING ALGORITHMS
    //=========================================================================
    
    bool debayerBilinear(ImageBuffer& image);
    bool debayerVNG(ImageBuffer& image);
    bool debayerSuperpixel(ImageBuffer& image);
    
    /**
     * @brief Get pixel color from Bayer pattern
     * @param x X coordinate
     * @param y Y coordinate
     * @return 0=R, 1=G, 2=B
     */
    int getBayerColor(int x, int y) const;
    
    PreprocessParams m_params;
    MasterFrames m_masters;
    CalibrationStats m_stats;
    std::atomic<bool> m_cancelled{false};
    
    std::vector<QPoint> m_deviantPixels; ///< Detected from master dark
    double m_flatNormalization = 1.0; ///< Cached normalization factor for master flat
    
    void addHistory(ImageBuffer& image, const QString& message);
};

/**
 * @brief Worker thread for batch preprocessing
 */
class PreprocessingWorker : public QThread {
    Q_OBJECT
    
public:
    PreprocessingWorker(const PreprocessParams& params,
                        const QStringList& files,
                        const QString& outputDir,
                        QObject* parent = nullptr);
    
    void run() override;
    void requestCancel();
    
    const CalibrationStats& stats() const { return m_engine.lastStats(); }
    
signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void imageProcessed(const QString& path, bool success);
    void finished(bool success);
    
private:
    PreprocessingEngine m_engine;
    QStringList m_files;
    QString m_outputDir;
};

} // namespace Preprocessing

#endif // PREPROCESSING_H
