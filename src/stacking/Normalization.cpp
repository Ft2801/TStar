
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

//=============================================================================
// MAIN COEFFICIENT COMPUTATION
//=============================================================================

bool Normalization::computeCoefficients(
    const ImageSequence& sequence,
    const StackingParams& params,
    NormCoefficients& coefficients,
    ProgressCallback progressCallback)
{
    if (!sequence.isValid()) {
        return false;
    }
    
    int nbImages = sequence.count();
    int nbLayers = sequence.channels();
    
    // Initialize coefficients
    coefficients.init(nbImages, nbLayers);
    
    if (params.normalization == NormalizationMethod::None) {
        return true;
    }

    if (params.overlapNormalization) {
        return computeOverlapNormalization(sequence, params, coefficients, progressCallback);
    }
    
    return computeFullImageNormalization(sequence, params, coefficients, progressCallback);
}

//=============================================================================
// FULL IMAGE NORMALIZATION — Two-Stage Approach
//
// Stage 1: Compute location and scale estimators for each image and channel.
//   - lite_norm (fast): location = median, scale = 1.5 * MAD
//   - full:             location = median, scale = MAD (we use MAD since
//                       we don't have IKSS/BWMV)
//
// Stage 2: Compute factors from estimators relative to reference image.
//   compute_factors_from_estimators():
//   - ADDITIVE_SCALING:  pscale[i] = refScale / imgScale
//                        poffset[i] = pscale[i] * imgLocation - refLocation
//   - ADDITIVE:          poffset[i] = imgLocation - refLocation  (pscale=1)
//   - MULTIPLICATIVE_SCALING: pscale[i] = refScale / imgScale
//                              pmul[i] = refLocation / imgLocation
//   - MULTIPLICATIVE:    pmul[i] = refLocation / imgLocation  (pscale=1)
//
// Per-pixel application
//   - ADDITIVE / ADDITIVE_SCALING:       pixel * pscale - poffset
//   - MULTIPLICATIVE / MULT_SCALING:     pixel * pscale * pmul
//=============================================================================

bool Normalization::computeFullImageNormalization(
    const ImageSequence& sequence,
    const StackingParams& params,
    NormCoefficients& coefficients,
    ProgressCallback progressCallback)
{
    int nbImages = sequence.count();
    int refImage = params.refImageIndex;
    if (refImage < 0 || refImage >= nbImages) refImage = sequence.referenceImage();
    
    int nbLayers = sequence.channels();
    bool liteNorm = params.fastNormalization;
    int regLayer = (params.registrationLayer >= 0 && params.registrationLayer < nbLayers) 
                   ? params.registrationLayer : 1;

    // ===== Stage 1: Compute estimators for ALL images =====
    // We store location in poffset, scale in pscale, and location(for mul) in pmul
    // These will be overwritten with actual factors in Stage 2.
    
    struct ImageEstimators {
        double location[3] = {0.0, 0.0, 0.0};
        double scale[3]    = {1.0, 1.0, 1.0};
        bool valid = false;
    };
    
    std::vector<ImageEstimators> estimators(nbImages);
    
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nbImages; ++i) {
        ImageBuffer buffer;
        if (!sequence.readImage(i, buffer)) {
            continue;
        }
        
        int imgLayers = buffer.channels();
        size_t total = (size_t)buffer.width() * buffer.height();
        if (total == 0) continue;
        
        for (int c = 0; c < imgLayers && c < 3; ++c) {
            // Extract non-zero pixel values for this channel
            std::vector<float> vec;
            vec.reserve(total);
            const float* ptr = buffer.data().data();
            for (size_t p = 0; p < total; ++p) {
                float v = ptr[p * imgLayers + c];
                if (v != 0.0f) vec.push_back(v);
            }
            
            if (vec.empty()) continue;
            
            float median = Statistics::quickMedian(vec);
            float mad = Statistics::mad(vec, median);
            
            estimators[i].location[c] = median;

            if (liteNorm) {
                estimators[i].scale[c] = 1.5 * mad;
            } else {
                estimators[i].scale[c] = mad;
            }
        }
        estimators[i].valid = true;
        
        if (progressCallback) {
            progressCallback("Computing estimators", (double)(i + 1) / (double)nbImages * 0.5);
        }
    }
    
    // Verify reference image has valid estimators
    if (!estimators[refImage].valid) {
        return false;
    }
    
    // ===== Stage 2: Compute factors from estimators =====
    // Reference estimators
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
                // Identity coefficients for invalid images
                coefficients.poffset[layer][i] = 0.0;
                coefficients.pmul[layer][i] = 1.0;
                coefficients.pscale[layer][i] = 1.0;
                continue;
            }
            
            double imgLocation = estimators[i].location[layer];
            double imgScale    = estimators[i].scale[layer];
            
            switch (params.normalization) {
                default:
                case NormalizationMethod::AdditiveScaling:
                    // pscale = refScale / imgScale
                    coefficients.pscale[layer][i] = (imgScale == 0.0) ? 1.0 
                        : refScale[reflayer] / imgScale;
                    // poffset = pscale * imgLocation - refLocation
                    coefficients.poffset[layer][i] = 
                        coefficients.pscale[layer][i] * imgLocation - refLocation[reflayer];
                    coefficients.pmul[layer][i] = 1.0;
                    break;
                    
                case NormalizationMethod::Additive:
                    // pscale = 1.0 (no scaling)
                    coefficients.pscale[layer][i] = 1.0;
                    // poffset = imgLocation - refLocation
                    coefficients.poffset[layer][i] = imgLocation - refLocation[reflayer];
                    coefficients.pmul[layer][i] = 1.0;
                    break;
                    
                case NormalizationMethod::MultiplicativeScaling:
                    // pscale = refScale / imgScale
                    coefficients.pscale[layer][i] = (imgScale == 0.0) ? 1.0 
                        : refScale[reflayer] / imgScale;
                    // pmul = refLocation / imgLocation
                    coefficients.pmul[layer][i] = (imgLocation == 0.0) ? 1.0 
                        : refLocation[reflayer] / imgLocation;
                    coefficients.poffset[layer][i] = 0.0;
                    break;
                    
                case NormalizationMethod::Multiplicative:
                    // pscale = 1.0 (no scaling)
                    coefficients.pscale[layer][i] = 1.0;
                    // pmul = refLocation / imgLocation
                    coefficients.pmul[layer][i] = (imgLocation == 0.0) ? 1.0 
                        : refLocation[reflayer] / imgLocation;
                    coefficients.poffset[layer][i] = 0.0;
                    break;
            }
            
            // Also set the global (mono) coefficients from layer 1 (green) or layer 0
            if (layer == 1 || (layer == 0 && nbLayers == 1)) {
                coefficients.scale[i] = coefficients.pscale[layer][i];
                coefficients.offset[i] = coefficients.poffset[layer][i];
                coefficients.mul[i] = coefficients.pmul[layer][i];
            }
        }
    }
    
    if (progressCallback) {
        progressCallback("Normalization complete", 1.0);
    }
    
    return true;
}

