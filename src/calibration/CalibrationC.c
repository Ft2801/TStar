/**
 * @file CalibrationC.c
 * @brief High-performance C implementation of image calibration primitives
 *
 * This module provides optimized, OpenMP-parallelized functions for astronomical
 * image calibration. All functions operate on float32 image data and support
 * both single-channel (mono/CFA) and multi-channel (RGB interleaved) formats.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "CalibrationC.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Suppress pedantic warnings for OpenMP pragmas on GCC/Clang */
#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <omp.h>
#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

/* ============================================================================
 * Internal Constants
 * ============================================================================ */

/** Minimum epsilon for division operations to prevent numerical instability */
#define FLAT_EPSILON_FACTOR 0.0001f

/** Minimum sigma value to prevent division by zero in statistical calculations */
#define MIN_SIGMA 1e-6f

/** Sigma threshold multiplier for bad line detection */
#define BAD_LINE_SIGMA_THRESHOLD 8.0f

/** Conversion factor from MAD to standard deviation (for normal distribution) */
#define MAD_TO_SIGMA 1.4826f

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Comparison function for qsort on float arrays
 */
static int compare_float(const void* a, const void* b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;

    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

/**
 * @brief Compute median of a float array (sorts array in-place)
 *
 * @param data Array of float values (will be sorted)
 * @param n    Number of elements
 * @return Median value, or 0.0 if n == 0
 */
static float compute_median(float* data, size_t n)
{
    if (n == 0) {
        return 0.0f;
    }

    qsort(data, n, sizeof(float), compare_float);

    size_t half = n / 2;
    if (n % 2 == 0) {
        return (data[half - 1] + data[half]) * 0.5f;
    }
    return data[half];
}

/**
 * @brief Compute robust sigma estimate using Median Absolute Deviation
 *
 * @param data   Array of float values
 * @param n      Number of elements
 * @param median Pre-computed median of the data
 * @return Sigma estimate (MAD * 1.4826)
 */
static float compute_mad_sigma(float* data, size_t n, float median)
{
    if (n == 0) {
        return MIN_SIGMA;
    }

    /* Compute absolute deviations from median (reusing input array) */
    for (size_t i = 0; i < n; ++i) {
        data[i] = fabsf(data[i] - median);
    }

    float mad = compute_median(data, n);
    float sigma = mad * MAD_TO_SIGMA;

    return (sigma < MIN_SIGMA) ? MIN_SIGMA : sigma;
}

/* ============================================================================
 * Basic Calibration Operations
 * ============================================================================ */

void subtract_bias_c(float* image, const float* bias, size_t size, int threads)
{
    #pragma omp parallel for num_threads(threads)
    for (long long i = 0; i < (long long)size; ++i) {
        float val = image[i] - bias[i];
        image[i] = (val < 0.0f) ? 0.0f : val;
    }
}

void subtract_dark_c(float* image, const float* dark, size_t size,
                     float k, float pedestal, int threads)
{
    #pragma omp parallel for num_threads(threads)
    for (long long i = 0; i < (long long)size; ++i) {
        float val = (image[i] - k * dark[i]) + pedestal;
        image[i] = (val < 0.0f) ? 0.0f : val;
    }
}

void apply_flat_c(float* image, const float* flat, size_t size,
                  float normalization, int threads)
{
    float epsilon = FLAT_EPSILON_FACTOR * normalization;

    #pragma omp parallel for num_threads(threads)
    for (long long i = 0; i < (long long)size; ++i) {
        float flat_val = flat[i];

        /* Exclude dead/un-flat-fieldable regions */
        if (flat_val > epsilon) {
            image[i] *= (normalization / flat_val);
        } else {
            image[i] = 0.0f;
        }
    }
}

/* ============================================================================
 * Deviant Pixel Detection
 * ============================================================================ */

int find_deviant_pixels_c(const float* dark, int width, int height,
                          float hot_sigma, float cold_sigma,
                          int* out_x, int* out_y, int max_output,
                          int cfa_pattern)
{
    /* Validate input parameters */
    if (!dark || !out_x || !out_y || max_output <= 0) {
        return 0;
    }

    int count = 0;

    /* Determine phase dimensions based on CFA pattern */
    int phases_x = (cfa_pattern >= 0) ? 2 : 1;
    int phases_y = (cfa_pattern >= 0) ? 2 : 1;

    /* Allocate working buffers for phase data */
    size_t max_phase_pixels = (size_t)((width / phases_x + 1) * (height / phases_y + 1));
    float* phase_data = (float*)malloc(max_phase_pixels * sizeof(float));
    float* abs_dev = (float*)malloc(max_phase_pixels * sizeof(float));

    if (!phase_data || !abs_dev) {
        free(phase_data);
        free(abs_dev);
        return 0;
    }

    /* Process each CFA phase separately to avoid cross-color contamination */
    for (int py = 0; py < phases_y; ++py) {
        for (int px = 0; px < phases_x; ++px) {

            /* Gather all pixels belonging to this phase */
            size_t n = 0;
            for (int y = py; y < height; y += phases_y) {
                for (int x = px; x < width; x += phases_x) {
                    phase_data[n++] = dark[y * width + x];
                }
            }

            /* Skip if insufficient data for statistics */
            if (n < 5) {
                continue;
            }

            /* Compute robust statistics */
            float median = compute_median(phase_data, n);

            /* Prepare a copy of phase_data for MAD calculation */
            for (size_t i = 0; i < n; ++i) {
                abs_dev[i] = phase_data[i];
            }
            float sigma = compute_mad_sigma(abs_dev, n, median);

            /* Compute detection thresholds */
            float hot_thresh = median + hot_sigma * sigma;
            float cold_thresh = median - cold_sigma * sigma;

            /* Scan for deviant pixels */
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

/* ============================================================================
 * Banding Correction
 * ============================================================================ */

void fix_banding_c(float* image, int width, int height, int channels,
                   int cfa_pattern, int threads)
{
    if (!image) {
        return;
    }

    /* Allocate working buffers */
    float* row_corrections = (float*)malloc(height * sizeof(float));
    size_t total_pixels = (size_t)width * height;
    float* med_buffer = (float*)malloc(total_pixels * sizeof(float));

    if (!row_corrections || !med_buffer) {
        free(row_corrections);
        free(med_buffer);
        return;
    }

    /* Process each color channel independently */
    for (int c = 0; c < channels; ++c) {

        /* Determine CFA phase count (only for mono images with CFA) */
        int phases_y = 1;
        if (channels == 1 && cfa_pattern >= 0) {
            phases_y = (cfa_pattern == 4) ? 6 : 2;  /* X-Trans uses 6x6 pattern */
        }

        float targets[6] = {0};

        /* Compute target background level for each phase */
        for (int p = 0; p < phases_y; ++p) {
            size_t sample_count = 0;

            /* Subsample large images for performance */
            size_t step = (total_pixels > 2000000) ? 10 : 1;

            /* Gather pixels for this channel/phase */
            for (int y = p; y < height; y += phases_y) {
                for (int x = 0; x < width; x += (int)step) {
                    med_buffer[sample_count++] = image[(y * width + x) * channels + c];
                }
            }

            if (sample_count > 0) {
                qsort(med_buffer, sample_count, sizeof(float), compare_float);
                /* Use 1st quartile (25th percentile) to avoid star/nebula contamination */
                targets[p] = med_buffer[sample_count / 4];
            }
        }

        /* Compute per-row corrections using thread-local buffers */
        float* thread_samples = (float*)malloc((size_t)threads * width * sizeof(float));

        if (thread_samples) {
            #pragma omp parallel num_threads(threads)
            {
                int tid = omp_get_thread_num();
                float* local_samples = thread_samples + (size_t)tid * width;

                #pragma omp for
                for (int y = 0; y < height; ++y) {
                    int phase = y % phases_y;

                    /* Gather row samples */
                    for (int x = 0; x < width; ++x) {
                        local_samples[x] = image[(y * width + x) * channels + c];
                    }

                    /* Compute row 1st quartile */
                    qsort(local_samples, width, sizeof(float), compare_float);
                    float row_q1 = local_samples[width / 4];

                    /* Correction brings row background to target level */
                    row_corrections[y] = targets[phase] - row_q1;
                }
            }
            free(thread_samples);
        }

        /* Apply row corrections */
        #pragma omp parallel for num_threads(threads)
        for (int y = 0; y < height; ++y) {
            float correction = row_corrections[y];
            for (int x = 0; x < width; ++x) {
                image[(y * width + x) * channels + c] += correction;
            }
        }
    }

    free(row_corrections);
    free(med_buffer);
}

/* ============================================================================
 * Bad Line Correction
 * ============================================================================ */

/**
 * @brief Internal helper to detect and fix bad horizontal lines for one channel
 */
static void fix_horizontal_bad_lines(float* image, int width, int height,
                                     int channels, int channel,
                                     int phases_y, int threads)
{
    /* Compute median for each row */
    float* row_meds = (float*)malloc(height * sizeof(float));
    if (!row_meds) {
        return;
    }

    #pragma omp parallel for num_threads(threads)
    for (int y = 0; y < height; ++y) {
        float* samples = (float*)malloc(width * sizeof(float));
        if (samples) {
            for (int x = 0; x < width; ++x) {
                samples[x] = image[(y * width + x) * channels + channel];
            }
            qsort(samples, width, sizeof(float), compare_float);
            row_meds[y] = samples[width / 2];
            free(samples);
        } else {
            row_meds[y] = 0;
        }
    }

    /* Process each phase */
    for (int p = 0; p < phases_y; ++p) {

        /* Count rows in this phase */
        int count = 0;
        for (int y = p; y < height; y += phases_y) {
            count++;
        }

        if (count < 7) {
            continue;
        }

        /* Allocate working arrays */
        float* p_vals = (float*)malloc(count * sizeof(float));
        float* abs_res = (float*)malloc((count - 2) * sizeof(float));
        unsigned char* is_bad = (unsigned char*)calloc((size_t)count, sizeof(unsigned char));

        if (!p_vals || !abs_res || !is_bad) {
            free(p_vals);
            free(abs_res);
            free(is_bad);
            continue;
        }

        /* Gather phase row medians */
        int idx = 0;
        for (int y = p; y < height; y += phases_y) {
            p_vals[idx++] = row_meds[y];
        }

        /* Compute high-pass residuals (local trend removed) */
        for (int i = 1; i < count - 1; ++i) {
            float local_ref = 0.5f * (p_vals[i - 1] + p_vals[i + 1]);
            abs_res[i - 1] = fabsf(p_vals[i] - local_ref);
        }

        /* Compute MAD-based threshold */
        qsort(abs_res, (size_t)(count - 2), sizeof(float), compare_float);
        float mad = abs_res[(count - 2) / 2];
        float sigma = mad * MAD_TO_SIGMA;
        if (sigma < MIN_SIGMA) {
            sigma = MIN_SIGMA;
        }

        float thresh = BAD_LINE_SIGMA_THRESHOLD * sigma;

        /* Mark bad lines */
        for (int i = 1; i < count - 1; ++i) {
            float local_ref = 0.5f * (p_vals[i - 1] + p_vals[i + 1]);
            if (fabsf(p_vals[i] - local_ref) > thresh) {
                is_bad[i] = 1;
            }
        }

        /* Interpolate bad lines */
        for (int i = 1; i < count - 1; ++i) {
            if (!is_bad[i]) {
                continue;
            }

            /* Find nearest good neighbors */
            int prev_i = i - 1;
            int next_i = i + 1;
            while (prev_i >= 0 && is_bad[prev_i]) prev_i--;
            while (next_i < count && is_bad[next_i]) next_i++;

            int y = p + i * phases_y;
            int prev_y = (prev_i >= 0) ? (p + prev_i * phases_y) : -1;
            int next_y = (next_i < count) ? (p + next_i * phases_y) : -1;

            /* Interpolate each pixel in the line */
            for (int x = 0; x < width; ++x) {
                float val1 = (prev_y >= 0) ?
                    image[(prev_y * width + x) * channels + channel] : -1.0f;
                float val2 = (next_y >= 0) ?
                    image[(next_y * width + x) * channels + channel] : -1.0f;

                if (val1 >= 0.0f && val2 >= 0.0f) {
                    image[(y * width + x) * channels + channel] = 0.5f * (val1 + val2);
                } else if (val1 >= 0.0f) {
                    image[(y * width + x) * channels + channel] = val1;
                } else if (val2 >= 0.0f) {
                    image[(y * width + x) * channels + channel] = val2;
                }
            }
        }

        free(p_vals);
        free(abs_res);
        free(is_bad);
    }

    free(row_meds);
}

/**
 * @brief Internal helper to detect and fix bad vertical lines for one channel
 */
static void fix_vertical_bad_lines(float* image, int width, int height,
                                   int channels, int channel,
                                   int phases_x, int threads)
{
    /* Compute median for each column */
    float* col_meds = (float*)malloc(width * sizeof(float));
    if (!col_meds) {
        return;
    }

    #pragma omp parallel for num_threads(threads)
    for (int x = 0; x < width; ++x) {
        float* samples = (float*)malloc(height * sizeof(float));
        if (samples) {
            for (int y = 0; y < height; ++y) {
                samples[y] = image[(y * width + x) * channels + channel];
            }
            qsort(samples, height, sizeof(float), compare_float);
            col_meds[x] = samples[height / 2];
            free(samples);
        } else {
            col_meds[x] = 0;
        }
    }

    /* Process each phase */
    for (int p = 0; p < phases_x; ++p) {

        /* Count columns in this phase */
        int count = 0;
        for (int x = p; x < width; x += phases_x) {
            count++;
        }

        if (count < 7) {
            continue;
        }

        /* Allocate working arrays */
        float* p_vals = (float*)malloc(count * sizeof(float));
        float* abs_res = (float*)malloc((count - 2) * sizeof(float));
        unsigned char* is_bad = (unsigned char*)calloc((size_t)count, sizeof(unsigned char));

        if (!p_vals || !abs_res || !is_bad) {
            free(p_vals);
            free(abs_res);
            free(is_bad);
            continue;
        }

        /* Gather phase column medians */
        int idx = 0;
        for (int x = p; x < width; x += phases_x) {
            p_vals[idx++] = col_meds[x];
        }

        /* Compute high-pass residuals */
        for (int i = 1; i < count - 1; ++i) {
            float local_ref = 0.5f * (p_vals[i - 1] + p_vals[i + 1]);
            abs_res[i - 1] = fabsf(p_vals[i] - local_ref);
        }

        /* Compute MAD-based threshold */
        qsort(abs_res, (size_t)(count - 2), sizeof(float), compare_float);
        float mad = abs_res[(count - 2) / 2];
        float sigma = mad * MAD_TO_SIGMA;
        if (sigma < MIN_SIGMA) {
            sigma = MIN_SIGMA;
        }

        float thresh = BAD_LINE_SIGMA_THRESHOLD * sigma;

        /* Mark bad columns */
        for (int i = 1; i < count - 1; ++i) {
            float local_ref = 0.5f * (p_vals[i - 1] + p_vals[i + 1]);
            if (fabsf(p_vals[i] - local_ref) > thresh) {
                is_bad[i] = 1;
            }
        }

        /* Interpolate bad columns */
        for (int i = 1; i < count - 1; ++i) {
            if (!is_bad[i]) {
                continue;
            }

            /* Find nearest good neighbors */
            int prev_i = i - 1;
            int next_i = i + 1;
            while (prev_i >= 0 && is_bad[prev_i]) prev_i--;
            while (next_i < count && is_bad[next_i]) next_i++;

            int x = p + i * phases_x;
            int prev_x = (prev_i >= 0) ? (p + prev_i * phases_x) : -1;
            int next_x = (next_i < count) ? (p + next_i * phases_x) : -1;

            /* Interpolate each pixel in the column */
            for (int y = 0; y < height; ++y) {
                float val1 = (prev_x >= 0) ?
                    image[(y * width + prev_x) * channels + channel] : -1.0f;
                float val2 = (next_x >= 0) ?
                    image[(y * width + next_x) * channels + channel] : -1.0f;

                if (val1 >= 0.0f && val2 >= 0.0f) {
                    image[(y * width + x) * channels + channel] = 0.5f * (val1 + val2);
                } else if (val1 >= 0.0f) {
                    image[(y * width + x) * channels + channel] = val1;
                } else if (val2 >= 0.0f) {
                    image[(y * width + x) * channels + channel] = val2;
                }
            }
        }

        free(p_vals);
        free(abs_res);
        free(is_bad);
    }

    free(col_meds);
}

void fix_bad_lines_c(float* image, int width, int height, int channels,
                     int cfa_pattern, int threads)
{
    if (!image) {
        return;
    }

    for (int c = 0; c < channels; ++c) {

        /* Determine phase count based on CFA pattern */
        int phases_y = 1;
        int phases_x = 1;

        if (channels == 1 && cfa_pattern >= 0) {
            phases_y = (cfa_pattern == 4) ? 6 : 2;
            phases_x = phases_y;
        }

        /* Fix horizontal bad lines */
        fix_horizontal_bad_lines(image, width, height, channels, c, phases_y, threads);

        /* Fix vertical bad lines */
        fix_vertical_bad_lines(image, width, height, channels, c, phases_x, threads);
    }
}

/* ============================================================================
 * CFA Equalization
 * ============================================================================ */

void equalize_cfa_c(float* image, int width, int height, int pattern_type, int threads)
{
    if (!image) {
        return;
    }

    /* Determine CFA block dimensions */
    int block_w = (pattern_type == 4) ? 6 : 2;
    int block_h = (pattern_type == 4) ? 6 : 2;
    int num_cells = block_w * block_h;

    /* X-Trans 6x6 color map: 0=G, 1=R, 2=B */
    static const int xtrans_map[6][6] = {
        {0, 1, 0, 0, 2, 0},
        {1, 0, 1, 2, 0, 2},
        {0, 1, 0, 0, 2, 0},
        {0, 2, 0, 0, 1, 0},
        {2, 0, 2, 1, 0, 1},
        {0, 2, 0, 0, 1, 0}
    };

    /* Allocate statistics arrays */
    double* cell_sums = (double*)calloc(num_cells, sizeof(double));
    long long* cell_counts = (long long*)calloc(num_cells, sizeof(long long));
    double* cell_means = (double*)malloc(num_cells * sizeof(double));

    if (!cell_sums || !cell_counts || !cell_means) {
        free(cell_sums);
        free(cell_counts);
        free(cell_means);
        return;
    }

    /* Sample center 1/3 region to avoid vignetting */
    int start_x = width / 3;
    int start_y = height / 3;
    int end_x = 2 * width / 3;
    int end_y = 2 * height / 3;

    /* Accumulate sums per CFA cell position */
    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            int cell_idx = (y % block_h) * block_w + (x % block_w);
            cell_sums[cell_idx] += image[y * width + x];
            cell_counts[cell_idx]++;
        }
    }

    /* Compute cell means */
    for (int i = 0; i < num_cells; ++i) {
        cell_means[i] = (cell_counts[i] > 0) ?
            (cell_sums[i] / cell_counts[i]) : 0.0;
    }

    /* Compute per-channel means */
    double ch_sums[3] = {0.0, 0.0, 0.0};
    int ch_counts[3] = {0, 0, 0};

    for (int y = 0; y < block_h; ++y) {
        for (int x = 0; x < block_w; ++x) {
            int cell_idx = y * block_w + x;
            int ch = 1;  /* Default to green */

            if (pattern_type == 4) {
                ch = xtrans_map[y][x];
            } else if (pattern_type == 0) {  /* RGGB */
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 0 : 1) : ((x % 2 == 0) ? 1 : 2);
            } else if (pattern_type == 1) {  /* BGGR */
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 2 : 1) : ((x % 2 == 0) ? 1 : 0);
            } else if (pattern_type == 2) {  /* GRBG */
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 1 : 0) : ((x % 2 == 0) ? 2 : 1);
            } else if (pattern_type == 3) {  /* GBRG */
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 1 : 2) : ((x % 2 == 0) ? 0 : 1);
            }

            if (cell_counts[cell_idx] > 0) {
                ch_sums[ch] += cell_means[cell_idx];
                ch_counts[ch]++;
            }
        }
    }

    /* Compute channel averages */
    double r_mean = (ch_counts[0] > 0) ? ch_sums[0] / ch_counts[0] : 0.0;
    double g_mean = (ch_counts[1] > 0) ? ch_sums[1] / ch_counts[1] : 0.0;
    double b_mean = (ch_counts[2] > 0) ? ch_sums[2] / ch_counts[2] : 0.0;

    /* Check for valid green mean */
    if (g_mean < 1e-9) {
        free(cell_sums);
        free(cell_counts);
        free(cell_means);
        return;
    }

    /* Compute equalization factors (normalize R and B to G) */
    float r_factor = (r_mean > 1e-9) ? (float)(g_mean / r_mean) : 1.0f;
    float b_factor = (b_mean > 1e-9) ? (float)(g_mean / b_mean) : 1.0f;

    /* Apply equalization */
    #pragma omp parallel for num_threads(threads)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int by = y % block_h;
            int bx = x % block_w;
            int ch = 1;

            if (pattern_type == 4) {
                ch = xtrans_map[by][bx];
            } else if (pattern_type == 0) {
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 0 : 1) : ((x % 2 == 0) ? 1 : 2);
            } else if (pattern_type == 1) {
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 2 : 1) : ((x % 2 == 0) ? 1 : 0);
            } else if (pattern_type == 2) {
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 1 : 0) : ((x % 2 == 0) ? 2 : 1);
            } else if (pattern_type == 3) {
                ch = (y % 2 == 0) ? ((x % 2 == 0) ? 1 : 2) : ((x % 2 == 0) ? 0 : 1);
            }

            if (ch == 0) {
                image[y * width + x] *= r_factor;
            } else if (ch == 2) {
                image[y * width + x] *= b_factor;
            }
        }
    }

    free(cell_sums);
    free(cell_counts);
    free(cell_means);
}

