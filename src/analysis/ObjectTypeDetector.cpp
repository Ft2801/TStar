#include "ObjectTypeDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <omp.h>

ObjectTypeResult ObjectTypeDetector::detect(const ImageBuffer& buf, int channel) {
    ObjectTypeResult res;
    
    if (!buf.isValid()) {
        return res;
    }
    
    const int w = buf.width();
    const int h = buf.height();
    const int ch = buf.channels();
    const auto& data = buf.data();
    
    if (channel < 0 || channel >= ch) channel = 0;
    
    // Extract channel into linear array
    std::vector<float> channelData(static_cast<size_t>(w) * h);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < w * h; ++i) {
        channelData[i] = data[static_cast<size_t>(i) * ch + channel];
    }
    
    // Compute background statistics
    double bg, noise;
    computeBackgroundStats(channelData.data(), w, h, bg, noise);
    res.backgroundMedian = bg;
    res.backgroundNoise = noise;
    
    // Analyze point sources (stars)
    int pointCount;
    double pointFraction;
    analyzePointSources(channelData.data(), w, h, bg, noise, pointCount, pointFraction);
    res.pointSourceCount = pointCount;
    res.pointSourceFraction = pointFraction;
    
    // Analyze extended sources (galaxies, nebulae)
    double extFraction, extSnr;
    analyzeExtendedSources(channelData.data(), w, h, bg, noise, extFraction, extSnr);
    res.extendedSourceFraction = extFraction;
    res.snr_extended = extSnr;
    
    res.diffuseEmissionFraction = 1.0 - pointFraction - extFraction;
    if (res.diffuseEmissionFraction < 0.0) res.diffuseEmissionFraction = 0.0;
    
    // Classification logic
    if (pointCount < 5) {
        // Very few point sources
        if (extFraction > 0.3) {
            res.type = ObjectType::ExtendedObject;
            res.confidence = 0.85;
        } else if (res.diffuseEmissionFraction > 0.4) {
            res.type = ObjectType::Nebula;
            res.confidence = 0.8;
        } else {
            res.type = ObjectType::Unknown;
            res.confidence = 0.3;
        }
    } else if (pointCount < 30) {
        // Moderate point sources
        if (extFraction > 0.4) {
            res.type = ObjectType::MixedField;
            res.confidence = 0.7;
        } else if (res.diffuseEmissionFraction > 0.3) {
            res.type = ObjectType::Nebula;
            res.confidence = 0.75;
        } else {
            res.type = ObjectType::StarField;
            res.confidence = 0.75;
        }
    } else {
        // Many point sources (crowded field)
        if (pointFraction > 0.7) {
            res.type = ObjectType::DeepField;
            res.confidence = 0.9;
        } else if (res.diffuseEmissionFraction > 0.2) {
            res.type = ObjectType::MixedField;
            res.confidence = 0.75;
        } else {
            res.type = ObjectType::StarField;
            res.confidence = 0.85;
        }
    }
    
    return res;
}

void ObjectTypeDetector::computeBackgroundStats(const float* data, int w, int h,
                                                double& median, double& noise) {
    int npix = w * h;
    std::vector<float> vals(data, data + npix);
    std::sort(vals.begin(), vals.end());
    
    median = vals[npix / 2];
    
    // Compute noise as MAD (Median Absolute Deviation)
    std::vector<float> deviations(npix);
    for (int i = 0; i < npix; ++i) {
        deviations[i] = std::abs(vals[i] - static_cast<float>(median));
    }
    std::sort(deviations.begin(), deviations.end());
    double mad = deviations[npix / 2];
    
    // Convert MAD to std dev: sigma ≈ 1.4826 * MAD
    noise = 1.4826 * mad;
    if (noise < 1e-6) noise = 1e-6;
}

void ObjectTypeDetector::analyzePointSources(const float* data, int w, int h,
                                             double bg, double noise,
                                             int& pointCount, double& pointFraction) {
    // Count pixels above 3σ threshold
    double threshold = bg + 3.0 * noise;
    int npix = w * h;
    int brightPixels = 0;
    
    #pragma omp parallel for reduction(+:brightPixels) schedule(static)
    for (int i = 0; i < npix; ++i) {
        if (data[i] > threshold) {
            brightPixels++;
        }
    }
    
    // Estimate number of point sources via clustering
    // Simple heuristic: assume average star has diameter ~2.5 FWHM ≈ 6 pixels → ~30 pixels per star
    pointCount = (brightPixels + 15) / 30;
    pointFraction = static_cast<double>(brightPixels) / npix;
}

void ObjectTypeDetector::analyzeExtendedSources(const float* data, int w, int h,
                                                double bg, double noise,
                                                double& extFraction, double& snr) {
    // Detect moderately bright extended emission (1-3σ)
    double thresholdLow = bg + 1.0 * noise;
    double thresholdHigh = bg + 3.0 * noise;
    int npix = w * h;
    int modPixels = 0;
    double sumExtended = 0.0;
    
    #pragma omp parallel for reduction(+:modPixels,sumExtended) schedule(static)
    for (int i = 0; i < npix; ++i) {
        if (data[i] > thresholdLow && data[i] < thresholdHigh) {
            modPixels++;
            sumExtended += (data[i] - bg);
        }
    }
    
    extFraction = static_cast<double>(modPixels) / npix;
    
    // Estimate SNR of extended emission
    if (modPixels > 0) {
        double meanExtended = sumExtended / modPixels;
        snr = meanExtended / noise;
    } else {
        snr = 0.0;
    }
}

ObjectTypeDetector::ProcessingRecommendation ObjectTypeDetector::getRecommendations(ObjectType type) {
    ProcessingRecommendation rec;
    
    switch (type) {
        case ObjectType::StarField:
            rec.deconvMethod = "RLTV";
            rec.deconvIterations = 100;
            rec.deconvTvWeight = 0.01;
            rec.denoiseMethod = "Wavelet";
            rec.denoiseAggressiveness = 0.5;
            rec.stretchLinear = false;
            rec.preserveColor = false;
            break;
            
        case ObjectType::DeepField:
            rec.deconvMethod = "Wiener";
            rec.deconvIterations = 1;
            rec.deconvTvWeight = 0.0;
            rec.denoiseMethod = "Wavelet";
            rec.denoiseAggressiveness = 0.7;
            rec.stretchLinear = false;
            rec.preserveColor = false;
            break;
            
        case ObjectType::ExtendedObject:
        case ObjectType::Nebula:
            rec.deconvMethod = "Wiener";
            rec.deconvIterations = 1;
            rec.deconvTvWeight = 0.0;
            rec.denoiseMethod = "TGV";
            rec.denoiseAggressiveness = 0.3;
            rec.stretchLinear = true;
            rec.preserveColor = true;
            break;
            
        case ObjectType::MixedField:
            rec.deconvMethod = "RLTV";
            rec.deconvIterations = 50;
            rec.deconvTvWeight = 0.005;
            rec.denoiseMethod = "TGV";
            rec.denoiseAggressiveness = 0.4;
            rec.stretchLinear = true;
            rec.preserveColor = true;
            break;
            
        default:
            rec.deconvMethod = "Wiener";
            rec.deconvIterations = 1;
            rec.deconvTvWeight = 0.0;
            rec.denoiseMethod = "Wavelet";
            rec.denoiseAggressiveness = 0.5;
            rec.stretchLinear = false;
            rec.preserveColor = false;
            break;
    }
    
    return rec;
}