//=============================================================================
// OVERLAP NORMALIZATION
// Same two-stage approach but using overlap ROI pixels only.
//=============================================================================

bool Normalization::computeOverlapNormalization(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback) 
{
    (void)progressCallback;
    int nbImages = sequence.count();
    int refImage = params.refImageIndex;
    if (refImage < 0 || refImage >= nbImages) refImage = sequence.referenceImage();

    ImageBuffer refBuffer;
    if (!sequence.readImage(refImage, refBuffer)) return false;
    
    int w = refBuffer.width();
    int h = refBuffer.height();
    int nbLayers = refBuffer.channels();
    int regLayer = (params.registrationLayer >= 0 && params.registrationLayer < nbLayers) 
                   ? params.registrationLayer : 1;

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nbImages; ++i) {
        if (i == refImage) {
            for (int c = 0; c < nbLayers && c < 3; ++c) {
                coefficients.poffset[c][i] = 0.0;
                coefficients.pmul[c][i] = 1.0;
                coefficients.pscale[c][i] = 1.0;
            }
            continue;
        }

        // Get Reg Data
        const auto& imgInfo = sequence.image(i);
        const auto& refInfo = sequence.image(refImage);
        
        double dx = imgInfo.registration.shiftX - refInfo.registration.shiftX;
        double dy = imgInfo.registration.shiftY - refInfo.registration.shiftY;
        
        int r_x = std::max(0, (int)dx);
        int r_y = std::max(0, (int)dy);
        int r_w = std::min(w, (int)(w + dx)) - r_x;
        int r_h = std::min(h, (int)(h + dy)) - r_y;
        
        if (r_w < 50 || r_h < 50) {
             // Too small overlap -> identity
             for (int c = 0; c < nbLayers && c < 3; ++c) {
                 coefficients.poffset[c][i] = 0.0;
                 coefficients.pmul[c][i] = 1.0;
                 coefficients.pscale[c][i] = 1.0;
             }
             continue;
        }

        ImageBuffer tgtBuffer;
        if (!sequence.readImage(i, tgtBuffer)) continue;
        
        size_t roi_n = (size_t)r_w * r_h;

        for (int c = 0; c < nbLayers && c < 3; ++c) {
             int reflayer = params.equalizeRGB ? regLayer : c;
             if (reflayer >= nbLayers) reflayer = c;

             std::vector<float> ref_roi;
             std::vector<float> tgt_roi;
             ref_roi.reserve(roi_n);
             tgt_roi.reserve(roi_n);

             const float* rptr = refBuffer.data().data();
             const float* tptr = tgtBuffer.data().data();
             
             for (int y = 0; y < r_h; ++y) {
                 for (int x = 0; x < r_w; ++x) {
                     int rx = r_x + x;
                     int ry = r_y + y;
                     int tx = rx - (int)dx; 
                     int ty = ry - (int)dy;
                     
                     // Both pixels must be non-zero
                     float refVal = 0.0f, tgtVal = 0.0f;
                     bool refOk = (rx >= 0 && rx < w && ry >= 0 && ry < h);
                     bool tgtOk = (tx >= 0 && tx < w && ty >= 0 && ty < h);
                     
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
                 coefficients.pmul[c][i] = 1.0;
                 coefficients.pscale[c][i] = 1.0;
                 continue;
             }

             // Compute estimators for overlap regions
             float refMedian = Statistics::quickMedian(ref_roi);
             float tgtMedian = Statistics::quickMedian(tgt_roi);
             float refMAD = Statistics::mad(ref_roi, refMedian);
             float tgtMAD = Statistics::mad(tgt_roi, tgtMedian);
             
             double refLoc = refMedian;
             double tgtLoc = tgtMedian;
             double refSca = params.fastNormalization ? 1.5 * refMAD : (double)refMAD;
             double tgtSca = params.fastNormalization ? 1.5 * tgtMAD : (double)tgtMAD;
             
             // Compute factors (same logic as Stage 2 above)
             switch (params.normalization) {
                 default:
                 case NormalizationMethod::AdditiveScaling:
                     coefficients.pscale[c][i] = (tgtSca == 0.0) ? 1.0 : refSca / tgtSca;
                     coefficients.poffset[c][i] = coefficients.pscale[c][i] * tgtLoc - refLoc;
                     coefficients.pmul[c][i] = 1.0;
                     break;
                 case NormalizationMethod::Additive:
                     coefficients.pscale[c][i] = 1.0;
                     coefficients.poffset[c][i] = tgtLoc - refLoc;
                     coefficients.pmul[c][i] = 1.0;
                     break;
                 case NormalizationMethod::MultiplicativeScaling:
                     coefficients.pscale[c][i] = (tgtSca == 0.0) ? 1.0 : refSca / tgtSca;
                     coefficients.pmul[c][i] = (tgtLoc == 0.0) ? 1.0 : refLoc / tgtLoc;
                     coefficients.poffset[c][i] = 0.0;
                     break;
                 case NormalizationMethod::Multiplicative:
                     coefficients.pscale[c][i] = 1.0;
                     coefficients.pmul[c][i] = (tgtLoc == 0.0) ? 1.0 : refLoc / tgtLoc;
                     coefficients.poffset[c][i] = 0.0;
                     break;
             }
             
             // Set mono coefficients from green/mono channel
             if (c == 1 || (c == 0 && nbLayers == 1)) {
                 coefficients.scale[i] = coefficients.pscale[c][i];
                 coefficients.offset[i] = coefficients.poffset[c][i];
                 coefficients.mul[i] = coefficients.pmul[c][i];
             }
        }
    }
    return true;
}


