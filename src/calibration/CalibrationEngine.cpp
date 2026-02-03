/**
 * @file CalibrationEngine.cpp
 * @brief Core calibration algorithms implementation
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "CalibrationEngine.h"
#include "CalibrationC.h"
#include "../stacking/Statistics.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <omp.h>
#include <mutex>

namespace Calibration {

float CalibrationEngine::findOptimalDarkScale(const ImageBuffer& light, 
                                             const ImageBuffer& masterDark,
                                             const Preprocessing::DarkOptimParams& params) {
    
    int w = light.width();
    int h = light.height();
    int channels = light.channels();
    const float* lightData = light.data().data();
    const float* darkData = masterDark.data().data();

    // Use default ROI if not optimizing for specific area?
    // C function calculates center ROI if passed 0.
    // But we might want use params logic.
    int sz = 512;
    int roi_x = (w - sz) / 2;
    int roi_y = (h - sz) / 2;
    
    return find_optimal_dark_scale_c(lightData, darkData, w, h, channels,
                                     params.K_min, params.K_max,
                                     params.tolerance, params.maxIterations,
                                     roi_x, roi_y, sz, sz);
}

double CalibrationEngine::computeFlatNormalization(const ImageBuffer& masterFlat) {
    return compute_flat_normalization_c(masterFlat.data().data(),
                                        masterFlat.width(),
                                        masterFlat.height(),
                                        masterFlat.channels());
}

bool CalibrationEngine::applyFlat(ImageBuffer& light, const ImageBuffer& masterFlat, double normalization) {
    if (normalization < 1e-6) return false;
    
    int size = light.width() * light.height() * light.channels();
    float* img = light.data().data();
    const float* flat = masterFlat.data().data();
    float norm = static_cast<float>(normalization);

    apply_flat_c(img, flat, (size_t)size, norm, omp_get_max_threads());
    return true;
}

void CalibrationEngine::fixBanding(ImageBuffer& image) {
    int w = image.width();
    int h = image.height();
    int channels = image.channels();
    float* data = image.data().data();

    QString bayer = image.getHeaderValue("BAYERPAT");
    int cfaPattern = -1; 
    if (channels == 1) {
        if (bayer.contains("Trans", Qt::CaseInsensitive)) cfaPattern = 4; // XTrans
        else cfaPattern = 0; // Standard Bayer
    }

    fix_banding_c(data, w, h, channels, cfaPattern, omp_get_max_threads());
}

void CalibrationEngine::fixBadLines(ImageBuffer& image) {
    Q_UNUSED(image);
    // Placeholder
}

void CalibrationEngine::fixXTransArtifacts(ImageBuffer& image) {
    QString model = image.getHeaderValue("INSTRUME");
    QString pattern = image.getHeaderValue("BAYERPAT");
    
    fix_xtrans_c(image.data().data(), image.width(), image.height(),
                 pattern.toLatin1().constData(),
                 model.toLatin1().constData(),
                 omp_get_max_threads());
}

void CalibrationEngine::equalizeCFAChannels(ImageBuffer& flat, Preprocessing::BayerPattern pattern) {
    if (flat.channels() != 1) return; // CFA is mono-like

    int w = flat.width();
    int h = flat.height();
    float* data = flat.data().data();

    // Map BayerPattern to C int
    // 0: RGGB, 1: BGGR, 2: GRBG, 3: GBRG, 4: XTRANS
    int cPattern = -1;
    switch (pattern) {
        case Preprocessing::BayerPattern::RGGB:   cPattern = 0; break;
        case Preprocessing::BayerPattern::BGGR:   cPattern = 1; break;
        case Preprocessing::BayerPattern::GRBG:   cPattern = 2; break;
        case Preprocessing::BayerPattern::GBRG:   cPattern = 3; break;
        case Preprocessing::BayerPattern::XTrans: cPattern = 4; break;
        default: break;
    }
    
    if (cPattern >= 0) {
        equalize_cfa_c(data, w, h, cPattern, omp_get_max_threads());
    }
}

std::vector<QPoint> CalibrationEngine::findDeviantPixels(const ImageBuffer& dark, 
                                                         float hotSigma, 
                                                         float coldSigma) {
    int w = dark.width();
    int h = dark.height();
    int channels = dark.channels();
    const float* data = dark.data().data();
    
    std::vector<QPoint> deviantPixels;
    std::mutex mtx;

    // Process each channel
    // Max deviant pixels to find per channel (safety limit)
    const int MAX_DEVIANT = 500000;
    
    for (int c = 0; c < channels; ++c) {
        // If single channel, we can use data directly.
        // If multi-channel, ImageBuffer is Interleaved (RGBRGB...), but C function expects Contiguous (Mono).
        // We must extract the channel.
        std::vector<float> channelBuffer;
        const float* layerPtr = nullptr;
        
        if (channels == 1) {
            layerPtr = data;
        } else {
            channelBuffer.resize(w * h);
            #pragma omp parallel for
            for(int i = 0; i < w * h; ++i) {
                channelBuffer[i] = data[i * channels + c];
            }
            layerPtr = channelBuffer.data();
        }
        
        int* outX = (int*)malloc(MAX_DEVIANT * sizeof(int));
        int* outY = (int*)malloc(MAX_DEVIANT * sizeof(int));
        
        if (!outX || !outY) {
            free(outX); free(outY);
            continue;
        }

        // If single channel, enable CFA-like split (treat as Bayer) to handle potential CFA patterns 
        // or just to have better local stats (2x2 grid).
        // If 3 channels, treat each as independent mono plane (-1).
        int cMode = (channels == 1) ? 0 : -1;

        int count = find_deviant_pixels_c(layerPtr, w, h, hotSigma, coldSigma, outX, outY, MAX_DEVIANT, cMode);
        
        if (count > 0) {
            std::lock_guard<std::mutex> lock(mtx);
            // Append to main list
            for (int i = 0; i < count; ++i) {
                deviantPixels.push_back(QPoint(outX[i], outY[i]));
            }
        }
        
        free(outX);
        free(outY);
    }

    // Sort and remove duplicates
    std::sort(deviantPixels.begin(), deviantPixels.end(), [](const QPoint& a, const QPoint& b) {
        return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x());
    });
    deviantPixels.erase(std::unique(deviantPixels.begin(), deviantPixels.end()), deviantPixels.end());

    return deviantPixels;
}

} // namespace Calibration
