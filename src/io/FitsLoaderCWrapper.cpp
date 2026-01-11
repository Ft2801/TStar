/**
 * @file FitsLoaderCWrapper.cpp
 * @brief C++ wrapper implementation - bridges FitsLoaderC to ImageBuffer
 */

#include "FitsLoaderCWrapper.h"
#include "FitsLoaderC.h"
#include <QDebug>
#include <cstring>

namespace IO {

bool FitsLoaderCWrapper::loadFitsC(const QString& filepath, ImageBuffer& out_buffer) {
    // Convert QString to UTF-8 C string
    QByteArray file_path_bytes = filepath.toUtf8();
    const char* c_filepath = file_path_bytes.constData();

    // Allocate C image structure
    FitsImage_C c_img = {0};

    // Load using optimized C code (cfitsio directly, no Qt)
    if (fitsload_single_c(c_filepath, &c_img) != 0) {
        qWarning() << "FitsLoaderC error:" << QString::fromUtf8(fitserror_c());
        return false;
    }

    // Convert C image to ImageBuffer
    std::vector<float> buffer_data;
    size_t total_pixels = (size_t)c_img.width * c_img.height * c_img.channels;
    
    if (c_img.data) {
        buffer_data.assign(c_img.data, c_img.data + total_pixels);
    }

    // Create ImageBuffer with C-loaded data
    out_buffer.setData(c_img.width, c_img.height, c_img.channels, buffer_data);

    // Free C image (but keep the data pointer we just copied)
    fitsimg_free_c(&c_img);

    return true;
}

int FitsLoaderCWrapper::loadFitsBatchC(const QStringList& filepaths, std::vector<ImageBuffer>& out_buffers, int max_threads) {
    int count = filepaths.size();
    if (count <= 0) return 0;

    // Convert QStringList to C array of const char*
    std::vector<QByteArray> path_bytes;
    std::vector<const char*> c_filepaths;
    
    for (const QString& path : filepaths) {
        path_bytes.push_back(path.toUtf8());
    }
    
    for (const QByteArray& bytes : path_bytes) {
        c_filepaths.push_back(bytes.constData());
    }

    // Allocate C image array
    FitsImage_C* c_images = (FitsImage_C*)calloc(count, sizeof(FitsImage_C));
    if (!c_images) return 0;

    // Load in parallel using pthreads (raw C, no Qt threads)
    int loaded = fitsload_batch_c(c_filepaths.data(), count, c_images, max_threads);

    // Convert C images to ImageBuffers
    out_buffers.clear();
    out_buffers.reserve(count);

    for (int i = 0; i < count; i++) {
        if (c_images[i].data != NULL) {
            std::vector<float> buffer_data;
            size_t total = (size_t)c_images[i].width * c_images[i].height * c_images[i].channels;
            buffer_data.assign(c_images[i].data, c_images[i].data + total);

            ImageBuffer img_buf;
            img_buf.setData(c_images[i].width, c_images[i].height, c_images[i].channels, buffer_data);
            out_buffers.push_back(img_buf);
        }
    }

    // Free C images
    for (int i = 0; i < count; i++) {
        fitsimg_free_c(&c_images[i]);
    }
    free(c_images);

    return loaded;
}

QString FitsLoaderCWrapper::getLastError() {
    return QString::fromUtf8(fitserror_c());
}

} // namespace IO
