#ifndef CALIBRATION_C_H
#define CALIBRATION_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Subtract bias frame from image
 */
void subtract_bias_c(float* image, const float* bias, size_t size, int threads);

/**
 * @brief Subtract dark frame with scaling factor and pedestal
 */
void subtract_dark_c(float* image, const float* dark, size_t size, float k, float pedestal, int threads);

/**
 * @brief Apply master flat frame
 */
void apply_flat_c(float* image, const float* flat, size_t size, float normalization, int threads);

/**
 * @brief Find deviant pixels in dark frame (hot/cold pixels)
 * Returns number of pixels found. Coordinates stored in out_x, out_y up to max_pixels.
 */
// cfa_pattern: -1=None, 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG
int find_deviant_pixels_c(const float* dark, int width, int height, 
                           float hot_sigma, float cold_sigma,
                           int* out_x, int* out_y, int max_output,
                           int cfa_pattern);

/**
 * @brief Remove Canon banding artifacts (horizontal/vertical lines)
 */
void fix_banding_c(float* image, int width, int height, int channels, int cfa_pattern, int threads);

/**
 * @brief Equalize CFA channels for flats (balance RGB before debayer)
 * Supports generic Bayer patterns (2x2) and X-Trans (6x6).
 * pattern_type: 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG, 4=XTRANS
 */
void equalize_cfa_c(float* image, int width, int height, int pattern_type, int threads);

/**
 * @brief Fast bilinear debayering
 */
void debayer_bilinear_c(const float* src, float* dst, int width, int height,
                        int r_row, int r_col, int b_row, int b_col, int threads);

/**
 * @brief Compute Flat Normalization factor (mean of center 1/3)
 */
double compute_flat_normalization_c(const float* flat, int width, int height, int channels);

/**
 * @brief Find Optimal Dark Scaling Factor (Golden Section Search)
 * Returns the scaling factor K.
 */
float find_optimal_dark_scale_c(const float* light, const float* dark, 
                                int width, int height, int channels,
                                float k_min, float k_max,
                                float tolerance, int max_iters,
                                int roi_x, int roi_y, int roi_w, int roi_h);

/**
 * @brief Apply Cosmetic Correction (Fix Hot/Cold pixels)
 * Can use a master map (via is_bad_map) or Sigma Clipping (if hot_sigma > 0).
 */
void apply_cosmetic_correction_c(float* image, int width, int height, int channels,
                                 const int* bad_pixels_x, const int* bad_pixels_y, int bad_count,
                                 float hot_sigma, float cold_sigma,
                                 int cfa_pattern,
                                 int* hot_fixed, int* cold_fixed,
                                 int threads);

/**
 * @brief Fix X-Trans AF Pixel Artifacts
 */
void fix_xtrans_c(float* image, int width, int height,
                  const char* bay_pattern, const char* model_name,
                  int threads);

#ifdef __cplusplus
}
#endif

#endif // CALIBRATION_C_H
