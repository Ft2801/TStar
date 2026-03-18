#pragma once
/*
 * TGVDenoise.h  —  Total Generalised Variation denoising (TGV-L2, order 2)
 *
 * Algorithm: Chambolle–Pock primal-dual splitting for the TGV²-L2 problem
 *
 *   min_{u,v}  (λ/2)·‖u − f‖²  +  α₁·‖∇u − v‖₁  +  α₀·‖εv‖₁
 *
 * where  εv = ½(∇v + ∇vᵀ)  is the symmetrised gradient of the vector field v.
 *
 *  Reference:
 *    Knoll F., Bredies K., Pock T., Stollberger R. (2011)
 *    "Second order total generalized variation (TGV) for MRI"
 *    Magnetic Resonance in Medicine 65(2):480-491.
 *
 *  Per-channel processing; supports mono (1ch) and colour (3ch) ImageBuffer.
 *  Multi-threaded via OpenMP.
 *
 *  Author: TStar project
 */

#ifndef TGVDENOISE_H
#define TGVDENOISE_H

#include <cstddef>
#include <functional>
#include <vector>
#include "ImageBuffer.h"   // float* data, width, height, channels

// ─── Parameter block ─────────────────────────────────────────────────────────
struct TGVParams {
    // Regularisation weights
    double alpha0   = 0.5;    ///< weight for symmetrised gradient term (TGV order 0)
    double alpha1   = 1.0;    ///< weight for first-order gradient term  (TGV order 1)
    double lambda   = 100.0;  ///< data fidelity weight (higher = less smoothing)

    // Solver settings
    int    maxIter  = 500;    ///< maximum primal-dual iterations
    double tol      = 1e-5;   ///< relative primal-dual gap convergence tolerance

    // Step sizes (auto if ≤ 0)
    double tau      = 0.0;
    double sigma    = 0.0;

    // Channel coupling
    bool   perChannel = true; ///< process each channel independently (recommended)

    // Optional progress callback (0..100, status text)
    std::function<void(int, const QString&)> progressCallback;
};

// ─── Result ──────────────────────────────────────────────────────────────────
struct TGVResult {
    bool   success    = false;
    int    iterations = 0;     ///< actual iterations performed
    double finalGap   = 0.0;   ///< final primal-dual gap
    QString errorMsg;
};

// ─── Main algorithm class ─────────────────────────────────────────────────────
class TGVDenoise {
public:
    TGVDenoise() = default;

    /// Denoise a single-channel float plane in-place.
    /// \param data   pointer to width×height float pixels (normalised [0,1])
    /// \param w      image width
    /// \param h      image height
    /// \param p      algorithm parameters
    /// \param result optional output diagnostics
    static void denoisePlane(float* data, int w, int h,
                             const TGVParams& p,
                             TGVResult* result = nullptr);

    /// Denoise an ImageBuffer (all channels) using the supplied parameters.
    static TGVResult denoiseBuffer(ImageBuffer& buf, const TGVParams& p);

private:
    // Internal helpers ─────────────────────────────────────────────────────────

    /// Evaluate primal energy (for convergence check, sampled every N iters)
    static double primalEnergy(const std::vector<float>& u,
                               const std::vector<float>& f,
                               const std::vector<float>& vx,
                               const std::vector<float>& vy,
                               int w, int h,
                               double lambda, double alpha0, double alpha1);

    /// Projection onto the unit L∞ ball of the dual vector field p1 (|p|≤α1)
    static inline void projectP1(double& px, double& py, double alpha1);

    /// Projection onto the unit L∞ ball of the dual tensor field p0 (|q|≤α0)
    static inline void projectP0(double& qxx, double& qxy, double& qyx, double& qyy,
                                 double alpha0);
};

#endif // TGVDENOISE_H
