/**
 * @file PreprocessingTypes.h
 * @brief Core types, enumerations, and parameter structures for the
 *        image preprocessing pipeline.
 *
 * This header defines all shared types used across the preprocessing
 * subsystem, including calibration frame identifiers, Bayer/CFA
 * pattern descriptors, demosaicing algorithm selectors, cosmetic
 * correction parameters, and the main preprocessing configuration
 * structure.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef PREPROCESSING_TYPES_H
#define PREPROCESSING_TYPES_H

#include <QString>
#include <functional>

namespace Preprocessing {

// ============================================================================
//  Enumerations
// ============================================================================

/**
 * @brief Identifies the type of a master calibration frame.
 */
enum class MasterType {
    Bias = 0,   ///< Master bias (sensor offset / read noise)
    Dark,       ///< Master dark (thermal signal)
    Flat,       ///< Master flat (optical vignetting / dust)
    DarkFlat    ///< Master dark specifically matched to flat exposures
};

/**
 * @brief Bayer / CFA pattern layout for single-channel sensor data.
 *
 * Standard 2x2 Bayer patterns are listed explicitly.  XTrans refers
 * to the Fujifilm 6x6 colour filter array.  Auto indicates that the
 * pattern should be read from the file header at load time.
 */
enum class BayerPattern {
    Auto  = -1, ///< Auto-detect from FITS / XISF header
    None  =  0, ///< Monochrome sensor or already-debayered data
    RGGB,       ///< Red  - Green / Green - Blue
    BGGR,       ///< Blue - Green / Green - Red
    GBRG,       ///< Green - Blue / Red   - Green
    GRBG,       ///< Green - Red  / Blue  - Green
    XTrans      ///< Fujifilm X-Trans 6x6 CFA
};

/**
 * @brief Selects the demosaicing (debayering) algorithm.
 */
enum class DebayerAlgorithm {
    Bilinear = 0, ///< Simple bilinear interpolation (fast, lower quality)
    VNG,          ///< Variable Number of Gradients
    AHD,          ///< Adaptive Homogeneity-Directed
    SuperPixel,   ///< 2x2 super-pixel binning (halves resolution)
    RCD,          ///< Ratio Corrected Demosaicing
    EdgeAware     ///< Edge-aware gradient-based interpolation
};

/**
 * @brief Strategy for detecting and correcting cosmetic defects.
 */
enum class CosmeticType {
    None = 0,   ///< No cosmetic correction applied
    FromMaster, ///< Derive defect map from the master dark frame
    Sigma,      ///< Statistical sigma-clipping detection per image
    Custom      ///< User-supplied bad-pixel map file
};

// ============================================================================
//  Callback Types
// ============================================================================

/**
 * @brief Callback for reporting progress.
 * @param message  Human-readable status string.
 * @param progress Fractional progress in the range [0.0, 1.0].
 */
using ProgressCallback = std::function<void(const QString& message, double progress)>;

/**
 * @brief Callback that returns true when the operation should be cancelled.
 */
using CancelCheck = std::function<bool()>;

// ============================================================================
//  Data Structures
// ============================================================================

/**
 * @brief Descriptive statistics computed for a loaded master frame.
 */
struct MasterStats {
    double mean        = 0.0;
    double median      = 0.0;
    double sigma       = 0.0;
    double min         = 0.0;
    double max         = 0.0;
    double exposure    = 0.0;   ///< Exposure time in seconds
    double temperature = 0.0;   ///< CCD temperature in degrees Celsius
    int    width       = 0;
    int    height      = 0;
    int    channels    = 0;
};

/**
 * @brief Configuration for cosmetic (hot / cold pixel) correction.
 */
struct CosmeticParams {
    CosmeticType type      = CosmeticType::None;
    float  coldSigma       = 3.0f;   ///< Sigma threshold for cold pixel detection
    float  hotSigma        = 3.0f;   ///< Sigma threshold for hot pixel detection
    double coldThreshold   = 0.0;    ///< Absolute threshold for cold pixels
    double hotThreshold    = 1.0;    ///< Absolute threshold for hot pixels
    QString badPixelMap;             ///< File path to a custom bad-pixel map
};

/**
 * @brief Configuration for numerical dark-frame scaling optimisation.
 *
 * When enabled, the engine searches for the scale factor K in
 * [K_min, K_max] that minimises the residual noise after dark
 * subtraction.
 */
struct DarkOptimParams {
    bool  enabled       = false;
    float K_min         = 0.5f;    ///< Lower bound of the search interval
    float K_max         = 2.0f;    ///< Upper bound of the search interval
    float tolerance     = 0.001f;  ///< Convergence tolerance for the solver
    int   maxIterations = 100;     ///< Maximum number of solver iterations
};

/**
 * @brief Configuration for per-channel CFA equalisation.
 */
struct CFAEqualizeParams {
    bool enabled       = false;
    bool preserveTotal = true;  ///< Scale channels so total intensity is preserved
};

/**
 * @brief Aggregate parameter set for the entire preprocessing pipeline.
 *
 * Collect all options that control calibration frame paths, calibration
 * flags, CFA handling, cosmetic correction, output format, and
 * miscellaneous sensor fixes into a single transportable structure.
 */
struct PreprocessParams {
    // -- Master frame file paths --
    QString masterBias;         ///< Path to master bias frame
    QString masterDark;         ///< Path to master dark frame
    QString masterFlat;         ///< Path to master flat frame
    QString masterDarkFlat;     ///< Path to dark-for-flat frame (optional)

    // -- Calibration enable flags --
    bool useBias = true;
    bool useDark = true;
    bool useFlat = true;

    // -- Dark optimisation --
    DarkOptimParams darkOptim;

    // -- CFA / debayering --
    BayerPattern    bayerPattern     = BayerPattern::None;
    bool            debayer          = false;
    DebayerAlgorithm debayerAlgorithm = DebayerAlgorithm::RCD;
    CFAEqualizeParams cfaEqualize;

    // -- Cosmetic correction --
    CosmeticParams cosmetic;

    // -- Output options --
    bool    outputFloat  = true;   ///< Write output as 32-bit IEEE float FITS
    QString outputPrefix;          ///< Filename prefix prepended to each output
    QString outputDir;             ///< Directory for output files

    // -- Sensor-specific fixes --
    bool fixBadLines = false;      ///< Repair defective CCD rows / columns
    bool fixBanding  = false;      ///< Reduce periodic sensor banding noise
    bool fixXTrans   = false;      ///< Correct X-Trans phase-detect AF artefacts

    // -- Advanced calibration options --
    double biasLevel    = 1e30;    ///< Synthetic bias level (1e30 = use master file instead)
    double pedestal     = 0.0;     ///< ADU pedestal added after dark subtraction
    bool   equalizeFlat = false;   ///< Equalise CFA channel medians in the master flat
};

} // namespace Preprocessing

#endif // PREPROCESSING_TYPES_H