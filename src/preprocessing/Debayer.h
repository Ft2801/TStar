/**
 * @file Debayer.h
 * @brief Bayer CFA demosaicing algorithms.
 *
 * Provides multiple demosaicing strategies for converting single-channel
 * Bayer colour-filter-array data into three-channel RGB images.
 * Supported algorithms: bilinear, VNG, super-pixel, AHD, and RCD.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef DEBAYER_H
#define DEBAYER_H

#include "../ImageBuffer.h"
#include "PreprocessingTypes.h"

#include <vector>

namespace Preprocessing {

/**
 * @brief Static utility class implementing Bayer CFA demosaicing algorithms.
 *
 * All public methods accept a single-channel CFA ImageBuffer and produce
 * a three-channel (RGB, planar) output buffer.  Pattern utilities are
 * also provided for adjusting the effective Bayer pattern after a sub-frame
 * crop.
 */
class Debayer {
public:
    // ========================================================================
    //  Demosaicing Algorithms
    // ========================================================================

    /**
     * @brief Bilinear interpolation demosaicing.
     * @param input   Single-channel CFA image.
     * @param output  Resulting three-channel RGB image.
     * @param pattern Bayer pattern of the input data.
     * @return true on success, false if the input is not single-channel.
     */
    static bool bilinear(const ImageBuffer& input, ImageBuffer& output,
                         BayerPattern pattern);

    /**
     * @brief Variable Number of Gradients (VNG) demosaicing.
     */
    static bool vng(const ImageBuffer& input, ImageBuffer& output,
                    BayerPattern pattern);

    /**
     * @brief 2x2 super-pixel binning (output is half resolution).
     */
    static bool superpixel(const ImageBuffer& input, ImageBuffer& output,
                           BayerPattern pattern);

    /**
     * @brief Adaptive Homogeneity-Directed (AHD) demosaicing.
     */
    static bool ahd(const ImageBuffer& input, ImageBuffer& output,
                    BayerPattern pattern);

    /**
     * @brief Ratio Corrected Demosaicing (RCD).
     */
    static bool rcd(const ImageBuffer& input, ImageBuffer& output,
                    BayerPattern pattern);

    /**
     * @brief Edge-aware demosaicing (ported from ChannelOps).
     *        Uses local gradient weights to guide interpolation.
     */
    static bool edgeAware(const ImageBuffer& input, ImageBuffer& output,
                          BayerPattern pattern);

    /**
     * @brief Perform bilinear interpolation at a single pixel location.
     *        Useful for boundary pixels in higher-order algorithms.
     */
    static void bilinearPixel(const float* src, int width, int height,
                              int x, int y, int redRow, int redCol,
                              int blueRow, int blueCol,
                              float& r, float& g, float& b);

    // ========================================================================
    //  Pattern Utilities
    // ========================================================================

    /**
     * @brief Compute the effective Bayer pattern after a sub-frame crop.
     * @param original Original pattern before cropping.
     * @param x        Horizontal crop offset in pixels.
     * @param y        Vertical crop offset in pixels.
     * @return Adjusted Bayer pattern for the cropped region.
     */
    static BayerPattern getPatternForCrop(BayerPattern original, int x, int y);

private:
    // -- Pattern helpers -----------------------------------------------------

    /**
     * @brief Retrieve row/column offsets of red and blue pixels for a pattern.
     */
    static void getPatternOffsets(BayerPattern pattern,
                                 int& redRow, int& redCol,
                                 int& blueRow, int& blueCol);

    /** @brief Test whether (x, y) is a red pixel position.  */
    static bool isRed(int x, int y, int redRow, int redCol);

    /** @brief Test whether (x, y) is a green pixel position. */
    static bool isGreen(int x, int y, int redRow, int redCol,
                        int blueRow, int blueCol);

    /** @brief Test whether (x, y) is a blue pixel position.  */
    static bool isBlue(int x, int y, int blueRow, int blueCol);

    // -- VNG internals -------------------------------------------------------

    /**
     * @brief Compute directional gradients for VNG interpolation.
     * @param data      Raw CFA pixel data.
     * @param width     Image width in pixels.
     * @param height    Image height in pixels.
     * @param x         Pixel x coordinate.
     * @param y         Pixel y coordinate.
     * @param gradients Output array of 8 gradient magnitudes (N, NE, E, ...).
     */
    static void computeVNGGradients(const float* data, int width, int height,
                                    int x, int y, float gradients[8]);

    /**
     * @brief Interpolate RGB values at a single pixel using VNG.
     */
    static void vngInterpolate(const float* data, int width, int height,
                               int x, int y, int redRow, int redCol,
                               int blueRow, int blueCol,
                               float& r, float& g, float& b);
};

} // namespace Preprocessing

#endif // DEBAYER_H