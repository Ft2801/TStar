#include "ImageBlendingRunner.h"
#include <algorithm>
#include <cmath>

ImageBlendingRunner::ImageBlendingRunner(QObject* parent) : QObject(parent) {}

bool ImageBlendingRunner::run(const ImageBuffer& base, const ImageBuffer& top, ImageBuffer& result, const ImageBlendingParams& params, QString* errorMsg) {
    if (base.width() != top.width() || base.height() != top.height()) {
        if (errorMsg) *errorMsg = "Dimensions mismatch between base and top images.";
        return false;
    }

    int w = base.width();
    int h = base.height();
    int baseC = base.channels();
    int topC = top.channels();
    
    // Result channels: if one is color, result is color.
    int outC = std::max(baseC, topC);
    result.resize(w, h, outC);

    const float* bData = base.data().data();
    const float* tData = top.data().data();
    float* outData = result.data().data();

    float low = params.lowRange;
    float high = params.highRange;
    float feather = params.feather;
    float opacity = params.opacity;

    size_t numPixels = (size_t)w * h;

    #pragma omp parallel for
    for (int i = 0; i < (int)numPixels; ++i) {
        // 1. Calculate top intensity for masking
        float topIntensity = 0.0f;
        if (topC == 3) {
            topIntensity = (tData[i * 3] + tData[i * 3 + 1] + tData[i * 3 + 2]) / 3.0f;
        } else {
            topIntensity = tData[i];
        }

        // 2. Calculate Mask based on Range & Feathering
        float mask = 1.0f;
        if (feather > 1e-6) {
            float lowM = std::clamp((topIntensity - (low - feather)) / feather, 0.0f, 1.0f);
            float highM = std::clamp(((high + feather) - topIntensity) / feather, 0.0f, 1.0f);
            mask = lowM * highM;
        } else {
            if (topIntensity < low || topIntensity > high) mask = 0.0f;
        }

        float totalAlpha = mask * opacity;

        // 3. Blend per channel
        for (int ch = 0; ch < outC; ++ch) {
            float bVal = (baseC == 3) ? bData[i * 3 + ch] : bData[i];
            float tVal = (topC == 3) ? tData[i * 3 + ch] : tData[i];

            // If we are blending Mono into Color, we might target specific channels
            bool applyInChannel = true;
            if (baseC == 3 && topC == 1) {
                if (params.targetChannel != 3 && params.targetChannel != ch) {
                    applyInChannel = false;
                }
            }

            if (applyInChannel) {
                float blended = blendPixel(bVal, tVal, params.mode);
                float finalVal = bVal * (1.0f - totalAlpha) + blended * totalAlpha;
                outData[i * outC + ch] = std::clamp(finalVal, 0.0f, 1.0f);
            } else {
                outData[i * outC + ch] = bVal;
            }
        }
    }

    result.setMetadata(base.metadata());
    return true;
}

float ImageBlendingRunner::blendPixel(float b, float t, ImageBlendingParams::BlendMode mode) {
    switch (mode) {
        case ImageBlendingParams::Normal:     return t;
        case ImageBlendingParams::Multiply:   return b * t;
        case ImageBlendingParams::Screen:     return 1.0f - (1.0f - b) * (1.0f - t);
        case ImageBlendingParams::Overlay:    return (b < 0.5f) ? (2.0f * b * t) : (1.0f - 2.0f * (1.0f - b) * (1.0f - t));
        case ImageBlendingParams::Add:         return std::min(1.0f, b + t);
        case ImageBlendingParams::Subtract:    return std::max(0.0f, b - t);
        case ImageBlendingParams::Difference:  return std::abs(b - t);
        case ImageBlendingParams::SoftLight:   return (1.0f - 2.0f * t) * b * b + 2.0f * t * b;
        case ImageBlendingParams::HardLight:   return (t < 0.5f) ? (2.0f * b * t) : (1.0f - 2.0f * (1.0f - b) * (1.0f - t));
        default: return t;
    }
}
