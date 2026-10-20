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
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
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