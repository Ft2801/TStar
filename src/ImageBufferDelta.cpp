// =============================================================================
// ImageBufferDelta.cpp
//
// Implementation of the delta-based undo/redo system for ImageBuffer.
// See ImageBufferDelta.h for design rationale and class documentation.
// =============================================================================

#include "ImageBufferDelta.h"
#include "ImageBuffer.h"

#include <algorithm>
#include <numeric>
#include <cmath>

// =============================================================================
// ImageBufferDelta -- factory methods and size estimation
// =============================================================================

size_t ImageBufferDelta::estimatedSize() const
{
    size_t bytes = sizeof(ImageBufferDelta);
    bytes += pixelDelta.capacity();
    bytes += metadataDelta.capacity();
    if (fullPixelData) {
        bytes += fullPixelData->capacity() * sizeof(float);
    }
    return bytes;
}

ImageBufferDelta ImageBufferDelta::createMetadataChange()
{
    ImageBufferDelta delta;
    delta.type     = CHANGE_METADATA_SMALL;
    delta.width    = 0;
    delta.height   = 0;
    delta.channels = 0;
    return delta;
}

ImageBufferDelta ImageBufferDelta::createPixelChange(
    int w, int h, int c,
    const std::vector<uint8_t>& delta)
{
    ImageBufferDelta change;
    change.type       = CHANGE_PIXEL_DATA;
    change.width      = w;
    change.height     = h;
    change.channels   = c;
    change.pixelDelta = delta;
    return change;
}

ImageBufferDelta ImageBufferDelta::createFullCopy(
    int w, int h, int c,
    const std::vector<float>& pixelData)
{
    ImageBufferDelta copy;
    copy.type          = CHANGE_FULL_COPY;
    copy.width         = w;
    copy.height        = h;
    copy.channels      = c;
    copy.fullPixelData = std::make_shared<std::vector<float>>(pixelData);
    return copy;
}

// =============================================================================
// ImageHistoryManager -- undo/redo stack management
// =============================================================================

ImageHistoryManager::ImageHistoryManager()
    : m_snapshotCounter(0)
    , m_baseline(nullptr)
{
}

ImageHistoryManager::~ImageHistoryManager()
{
    clear();
}

// -----------------------------------------------------------------------------
// pushUndo -- record a new state before a destructive operation
// -----------------------------------------------------------------------------

void ImageHistoryManager::pushUndo(const ImageBuffer& buffer,
                                   const QString& description)
{
    // Decide whether to store a full snapshot or a delta
    const bool shouldSnapshot =
        (m_snapshotCounter % SNAPSHOT_INTERVAL) == 0;

    ImageBufferDelta delta;

    if (shouldSnapshot && m_baseline) {
        // Periodic full snapshot to bound reconstruction cost
        delta = ImageBufferDelta::createFullCopy(
            buffer.width(), buffer.height(), buffer.channels(),
            buffer.data()
        );
        m_baseline = std::make_shared<ImageBuffer>(buffer);

    } else if (m_baseline) {
        // Attempt delta encoding against the baseline
        const bool dimensionsMatch =
            m_baseline->width()    == buffer.width()  &&
            m_baseline->height()   == buffer.height() &&
            m_baseline->channels() == buffer.channels();

        if (dimensionsMatch) {
            const size_t pixelCount =
                static_cast<size_t>(buffer.width()) * buffer.height() * buffer.channels();
            const float* basePtr    = m_baseline->data().data();
            const float* currentPtr = buffer.data().data();

            // Build a sparse delta: (index, value) pairs for changed pixels
            std::vector<uint8_t> deltaBytes;
            deltaBytes.reserve(pixelCount / 10);  // heuristic: ~10 % change

            for (size_t i = 0; i < pixelCount; ++i) {
                if (basePtr[i] != currentPtr[i]) {
                    const uint32_t idx = static_cast<uint32_t>(i);
                    const size_t   pos = deltaBytes.size();
                    deltaBytes.resize(pos + 8);
                    std::memcpy(deltaBytes.data() + pos,     &idx,            4);
                    std::memcpy(deltaBytes.data() + pos + 4, &currentPtr[i],  4);
                }
            }

            if (deltaBytes.size() < DELTA_SIZE_THRESHOLD) {
                delta = ImageBufferDelta::createPixelChange(
                    buffer.width(), buffer.height(), buffer.channels(),
                    deltaBytes
                );
            } else {
                // Delta too large; fall back to a full copy
                delta = ImageBufferDelta::createFullCopy(
                    buffer.width(), buffer.height(), buffer.channels(),
                    buffer.data()
                );
                m_baseline = std::make_shared<ImageBuffer>(buffer);
            }
        } else {
            // Dimension mismatch: must store a full copy
            delta = ImageBufferDelta::createFullCopy(
                buffer.width(), buffer.height(), buffer.channels(),
                buffer.data()
            );
            m_baseline = std::make_shared<ImageBuffer>(buffer);
        }

    } else {
        // First operation: establish the initial baseline
        delta = ImageBufferDelta::createFullCopy(
            buffer.width(), buffer.height(), buffer.channels(),
            buffer.data()
        );
        m_baseline = std::make_shared<ImageBuffer>(buffer);
    }

    delta.description = description;

    // A new operation invalidates the redo stack
    m_redoStack.clear();

    m_undoStack.push_back(std::move(delta));
    m_snapshotCounter++;

    // Enforce the memory budget
    pruneHistory(MAX_HISTORY_SIZE);
}

