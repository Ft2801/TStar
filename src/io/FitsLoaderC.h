
#ifndef FITSLOADERC_H
#define FITSLOADERC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float* data;           // Interleaved pixel data (row-major)
    int width;
    int height;
    int channels;          // 1=Mono, 3=RGB (auto-debayered)
    int bitpix;            // Original FITS bitpix
    char* header;          // Raw FITS header (for metadata preservation)
    size_t header_size;
    int managed;           // 1=free on unload, 0=external pointer
} FitsImage_C;

int fitsload_single_c(const char* filepath, FitsImage_C* out_img);

int fitsload_batch_c(const char** filepaths, int count, FitsImage_C* out_images, int max_threads);

int fitssave_c(const char* filepath, const FitsImage_C* img);

float* debayer_nn_c(const float* bayer_data, int width, int height, const char* pattern);

void fitsimg_free_c(FitsImage_C* img);

const char* fitserror_c(void);

#ifdef __cplusplus
}
#endif

#endif // FITSLOADERC_H
