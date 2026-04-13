#ifndef IMAGE_CACHE_H
#define IMAGE_CACHE_H

/**
 * @file ImageCache.h
 * @brief Thread-safe in-memory image cache for stacking operations.
 *
 * Provides demand-loaded caching of images from an ImageSequence,
 * with FIFO eviction when the cache reaches its count or memory limit.
 * This avoids repeated disk I/O during multi-pass stacking algorithms.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "../ImageBuffer.h"
#include "StackingSequence.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <functional>

namespace Stacking {

class ImageCache {
public:

    /**
     * @brief Construct a cache with an optional image count limit.
     * @param maxImages  Maximum number of images to hold (0 = unlimited).
     */
    explicit ImageCache(int maxImages = 0);

    ~ImageCache();

    /**
     * @brief Bind this cache to an image sequence.
     *
     * Clears any previously cached data and prepares internal storage
     * for the new sequence.
     *
     * @param sequence  Pointer to the source image sequence.
     */
    void setSequence(ImageSequence* sequence);

    /**
     * @brief Retrieve an image by index, loading from disk if necessary.
     *
     * If the cache is full, the oldest entry is evicted first.
     *
     * @param index  Image index within the sequence.
     * @return Pointer to the cached image, or nullptr on failure.
     */
    const ImageBuffer* get(int index);

    /**
     * @brief Preload all images in the sequence into the cache.
     * @param progressCallback  Optional (current, total) progress callback.
     * @return Number of images successfully loaded.
     */
    int preloadAll(std::function<void(int, int)> progressCallback = nullptr);

    /**
     * @brief Preload a specific subset of images.
     * @param indices           List of image indices to preload.
     * @param progressCallback  Optional progress callback.
     * @return Number of images successfully loaded.
     */
    int preload(const std::vector<int>& indices,
                std::function<void(int, int)> progressCallback = nullptr);

    /**
     * @brief Release all cached images and reset memory tracking.
     */
    void clear();

    /** @brief Number of images currently held in the cache. */
    int size() const;

    /** @brief Check whether a specific index is currently cached. */
    bool isCached(int index) const;

    /** @brief Estimated memory usage of cached images in megabytes. */
    double memoryUsageMB() const;

private:

    ImageSequence*                              m_sequence    = nullptr;
    std::vector<std::unique_ptr<ImageBuffer>>   m_cache;
    std::vector<bool>                           m_loaded;
    int                                         m_maxImages   = 0;
    mutable std::mutex                          m_mutex;
    size_t                                      m_memoryUsage = 0;

    /** @brief Evict the first (oldest) loaded image to free memory. */
    void evictOldest();
};

} // namespace Stacking

#endif // IMAGE_CACHE_H