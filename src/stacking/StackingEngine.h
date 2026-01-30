
#ifndef STACKING_ENGINE_H
#define STACKING_ENGINE_H

#include "StackingTypes.h"
#include "StackingSequence.h"
#include "Normalization.h"
#include "RejectionAlgorithms.h"
#include "Statistics.h"
#include "Weighting.h"
#include "ImageCache.h"
#include "Blending.h"
#include "RejectionMaps.h"
#include "CosmeticCorrection.h"
#include "DrizzleStacking.h"
#include "../ImageBuffer.h"

#include <QObject>
#include <QThread>
#include <memory>
#include <atomic>

namespace Stacking {

/**
 * @brief Stacking operation arguments
 * 
 * Contains all data needed for a complete stacking operation.
 */
struct StackingArgs {
    StackingParams params;          ///< Configuration parameters
    ImageSequence* sequence;        ///< Pointer to image sequence
    
    // Runtime data
    std::vector<int> imageIndices;  ///< Indices of filtered images
    int nbImagesToStack = 0;        ///< Number of images to stack
    NormCoefficients coefficients;  ///< Normalization coefficients
    std::vector<double> weights;    ///< Image weights
    
    // Rejection maps (optional output)
    RejectionMaps rejectionMaps;
    RejectionStats rejectionStats;
    
    // Drizzle-specific
    ImageBuffer drizzleCounts;
    
    // Comet/Modified Registration
    std::vector<RegistrationData> effectiveRegs;

    // Cosmetic Correction
    CosmeticMap cosmeticMap;

    // Output
    ImageBuffer result;             ///< Stacked result
    int returnValue = 0;            ///< Result code
    
    // Progress
    ProgressCallback progressCallback;
    CancelCheck cancelCheck;
    LogCallback logCallback;
    
    /**
     * @brief Log a message
     */
    void log(const QString& msg, const QString& color = QString()) {
        if (logCallback) {
            logCallback(msg, color);
        }
    }
    
    /**
     * @brief Update progress
     */
    void progress(const QString& msg, double pct) {
        if (progressCallback) {
            progressCallback(msg, pct);
        }
    }
    
    /**
     * @brief Check if operation was cancelled
     */
    bool isCancelled() const {
        return cancelCheck && cancelCheck();
    }
};

/**
 * @brief Main stacking engine
 * 
 * Orchestrates the complete stacking pipeline including:
 * - Image filtering and selection
 * - Normalization coefficient computation
 * - Stacking method execution
 * - Post-processing and output generation
 */
class StackingEngine : public QObject {
    Q_OBJECT
    
public:
    explicit StackingEngine(QObject* parent = nullptr);
    ~StackingEngine() override;
    
    /**
     * @brief Execute stacking operation
     * 
     * Main entry point for stacking. This can be called from
     * a worker thread for non-blocking operation.
     * 
     * @param args Stacking arguments (modified with results)
     * @return StackResult code
     */
    /**
     * @brief Execute stacking operation
     * 
     * Main entry point for stacking. This can be called from
     * a worker thread for non-blocking operation.
     * 
     * @param args Stacking arguments (modified with results)
     * @return StackResult code
     */
    StackResult execute(StackingArgs& args);
    
    //=========================================================================
    // CONFIGURATION HELPERS
    //=========================================================================

    /**
     * @brief Configure arguments for Master Bias generation
     * Enforces: No Normalization, Winsorized Rejection, No Weighting
     */
    static void configureForMasterBias(StackingArgs& args);

    /**
     * @brief Configure arguments for Master Dark generation
     * Enforces: No Normalization, Winsorized Rejection (Hot Pixel detection), No Weighting
     */
    static void configureForMasterDark(StackingArgs& args);

    /**
     * @brief Configure arguments for Master Flat generation
     * Enforces: Multiplicative Normalization, Winsorized Rejection, No Weighting
     */
    static void configureForMasterFlat(StackingArgs& args);

    /**
     * @brief Request cancellation of current operation
     */
    void requestCancel();
    
    /**
     * @brief Check if cancellation was requested
     */
    bool isCancelled() const { return m_cancelled.load(); }
    
signals:
    /**
     * @brief Emitted when progress changes
     */
    void progressChanged(const QString& message, double progress);
    
    /**
     * @brief Emitted for log messages
     */
    void logMessage(const QString& message, const QString& color);
    
    /**
     * @brief Emitted when stacking is complete
     */
    void finished(bool success);
    
private:
    //=========================================================================
    // STACKING METHODS
    //=========================================================================
    
    /**
     * @brief Sum stacking
     * 
     * Sums all pixels and normalizes by max value.
     */
    StackResult stackSum(StackingArgs& args);
    
