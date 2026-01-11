
#include "FitsLoaderC.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fitsio.h>

// Thread-safe error storage (thread local would be better, but TLS is complex in C)
static char g_error_msg[512] = "No error";

// ============================================================================
// DEBAYERING: Nearest-neighbor (fastest, good enough for stacking)
// ============================================================================

float* debayer_nn_c(const float* bayer_data, int width, int height, const char* pattern) {
    if (!bayer_data || !pattern || width < 2 || height < 2) {
        strcpy(g_error_msg, "Invalid debayer parameters");
        return NULL;
    }

    size_t rgb_size = (size_t)width * height * 3 * sizeof(float);
    float* rgb = (float*)malloc(rgb_size);
    if (!rgb) {
        strcpy(g_error_msg, "Failed to allocate RGB buffer");
        return NULL;
    }

    // Determine pattern (assuming top-left corner orientation)
    int r_x = 0, r_y = 0, g1_x = 0, g1_y = 0, b_x = 0, b_y = 0, g2_x = 0, g2_y = 0;
    
    if (strcmp(pattern, "RGGB") == 0) {
        r_x = 0; r_y = 0;  // Top-left
        g1_x = 1; g1_y = 0; // Top-right
        b_x = 1; b_y = 1;  // Bottom-right
        g2_x = 0; g2_y = 1; // Bottom-left
    } else if (strcmp(pattern, "BGGR") == 0) {
        b_x = 0; b_y = 0;
        g1_x = 1; g1_y = 0;
        r_x = 1; r_y = 1;
        g2_x = 0; g2_y = 1;
    } else if (strcmp(pattern, "GRBG") == 0) {
        g1_x = 0; g1_y = 0;
        r_x = 1; r_y = 0;
        b_x = 1; b_y = 1;
        g2_x = 0; g2_y = 1;
    } else if (strcmp(pattern, "GBRG") == 0) {
        g1_x = 0; g1_y = 0;
        b_x = 1; b_y = 0;
        r_x = 1; r_y = 1;
        g2_x = 0; g2_y = 1;
    } else {
        strcpy(g_error_msg, "Unknown Bayer pattern");
        free(rgb);
        return NULL;
    }

    // Nearest-neighbor interpolation (fast, good for stacking)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t rgb_idx = ((size_t)y * width + x) * 3;
            
            // Nearest R, G, B values in Bayer grid
            float r_val = bayer_data[(size_t)((y - y % 2 + r_y) * width + (x - x % 2 + r_x))];
            float b_val = bayer_data[(size_t)((y - y % 2 + b_y) * width + (x - x % 2 + b_x))];
            
            // Green: average of two greens
            float g1 = bayer_data[(size_t)((y - y % 2 + g1_y) * width + (x - x % 2 + g1_x))];
            float g2 = bayer_data[(size_t)((y - y % 2 + g2_y) * width + (x - x % 2 + g2_x))];
            float g_val = (g1 + g2) * 0.5f;
            
            rgb[rgb_idx + 0] = r_val;  // R
            rgb[rgb_idx + 1] = g_val;  // G
            rgb[rgb_idx + 2] = b_val;  // B
        }
    }

    return rgb;
}

// ============================================================================
// FITS READING (cfitsio directly)
// ============================================================================

int fitsload_single_c(const char* filepath, FitsImage_C* out_img) {
    if (!filepath || !out_img) {
        strcpy(g_error_msg, "NULL parameters");
        return -1;
    }

    fitsfile* fptr = NULL;
    int status = 0;
    int hdu_type, naxis, bitpix;
    long naxes[3] = {1, 1, 1};
    char bayer_pattern[16] = {0};
    float* bayer_data = NULL;

    // Open FITS file (cfitsio native)
    fits_open_file(&fptr, filepath, READONLY, &status);
    if (status) {
        snprintf(g_error_msg, sizeof(g_error_msg), "Failed to open FITS: %s", filepath);
        return -1;
    }

    // Get image dimensions
    fits_get_hdu_type(fptr, &hdu_type, &status);
    fits_get_img_dim(fptr, &naxis, &status);
    fits_get_img_type(fptr, &bitpix, &status);
    fits_get_img_size(fptr, 3, naxes, &status);

    if (status || naxis < 2) {
        strcpy(g_error_msg, "Invalid FITS image");
        fits_close_file(fptr, &status);
        return -1;
    }

    int width = (int)naxes[0];
    int height = (int)naxes[1];
    int channels = (int)naxes[2];
    if (channels == 0) channels = 1;

    out_img->width = width;
    out_img->height = height;
    out_img->bitpix = bitpix;
    
    // Try to get Bayer pattern from header
    char card[81] = {0};
    fits_read_card(fptr, "BAYERPAT", card, &status);
    status = 0;  // Reset status, BAYERPAT may not exist
    
    if (strlen(card) > 0) {
        sscanf(card, "BAYERPAT= '%15s'", bayer_pattern);
    }

    // Allocate and read image data
    size_t total_pixels = (size_t)width * height;
    
    if (channels == 1 && strlen(bayer_pattern) > 0) {
        // Mono Bayer CFA - read and debayer
        bayer_data = (float*)malloc(total_pixels * sizeof(float));
        if (!bayer_data) {
            strcpy(g_error_msg, "Memory allocation failed for Bayer data");
            fits_close_file(fptr, &status);
            return -1;
        }

        // Read Bayer plane
        long fpixel[3] = {1, 1, 1};
        fits_read_pix(fptr, TFLOAT, fpixel, total_pixels, NULL, bayer_data, NULL, &status);
        
        if (status) {
            strcpy(g_error_msg, "Failed to read FITS image data");
            free(bayer_data);
            fits_close_file(fptr, &status);
            return -1;
        }

        // Debayer
        float* rgb = debayer_nn_c(bayer_data, width, height, bayer_pattern);
        free(bayer_data);
        
        if (!rgb) {
            fits_close_file(fptr, &status);
            return -1;
        }

        out_img->data = rgb;
        out_img->channels = 3;
    } else {
        // Already RGB or Mono
        size_t total_size = total_pixels * channels * sizeof(float);
        out_img->data = (float*)malloc(total_size);
        if (!out_img->data) {
            strcpy(g_error_msg, "Memory allocation failed");
            fits_close_file(fptr, &status);
            return -1;
        }

        long fpixel[3] = {1, 1, 1};
        fits_read_pix(fptr, TFLOAT, fpixel, total_pixels * channels, NULL, out_img->data, NULL, &status);
        
        if (status) {
            strcpy(g_error_msg, "Failed to read FITS image data");
            free(out_img->data);
            fits_close_file(fptr, &status);
            return -1;
        }

        out_img->channels = channels;
    }

    out_img->managed = 1;  // We allocated this
    out_img->header = NULL;
    out_img->header_size = 0;

    fits_close_file(fptr, &status);
    return 0;
}