//=============================================================================
// APPLICATION HELPERS
//
//   ADDITIVE / ADDITIVE_SCALING:    pixel * pscale - poffset
//   MULTIPLICATIVE / MULT_SCALING:  pixel * pscale * pmul
//   NO_NORM:                        pixel (identity)
//=============================================================================

float Normalization::applyToPixel(float pixel, NormalizationMethod method, 
                                  int imageIndex, int layer, 
                                  const NormCoefficients& coefficients) {
    if (method == NormalizationMethod::None) return pixel;
    if (pixel == 0.0f) return 0.0f; // Do not normalize zero pixels
    if (imageIndex < 0 || imageIndex >= (int)coefficients.scale.size()) return pixel;
    
    double pscale = 1.0, poffset = 0.0, pmul = 1.0;
    
    if (layer >= 0 && layer < 3 && 
        imageIndex < (int)coefficients.pscale[layer].size()) {
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

void Normalization::applyToImage(ImageBuffer& buffer, NormalizationMethod method, 
                                 int imageIndex, const NormCoefficients& coefficients) {
    if (method == NormalizationMethod::None) return;
    
    int w = buffer.width();
    int h = buffer.height();
    int channels = buffer.channels();
    float* data = buffer.data().data();
    size_t count = (size_t)w * h;
    
    for (int c = 0; c < channels; ++c) {
        int layerIdx = (channels == 1) ? 0 : c;
        
        double pscale = 1.0, poffset = 0.0, pmul = 1.0;
        
        if (layerIdx < 3 && imageIndex < (int)coefficients.pscale[layerIdx].size()) {
            pscale  = coefficients.pscale[layerIdx][imageIndex];
            poffset = coefficients.poffset[layerIdx][imageIndex];
            pmul    = coefficients.pmul[layerIdx][imageIndex];
        } else if (imageIndex < (int)coefficients.scale.size()) {
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
            if (val == 0.0f) continue; // Skip zero pixels
            
            if (isAdditive) {
                data[idx] = static_cast<float>(val * pscale - poffset);
            } else {
                data[idx] = static_cast<float>(val * pscale * pmul);
            }
        }
    }
}

//=============================================================================
// OUTPUT NORMALIZATION
// Skips zero pixels in min/max search and preserves them as 0 in output.
//=============================================================================

void Normalization::normalizeOutput(ImageBuffer& buffer) {
    int w = buffer.width();
    int h = buffer.height();
    int channels = buffer.channels();
    float* data = buffer.data().data();
    size_t count = (size_t)w * h * channels;
    
    float minVal = 1e30f;
    float maxVal = -1e30f;
    
    // Skip zero pixels in min/max calculation
    #pragma omp parallel for reduction(min:minVal) reduction(max:maxVal)
    for (size_t i = 0; i < count; ++i) {
        float val = data[i];
        if (val == 0.0f) continue; // Skip zero pixels
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    if (maxVal <= minVal) return;
    
    float range = maxVal - minVal;
    float invRange = 1.0f / range;
    
    // Preserve zero pixels as 0, normalize the rest
    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        if (data[i] == 0.0f) continue; // Preserve zero pixels
        data[i] = (data[i] - minVal) * invRange;
    }
}

//=============================================================================
// RGB EQUALIZATION — Using median for robustness
//=============================================================================

void Normalization::equalizeRGB(ImageBuffer& buffer, int referenceChannel) {
    if (buffer.channels() != 3) return;
    
    size_t count = buffer.width() * buffer.height();
    float* data = buffer.data().data();
    
    if (referenceChannel < 0 || referenceChannel > 2) referenceChannel = 1; // Green
    
    // Compute median per channel (more robust than mean)
    double medians[3] = {0.0};
    for (int c = 0; c < 3; ++c) {
        std::vector<float> channelData;
        channelData.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            float v = data[i * 3 + c];
            if (v != 0.0f) channelData.push_back(v);
        }
        if (!channelData.empty()) {
            medians[c] = Statistics::quickMedian(channelData);
        }
    }
    
    double targetMedian = medians[referenceChannel];
    if (targetMedian < 1e-9) return;
    
    double factors[3];
    for (int c = 0; c < 3; ++c) {
        factors[c] = (medians[c] > 1e-9) ? targetMedian / medians[c] : 1.0;
    }
    
    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < 3; ++c) {
            float& val = data[i * 3 + c];
            if (val != 0.0f) {
                val *= (float)factors[c];
            }
        }
    }
}

//=============================================================================
// HELPER STUBS
//=============================================================================

bool Normalization::computeImageStats(const ImageBuffer& buffer, int layer, bool fastMode, ImageStats& stats) {
    if (buffer.width() == 0 || buffer.height() == 0) return false;
    
    int channels = buffer.channels();
    size_t total = (size_t)buffer.width() * buffer.height();
    const float* data = buffer.data().data();
    
    // Extract non-zero pixels for this layer
    std::vector<float> vec;
    vec.reserve(total);
    for (size_t p = 0; p < total; ++p) {
        float v = data[p * channels + layer];
        if (v != 0.0f) vec.push_back(v);
    }
    
    if (vec.empty()) return false;
    
    stats.median = Statistics::quickMedian(vec);
    stats.mad = Statistics::mad(vec, (float)stats.median);
    
    if (fastMode) {
        stats.location = stats.median;
        stats.scale = 1.5 * stats.mad;
    } else {
        stats.location = stats.median;
        stats.scale = stats.mad;
    }
    
    stats.valid = true;
    return true;
}

bool Normalization::findOverlap(const SequenceImage&, const SequenceImage&, int&, int&, int&, int&) { 
    return false; 
}

} // namespace Stacking
