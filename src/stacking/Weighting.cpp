/**
 * @file Weighting.cpp
 * @brief Implementation of per-image weight computation and application.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "Weighting.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Stacking {

// ============================================================================
// Weight computation
// ============================================================================

bool Weighting::computeWeights(const ImageSequence& sequence,
                               WeightingType type,
                               const NormCoefficients& coefficients,
                               std::vector<double>& weights)
{
    if (!sequence.isValid()) {
        return false;
    }

    const int nbImages   = sequence.count();
    const int nbChannels = sequence.channels();

    /* Trivial case: equal weights for every frame. */
    if (type == WeightingType::None) {
        weights.assign(nbImages * nbChannels, 1.0);
        return true;
    }

    weights.resize(nbImages * nbChannels);

    // ------------------------------------------------------------------------
    // WeightedFWHM
    //   w = (1/fwhm^2 - 1/fwhm_max^2) / (1/fwhm_min^2 - 1/fwhm_max^2)
    //   FWHM is a geometric measure, independent of normalization scaling.
    // ------------------------------------------------------------------------
    if (type == WeightingType::WeightedFWHM) {
        double fwhmMin =  std::numeric_limits<double>::max();
        double fwhmMax = -std::numeric_limits<double>::max();

        for (int i = 0; i < nbImages; ++i) {
            const double fwhm = sequence.image(i).quality.weightedFwhm;
            if (fwhm > 0.0) {
                if (fwhm < fwhmMin) fwhmMin = fwhm;
                if (fwhm > fwhmMax) fwhmMax = fwhm;
            }
        }

        if (fwhmMin >= fwhmMax || fwhmMin <= 0.0) {
            /* All images have identical FWHM or no valid data -- equal weights. */
            weights.assign(nbImages * nbChannels, 1.0);
            return true;
        }

        const double invFwhmMax2 = 1.0 / (fwhmMax * fwhmMax);
        const double invDenom    = 1.0 / (1.0 / (fwhmMin * fwhmMin) - invFwhmMax2);

        for (int i = 0; i < nbImages; ++i) {
            const double fwhm = sequence.image(i).quality.weightedFwhm;
            const double w    = (fwhm > 0.0)
                ? (1.0 / (fwhm * fwhm) - invFwhmMax2) * invDenom
                : 0.0;

            for (int c = 0; c < nbChannels; ++c) {
                weights[c * nbImages + i] = w;
            }
        }

        /* Normalise so that the mean weight per channel is 1. */
        for (int c = 0; c < nbChannels; ++c) {
            double* cw  = &weights[c * nbImages];
            double  sum = 0.0;
            for (int i = 0; i < nbImages; ++i) sum += cw[i];
            if (sum > 0.0) {
                const double invMean = static_cast<double>(nbImages) / sum;
                for (int i = 0; i < nbImages; ++i) cw[i] *= invMean;
            }
        }
        return true;
    }

    // ------------------------------------------------------------------------
    // StarCount
    //   w = ((n_stars - star_min) / (star_max - star_min))^2
    // ------------------------------------------------------------------------
    if (type == WeightingType::StarCount) {
        int starMin = std::numeric_limits<int>::max();
        int starMax = 0;

        for (int i = 0; i < nbImages; ++i) {
            const int ns = sequence.image(i).quality.starCount;
            if (ns < starMin) starMin = ns;
            if (ns > starMax) starMax = ns;
        }
        if (starMin < 0) starMin = 0;

        if (starMax == starMin) {
            weights.assign(nbImages * nbChannels, 1.0);
            return true;
        }

        const double invDenom = 1.0 / static_cast<double>(starMax - starMin);

        for (int i = 0; i < nbImages; ++i) {
            const double frac = (sequence.image(i).quality.starCount - starMin) * invDenom;
            const double w    = frac * frac;    // Quadratic weighting.

            for (int c = 0; c < nbChannels; ++c) {
                weights[c * nbImages + i] = w;
            }
        }

        for (int c = 0; c < nbChannels; ++c) {
            double* cw  = &weights[c * nbImages];
            double  sum = 0.0;
            for (int i = 0; i < nbImages; ++i) sum += cw[i];
            if (sum > 0.0) {
                const double invMean = static_cast<double>(nbImages) / sum;
                for (int i = 0; i < nbImages; ++i) cw[i] *= invMean;
            }
        }
        return true;
    }

    // ------------------------------------------------------------------------
    // Remaining types: Noise, Roundness, Quality, StackCount
    // ------------------------------------------------------------------------
    for (int i = 0; i < nbImages; ++i) {
        const auto& img = sequence.image(i);

        for (int c = 0; c < nbChannels; ++c) {
            double weight = 1.0;

            const int layer = (nbChannels == 1) ? -1 : c;

            /* Retrieve the normalization scale factor (relevant only for Noise). */
            double scale = 1.0;
            if (layer >= 0 && layer < 3) {
                scale = coefficients.pscale[layer][i];
            } else {
                scale = coefficients.scale[i];
            }
            if (scale <= 0.0) {
                scale = 1.0;
            }

            switch (type) {
            case WeightingType::Noise:
                /* w = 1 / (sigma^2 * scale^2) */
                if (img.quality.noise > 0.0) {
                    const double sigma = img.quality.noise;
                    weight = 1.0 / (sigma * sigma * scale * scale);
                }
                break;

            case WeightingType::Roundness:
                weight = std::max(0.1, img.quality.roundness);
                break;

            case WeightingType::Quality:
                weight = std::max(0.01, img.quality.quality);
                break;

            case WeightingType::StackCount:
                weight = std::max(1.0, static_cast<double>(img.stackCount));
                break;

            default:
                break;
            }

            weights[c * nbImages + i] = weight;
        }
    }

    /* Normalise weights per channel so that the mean equals 1. */
    for (int c = 0; c < nbChannels; ++c) {
        double* chanWeights = &weights[c * nbImages];
        double  sum = 0.0;
        for (int i = 0; i < nbImages; ++i) {
            sum += chanWeights[i];
        }
        if (sum > 0.0) {
            const double invMean = static_cast<double>(nbImages) / sum;
            for (int i = 0; i < nbImages; ++i) {
                chanWeights[i] *= invMean;
            }
        }
    }

    return true;
}

