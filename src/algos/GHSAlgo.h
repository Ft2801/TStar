#ifndef GHSALGO_H
#define GHSALGO_H

#include <cmath>
#include <algorithm>
#include <vector>


namespace GHSAlgo {

    enum StretchType {
        STRETCH_LINEAR = 0,
        STRETCH_PAYNE_NORMAL = 1,
        STRETCH_PAYNE_INVERSE = 2,
        STRETCH_ASINH = 3,
        STRETCH_INVASINH = 4
    };

    struct GHSComputeParams {
        float qlp, q0, qwp, q1, q;
        float b1, a2, b2, c2, d2, a3, b3, c3, d3, a4, b4;
        float e2, e3; // Exponents for power functions
        float a1, LPT, SPT, HPT; // Additional params
    };

    struct GHSParams {
        float D, B, SP, LP, HP, BP;
        StretchType type;
        // Color channels are handled by caller
    };

    void setup(GHSComputeParams& c, float B, float D, float LP, float SP, float HP, int stretchtype);
    float compute(float in, const GHSParams& params, const GHSComputeParams& c);

    // Apply to a data buffer
    void applyToBuffer(std::vector<float>& data, const GHSParams& params);

}

#endif // GHSALGO_H
