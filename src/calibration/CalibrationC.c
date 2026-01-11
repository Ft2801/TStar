#include "CalibrationC.h"
#include <math.h>
#include <omp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void subtract_bias_c(float* image, const float* bias, size_t size, int threads) {
    #pragma omp parallel for num_threads(threads)
    for (long long i = 0; i < (long long)size; ++i) {
        image[i] -= bias[i];
    }
}

void subtract_dark_c(float* image, const float* dark, size_t size, float k, float pedestal, int threads) {
    #pragma omp parallel for num_threads(threads)
    for (long long i = 0; i < (long long)size; ++i) {
        image[i] = (image[i] - k * dark[i]) + pedestal;
    }
}

void apply_flat_c(float* image, const float* flat, size_t size, float normalization, int threads) {
    float eps = 0.0001f * normalization;
    #pragma omp parallel for num_threads(threads)
    for (long long i = 0; i < (long long)size; ++i) {
        float f = flat[i];
        if (f > eps) {
            image[i] *= (normalization / f);
        } else {
            image[i] *= (normalization / eps);
        }
    }
}

// Helper for median computation
static int cmp_float(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

// Helper for quick median of a float array
static float quick_median(float* data, size_t n) {
    if (n == 0) return 0.0f;
    size_t half = n / 2;
    qsort(data, n, sizeof(float), cmp_float);
    if (n % 2 == 0) return (data[half - 1] + data[half]) * 0.5f;
    return data[half];
}

int find_deviant_pixels_c(const float* dark, int width, int height, 
                           float hot_sigma, float cold_sigma,
                           int* out_x, int* out_y, int max_output,
                           int cfa_pattern) {
    if (!dark || !out_x || !out_y || max_output <= 0) return 0;

    // NOTE: Dark frame is usually SINGLE CHANNEL (Mono) even for OSC before debayer.
    // If it is 3 channel (Interleaved), we must handle stride.
    // For now assuming Mono input for finding hot pixels on dark frame.
    // If logic changes to require multichannel support, stride MUST be added.
    // Given the signature takes 'const float* dark' without channels arg, it implies Mono/Raw.
    
    int count = 0;
    
    // If NOT CFA, treat as one block
    int phases_x = (cfa_pattern >= 0) ? 2 : 1;
    int phases_y = (cfa_pattern >= 0) ? 2 : 1;
    
    size_t max_phase_pixels = (size_t)((width / phases_x + 1) * (height / phases_y + 1));
    float* phase_data = (float*)malloc(max_phase_pixels * sizeof(float));
    float* abs_dev = (float*)malloc(max_phase_pixels * sizeof(float));
    
    if (!phase_data || !abs_dev) {
        free(phase_data); free(abs_dev);
        return 0;
    }

    // Process each phase
    for (int py = 0; py < phases_y; ++py) {
        for (int px = 0; px < phases_x; ++px) {
            size_t n = 0;
            for (int y = py; y < height; y += phases_y) {
                for (int x = px; x < width; x += phases_x) {
                    phase_data[n++] = dark[y * width + x];
                }
            }
            
            if (n < 5) continue;

            float median = quick_median(phase_data, n);
            
            for (size_t i = 0; i < n; ++i) {
                abs_dev[i] = fabsf(phase_data[i] - median);
            }
            float mad = quick_median(abs_dev, n);
            float sigma = mad * 1.4826f;
            if (sigma < 1e-6f) sigma = 1e-6f;

            float hot_thresh = median + hot_sigma * sigma;
            float cold_thresh = median - cold_sigma * sigma;

            for (int y = py; y < height; y += phases_y) {
                for (int x = px; x < width; x += phases_x) {
                    float val = dark[y * width + x];
                    if (val > hot_thresh || val < cold_thresh) {
                        if (count < max_output) {
                            out_x[count] = x;
                            out_y[count] = y;
                            count++;
                        }
                    }
                }
            }
        }
    }
    
    free(phase_data);
    free(abs_dev);
    return count;
}

void fix_banding_c(float* image, int width, int height, int channels, int cfa_pattern, int threads) {
    if (!image) return;

    // SUPPORT INTERLEAVED DATA: Pixel = [C1, C2, C3, C1, C2, C3...]
    // Channels argument tells us stride.
    
    float* row_buffer = (float*)malloc(width * sizeof(float)); // Temp wrapper for gather
    float* row_corrections = (float*)malloc(height * sizeof(float));
    
    // We need a large buffer for median calc. Max is whole channel.
    size_t total_px = (size_t)width * height;
    float* med_buffer = (float*)malloc(total_px * sizeof(float));

    if (!row_buffer || !row_corrections || !med_buffer) {
        free(row_buffer); free(row_corrections); free(med_buffer);
        return;
    }

    for (int c = 0; c < channels; ++c) {
        // Handle CFA Phases if mono (channels=1)
        int phases_y = (channels == 1 && cfa_pattern >= 0) ? 2 : 1;
        if (cfa_pattern == 4) phases_y = 6;
        
        float targets[6] = {0};
        
        for(int p=0; p<phases_y; ++p) {
            size_t count = 0;
            size_t step = (total_px > 2000000) ? 10 : 1; 
            
            // Gather ALL pixels of this channel/phase into med_buffer
            // Stride X = channels * step? No, step is for subsampling.
            // Pixel Addr = (y * width + x) * channels + c
            
            for(int y = p; y < height; y += phases_y) {
                for(int x = 0; x < width; x += step) {
                    med_buffer[count++] = image[(y * width + x) * channels + c];
                }
            }
            
            if (count > 0) {
                qsort(med_buffer, count, sizeof(float), cmp_float);
                targets[p] = (count % 2 == 0) ? (med_buffer[count/2-1] + med_buffer[count/2])*0.5f : med_buffer[count/2];
            }
        }
        
        // Per-row correction
        #pragma omp parallel for num_threads(threads)
        for(int y = 0; y < height; ++y) {
            // Determine phase
            int p = y % phases_y;
            
            // Gather row (INTERLEAVED stride)
            // Cannot use memcpy. Must loop.
            // allocate local stack buffer? or use shared?
            // malloc inside omp loop is slow.
            // Let's alloc 'row_samples' locally.
            float* row_samples = (float*)malloc(width * sizeof(float));
            if(row_samples) {
                for(int x=0; x<width; ++x) {
                    row_samples[x] = image[(y * width + x) * channels + c];
                }
                qsort(row_samples, width, sizeof(float), cmp_float);
                float row_med = (width % 2 == 0) ? (row_samples[width/2-1] + row_samples[width/2])*0.5f : row_samples[width/2];
                row_corrections[y] = targets[p] - row_med;
                free(row_samples);
            }
        }
        
        // Apply
        #pragma omp parallel for num_threads(threads)
        for (int y = 0; y < height; ++y) {
            float corr = row_corrections[y];
            for (int x = 0; x < width; ++x) {
                image[(y * width + x) * channels + c] += corr;
            }
        }
    }

    free(row_buffer);
    free(row_corrections);
    free(med_buffer);
}

void equalize_cfa_c(float* image, int width, int height, int pattern_type, int threads) {
    if (!image) return;
    // Assuming Mono Raw for CFA Equalization (channels=1 usually)
    // If it were RGB, it would be weird to equalize CFA.
    // So assume stride 1.
    
    int block_w = (pattern_type == 4) ? 6 : 2;
    int block_h = (pattern_type == 4) ? 6 : 2;
    int num_cells = block_w * block_h;

    double* cell_sums = (double*)calloc(num_cells, sizeof(double));
    long long* cell_counts = (long long*)calloc(num_cells, sizeof(long long));

    int start_x = width / 3;
    int start_y = height / 3;
    int end_x = 2 * width / 3;
    int end_y = 2 * height / 3;

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            int cell_idx = (y % block_h) * block_w + (x % block_w);
            cell_sums[cell_idx] += image[y * width + x];
            cell_counts[cell_idx]++;
        }
    }

    double* cell_means = (double*)malloc(num_cells * sizeof(double));
    for (int i = 0; i < num_cells; ++i) {
        cell_means[i] = (cell_counts[i] > 0) ? (cell_sums[i] / cell_counts[i]) : 0.0;
    }

    // XTrans map (6x6): 0=G, 1=R, 2=B
    const int xtrans_map[6][6] = {
        {0, 1, 0, 0, 2, 0}, {1, 0, 1, 2, 0, 2}, {0, 1, 0, 0, 2, 0},
        {0, 2, 0, 0, 1, 0}, {2, 0, 2, 1, 0, 1}, {0, 2, 0, 0, 1, 0}
    };

    double ch_sums[3] = {0};
    int ch_counts[3] = {0};

    for (int y = 0; y < block_h; ++y) {
        for (int x = 0; x < block_w; ++x) {
            int cell_idx = y * block_w + x;
            int ch = 1; 
            
            if (pattern_type == 4) {
                ch = xtrans_map[y][x];
            } else if (pattern_type == 0) { // RGGB
                if (y%2==0) ch = (x%2==0) ? 0 : 1; else ch = (x%2==0) ? 1 : 2;
            } else if (pattern_type == 1) { // BGGR
                if (y%2==0) ch = (x%2==0) ? 2 : 1; else ch = (x%2==0) ? 1 : 0;
            } else if (pattern_type == 2) { // GRBG
                if (y%2==0) ch = (x%2==0) ? 1 : 0; else ch = (x%2==0) ? 2 : 1;
            } else if (pattern_type == 3) { // GBRG
                if (y%2==0) ch = (x%2==0) ? 1 : 2; else ch = (x%2==0) ? 0 : 1;
            }
            
            if (cell_counts[cell_idx] > 0) {
                ch_sums[ch] += cell_means[cell_idx];
                ch_counts[ch]++;
            }
        }
    }

    double r_mean = (ch_counts[0] > 0) ? ch_sums[0] / ch_counts[0] : 0.0;
    double g_mean = (ch_counts[1] > 0) ? ch_sums[1] / ch_counts[1] : 0.0;
    double b_mean = (ch_counts[2] > 0) ? ch_sums[2] / ch_counts[2] : 0.0;

    if (g_mean < 1e-9) { 
        free(cell_sums); free(cell_counts); free(cell_means);
        return;
    }

    float r_factor = (r_mean > 1e-9) ? (float)(g_mean / r_mean) : 1.0f;
    float b_factor = (b_mean > 1e-9) ? (float)(g_mean / b_mean) : 1.0f;

    #pragma omp parallel for num_threads(threads)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int ch = 1;
            int by = y % block_h;
            int bx = x % block_w;
            
            if (pattern_type == 4) ch = xtrans_map[by][bx];
            else if (pattern_type == 0) ch = (y%2==0) ? ((x%2==0)?0:1) : ((x%2==0)?1:2);
            else if (pattern_type == 1) ch = (y%2==0) ? ((x%2==0)?2:1) : ((x%2==0)?1:0);
            else if (pattern_type == 2) ch = (y%2==0) ? ((x%2==0)?1:0) : ((x%2==0)?2:1);
            else if (pattern_type == 3) ch = (y%2==0) ? ((x%2==0)?1:2) : ((x%2==0)?0:1);
            
            if (ch == 0) image[y * width + x] *= r_factor;
            else if (ch == 2) image[y * width + x] *= b_factor;
        }
    }

    free(cell_sums); free(cell_counts); free(cell_means);
}