/* ============================================================================
 * Bilinear Debayering
 * ============================================================================ */

void debayer_bilinear_c(const float* src, float* dst, int width, int height,
                        int r_row, int r_col, int b_row, int b_col, int threads)
{
    #pragma omp parallel for num_threads(threads)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int src_idx = y * width + x;
            int dst_idx = src_idx * 3;

            float r_val = 0.0f;
            float g_val = 0.0f;
            float b_val = 0.0f;

            bool is_red = (y % 2 == r_row) && (x % 2 == r_col);
            bool is_blue = (y % 2 == b_row) && (x % 2 == b_col);

            if (is_red) {
                /* Red pixel: sample R directly, interpolate G and B */
                r_val = src[src_idx];

                /* Green from 4-neighbors */
                float g_sum = 0.0f;
                int g_count = 0;
                if (y > 0)          { g_sum += src[src_idx - width]; g_count++; }
                if (y < height - 1) { g_sum += src[src_idx + width]; g_count++; }
                if (x > 0)          { g_sum += src[src_idx - 1];     g_count++; }
                if (x < width - 1)  { g_sum += src[src_idx + 1];     g_count++; }
                g_val = g_sum / g_count;

                /* Blue from diagonal neighbors */
                float b_sum = 0.0f;
                int b_count = 0;
                if (y > 0 && x > 0)              { b_sum += src[src_idx - width - 1]; b_count++; }
                if (y > 0 && x < width - 1)      { b_sum += src[src_idx - width + 1]; b_count++; }
                if (y < height - 1 && x > 0)     { b_sum += src[src_idx + width - 1]; b_count++; }
                if (y < height - 1 && x < width - 1) { b_sum += src[src_idx + width + 1]; b_count++; }
                b_val = b_sum / b_count;

            } else if (is_blue) {
                /* Blue pixel: sample B directly, interpolate G and R */
                b_val = src[src_idx];

                /* Green from 4-neighbors */
                float g_sum = 0.0f;
                int g_count = 0;
                if (y > 0)          { g_sum += src[src_idx - width]; g_count++; }
                if (y < height - 1) { g_sum += src[src_idx + width]; g_count++; }
                if (x > 0)          { g_sum += src[src_idx - 1];     g_count++; }
                if (x < width - 1)  { g_sum += src[src_idx + 1];     g_count++; }
                g_val = g_sum / g_count;

                /* Red from diagonal neighbors */
                float r_sum = 0.0f;
                int r_count = 0;
                if (y > 0 && x > 0)              { r_sum += src[src_idx - width - 1]; r_count++; }
                if (y > 0 && x < width - 1)      { r_sum += src[src_idx - width + 1]; r_count++; }
                if (y < height - 1 && x > 0)     { r_sum += src[src_idx + width - 1]; r_count++; }
                if (y < height - 1 && x < width - 1) { r_sum += src[src_idx + width + 1]; r_count++; }
                r_val = r_sum / r_count;

            } else {
                /* Green pixel: sample G directly, interpolate R and B */
                g_val = src[src_idx];

                if (y % 2 == r_row) {
                    /* Green in red row: R from horizontal, B from vertical */
                    float r_sum = 0.0f;
                    int r_count = 0;
                    if (x > 0)         { r_sum += src[src_idx - 1]; r_count++; }
                    if (x < width - 1) { r_sum += src[src_idx + 1]; r_count++; }
                    r_val = r_sum / r_count;

                    float b_sum = 0.0f;
                    int b_count = 0;
                    if (y > 0)          { b_sum += src[src_idx - width]; b_count++; }
                    if (y < height - 1) { b_sum += src[src_idx + width]; b_count++; }
                    b_val = b_sum / b_count;
                } else {
                    /* Green in blue row: B from horizontal, R from vertical */
                    float b_sum = 0.0f;
                    int b_count = 0;
                    if (x > 0)         { b_sum += src[src_idx - 1]; b_count++; }
                    if (x < width - 1) { b_sum += src[src_idx + 1]; b_count++; }
                    b_val = b_sum / b_count;

                    float r_sum = 0.0f;
                    int r_count = 0;
                    if (y > 0)          { r_sum += src[src_idx - width]; r_count++; }
                    if (y < height - 1) { r_sum += src[src_idx + width]; r_count++; }
                    r_val = r_sum / r_count;
                }
            }

            /* Write interleaved RGB output */
            dst[dst_idx + 0] = r_val;
            dst[dst_idx + 1] = g_val;
            dst[dst_idx + 2] = b_val;
        }
    }
}

