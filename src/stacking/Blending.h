
#ifndef BLENDING_H
#define BLENDING_H

#include "../ImageBuffer.h"
#include <vector>

namespace Stacking {

/**
 * @brief Blend mask generation and application
 * 
 * Creates feathered blend masks for smooth transitions
 * at image boundaries during stacking.
 */
class Blending {
public:
    /**
     * @brief Generate a blend mask from an image
     * 
     * Creates a mask where edge pixels are feathered
     * to allow smooth blending with other images.
     * 
     * @param image Source image
     * @param featherDistance Number of pixels to feather from edge
     * @param mask Output mask (0-1 values)
     */
    static void generateBlendMask(const ImageBuffer& image,
                                  int featherDistance,
                                  std::vector<float>& mask);
    
    /**
     * @brief Generate a blend mask from non-zero pixels
     * 
     * Creates a mask where zero pixels mark the boundary
     * and the feather extends inward.
     * 
     * @param image Source image
     * @param featherDistance Number of pixels to feather
     * @param mask Output mask
     */
    static void generateNonZeroMask(const ImageBuffer& image,
                                    int featherDistance,
                                    std::vector<float>& mask);
    
    /**
     * @brief Apply blend masks during stacking
     * 
     * Blends multiple images using their corresponding masks.
     * 
     * @param images Vector of images to blend
     * @param masks Vector of blend masks
     * @param output Output blended image
     */
    static void applyBlending(const std::vector<const ImageBuffer*>& images,
                              const std::vector<std::vector<float>>& masks,
                              ImageBuffer& output);
    
    /**
     * @brief Compute distance from edge for each pixel
     * 
     * Uses euclidean distance transform from image borders
     * or zero-pixel boundaries.
     * 
     * @param image Source image
     * @param distanceMap Output distance values
     */
    static void computeDistanceTransform(const ImageBuffer& image,
                                         std::vector<float>& distanceMap);
    
    /**
     * @brief Apply feather function to distance values
     * 
     * Converts distance to blend weight using smooth falloff.
     * 
     * @param distance Distance from edge
     * @param featherDistance Feather width
     * @return Blend weight (0-1)
     */
    static float featherFunction(float distance, int featherDistance);
    
private:
    /**
     * @brief Compute distance to nearest zero pixel
     * 
     * Fast approximation using Manhattan distance
     */
    static void computeDistanceFromZeros(const float* data,
                                         int width, int height,
                                         std::vector<float>& distances);
    
    /**
     * @brief Smooth edge transition function
     */
    static float smoothStep(float edge0, float edge1, float x);
};

} // namespace Stacking

#endif // BLENDING_H
