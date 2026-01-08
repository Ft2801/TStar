#ifndef IMAGEBUFFERDELTA_H
#define IMAGEBUFFERDELTA_H

#include <cstdint>
#include <vector>
#include <memory>
#include <cstring>

/**
 * @brief Delta-based undo/redo system for ImageBuffer
 * 
 * Instead of storing full ImageBuffer copies, we store only the differences
 * between states. This reduces memory usage from 3GB * 20 = 60GB to ~2GB total.
 * 
 * Strategy:
 * - Full snapshots every N operations (baseline)
 * - Delta patches between snapshots (lightweight)
 * - Automatic garbage collection when memory limit exceeded
 */

struct ImageBufferDelta {
    // Metadata about what changed
    enum ChangeType {
        CHANGE_FULL_COPY,      // Full ImageBuffer copy (expensive)
        CHANGE_PIXEL_DATA,     // Only pixel data changed
        CHANGE_METADATA,       // Only metadata changed
        CHANGE_METADATA_SMALL  // Minimal metadata change (display mode, etc.)
    };
    
    ChangeType type;
    int width;
    int height;
    int channels;
    
    // Pixel delta (only for CHANGE_PIXEL_DATA)
    std::vector<uint8_t> pixelDelta;
    
    // Metadata delta
    std::vector<uint8_t> metadataDelta;
    
    // Full copy fallback (for CHANGE_FULL_COPY)
    std::shared_ptr<std::vector<float>> fullPixelData;
    
    // Estimated memory size in bytes
    size_t estimatedSize() const;
    
    // Constructor for metadata-only changes (cheapest)
    static ImageBufferDelta createMetadataChange();
    
    // Constructor for pixel-only changes
    static ImageBufferDelta createPixelChange(
        int w, int h, int c,
        const std::vector<uint8_t>& delta);
    
    // Constructor for full copy (fallback when delta gets too large)
    static ImageBufferDelta createFullCopy(
        int w, int h, int c,
        const std::vector<float>& pixelData);
};

/**
 * @brief History management with delta compression
 * 
 * Stores maximum 50MB of deltas, then switches to full snapshots
 * Automatically prunes oldest entries when limit exceeded
 */
class ImageHistoryManager {
public:
    ImageHistoryManager();
    ~ImageHistoryManager();
    
    // Push undo state (must call before destructive operation)
    void pushUndo(const class ImageBuffer& buffer);
    
    // Check undo/redo availability
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
    
    // Undo/redo operations
    void performUndo();
    void performRedo();
    
    // Get size of history in MB
    double getHistorySizeMB() const;
    
    // Get number of undo states available
    int getUndoCount() const { return m_undoStack.size(); }
    
    // Manual cleanup (called when memory pressure detected)
    void pruneHistory(size_t maxSizeBytes = 50 * 1024 * 1024);
    
    // Clear all history
    void clear() {
        m_undoStack.clear();
        m_redoStack.clear();
    }
    
private:
    std::vector<ImageBufferDelta> m_undoStack;
    std::vector<ImageBufferDelta> m_redoStack;
    
    // Track baseline snapshots for compression reference
    int m_snapshotCounter = 0;
    static constexpr int SNAPSHOT_INTERVAL = 5;  // Full snapshot every 5 deltas
    
    // Baseline for computing deltas (to reconstruct if needed)
    std::shared_ptr<class ImageBuffer> m_baseline;
    
    // Memory limits
    static constexpr size_t MAX_HISTORY_SIZE = 50 * 1024 * 1024;  // 50MB
    static constexpr size_t DELTA_SIZE_THRESHOLD = 5 * 1024 * 1024; // 5MB per delta
};

#endif // IMAGEBUFFERDELTA_H
