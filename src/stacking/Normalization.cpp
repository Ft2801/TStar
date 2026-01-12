
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



// Helper (Static, not class member)
static float applyToPixels_Shim(float pixel, Stacking::NormalizationMethod, int imageIndex, int layer, const Stacking::NormCoefficients& coefficients) {
    if (imageIndex < 0 || imageIndex >= (int)coefficients.scale.size()) return pixel;
    
    double slope = 1.0;
    double intercept = 0.0;
    
    if (layer >= 0 && layer < 3) {
        slope = coefficients.pscale[layer][imageIndex];
        intercept = coefficients.poffset[layer][imageIndex];
    } else {
        slope = coefficients.scale[imageIndex];
        intercept = coefficients.offset[imageIndex];
    }
    
    return static_cast<float>(pixel * slope + intercept);
}

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
// FULL IMAGE NORMALIZATION
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
    
    // Load Reference Data
    ImageBuffer refBuffer;
    if (!sequence.readImage(refImage, refBuffer)) return false;
    
    int nbLayers = refBuffer.channels();
    
    // If Equalize RGB is enabled, we normalize all channels against the reference's GREEN channel (index 1)
    
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nbImages; ++i) {
        if (i == refImage) {
            for(int c=0; c<nbLayers; ++c) coefficients.set(i, c, 1.0, 0.0);
            continue;
        }
        
        ImageBuffer tgtBuffer;
        if (!sequence.readImage(i, tgtBuffer)) {
            // Error case, skip or set identity
            for(int c=0; c<nbLayers; ++c) coefficients.set(i, c, 1.0, 0.0);
            continue;
        }
        
        int regLayer = (params.registrationLayer >= 0 && params.registrationLayer < nbLayers) ? params.registrationLayer : 1; 

        for (int c = 0; c < nbLayers && c < 3; ++c) {
             size_t total = (size_t)refBuffer.width() * refBuffer.height();
             // std::vector<float> ref_vec(total); // Removed to avoid dup
             
             // Determine which reference layer to use
             int currentRefLayer = c;
             if (params.equalizeRGB && nbLayers >= 3) {
                 currentRefLayer = regLayer;
             }

             // Exclude zeros for robust statistics (matches Siril behavior)
             std::vector<float> ref_vec;
             ref_vec.reserve(total);
             const float* rptr = refBuffer.data().data();
             for(size_t p=0; p<total; p++) {
                float v = rptr[p * nbLayers + currentRefLayer];
                if (v != 0.0f) ref_vec.push_back(v);
             }
             
             std::vector<float> tgt_vec;
             tgt_vec.reserve(total);
             const float* tptr = tgtBuffer.data().data();
             for(size_t p=0; p<total; p++) {
                float v = tptr[p * nbLayers + c];
                if (v != 0.0f) tgt_vec.push_back(v);
             }

             // Compute robust statistics (Identity if vectors empty)
             if (total == 0) continue;

             float m1 = Statistics::quickMedian(ref_vec);
             float m2 = Statistics::quickMedian(tgt_vec);
             
             double slope = 1.0;
             double intercept = 0.0;
             
             if (params.normalization == NormalizationMethod::Additive) {
                 slope = 1.0;
                 intercept = m1 - m2;
             }
             else if (params.normalization == NormalizationMethod::Multiplicative) {
                 if (std::abs(m2) > 1e-5f) slope = m1 / m2;
                 intercept = 0.0;
             }
             else {
                 // Additive + Scaling
                 float s1 = Statistics::mad(ref_vec, m1);
                 float s2 = Statistics::mad(tgt_vec, m2);
                 
                 if (std::abs(s2) > 1e-5f) slope = s1 / s2;
                 intercept = m1 - slope * m2;
             }
             
             coefficients.set(i, c, slope, intercept);
        }
        if (progressCallback) progressCallback("Normalized", (double)(i+1)/(double)nbImages);
    }
    
    return true;
}

//=============================================================================
// OVERLAP NORMALIZATION
//=============================================================================

