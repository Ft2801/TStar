#ifndef COSMETIC_CORRECTION_H
#define COSMETIC_CORRECTION_H

/**
 * @file CosmeticCorrection.h
 * @brief Detection and correction of hot and cold sensor pixels.
 *
 * Identifies defective pixels from a master dark frame using a two-stage
 * approach (global sigma screening followed by local validation), then
 * replaces them with the median of their valid neighbors.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include <vector>
#include "ImageBuffer.h"

namespace Stacking {

/**
 * @brief Map of detected sensor defects (hot and cold pixels).
 */
struct CosmeticMap {
    std::vector<bool> hotPixels;    ///< Per-pixel flag: true = hot defect.
    std::vector<bool> coldPixels;   ///< Per-pixel flag: true = cold defect.
    int width  = 0;                 ///< Map width in pixels.
    int height = 0;                 ///< Map height in pixels.
    int count  = 0;                 ///< Total number of defects detected.

    bool isValid() const { return width > 0 && height > 0 && count > 0; }

    void clear() {
        hotPixels.clear();
        coldPixels.clear();
        width  = 0;
        height = 0;
        count  = 0;
    }
};

/**
 * @brief Detects and corrects sensor defects (hot/cold pixels).
 */
class CosmeticCorrection {
public:

    /**
     * @brief Detect hot and cold pixels in a master dark frame.
     *
     * Uses a two-stage detection strategy:
     *   1. Global: flag pixels exceeding median +/- sigma * threshold.
     *   2. Local:  validate each candidate against its neighborhood statistics.
     *
     * @param dark       Master dark frame.
     * @param hotSigma   Sigma threshold for hot pixel detection (e.g. 3.0).
     * @param coldSigma  Sigma threshold for cold pixel detection (e.g. 3.0).
     * @param cfa        If true, use CFA-aware 2x2 stepping for neighborhood.
     * @return Map of detected defects.
     */
    static CosmeticMap findDefects(const ImageBuffer& dark,
                                   float hotSigma, float coldSigma,
                                   bool cfa = true);

    /**
     * @brief Apply cosmetic correction to an entire image.
     *
     * Replaces each defective pixel with the median of its valid neighbors.
     *
     * @param image  Image to correct (modified in-place).
     * @param map    Defect map from findDefects().
     * @param cfa    If true, preserves 2x2 Bayer block structure.
     */
    static void apply(ImageBuffer& image, const CosmeticMap& map,
                      bool cfa = false);

    /**
     * @brief Apply cosmetic correction to a region of interest (ROI).
     *
     * The ROI's pixel at (x, y) maps to the defect map at (x+offsetX, y+offsetY).
     *
     * @param image    ROI image to correct (modified in-place).
     * @param map      Global defect map.
     * @param offsetX  Horizontal offset of the ROI within the map.
     * @param offsetY  Vertical offset of the ROI within the map.
     * @param cfa      If true, preserves 2x2 Bayer block structure.
     */
    static void apply(ImageBuffer& image, const CosmeticMap& map,
                      int offsetX, int offsetY, bool cfa = false);

private:

    /**
     * @brief Compute robust image statistics (median and MAD-based sigma).
     */
    static void computeStats(const ImageBuffer& img,
                             float& median, float& sigma);
};

} // namespace Stacking

#endif // COSMETIC_CORRECTION_H