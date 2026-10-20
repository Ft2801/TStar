/**
 * @file ImageCache.cpp
 * @brief Implementation of the in-memory image cache.
 *
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
 */

#include "ImageCache.h"
#include <algorithm>
#include "../core/ResourceManager.h"

namespace Stacking {

// =============================================================================
// Construction / Destruction
// =============================================================================

ImageCache::ImageCache(int maxImages)
    : m_maxImages(maxImages)
{
}

ImageCache::~ImageCache()
{
    clear();
}

// =============================================================================
// Sequence Binding
// =============================================================================

void ImageCache::setSequence(ImageSequence* sequence)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    clear();
    m_sequence = sequence;

    if (sequence) {
        int count = sequence->count();
        m_cache.resize(count);
        m_loaded.assign(count, false);
    }
}

// =============================================================================
// Image Retrieval (with demand loading)
// =============================================================================

const ImageBuffer* ImageCache::get(int index)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sequence || index < 0 || index >= static_cast<int>(m_cache.size())) {
        return nullptr;
    }

    // Return immediately if already cached.
    if (m_loaded[index] && m_cache[index]) {
        return m_cache[index].get();
    }

    // Estimate the memory cost of loading this image.
    size_t expectedSize = static_cast<size_t>(m_sequence->width())
                        * m_sequence->height()
                        * m_sequence->channels()
                        * sizeof(float);

    // Evict entries until we have room (by count or available memory).
    while ((m_maxImages > 0 && size() >= m_maxImages) ||
           !ResourceManager::instance().isMemorySafe(expectedSize))
    {
        if (size() == 0) break;
        evictOldest();
    }

    // Load the image from disk.
    m_cache[index] = std::make_unique<ImageBuffer>();
    if (!m_sequence->readImage(index, *m_cache[index])) {
        m_cache[index].reset();
        return nullptr;
    }

    m_loaded[index] = true;

    // Update memory tracking.
    if (m_cache[index]) {
        size_t imgSize = static_cast<size_t>(m_cache[index]->width())
                       * m_cache[index]->height()
                       * m_cache[index]->channels()
                       * sizeof(float);
        m_memoryUsage += imgSize;
    }

    return m_cache[index].get();
}

// =============================================================================
// Preloading
// =============================================================================

int ImageCache::preloadAll(std::function<void(int, int)> progressCallback)
{
    if (!m_sequence) return 0;

    int count = m_sequence->count();
    std::vector<int> indices(count);
    for (int i = 0; i < count; ++i) {
        indices[i] = i;
    }

    return preload(indices, progressCallback);
}

int ImageCache::preload(const std::vector<int>& indices,
                        std::function<void(int, int)> progressCallback)
{
    int loaded = 0;
    const int total = static_cast<int>(indices.size());

    for (int i = 0; i < total; ++i) {
        if (progressCallback) {
            progressCallback(i, total);
        }
        if (get(indices[i]) != nullptr) {
            ++loaded;
        }
    }

    return loaded;
}

// =============================================================================
// Cache Management
// =============================================================================

void ImageCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
    m_loaded.clear();
    m_memoryUsage = 0;
}

int ImageCache::size() const
{
    int count = 0;
    for (bool loaded : m_loaded) {
        if (loaded) ++count;
    }
    return count;
}

bool ImageCache::isCached(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_loaded.size())) {
        return false;
    }
    return m_loaded[index];
}

double ImageCache::memoryUsageMB() const
{
    return static_cast<double>(m_memoryUsage) / (1024.0 * 1024.0);
}

void ImageCache::evictOldest()
{
    // Simple FIFO eviction: release the first loaded entry.
    for (size_t i = 0; i < m_loaded.size(); ++i) {
        if (m_loaded[i] && m_cache[i]) {
            size_t imgSize = static_cast<size_t>(m_cache[i]->width())
                           * m_cache[i]->height()
                           * m_cache[i]->channels()
                           * sizeof(float);
            m_memoryUsage -= imgSize;

            m_cache[i].reset();
            m_loaded[i] = false;
            break;
        }
    }
}

} // namespace Stacking