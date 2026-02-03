#include "ImageBufferDelta.h"
#include "ImageBuffer.h"
#include <algorithm>
#include <numeric>
#include <cmath>

// ============================================================================
// ImageBufferDelta Implementation
// ============================================================================

size_t ImageBufferDelta::estimatedSize() const {
    size_t size = sizeof(ImageBufferDelta);
    size += pixelDelta.capacity();
    size += metadataDelta.capacity();
    if (fullPixelData) {
        size += fullPixelData->capacity() * sizeof(float);
    }
    return size;
}

ImageBufferDelta ImageBufferDelta::createMetadataChange() {
    ImageBufferDelta delta;
    delta.type = CHANGE_METADATA_SMALL;
    delta.width = 0;
    delta.height = 0;
    delta.channels = 0;
    return delta;
}

ImageBufferDelta ImageBufferDelta::createPixelChange(
    int w, int h, int c,
    const std::vector<uint8_t>& delta) {
    ImageBufferDelta change;
    change.type = CHANGE_PIXEL_DATA;
    change.width = w;
    change.height = h;
    change.channels = c;
    change.pixelDelta = delta;
    return change;
}

ImageBufferDelta ImageBufferDelta::createFullCopy(
    int w, int h, int c,
    const std::vector<float>& pixelData) {
    ImageBufferDelta copy;
    copy.type = CHANGE_FULL_COPY;
    copy.width = w;
    copy.height = h;
    copy.channels = c;
    copy.fullPixelData = std::make_shared<std::vector<float>>(pixelData);
    return copy;
}

// ============================================================================
// ImageHistoryManager Implementation
// ============================================================================

ImageHistoryManager::ImageHistoryManager()
    : m_snapshotCounter(0), m_baseline(nullptr) {
}

ImageHistoryManager::~ImageHistoryManager() {
    clear();
}

void ImageHistoryManager::pushUndo(const ImageBuffer& buffer) {
    // Take a full snapshot every N operations to enable faster reconstruction
    bool shouldSnapshot = (m_snapshotCounter % SNAPSHOT_INTERVAL) == 0;
    
    ImageBufferDelta delta;
    if (shouldSnapshot && m_baseline) {
        // Store full copy as baseline for compression reference
        delta = ImageBufferDelta::createFullCopy(
            buffer.width(),
            buffer.height(),
            buffer.channels(),
            buffer.data()
        );
        m_baseline = std::make_shared<ImageBuffer>(buffer);
    } else if (m_baseline) {
        // Compute delta from baseline
        // Strategy: Only compute delta if dimensions and channel count match.
        // If they differ, a full new baseline is required.
        if (m_baseline->width() == buffer.width() &&
            m_baseline->height() == buffer.height() &&
            m_baseline->channels() == buffer.channels()) {
            
            size_t pixelCount = buffer.width() * buffer.height() * buffer.channels();
            const float* baseline_data = m_baseline->data().data();
            const float* current_data = buffer.data().data();
            
            // Compute run-length encoded delta
            std::vector<uint8_t> deltaBytes;
            deltaBytes.reserve(pixelCount / 10);  // Heuristic reservation (10% change)
            
            // Simplified delta: store as index + value pairs for changed pixels
            for (size_t i = 0; i < pixelCount; ++i) {
                if (baseline_data[i] != current_data[i]) {
                    // Encode: 4 bytes index + 4 bytes float value
                    uint32_t idx = i;
                    memcpy(deltaBytes.data() + deltaBytes.size(), &idx, 4);
                    memcpy(deltaBytes.data() + deltaBytes.size() + 4, &current_data[i], 4);
                }
            }
            
            // If delta is too large, use full copy instead
            if (deltaBytes.size() < DELTA_SIZE_THRESHOLD) {
                delta = ImageBufferDelta::createPixelChange(
                    buffer.width(),
                    buffer.height(),
                    buffer.channels(),
                    deltaBytes
                );
            } else {
                delta = ImageBufferDelta::createFullCopy(
                    buffer.width(),
                    buffer.height(),
                    buffer.channels(),
                    buffer.data()
                );
                m_baseline = std::make_shared<ImageBuffer>(buffer);
            }
        } else {
            // Size mismatch: force full copy
            delta = ImageBufferDelta::createFullCopy(
                buffer.width(),
                buffer.height(),
                buffer.channels(),
                buffer.data()
            );
            m_baseline = std::make_shared<ImageBuffer>(buffer);
        }
    } else {
        // First operation: always full copy
        delta = ImageBufferDelta::createFullCopy(
            buffer.width(),
            buffer.height(),
            buffer.channels(),
            buffer.data()
        );
        m_baseline = std::make_shared<ImageBuffer>(buffer);
    }
    
    // Clear redo stack on new operation
    m_redoStack.clear();
    
    // Add to undo stack
    m_undoStack.push_back(std::move(delta));
    m_snapshotCounter++;
    
    // Check if we exceeded memory limit
    pruneHistory(MAX_HISTORY_SIZE);
}

void ImageHistoryManager::performUndo() {
    if (m_undoStack.empty()) return;
    
    // Move current state to redo stack
    // Note: In actual use, caller provides current buffer
    // This is a simplified version - in practice, ImageViewer would manage this
    
    m_redoStack.push_back(m_undoStack.back());
    m_undoStack.pop_back();
}

void ImageHistoryManager::performRedo() {
    if (m_redoStack.empty()) return;
    
    m_undoStack.push_back(m_redoStack.back());
    m_redoStack.pop_back();
}

double ImageHistoryManager::getHistorySizeMB() const {
    size_t totalSize = 0;
    
    for (const auto& delta : m_undoStack) {
        totalSize += delta.estimatedSize();
    }
    for (const auto& delta : m_redoStack) {
        totalSize += delta.estimatedSize();
    }
    
    return totalSize / (1024.0 * 1024.0);
}

void ImageHistoryManager::pruneHistory(size_t maxSizeBytes) {
    while (getHistorySizeMB() * 1024 * 1024 > maxSizeBytes && !m_undoStack.empty()) {
        // Remove oldest (first) undo state
        m_undoStack.erase(m_undoStack.begin());
    }
    
    while (getHistorySizeMB() * 1024 * 1024 > maxSizeBytes && !m_redoStack.empty()) {
        // Remove oldest (first) redo state
        m_redoStack.erase(m_redoStack.begin());
    }
}
