
#ifndef WEIGHTING_H
#define WEIGHTING_H

#include "StackingTypes.h"
#include "StackingSequence.h"
#include <vector>

namespace Stacking {

class Weighting {
public:

    static bool computeWeights(const ImageSequence& sequence,
                               WeightingType type,
                               std::vector<double>& weights);
    
    static float applyWeight(float pixel, int imageIndex, int channelIndex,
                            const std::vector<double>& weights, int nbChannels);
    
    static float computeWeightedMean(const std::vector<float>& stack,
                                     const std::vector<int>& rejected,
                                     const std::vector<int>& imageIndices,
                                     const std::vector<double>& weights,
                                     int channelIndex, int nbChannels);

private:

    static void normalizeWeights(std::vector<double>& weights, int count);
};

inline QString weightingTypeToString(WeightingType type) {
    switch (type) {
        case WeightingType::None: return "None";
        case WeightingType::StarCount: return "Star Count";
        case WeightingType::WeightedFWHM: return "FWHM";
        case WeightingType::Noise: return "Noise";
        case WeightingType::Roundness: return "Roundness";
        case WeightingType::Quality: return "Quality";
        default: return "Unknown";
    }
}

}

#endif 
