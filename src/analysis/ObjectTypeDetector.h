#pragma once
#ifndef OBJECTTYPEDETECTOR_H
#define OBJECTTYPEDETECTOR_H

#include <QString>
#include "../ImageBuffer.h"

/**
 * @brief Automatic object type detection for image processing optimization
 * 
 * Analyzes image characteristics to determine:
 * - Star field (bright point sources, PSF dominated) → Best for deconvolution
 * - Galaxy/Object field (extended sources, diffuse) → Best for restoration
 * - Nebula (diffuse emission, complex structure) → Best for denoise + restore
 * 
 * Used to auto-configure deconvolution, denoise, and other filters
 */

enum class ObjectType {
    Unknown,              // Could not determine
    StarField,            // Dominated by point sources (stars)
    ExtendedObject,       // Single/few galaxies or nebulae
    DeepField,            // Crowded star field with many faint sources
    Nebula,               // Emission/reflection nebulae (diffuse, structured)
    MixedField            // Multiple object types present
};

struct ObjectTypeResult {
    ObjectType type = ObjectType::Unknown;
    double confidence = 0.0;      // [0,1] confidence in classification
    
    // Detail metrics
    int pointSourceCount = 0;     // Number of detected stars
    double pointSourceFraction = 0.0;  // Fraction of pixels > 3σ above background
    double extendedSourceFraction = 0.0; // Fraction in extended sources
    double diffuseEmissionFraction = 0.0; // Fraction in diffuse regions
    
    // SNR and noise characteristics
    double backgroundMedian = 0.0;
    double backgroundNoise = 0.0;
    double snr_extended = 0.0;   // SNR of extended emission
    double snr_point = 0.0;      // SNR of brightest point source
};

/**
 * @brief Detect object type from image statistics
 */
class ObjectTypeDetector {
public:
    /**
     * @brief Analyze image and determine object type
     * 
     * @param buf Image to analyze (can be any number of channels)
     * @param channel Channel to analyze (default 0 for luminance)
     * @return Detection result with type and confidence
     */
    static ObjectTypeResult detect(const ImageBuffer& buf, int channel = 0);
    
    /**
     * @brief Get recommended processing parameters based on object type
     * 
     * Structure containing suggested values for:
     * - Deconvolution: PSF type, iterations, regularization
     * - Denoise: Algorithm choice, aggressiveness
     * - Stretching: Linear/gamma vs asinh preferences
     */
    struct ProcessingRecommendation {
        QString deconvMethod;      // "RLTV" for stars, "Wiener" for extended
        int deconvIterations;
        double deconvTvWeight;
        
        QString denoiseMethod;     // "Wavelet" for stars, "TGV" for nebulae
        double denoiseAggressiveness;
        
        bool stretchLinear;        // true for nebulae (preserve structure), false for stars
        bool preserveColor;        // true for nebulae
    };
    
    static ProcessingRecommendation getRecommendations(ObjectType type);

private:
    // Analysis helpers
    static void computeBackgroundStats(const float* data, int w, int h,
                                       double& median, double& noise);
    static void analyzePointSources(const float* data, int w, int h,
                                    double bg, double noise,
                                    int& pointCount, double& pointFraction);
    static void analyzeExtendedSources(const float* data, int w, int h,
                                       double bg, double noise,
                                       double& extFraction, double& snr);
};

#endif // OBJECTTYPEDETECTOR_H
