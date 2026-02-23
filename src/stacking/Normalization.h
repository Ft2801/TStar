
#ifndef STACKING_NORMALIZATION_H
#define STACKING_NORMALIZATION_H

#include "StackingTypes.h"
#include "StackingSequence.h"
#include "Statistics.h"
#include "../ImageBuffer.h"
#include <vector>

namespace Stacking {

/**
 * @brief Image normalization for stacking
 * 
 * Computes and applies normalization coefficients to equalize
 * images before stacking. Supports various normalization modes
 * and can work on full images or overlapping regions.
 */
class Normalization {
public:
    /**
     * @brief Compute normalization coefficients for a sequence
     * 
     * @param sequence Image sequence
     * @param params Stacking parameters
     * @param coefficients Output: computed coefficients
     * @param progressCallback Progress callback
     * @return true if successful
     */
    static bool computeCoefficients(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback = nullptr
    );
    
    /**
     * @brief Apply normalization to a pixel value
     * 
     * @param pixel Original pixel value
     * @param params Normalization parameters
     * @param imageIndex Index of source image
     * @param layer Color layer (0=R/mono, 1=G, 2=B)
     * @param coefficients Normalization coefficients
     * @return Normalized pixel value
     */
    static float applyToPixel(
        float pixel,
        NormalizationMethod normType,
        int imageIndex,
        int layer,
        const NormCoefficients& coefficients
    );
    
    /**
     * @brief Apply normalization to an entire image buffer
     * 
     * @param buffer Image buffer (modified in place)
     * @param params Normalization parameters
     * @param imageIndex Index of source image
     * @param coefficients Normalization coefficients
     */
    static void applyToImage(
        ImageBuffer& buffer,
        NormalizationMethod normType,
        int imageIndex,
        const NormCoefficients& coefficients
    );
    
    /**
     * @brief Normalize final stacked result to [0, 1] range
     * 
     * @param buffer Stacked result (modified in place)
     */
    static void normalizeOutput(ImageBuffer& buffer);
    
    /**
     * @brief Equalize RGB channels
     * 
     * Adjusts RGB channels to have similar statistics.
     * 
     * @param buffer Image buffer (modified in place)
     * @param referenceChannel Channel to use as reference (default: green=1)
     */
    static void equalizeRGB(ImageBuffer& buffer, int referenceChannel = 1);
    
private:
    /**
     * @brief Statistics computed for normalization
     */
    struct ImageStats {
        double location = 0.0;   ///< Location estimate (mean or median)
        double scale = 1.0;      ///< Scale estimate (sigma or MAD)
        double median = 0.0;     ///< Median value
        double mad = 0.0;        ///< Median Absolute Deviation
        bool valid = false;
    };
    
    /**
     * @brief Compute statistics for a single image
     */
    static bool computeImageStats(
        const ImageBuffer& buffer,
        int layer,
        bool fastMode,
        ImageStats& stats
    );
    
    /**
     * @brief Compute normalization coefficients using full images
     */
    static bool computeFullImageNormalization(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback
    );
    
    /**
     * @brief Compute normalization coefficients using overlapping regions
     * 
     * For mosaic stacking where images partially overlap.
     */
    static bool computeOverlapNormalization(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback
    );


    /**
     * @brief Find overlapping region between two images
     */
    static bool findOverlap(
        const SequenceImage& img1,
        const SequenceImage& img2,
        int& x, int& y, int& width, int& height
    );
};

} // namespace Stacking

#endif // STACKING_NORMALIZATION_H