// -----------------------------------------------------------------------------
// Undo / Redo
// -----------------------------------------------------------------------------

void ImageHistoryManager::performUndo()
{
    if (m_undoStack.empty()) return;
    m_redoStack.push_back(m_undoStack.back());
    m_undoStack.pop_back();
}

void ImageHistoryManager::performRedo()
{
    if (m_redoStack.empty()) return;
    m_undoStack.push_back(m_redoStack.back());
    m_redoStack.pop_back();
}

// -----------------------------------------------------------------------------
// Description accessors
// -----------------------------------------------------------------------------

QString ImageHistoryManager::getUndoDescription() const
{
    if (m_undoStack.empty()) return QString();
    return m_undoStack.back().description;
}

QString ImageHistoryManager::getRedoDescription() const
{
    if (m_redoStack.empty()) return QString();
    return m_redoStack.back().description;
}

std::vector<QString> ImageHistoryManager::getAllUndoDescriptions() const
{
    std::vector<QString> descs(m_undoStack.size());
    for (size_t i = 0; i < m_undoStack.size(); ++i) {
        descs[i] = m_undoStack[i].description;
    }
    return descs;
}

std::vector<QString> ImageHistoryManager::getAllRedoDescriptions() const
{
    std::vector<QString> descs(m_redoStack.size());
    for (size_t i = 0; i < m_redoStack.size(); ++i) {
        descs[i] = m_redoStack[i].description;
    }
    return descs;
}

// -----------------------------------------------------------------------------
// Memory accounting and pruning
// -----------------------------------------------------------------------------

double ImageHistoryManager::getHistorySizeMB() const
{
    size_t totalSize = 0;
    for (const auto& d : m_undoStack) totalSize += d.estimatedSize();
    for (const auto& d : m_redoStack) totalSize += d.estimatedSize();
    return static_cast<double>(totalSize) / (1024.0 * 1024.0);
}

void ImageHistoryManager::pruneHistory(size_t maxSizeBytes)
{
    const double limitMB = static_cast<double>(maxSizeBytes) / (1024.0 * 1024.0);

    while (getHistorySizeMB() > limitMB && !m_undoStack.empty()) {
        m_undoStack.erase(m_undoStack.begin());
    }

    while (getHistorySizeMB() > limitMB && !m_redoStack.empty()) {
        m_redoStack.erase(m_redoStack.begin());
    }
}