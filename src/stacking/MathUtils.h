#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <vector>
#include <cmath>
#include <stdexcept>

namespace Stacking {

class MathUtils {
public:
    // Solve Ax = B using Gaussian Elimination
    // A is NxN flattened, B is N
    static bool solveLinearSystem(int n, std::vector<double>& A, std::vector<double>& B, std::vector<double>& x) {
        // Gaussian Elimination with partial pivoting
        
        // Working copies
        std::vector<double> a = A;
        std::vector<double> b = B;
        x.assign(n, 0.0);
        
        for (int i = 0; i < n; ++i) {
            // Find pivot
            int pivot = i;
            for (int j = i + 1; j < n; ++j) {
                if (std::abs(a[j * n + i]) > std::abs(a[pivot * n + i])) {
                    pivot = j;
                }
            }
            
            // Swap rows
            if (pivot != i) {
                for (int j = i; j < n; ++j) std::swap(a[i * n + j], a[pivot * n + j]);
                std::swap(b[i], b[pivot]);
            }
            
            if (std::abs(a[i * n + i]) < 1e-10) return false; // Singular
            
            // Eliminate
            for (int j = i + 1; j < n; ++j) {
                double factor = a[j * n + i] / a[i * n + i];
                for (int k = i; k < n; ++k) {
                    a[j * n + k] -= factor * a[i * n + k];
                }
                b[j] -= factor * b[i];
            }
        }
        
        // Back substitution
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