void debayer_bilinear_c(const float* src, float* dst, int width, int height,
                        int r_row, int r_col, int b_row, int b_col, int threads) {
    // Input is Mono (src). Output is RGB Interleaved (dst).
    // dst layout: RGBRGBRGB...
    // Stride is 3 * width.
    
    // Ensure we write to INTERLEAVED dst.
    // dst[ (y*width + x)*3 + c ]
    
    #pragma omp parallel for collapse(1) num_threads(threads)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            int out_idx = idx * 3; 
            
            float r_val=0, g_val=0, b_val=0;
            
            bool is_red = (y % 2 == r_row) && (x % 2 == r_col);
            bool is_blue = (y % 2 == b_row) && (x % 2 == b_col);

            if (is_red) {
                r_val = src[idx];
                float g = 0; int gc = 0;
                if (y > 0) { g += src[idx - width]; gc++; }
                if (y < height - 1) { g += src[idx + width]; gc++; }
                if (x > 0) { g += src[idx - 1]; gc++; }
                if (x < width - 1) { g += src[idx + 1]; gc++; }
                g_val = g / gc;
                float b = 0; int bc = 0;
                if (y > 0 && x > 0) { b += src[idx - width - 1]; bc++; }
                if (y > 0 && x < width - 1) { b += src[idx - width + 1]; bc++; }
                if (y < height - 1 && x > 0) { b += src[idx + width - 1]; bc++; }
                if (y < height - 1 && x < width - 1) { b += src[idx + width + 1]; bc++; }
                b_val = b / bc;
            } else if (is_blue) {
                b_val = src[idx];
                float g = 0; int gc = 0;
                if (y > 0) { g += src[idx - width]; gc++; }
                if (y < height - 1) { g += src[idx + width]; gc++; }
                if (x > 0) { g += src[idx - 1]; gc++; }
                if (x < width - 1) { g += src[idx + 1]; gc++; }
                g_val = g / gc;
                float r = 0; int rc = 0;
                if (y > 0 && x > 0) { r += src[idx - width - 1]; rc++; }
                if (y > 0 && x < width - 1) { r += src[idx - width + 1]; rc++; }
                if (y < height - 1 && x > 0) { r += src[idx + width - 1]; rc++; }
                if (y < height - 1 && x < width - 1) { r += src[idx + width + 1]; rc++; }
                r_val = r / rc;
            } else {
                g_val = src[idx];
                if (y % 2 == r_row) {
                    float r = 0; int rc = 0;
                    if (x > 0) { r += src[idx - 1]; rc++; }
                    if (x < width - 1) { r += src[idx + 1]; rc++; }
                    r_val = r / rc;
                    float b = 0; int bc = 0;
                    if (y > 0) { b += src[idx - width]; bc++; }
                    if (y < height - 1) { b += src[idx + width]; bc++; }
                    b_val = b / bc;
                } else {
                    float b = 0; int bc = 0;
                    if (x > 0) { b += src[idx - 1]; bc++; }
                    if (x < width - 1) { b += src[idx + 1]; bc++; }
                    b_val = b / bc;
                    float r = 0; int rc = 0;
                    if (y > 0) { r += src[idx - width]; rc++; }
                    if (y < height - 1) { r += src[idx + width]; rc++; }
                    r_val = r / rc;
                }
            }
            
            dst[out_idx + 0] = r_val;
            dst[out_idx + 1] = g_val;
            dst[out_idx + 2] = b_val;
        }
    }
}

