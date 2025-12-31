#include "StarStretchRunner.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>

StarStretchRunner::StarStretchRunner(QObject* parent) : QObject(parent) {}

static void applyPixelMath(std::vector<float>& data, int channels, float amount) {
    if (std::abs(amount) < 1e-5f) return;

    double factor = std::pow(3.0, amount);
    double denom_factor = factor - 1.0;

    // val = (factor * p) / (denom_factor * p + 1)
    
    size_t size = data.size();
    for (size_t i = 0; i < size; ++i) {
        float p = data[i];
        if (p < 0.0f) p = 0.0f;
        else if (p > 1.0f) p = 1.0f;
        
        double val = (factor * (double)p) / (denom_factor * (double)p + 1.0);
        data[i] = (float)std::max(0.0, std::min(1.0, val));
    }
}

static void applySaturation(std::vector<float>& data, int w, int h, int c, float amount) {
    if (c < 3) return; // Need RGB
    if (std::abs(amount - 1.0f) < 1e-4f) return;

    // C' = mean + (C - mean) * amount
    size_t numPixels = (size_t)w * h;
    for (size_t i = 0; i < numPixels; ++i) {
        float r = data[i * c + 0];
        float g = data[i * c + 1];
        float b = data[i * c + 2];
        
    float mean = (r + g + b) / 3.0f; // Simple mean (matches reference implementation)
        
        float r_new = mean + (r - mean) * amount;
        float g_new = mean + (g - mean) * amount;
        float b_new = mean + (b - mean) * amount;
        
        data[i * c + 0] = std::max(0.0f, std::min(1.0f, r_new));
        data[i * c + 1] = std::max(0.0f, std::min(1.0f, g_new));
        data[i * c + 2] = std::max(0.0f, std::min(1.0f, b_new));
    }
}

static void applySCNR(std::vector<float>& data, int w, int h, int c) {
    if (c < 3) return;
    size_t numPixels = (size_t)w * h;
    for (size_t i = 0; i < numPixels; ++i) {
        float r = data[i * c + 0];
        float g = data[i * c + 1];
        float b = data[i * c + 2];
        
        // g2 = min(g, 0.5 * (r + b))
        float limit = 0.5f * (r + b);
        if (g > limit) {
             data[i * c + 1] = limit;
        }
    }
}

bool StarStretchRunner::run(const ImageBuffer& input, ImageBuffer& output, const StarStretchParams& params, QString* errorMsg) {
    // Copy input to output
    output = input;
    
    // Ensure float32 range? ImageBuffer is usually 0-1 float.
    
    int w = output.width();
    int h = output.height();
    int c = output.channels();
    auto& data = output.data();
    
    // 1. Star Stretch (Pixel Math)
    if (params.stretchAmount > 0.0f) {
        emit processOutput("Applying Stretch...");
        applyPixelMath(data, c, params.stretchAmount);
    }
    
    // 2. Saturation (RGB only)
    if (c == 3 && std::abs(params.colorBoost - 1.0f) > 1e-4f) {
        emit processOutput("Applying Color Boost...");
        applySaturation(data, w, h, c, params.colorBoost);
    }
    
    // 3. SCNR (RGB only)
    if (c == 3 && params.scnr) {
        emit processOutput("Applying SCNR (Green Removal)...");
        applySCNR(data, w, h, c);
    }
    
    // Apply Mask if present using the input as the original source
    if (output.hasMask()) {
        output.blendResult(input);
    }
    
    return true;
}
