/**
 * @file CosmeticCorrection.cpp
 * @brief Implementation of hot/cold pixel detection and correction.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "CosmeticCorrection.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Stacking {

// =============================================================================
// Robust Statistics Helper
// =============================================================================

void CosmeticCorrection::computeStats(const ImageBuffer& img,
                                      float& median, float& sigma)
{
    if (!img.isValid()) return;

    const size_t count = static_cast<size_t>(img.width()) * img.height();
    const float* data  = img.data().data();

    std::vector<float> pixels(data, data + count);
    const size_t n = pixels.size();
    if (n == 0) return;

    // Compute median.
    std::sort(pixels.begin(), pixels.end());
    median = (n % 2 == 0)
           ? (pixels[n / 2 - 1] + pixels[n / 2]) * 0.5f
           : pixels[n / 2];

    // Compute MAD (Median Absolute Deviation).
    std::vector<float> deviations(n);
    for (size_t i = 0; i < n; ++i) {
        deviations[i] = std::abs(pixels[i] - median);
    }
    std::sort(deviations.begin(), deviations.end());

    float mad = (n % 2 == 0)
              ? (deviations[n / 2 - 1] + deviations[n / 2]) * 0.5f
              : deviations[n / 2];

    // Convert MAD to Gaussian-equivalent sigma: sigma ~ 1.4826 * MAD.
    sigma = mad * 1.4826f;
}

// =============================================================================
// Defect Detection
// =============================================================================

CosmeticMap CosmeticCorrection::findDefects(const ImageBuffer& dark,
                                            float hotSigma, float coldSigma,
                                            bool cfa)
{
    CosmeticMap map;
    if (!dark.isValid()) return map;

    map.width  = dark.width();
    map.height = dark.height();
    const size_t size = static_cast<size_t>(map.width) * map.height;
    map.hotPixels.resize(size, false);
    map.coldPixels.resize(size, false);

    // Stage 1: Global threshold from overall image statistics.
    float globalMedian = 0.0f;
    float globalSigma  = 0.0f;
    computeStats(dark, globalMedian, globalSigma);
    if (globalSigma <= 1e-9f) globalSigma = 1e-9f;

    const float globalHotThresh  = globalMedian + hotSigma  * globalSigma;
    const float globalColdThresh = globalMedian - coldSigma * globalSigma;

    const float* data = dark.data().data();
    const int width   = dark.width();
    const int height  = dark.height();
    const int step    = cfa ? 2 : 1;
    const int radius  = 2 * step;   // 5x5 window in CFA-aware mode.

    int defects = 0;

    // Stage 2: Local validation of global candidates.
    #pragma omp parallel for reduction(+:defects)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t i   = static_cast<size_t>(y) * width + x;
            const float  val = data[i];

            const bool potentialHot  = (val > globalHotThresh);
            const bool potentialCold = (val < globalColdThresh);
            if (!potentialHot && !potentialCold) continue;

            // Gather same-color-channel neighbors for local statistics.
            std::vector<float> neighbors;
            neighbors.reserve(25);

            for (int dy = -radius; dy <= radius; dy += step) {
                for (int dx = -radius; dx <= radius; dx += step) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        neighbors.push_back(data[ny * width + nx]);
                    }
                }
            }

            // Too few neighbors (border): trust the global screening.
            if (neighbors.size() < 4) {
                if (potentialHot)  map.hotPixels[i]  = true;
                if (potentialCold) map.coldPixels[i] = true;
                ++defects;
                continue;
            }

            // Local robust statistics via partial sort.
            const size_t n    = neighbors.size();
            const size_t half = n / 2;
            std::nth_element(neighbors.begin(), neighbors.begin() + half, neighbors.end());
            float localMedian = neighbors[half];

            std::vector<float> devs(n);
            for (size_t k = 0; k < n; ++k) {
                devs[k] = std::abs(neighbors[k] - localMedian);
            }
            std::nth_element(devs.begin(), devs.begin() + half, devs.end());
            float localSigma = devs[half] * 1.4826f;
            if (localSigma < 1e-6f) localSigma = 1e-6f;

            // Validate candidate against local statistics.
            if (potentialHot && val > localMedian + hotSigma * localSigma) {
                map.hotPixels[i] = true;
                ++defects;
            } else if (potentialCold && val < localMedian - coldSigma * localSigma) {
                map.coldPixels[i] = true;
                ++defects;
            }
        }
    }

    map.count = defects;
    return map;
}

// =============================================================================
// Correction Application (Full Image)
// =============================================================================

void CosmeticCorrection::apply(ImageBuffer& image, const CosmeticMap& map,
                               bool cfa)
{
    apply(image, map, 0, 0, cfa);
}

// =============================================================================
// Correction Application (ROI)
// =============================================================================

void CosmeticCorrection::apply(ImageBuffer& image, const CosmeticMap& map,
                               int offsetX, int offsetY, bool cfa)
{
    if (!image.isValid() || !map.isValid()) return;

    const int width    = image.width();
    const int height   = image.height();
    const int channels = image.channels();
    float* data        = image.data().data();
    const int step     = cfa ? 2 : 1;

    for (int y = 0; y < height; ++y) {
        int gy = y + offsetY;
        if (gy < 0 || gy >= map.height) continue;

        for (int x = 0; x < width; ++x) {
            int gx = x + offsetX;
            if (gx < 0 || gx >= map.width) continue;

            size_t mapIdx = static_cast<size_t>(gy) * map.width + gx;
            if (!map.hotPixels[mapIdx] && !map.coldPixels[mapIdx]) continue;

            // Replace defective pixel in every channel with neighbor median.
            for (int c = 0; c < channels; ++c) {
                std::vector<float> neighbors;
                neighbors.reserve(8);

                for (int dy = -step; dy <= step; dy += step) {
                    for (int dx = -step; dx <= step; dx += step) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                            size_t nIdx = static_cast<size_t>(ny) * width + nx;
                            neighbors.push_back(data[nIdx * channels + c]);
                        }
                    }
                }

                if (neighbors.empty()) continue;

                std::sort(neighbors.begin(), neighbors.end());
                size_t n = neighbors.size();
                float replacement = (n % 2 == 0)
                    ? (neighbors[n / 2 - 1] + neighbors[n / 2]) * 0.5f
                    : neighbors[n / 2];

                size_t idx = static_cast<size_t>(y) * width + x;
                data[idx * channels + c] = replacement;
            }
        }
    }
}

} // namespace Stacking