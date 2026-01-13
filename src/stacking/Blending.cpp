
#include "Blending.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Stacking {

//=============================================================================
// BLEND MASK GENERATION
//=============================================================================

void Blending::generateBlendMask(const ImageBuffer& image,
                                 int featherDistance,
                                 std::vector<float>& mask) {
    int w = image.width();
    int h = image.height();
    
    mask.resize(static_cast<size_t>(w) * h);
    
    // Generate mask from edge distances
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Distance from nearest edge
            float distX = std::min(static_cast<float>(x), static_cast<float>(w - 1 - x));
            float distY = std::min(static_cast<float>(y), static_cast<float>(h - 1 - y));
            float dist = std::min(distX, distY);
            
            // Apply feather function
            size_t idx = static_cast<size_t>(y) * w + x;
            mask[idx] = featherFunction(dist, featherDistance);
        }
    }
}

void Blending::generateNonZeroMask(const ImageBuffer& image,
                                   int featherDistance,
                                   std::vector<float>& mask) {
    int w = image.width();
    int h = image.height();
    
    // First compute distance from zero pixels
    std::vector<float> distances;
    computeDistanceFromZeros(image.data().data(), w, h, distances);
    
    // Convert distances to mask values
    mask.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < distances.size(); ++i) {
        mask[i] = featherFunction(distances[i], featherDistance);
    }
}

//=============================================================================
// BLENDING APPLICATION
//=============================================================================

void Blending::applyBlending(const std::vector<const ImageBuffer*>& images,
                             const std::vector<std::vector<float>>& masks,
                             ImageBuffer& output) {
    if (images.empty() || images.size() != masks.size()) {
        return;
    }
    
    int w = images[0]->width();
    int h = images[0]->height();
    int channels = images[0]->channels();
    
    output = ImageBuffer(w, h, channels);
    
    size_t pixelCount = static_cast<size_t>(w) * h;
    float* outData = output.data().data();
    
    // For each pixel, compute weighted average
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        // Accumulate weighted sum per channel
        for (int c = 0; c < channels; ++c) {
            double weightedSum = 0.0;
            double totalWeight = 0.0;
            
            for (size_t i = 0; i < images.size(); ++i) {
                if (!images[i] || masks[i].size() != pixelCount) continue;
                
                float weight = masks[i][pixel];
                if (weight > 0.0f) {
                    size_t dataIdx = c * pixelCount + pixel;
                    float pixelValue = images[i]->data().data()[dataIdx];
                    
                    // Skip zero pixels (no data)
                    if (pixelValue != 0.0f) {
                        weightedSum += pixelValue * weight;
                        totalWeight += weight;
                    }
                }
            }
            
            size_t outIdx = c * pixelCount + pixel;
            if (totalWeight > 0.0) {
                outData[outIdx] = static_cast<float>(weightedSum / totalWeight);
            } else {
                outData[outIdx] = 0.0f;
            }
        }
    }
}

//=============================================================================
// DISTANCE COMPUTATION
//=============================================================================

void Blending::computeDistanceTransform(const ImageBuffer& image,
                                        std::vector<float>& distanceMap) {
    computeDistanceFromZeros(image.data().data(), image.width(), image.height(), distanceMap);
}

void Blending::computeDistanceFromZeros(const float* data,
                                        int width, int height,
                                        std::vector<float>& distances) {
    size_t pixelCount = static_cast<size_t>(width) * height;
    distances.assign(pixelCount, std::numeric_limits<float>::max());
    
    // Forward pass
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>(y) * width + x;
            
            // Check if this is a zero pixel (boundary)
            if (data[idx] == 0.0f) {
                distances[idx] = 0.0f;
                continue;
            }
            
            // Check left neighbor
            if (x > 0) {
                size_t leftIdx = idx - 1;
                distances[idx] = std::min(distances[idx], distances[leftIdx] + 1.0f);
            }
            
            // Check top neighbor
            if (y > 0) {
                size_t topIdx = idx - width;
                distances[idx] = std::min(distances[idx], distances[topIdx] + 1.0f);
            }
        }
    }
    
    // Backward pass
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            size_t idx = static_cast<size_t>(y) * width + x;
            
            // Check right neighbor
            if (x < width - 1) {
                size_t rightIdx = idx + 1;
                distances[idx] = std::min(distances[idx], distances[rightIdx] + 1.0f);
            }
            
            // Check bottom neighbor
            if (y < height - 1) {
                size_t bottomIdx = idx + width;
                distances[idx] = std::min(distances[idx], distances[bottomIdx] + 1.0f);
            }
        }
    }
}

//=============================================================================
// FEATHER FUNCTIONS
//=============================================================================

float Blending::featherFunction(float distance, int featherDistance) {
    if (featherDistance <= 0) {
        return 1.0f;  // No feathering
    }
    
    if (distance >= static_cast<float>(featherDistance)) {
        return 1.0f;  // Fully inside
    }
    
    if (distance <= 0.0f) {
        return 0.0f;  // On boundary
    }
    
    // Smooth falloff using smoothstep
    return smoothStep(0.0f, static_cast<float>(featherDistance), distance);
}

float Blending::smoothStep(float edge0, float edge1, float x) {
    // Clamp x to [0, 1]
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    
    // Evaluate smoothstep polynomial
    return t * t * (3.0f - 2.0f * t);
}

} // namespace Stacking