/* ============================================================================
 * Flat Normalization Computation
 * ============================================================================ */

double compute_flat_normalization_c(const float* flat, int width, int height, int channels)
{
    if (!flat) {
        return 1.0;
    }

    /* Define center 1/3 region to avoid vignetting at edges */
    int roi_w = width / 3;
    int roi_h = height / 3;
    int start_x = width / 3;
    int start_y = height / 3;
    int end_x = start_x + roi_w;
    int end_y = start_y + roi_h;

    double total_sum = 0.0;
    long long count = 0;

    /* Iterate over center region for all channels */
    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            long idx = (y * width + x) * channels;
            for (int c = 0; c < channels; ++c) {
                float val = flat[idx + c];
                if (val > 0.0f) {
                    total_sum += val;
                    count++;
                }
            }
        }
    }

    if (count == 0) {
        return 1.0;
    }

    return total_sum / count;
}

/* ============================================================================
 * Dark Scale Optimization
 * ============================================================================ */

/**
 * @brief Evaluate noise level for a given dark scaling factor
 *
 * Computes normalized MAD of (Light - k*Dark) over the ROI.
 * Lower values indicate better dark subtraction.
 */
static float evaluate_dark_noise(const float* light, const float* dark,
                                 int width, int height, int channels,
                                 float k,
                                 int roi_x, int roi_y, int roi_w, int roi_h)
{
    (void)height;  /* Unused parameter */

    int n_pixels = roi_w * roi_h;
    if (n_pixels <= 0) {
        return 0.0f;
    }

    /* Limit sample size for performance */
    int step = 1;
    if (n_pixels > 50000) {
        step = n_pixels / 50000;
    }

    float* diffs = (float*)malloc((n_pixels / step + 100) * channels * sizeof(float));
    if (!diffs) {
        return 1e9f;
    }

    int count = 0;

    /* Sample differences over ROI */
    for (int y = roi_y; y < roi_y + roi_h; ++y) {
        for (int x = roi_x; x < roi_x + roi_w; x += step) {
            long idx = (y * width + x) * channels;
            for (int c = 0; c < channels; ++c) {
                float val = light[idx + c] - k * dark[idx + c];
                diffs[count++] = val;
            }
        }
    }

    if (count < 5) {
        free(diffs);
        return 1e9f;
    }

    /* Compute median of differences (sky background level) */
    float median = compute_median(diffs, count);

    /* Compute MAD for noise estimate */
    for (int i = 0; i < count; ++i) {
        diffs[i] = fabsf(diffs[i] - median);
    }
    float mad = compute_median(diffs, count);

    free(diffs);

    /* Return normalized noise (relative to background level) */
    if (fabsf(median) > MIN_SIGMA) {
        return mad / fabsf(median);
    }
    return mad;
}

