/**
 * @file Debayer.h
 * @brief Advanced debayering algorithms
 * 
 * Implements various CFA (Color Filter Array) demosaicing algorithms
 * including VNG and other advanced methods.
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
 * @brief Debayering algorithms implementation
 */
class Debayer {
public:
    
    static bool bilinear(const ImageBuffer& input, ImageBuffer& output,
                        BayerPattern pattern);
    
    static bool vng(const ImageBuffer& input, ImageBuffer& output,
                   BayerPattern pattern);
    
    static bool superpixel(const ImageBuffer& input, ImageBuffer& output,
                          BayerPattern pattern);
    
    static bool ahd(const ImageBuffer& input, ImageBuffer& output,
                   BayerPattern pattern);
    
    static bool rcd(const ImageBuffer& input, ImageBuffer& output,
                   BayerPattern pattern);

    /**
     * @brief Get effective Bayer pattern for a cropped region
     */
    static BayerPattern getPatternForCrop(BayerPattern original, int x, int y);
    
private:
    /**
     * @brief Get offset indices for Bayer pattern
     */
    static void getPatternOffsets(BayerPattern pattern,
                                  int& redRow, int& redCol,
                                  int& blueRow, int& blueCol);
    
    /**
     * @brief Check if coordinate is at red pixel
     */
    static bool isRed(int x, int y, int redRow, int redCol);
    
    /**
     * @brief Check if coordinate is at green pixel
     */
    static bool isGreen(int x, int y, int redRow, int redCol, int blueRow, int blueCol);
    
    /**
     * @brief Check if coordinate is at blue pixel
     */
    static bool isBlue(int x, int y, int blueRow, int blueCol);
    
    /**
     * @brief VNG gradient computation
     */
    static void computeVNGGradients(const float* data, int width, int height,
                                    int x, int y, float gradients[8]);
    
    /**
     * @brief VNG color interpolation
     */
    static void vngInterpolate(const float* data, int width, int height,
                               int x, int y, int redRow, int redCol,
                               int blueRow, int blueCol,
                               float& r, float& g, float& b);
};

} // namespace Preprocessing

#endif // DEBAYER_H