// ============================================================================
// PARALLEL BATCH LOADING (pthreads)
// ============================================================================

typedef struct {
    const char* filepath;
    FitsImage_C* out_img;
    int result;
} FitsLoadTask;

void* fitsload_thread(void* arg) {
    FitsLoadTask* task = (FitsLoadTask*)arg;
    task->result = fitsload_single_c(task->filepath, task->out_img);
    return NULL;
}

int fitsload_batch_c(const char** filepaths, int count, FitsImage_C* out_images, int max_threads) {
    if (!filepaths || !out_images || count <= 0 || max_threads <= 0) {
        strcpy(g_error_msg, "Invalid batch parameters");
        return 0;
    }

    if (max_threads > count) max_threads = count;

    // Create thread pool
    pthread_t* threads = (pthread_t*)malloc(max_threads * sizeof(pthread_t));
    FitsLoadTask* tasks = (FitsLoadTask*)malloc(count * sizeof(FitsLoadTask));
    
    if (!threads || !tasks) {
        free(threads);
        free(tasks);
        strcpy(g_error_msg, "Failed to allocate thread structures");
        return 0;
    }

    // Initialize tasks
    for (int i = 0; i < count; i++) {
        tasks[i].filepath = filepaths[i];
        tasks[i].out_img = &out_images[i];
        tasks[i].result = -1;
    }

    // Launch threads (simple round-robin)
    int task_idx = 0;
    for (int t = 0; t < max_threads && task_idx < count; t++) {
        pthread_create(&threads[t], NULL, fitsload_thread, &tasks[task_idx]);
        task_idx++;
    }

    // Wait for threads and queue next tasks
    for (int t = 0; t < max_threads; t++) {
        pthread_join(threads[t], NULL);
        
        if (task_idx < count) {
            pthread_create(&threads[t], NULL, fitsload_thread, &tasks[task_idx]);
            task_idx++;
        }
    }

    // Wait for remaining threads
    for (int t = 0; task_idx - max_threads + t < count; t++) {
        if (t < max_threads) {
            pthread_join(threads[t], NULL);
        }
    }

    // Count successes
    int success = 0;
    for (int i = 0; i < count; i++) {
        if (tasks[i].result == 0) success++;
    }

    free(threads);
    free(tasks);
    return success;
}

// ============================================================================
// FITS WRITING
// ============================================================================

int fitssave_c(const char* filepath, const FitsImage_C* img) {
    if (!filepath || !img || !img->data) {
        strcpy(g_error_msg, "NULL parameters");
        return -1;
    }

    fitsfile* fptr = NULL;
    int status = 0;
    long naxes[3] = {img->width, img->height, img->channels};

    // Create FITS file
    fits_create_file(&fptr, filepath, &status);
    if (status) {
        snprintf(g_error_msg, sizeof(g_error_msg), "Failed to create FITS: %s", filepath);
        return -1;
    }

    // Create image HDU
    int naxis = (img->channels > 1) ? 3 : 2;
    fits_create_img(fptr, FLOAT_IMG, naxis, naxes, &status);

    // Write image data
    long fpixel[3] = {1, 1, 1};
    size_t total = (size_t)img->width * img->height * img->channels;
    fits_write_pix(fptr, TFLOAT, fpixel, total, img->data, &status);

    if (status) {
        strcpy(g_error_msg, "Failed to write FITS image data");
        fits_close_file(fptr, &status);
        return -1;
    }

    fits_close_file(fptr, &status);
    return 0;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

const char* fitserror_c(void) {
    return g_error_msg;
}

void fitsimg_free_c(FitsImage_C* img) {
    if (!img) return;
    if (img->managed && img->data) {
        free(img->data);
        img->data = NULL;
    }
    if (img->header) {
        free(img->header);
        img->header = NULL;
    }
}
