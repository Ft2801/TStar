#ifndef ANSCOMBE_TRANSFORM_H
#define ANSCOMBE_TRANSFORM_H

/**
 * @file AnscombeTransform.h
 * @brief Anscombe variance-stabilizing transform for Poisson-Gaussian noise.
 *
 * Provides forward and inverse transforms that convert Poisson or
 * Poisson-Gaussian distributed data into approximately Gaussian data
 * with constant variance. This is essential for applying Gaussian-based
 * denoising algorithms to photon-counting imagery.
 *
 * References:
 *   - Anscombe, F.J. (1948). "The transformation of Poisson variables."
 *   - Makitalo, M. & Foi, A. (2011). "Optimal inversion of the Anscombe
 *     transformation in finite-count photon imaging."
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include <cmath>
#include <vector>
#include <algorithm>

namespace Stacking {

class AnscombeTransform {
public:

    // =========================================================================
    // Generalized Anscombe Transform (Poisson-Gaussian model)
    // =========================================================================

    /**
     * @brief Apply the generalized forward Anscombe transform in-place.
     *
     * Stabilizes the variance of data following a Poisson-Gaussian mixture:
     *   y = gain * x + readNoise
     *
     * The transformed value is:
     *   z = (2 / gain) * sqrt(max(gain * x + addTerm, 0))
     * where addTerm = gain^2 * 3/8 + sigma_rn^2 - gain * mu_rn.
     *
     * @param data            Pixel array to transform (modified in-place).
     * @param count           Number of elements in the array.
     * @param gain            Detector gain (e-/ADU).
     * @param readNoiseMean   Mean of the read noise distribution.
     * @param readNoiseSigma  Standard deviation of the read noise distribution.
     */
    static void forward(float* data, size_t count,
                        float gain, float readNoiseMean, float readNoiseSigma)
    {
        const float addTerm = gain * gain * 0.375f
                            + readNoiseSigma * readNoiseSigma
                            - gain * readNoiseMean;
        const float factor  = 2.0f / gain;

        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            float y  = gain * data[i] + addTerm;
            data[i]  = factor * std::sqrt(std::max(y, 0.0f));
        }
    }

    /**
     * @brief Apply the generalized inverse Anscombe transform in-place.
     *
     * Uses the closed-form algebraic approximation by Makitalo & Foi to
     * recover the original signal from the stabilized domain.
     *
     * @param data            Stabilized pixel array (modified in-place).
     * @param count           Number of elements in the array.
     * @param gain            Detector gain (e-/ADU).
     * @param readNoiseMean   Mean of the read noise distribution.
     * @param readNoiseSigma  Standard deviation of the read noise distribution.
     */
    static void inverse(float* data, size_t count,
                        float gain, float readNoiseMean, float readNoiseSigma)
    {
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            float z = std::max(data[i], 1.0f);

            // Closed-form approximation (Makitalo & Foi, 2011)
            float exactInv = 0.25f * z * z
                            + 0.25f * std::sqrt(1.5f) / z
                            - 1.375f / (z * z)
                            + 0.625f * std::sqrt(1.5f) / (z * z * z)
                            - 0.125f
                            - readNoiseSigma * readNoiseSigma;

            exactInv  = std::max(0.0f, exactInv);
            exactInv *= gain;
            exactInv += readNoiseMean;

            // Guard against NaN propagation
            if (exactInv != exactInv) {
                exactInv = 0.0f;
            }

            data[i] = exactInv;
        }
    }

    // =========================================================================
    // Simple Anscombe Transform (pure Poisson model)
    // =========================================================================

    /**
     * @brief Apply the classical forward Anscombe transform (Poisson-only).
     *
     * z = 2 * sqrt(x + 3/8)
     *
     * @param data   Pixel array to transform (modified in-place).
     * @param count  Number of elements in the array.
     */
    static void forwardSimple(float* data, size_t count)
    {
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            data[i] = 2.0f * std::sqrt(data[i] + 0.375f);
        }
    }

    /**
     * @brief Apply the classical inverse Anscombe transform (Poisson-only).
     *
     * Uses the same Makitalo & Foi algebraic approximation without the
     * read-noise correction terms.
     *
     * @param data   Stabilized pixel array (modified in-place).
     * @param count  Number of elements in the array.
     */
    static void inverseSimple(float* data, size_t count)
    {
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            float z   = data[i];
            float inv = 0.25f * z * z
                      + 0.25f * std::sqrt(1.5f) / z
                      - 1.375f / (z * z)
                      + 0.625f * std::sqrt(1.5f) / (z * z * z)
                      - 0.125f;
            data[i] = std::max(0.0f, inv);
        }
    }
};

} // namespace Stacking

#endif // ANSCOMBE_TRANSFORM_H