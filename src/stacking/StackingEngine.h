#ifndef STACKING_ENGINE_H
#define STACKING_ENGINE_H

/**
 * @file StackingEngine.h
 * @brief Core stacking engine and supporting types.
 *
 * The StackingEngine orchestrates the complete stacking pipeline:
 *   1. Image filtering and selection
 *   2. Normalization coefficient computation
 *   3. Optional comet-alignment registration adjustment
 *   4. Stacking method execution (Sum, Mean, Median, Min, Max, Drizzle)
 *   5. Pixel rejection
 *   6. Post-processing (output normalisation, RGB equalisation)
 *   7. FITS metadata / WCS update
 *
 * StackingWorker wraps the engine in a QThread for non-blocking operation.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

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

// ============================================================================
// StackingArgs
// ============================================================================

/**
 * @brief Complete set of inputs, runtime state, and outputs for a stacking run.
 */
struct StackingArgs {
    StackingParams  params;     ///< User-configured stacking parameters.
    ImageSequence*  sequence;   ///< Pointer to the image sequence.

    // ---- Runtime data ------------------------------------------------------
    std::vector<int>    imageIndices;    ///< Indices of images passing the filter.
    int                 nbImagesToStack = 0; ///< Count of images to stack.
    NormCoefficients    coefficients;    ///< Per-channel normalization coefficients.
    std::vector<double> weights;        ///< Per-image quality weights.

    // ---- Rejection output --------------------------------------------------
    RejectionMaps  rejectionMaps;       ///< Optional low/high rejection maps.
    RejectionStats rejectionStats;      ///< Cumulative rejection statistics.

    // ---- Drizzle -----------------------------------------------------------
    ImageBuffer drizzleCounts;          ///< Per-pixel contribution counts (drizzle).

    // ---- Comet / modified registration -------------------------------------
    std::vector<RegistrationData> effectiveRegs; ///< Adjusted registrations for comet mode.

    // ---- Cosmetic correction -----------------------------------------------
    CosmeticMap cosmeticMap;            ///< Bad-pixel map for cosmetic correction.

    // ---- Output ------------------------------------------------------------
    ImageBuffer result;                 ///< Final stacked image.
    int         returnValue = 0;        ///< Numeric result code.

    // ---- Callbacks ---------------------------------------------------------
    ProgressCallback progressCallback;
    CancelCheck      cancelCheck;
    LogCallback      logCallback;

    /** @brief Emit a log message through the configured callback. */
    void log(const QString& msg, const QString& color = QString()) {
        if (logCallback) logCallback(msg, color);
    }

    /** @brief Report progress through the configured callback. */
    void progress(const QString& msg, double pct) {
        if (progressCallback) progressCallback(msg, pct);
    }

    /** @brief Query the cancellation callback. */
    bool isCancelled() const {
        return cancelCheck && cancelCheck();
    }
};

// ============================================================================
// StackingEngine
// ============================================================================

/**
 * @brief Main stacking engine.
 *
 * Thread-safe for a single concurrent execute() call.  Multiple instances
 * may run in parallel on different sequences.
 */
class StackingEngine : public QObject {
    Q_OBJECT

public:
    explicit StackingEngine(QObject* parent = nullptr);
    ~StackingEngine() override;

    /**
     * @brief Execute a complete stacking operation.
     *
     * This is the primary entry point.  Can be called from a worker thread
     * for non-blocking operation.
     *
     * @param[in,out] args Stacking arguments (populated with results on return).
     * @return StackResult status code.
     */
    StackResult execute(StackingArgs& args);

    // ---- Configuration helpers for calibration masters ---------------------

    /**
     * @brief Configure arguments for master bias generation.
     * Sets: Mean method, Winsorized rejection, no normalization, no weighting.
     */
    static void configureForMasterBias(StackingArgs& args);

    /**
     * @brief Configure arguments for master dark generation.
     * Sets: Mean method, Winsorized rejection (hot pixel), no normalization.
     */
    static void configureForMasterDark(StackingArgs& args);

    /**
     * @brief Configure arguments for master flat generation.
     * Sets: Mean method, Winsorized rejection, multiplicative normalization.
     */
    static void configureForMasterFlat(StackingArgs& args);

    /** @brief Request cancellation of the current operation. */
    void requestCancel();

    /** @brief Query whether cancellation has been requested. */
    bool isCancelled() const { return m_cancelled.load(); }

signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void finished(bool success);

private:
    // ---- Stacking method implementations -----------------------------------

    /** @brief Sum stacking (normalised by maximum value). */
    StackResult stackSum(StackingArgs& args);

    /** @brief Mean stacking with optional pixel rejection. */
    StackResult stackMean(StackingArgs& args);

    /** @brief Accelerated C-based mean stacking (stub). */
    StackResult tryStackMeanC(StackingArgs& args, int width, int height, int channels);

