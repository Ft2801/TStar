#ifndef CATALOGGRADIENTEXTRACTOR_H
#define CATALOGGRADIENTEXTRACTOR_H

#include "../ImageBuffer.h"
#include <functional>

namespace Background {

class CatalogGradientExtractor {
public:
    struct Options {
        int blurScale = 64;             // Gaussian sigma for large-scale extraction (pixels)
        bool protectStars = true;       // Apply morphological filter to suppress stars before blurring
        bool outputGradientMap = false; // If true, output the gradient map instead of the corrected image
    };

    /**
     * @brief Performs catalog-based background gradient extraction.
     *
     * Compares the target image against a reference survey image (from HiPS)
     * to isolate and remove large-scale gradients while preserving color balance.
     *
     * @param target    Image to correct (modified in-place, or replaced with gradient map)
     * @param reference Aligned reference image from HiPS (can be mono or RGB)
     * @param opts      Configuration options
     * @return true if successful
     */
    static bool extract(ImageBuffer& target,
                        const ImageBuffer& reference,
                        const Options& opts,
                        std::function<void(int)> progress = nullptr,
                        const bool* cancelFlag = nullptr);

    /**
     * @brief Computes the gradient map (target_low_freq - matched_reference_low_freq).
     */
    static ImageBuffer computeGradientMap(const ImageBuffer& target,
                                           const ImageBuffer& reference,
                                           const Options& opts);
};

} // namespace Background

#endif // CATALOGGRADIENTEXTRACTOR_H