    /**
     * @brief Mean stacking with rejection
     * 
     * Averages pixels after applying rejection algorithm.
     */
    StackResult stackMean(StackingArgs& args);
    
    /**
     * @brief Accelerated C-based mean stacking (no rejection)
     * 
     * Uses optimized C implementation for simple mean stacking.
     */
    StackResult tryStackMeanC(StackingArgs& args, int width, int height, int channels);
    
    /**
     * @brief Median stacking
     * 
     * Takes median value for each pixel.
     */
    StackResult stackMedian(StackingArgs& args);
    
    /**
     * @brief Maximum stacking
     * 
     * Takes maximum value for each pixel.
     */
    StackResult stackMax(StackingArgs& args);
    
    /**
     * @brief Minimum stacking
     * 
     * Takes minimum value for each pixel.
     */
    StackResult stackMin(StackingArgs& args);
    
    /**
     * @brief Drizzle Stacking
     * 
     * Performs drizzle integration using input images and rejection maps.
     */
    StackResult stackDrizzle(StackingArgs& args);
    
    //=========================================================================
    // HELPER METHODS
    //=========================================================================
    
    /**
     * @brief Filter images based on criteria
     */
    bool filterImages(StackingArgs& args);
    
    /**
     * @brief Compute normalization coefficients
     */
    bool computeNormalization(StackingArgs& args);
    
    /**
     * @brief Prepare output image buffer
     */
    bool prepareOutput(StackingArgs& args, int width, int height, int channels);
    
    /**
     * @brief Compute output dimensions
     * 
     * Accounts for registration shifts and maximize framing option.
     */
    void computeOutputDimensions(const StackingArgs& args, 
                                  int& width, int& height,
                                  int& offsetX, int& offsetY);

    /**
     * @brief Get pixel value from image with registration shift
     * @param x Output X coordinate
     * @param y Output Y coordinate
     * @param channel Color channel
     * @param shiftX Registration X shift
     * @param shiftY Registration Y shift
     * @param outValue Output pixel value
     * @return true if pixel is within source image bounds
     */
    bool getShiftedPixel(const ImageBuffer& buffer,
                         int x, int y, int channel,
                         const RegistrationData& reg,
                         int offsetX, int offsetY,
                         float& outValue,
                         int srcOffsetX = 0, int srcOffsetY = 0);
    
    /**
     * @brief Get pixel value using bicubic interpolation
     */
    float getInterpolatedPixel(const ImageBuffer& buffer, 
                              double x, double y, int channel);
    
    /**
     * @brief Cubic hermite spline
     */
    inline float cubicHermite(float A, float B, float C, float D, float t) const {
        float a = -A / 2.0f + (3.0f * B) / 2.0f - (3.0f * C) / 2.0f + D / 2.0f;
        float b = A - (5.0f * B) / 2.0f + 2.0f * C - D / 2.0f;
        float c = -A / 2.0f + C / 2.0f;
        float d = B;
        return a * t * t * t + b * t * t + c * t + d;
    }
    
    /**
     * @brief Process a block of rows for mean stacking
     * 
     * Reads data from all images for a range of rows,
     * applies normalization and rejection, then computes mean.
     */
    StackResult processMeanBlock(StackingArgs& args,
                                 int startRow, int endRow,
                                 int outputWidth, int channels,
                                 int offsetX, int offsetY,
                                 float* outputData, // Flattened [row][col][channel]
                                 ImageCache* cache = nullptr);

    /**
     * @brief Load block data for all images for a specific channel
     */
    void loadBlockData(const StackingArgs& args,
                       int startRow, int endRow,
                       int outputWidth, int channel,
                       int offsetX, int offsetY,
                       std::vector<float>& blockData);
    
    /**
     * @brief Compute optimal block height based on available memory
     */
    int computeOptimalBlockSize(const StackingArgs& args, int outputWidth, int channels);
    
    /**
     * @brief Update FITS header with stacking info
     */
    void updateMetadata(StackingArgs& args);
    
    /**
     * @brief Generate stacking summary message
     */
    QString generateSummary(const StackingArgs& args);
    
    std::atomic<bool> m_cancelled{false};
};

/**
 * @brief Worker thread for stacking operations
 */
class StackingWorker : public QThread {
    Q_OBJECT
    
public:
    explicit StackingWorker(StackingArgs args, QObject* parent = nullptr);
    
    void run() override;
    void requestCancel() { m_engine.requestCancel(); }
    
    const StackingArgs& args() const { return m_args; }
    StackingArgs& args() { return m_args; }
    
signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void finished(bool success);
    
private:
    StackingArgs m_args;
    StackingEngine m_engine;
};

} // namespace Stacking

#endif // STACKING_ENGINE_H