bool Normalization::computeOverlapNormalization(
        const ImageSequence& sequence,
        const StackingParams& params,
        NormCoefficients& coefficients,
        ProgressCallback progressCallback) 
{
    (void)progressCallback;
    // Simplified Overlap: Find Intersection ROI -> Compute Norm on ROI
    // REQUIRES Registration info in sequence
    int nbImages = sequence.count();
    int refImage = params.refImageIndex;
    if (refImage < 0 || refImage >= nbImages) refImage = sequence.referenceImage();

    ImageBuffer refBuffer;
    if (!sequence.readImage(refImage, refBuffer)) return false;
    
    int w = refBuffer.width();
    int h = refBuffer.height();
    int nbLayers = refBuffer.channels();

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nbImages; ++i) {
        if (i == refImage) {
            for(int c=0; c<nbLayers; ++c) coefficients.set(i, c, 1.0, 0.0);
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
             // Too small overlap -> fallbackIdentity
             for(int c=0; c<nbLayers; ++c) coefficients.set(i, c, 1.0, 0.0);
             continue;
        }

        ImageBuffer tgtBuffer;
        if (!sequence.readImage(i, tgtBuffer)) continue;
        
        // Extract ROI
        int regLayer = (params.registrationLayer >= 0 && params.registrationLayer < nbLayers) ? params.registrationLayer : 1;
        size_t roi_n = (size_t)r_w * r_h;

        for (int c = 0; c < nbLayers && c < 3; ++c) {
             int currentRefLayer = c;
             if (params.equalizeRGB && nbLayers >= 3) {
                 currentRefLayer = regLayer;
             }

             std::vector<float> ref_roi;
             std::vector<float> tgt_roi;
             ref_roi.reserve(roi_n);
             tgt_roi.reserve(roi_n);

             const float* rptr = refBuffer.data().data();
             const float* tptr = tgtBuffer.data().data();
             
             for(int y=0; y<r_h; ++y) {
                 for(int x=0; x<r_w; ++x) {
                     // Ref Coord
                     int rx = r_x + x;
                     int ry = r_y + y;
                     // Tgt Coord (shifted back)
                     int tx = rx - (int)dx; 
                     int ty = ry - (int)dy;
                     
                     if (rx>=0 && rx<w && ry>=0 && ry<h) {
                        float v = rptr[(ry*w+rx)*nbLayers + currentRefLayer];
                        if (v != 0.0f) ref_roi.push_back(v);
                     }
                        
                     if (tx>=0 && tx<w && ty>=0 && ty<h) {
                        float v = tptr[(ty*w+tx)*nbLayers + c];
                        if (v != 0.0f) tgt_roi.push_back(v);
                     }
                 }
             }
             
             if (ref_roi.empty() || tgt_roi.empty()) {
                 coefficients.set(i, c, 1.0, 0.0);
                 continue;
             }

             float m1 = Statistics::quickMedian(ref_roi);
             float m2 = Statistics::quickMedian(tgt_roi);
             
             double slope = 1.0;
             double intercept = 0.0;
             
             if (params.normalization == NormalizationMethod::Additive) {
                 slope = 1.0;
                 intercept = m1 - m2;
             }
             else if (params.normalization == NormalizationMethod::Multiplicative) {
                 if (std::abs(m2) > 1e-5f) slope = m1 / m2;
                 intercept = 0.0;
             }
             else if (params.normalization == NormalizationMethod::AdditiveScaling || 
                      params.normalization == NormalizationMethod::MultiplicativeScaling) {
                 // Additive + Scaling
                 float s1 = Statistics::mad(ref_roi, m1);
                 float s2 = Statistics::mad(tgt_roi, m2);
                 
                 if (std::abs(s2) > 1e-5f) slope = s1 / s2;
                 intercept = m1 - slope * m2;
             }
             
             coefficients.set(i, c, slope, intercept);
        }
    }
    return true;
}

bool Normalization::computeGradientNormalization(
        const ImageSequence&,
        const StackingParams&,
        NormCoefficients&,
        ProgressCallback) { 
    // Not implemented yet
    return false; 
}


//=============================================================================
// APPLICATION HELPERS
//=============================================================================

float Normalization::applyToPixel(float pixel, NormalizationMethod m, int imageIndex, int layer, const NormCoefficients& coefficients) {
    return applyToPixels_Shim(pixel, m, imageIndex, layer, coefficients);
}

void Normalization::applyToImage(ImageBuffer& buffer, NormalizationMethod method, int imageIndex, const NormCoefficients& coefficients) {
    if (method == NormalizationMethod::None) return;
    
    int w = buffer.width();
    int h = buffer.height();
    int channels = buffer.channels();
    float* data = buffer.data().data();
    size_t count = (size_t)w * h;
    
    for (int c = 0; c < channels; ++c) {
        double slope = 1.0;
        double intercept = 0.0;
        
        int layerIdx = (channels == 1) ? 0 : c; // simplified mapping
        
        if (layerIdx < 3 && imageIndex < (int)coefficients.pscale[layerIdx].size()) {
            slope = coefficients.pscale[layerIdx][imageIndex];
            intercept = coefficients.poffset[layerIdx][imageIndex];
        } else if (imageIndex < (int)coefficients.scale.size()) {
             slope = coefficients.scale[imageIndex];
             intercept = coefficients.offset[imageIndex];
        }
        
        if (std::abs(slope) < 1e-9) slope = 1.0;
        
        #pragma omp parallel for
        for (size_t i = 0; i < count; ++i) {
            size_t idx = i * channels + c;
            data[idx] = static_cast<float>(data[idx] * slope + intercept);
        }
    }
}

void Normalization::normalizeOutput(ImageBuffer& buffer) {
    int w = buffer.width();
    int h = buffer.height();
    int channels = buffer.channels();
    float* data = buffer.data().data();
    size_t count = (size_t)w * h * channels;
    
    float minVal = 1e30f;
    float maxVal = -1e30f;
    
    #pragma omp parallel for reduction(min:minVal) reduction(max:maxVal)
    for (size_t i = 0; i < count; ++i) {
        float val = data[i];
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    if (maxVal <= minVal) return;
    
    float range = maxVal - minVal;
    float scale = 1.0f / range;
    
    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        data[i] = (data[i] - minVal) * scale;
    }
}

void Normalization::equalizeRGB(ImageBuffer& buffer, int referenceChannel) {
    if (buffer.channels() != 3) return;
    
    double means[3] = {0};
    size_t count = buffer.width() * buffer.height();
    float* data = buffer.data().data();
    
    for (int c = 0; c < 3; ++c) {
        double sum = 0;
        #pragma omp parallel for reduction(+:sum)
        for (size_t i = 0; i < count; ++i) {
            sum += data[i * 3 + c];
        }
        means[c] = sum / count;
    }
    
    if (referenceChannel < 0 || referenceChannel > 2) referenceChannel = 1; // Green
    double targetMean = means[referenceChannel];
    if (targetMean < 1e-9) return;
    
    double factors[3];
    for(int c=0; c<3; ++c) {
        factors[c] = (means[c] > 1e-9) ? targetMean / means[c] : 1.0;
    }
    
    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < 3; ++c) {
            data[i*3 + c] *= (float)factors[c];
        }
    }
}

bool Normalization::computeImageStats(const ImageBuffer&, int, bool, ImageStats&) { return false; }
bool Normalization::findOverlap(const SequenceImage&, const SequenceImage&, int&, int&, int&, int&) { return false; }

} // namespace Stacking