float find_optimal_dark_scale_c(const float* light, const float* dark,
                                int width, int height, int channels,
                                float k_min, float k_max,
                                float tolerance, int max_iters,
                                int roi_x, int roi_y, int roi_w, int roi_h)
{
    /* Golden ratio for golden section search */
    const float golden_ratio = (sqrtf(5.0f) - 1.0f) / 2.0f;

    float a = k_min;
    float b = k_max;

    float c = b - golden_ratio * (b - a);
    float d = a + golden_ratio * (b - a);

    /* Clamp ROI to image bounds */
    if (roi_x < 0) roi_x = 0;
    if (roi_y < 0) roi_y = 0;
    if (roi_x + roi_w > width)  roi_w = width - roi_x;
    if (roi_y + roi_h > height) roi_h = height - roi_y;

    /* Evaluate initial bracket points */
    float fc = evaluate_dark_noise(light, dark, width, height, channels,
                                   c, roi_x, roi_y, roi_w, roi_h);
    float fd = evaluate_dark_noise(light, dark, width, height, channels,
                                   d, roi_x, roi_y, roi_w, roi_h);

    /* Golden section search iteration */
    for (int i = 0; i < max_iters; ++i) {
        if (fabsf(c - d) < tolerance) {
            break;
        }

        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - golden_ratio * (b - a);
            fc = evaluate_dark_noise(light, dark, width, height, channels,
                                     c, roi_x, roi_y, roi_w, roi_h);
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + golden_ratio * (b - a);
            fd = evaluate_dark_noise(light, dark, width, height, channels,
                                     d, roi_x, roi_y, roi_w, roi_h);
        }
    }

    return (a + b) * 0.5f;
}

