#ifndef REJECTION_MAPS_H
#define REJECTION_MAPS_H

/**
 * @file RejectionMaps.h
 * @brief Rejection map generation and management for image stacking.
 *
 * During stacking with pixel rejection, this class accumulates per-pixel
 * rejection counts into image buffers (low and high maps) that can later
 * be saved as FITS files for diagnostic purposes.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "../ImageBuffer.h"
#include "StackingTypes.h"

#include <memory>
#include <vector>

namespace Stacking {

// ============================================================================
// Rejection Map Type
// ============================================================================

/**
 * @brief Selects which rejection map(s) to retrieve or generate.
 */
enum class RejectionMapType {
    Low,      ///< Low (dark-side) rejections only.
    High,     ///< High (bright-side) rejections only.
    Combined  ///< Both low and high encoded in a single RGB map.
};

// ============================================================================
// RejectionMaps
// ============================================================================

/**
 * @brief Accumulates and stores per-pixel rejection counts.
 *
 * Each call to recordRejection() increments the appropriate map element.
 * The combined map encodes low counts in the red channel and high counts
 * in the blue channel of a 3-channel image, normalised to [0, 1].
 */
class RejectionMaps {
public:
    /**
     * @brief Allocate and zero-initialise the rejection map buffers.
     *
     * @param width      Image width in pixels.
     * @param height     Image height in pixels.
     * @param channels   Number of image channels.
     * @param createLow  Whether to create the low-rejection map.
     * @param createHigh Whether to create the high-rejection map.
     */
    void initialize(int width, int height, int channels,
                    bool createLow = true, bool createHigh = true);

    /**
     * @brief Record a single pixel rejection event.
     *
     * @param x             X coordinate.
     * @param y             Y coordinate.
     * @param channel       Channel index.
     * @param rejectionType -1 for low rejection, +1 for high rejection.
     */
    void recordRejection(int x, int y, int channel, int rejectionType);

    /** @brief Access the low-rejection map buffer (may be null). */
    ImageBuffer* getLowMap();

    /** @brief Access the high-rejection map buffer (may be null). */
    ImageBuffer* getHighMap();

    /**
     * @brief Build a combined 3-channel map.
     *
     * Red = low rejection count, Blue = high rejection count,
     * Green = overlap (min of low and high). Normalised to [0, 1].
     *
     * @return New ImageBuffer containing the combined map.
     */
    ImageBuffer getCombinedMap() const;

    /** @brief Check whether the maps have been initialised. */
    bool isInitialized() const { return m_initialized; }

    /** @brief Cumulative low-rejection count across all pixels. */
    int64_t totalLowRejections()  const { return m_totalLow; }

    /** @brief Cumulative high-rejection count across all pixels. */
    int64_t totalHighRejections() const { return m_totalHigh; }

    /**
     * @brief Save the rejection maps as FITS files.
     *
     * Writes low, high, and combined maps to separate FITS files
     * using the given base path and filename prefix.
     *
     * @param basePath Directory in which to write the files.
     * @param prefix   Filename prefix (e.g. "stack").
     * @return true if all writes succeeded.
     */
    bool save(const QString& basePath, const QString& prefix) const;

    /** @brief Release all map buffers and reset state. */
    void clear();

private:
    std::unique_ptr<ImageBuffer> m_lowMap;
    std::unique_ptr<ImageBuffer> m_highMap;
    int     m_width       = 0;
    int     m_height      = 0;
    int     m_channels    = 0;
    int64_t m_totalLow    = 0;
    int64_t m_totalHigh   = 0;
    bool    m_initialized = false;
};

} // namespace Stacking

#endif // REJECTION_MAPS_H