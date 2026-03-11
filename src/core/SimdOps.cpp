#include "SimdOps.h"
#include <algorithm>
#include <cmath>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

// --- Internal Utilities ---
namespace {
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

    static void applyGainRGB_Scalar(float* data, size_t numPixels, float r, float g, float b) {
        for (size_t i = 0; i < numPixels; ++i) {
            data[i * 3 + 0] *= r;
            data[i * 3 + 1] *= g;
            data[i * 3 + 2] *= b;
        }
    }

    static void applySTF_Row_Scalar(const float* src, uint8_t* dst, size_t numPixels, const SimdOps::STFParams& params, bool inverted) {
        for (size_t i = 0; i < numPixels; ++i) {
            float vals[3];
            for (int c = 0; c < 3; ++c) {
                float v = safeClamp01(src[i * 3 + c]);
                v = (v - params.shadow[c]) * params.invRange[c];
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

// --- Runtime Dispatch Logic ---

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    #define ATTRIBUTE_TARGET_AVX2 __attribute__((target("avx2")))
    static inline bool supports_avx2() {
        static bool supported = __builtin_cpu_supports("avx2");
        return supported;
    }
#elif (defined(__x86_64__) || defined(_M_X64)) && defined(_MSC_VER)
    // MSVC handles intrinsics differently; for now, we follow the same pattern
    #define ATTRIBUTE_TARGET_AVX2 
    static inline bool supports_avx2() { return false; } // MSVC would need __cpuid
#else
    #define ATTRIBUTE_TARGET_AVX2
    static inline bool supports_avx2() { return false; }
#endif

namespace SimdOps {

    #if defined(__x86_64__) || defined(_M_X64)
    ATTRIBUTE_TARGET_AVX2
    static void applyGainRGB_AVX2(float* data, size_t numPixels, float r, float g, float b) {
        __m256 k1 = _mm256_setr_ps(r, g, b, r, g, b, r, g);
        __m256 k2 = _mm256_setr_ps(b, r, g, b, r, g, b, r);
        __m256 k3 = _mm256_setr_ps(g, b, r, g, b, r, g, b);

        size_t i = 0;
        for (; i + 8 <= numPixels; i += 8) {
            float* ptr = data + i * 3;
            __m256 v1 = _mm256_loadu_ps(ptr);
            __m256 v2 = _mm256_loadu_ps(ptr + 8);
            __m256 v3 = _mm256_loadu_ps(ptr + 16);
            _mm256_storeu_ps(ptr, _mm256_mul_ps(v1, k1));
            _mm256_storeu_ps(ptr + 8, _mm256_mul_ps(v2, k2));
            _mm256_storeu_ps(ptr + 16, _mm256_mul_ps(v3, k3));
        }
        applyGainRGB_Scalar(data + i * 3, numPixels - i, r, g, b);
    }

    ATTRIBUTE_TARGET_AVX2
    static void applySTF_Row_AVX2(const float* src, uint8_t* dst, size_t numPixels, const STFParams& params, bool inverted) {
        __m256 c0_1 = _mm256_setr_ps(params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1]);
        __m256 c0_2 = _mm256_setr_ps(params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0]);
        __m256 c0_3 = _mm256_setr_ps(params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2], params.shadow[0], params.shadow[1], params.shadow[2]);
        __m256 n_1 = _mm256_setr_ps(params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1]);
        __m256 n_2 = _mm256_setr_ps(params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0]);
        __m256 n_3 = _mm256_setr_ps(params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2], params.invRange[0], params.invRange[1], params.invRange[2]);
        __m256 m_1 = _mm256_setr_ps(params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1]);
        __m256 m_2 = _mm256_setr_ps(params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0]);
        __m256 m_3 = _mm256_setr_ps(params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2], params.midtones[0], params.midtones[1], params.midtones[2]);

        auto getFactor = [](float m) { return 2.0f * m - 1.0f; };
        __m256 f_1 = _mm256_setr_ps(getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]));
        __m256 f_2 = _mm256_setr_ps(getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]));
        __m256 f_3 = _mm256_setr_ps(getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]), getFactor(params.midtones[0]), getFactor(params.midtones[1]), getFactor(params.midtones[2]));

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
            __m256 v1 = _mm256_loadu_ps(ptr);
            __m256 v2 = _mm256_loadu_ps(ptr + 8);
            __m256 v3 = _mm256_loadu_ps(ptr + 16);

            __m256 ord1 = _mm256_cmp_ps(v1, v1, _CMP_ORD_Q);
            __m256 ord2 = _mm256_cmp_ps(v2, v2, _CMP_ORD_Q);
            __m256 ord3 = _mm256_cmp_ps(v3, v3, _CMP_ORD_Q);
            v1 = _mm256_and_ps(v1, ord1);
            v2 = _mm256_and_ps(v2, ord2);
            v3 = _mm256_and_ps(v3, ord3);

            v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v1));
            v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v2));
            v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v3));

            v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, _mm256_mul_ps(_mm256_sub_ps(v1, c0_1), n_1)));
            v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, _mm256_mul_ps(_mm256_sub_ps(v2, c0_2), n_2)));
            v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, _mm256_mul_ps(_mm256_sub_ps(v3, c0_3), n_3)));

            v1 = _mm256_div_ps(_mm256_mul_ps(v1, num_1), _mm256_sub_ps(_mm256_mul_ps(f_1, v1), m_1));
            v2 = _mm256_div_ps(_mm256_mul_ps(v2, num_2), _mm256_sub_ps(_mm256_mul_ps(f_2, v2), m_2));
            v3 = _mm256_div_ps(_mm256_mul_ps(v3, num_3), _mm256_sub_ps(_mm256_mul_ps(f_3, v3), m_3));

            if (inverted) {
                v1 = _mm256_sub_ps(vOne, v1);
                v2 = _mm256_sub_ps(vOne, v2);
                v3 = _mm256_sub_ps(vOne, v3);
            }
            v1 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v1));
            v2 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v2));
            v3 = _mm256_max_ps(vZero, _mm256_min_ps(vOne, v3));

            __m256i i1 = _mm256_cvtps_epi32(_mm256_add_ps(_mm256_mul_ps(v1, v255), vHalf));
            __m256i i2 = _mm256_cvtps_epi32(_mm256_add_ps(_mm256_mul_ps(v2, v255), vHalf));
            __m256i i3 = _mm256_cvtps_epi32(_mm256_add_ps(_mm256_mul_ps(v3, v255), vHalf));

            alignas(32) int32_t tempI[24];
            _mm256_store_si256((__m256i*)tempI, i1);
            _mm256_store_si256((__m256i*)(tempI + 8), i2);
            _mm256_store_si256((__m256i*)(tempI + 16), i3);

            for(int k=0; k<24; ++k) {
                dPtr[k] = static_cast<uint8_t>(std::min(std::max(tempI[k], 0), 255));
            }
        }
        applySTF_Row_Scalar(src + i * 3, dst + i * 3, numPixels - i, params, inverted);
    }
    #endif

    // --- Public Dispatchers ---

    void applyGainRGB(float* data, size_t numPixels, float r, float g, float b) {
        #if defined(__x86_64__) || defined(_M_X64)
        if (supports_avx2()) {
            applyGainRGB_AVX2(data, numPixels, r, g, b);
            return;
        }
        #endif
        applyGainRGB_Scalar(data, numPixels, r, g, b);
    }

    void applySTF_Row(const float* src, uint8_t* dst, size_t numPixels, const STFParams& params, bool inverted) {
        #if defined(__x86_64__) || defined(_M_X64)
        if (supports_avx2()) {
            applySTF_Row_AVX2(src, dst, numPixels, params, inverted);
            return;
        }
        #endif
        applySTF_Row_Scalar(src, dst, numPixels, params, inverted);
    }

} // namespace SimdOps
