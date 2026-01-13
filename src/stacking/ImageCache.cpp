/**
 * @file ImageCache.cpp
 * @brief Implementation of image caching for stacking
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "ImageCache.h"
#include <algorithm>

namespace Stacking {

ImageCache::ImageCache(int maxImages)
    : m_maxImages(maxImages)
{
}

ImageCache::~ImageCache() {
    clear();
}

void ImageCache::setSequence(ImageSequence* sequence) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    clear();
    m_sequence = sequence;
    
    if (sequence) {
        int count = sequence->count();
        m_cache.resize(count);
        m_loaded.assign(count, false);
    }
}

const ImageBuffer* ImageCache::get(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_sequence || index < 0 || index >= static_cast<int>(m_cache.size())) {
        return nullptr;
    }
    
    // Return cached image if available
    if (m_loaded[index] && m_cache[index]) {
        return m_cache[index].get();
    }
    
    // Check memory limit
    if (m_maxImages > 0 && size() >= m_maxImages) {
        evictOldest();
    }
    
    // Load from disk
    m_cache[index] = std::make_unique<ImageBuffer>();
    if (!m_sequence->readImage(index, *m_cache[index])) {
        m_cache[index].reset();
        return nullptr;
    }
    
    m_loaded[index] = true;
    
    // Update memory usage estimate
    if (m_cache[index]) {
        size_t imgSize = static_cast<size_t>(m_cache[index]->width()) * 
                         m_cache[index]->height() * 
                         m_cache[index]->channels() * sizeof(float);
        m_memoryUsage += imgSize;
    }
    
    return m_cache[index].get();
}

int ImageCache::preloadAll(std::function<void(int, int)> progressCallback) {
    if (!m_sequence) return 0;
    
    int count = m_sequence->count();
    std::vector<int> indices(count);
    for (int i = 0; i < count; ++i) {
        indices[i] = i;
    }
    
    return preload(indices, progressCallback);
}

int ImageCache::preload(const std::vector<int>& indices,
                        std::function<void(int, int)> progressCallback) {
    int loaded = 0;
    int total = static_cast<int>(indices.size());
    
    for (int i = 0; i < total; ++i) {
        int index = indices[i];
        
        if (progressCallback) {
            progressCallback(i, total);
        }
        
        if (get(index) != nullptr) {
            loaded++;
        }
    }
    
    return loaded;
}

void ImageCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_cache.clear();
    m_loaded.clear();
    m_memoryUsage = 0;
}

int ImageCache::size() const {
    int count = 0;
    for (bool loaded : m_loaded) {
        if (loaded) count++;
    }
    return count;
}

bool ImageCache::isCached(int index) const {
    if (index < 0 || index >= static_cast<int>(m_loaded.size())) {
        return false;
    }
    return m_loaded[index];
}

double ImageCache::memoryUsageMB() const {
    return static_cast<double>(m_memoryUsage) / (1024.0 * 1024.0);
}

void ImageCache::evictOldest() {
    // Simple FIFO eviction - evict first loaded
    for (size_t i = 0; i < m_loaded.size(); ++i) {
        if (m_loaded[i] && m_cache[i]) {
            // Calculate size before freeing
            size_t imgSize = static_cast<size_t>(m_cache[i]->width()) * 
                             m_cache[i]->height() * 
                             m_cache[i]->channels() * sizeof(float);
            m_memoryUsage -= imgSize;
            
            m_cache[i].reset();
            m_loaded[i] = false;
            break;
        }
    }
}

} // namespace Stacking