// ============================================================================
// NEW CALIBRATION PRIMITIVES
// ============================================================================

double compute_flat_normalization_c(const float* flat, int width, int height, int channels) {
    if (!flat) return 1.0;
    
    // Center 1/3 Region
    int roi_w = width / 3;
    int roi_h = height / 3;
    int start_x = width / 3;
    int start_y = height / 3;
    int end_x = start_x + roi_w;
    int end_y = start_y + roi_h;
    
    double total_sum = 0.0;
    long long count = 0;
    
    // Iterate over center region
    // Handle Interleaved data: [y][x][c]
    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            long idx = (y * width + x) * channels;
            for (int c = 0; c < channels; ++c) {
                total_sum += flat[idx + c];
                count++;
            }
        }
    }
    
    if (count == 0) return 1.0;
    return total_sum / count;
}

// Calculate Noise (MAD) of Light - K*Dark
static float evaluate_uncalibrated_noise(const float* light, const float* dark, 
                                        int width, int height, int channels,
                                        float k, 
                                        int roi_x, int roi_y, int roi_w, int roi_h) {
    // Only sample the ROI to be fast
    // Compute Diff = Light - K*Dark
    int n_pixels = roi_w * roi_h;
    if (n_pixels <= 0) return 0.0f;
    
    // Limit sample size for speed?
    // Let's use max 50000 pixels
    int step = 1;
    if (n_pixels > 50000) step = n_pixels / 50000;
    
    float* diffs = (float*)malloc((n_pixels / step + 100) * channels * sizeof(float));
    if (!diffs) return 1e9f;
    
    int count = 0;
    
    for (int y = roi_y; y < roi_y + roi_h; ++y) {
        for (int x = roi_x; x < roi_x + roi_w; x += step) {
            long idx = (y * width + x) * channels;
            for (int c = 0; c < channels; ++c) {
                // If interleaved:
                float val = light[idx + c] - k * dark[idx + c];
                diffs[count++] = val;
            }
        }
    }
    
    if (count < 5) {
        free(diffs);
        return 1e9f;
    }
    
    float median = quick_median(diffs, count);
    
    // MAD
    for (int i=0; i<count; ++i) {
        diffs[i] = fabsf(diffs[i] - median);
    }
    float mad = quick_median(diffs, count);
    
    free(diffs);
    
    // Normalize by median intensity? 
    // C++ version: totalNoise += noise / abs(median) if median > 1e-6
    // But here 'median' is median of DIFF. That's approx 0.
    // The C++ logic was median of DIFF (Light - kDark).
    // If median of diff is 0, we just return MAD.
    // Wait, typical background is > 0. 
    // Light ~ Dark + Sky. Light - Dark ~ Sky. Sky > 0.
    // So Median ~ SkyBackground.
    
    if (fabsf(median) > 1e-6f) {
        return mad / fabsf(median);
    }
    return mad;
}