void Weighting::normalizeWeights(std::vector<double>& weights, int count)
{
    if (count <= 0) return;

    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        sum += weights[i];
    }
    if (sum <= 0.0) return;

    const double invMean = static_cast<double>(count) / sum;
    for (int i = 0; i < count; ++i) {
        weights[i] *= invMean;
    }
}

// ============================================================================
// Weight application
// ============================================================================

float Weighting::applyWeight(float pixel, int imageIndex, int channelIndex,
                             const std::vector<double>& weights, int nbChannels)
{
    if (weights.empty()) {
        return pixel;
    }

    const int idx = channelIndex * (static_cast<int>(weights.size()) / nbChannels)
                  + imageIndex;

    if (idx < 0 || idx >= static_cast<int>(weights.size())) {
        return pixel;
    }

    return pixel * static_cast<float>(weights[idx]);
}

float Weighting::computeWeightedMean(const std::vector<float>& stack,
                                     const std::vector<int>& rejected,
                                     const std::vector<int>& imageIndices,
                                     const std::vector<double>& weights,
                                     int channelIndex, int nbChannels)
{
    double weightedSum = 0.0;
    double totalWeight = 0.0;

    const int nbImages = static_cast<int>(weights.size()) / nbChannels;

    for (size_t i = 0; i < stack.size(); ++i) {
        /* Skip rejected pixels. */
        if (rejected.size() > i && rejected[i] != 0) {
            continue;
        }

        /* Look up the weight for this image / channel. */
        double w = 1.0;
        if (!weights.empty() && i < imageIndices.size()) {
            const int imgIdx = imageIndices[i];
            const int wIdx   = channelIndex * nbImages + imgIdx;
            if (wIdx >= 0 && wIdx < static_cast<int>(weights.size())) {
                w = weights[wIdx];
            }
        }

        weightedSum += stack[i] * w;
        totalWeight += w;
    }

    if (totalWeight <= 0.0) {
        /* Fallback: unweighted mean of non-rejected values. */
        double sum   = 0.0;
        int    count = 0;
        for (size_t i = 0; i < stack.size(); ++i) {
            if (rejected.size() <= i || rejected[i] == 0) {
                sum += stack[i];
                ++count;
            }
        }
        return (count > 0) ? static_cast<float>(sum / count) : 0.0f;
    }

    return static_cast<float>(weightedSum / totalWeight);
}

} // namespace Stacking