/**
 * @file PreprocessingTypes.h
 * @brief Types and enumerations for image preprocessing
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef PREPROCESSING_TYPES_H
#define PREPROCESSING_TYPES_H

#include <QString>
#include <functional>

namespace Preprocessing {

//=============================================================================
// ENUMERATIONS
//=============================================================================

/**
 * @brief Type of master calibration frame
 */
enum class MasterType {
    Bias = 0,    ///< Master Bias (offset)
    Dark,        ///< Master Dark
    Flat,        ///< Master Flat
    DarkFlat     ///< Master Dark for flats
};

/**
 * @brief Bayer pattern for CFA sensors
 */
enum class BayerPattern {
    Auto = -1,   ///< Auto-detect from header
    None = 0,    ///< Not a Bayer image (mono or already debayered)
    RGGB,        ///< Red-Green-Green-Blue
    BGGR,        ///< Blue-Green-Green-Red  
    GBRG,        ///< Green-Blue-Red-Green
    GRBG,        ///< Green-Red-Blue-Green
    XTrans       ///< Fujifilm X-Trans
};

/**
 * @brief Debayering algorithm
 */
enum class DebayerAlgorithm {
    Bilinear = 0,    ///< Simple bilinear interpolation
    VNG,             ///< Variable Number of Gradients
    AHD,             ///< Adaptive Homogeneity-Directed
    SuperPixel,      ///< 2x2 superpixel (half resolution)
    RCD              ///< Ratio Corrected Demosaicing
};

/**
 * @brief Cosmetic correction type
 */
enum class CosmeticType {
    None = 0,       ///< No correction
    FromMaster,     ///< Use data from master dark (cold pixels)
    Sigma,          ///< Statistical detection (sigma clipping)
    Custom          ///< User-defined bad pixel map
};

/**
 * @brief Progress callback type
 */
using ProgressCallback = std::function<void(const QString&, double)>;

/**
 * @brief Cancellation check type
 */
using CancelCheck = std::function<bool()>;

//=============================================================================
// STRUCTURES
//=============================================================================

/**
 * @brief Statistics for a master frame
 */
struct MasterStats {
    double mean = 0.0;
    double median = 0.0;
    double sigma = 0.0;
    double min = 0.0;
    double max = 0.0;
    double exposure = 0.0;
    double temperature = 0.0;
    int width = 0;
    int height = 0;
    int channels = 0;
};

/**
 * @brief Parameters for cosmetic correction
 */
struct CosmeticParams {
    CosmeticType type = CosmeticType::None;
    float coldSigma = 3.0f;       ///< Sigma for cold pixel detection
    float hotSigma = 3.0f;        ///< Sigma for hot pixel detection  
    double coldThreshold = 0.0;   ///< Absolute threshold for cold
    double hotThreshold = 1.0;    ///< Absolute threshold for hot
    QString badPixelMap;          ///< Path to custom bad pixel map
};

/**
 * @brief Parameters for dark optimization
 */
struct DarkOptimParams {
    bool enabled = false;
    float K_min = 0.5f;           ///< Minimum scale factor
    float K_max = 2.0f;           ///< Maximum scale factor
    float tolerance = 0.001f;     ///< Convergence tolerance
    int maxIterations = 100;      ///< Maximum iterations
};

/**
 * @brief Parameters for CFA equalization
 */
struct CFAEqualizeParams {
    bool enabled = false;
    bool preserveTotal = true;    ///< Keep total intensity constant
};

/**
 * @brief Main preprocessing parameters
 */
struct PreprocessParams {
    // Master frames
    QString masterBias;           ///< Path to master bias
    QString masterDark;           ///< Path to master dark  
    QString masterFlat;           ///< Path to master flat
    QString masterDarkFlat;       ///< Path to dark for flat (optional)
    
    // Calibration flags
    bool useBias = true;
    bool useDark = true;
    bool useFlat = true;
    
    // Dark optimization
    DarkOptimParams darkOptim;
    
    // CFA handling
    BayerPattern bayerPattern = BayerPattern::None;
    bool debayer = false;
    DebayerAlgorithm debayerAlgorithm = DebayerAlgorithm::RCD;
    CFAEqualizeParams cfaEqualize;
    
    // Cosmetic correction
    CosmeticParams cosmetic;
    
    // Output options
    bool outputFloat = true;      ///< Output as 32-bit float
    QString outputPrefix;         ///< Prefix for output files
    QString outputDir;            ///< Output directory
    
    // Fixes
    bool fixBadLines = false;     ///< Fix bad CCD lines
    bool fixBanding = false;      ///< Fix sensor banding
    bool fixXTrans = false;       ///< Fix X-Trans sensor artifacts
    
    // Advanced
    double biasLevel = 1e30;      ///< Synthetic bias level (1e30 = use master file)
    double pedestal = 0.0;       ///< Pedestal to add during calibration
    bool equalizeFlat = false;   ///< Equalize CFA channels in master flat
};

} // namespace Preprocessing

#endif // PREPROCESSING_TYPES_H
