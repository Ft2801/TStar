/**
 * @file Weighting.h
 * @brief Per-image weighting computation and application for stacking.
 *
 * Provides the Weighting utility class that derives per-frame weights
 * from quality metrics and normalization coefficients, and applies those
 * weights during pixel combination.
 *
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
 */

#ifndef STACKING_WEIGHTING_H
#define STACKING_WEIGHTING_H

#include "StackingTypes.h"
#include "StackingSequence.h"

#include <vector>

namespace Stacking {

/**
 * @brief Static utility class for image weight computation and application.
 *
 * Weights are stored in a flat vector indexed as
 * [channel * nbImages + imageIndex], and are normalised so that the
 * mean weight per channel equals 1.0.
 */
class Weighting {
public:
    /**
     * @brief Compute per-image weights for all channels.
     *
     * @param sequence     Image sequence with quality metrics.
     * @param type         Weighting criterion to apply.
     * @param coefficients Pre-computed normalization coefficients (used by Noise weighting).
     * @param weights      [out] Flat vector of computed weights.
     * @return true on success.
     */
    static bool computeWeights(const ImageSequence& sequence,
                               WeightingType type,
                               const NormCoefficients& coefficients,
                               std::vector<double>& weights);

    /**
     * @brief Multiply a single pixel value by its image weight.
     *
     * @param pixel        Input pixel value.
     * @param imageIndex   Zero-based image index.
     * @param channelIndex Zero-based channel index.
     * @param weights      Weight vector produced by computeWeights().
     * @param nbChannels   Total number of channels.
     * @return Weighted pixel value.
     */
    static float applyWeight(float pixel, int imageIndex, int channelIndex,
                             const std::vector<double>& weights, int nbChannels);

    /**
     * @brief Compute the weighted mean of a pixel stack, honouring rejection flags.
     *
     * @param stack         Per-image pixel values at one spatial position.
     * @param rejected      Rejection flags (non-zero = rejected). May be empty.
     * @param imageIndices  Mapping from stack position to image index.
     * @param weights       Weight vector produced by computeWeights().
     * @param channelIndex  Current channel.
     * @param nbChannels    Total number of channels.
     * @return Weighted mean of non-rejected values.
     */
    static float computeWeightedMean(const std::vector<float>& stack,
                                     const std::vector<int>& rejected,
                                     const std::vector<int>& imageIndices,
                                     const std::vector<double>& weights,
                                     int channelIndex, int nbChannels);

private:
    /**
     * @brief Normalise a weight sub-vector so that its mean equals 1.0.
     * @param weights Weight sub-vector.
     * @param count   Number of elements.
     */
    static void normalizeWeights(std::vector<double>& weights, int count);
};

/**
 * @brief Convert a WeightingType enum to a human-readable string.
 * @param type Weighting type.
 * @return Display-friendly name.
 */
inline QString weightingTypeToString(WeightingType type)
{
    switch (type) {
    case WeightingType::None:         return QStringLiteral("None");
    case WeightingType::StarCount:    return QStringLiteral("Star Count");
    case WeightingType::WeightedFWHM: return QStringLiteral("FWHM");
    case WeightingType::Noise:        return QStringLiteral("Noise");
    case WeightingType::Roundness:    return QStringLiteral("Roundness");
    case WeightingType::Quality:      return QStringLiteral("Quality");
    default:                          return QStringLiteral("Unknown");
    }
}

} // namespace Stacking

#endif // STACKING_WEIGHTING_H