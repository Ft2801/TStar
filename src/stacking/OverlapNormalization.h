#ifndef OVERLAP_NORMALIZATION_H
#define OVERLAP_NORMALIZATION_H

/**
 * @file OverlapNormalization.h
 * @brief Pairwise overlap-based normalization for mosaic stacking.
 *
 * Computes normalization coefficients by analyzing the statistics of
 * overlapping pixel regions between image pairs, then solves for
 * globally consistent intensity adjustments.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "../ImageBuffer.h"
#include "StackingTypes.h"
#include <vector>

namespace Stacking {

class OverlapNormalization {
public:

    /**
     * @brief Statistics measured in the overlapping region of two images.
     */
    struct OverlapStats {
        int    imgI, imgJ;              ///< Indices of the image pair.
        size_t pixelCount;              ///< Number of overlapping pixels used.
        double medianI, medianJ;        ///< Medians in the overlap region.
        double madI,    madJ;           ///< Median absolute deviations.
        double locationI, locationJ;    ///< Robust location estimators.
        double scaleI,    scaleJ;       ///< Robust scale estimators.
    };

    /**
     * @brief Compute the overlap rectangle between two registered images.
     *
     * Uses the translation components of the homography matrices to
     * determine the shared pixel region.
     *
     * @param regI, regJ           Registration data for both images.
     * @param widthI, heightI      Dimensions of image I.
     * @param widthJ, heightJ      Dimensions of image J.
     * @param areaI                Output: overlap rectangle in image I coordinates.
     * @param areaJ                Output: overlap rectangle in image J coordinates.
     * @return Number of overlapping pixels (0 if no overlap).
     */
    static size_t computeOverlapRegion(const RegistrationData& regI,
                                       const RegistrationData& regJ,
                                       int widthI, int heightI,
                                       int widthJ, int heightJ,
                                       QRect& areaI, QRect& areaJ);

    /**
     * @brief Compute robust statistics on the overlapping pixel regions.
     *
     * @param imgI, imgJ   The two image buffers.
     * @param areaI, areaJ Overlap rectangles in each image's coordinate system.
     * @param channel      Color channel to analyze.
     * @param stats        Output: computed overlap statistics.
     * @return true if enough valid pixels were found.
     */
    static bool computeOverlapStats(const ImageBuffer& imgI,
                                    const ImageBuffer& imgJ,
                                    const QRect& areaI, const QRect& areaJ,
                                    int channel, OverlapStats& stats);

    /**
     * @brief Solve for per-image normalization coefficients.
     *
     * Given all pairwise overlap statistics, computes coefficients that
     * minimize intensity differences across all overlapping regions.
     *
     * @param allStats    Vector of all pairwise overlap statistics.
     * @param numImages   Total number of images in the sequence.
     * @param refIndex    Index of the reference image (coefficient = identity).
     * @param additive    true for additive coefficients, false for multiplicative.
     * @param coeffs      Output: one coefficient per image.
     * @return true if the system was solved successfully.
     */
    static bool solveCoefficients(const std::vector<OverlapStats>& allStats,
                                  int numImages, int refIndex, bool additive,
                                  std::vector<double>& coeffs);
};

} // namespace Stacking

#endif // OVERLAP_NORMALIZATION_H