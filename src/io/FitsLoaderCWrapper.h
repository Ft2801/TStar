/**
 * @file FitsLoaderCWrapper.h
 * @brief C++ wrapper for FitsLoaderC - integrates high-speed C FITS I/O with Qt
 */

#ifndef FITSLOADERCWRAPPER_H
#define FITSLOADERCWRAPPER_H

#include <QString>
#include <vector>
#include "../ImageBuffer.h"

namespace IO {

class FitsLoaderCWrapper {
public:
    /**
     * @brief Load single FITS file using optimized C code
     * Returns ImageBuffer with data loaded via cfitsio (no Qt overhead)
     */
    static bool loadFitsC(const QString& filepath, ImageBuffer& out_buffer);

    /**
     * @brief Batch load FITS files in parallel using pthreads
     * Much faster than sequential Qt-based loading
     */
    static int loadFitsBatchC(const QStringList& filepaths, std::vector<ImageBuffer>& out_buffers, int max_threads);

    /**
     * @brief Get error message from last C operation
     */
    static QString getLastError();
};

} // namespace IO

#endif
