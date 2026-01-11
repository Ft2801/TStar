#ifndef COSMETIC_CORRECTION_H
#define COSMETIC_CORRECTION_H

#include <vector>
#include <vector>
#include "ImageBuffer.h"

namespace Stacking {

/**
 * @brief Structure to hold defect data (hot/cold pixels)
 */
struct CosmeticMap {
    std::vector<bool> hotPixels;  ///< Map of hot pixels (true = bad)
    std::vector<bool> coldPixels; ///< Map of cold pixels (true = bad)
    int width = 0;
    int height = 0;
    int count = 0; // Total defects

    bool isValid() const { return width > 0 && height > 0 && count > 0; }
    void clear() {
        hotPixels.clear();
        coldPixels.clear();
        width = 0;
        height = 0;
        count = 0;
    }
};

/**
 * @brief Class for detecting and correcting sensor defects (Hot/Cold pixels)
 */
class CosmeticCorrection {
public:
    /**
     * @brief Detect hot/cold pixels in a Master Dark
     * @param dark: The master dark frame
     * @param hotSigma: Sigma threshold for hot pixels (e.g. 3.0)
     * @param coldSigma: Sigma threshold for cold pixels (e.g. 3.0)
     * @param cfa: Whether to use CFA pattern for local validation
     * @return: Map of defects
     */
    static CosmeticMap findDefects(const ImageBuffer& dark, float hotSigma, float coldSigma, bool cfa = true);

    /**
     * @brief Apply cosmetic correction to an image using a defect map
     * @param image: The image to correct (in-place)
     * @param map: The defect map
     * @param cfa: If true, treats 2x2 Bayer block preservation (CFA aware)
     */
    static void apply(ImageBuffer& image, const CosmeticMap& map, bool cfa = false);

    /**
     * @brief Apply cosmetic correction to an ROI of the image
     * @param image: The ROI image
     * @param map: The global defect map
     * @param offsetX: X offset of ROI in Map
     * @param offsetY: Y offset of ROI in Map
     * @param cfa: If true, treats 2x2 Bayer block preservation
     */
    static void apply(ImageBuffer& image, const CosmeticMap& map, int offsetX, int offsetY, bool cfa = false);

private:
    // Helper to calculate median/sigma of an image
    static void computeStats(const ImageBuffer& img, float& median, float& sigma);
};

} // namespace Stacking

#endif // COSMETIC_CORRECTION_H
