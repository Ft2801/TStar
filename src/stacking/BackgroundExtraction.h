#ifndef BACKGROUND_EXTRACTION_H
#define BACKGROUND_EXTRACTION_H

/**
 * @file BackgroundExtraction.h
 * @brief Polynomial background model generation from spatial samples.
 *
 * Fits a 2D polynomial surface to a set of background sample points
 * and produces a full-resolution background model image. This is used
 * to flatten gradients caused by light pollution, vignetting, or
 * other large-scale illumination non-uniformities.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "../ImageBuffer.h"
#include <vector>
#include <QPointF>

namespace Stacking {

/**
 * @brief Measured background sample at a specific image location.
 */
struct BackgroundSample {
    int   x;        ///< Horizontal pixel coordinate of the sample center.
    int   y;        ///< Vertical pixel coordinate of the sample center.
    float value;    ///< Measured background level (typically the median of a local box).
};

/**
 * @brief Generates polynomial background models from spatial samples.
 */
class BackgroundExtraction {
public:
    /**
     * @brief Polynomial degree for the background model surface.
     */
    enum ModelType {
        Degree1 = 1,    ///< Planar (tilted flat field).
        Degree2 = 2,    ///< Quadratic (simple vignetting).
        Degree3 = 3,    ///< Cubic.
        Degree4 = 4     ///< Quartic (complex gradients).
    };

    /**
     * @brief Generate a background model image from spatial samples.
     *
     * Solves the least-squares normal equations (A^T A) x = A^T b for
     * the polynomial coefficients, then evaluates the surface at every
     * pixel to produce the output model image.
     *
     * @param width    Width of the output model (pixels).
     * @param height   Height of the output model (pixels).
     * @param samples  Vector of measured background samples.
     * @param degree   Polynomial degree (1..4).
     * @param model    Output image buffer (resized automatically).
     * @return true if the system was solved successfully, false otherwise.
     */
    static bool generateModel(int width, int height,
                              const std::vector<BackgroundSample>& samples,
                              ModelType degree,
                              ImageBuffer& model);

    /**
     * @brief Evaluate a 2D polynomial at a specific coordinate.
     *
     * @param x       Horizontal coordinate.
     * @param y       Vertical coordinate.
     * @param degree  Polynomial degree.
     * @param coeffs  Flattened coefficient vector (term order matches generateModel).
     * @return Polynomial value at (x, y).
     */
    static double evaluatePoly(double x, double y, int degree,
                               const std::vector<double>& coeffs);

private:
    /**
     * @brief Compute the number of coefficients for a given polynomial degree.
     *
     * For a 2D polynomial of degree d, the count is (d+1)(d+2)/2.
     */
    static int numCoeffs(int degree);
};

} // namespace Stacking

#endif // BACKGROUND_EXTRACTION_H