#ifndef BLENDING_H
#define BLENDING_H

/**
 * @file Blending.h
 * @brief Feathered blend-mask generation and multi-image blending.
 *
 * Creates smooth blend masks based on pixel distance from image edges
 * or from zero-valued (no-data) boundaries, then applies weighted
 * averaging to composite multiple overlapping images seamlessly.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "../ImageBuffer.h"
#include <vector>

namespace Stacking {

class Blending {
public:

    // =========================================================================
    // Mask Generation
    // =========================================================================

    /**
     * @brief Generate a blend mask from image edge distances.
     *
     * Pixels near the border of the image receive lower weight,
     * creating a smooth transition for mosaics and stacking.
     *
     * @param image            Source image (used for dimensions only).
     * @param featherDistance   Width of the feathering zone in pixels.
     * @param mask              Output mask with values in [0, 1].
     */
    static void generateBlendMask(const ImageBuffer& image,
                                  int featherDistance,
                                  std::vector<float>& mask);

    /**
     * @brief Generate a blend mask from non-zero pixel boundaries.
     *
     * Zero-valued pixels define the boundary; the feather extends
     * inward from that boundary.
     *
     * @param image            Source image.
     * @param featherDistance   Width of the feathering zone in pixels.
     * @param mask              Output mask with values in [0, 1].
     */
    static void generateNonZeroMask(const ImageBuffer& image,
                                    int featherDistance,
                                    std::vector<float>& mask);

    // =========================================================================
    // Blending Application
    // =========================================================================

    /**
     * @brief Blend multiple images using their corresponding masks.
     *
     * Computes a weighted average at each pixel, where the weight
     * comes from the per-image blend mask. Zero-valued pixels in any
     * source image are treated as missing data.
     *
     * @param images  Vector of pointers to source images.
     * @param masks   Vector of blend masks (one per image).
     * @param output  Output blended image.
     */
    static void applyBlending(const std::vector<const ImageBuffer*>& images,
                              const std::vector<std::vector<float>>& masks,
                              ImageBuffer& output);

    // =========================================================================
    // Distance Computation
    // =========================================================================

    /**
     * @brief Compute the distance from each pixel to the nearest zero-valued boundary.
     *
     * Uses a two-pass Manhattan-distance approximation (forward + backward).
     *
     * @param image        Source image.
     * @param distanceMap  Output per-pixel distance values.
     */
    static void computeDistanceTransform(const ImageBuffer& image,
                                         std::vector<float>& distanceMap);

    /**
     * @brief Convert a distance value to a blend weight via smooth falloff.
     *
     * @param distance         Distance from the nearest boundary (pixels).
     * @param featherDistance   Total feathering width (pixels).
     * @return Blend weight in [0, 1].
     */
    static float featherFunction(float distance, int featherDistance);

private:

    /**
     * @brief Two-pass Manhattan distance transform from zero-valued pixels.
     */
    static void computeDistanceFromZeros(const float* data,
                                         int width, int height,
                                         std::vector<float>& distances);

    /**
     * @brief Hermite smooth-step interpolation.
     *
     * Returns t^2 * (3 - 2t) where t = clamp((x - edge0) / (edge1 - edge0), 0, 1).
     */
    static float smoothStep(float edge0, float edge1, float x);
};

} // namespace Stacking

#endif // BLENDING_H