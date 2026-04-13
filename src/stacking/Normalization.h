#ifndef STACKING_NORMALIZATION_H
#define STACKING_NORMALIZATION_H

/**
 * @file Normalization.h
 * @brief Image normalization for stacking.
 *
 * Computes and applies per-frame normalization coefficients that equalise
 * location (background level) and scale (noise amplitude) across frames
 * before they are combined.  Supports additive, multiplicative, and
 * mixed modes, as well as full-image and overlap-region strategies.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "StackingTypes.h"
#include "StackingSequence.h"
#include "Statistics.h"
#include "../ImageBuffer.h"
#include <vector>

namespace Stacking {

/**
 * @brief Static utility class for computing and applying normalization.
 *
 * Two-stage pipeline:
 *   Stage 1 -- Estimate location and scale for every image and channel.
 *   Stage 2 -- Derive per-frame coefficients relative to the reference.
 *
 * Per-pixel application formulae:
 *   Additive / AdditiveScaling:            pixel * pscale - poffset
 *   Multiplicative / MultiplicativeScaling: pixel * pscale * pmul
 */
class Normalization {
public:

    // -- Coefficient computation ---------------------------------------------

    /**
     * @brief Compute normalization coefficients for a complete sequence.
     *
     * Delegates to either computeFullImageNormalization() or
     * computeOverlapNormalization() depending on the stacking parameters.
     *
     * @param sequence         Image sequence (provides image access).
     * @param params           Stacking configuration.
     * @param coefficients     Output: computed normalization coefficients.
     * @param progressCallback Optional progress feedback.
     * @return true on success.
     */
    static bool computeCoefficients(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback = nullptr);

    // -- Per-pixel / per-image application -----------------------------------

    /**
     * @brief Normalize a single pixel value.
     *
     * @param pixel        Raw pixel value.
     * @param normType     Normalization method selector.
     * @param imageIndex   Index of the source frame.
     * @param layer        Colour channel (0 = R/mono, 1 = G, 2 = B).
     * @param coefficients Pre-computed coefficients.
     * @return Normalized pixel value.
     */
    static float applyToPixel(float pixel,
                              NormalizationMethod normType,
                              int imageIndex,
                              int layer,
                              const NormCoefficients& coefficients);

    /**
     * @brief Normalize an entire image buffer in place.
     *
     * @param buffer       Image data (modified).
     * @param normType     Normalization method selector.
     * @param imageIndex   Index of the source frame.
     * @param coefficients Pre-computed coefficients.
     */
    static void applyToImage(ImageBuffer& buffer,
                             NormalizationMethod normType,
                             int imageIndex,
                             const NormCoefficients& coefficients);

    // -- Post-stack helpers --------------------------------------------------

    /**
     * @brief Normalize the final stacked image to [0, 1].
     *
     * Zero pixels (no-data regions) are preserved as zero.
     *
     * @param buffer Stacked image (modified in place).
     */
    static void normalizeOutput(ImageBuffer& buffer);

    /**
     * @brief Equalize RGB channels by matching their medians.
     *
     * Scales each channel so that its median matches that of the
     * reference channel (default: green).
     *
     * @param buffer           Image data (modified in place).
     * @param referenceChannel Channel whose median is the target (0/1/2).
     */
    static void equalizeRGB(ImageBuffer& buffer, int referenceChannel = 1);

private:

    /**
     * @brief Per-image statistics used during coefficient computation.
     */
    struct ImageStats {
        double location = 0.0;  ///< Robust location (median or IKSS centre)
        double scale    = 1.0;  ///< Robust scale (MAD-based or biweight)
        double median   = 0.0;  ///< Simple median
        double mad      = 0.0;  ///< Median absolute deviation
        bool   valid    = false;
    };

    /**
     * @brief Compute location/scale statistics for one channel of one image.
     */
    static bool computeImageStats(const ImageBuffer& buffer,
                                  int layer,
                                  bool fastMode,
                                  ImageStats& stats);

    /**
     * @brief Full-image normalization (all pixels participate).
     */
    static bool computeFullImageNormalization(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback);

    /**
     * @brief Overlap-region normalization (mosaic / panorama mode).
     */
    static bool computeOverlapNormalization(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback);

    /**
     * @brief Find the overlapping rectangle between two registered images.
     */
    static bool findOverlap(const SequenceImage& img1,
                            const SequenceImage& img2,
                            int& x, int& y,
                            int& width, int& height);
};

} // namespace Stacking

#endif // STACKING_NORMALIZATION_H