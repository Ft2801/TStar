#ifndef ABEMATH_H
#define ABEMATH_H

#include <vector>
#include <functional>

namespace AbeMath {
    
    struct Point {
        float x, y;
    };

    struct Sample {
        float x, y;
        float z; // Intensity
    };
    
    // Polynomial
    std::vector<float> fitPolynomial(const std::vector<Sample>& samples, int degree);
    float evalPolynomial(float x, float y, const std::vector<float>& coeffs, int degree);
    
    // RBF
    struct RbfModel {
        std::vector<Sample> centers;
        std::vector<float> weights;
        float smooth;
    };
    
    RbfModel fitRbf(const std::vector<Sample>& samples, float smooth);
    float evalRbf(float x, float y, const RbfModel& model);
    
    // Sampling
    float getMedianBox(const std::vector<float>& data, int w, int h, int cx, int cy, int size);
    Point findDimmest(const std::vector<float>& data, int w, int h, int cx, int cy, int patchSize);
    
    // Automatic Sample Generation
    std::vector<Point> generateSamples(const std::vector<float>& data, int w, int h, int numSamples, int patchSize, const std::vector<bool>& exclusionMask);
}

#endif // ABEMATH_H
