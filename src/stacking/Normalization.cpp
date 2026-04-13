/**
 * @file Normalization.cpp
 * @brief Implementation of normalization coefficient computation and application.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "Normalization.h"
#include "Statistics.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <iostream>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "../preprocessing/Debayer.h"
#include "../preprocessing/PreprocessingTypes.h"

#ifndef FLOAT_IMG
#define FLOAT_IMG -32
#endif

namespace Stacking {

// ============================================================================
// Public: Coefficient computation entry point
// ============================================================================

bool Normalization::computeCoefficients(
    const ImageSequence& sequence,
    const StackingParams& params,
    NormCoefficients& coefficients,
    ProgressCallback progressCallback)
{
    if (!sequence.isValid()) return false;

    const int nbImages = sequence.count();
    const int nbLayers = sequence.channels();

    coefficients.init(nbImages, nbLayers);

    if (params.normalization == NormalizationMethod::None)
        return true;

    if (params.overlapNormalization)
        return computeOverlapNormalization(sequence, params,
                                           coefficients, progressCallback);

    return computeFullImageNormalization(sequence, params,
                                         coefficients, progressCallback);
}

// ============================================================================
// Full-image normalization
//
//   Stage 1: For each image and channel, compute a robust location
//            estimator (median or IKSS centre) and a robust scale
//            estimator (1.5*MAD or biweight midvariance).
//
//   Stage 2: Derive per-frame factors relative to the reference image:
//
//     AdditiveScaling:        pscale = refScale / imgScale
//                             poffset = pscale * imgLoc - refLoc
//
//     Additive:               poffset = imgLoc - refLoc   (pscale = 1)
//
//     MultiplicativeScaling:  pscale = refScale / imgScale
//                             pmul   = refLoc / imgLoc
//
//     Multiplicative:         pmul   = refLoc / imgLoc    (pscale = 1)
//
//   Per-pixel application:
//     Additive modes:         pixel * pscale - poffset
//     Multiplicative modes:   pixel * pscale * pmul
// ============================================================================

bool Normalization::computeFullImageNormalization(
    const ImageSequence& sequence,
    const StackingParams& params,
    NormCoefficients& coefficients,
    ProgressCallback progressCallback)
{
    const int nbImages = sequence.count();
    int refImage = params.refImageIndex;
    if (refImage < 0 || refImage >= nbImages)
        refImage = sequence.referenceImage();

    const int  nbLayers = sequence.channels();
    const bool liteNorm = params.fastNormalization;
    const int  regLayer = (params.registrationLayer >= 0 &&
                           params.registrationLayer < nbLayers)
                          ? params.registrationLayer : 1;

    // --- Stage 1: Compute location/scale estimators for every image ---

    struct ImageEstimators {
        double location[3] = {0.0, 0.0, 0.0};
        double scale[3]    = {1.0, 1.0, 1.0};
        bool   valid       = false;
    };

    std::vector<ImageEstimators> estimators(nbImages);

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nbImages; ++i) {
        ImageBuffer buffer;
        if (!sequence.readImage(i, buffer)) continue;

        const int    imgLayers = buffer.channels();
        const size_t total     = static_cast<size_t>(buffer.width()) * buffer.height();
        if (total == 0) continue;

        for (int c = 0; c < imgLayers && c < 3; ++c) {

            // Collect non-zero pixel values for this channel
            std::vector<float> vec;
            vec.reserve(total);
            const float* ptr = buffer.data().data();
            for (size_t p = 0; p < total; ++p) {
                float v = ptr[p * imgLayers + c];
                if (v != 0.0f) vec.push_back(v);
            }
            if (vec.empty()) continue;

            float median = Statistics::quickMedian(vec);
            float mad    = Statistics::mad(vec, median);
            estimators[i].location[c] = median;

            if (liteNorm) {
                // Fast mode: location = median, scale = 1.5 * MAD
                estimators[i].scale[c] = 1.5 * mad;
            } else {
                // Full mode: refine via IKSS-lite (biweight midvariance)
                double ikssLocation = median;
                double ikssScale    = mad;
                Statistics::ikssLite(vec.data(), vec.size(), median, mad,
                                    ikssLocation, ikssScale);
                estimators[i].location[c] = ikssLocation;
                estimators[i].scale[c]    = (ikssScale > 0.0) ? ikssScale : mad;
            }
        }
        estimators[i].valid = true;

        if (progressCallback) {
            progressCallback("Computing estimators",
                             static_cast<double>(i + 1) / nbImages * 0.5);
        }
    }

    if (!estimators[refImage].valid) return false;

    // --- Stage 2: Compute factors from estimators ---

    double refLocation[3], refScale[3];
    for (int c = 0; c < 3; ++c) {
        refLocation[c] = estimators[refImage].location[c];
        refScale[c]    = estimators[refImage].scale[c];
    }

    for (int layer = 0; layer < nbLayers && layer < 3; ++layer) {
        int reflayer = params.equalizeRGB ? regLayer : layer;
        if (reflayer >= nbLayers) reflayer = layer;

        for (int i = 0; i < nbImages; ++i) {
            if (!estimators[i].valid) {
                coefficients.poffset[layer][i] = 0.0;
                coefficients.pmul[layer][i]    = 1.0;
                coefficients.pscale[layer][i]  = 1.0;
                continue;
            }

            double imgLocation = estimators[i].location[layer];
            double imgScale    = estimators[i].scale[layer];

            switch (params.normalization) {
            default:
            case NormalizationMethod::AdditiveScaling:
                coefficients.pscale[layer][i]  = (imgScale == 0.0)
                    ? 1.0 : refScale[reflayer] / imgScale;
                coefficients.poffset[layer][i] =
                    coefficients.pscale[layer][i] * imgLocation - refLocation[reflayer];
                coefficients.pmul[layer][i]    = 1.0;
                break;

            case NormalizationMethod::Additive:
                coefficients.pscale[layer][i]  = 1.0;
                coefficients.poffset[layer][i] = imgLocation - refLocation[reflayer];
                coefficients.pmul[layer][i]    = 1.0;
                break;

            case NormalizationMethod::MultiplicativeScaling:
                coefficients.pscale[layer][i]  = (imgScale == 0.0)
                    ? 1.0 : refScale[reflayer] / imgScale;
                coefficients.pmul[layer][i]    = (imgLocation == 0.0)
                    ? 1.0 : refLocation[reflayer] / imgLocation;
                coefficients.poffset[layer][i] = 0.0;
                break;

            case NormalizationMethod::Multiplicative:
                coefficients.pscale[layer][i]  = 1.0;
                coefficients.pmul[layer][i]    = (imgLocation == 0.0)
                    ? 1.0 : refLocation[reflayer] / imgLocation;
                coefficients.poffset[layer][i] = 0.0;
                break;
            }

            // Mirror green (or mono) channel into the global coefficients
            if (layer == 1 || (layer == 0 && nbLayers == 1)) {
                coefficients.scale[i]  = coefficients.pscale[layer][i];
                coefficients.offset[i] = coefficients.poffset[layer][i];
                coefficients.mul[i]    = coefficients.pmul[layer][i];
            }
        }
    }

    if (progressCallback)
        progressCallback("Normalization complete", 1.0);

    return true;
}

// ============================================================================
// Overlap-region normalization
// ============================================================================

bool Normalization::computeOverlapNormalization(
    const ImageSequence& sequence,
    const StackingParams& params,
    NormCoefficients& coefficients,
    ProgressCallback progressCallback)
{
    (void)progressCallback;

    const int nbImages = sequence.count();
    int refImage = params.refImageIndex;
    if (refImage < 0 || refImage >= nbImages)
        refImage = sequence.referenceImage();

    ImageBuffer refBuffer;
    if (!sequence.readImage(refImage, refBuffer)) return false;

    const int w        = refBuffer.width();
    const int h        = refBuffer.height();
    const int nbLayers = refBuffer.channels();
    const int regLayer = (params.registrationLayer >= 0 &&
                          params.registrationLayer < nbLayers)
                         ? params.registrationLayer : 1;

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nbImages; ++i) {

        // Reference image gets identity coefficients
        if (i == refImage) {
            for (int c = 0; c < nbLayers && c < 3; ++c) {
                coefficients.poffset[c][i] = 0.0;
                coefficients.pmul[c][i]    = 1.0;
                coefficients.pscale[c][i]  = 1.0;
            }
            continue;
        }

        // Compute the overlap ROI from the registration shifts
        const auto& imgInfo = sequence.image(i);
        const auto& refInfo = sequence.image(refImage);

        double dx = imgInfo.registration.shiftX - refInfo.registration.shiftX;
        double dy = imgInfo.registration.shiftY - refInfo.registration.shiftY;

        int r_x = std::max(0, static_cast<int>(dx));
        int r_y = std::max(0, static_cast<int>(dy));
        int r_w = std::min(w, static_cast<int>(w + dx)) - r_x;
        int r_h = std::min(h, static_cast<int>(h + dy)) - r_y;

        if (r_w < 50 || r_h < 50) {
            // Overlap too small -- use identity coefficients
            for (int c = 0; c < nbLayers && c < 3; ++c) {
                coefficients.poffset[c][i] = 0.0;
                coefficients.pmul[c][i]    = 1.0;
                coefficients.pscale[c][i]  = 1.0;
            }
            continue;
        }

        ImageBuffer tgtBuffer;
        if (!sequence.readImage(i, tgtBuffer)) continue;

        const size_t roi_n = static_cast<size_t>(r_w) * r_h;

        for (int c = 0; c < nbLayers && c < 3; ++c) {
            int reflayer = params.equalizeRGB ? regLayer : c;
            if (reflayer >= nbLayers) reflayer = c;

            // Collect paired pixel values from the overlap region
            std::vector<float> ref_roi, tgt_roi;
            ref_roi.reserve(roi_n);
            tgt_roi.reserve(roi_n);

            const float* rptr = refBuffer.data().data();
            const float* tptr = tgtBuffer.data().data();

            for (int y = 0; y < r_h; ++y) {
                for (int x = 0; x < r_w; ++x) {
                    int rx = r_x + x;
                    int ry = r_y + y;
                    int tx = rx - static_cast<int>(dx);
                    int ty = ry - static_cast<int>(dy);

                    bool refOk = (rx >= 0 && rx < w && ry >= 0 && ry < h);
                    bool tgtOk = (tx >= 0 && tx < w && ty >= 0 && ty < h);

                    float refVal = 0.0f, tgtVal = 0.0f;
                    if (refOk) refVal = rptr[(ry * w + rx) * nbLayers + reflayer];
                    if (tgtOk) tgtVal = tptr[(ty * w + tx) * nbLayers + c];

                    if (refOk && tgtOk && refVal != 0.0f && tgtVal != 0.0f) {
                        ref_roi.push_back(refVal);
                        tgt_roi.push_back(tgtVal);
                    }
                }
            }

            if (ref_roi.size() < 3 || tgt_roi.size() < 3) {
                coefficients.poffset[c][i] = 0.0;
                coefficients.pmul[c][i]    = 1.0;
                coefficients.pscale[c][i]  = 1.0;
                continue;
            }

            // Compute robust estimators on the overlap samples
            float refMedian = Statistics::quickMedian(ref_roi);
            float tgtMedian = Statistics::quickMedian(tgt_roi);
            float refMAD    = Statistics::mad(ref_roi, refMedian);
            float tgtMAD    = Statistics::mad(tgt_roi, tgtMedian);

            double refLoc = refMedian;
            double tgtLoc = tgtMedian;
            double refSca, tgtSca;

            if (params.fastNormalization) {
                refSca = 1.5 * refMAD;
                tgtSca = 1.5 * tgtMAD;
            } else {
                double ikssLoc = refMedian;
                Statistics::ikssLite(ref_roi.data(), ref_roi.size(),
                                    refMedian, refMAD, ikssLoc, refSca);
                refLoc = ikssLoc;
                if (refSca <= 0.0) refSca = refMAD;

                Statistics::ikssLite(tgt_roi.data(), tgt_roi.size(),
                                    tgtMedian, tgtMAD, tgtLoc, tgtSca);
                if (tgtSca <= 0.0) tgtSca = tgtMAD;
            }

            // Derive coefficients (same formulae as full-image path)
            switch (params.normalization) {
            default:
            case NormalizationMethod::AdditiveScaling:
                coefficients.pscale[c][i]  = (tgtSca == 0.0)
                    ? 1.0 : refSca / tgtSca;
                coefficients.poffset[c][i] =
                    coefficients.pscale[c][i] * tgtLoc - refLoc;
                coefficients.pmul[c][i]    = 1.0;
                break;

            case NormalizationMethod::Additive:
                coefficients.pscale[c][i]  = 1.0;
                coefficients.poffset[c][i] = tgtLoc - refLoc;
                coefficients.pmul[c][i]    = 1.0;
                break;

            case NormalizationMethod::MultiplicativeScaling:
                coefficients.pscale[c][i]  = (tgtSca == 0.0)
                    ? 1.0 : refSca / tgtSca;
                coefficients.pmul[c][i]    = (tgtLoc == 0.0)
                    ? 1.0 : refLoc / tgtLoc;
                coefficients.poffset[c][i] = 0.0;
                break;

            case NormalizationMethod::Multiplicative:
                coefficients.pscale[c][i]  = 1.0;
                coefficients.pmul[c][i]    = (tgtLoc == 0.0)
                    ? 1.0 : refLoc / tgtLoc;
                coefficients.poffset[c][i] = 0.0;
                break;
            }

            // Mirror into global (mono) coefficients
            if (c == 1 || (c == 0 && nbLayers == 1)) {
                coefficients.scale[i]  = coefficients.pscale[c][i];
                coefficients.offset[i] = coefficients.poffset[c][i];
                coefficients.mul[i]    = coefficients.pmul[c][i];
            }
        }
    }

    return true;
}

// ============================================================================
// Per-pixel application
// ============================================================================

float Normalization::applyToPixel(float pixel,
                                   NormalizationMethod method,
                                   int imageIndex, int layer,
                                   const NormCoefficients& coefficients)
{
    if (method == NormalizationMethod::None) return pixel;
    if (pixel == 0.0f) return 0.0f;
    if (imageIndex < 0 || imageIndex >= static_cast<int>(coefficients.scale.size()))
        return pixel;

    double pscale = 1.0, poffset = 0.0, pmul = 1.0;

    if (layer >= 0 && layer < 3 &&
        imageIndex < static_cast<int>(coefficients.pscale[layer].size()))
    {
        pscale  = coefficients.pscale[layer][imageIndex];
        poffset = coefficients.poffset[layer][imageIndex];
        pmul    = coefficients.pmul[layer][imageIndex];
    } else {
        pscale  = coefficients.scale[imageIndex];
        poffset = coefficients.offset[imageIndex];
        pmul    = coefficients.mul[imageIndex];
    }

    switch (method) {
    case NormalizationMethod::Additive:
    case NormalizationMethod::AdditiveScaling:
        return static_cast<float>(pixel * pscale - poffset);

    case NormalizationMethod::Multiplicative:
    case NormalizationMethod::MultiplicativeScaling:
        return static_cast<float>(pixel * pscale * pmul);

    default:
        return pixel;
    }
}

// ============================================================================
// Whole-image application
// ============================================================================

void Normalization::applyToImage(ImageBuffer& buffer,
                                  NormalizationMethod method,
                                  int imageIndex,
                                  const NormCoefficients& coefficients)
{
    if (method == NormalizationMethod::None) return;

    const int    w        = buffer.width();
    const int    h        = buffer.height();
    const int    channels = buffer.channels();
    float*       data     = buffer.data().data();
    const size_t count    = static_cast<size_t>(w) * h;

    for (int c = 0; c < channels; ++c) {
        int layerIdx = (channels == 1) ? 0 : c;

        double pscale = 1.0, poffset = 0.0, pmul = 1.0;

        if (layerIdx < 3 &&
            imageIndex < static_cast<int>(coefficients.pscale[layerIdx].size()))
        {
            pscale  = coefficients.pscale[layerIdx][imageIndex];
            poffset = coefficients.poffset[layerIdx][imageIndex];
            pmul    = coefficients.pmul[layerIdx][imageIndex];
        } else if (imageIndex < static_cast<int>(coefficients.scale.size())) {
            pscale  = coefficients.scale[imageIndex];
            poffset = coefficients.offset[imageIndex];
            pmul    = coefficients.mul[imageIndex];
        }

        if (std::abs(pscale) < 1e-9) pscale = 1.0;

        bool isAdditive = (method == NormalizationMethod::Additive ||
                           method == NormalizationMethod::AdditiveScaling);

        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            size_t idx = i * channels + c;
            float val = data[idx];
            if (val == 0.0f) continue;

            if (isAdditive)
                data[idx] = static_cast<float>(val * pscale - poffset);
            else
                data[idx] = static_cast<float>(val * pscale * pmul);
        }
    }
}

// ============================================================================
// Output normalization to [0, 1]
// ============================================================================

void Normalization::normalizeOutput(ImageBuffer& buffer)
{
    const int    w        = buffer.width();
    const int    h        = buffer.height();
    const int    channels = buffer.channels();
    float*       data     = buffer.data().data();
    const size_t count    = static_cast<size_t>(w) * h * channels;

    float minVal =  1e30f;
    float maxVal = -1e30f;

    // Find min/max, skipping zero (no-data) pixels
    #pragma omp parallel for reduction(min:minVal) reduction(max:maxVal)
    for (size_t i = 0; i < count; ++i) {
        float val = data[i];
        if (val == 0.0f) continue;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }

    if (maxVal <= minVal) return;

    float range    = maxVal - minVal;
    float invRange = 1.0f / range;

    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        if (data[i] == 0.0f) continue;
        data[i] = (data[i] - minVal) * invRange;
    }
}

// ============================================================================
// RGB channel equalisation
// ============================================================================

void Normalization::equalizeRGB(ImageBuffer& buffer, int referenceChannel)
{
    if (buffer.channels() != 3) return;

    const size_t count = buffer.width() * buffer.height();
    float*       data  = buffer.data().data();

    if (referenceChannel < 0 || referenceChannel > 2)
        referenceChannel = 1;

    // Compute per-channel medians (robust against outliers)
    double medians[3] = {0.0};
    for (int c = 0; c < 3; ++c) {
        std::vector<float> channelData;
        channelData.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            float v = data[i * 3 + c];
            if (v != 0.0f) channelData.push_back(v);
        }
        if (!channelData.empty())
            medians[c] = Statistics::quickMedian(channelData);
    }

    double targetMedian = medians[referenceChannel];
    if (targetMedian < 1e-9) return;

    double factors[3];
    for (int c = 0; c < 3; ++c)
        factors[c] = (medians[c] > 1e-9) ? targetMedian / medians[c] : 1.0;

    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < 3; ++c) {
            float& val = data[i * 3 + c];
            if (val != 0.0f)
                val *= static_cast<float>(factors[c]);
        }
    }
}

// ============================================================================
// Helper: Single-image statistics
// ============================================================================

bool Normalization::computeImageStats(const ImageBuffer& buffer,
                                       int layer, bool fastMode,
                                       ImageStats& stats)
{
    if (buffer.width() == 0 || buffer.height() == 0) return false;

    const int    channels = buffer.channels();
    const size_t total    = static_cast<size_t>(buffer.width()) * buffer.height();
    const float* data     = buffer.data().data();

    std::vector<float> vec;
    vec.reserve(total);
    for (size_t p = 0; p < total; ++p) {
        float v = data[p * channels + layer];
        if (v != 0.0f) vec.push_back(v);
    }

    if (vec.empty()) return false;

    stats.median = Statistics::quickMedian(vec);
    stats.mad    = Statistics::mad(vec, static_cast<float>(stats.median));

    if (fastMode) {
        stats.location = stats.median;
        stats.scale    = 1.5 * stats.mad;
    } else {
        stats.location = stats.median;
        stats.scale    = stats.mad;
    }

    stats.valid = true;
    return true;
}

// ============================================================================
// Stub: Overlap finder (placeholder for future implementation)
// ============================================================================

bool Normalization::findOverlap(const SequenceImage&, const SequenceImage&,
                                 int&, int&, int&, int&)
{
    return false;
}

} // namespace Stacking