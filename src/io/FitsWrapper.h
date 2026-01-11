/**
 * @file FitsIO.h
 * @brief Unified FITS I/O wrapper for stacking operations
 * 
 * This is a wrapper around the existing FitsLoader that provides
 * the interface expected by the stacking module.
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef FITS_IO_H
#define FITS_IO_H

#include "../ImageBuffer.h"
#include "FitsLoader.h"
#include <QString>

namespace Stacking {

class FitsIO {
public:

    static bool read(const QString& path, ImageBuffer& buffer) {
        FitsLoader loader;
        return loader.load(path, buffer);
    }
    
    static bool readHeader(const QString& path, ImageBuffer& buffer) {
        return FitsLoader::loadMetadata(path, buffer);
    }
    
    static bool readRegion(const QString& path, ImageBuffer& buffer,
                          int x, int y, int width, int height, int channel = -1) {
        Q_UNUSED(channel);
        return FitsLoader::loadRegion(path, buffer, x, y, width, height);
    }
    
    static bool write(const QString& path, const ImageBuffer& buffer, int bitDepth = 32) {
        ImageBuffer::BitDepth depth = (bitDepth == 16) ? ImageBuffer::Depth_16Int : ImageBuffer::Depth_32Float;
        return buffer.save(path, "FITS", depth);
    }
};

} // namespace Stacking

#endif // FITS_IO_H