/* ============================================================================
 * Cosmetic Correction
 * ============================================================================ */

void apply_cosmetic_correction_c(float* image, int width, int height, int channels,
                                 const int* bad_pixels_x, const int* bad_pixels_y,
                                 int bad_count,
                                 float hot_sigma, float cold_sigma,
                                 int cfa_pattern,
                                 int* hot_fixed, int* cold_fixed,
                                 int threads)
{
    if (!image) {
        return;
    }

    int h_fix = 0;
    int c_fix = 0;

    /* Mode 1: Master bad pixel map provided */
    if (bad_pixels_x && bad_pixels_y && bad_count > 0) {

        #pragma omp parallel for reduction(+:h_fix) num_threads(threads)
        for (int i = 0; i < bad_count; ++i) {
            int x = bad_pixels_x[i];
            int y = bad_pixels_y[i];

            /* Skip edge pixels */
            if (x < 1 || x >= width - 1 || y < 1 || y >= height - 1) {
                continue;
            }

            for (int c = 0; c < channels; ++c) {
                float neighbors[8];
                int n_count = 0;

                /* For CFA images, use same-color neighbors (distance 2) */
                if (channels == 1 && cfa_pattern >= 0) {
                    if (x >= 2) {
                        neighbors[n_count++] = image[(y * width + (x - 2)) * channels + c];
                    }
                    if (x < width - 2) {
                        neighbors[n_count++] = image[(y * width + (x + 2)) * channels + c];
                    }
                    if (y >= 2) {
                        neighbors[n_count++] = image[((y - 2) * width + x) * channels + c];
                    }
                    if (y < height - 2) {
                        neighbors[n_count++] = image[((y + 2) * width + x) * channels + c];
                    }
                } else {
                    /* Standard 3x3 neighborhood for RGB or non-CFA */
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            neighbors[n_count++] = image[((y + dy) * width + (x + dx)) * channels + c];
                        }
                    }
                }

                /* Replace with median of neighbors */
                if (n_count > 0) {
                    qsort(neighbors, n_count, sizeof(float), compare_float);
                    float median;
                    if (n_count % 2 == 0) {
                        median = (neighbors[n_count / 2 - 1] + neighbors[n_count / 2]) * 0.5f;
                    } else {
                        median = neighbors[n_count / 2];
                    }

                    image[(y * width + x) * channels + c] = median;
                    h_fix++;
                }
            }
        }
    }
    /* Mode 2: Auto-detection via sigma clipping */
    else if (hot_sigma > 0.0f || cold_sigma > 0.0f) {

        for (int c = 0; c < channels; ++c) {
            /* Subsample for statistics computation */
            size_t total = (size_t)width * height;
            size_t step = (total > 500000) ? total / 500000 : 1;
            size_t n_samples = total / step;

            float* samples = (float*)malloc(n_samples * sizeof(float));
            if (!samples) {
                continue;
            }

            size_t k = 0;
            for (size_t i = 0; i < total; i += step) {
                samples[k++] = image[i * channels + c];
            }

            /* Compute robust statistics */
            float median = compute_median(samples, k);

            for (size_t i = 0; i < k; ++i) {
                samples[i] = fabsf(samples[i] - median);
            }
            float mad = compute_median(samples, k);
            float sigma_val = mad * MAD_TO_SIGMA;
            free(samples);

            /* Compute detection thresholds */
            float h_thresh = median + hot_sigma * sigma_val;
            float c_thresh = median - cold_sigma * sigma_val;

            /* Detect and fix outliers */
            #pragma omp parallel for reduction(+:h_fix, c_fix) num_threads(threads)
            for (int y = 1; y < height - 1; ++y) {
                for (int x = 1; x < width - 1; ++x) {
                    float val = image[(y * width + x) * channels + c];
                    bool is_bad = false;

                    if (hot_sigma > 0 && val > h_thresh) {
                        is_bad = true;
                        h_fix++;
                    }
                    if (cold_sigma > 0 && val < c_thresh) {
                        is_bad = true;
                        c_fix++;
                    }

                    if (is_bad) {
                        /* Gather 8-neighbors */
                        float neighbors[8];
                        int nx = 0;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0) continue;
                                neighbors[nx++] = image[((y + dy) * width + (x + dx)) * channels + c];
                            }
                        }

                        /* Replace with median */
                        qsort(neighbors, 8, sizeof(float), compare_float);
                        image[(y * width + x) * channels + c] = (neighbors[3] + neighbors[4]) * 0.5f;
                    }
                }
            }
        }
    }

    /* Return fix counts */
    if (hot_fixed)  *hot_fixed = h_fix;
    if (cold_fixed) *cold_fixed = c_fix;
}

