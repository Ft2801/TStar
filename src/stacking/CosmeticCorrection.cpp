#include "CosmeticCorrection.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Stacking {

//-----------------------------------------------------------------------------
// Helper: Compute Median and Sigma (MAD-based for robustness)
//-----------------------------------------------------------------------------
void CosmeticCorrection::computeStats(const ImageBuffer& img, float& median, float& sigma) {
    if (!img.isValid()) return;

    size_t count = static_cast<size_t>(img.width()) * img.height();
    std::vector<float> pixels;
    pixels.reserve(count);

    const float* data = img.data().data();
    for (size_t i = 0; i < count; ++i) {
        pixels.push_back(data[i]);
    }

    // Sort for median
    size_t n = pixels.size();
    if (n == 0) return;
    
    std::sort(pixels.begin(), pixels.end());
    
    if (n % 2 == 0) {
        median = (pixels[n/2 - 1] + pixels[n/2]) * 0.5f;
    } else {
        median = pixels[n/2];
    }

    // Compute MAD (Median Absolute Deviation)
    std::vector<float> deviations;
    deviations.reserve(n);
    for (float val : pixels) {
        deviations.push_back(std::abs(val - median));
    }
    std::sort(deviations.begin(), deviations.end());
    
    float mad;
    if (n % 2 == 0) {
        mad = (deviations[n/2 - 1] + deviations[n/2]) * 0.5f;
    } else {
        mad = deviations[n/2];
    }

    // Sigma approx from MAD: Sigma ~= 1.4826 * MAD
    sigma = mad * 1.4826f;
}

//-----------------------------------------------------------------------------
// Find Defects
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Find Defects
//-----------------------------------------------------------------------------
CosmeticMap CosmeticCorrection::findDefects(const ImageBuffer& dark, float hotSigma, float coldSigma, bool cfa) {
    CosmeticMap map;
    if (!dark.isValid()) return map;

    map.width = dark.width();
    map.height = dark.height();
    size_t size = static_cast<size_t>(map.width) * map.height;

    map.hotPixels.resize(size, false);
    map.coldPixels.resize(size, false);

    // 1. Global Statistics
    float globalMedian = 0.0f;
    float globalSigma = 0.0f;
    computeStats(dark, globalMedian, globalSigma);

    if (globalSigma <= 1e-9f) globalSigma = 1e-9f;

    float globalHotThresh = globalMedian + hotSigma * globalSigma;
    float globalColdThresh = globalMedian - coldSigma * globalSigma;

    const float* data = dark.data().data();
    int defects = 0;
    int width = dark.width();
    int height = dark.height();
    int step = cfa ? 2 : 1;
    
    // 5x5 window (radius 2 steps)
    // If CFA (step=2), radius 2 steps = 4 pixels. 
    int radius = 2 * step; 

    #pragma omp parallel for reduction(+:defects)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t i = static_cast<size_t>(y) * width + x;
            float val = data[i];
            
            bool potentialHot = (val > globalHotThresh);
            bool potentialCold = (val < globalColdThresh);
            
            if (!potentialHot && !potentialCold) continue;
            
            // 2. Local Validation
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
            
            // Need enough neighbors for stats
            if (neighbors.size() < 4) {
                // Edge case: Trust global? Or reject?
                // Let's trust global if on edge
                if (potentialHot) map.hotPixels[i] = true;
                if (potentialCold) map.coldPixels[i] = true;
                defects++;
                continue;
            }
            
            // Compute Local Stats (Quick Median + MAD)
            size_t n = neighbors.size();
            size_t half = n / 2;
            std::nth_element(neighbors.begin(), neighbors.begin() + half, neighbors.end());
            float localMedian = neighbors[half];
            
            // Local MAD
            std::vector<float> deviations;
            deviations.reserve(n);
            for(float v : neighbors) deviations.push_back(std::abs(v - localMedian));
            std::nth_element(deviations.begin(), deviations.begin() + half, deviations.end());
            float localMad = deviations[half];
            float localSigma = localMad * 1.4826f;
            
            if (localSigma < 1e-6f) localSigma = 1e-6f; // Avoid zero sigma (flat area)
            
            if (potentialHot) {
                if (val > localMedian + hotSigma * localSigma) {
                    map.hotPixels[i] = true;
                    defects++;
                }
            } else if (potentialCold) {
                if (val < localMedian - coldSigma * localSigma) {
                    map.coldPixels[i] = true;
                    defects++;
                }
            }
        }
    }

    map.count = defects;
    return map;
}

//-----------------------------------------------------------------------------
// Apply Correction (Full Image)
//-----------------------------------------------------------------------------
void CosmeticCorrection::apply(ImageBuffer& image, const CosmeticMap& map, bool cfa) {
    apply(image, map, 0, 0, cfa);
}

//-----------------------------------------------------------------------------
// Apply Correction (ROI)
//-----------------------------------------------------------------------------
void CosmeticCorrection::apply(ImageBuffer& image, const CosmeticMap& map, int offsetX, int offsetY, bool cfa) {
    if (!image.isValid() || !map.isValid()) return;
    
    // Bounds check? No, trust caller or clip.
    // offsets are such that image pixel (x,y) corresponds to map pixel (x+offsetX, y+offsetY)

    int width = image.width();
    int height = image.height();
    int channels = image.channels();
    float* data = image.data().data();

    int step = cfa ? 2 : 1;

    for (int y = 0; y < height; ++y) {
        int gy = y + offsetY;
        if (gy < 0 || gy >= map.height) continue;

        for (int x = 0; x < width; ++x) {
            int gx = x + offsetX;
            if (gx < 0 || gx >= map.width) continue;

            size_t mapIdx = static_cast<size_t>(gy) * map.width + gx;
            
            bool isHot = map.hotPixels[mapIdx];
            bool isCold = map.coldPixels[mapIdx];

            if (!isHot && !isCold) continue;

            // Fix all channels for this pixel location
            for (int c = 0; c < channels; ++c) {
                std::vector<float> neighbors;
                neighbors.reserve(8);

                for (int dy = -step; dy <= step; dy += step) {
                    for (int dx = -step; dx <= step; dx += step) {
                        if (dx == 0 && dy == 0) continue;

                        int nx = x + dx;
                        int ny = y + dy;

                        // Check if neighbor is in image bounds
                        if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                            size_t nIdx = static_cast<size_t>(ny) * width + nx;
                            neighbors.push_back(data[nIdx * channels + c]);
                        }
                    }
                }

                if (neighbors.empty()) continue;

                std::sort(neighbors.begin(), neighbors.end());
                float replacement;
                size_t n = neighbors.size();
                if (n % 2 == 0) {
                    replacement = (neighbors[n/2 - 1] + neighbors[n/2]) * 0.5f;
                } else {
                    replacement = neighbors[n/2];
                }

                size_t idx = static_cast<size_t>(y) * width + x;
                data[idx * channels + c] = replacement;
            }
        }
    }
}

} // namespace Stacking
