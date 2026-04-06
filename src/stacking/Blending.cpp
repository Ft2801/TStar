/**
 * @file Blending.cpp
 * @brief Implementation of feathered blending for image compositing.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "Blending.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Stacking {

// =============================================================================
// Mask Generation
// =============================================================================

void Blending::generateBlendMask(const ImageBuffer& image,
                                 int featherDistance,
                                 std::vector<float>& mask)
{
    const int w = image.width();
    const int h = image.height();

    mask.resize(static_cast<size_t>(w) * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Minimum distance to the nearest image edge.
            float distX = std::min(static_cast<float>(x),
                                   static_cast<float>(w - 1 - x));
            float distY = std::min(static_cast<float>(y),
                                   static_cast<float>(h - 1 - y));
            float dist  = std::min(distX, distY);

            size_t idx = static_cast<size_t>(y) * w + x;
            mask[idx]  = featherFunction(dist, featherDistance);
        }
    }
}

void Blending::generateNonZeroMask(const ImageBuffer& image,
                                   int featherDistance,
                                   std::vector<float>& mask)
{
    const int w = image.width();
    const int h = image.height();

    // Compute per-pixel distance from the nearest zero-valued boundary.
    std::vector<float> distances;
    computeDistanceFromZeros(image.data().data(), w, h, distances);

    // Convert distances to mask weights.
    mask.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < distances.size(); ++i) {
        mask[i] = featherFunction(distances[i], featherDistance);
    }
}

// =============================================================================
// Blending Application
// =============================================================================

void Blending::applyBlending(const std::vector<const ImageBuffer*>& images,
                             const std::vector<std::vector<float>>& masks,
                             ImageBuffer& output)
{
    if (images.empty() || images.size() != masks.size()) {
        return;
    }

    const int w        = images[0]->width();
    const int h        = images[0]->height();
    const int channels = images[0]->channels();

    output = ImageBuffer(w, h, channels);

    const size_t pixelCount = static_cast<size_t>(w) * h;
    float* outData = output.data().data();
    size_t numImages = images.size();

    // Weighted average across all input images for each pixel and channel.
    // Iterating channels on the outer loop is much more cache-friendly for planar images.
#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int c = 0; c < channels; ++c) {
        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            double weightedSum = 0.0;
            double totalWeight = 0.0;
            double c_kahan = 0.0; // Kahan summation error compensator for precision

            for (size_t i = 0; i < numImages; ++i) {
                if (!images[i] || masks[i].size() != pixelCount) continue;

                float weight = masks[i][pixel];
                if (weight > 0.0f) {
                    size_t dataIdx   = static_cast<size_t>(c) * pixelCount + pixel;
                    float pixelValue = images[i]->data().data()[dataIdx];

                    // Skip zero pixels (no data).
                    if (pixelValue != 0.0f) {
                        double term = (pixelValue * weight) - c_kahan;
                        double t = weightedSum + term;
                        c_kahan = (t - weightedSum) - term;
                        weightedSum = t;
                        totalWeight += weight;
                    }
                }
            }

            size_t outIdx = static_cast<size_t>(c) * pixelCount + pixel;
            if (totalWeight > 0.0) {
                outData[outIdx] = static_cast<float>(weightedSum / totalWeight);
            } else {
                outData[outIdx] = 0.0f;
            }
        }
    }
}

// =============================================================================
// Distance Computation
// =============================================================================

void Blending::computeDistanceTransform(const ImageBuffer& image,
                                        std::vector<float>& distanceMap)
{
    computeDistanceFromZeros(image.data().data(),
                             image.width(), image.height(),
                             distanceMap);
}

void Blending::computeDistanceFromZeros(const float* data,
                                        int width, int height,
                                        std::vector<float>& distances)
{
    const size_t pixelCount = static_cast<size_t>(width) * height;
    distances.assign(pixelCount, std::numeric_limits<float>::max());

    // Forward pass (top-left to bottom-right).
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>(y) * width + x;

            if (data[idx] == 0.0f) {
                distances[idx] = 0.0f;
                continue;
            }

            if (x > 0) {
                distances[idx] = std::min(distances[idx], distances[idx - 1] + 1.0f);
            }
            if (y > 0) {
                size_t topIdx = idx - width;
                distances[idx] = std::min(distances[idx], distances[topIdx] + 1.0f);
            }
        }
    }

    // Backward pass (bottom-right to top-left).
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            size_t idx = static_cast<size_t>(y) * width + x;

            if (x < width - 1) {
                distances[idx] = std::min(distances[idx], distances[idx + 1] + 1.0f);
            }
            if (y < height - 1) {
                size_t bottomIdx = idx + width;
                distances[idx] = std::min(distances[idx], distances[bottomIdx] + 1.0f);
            }
        }
    }
}

// =============================================================================
// Feather Functions
// =============================================================================

float Blending::featherFunction(float distance, int featherDistance)
{
    if (featherDistance <= 0) {
        return 1.0f;
    }
    if (distance >= static_cast<float>(featherDistance)) {
        return 1.0f;
    }
    if (distance <= 0.0f) {
        return 0.0f;
    }

    return smoothStep(0.0f, static_cast<float>(featherDistance), distance);
}

float Blending::smoothStep(float edge0, float edge1, float x)
{
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace Stacking