#include "SimdOps.h"
#include <algorithm>
#include <cmath>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

// Fallback scalar implementations
namespace SimdOps {

    static void applyGainRGB_Scalar(float* data, size_t numPixels, float r, float g, float b) {
        for (size_t i = 0; i < numPixels; ++i) {
            data[i * 3 + 0] *= r;
            data[i * 3 + 1] *= g;
            data[i * 3 + 2] *= b;
        }
    }

// NaN-safe clamp to [0,1]: NaN comparisons return false, so !(v>=0) catches NaN.
static inline float safeClamp01(float v) {
    if (!(v >= 0.0f)) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

    static float mtf_scalar(float m, float x) {
        if (x <= 0) return 0;
        if (x >= 1) return 1;
        float numer = (m - 1.0f) * x;
        float denom = (2.0f * m - 1.0f) * x - m;
        return numer / denom;
    }

    static void applySTF_Row_Scalar(const float* src, uint8_t* dst, size_t numPixels, const STFParams& params, bool inverted) {
        for (size_t i = 0; i < numPixels; ++i) {
            float vals[3];
            for (int c = 0; c < 3; ++c) {
                float v = src[i * 3 + c];
                // Sanitize NaN/Inf/out-of-range BEFORE stretch
                v = safeClamp01(v);
                // Shadow / Highlight Normalize
                v = (v - params.shadow[c]) * params.invRange[c];
                // MTF
                v = mtf_scalar(params.midtones[c], safeClamp01(v));
                if (inverted) v = 1.0f - v;
                vals[c] = safeClamp01(v);
            }
            dst[i * 3 + 0] = static_cast<uint8_t>(vals[0] * 255.0f + 0.5f);
            dst[i * 3 + 1] = static_cast<uint8_t>(vals[1] * 255.0f + 0.5f);
            dst[i * 3 + 2] = static_cast<uint8_t>(vals[2] * 255.0f + 0.5f);
        }
    }

}

#ifdef __AVX2__
namespace SimdOps {

    void applyGainRGB(float* data, size_t numPixels, float r, float g, float b) {
        // Prepare constants for 24-float block (3 YMM registers)
        // RGBRGBRG BRGBRGBR GBRGBRGB
        
        // We can just construct them on the fly
        // Block 1: R G B R G B R G
        // Block 2: B R G B R G B R
        // Block 3: G B R G B R G B
        
        __m256 k1 = _mm256_setr_ps(r, g, b, r, g, b, r, g);
        __m256 k2 = _mm256_setr_ps(b, r, g, b, r, g, b, r);
        __m256 k3 = _mm256_setr_ps(g, b, r, g, b, r, g, b);

        size_t i = 0;
        // Process 8 pixels (24 floats) at a time
        for (; i + 8 <= numPixels; i += 8) {
            float* ptr = data + i * 3;
            
            __m256 v1 = _mm256_loadu_ps(ptr);
            __m256 v2 = _mm256_loadu_ps(ptr + 8);
            __m256 v3 = _mm256_loadu_ps(ptr + 16);
            
            v1 = _mm256_mul_ps(v1, k1);
            v2 = _mm256_mul_ps(v2, k2);
            v3 = _mm256_mul_ps(v3, k3);
            
            _mm256_storeu_ps(ptr, v1);
            _mm256_storeu_ps(ptr + 8, v2);
            _mm256_storeu_ps(ptr + 16, v3);
        }
        
        // Scalar remainder
        applyGainRGB_Scalar(data + i * 3, numPixels - i, r, g, b);
    }

