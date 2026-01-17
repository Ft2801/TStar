
#include "Weighting.h"
#include <algorithm>
#include <cmath>

namespace Stacking {

//=============================================================================
// WEIGHT COMPUTATION
//=============================================================================

bool Weighting::computeWeights(const ImageSequence& sequence,
                               WeightingType type,
                               const NormCoefficients& coefficients,
                               std::vector<double>& weights) {
    if (!sequence.isValid()) {
        return false;
    }
    
    int nbImages = sequence.count();
    int nbChannels = sequence.channels();
    
    if (type == WeightingType::None) {
        // All weights = 1
        weights.assign(nbImages * nbChannels, 1.0);
        return true;
    }
    
    weights.resize(nbImages * nbChannels);
    
    for (int i = 0; i < nbImages; ++i) {
        const auto& img = sequence.image(i);
        
        for (int c = 0; c < nbChannels; ++c) {
            double weight = 1.0;
            int layer = (nbChannels == 1) ? -1 : c;
            
            // Get scale factor from normalization
            double scale = 1.0;
            if (layer >= 0 && layer < 3) {
                scale = coefficients.pscale[layer][i];
            } else {
                scale = coefficients.scale[i];
            }
            if (scale <= 0.0) scale = 1.0;

            switch (type) {
                case WeightingType::StarCount:
                    weight = static_cast<double>(img.quality.starCount);
                    if (weight < 1.0) weight = 1.0;
                    break;
                    
                case WeightingType::WeightedFWHM:
                    if (img.quality.fwhm > 0.1) {
                        weight = 1.0 / (img.quality.fwhm * img.quality.fwhm * scale * scale);
                    } else {
                        weight = 1.0;
                    }
                    break;
                    
                case WeightingType::Noise:
                    if (img.quality.noise > 0.0) {
                        double sigma = img.quality.noise;
                        weight = 1.0 / (sigma * sigma * scale * scale);
                    } else {
                        weight = 1.0;
                    }
                    break;
                    
                case WeightingType::Roundness:
                    weight = img.quality.roundness;
                    if (weight < 0.1) weight = 0.1;
                    break;
                    
                case WeightingType::Quality:
                    weight = img.quality.quality;
                    if (weight < 0.01) weight = 0.01;
                    break;
                    
                default:
                    weight = 1.0;
                    break;
            }
            
            weights[c * nbImages + i] = weight;
        }
    }
    
    // Normalize weights per channel
    for (int c = 0; c < nbChannels; ++c) {
        double* chanWeights = &weights[c * nbImages];
        // Compute mean for this channel
        double sum = 0.0;
        for (int i = 0; i < nbImages; ++i) sum += chanWeights[i];
        
        if (sum > 0.0) {
            double invMean = static_cast<double>(nbImages) / sum;
            for (int i = 0; i < nbImages; ++i) chanWeights[i] *= invMean;
        }
    }
    
    return true;
}

void Weighting::normalizeWeights(std::vector<double>& weights, int count) {
    if (count <= 0) return;
    
    // Compute mean
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        sum += weights[i];
    }
    
    if (sum <= 0.0) return;
    
    // Normalize so mean = 1
    double invMean = static_cast<double>(count) / sum;
    for (int i = 0; i < count; ++i) {
        weights[i] *= invMean;
    }
}

//=============================================================================
// WEIGHT APPLICATION
//=============================================================================

float Weighting::applyWeight(float pixel, int imageIndex, int channelIndex,
                            const std::vector<double>& weights, int nbChannels) {
    if (weights.empty()) {
        return pixel;
    }
    
    int idx = channelIndex * (static_cast<int>(weights.size()) / nbChannels) + imageIndex;
    if (idx < 0 || idx >= static_cast<int>(weights.size())) {
        return pixel;
    }
    
    return pixel * static_cast<float>(weights[idx]);
}

float Weighting::computeWeightedMean(const std::vector<float>& stack,
                                     const std::vector<int>& rejected,
                                     const std::vector<int>& imageIndices,
                                     const std::vector<double>& weights,
                                     int channelIndex, int nbChannels) {
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    
    int nbImages = static_cast<int>(weights.size()) / nbChannels;
    
    for (size_t i = 0; i < stack.size(); ++i) {
        if (rejected.size() > i && rejected[i] != 0) {
            continue;  // Rejected pixel
        }
        
        // Get weight for this image
        double w = 1.0;
        if (!weights.empty() && i < imageIndices.size()) {
            int imgIdx = imageIndices[i];
            int wIdx = channelIndex * nbImages + imgIdx;
            if (wIdx >= 0 && wIdx < static_cast<int>(weights.size())) {
                w = weights[wIdx];
            }
        }
        
        weightedSum += stack[i] * w;
        totalWeight += w;
    }
    
    if (totalWeight <= 0.0) {
        // Fallback to simple mean
        double sum = 0.0;
        int count = 0;
        for (size_t i = 0; i < stack.size(); ++i) {
            if (rejected.size() <= i || rejected[i] == 0) {
                sum += stack[i];
                count++;
            }
        }
        return count > 0 ? static_cast<float>(sum / count) : 0.0f;
    }
    
    return static_cast<float>(weightedSum / totalWeight);
}

} // namespace Stacking
