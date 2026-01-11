#ifndef ANSCOMBE_TRANSFORM_H
#define ANSCOMBE_TRANSFORM_H

#include <cmath>
#include <vector>
#include <algorithm>

namespace Stacking {

class AnscombeTransform {
public:

    static void forward(float* data, size_t count, float gain, float readNoiseMean, float readNoiseSigma) {
        float addTerm = gain * gain * 0.375f + readNoiseSigma * readNoiseSigma - gain * readNoiseMean;
        float factor = 2.0f / gain;
        
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            float y = gain * data[i] + addTerm;
            data[i] = factor * std::sqrt(std::max(y, 0.0f));
        }
    }
    
    static void inverse(float* data, size_t count, float gain, float readNoiseMean, float readNoiseSigma) {
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            float z = std::max(data[i], 1.0f);
            
            // Closed-form approximation (Makitalo & Foi)
            float exactInv = 0.25f * z * z 
                           + 0.25f * std::sqrt(1.5f) / z
                           - 1.375f / (z * z)
                           + 0.625f * std::sqrt(1.5f) / (z * z * z)
                           - 0.125f
                           - readNoiseSigma * readNoiseSigma;
            
            exactInv = std::max(0.0f, exactInv);
            exactInv *= gain;
            exactInv += readNoiseMean;
            
            // NaN check
            if (exactInv != exactInv) exactInv = 0.0f;
            
            data[i] = exactInv;
        }
    }
    
    /**
     * @brief Simple Anscombe (Poisson-only)
     */
    static void forwardSimple(float* data, size_t count) {
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            data[i] = 2.0f * std::sqrt(data[i] + 0.375f);
        }
    }
    
    /**
     * @brief Simple Anscombe Inverse (Poisson-only)
     */
    static void inverseSimple(float* data, size_t count) {
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            float z = data[i];
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
