/**
 * @file RejectionMaps.cpp
 * @brief Implementation of rejection map generation and persistence.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "RejectionMaps.h"
#include "../io/FitsWrapper.h"

#include <QDir>
#include <algorithm>

namespace Stacking {

// ============================================================================
// Initialisation and State Management
// ============================================================================

void RejectionMaps::initialize(int width, int height, int channels,
                               bool createLow, bool createHigh)
{
    clear();

    m_width    = width;
    m_height   = height;
    m_channels = channels;

    if (createLow) {
        m_lowMap = std::make_unique<ImageBuffer>(width, height, channels);
        std::fill(m_lowMap->data().begin(), m_lowMap->data().end(), 0.0f);
    }

    if (createHigh) {
        m_highMap = std::make_unique<ImageBuffer>(width, height, channels);
        std::fill(m_highMap->data().begin(), m_highMap->data().end(), 0.0f);
    }

    m_totalLow    = 0;
    m_totalHigh   = 0;
    m_initialized = true;
}

void RejectionMaps::clear()
{
    m_lowMap.reset();
    m_highMap.reset();
    m_width       = 0;
    m_height      = 0;
    m_channels    = 0;
    m_totalLow    = 0;
    m_totalHigh   = 0;
    m_initialized = false;
}

// ============================================================================
// Recording
// ============================================================================

void RejectionMaps::recordRejection(int x, int y, int channel, int rejectionType)
{
    if (!m_initialized) return;
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    if (channel < 0 || channel >= m_channels) return;

    const size_t idx = static_cast<size_t>(channel) * m_width * m_height
                     + static_cast<size_t>(y) * m_width + x;

    if (rejectionType < 0 && m_lowMap) {
        m_lowMap->data()[idx] += 1.0f;
        m_totalLow++;
    } else if (rejectionType > 0 && m_highMap) {
        m_highMap->data()[idx] += 1.0f;
        m_totalHigh++;
    }
}

// ============================================================================
// Map Access
// ============================================================================

ImageBuffer* RejectionMaps::getLowMap()
{
    return m_lowMap.get();
}

ImageBuffer* RejectionMaps::getHighMap()
{
    return m_highMap.get();
}

ImageBuffer RejectionMaps::getCombinedMap() const
{
    if (!m_initialized)
        return ImageBuffer();

    // Build a 3-channel diagnostic image:
    //   Red   = low rejection count
    //   Green = overlap (minimum of low and high)
    //   Blue  = high rejection count
    ImageBuffer combined(m_width, m_height, 3);
    const size_t layerSize = static_cast<size_t>(m_width) * m_height;

    float* data = combined.data().data();

    for (size_t i = 0; i < layerSize; ++i) {
        const float low  = m_lowMap  ? m_lowMap->data()[i]  : 0.0f;
        const float high = m_highMap ? m_highMap->data()[i] : 0.0f;

        data[i]                    = low;
        data[layerSize + i]        = std::min(low, high);
        data[2 * layerSize + i]    = high;
    }

    // Normalise the entire map to [0, 1].
    float maxVal = 0.0f;
    for (size_t i = 0; i < 3 * layerSize; ++i) {
        if (data[i] > maxVal)
            maxVal = data[i];
    }

    if (maxVal > 0.0f) {
        const float invMax = 1.0f / maxVal;
        for (size_t i = 0; i < 3 * layerSize; ++i)
            data[i] *= invMax;
    }

    return combined;
}

// ============================================================================
// Persistence
// ============================================================================

bool RejectionMaps::save(const QString& basePath, const QString& prefix) const
{
    if (!m_initialized)
        return false;

    QDir dir(basePath);
    if (!dir.exists())
        dir.mkpath(".");

    bool success = true;

    // Save individual low/high maps.
    if (m_lowMap) {
        const QString lowPath = dir.absoluteFilePath(prefix + "_rejmap_low.fit");
        if (!FitsIO::write(lowPath, *m_lowMap, 32))
            success = false;
    }

    if (m_highMap) {
        const QString highPath = dir.absoluteFilePath(prefix + "_rejmap_high.fit");
        if (!FitsIO::write(highPath, *m_highMap, 32))
            success = false;
    }

    // Save the combined diagnostic map.
    ImageBuffer combined = getCombinedMap();
    if (combined.isValid()) {
        const QString combinedPath = dir.absoluteFilePath(prefix + "_rejmap_combined.fit");
        if (!FitsIO::write(combinedPath, combined, 32))
            success = false;
    }

    return success;
}

} // namespace Stacking