
#include "Weighting.h"
#include <algorithm>
#include <cmath>
#include <limits>

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
        weights.assign(nbImages * nbChannels, 1.0);
        return true;
    }

    weights.resize(nbImages * nbChannels);

    // -------------------------------------------------------------------------
    // WeightedFWHM
    //   w = (1/fwhm² − 1/fwhmmax²) / (1/fwhmmin² − 1/fwhmmax²)
    //   No scale factor (fwhm is a geometric measure, independent of normalization).
    //   Two-pass: first scan for min/max, then compute weights.
    // -------------------------------------------------------------------------
    if (type == WeightingType::WeightedFWHM) {
        double fwhmmin = std::numeric_limits<double>::max();
        double fwhmmax = -std::numeric_limits<double>::max();
        for (int i = 0; i < nbImages; ++i) {
            double fwhm = sequence.image(i).quality.weightedFwhm;
            if (fwhm > 0.0) {
                if (fwhm < fwhmmin) fwhmmin = fwhm;
                if (fwhm > fwhmmax) fwhmmax = fwhm;
            }
        }
        if (fwhmmin >= fwhmmax || fwhmmin <= 0.0) {
            // All images identical or no valid data — equal weights
            weights.assign(nbImages * nbChannels, 1.0);
            return true;
        }
        double invfwhmax2 = 1.0 / (fwhmmax * fwhmmax);
        double invdenom   = 1.0 / (1.0 / (fwhmmin * fwhmmin) - invfwhmax2);

        for (int i = 0; i < nbImages; ++i) {
            double fwhm = sequence.image(i).quality.weightedFwhm;
            double w = (fwhm > 0.0) ? (1.0 / (fwhm * fwhm) - invfwhmax2) * invdenom
                                     : 0.0;
            for (int c = 0; c < nbChannels; ++c)
                weights[c * nbImages + i] = w;
        }

        // Normalize per channel (weights /= mean → mean weight = 1)
        for (int c = 0; c < nbChannels; ++c) {
            double* cw = &weights[c * nbImages];
            double sum = 0.0;
            for (int i = 0; i < nbImages; ++i) sum += cw[i];
            if (sum > 0.0) {
                double invMean = static_cast<double>(nbImages) / sum;
                for (int i = 0; i < nbImages; ++i) cw[i] *= invMean;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // StarCount
    //   w = ((nstars − starmin) / (starmax − starmin))²
    //   Equal weights when starmax == starmin.
    //   Two-pass: first scan for min/max, then compute weights.
    // -------------------------------------------------------------------------
    if (type == WeightingType::StarCount) {
        int starmin = std::numeric_limits<int>::max();
        int starmax = 0;
        for (int i = 0; i < nbImages; ++i) {
            int ns = sequence.image(i).quality.starCount;
            if (ns < starmin) starmin = ns;
            if (ns > starmax) starmax = ns;
        }
        if (starmin < 0) starmin = 0;

        if (starmax == starmin) {
            weights.assign(nbImages * nbChannels, 1.0);
            return true;
        }
        double invdenom = 1.0 / static_cast<double>(starmax - starmin);

        for (int i = 0; i < nbImages; ++i) {
            double frac = (sequence.image(i).quality.starCount - starmin) * invdenom;
            double w = frac * frac;   // quadratic
            for (int c = 0; c < nbChannels; ++c)
                weights[c * nbImages + i] = w;
        }

        for (int c = 0; c < nbChannels; ++c) {
            double* cw = &weights[c * nbImages];
            double sum = 0.0;
            for (int i = 0; i < nbImages; ++i) sum += cw[i];
            if (sum > 0.0) {
                double invMean = static_cast<double>(nbImages) / sum;
                for (int i = 0; i < nbImages; ++i) cw[i] *= invMean;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // All other weight types (Noise, Roundness, Quality)
    // -------------------------------------------------------------------------
    for (int i = 0; i < nbImages; ++i) {
        const auto& img = sequence.image(i);

        for (int c = 0; c < nbChannels; ++c) {
            double weight = 1.0;
            int layer = (nbChannels == 1) ? -1 : c;

            // Scale factor from normalization (used only for Noise)
            double scale = 1.0;
            if (layer >= 0 && layer < 3) {
                scale = coefficients.pscale[layer][i];
            } else {
                scale = coefficients.scale[i];
            }
            if (scale <= 0.0) scale = 1.0;

            switch (type) {
                // Noise: w = 1 / (noise² × scale²)
                case WeightingType::Noise:
                    if (img.quality.noise > 0.0) {
                        double sigma = img.quality.noise;
                        weight = 1.0 / (sigma * sigma * scale * scale);
                    }
                    break;

                case WeightingType::Roundness:
                    weight = std::max(0.1, img.quality.roundness);
                    break;

                case WeightingType::Quality:
                    weight = std::max(0.01, img.quality.quality);
                    break;

                // StackCount: weight by prior stack count
                case WeightingType::StackCount:
                    weight = std::max(1.0, (double)img.stackCount);
                    break;

                default:
                    break;
            }

            weights[c * nbImages + i] = weight;
        }
    }

    // Normalize weights per channel (mean weight = 1)
    for (int c = 0; c < nbChannels; ++c) {
        double* chanWeights = &weights[c * nbImages];
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
