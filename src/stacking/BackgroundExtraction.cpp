/**
 * @file BackgroundExtraction.cpp
 * @brief Implementation of polynomial background model fitting.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "BackgroundExtraction.h"
#include "MathUtils.h"
#include <cmath>

namespace Stacking {

// =============================================================================
// Coefficient Count
// =============================================================================

int BackgroundExtraction::numCoeffs(int degree)
{
    // Number of terms in a 2D polynomial of degree d:
    //   Degree 1 ->  3  (1, x, y)
    //   Degree 2 ->  6  (1, x, y, x^2, xy, y^2)
    //   Degree 3 -> 10
    //   Degree 4 -> 15
    return (degree + 1) * (degree + 2) / 2;
}

// =============================================================================
// Polynomial Evaluation
// =============================================================================

double BackgroundExtraction::evaluatePoly(double x, double y, int degree,
                                          const std::vector<double>& c)
{
    if (c.empty()) return 0.0;

    double sum = 0.0;
    int idx    = 0;

    for (int d = 0; d <= degree; ++d) {
        for (int k = 0; k <= d; ++k) {
            int px = d - k;   // Power of x.
            int py = k;       // Power of y.

            if (idx < static_cast<int>(c.size())) {
                sum += c[idx] * std::pow(x, px) * std::pow(y, py);
            }
            ++idx;
        }
    }

    return sum;
}

// =============================================================================
// Model Generation
// =============================================================================

bool BackgroundExtraction::generateModel(int width, int height,
                                         const std::vector<BackgroundSample>& samples,
                                         ModelType degree,
                                         ImageBuffer& model)
{
    if (samples.empty()) return false;

    const int nCoeffs  = numCoeffs(degree);
    const int nSamples = static_cast<int>(samples.size());

    // The system is underdetermined if there are fewer samples than coefficients.
    if (nSamples < nCoeffs) return false;

    // -------------------------------------------------------------------------
    // Build Normal Equations:  (A^T A) x = A^T b
    //
    //   A is [nSamples x nCoeffs]
    //   A^T A is [nCoeffs x nCoeffs]  (symmetric positive-definite when solvable)
    // -------------------------------------------------------------------------

    std::vector<double> ata(nCoeffs * nCoeffs, 0.0);
    std::vector<double> atb(nCoeffs, 0.0);

    for (const auto& s : samples) {
        double val = s.value;

        // Normalize coordinates to [-1, 1] for numerical stability.
        double xn = (static_cast<double>(s.x) / width)  * 2.0 - 1.0;
        double yn = (static_cast<double>(s.y) / height) * 2.0 - 1.0;

        // Generate polynomial basis terms for this sample.
        std::vector<double> terms;
        terms.reserve(nCoeffs);
        for (int d = 0; d <= degree; ++d) {
            for (int k = 0; k <= d; ++k) {
                terms.push_back(std::pow(xn, d - k) * std::pow(yn, k));
            }
        }

        // Accumulate into A^T A and A^T b.
        for (int i = 0; i < nCoeffs; ++i) {
            for (int j = 0; j < nCoeffs; ++j) {
                ata[i * nCoeffs + j] += terms[i] * terms[j];
            }
            atb[i] += terms[i] * val;
        }
    }

    // -------------------------------------------------------------------------
    // Solve for polynomial coefficients via Gaussian elimination.
    // -------------------------------------------------------------------------

    std::vector<double> coeffs(nCoeffs);
    if (!MathUtils::solveLinearSystem(nCoeffs, ata, atb, coeffs)) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Evaluate the polynomial surface at every pixel to produce the model.
    // -------------------------------------------------------------------------

    model = ImageBuffer(width, height, 1);
    float* data = model.data().data();

    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        double yn = (static_cast<double>(y) / height) * 2.0 - 1.0;

        for (int x = 0; x < width; ++x) {
            double xn = (static_cast<double>(x) / width) * 2.0 - 1.0;

            // Evaluate using the same term ordering as the fitting step.
            double sum = 0.0;
            int idx    = 0;
            for (int d = 0; d <= degree; ++d) {
                for (int k = 0; k <= d; ++k) {
                    sum += coeffs[idx] * std::pow(xn, d - k) * std::pow(yn, k);
                    ++idx;
                }
            }

            data[y * width + x] = static_cast<float>(sum);
        }
    }

    return true;
}

} // namespace Stacking