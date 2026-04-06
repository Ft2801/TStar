/**
 * @file OverlapNormalization.cpp
 * @brief Implementation of pairwise overlap normalization.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "OverlapNormalization.h"
#include "MathUtils.h"
#include "Statistics.h"
#include <cmath>
#include <algorithm>

namespace Stacking {

// =============================================================================
// Overlap Region Computation
// =============================================================================

size_t OverlapNormalization::computeOverlapRegion(
    const RegistrationData& regI, const RegistrationData& regJ,
    int widthI, int heightI, int widthJ, int heightJ,
    QRect& areaI, QRect& areaJ)
{
    // Extract translation offsets from the homography matrices.
    double dxI = regI.H[0][2],  dyI = regI.H[1][2];
    double dxJ = regJ.H[0][2],  dyJ = regJ.H[1][2];

    // Relative shift of J with respect to I.
    int dx = static_cast<int>(std::round(dxJ - dxI));
    int dy = static_cast<int>(std::round(dyI - dyJ));

    // Overlap in I's coordinate system.
    int x_tli = std::max(0, dx);
    int y_tli = std::max(0, dy);
    int x_bri = std::min(widthI,  dx + widthJ);
    int y_bri = std::min(heightI, dy + heightJ);

    // Overlap in J's coordinate system.
    int x_tlj = std::max(0, -dx);
    int y_tlj = std::max(0, -dy);
    int x_brj = std::min(widthJ,  -dx + widthI);
    int y_brj = std::min(heightJ, -dy + heightI);

    if (x_tli < x_bri && y_tli < y_bri) {
        areaI = QRect(x_tli, y_tli, x_bri - x_tli, y_bri - y_tli);
        areaJ = QRect(x_tlj, y_tlj, x_brj - x_tlj, y_brj - y_tlj);
        return static_cast<size_t>(areaI.width()) * areaI.height();
    }

    return 0;
}

// =============================================================================
// Overlap Statistics
// =============================================================================

bool OverlapNormalization::computeOverlapStats(
    const ImageBuffer& imgI, const ImageBuffer& imgJ,
    const QRect& areaI, const QRect& areaJ,
    int channel, OverlapStats& stats)
{
    if (areaI.width() != areaJ.width() || areaI.height() != areaJ.height()) {
        return false;
    }

    const size_t maxCount = static_cast<size_t>(areaI.width()) * areaI.height();
    if (maxCount < 10) return false;

    std::vector<float> dataI, dataJ;
    dataI.reserve(maxCount);
    dataJ.reserve(maxCount);

    // Collect paired pixel values, skipping zeros (out-of-bounds / masked).
    for (int y = 0; y < areaI.height(); ++y) {
        for (int x = 0; x < areaI.width(); ++x) {
            float vI = imgI.value(areaI.x() + x, areaI.y() + y, channel);
            float vJ = imgJ.value(areaJ.x() + x, areaJ.y() + y, channel);
            if (vI > 0.0f && vJ > 0.0f) {
                dataI.push_back(vI);
                dataJ.push_back(vJ);
            }
        }
    }

    if (dataI.size() < 10) return false;

    stats.pixelCount = dataI.size();
    stats.medianI    = Statistics::quickMedian(dataI);
    stats.medianJ    = Statistics::quickMedian(dataJ);
    stats.madI       = Statistics::mad(dataI, stats.medianI);
    stats.madJ       = Statistics::mad(dataJ, stats.medianJ);

    // Approximate robust location / scale using median and scaled MAD.
    stats.locationI  = stats.medianI;
    stats.locationJ  = stats.medianJ;
    stats.scaleI     = stats.madI * 1.4826;
    stats.scaleJ     = stats.madJ * 1.4826;

    return true;
}

// =============================================================================
// Coefficient Solver
// =============================================================================

static bool solveLinearSystem(std::vector<double>& A, std::vector<double>& b, int n) {
    // Basic Gaussian elimination with partial pivoting
    for (int i = 0; i < n; ++i) {
        // Find pivot
        double maxEl = std::abs(A[i * n + i]);
        int maxRow = i;
        for (int k = i + 1; k < n; ++k) {
            if (std::abs(A[k * n + i]) > maxEl) {
                maxEl = std::abs(A[k * n + i]);
                maxRow = k;
            }
        }

        if (maxEl < 1e-12) return false; // Singular matrix

        // Swap rows
        if (maxRow != i) {
            for (int k = i; k < n; ++k) {
                std::swap(A[maxRow * n + k], A[i * n + k]);
            }
            std::swap(b[maxRow], b[i]);
        }

        // Eliminate
        for (int k = i + 1; k < n; ++k) {
            double c = -A[k * n + i] / A[i * n + i];
            for (int j = i; j < n; ++j) {
                if (i == j) {
                    A[k * n + j] = 0;
                } else {
                    A[k * n + j] += c * A[i * n + j];
                }
            }
            b[k] += c * b[i];
        }
    }

    // Back substitution
    for (int i = n - 1; i >= 0; i--) {
        b[i] = b[i] / A[i * n + i];
        for (int k = i - 1; k >= 0; k--) {
            b[k] -= A[k * n + i] * b[i];
        }
    }
    return true;
}

bool OverlapNormalization::solveCoefficients(
    const std::vector<OverlapStats>& allStats,
    int numImages, int refIndex, bool additive,
    std::vector<double>& coeffs)
{
    // Initialize: identity coefficients (0 for additive, 1 for multiplicative).
    coeffs.assign(numImages, additive ? 0.0 : 1.0);

    if (numImages <= 1) return true;

    // We build an NxN linear system A*x = b
    // A minimizes the sum of squared differences over overlapping regions.
    std::vector<double> A(numImages * numImages, 0.0);
    std::vector<double> b(numImages, 0.0);

    for (const auto& stat : allStats) {
        int i = stat.imgI;
        int j = stat.imgJ;
        int w = static_cast<int>(stat.pixelCount); // Weight of this overlap
        
        if (w <= 0) continue;

        double delta = 0.0;
        if (additive) {
            // We want c_i + loc_i = c_j + loc_j  => c_i - c_j = loc_j - loc_i
            delta = stat.locationJ - stat.locationI;
        } else {
            // We want c_i * scale_i = c_j * scale_j => log(c_i) - log(c_j) = log(scale_j) - log(scale_i)
            if (stat.scaleI <= 0.0 || stat.scaleJ <= 0.0) continue;
            delta = std::log(stat.scaleJ) - std::log(stat.scaleI);
        }

        // Add to normal equations
        A[i * numImages + i] += w;
        A[j * numImages + j] += w;
        A[i * numImages + j] -= w;
        A[j * numImages + i] -= w;

        b[i] += w * delta;
        b[j] -= w * delta;
    }

    // Pin the reference image to prevent singular matrix (infinite solutions)
    if (refIndex >= 0 && refIndex < numImages) {
        for (int j = 0; j < numImages; ++j) {
            A[refIndex * numImages + j] = 0.0;
        }
        A[refIndex * numImages + refIndex] = 1.0;
        b[refIndex] = 0.0; // c_ref = 0
    } else {
        // If no explicit reference, pin the first image
        for (int j = 0; j < numImages; ++j) {
            A[0 * numImages + j] = 0.0;
        }
        A[0 * numImages + 0] = 1.0;
        b[0] = 0.0;
    }

    // Solve Ax = b
    if (!solveLinearSystem(A, b, numImages)) {
        return false; // System is singular and could not be solved.
    }

    // Output results
    for (int k = 0; k < numImages; ++k) {
        if (additive) {
            coeffs[k] = b[k];
        } else {
            coeffs[k] = std::exp(b[k]);
        }
    }

    return true;
}

} // namespace Stacking