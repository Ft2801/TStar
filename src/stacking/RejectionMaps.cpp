/**
 * @file RejectionMaps.cpp
 * @brief Implementation of rejection map generation
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "RejectionMaps.h"
#include "../io/FitsWrapper.h"
#include <QDir>

namespace Stacking {

void RejectionMaps::initialize(int width, int height, int channels,
                               bool createLow, bool createHigh) {
    clear();
    
    m_width = width;
    m_height = height;
    m_channels = channels;
    
    if (createLow) {
        m_lowMap = std::make_unique<ImageBuffer>(width, height, channels);
        // Initialize to zero
        std::fill(m_lowMap->data().begin(), m_lowMap->data().end(), 0.0f);
    }
    
    if (createHigh) {
        m_highMap = std::make_unique<ImageBuffer>(width, height, channels);
        std::fill(m_highMap->data().begin(), m_highMap->data().end(), 0.0f);
    }
    
    m_totalLow = 0;
    m_totalHigh = 0;
    m_initialized = true;
}

void RejectionMaps::recordRejection(int x, int y, int channel, int rejectionType) {
    if (!m_initialized) return;
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    if (channel < 0 || channel >= m_channels) return;
    
    size_t idx = static_cast<size_t>(channel) * m_width * m_height +
                 static_cast<size_t>(y) * m_width + x;
    
    if (rejectionType < 0 && m_lowMap) {
        // Low rejection
        m_lowMap->data()[idx] += 1.0f;
        m_totalLow++;
    } else if (rejectionType > 0 && m_highMap) {
        // High rejection
        m_highMap->data()[idx] += 1.0f;
        m_totalHigh++;
    }
}

ImageBuffer* RejectionMaps::getLowMap() {
    return m_lowMap.get();
}

ImageBuffer* RejectionMaps::getHighMap() {
    return m_highMap.get();
}

ImageBuffer RejectionMaps::getCombinedMap() const {
    if (!m_initialized) {
        return ImageBuffer();
    }
    
    // Create 3-channel map: Red=Low, Green=0, Blue=High
    ImageBuffer combined(m_width, m_height, 3);
    size_t layerSize = static_cast<size_t>(m_width) * m_height;
    
    float* data = combined.data().data();
    
    for (size_t i = 0; i < layerSize; ++i) {
        // Red channel: low rejections
        data[i] = m_lowMap ? m_lowMap->data()[i] : 0.0f;
        
        // Green channel: overlap (where both)
        float low = m_lowMap ? m_lowMap->data()[i] : 0.0f;
        float high = m_highMap ? m_highMap->data()[i] : 0.0f;
        data[layerSize + i] = std::min(low, high);
        
        // Blue channel: high rejections
        data[2 * layerSize + i] = m_highMap ? m_highMap->data()[i] : 0.0f;
    }
    
    // Normalize to [0, 1]
    float maxVal = 0.0f;
    for (size_t i = 0; i < 3 * layerSize; ++i) {
        if (data[i] > maxVal) maxVal = data[i];
    }
    
    if (maxVal > 0.0f) {
        float invMax = 1.0f / maxVal;
        for (size_t i = 0; i < 3 * layerSize; ++i) {
            data[i] *= invMax;
        }
    }
    
    return combined;
}

bool RejectionMaps::save(const QString& basePath, const QString& prefix) const {
    if (!m_initialized) {
        return false;
    }
    
    QDir dir(basePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    bool success = true;
    
    if (m_lowMap) {
        QString lowPath = dir.absoluteFilePath(prefix + "_rejmap_low.fit");
        if (!FitsIO::write(lowPath, *m_lowMap, 32)) {
            success = false;
        }
    }
    
    if (m_highMap) {
        QString highPath = dir.absoluteFilePath(prefix + "_rejmap_high.fit");
        if (!FitsIO::write(highPath, *m_highMap, 32)) {
            success = false;
        }
    }
    
    // Also save combined map
    ImageBuffer combined = getCombinedMap();
    if (combined.isValid()) {
        QString combinedPath = dir.absoluteFilePath(prefix + "_rejmap_combined.fit");
        if (!FitsIO::write(combinedPath, combined, 32)) {
            success = false;
        }
    }
    
    return success;
}

void RejectionMaps::clear() {
    m_lowMap.reset();
    m_highMap.reset();
    m_width = 0;
    m_height = 0;
    m_channels = 0;
    m_totalLow = 0;
    m_totalHigh = 0;
    m_initialized = false;
}

} // namespace Stacking
