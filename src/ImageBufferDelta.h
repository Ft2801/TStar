// =============================================================================
// ImageBufferDelta.h
//
// Delta-based undo/redo system for ImageBuffer. Instead of storing complete
// copies of each image state, this system stores only the differences between
// consecutive states, significantly reducing memory consumption.
//
// Strategy:
//   - A full snapshot is taken every N operations as a baseline.
//   - Between snapshots, lightweight delta patches are stored.
//   - When the total history exceeds the memory limit, the oldest entries
//     are pruned automatically.
// =============================================================================

#ifndef IMAGEBUFFERDELTA_H
#define IMAGEBUFFERDELTA_H

#include <cstdint>
#include <vector>
#include <memory>
#include <cstring>
#include <QString>

// =============================================================================
// ImageBufferDelta -- a single history entry
// =============================================================================

struct ImageBufferDelta {

    /// Describes what kind of change this entry represents.
    enum ChangeType {
        CHANGE_FULL_COPY,       ///< Complete image copy (most expensive)
        CHANGE_PIXEL_DATA,      ///< Only pixel data changed (delta-encoded)
        CHANGE_METADATA,        ///< Only metadata changed
        CHANGE_METADATA_SMALL   ///< Minimal metadata change (display mode, etc.)
    };

    ChangeType type;
    int width;
    int height;
    int channels;

    /// Run-length encoded pixel delta (used when type == CHANGE_PIXEL_DATA).
    std::vector<uint8_t> pixelDelta;

    /// Serialized metadata delta.
    std::vector<uint8_t> metadataDelta;

    /// Full pixel data (used when type == CHANGE_FULL_COPY).
    std::shared_ptr<std::vector<float>> fullPixelData;

    /// Human-readable description of the operation (e.g. "ABE", "Crop").
    QString description;

    /// Estimated memory footprint of this entry in bytes.
    size_t estimatedSize() const;

    // ---- Factory methods ----------------------------------------------------

    /// Create a metadata-only change entry (cheapest).
    static ImageBufferDelta createMetadataChange();

    /// Create a pixel-delta change entry.
    static ImageBufferDelta createPixelChange(
        int w, int h, int c,
        const std::vector<uint8_t>& delta
    );

    /// Create a full-copy entry (fallback when delta is too large).
    static ImageBufferDelta createFullCopy(
        int w, int h, int c,
        const std::vector<float>& pixelData
    );
};

// =============================================================================
// ImageHistoryManager -- manages undo/redo stacks with delta compression
// =============================================================================

class ImageHistoryManager {
public:
    ImageHistoryManager();
    ~ImageHistoryManager();

    /// Push the current buffer state onto the undo stack.
    /// Must be called before each destructive operation.
    void pushUndo(const class ImageBuffer& buffer,
                  const QString& description = QString());

    /// Human-readable description of the top undo/redo entries.
    QString getUndoDescription() const;
    QString getRedoDescription() const;

    std::vector<QString> getAllUndoDescriptions() const;
    std::vector<QString> getAllRedoDescriptions() const;

    /// Check whether undo or redo is available.
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

    /// Perform undo (moves top of undo stack to redo stack).
    void performUndo();

    /// Perform redo (moves top of redo stack to undo stack).
    void performRedo();

    /// Total memory consumed by all history entries, in megabytes.
    double getHistorySizeMB() const;

    /// Number of undo states currently stored.
    int getUndoCount() const { return static_cast<int>(m_undoStack.size()); }

    /// Remove the oldest entries until the total size is below the limit.
    void pruneHistory(size_t maxSizeBytes = 50 * 1024 * 1024);

    /// Discard all history.
    void clear() {
        m_undoStack.clear();
        m_redoStack.clear();
    }

private:
    std::vector<ImageBufferDelta> m_undoStack;
    std::vector<ImageBufferDelta> m_redoStack;

    /// Counter used to decide when to take a full snapshot.
    int m_snapshotCounter = 0;

    /// A full snapshot is stored every this many operations.
    static constexpr int SNAPSHOT_INTERVAL = 5;

    /// Reference baseline for computing deltas.
    std::shared_ptr<class ImageBuffer> m_baseline;

    /// Maximum total size of all history entries before pruning.
    static constexpr size_t MAX_HISTORY_SIZE     = 50 * 1024 * 1024;  // 50 MB

    /// Maximum size of a single delta before falling back to a full copy.
    static constexpr size_t DELTA_SIZE_THRESHOLD =  5 * 1024 * 1024;  //  5 MB
};

#endif // IMAGEBUFFERDELTA_H