    /** @brief Median stacking. */
    StackResult stackMedian(StackingArgs& args);

    /** @brief Maximum-value stacking. */
    StackResult stackMax(StackingArgs& args);

    /** @brief Minimum-value stacking. */
    StackResult stackMin(StackingArgs& args);

    /** @brief Mosaic stacking assembly. */
    StackResult stackMosaic(StackingArgs& args);

    /** @brief Drizzle integration. */
    StackResult stackDrizzle(StackingArgs& args);

    // ---- Pipeline helpers --------------------------------------------------

    /** @brief Apply quality / selection filters to the image sequence. */
    bool filterImages(StackingArgs& args);

    /** @brief Compute per-image normalization coefficients. */
    bool computeNormalization(StackingArgs& args);

    /** @brief Allocate and zero-initialise the output ImageBuffer. */
    bool prepareOutput(StackingArgs& args, int width, int height, int channels);

    /**
     * @brief Compute the output image dimensions and framing offsets.
     *
     * Accounts for registration shifts and the maximize-framing option.
     */
    void computeOutputDimensions(const StackingArgs& args,
                                 int& width, int& height,
                                 int& offsetX, int& offsetY);

    /**
     * @brief Sample a pixel from a registered source image.
     *
     * Handles both shift-only and full-homography registration, including
     * optional SIP distortion correction.
     *
     * @param[in]  buffer    Source image buffer.
     * @param[in]  x, y      Output pixel coordinates.
     * @param[in]  channel   Colour channel index.
     * @param[in]  reg       Registration data for this image.
     * @param[in]  offsetX, offsetY  Framing offsets.
     * @param[out] outValue  Interpolated pixel value.
     * @param[in]  srcOffsetX, srcOffsetY  Source sub-region offsets.
     * @return true if the source pixel is within bounds.
     */
    bool getShiftedPixel(const ImageBuffer& buffer,
                         int x, int y, int channel,
                         const RegistrationData& reg,
                         int offsetX, int offsetY,
                         float& outValue,
                         int srcOffsetX = 0, int srcOffsetY = 0);

    /**
     * @brief Bicubic interpolation with edge clamping.
     * @return Interpolated value, or -1.0f if out of bounds.
     */
    float getInterpolatedPixel(const ImageBuffer& buffer,
                               double x, double y, int channel);

    /** @brief Evaluate a cubic Hermite spline at parameter t. */
    inline float cubicHermite(float A, float B, float C, float D, float t) const {
        const float a = -A * 0.5f + 1.5f * B - 1.5f * C + D * 0.5f;
        const float b =  A       - 2.5f * B + 2.0f * C - D * 0.5f;
        const float c = -A * 0.5f            + C * 0.5f;
        const float d =  B;
        return a * t * t * t + b * t * t + c * t + d;
    }

    /** @brief Prepare per-frame registration transforms for comet-aligned stacking. */
    void prepareCometAlignment(StackingArgs& args);

    /** @brief Attempt to auto-detect the Bayer pattern from the reference image. */
    void autoDetectBayerPattern(StackingArgs& args);

    /** @brief Load and analyze the cosmetic correction master dark. */
    void prepareCosmeticCorrection(StackingArgs& args);

    /** @brief Route to the appropriate stacking implementation. */
    StackResult dispatchStacking(StackingArgs& args);

    /**
     * @brief Load pixel data for all images in a block (single channel).
     *
     * Performs sequential I/O followed by parallel bicubic interpolation.
     */
    void loadBlockData(const StackingArgs& args,
                       int startRow, int endRow,
                       int outputWidth, int channel,
                       int offsetX, int offsetY,
                       std::vector<float>& blockData);

    /** @brief Heuristic for choosing the block height given available memory. */
    int computeOptimalBlockSize(const StackingArgs& args,
                                int outputWidth, int channels);

    /** @brief Update FITS header metadata on the stacked result. */
    void updateMetadata(StackingArgs& args, int offsetX = 0, int offsetY = 0);

    /** @brief Build a human-readable stacking summary string. */
    QString generateSummary(const StackingArgs& args);

    // ---- State -------------------------------------------------------------
    std::atomic<bool> m_cancelled{false};
};

// ============================================================================
// StackingWorker
// ============================================================================

/**
 * @brief QThread wrapper for executing a stacking operation asynchronously.
 */
class StackingWorker : public QThread {
    Q_OBJECT

public:
    explicit StackingWorker(StackingArgs args, QObject* parent = nullptr);

    void run() override;
    void requestCancel() { m_engine.requestCancel(); }

    const StackingArgs& args() const { return m_args; }
    StackingArgs&       args()       { return m_args; }

signals:
    void progressChanged(const QString& message, double progress);
    void logMessage(const QString& message, const QString& color);
    void finished(bool success);

private:
    StackingArgs   m_args;
    StackingEngine m_engine;
};

} // namespace Stacking

#endif // STACKING_ENGINE_H
