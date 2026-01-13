/**
 * @file ImageCache.h
 * @brief Image caching system for efficient stacking
 * 
 * Provides in-memory caching of images to avoid repeated disk reads
 * during stacking operations.
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef IMAGE_CACHE_H
#define IMAGE_CACHE_H

#include "../ImageBuffer.h"
#include "StackingSequence.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace Stacking {

/**
 * @brief Thread-safe image cache for stacking operations
 * 
 * Caches images in memory to avoid repeated disk reads during
 * the stacking process. Supports LRU eviction when memory is limited.
 */
class ImageCache {
public:
    /**
     * @brief Constructor
     * @param maxImages Maximum number of images to cache (0 = unlimited)
     */
    explicit ImageCache(int maxImages = 0);
    
    ~ImageCache();
    
    /**
     * @brief Set the source sequence
     * @param sequence Pointer to the image sequence
     */
    void setSequence(ImageSequence* sequence);
    
    /**
     * @brief Get an image, loading from disk if necessary
     * @param index Image index in sequence
     * @return Pointer to cached image (nullptr if failed)
     */
    const ImageBuffer* get(int index);
    
    /**
     * @brief Preload all images into cache
     * @param progressCallback Optional progress callback
     * @return Number of images successfully loaded
     */
    int preloadAll(std::function<void(int, int)> progressCallback = nullptr);
    
    /**
     * @brief Preload specific images
     * @param indices List of indices to preload
     * @param progressCallback Optional progress callback
     * @return Number of images successfully loaded
     */
    int preload(const std::vector<int>& indices,
                std::function<void(int, int)> progressCallback = nullptr);
    
    /**
     * @brief Clear the cache
     */
    void clear();
    
    /**
     * @brief Get current cache size
     */
    int size() const;
    
    /**
     * @brief Check if an image is cached
     */
    bool isCached(int index) const;
    
    /**
     * @brief Get estimated memory usage in MB
     */
    double memoryUsageMB() const;

private:
    ImageSequence* m_sequence = nullptr;
    std::vector<std::unique_ptr<ImageBuffer>> m_cache;
    std::vector<bool> m_loaded;
    int m_maxImages = 0;
    mutable std::mutex m_mutex;
    size_t m_memoryUsage = 0;
    
    void evictOldest();
};

} // namespace Stacking

#endif // IMAGE_CACHE_H
