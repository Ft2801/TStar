#ifndef OVERLAP_NORMALIZATION_H
#define OVERLAP_NORMALIZATION_H

#include "../ImageBuffer.h"
#include "StackingTypes.h"
#include <vector>

namespace Stacking {

class OverlapNormalization {
public:
    struct OverlapStats {
        int imgI, imgJ;           // Image pair indices
        size_t pixelCount;        // Number of overlapping pixels
        double medianI, medianJ;  // Medians in overlap region
        double madI, madJ;        // MAD in overlap region
        double locationI, locationJ; // Robust location estimators
        double scaleI, scaleJ;    // Robust scale estimators
    };
    
    /**
     * @brief Compute overlap region between two images
     * @param regI Registration for image I
     * @param regJ Registration for image J
     * @param widthI, heightI Dimensions of image I
     * @param widthJ, heightJ Dimensions of image J
     * @param areaI Output: overlap rectangle in Image I coordinates
     * @param areaJ Output: overlap rectangle in Image J coordinates
     * @return Number of overlapping pixels (0 if none)
     */
    static size_t computeOverlapRegion(const RegistrationData& regI, const RegistrationData& regJ,
                                       int widthI, int heightI, int widthJ, int heightJ,
                                       QRect& areaI, QRect& areaJ);
    
    /**
     * @brief Compute statistics on overlapping regions
     */
    static bool computeOverlapStats(const ImageBuffer& imgI, const ImageBuffer& imgJ,
                                    const QRect& areaI, const QRect& areaJ,
                                    int channel, OverlapStats& stats);
    
    /**
     * @brief Solve for normalization coefficients using overlap statistics
     * 
     * Solves minimization problem to find coefficients that minimize
     * intensity differences in all overlapping regions.
     * 
     * @param allStats Vector of all pair-wise overlap stats
     * @param numImages Total number of images
     * @param refIndex Index of reference image
     * @param additive True for additive, false for multiplicative
     * @param coeffs Output: normalization coefficients per image
     */
    static bool solveCoefficients(const std::vector<OverlapStats>& allStats,
                                  int numImages, int refIndex, bool additive,
                                  std::vector<double>& coeffs);
};

} // namespace Stacking

#endif // OVERLAP_NORMALIZATION_H
