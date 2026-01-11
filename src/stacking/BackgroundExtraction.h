#ifndef BACKGROUND_EXTRACTION_H
#define BACKGROUND_EXTRACTION_H

#include "../ImageBuffer.h"
#include <vector>
#include <QPointF>

namespace Stacking {

struct BackgroundSample {
    int x, y;
    float value; // Measured background value (median of box)
};

class BackgroundExtraction {
public:
    enum ModelType {
        Degree1 = 1, // Planar
        Degree2 = 2,
        Degree3 = 3,
        Degree4 = 4
    };

    /**
     * @brief Generate a background model image from samples
     * @param width Width of output model
     * @param height Height of output model
     * @param samples List of samples
     * @param degree Polynomial degree
     * @param model Output buffer (resized automatically)
     * @return true if successful
     */
    static bool generateModel(int width, int height, 
                              const std::vector<BackgroundSample>& samples, 
                              ModelType degree, 
                              ImageBuffer& model);

    /**
     * @brief Evaluate polynomial at specific coordinate
     */
    static double evaluatePoly(double x, double y, int degree, const std::vector<double>& coeffs);

private:
    static int numCoeffs(int degree);
};

} // namespace Stacking

#endif // BACKGROUND_EXTRACTION_H
