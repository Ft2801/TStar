#include "BackgroundExtraction.h"
#include "MathUtils.h"
#include <cmath>

namespace Stacking {

int BackgroundExtraction::numCoeffs(int degree) {
    // Number of coeffs = (d+1)(d+2)/2
    // Deg 1: 3 (1, x, y)
    // Deg 2: 6 (1, x, y, x2, xy, y2)
    // Deg 3: 10
    // Deg 4: 15
    return (degree + 1) * (degree + 2) / 2;
}

double BackgroundExtraction::evaluatePoly(double x, double y, int degree, const std::vector<double>& c) {
    if (c.empty()) return 0.0;
    
    double sum = 0.0;
    int idx = 0;
    for (int d = 0; d <= degree; ++d) {
        for (int k = 0; k <= d; ++k) {
            // Term x^(d-k) * y^k  (e.g. d=1: k=0->x, k=1->y)
            int px = d - k;
            int py = k;
            if (idx < (int)c.size()) {
                sum += c[idx] * std::pow(x, px) * std::pow(y, py);
            }
            idx++;
        }
    }
    return sum;
}

bool BackgroundExtraction::generateModel(int width, int height, 
                                       const std::vector<BackgroundSample>& samples, 
                                       ModelType degree, 
                                       ImageBuffer& model) 
{
    if (samples.empty()) return false;
    
    int nCoeffs = numCoeffs(degree);
    int nSamples = samples.size();
    
    if (nSamples < nCoeffs) return false; // Underdetermined
    
    // Build Normal Equations: (A^T A) x = A^T B
    // A is [nSamples x nCoeffs]
    // A^T A is [nCoeffs x nCoeffs]
    
    std::vector<double> ata(nCoeffs * nCoeffs, 0.0);
    std::vector<double> atb(nCoeffs, 0.0);
    
    // Fill ATA and ATB directly
    for (const auto& s : samples) {
        double x = s.x; // Normalized? Usually better for stability.
        double y = s.y;
        double val = s.value;
        
        // Normalize coordinates to [-1, 1] for numerical stability
        double xn = (x / width) * 2.0 - 1.0;
        double yn = (y / height) * 2.0 - 1.0;
        
        // Generate terms for this sample
        std::vector<double> terms;
        terms.reserve(nCoeffs);
        for (int d = 0; d <= degree; ++d) {
            for (int k = 0; k <= d; ++k) {
                terms.push_back(std::pow(xn, d - k) * std::pow(yn, k));
            }
        }
        
        // Add to ATA and ATB
        for (int i = 0; i < nCoeffs; ++i) {
            for (int j = 0; j < nCoeffs; ++j) {
                ata[i * nCoeffs + j] += terms[i] * terms[j];
            }
            atb[i] += terms[i] * val;
        }
    }
    
    // Solve
    std::vector<double> coeffs(nCoeffs);
    if (!MathUtils::solveLinearSystem(nCoeffs, ata, atb, coeffs)) {
        return false;
    }
    
    // Generate Model Image
    model = ImageBuffer(width, height, 1); // Mono model usually (applied per channel)
    float* data = model.data().data();
    
    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        double yn = (y / (double)height) * 2.0 - 1.0;
        for (int x = 0; x < width; ++x) {
            double xn = (x / (double)width) * 2.0 - 1.0;
            
            // Re-evaluate using helper (modified for normalized coords logic)
            // But helper above assumes generic poly.
            // We must match the term generation order!
            
            double sum = 0.0;
            int idx = 0;
            for (int d = 0; d <= degree; ++d) {
                for (int k = 0; k <= d; ++k) {
                    sum += coeffs[idx] * std::pow(xn, d - k) * std::pow(yn, k);
                    idx++;
                }
            }
            
            data[y * width + x] = static_cast<float>(sum);
        }
    }
    
    return true;
}

} // namespace Stacking