float find_optimal_dark_scale_c(const float* light, const float* dark, 
                                int width, int height, int channels,
                                float k_min, float k_max,
                                float tolerance, int max_iters,
                                int roi_x, int roi_y, int roi_w, int roi_h) {
                                    
    float a = k_min;
    float b = k_max;
    float gr = (sqrtf(5.0f) - 1.0f) / 2.0f;
    
    float c = b - gr * (b - a);
    float d = a + gr * (b - a);
    
    if (roi_w <= 0 || roi_h <= 0) {
        roi_w = 512; roi_h = 512;
        roi_x = (width - roi_w) / 2;
        roi_y = (height - roi_h) / 2;
        if (roi_x < 0) roi_x = 0;
        if (roi_y < 0) roi_y = 0;
        if (roi_w > width) roi_w = width;
        if (roi_h > height) roi_h = height;
    }

    float fc = evaluate_uncalibrated_noise(light, dark, width, height, channels, c, roi_x, roi_y, roi_w, roi_h);
    float fd = evaluate_uncalibrated_noise(light, dark, width, height, channels, d, roi_x, roi_y, roi_w, roi_h);
    
    for (int i = 0; i < max_iters; ++i) {
        if (fabsf(c - d) < tolerance) break;
        
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - gr * (b - a);
            fc = evaluate_uncalibrated_noise(light, dark, width, height, channels, c, roi_x, roi_y, roi_w, roi_h);
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + gr * (b - a);
            fd = evaluate_uncalibrated_noise(light, dark, width, height, channels, d, roi_x, roi_y, roi_w, roi_h);
        }
    }
    
    return (a + b) * 0.5f;
}