    void applySTF_Row(const float* src, uint8_t* dst, size_t numPixels, const STFParams& params, bool inverted) {
        // Constants setup for 8-pixel blocks
        // Shadows/C0
        __m256 c0_1 = _mm256_setr_ps(params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1]);
        __m256 c0_2 = _mm256_setr_ps(params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0]);
        __m256 c0_3 = _mm256_setr_ps(params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2]);

        // Norm (InvRange)
        __m256 n_1 = _mm256_setr_ps(params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1]);
        __m256 n_2 = _mm256_setr_ps(params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0]);
        __m256 n_3 = _mm256_setr_ps(params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2]);

        // Midtones (m)
        __m256 m_1 = _mm256_setr_ps(params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1]);
        __m256 m_2 = _mm256_setr_ps(params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0]);
        __m256 m_3 = _mm256_setr_ps(params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2]);

        // MTF Factor (2m - 1)
        auto getFactor = [](float m) { return 2.0f * m - 1.0f; };
        __m256 f_1 = _mm256_setr_ps(getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]));
        __m256 f_2 = _mm256_setr_ps(getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]));
        __m256 f_3 = _mm256_setr_ps(getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]));

        // MTF Numerator term (m - 1)
        auto getNum = [](float m) { return m - 1.0f; };
        __m256 num_1 = _mm256_setr_ps(getNum(params.midtones[0]), getNum(params.midtones[1]), getNum(params.midtones[2]), getNum(params.midtones[0]), getNum(params.midtones[1]), getNum(params.midtones[2]), getNum(params.midtones[0]), getNum(params.midtones[1]));
        __m256 num_2 = _mm256_setr_ps(getNum(params.midtones[2]), getNum(params.midtones[0]), getNum(params.midtones[1]), getNum(params.midtones[2]), getNum(params.midtones[0]), getNum(params.midtones[1]), getNum(params.midtones[2]), getNum(params.midtones[0]));
        __m256 num_3 = _mm256_setr_ps(getNum(params.midtones[1]), getNum(params.midtones[2]), getNum(params.midtones[0]), getNum(params.midtones[1]), getNum(params.midtones[2]), getNum(params.midtones[0]), getNum(params.midtones[1]), getNum(params.midtones[2]));

        __m256 vZero = _mm256_setzero_ps();
        __m256 vOne = _mm256_set1_ps(1.0f);
        __m256 v255 = _mm256_set1_ps(255.0f);
        __m256 vHalf = _mm256_set1_ps(0.5f);

        size_t i = 0;
        for (; i + 8 <= numPixels; i += 8) {
            const float* ptr = src + i * 3;
            uint8_t* dPtr = dst + i * 3;

            // Load 3 regs
            __m256 v1 = _mm256_loadu_ps(ptr);
            __m256 v2 = _mm256_loadu_ps(ptr + 8);
            __m256 v3 = _mm256_loadu_ps(ptr + 16);

            // Sanitize NaN/Inf: replace with 0 before any processing
            // _CMP_ORD_Q: true if both operands are ordered (i.e., neither is NaN)
            __m256 ord1 = _mm256_cmp_ps(v1, v1, _CMP_ORD_Q);
            __m256 ord2 = _mm256_cmp_ps(v2, v2, _CMP_ORD_Q);
            __m256 ord3 = _mm256_cmp_ps(v3, v3, _CMP_ORD_Q);
            v1 = _mm256_and_ps(v1, ord1);  // NaN lanes become 0
            v2 = _mm256_and_ps(v2, ord2);
            v3 = _mm256_and_ps(v3, ord3);

            // Clamp to [0, 1] (handles Inf and out-of-range)
            v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v1));
            v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v2));
            v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v3));

            // 1. Normalize x = (v - c0) * norm
            v1 = _mm256_mul_ps(_mm256_sub_ps(v1, c0_1), n_1);
            v2 = _mm256_mul_ps(_mm256_sub_ps(v2, c0_2), n_2);
            v3 = _mm256_mul_ps(_mm256_sub_ps(v3, c0_3), n_3);

            // Clamp 0-1
            v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v1));
            v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v2));
            v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v3));

            // 2. MTF: y = (m-1)x / ((2m-1)x - m)
            // Denom = (Factor * x) - m
            __m256 d1 = _mm256_sub_ps(_mm256_mul_ps(f_1, v1), m_1);
            __m256 d2 = _mm256_sub_ps(_mm256_mul_ps(f_2, v2), m_2);
            __m256 d3 = _mm256_sub_ps(_mm256_mul_ps(f_3, v3), m_3);

            // Numer = x * (m-1)
            __m256 n1 = _mm256_mul_ps(v1, num_1);
            __m256 n2 = _mm256_mul_ps(v2, num_2);
            __m256 n3 = _mm256_mul_ps(v3, num_3);

            // Div
            v1 = _mm256_div_ps(n1, d1);
            v2 = _mm256_div_ps(n2, d2);
            v3 = _mm256_div_ps(n3, d3);

            // Clamp again for safety/inversion
            if (inverted) {
                v1 = _mm256_sub_ps(vOne, v1);
                v2 = _mm256_sub_ps(vOne, v2);
                v3 = _mm256_sub_ps(vOne, v3);
            }
            v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v1));
            v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v2));
            v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v3));

            // Convert to 255
            v1 = _mm256_add_ps(_mm256_mul_ps(v1, v255), vHalf);
            v2 = _mm256_add_ps(_mm256_mul_ps(v2, v255), vHalf);
            v3 = _mm256_add_ps(_mm256_mul_ps(v3, v255), vHalf);

            // Convert to Int32
            __m256i i1 = _mm256_cvtps_epi32(v1);
            __m256i i2 = _mm256_cvtps_epi32(v2);
            __m256i i3 = _mm256_cvtps_epi32(v3);

            alignas(32) int32_t tempI[24];
            _mm256_store_si256((__m256i*)tempI, i1);
            _mm256_store_si256((__m256i*)(tempI + 8), i2);
            _mm256_store_si256((__m256i*)(tempI + 16), i3);

            for(int k=0; k<24; ++k) {
                // Clamp to [0,255]: cvtps_epi32 rounds 255.5 → 256 which wraps to 0 as uint8_t,
                // causing color artifacts (yellow/cyan/magenta) in highlight areas.
                dPtr[k] = static_cast<uint8_t>(std::min(std::max(tempI[k], 0), 255));
            }
        }

        // Remainder
        applySTF_Row_Scalar(src + i * 3, dst + i * 3, numPixels - i, params, inverted);
    }
}
#else
// Non-AVX2 Fallback
namespace SimdOps {
    void applyGainRGB(float* data, size_t numPixels, float r, float g, float b) {
        applyGainRGB_Scalar(data, numPixels, r, g, b);
    }
    void applySTF_Row(const float* src, uint8_t* dst, size_t numPixels, const STFParams& params, bool inverted) {
        applySTF_Row_Scalar(src, dst, numPixels, params, inverted);
    }
}
#endif
