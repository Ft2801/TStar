#ifndef MATH_UTILS_H
#define MATH_UTILS_H

/**
 * @file MathUtils.h
 * @brief General-purpose linear algebra utilities.
 *
 * Currently provides dense linear system solving via Gaussian elimination
 * with partial pivoting. Used by BackgroundExtraction and other modules
 * that require small dense solvers.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include <vector>
#include <cmath>
#include <algorithm>

namespace Stacking {

class MathUtils {
public:

    /**
     * @brief Solve the linear system Ax = b using Gaussian elimination.
     *
     * Performs partial pivoting for numerical stability. The matrix A
     * is stored in row-major order as a flat vector of size n*n.
     *
     * @param n  System dimension.
     * @param A  Coefficient matrix (n x n, row-major). Modified internally.
     * @param B  Right-hand side vector (length n). Modified internally.
     * @param x  Solution vector (length n, output).
     * @return true if the system was solved, false if the matrix is singular.
     */
    static bool solveLinearSystem(int n,
                                  std::vector<double>& A,
                                  std::vector<double>& B,
                                  std::vector<double>& x)
    {
        // Work on copies to preserve the caller's data.
        std::vector<double> a = A;
        std::vector<double> b = B;
        x.assign(n, 0.0);

        // Forward elimination with partial pivoting.
        for (int i = 0; i < n; ++i) {
            // Find the pivot row.
            int pivot = i;
            for (int j = i + 1; j < n; ++j) {
                if (std::abs(a[j * n + i]) > std::abs(a[pivot * n + i])) {
                    pivot = j;
                }
            }

            // Swap pivot row into position.
            if (pivot != i) {
                for (int j = i; j < n; ++j) {
                    std::swap(a[i * n + j], a[pivot * n + j]);
                }
                std::swap(b[i], b[pivot]);
            }

            // Check for singularity.
            if (std::abs(a[i * n + i]) < 1e-10) {
                return false;
            }

            // Eliminate entries below the pivot.
            for (int j = i + 1; j < n; ++j) {
                double factor = a[j * n + i] / a[i * n + i];
                for (int k = i; k < n; ++k) {
                    a[j * n + k] -= factor * a[i * n + k];
                }
                b[j] -= factor * b[i];
            }
        }

        // Back-substitution.
        for (int i = n - 1; i >= 0; --i) {
            double sum = 0.0;
            for (int j = i + 1; j < n; ++j) {
                sum += a[i * n + j] * x[j];
            }
            x[i] = (b[i] - sum) / a[i * n + i];
        }

        return true;
    }
};

} // namespace Stacking

#endif // MATH_UTILS_H