void apply_cosmetic_correction_c(float* image, int width, int height, int channels,
                                 const int* bad_pixels_x, const int* bad_pixels_y, int bad_count,
                                 float hot_sigma, float cold_sigma,
                                 int cfa_pattern,
                                 int* hot_fixed, int* cold_fixed,
                                 int threads) {
    if (!image) return;
    int h_fix = 0;
    int c_fix = 0;
    
    // Mode 1: Master Map (Bad Pixels List provided)
    if (bad_pixels_x && bad_pixels_y && bad_count > 0) {
        #pragma omp parallel for reduction(+:h_fix) num_threads(threads)
        for (int i = 0; i < bad_count; ++i) {
            int x = bad_pixels_x[i];
            int y = bad_pixels_y[i];
            
            if (x < 1 || x >= width - 1 || y < 1 || y >= height - 1) continue;
            
            for (int c = 0; c < channels; ++c) {
                // Determine neighbors
                float neighbors[8];
                int n_count = 0;
                
                // If CFA (channels=1 usually, but could be 3 if debayer not done yet? No usually pre-debayer)
                if (channels == 1 && cfa_pattern >= 0) {
                   // Same-color separation is 2
                   // 4 neighbors: (x-2, y), (x+2, y), (x, y-2), (x, y+2)
                   // Check bounds
                   if (x >= 2) neighbors[n_count++] = image[(y * width + (x-2)) * channels + c];
                   if (x < width - 2) neighbors[n_count++] = image[(y * width + (x+2)) * channels + c];
                   if (y >= 2) neighbors[n_count++] = image[((y-2) * width + x) * channels + c];
                   if (y < height - 2) neighbors[n_count++] = image[((y+2) * width + x) * channels + c];
                } else {
                    // Standard 3x3
                    for(int dy=-1; dy<=1; ++dy) {
                        for(int dx=-1; dx<=1; ++dx) {
                            if(dx==0 && dy==0) continue;
                            neighbors[n_count++] = image[((y+dy) * width + (x+dx)) * channels + c];
                        }
                    }
                }
                
                if (n_count > 0) {
                    qsort(neighbors, n_count, sizeof(float), cmp_float);
                    float median;
                    if (n_count % 2 == 0) median = (neighbors[n_count/2 - 1] + neighbors[n_count/2]) * 0.5f;
                    else median = neighbors[n_count/2];
                    
                    image[(y * width + x) * channels + c] = median;
                    h_fix++;
                }
            }
        }
    } 
    // Mode 2: Sigma Clipping (Auto Detection)
    else if (hot_sigma > 0.0f || cold_sigma > 0.0f) {
        
        // Compute Stats per channel
        for (int c = 0; c < channels; ++c) {
            // Subsample for stats
             size_t total = (size_t)width * height;
             size_t step = (total > 500000) ? total / 500000 : 1;
             size_t n_samples = total / step;
             float* info = (float*)malloc(n_samples * sizeof(float));
             if(!info) continue;
             
             size_t k = 0;
             for(size_t i=0; i<total; i+=step) {
                 info[k++] = image[i * channels + c];
             }
             
             float median = quick_median(info, k);
             for(size_t i=0; i<k; ++i) info[i] = fabsf(info[i] - median);
             float mad = quick_median(info, k);
             float sigma_val = mad * 1.4826f;
             free(info);
             
             float h_thresh = median + hot_sigma * sigma_val;
             float c_thresh = median - cold_sigma * sigma_val;
             
             #pragma omp parallel for reduction(+:h_fix, c_fix) num_threads(threads)
             for (int y = 1; y < height - 1; ++y) {
                 for (int x = 1; x < width - 1; ++x) {
                     float val = image[(y*width + x)*channels + c];
                     bool bad = false;
                     if (hot_sigma > 0 && val > h_thresh) { bad = true; h_fix++; }
                     if (cold_sigma > 0 && val < c_thresh) { bad = true; c_fix++; }
                     
                     if (bad) {
                        float neighbors[8];
                        int nx = 0;
                        for(int dy=-1; dy<=1; ++dy) {
                            for(int dx=-1; dx<=1; ++dx) {
                                if(dx==0 && dy==0) continue;
                                neighbors[nx++] = image[((y+dy)*width + (x+dx))*channels + c];
                            }
                        }
                        qsort(neighbors, 8, sizeof(float), cmp_float);
                        image[(y*width + x)*channels + c] = (neighbors[3] + neighbors[4]) * 0.5f;
                     }
                 }
             }
        }
    }
    
    if (hot_fixed) *hot_fixed = h_fix;
    if (cold_fixed) *cold_fixed = c_fix;
}

void fix_xtrans_c(float* image, int width, int height,
                  const char* bay_pattern, const char* model_name,
                  int threads) {
   // Placeholder strictly.
   // X-Trans logic is complex string matching.
   // Given current constraints, we stick to the C++ implementation calling simple C iterators if needed.
   // Or just leave it as C++.
   // The artifact issue is not X-Trans (user has OSC/Mono usually).
   // Leaving empty for now to satisfy linker, will implement if requested.
   (void)image; (void)width; (void)height; (void)bay_pattern; (void)model_name; (void)threads;
}