/* ============================================================================
 * X-Trans Artifact Correction
 * ============================================================================ */

void fix_xtrans_c(float* image, int width, int height,
                  const char* bay_pattern, const char* model_name,
                  int threads)
{
    if (!image || !bay_pattern) {
        return;
    }

    /* Standard X-Trans 6x6 color map: 0=G, 1=R, 2=B */
    static const int std_xtrans[6][6] = {
        {0, 1, 0, 0, 2, 0},
        {1, 0, 1, 2, 0, 2},
        {0, 1, 0, 0, 2, 0},
        {0, 2, 0, 0, 1, 0},
        {2, 0, 2, 1, 0, 1},
        {0, 2, 0, 0, 1, 0}
    };

    int xt_map[6][6];
    int pattern_len = (int)strlen(bay_pattern);

    /* Parse pattern string if available (36 characters for 6x6) */
    if (pattern_len >= 36) {
        for (int i = 0; i < 36; ++i) {
            char c = bay_pattern[i];
            int color = 0;  /* Default to Green */
            if (c == 'R' || c == 'r') color = 1;
            else if (c == 'B' || c == 'b') color = 2;
            xt_map[i / 6][i % 6] = color;
        }
    } else {
        /* Fall back to standard pattern */
        memcpy(xt_map, std_xtrans, sizeof(xt_map));

        #ifdef _DEBUG
        if (model_name) {
            printf("[CalibrationC] Using default X-Trans pattern for %s\n", model_name);
        }
        #endif
        (void)model_name;  /* Suppress unused warning in release */
    }

    /* Detect and fix outlier pixels using same-color neighbors in 7x7 window */
    #pragma omp parallel for collapse(2) num_threads(threads)
    for (int y = 3; y < height - 3; ++y) {
        for (int x = 3; x < width - 3; ++x) {
            float val = image[y * width + x];
            int color = xt_map[y % 6][x % 6];

            /* Collect same-color neighbors in 7x7 window */
            float neighbors[24];
            int count = 0;

            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (xt_map[(y + dy) % 6][(x + dx) % 6] == color) {
                        if (count < 24) {
                            neighbors[count++] = image[(y + dy) * width + (x + dx)];
                        }
                    }
                }
            }

            /* Require minimum neighbors for statistics */
            if (count >= 4) {
                qsort(neighbors, count, sizeof(float), compare_float);

                float median;
                if (count % 2 == 0) {
                    median = (neighbors[count / 2 - 1] + neighbors[count / 2]) * 0.5f;
                } else {
                    median = neighbors[count / 2];
                }

                /* Compute MAD-based sigma */
                float mad_sum = 0.0f;
                for (int i = 0; i < count; ++i) {
                    mad_sum += fabsf(neighbors[i] - median);
                }
                float sigma = (mad_sum / count) * MAD_TO_SIGMA;
                if (sigma < MIN_SIGMA) {
                    sigma = MIN_SIGMA;
                }

                /* Replace if > 8 sigma outlier (likely hot/cold AF pixel) */
                if (fabsf(val - median) > BAD_LINE_SIGMA_THRESHOLD * sigma) {
                    image[y * width + x] = median;
                }
            }
        }
    }
}