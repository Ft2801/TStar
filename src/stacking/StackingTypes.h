/**
 * @file StackingTypes.h
 * @brief Core type definitions for the stacking subsystem.
 *
 * Provides enumerations (stacking method, rejection algorithm,
 * normalization, weighting, filtering, drizzle kernel), data structures
 * (registration transforms, quality metrics, normalization coefficients,
 * stacking parameters), callback typedefs, compile-time constants and
 * string conversion helpers.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef STACKING_TYPES_H
#define STACKING_TYPES_H

#include "../preprocessing/PreprocessingTypes.h"
#include <QPointF>
#include <QString>
#include <cmath>
#include <functional>
#include <vector>
#include <cstdint>

namespace Stacking {

// ============================================================================
// Enumerations
// ============================================================================

/**
 * @brief Stacking combination method.
 */
enum class Method {
    Sum = 0,    ///< Sum all pixel values (with post-hoc normalisation).
    Mean,       ///< Arithmetic mean, optionally with pixel rejection.
    Median,     ///< Pixel-wise median (robust to outliers).
    Max,        ///< Maximum value (e.g. star trails).
    Min,        ///< Minimum value (e.g. bright-artefact removal).
    Mosaic      ///< Mosaic assembly (overlap blending).
};

/**
 * @brief Pixel rejection algorithm applied before mean combination.
 *
 * Each algorithm identifies and excludes outlier pixel values across
 * the stack before the final combination is computed.  Only meaningful
 * when the stacking method is Mean.
 */
enum class Rejection {
    None = 0,       ///< No rejection -- use every pixel value.
    Percentile,     ///< Reject values beyond a percentile range from the median.
    Sigma,          ///< Standard sigma clipping.
    MAD,            ///< Median Absolute Deviation clipping.
    SigmaMedian,    ///< Replace rejected values with the median rather than removing them.
    Winsorized,     ///< Winsorized sigma clipping (more robust variant).
    LinearFit,      ///< Linear-fit clipping.
    GESDT,          ///< Generalized Extreme Studentized Deviate Test.
    Biweight,       ///< Biweight location/scale estimator (robust).
    ModifiedZScore  ///< Modified Z-Score (median-based robust method).
};

/**
 * @brief Pre-combination normalisation strategy.
 *
 * Compensates for varying sky background levels and throughput
 * differences between exposures.
 */
enum class NormalizationMethod {
    None = 0,               ///< No normalisation.
    Additive,               ///< Additive offset to match medians.
    Multiplicative,         ///< Multiplicative scaling to match medians.
    AdditiveScaling,        ///< Additive offset + dispersion scaling.
    MultiplicativeScaling   ///< Multiplicative factor + dispersion scaling.
};

/**
 * @brief Output frame sizing policy.
 */
enum class FramingMode {
    Reference,      ///< Match the reference frame dimensions.
    Union,          ///< Enclose the union of all frame footprints (largest output).
    Intersection    ///< Use only the region common to all frames (smallest output).
};

/**
 * @brief Per-image weighting strategy during combination.
 */
enum class WeightingType {
    None = 0,       ///< Equal weight for every frame.
    StarCount,      ///< Weight proportional to detected star count.
    WeightedFWHM,   ///< Weight inversely proportional to FWHM (sharpness).
    Noise,          ///< Weight inversely proportional to noise level.
    Roundness,      ///< Weight proportional to stellar roundness.
    Quality,        ///< Weight by composite quality score.
    StackCount      ///< Weight by prior integration depth (master-of-masters).
};

/**
 * @brief Criterion for automatic frame selection (filtering).
 */
enum class ImageFilter {
    All = 0,            ///< Accept all frames.
    Selected,           ///< Honour the existing manual selection.
    BestFWHM,           ///< Best N% by FWHM.
    BestWeightedFWHM,   ///< Best N% by weighted FWHM.
    BestRoundness,      ///< Best N% by stellar roundness.
    BestBackground,     ///< Best N% by background level.
    BestStarCount,      ///< Best N% by detected star count.
    BestQuality         ///< Best N% by composite quality score.
};

/**
 * @brief Mode of the quality filter threshold.
 */
enum class FilterMode {
    Percentage = 0, ///< Keep the best N percent (0-100).
    KSigma          ///< Reject beyond k standard deviations from the mean.
};

/**
 * @brief Reconstruction kernel for drizzle integration.
 */
enum class DrizzleKernelType {
    Point,
    Square,
    Gaussian,
    Lanczos
};

// ============================================================================
// Error codes
// ============================================================================

/**
 * @brief Result codes returned by stacking operations.
 */
enum class StackResult {
    OK              =   0,  ///< Operation completed successfully.
    GenericError    =  -1,  ///< Unspecified error.
    SequenceError   =  -2,  ///< Error reading the image sequence.
    CancelledError  =  -9,  ///< Operation was cancelled by the user.
    AllocError      = -10   ///< Memory allocation failure.
};

// ============================================================================
// Structures
// ============================================================================

/**
 * @brief Pre-computed normalisation coefficients for a stacking sequence.
 *
 * Stores per-image (and per-channel) additive offsets, multiplicative
 * factors, dispersion scales, and optional gradient plane coefficients.
 */
struct NormCoefficients {
    std::vector<double> offset;     ///< Global additive offset per image.
    std::vector<double> mul;        ///< Global multiplicative factor per image.
    std::vector<double> scale;      ///< Global dispersion scale per image.

    /* Per-channel (up to 3 layers for RGB) coefficients. */
    std::vector<double> poffset[3];
    std::vector<double> pmul[3];
    std::vector<double> pscale[3];

    /* Gradient-plane coefficients (z = A*x + B*y + C) per channel. */
    std::vector<double> pgradA[3];
    std::vector<double> pgradB[3];
    std::vector<double> pgradC[3];

    /**
     * @brief Allocate and zero-initialise storage for @p nbImages frames.
     * @param nbImages Number of images in the sequence.
     * @param nbLayers Number of colour channels (1 or 3).
     */
    void init(int nbImages, int nbLayers)
    {
        offset.resize(nbImages, 0.0);
        mul.resize(nbImages, 1.0);
        scale.resize(nbImages, 1.0);

        for (int l = 0; l < nbLayers && l < 3; ++l) {
            poffset[l].resize(nbImages, 0.0);
            pmul[l].resize(nbImages, 1.0);
            pscale[l].resize(nbImages, 1.0);
            pgradA[l].resize(nbImages, 0.0);
            pgradB[l].resize(nbImages, 0.0);
            pgradC[l].resize(nbImages, 0.0);
        }
    }

    /**
     * @brief Store normalisation coefficients for one image and channel.
     * @param imgIndex    Image index.
     * @param layer       Channel index (-1 or >=3 sets the global coefficients).
     * @param slopeVal    Scale / slope value.
     * @param interceptVal Offset / intercept value.
     */
    void set(int imgIndex, int layer, double slopeVal, double interceptVal)
    {
        if (imgIndex < 0 || imgIndex >= static_cast<int>(scale.size())) {
            return;
        }
        if (layer < 0 || layer >= 3) {
            scale[imgIndex]  = slopeVal;
            offset[imgIndex] = interceptVal;
        } else {
            pscale[layer][imgIndex]  = slopeVal;
            poffset[layer][imgIndex] = interceptVal;
            /* Promote the green channel as the global representative. */
            if (layer == 1) {
                scale[imgIndex]  = slopeVal;
                offset[imgIndex] = interceptVal;
            }
        }
    }

    /** @brief Release all allocated storage. */
    void clear()
    {
        offset.clear();
        mul.clear();
        scale.clear();
        for (int i = 0; i < 3; ++i) {
            poffset[i].clear();
            pmul[i].clear();
            pscale[i].clear();
            pgradA[i].clear();
            pgradB[i].clear();
            pgradC[i].clear();
        }
    }
};

/**
 * @brief Per-frame geometric registration data.
 *
 * Stores the translation, rotation, scale, homography matrix and
 * optional SIP distortion polynomial for a single image relative to
 * the reference frame.
 */
struct RegistrationData {
    double shiftX   = 0.0;      ///< Horizontal translation (pixels).
    double shiftY   = 0.0;      ///< Vertical translation (pixels).
    double rotation = 0.0;      ///< Rotation angle (radians).

    /* Comet position (for comet-mode stacking). */
    double cometX   = 0.0;      ///< Comet X coordinate in this frame.
    double cometY   = 0.0;      ///< Comet Y coordinate in this frame.

    double scaleX   = 1.0;      ///< Horizontal scale factor.
    double scaleY   = 1.0;      ///< Vertical scale factor.
    bool   hasRegistration = false;

    /** @brief 3x3 homography matrix for the full projective transform. */
    double H[3][3] = {
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1}
    };

    /* SIP distortion polynomials (optional). */
    bool hasDistortion = false;
    int  sipOrder      = 0;
    std::vector<std::vector<double>> sipA;      ///< Forward X distortion coefficients.
    std::vector<std::vector<double>> sipB;      ///< Forward Y distortion coefficients.
    std::vector<std::vector<double>> sipAP;     ///< Reverse X distortion coefficients.
    std::vector<std::vector<double>> sipBP;     ///< Reverse Y distortion coefficients.

    /**
     * @brief Apply the homography to a 2-D point.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @return Transformed point.
     */
    QPointF transform(double x, double y) const
    {
        const double z     = H[2][0] * x + H[2][1] * y + H[2][2];
        const double scale = (std::abs(z) > 1e-9) ? 1.0 / z : 1.0;
        return QPointF(
            (H[0][0] * x + H[0][1] * y + H[0][2]) * scale,
            (H[1][0] * x + H[1][1] * y + H[1][2]) * scale);
    }

    /**
     * @brief Compute a unified drizzle matrix.
     * @param scale Original drizzle scale factor.
     * @param offsetX Framing/Crop X offset.
     * @param offsetY Framing/Crop Y offset.
     * @param outH Resulting 3x3 matrix.
     */
    void getDrizzleMatrix(double scale, double offsetX, double offsetY, double outH[3][3]) const
    {
        // Compute Drizzle Scaling + Shifting matrix S
        // Formula: X' = (X - off) * scale + 0.5 * (scale - 1)
        // This expands to: X' = X * scale + [0.5 * (scale - 1) - off * scale]
        
        double s = scale;
        double tx = 0.5 * (s - 1.0) - (offsetX * s);
        double ty = 0.5 * (s - 1.0) - (offsetY * s);

        double S[3][3] = {
            {s, 0, tx},
            {0, s, ty},
            {0, 0, 1.0}
        };

        // outH = S * H (Matrix multiplication: Scale/Shift * Registration)
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                outH[i][j] = 0;
                for (int k = 0; k < 3; ++k) {
                    outH[i][j] += S[i][k] * H[k][j];
                }
            }
        }
    }


    /**
     * @brief Test whether the registration is a pure translation.
     * @return true if no rotation, scaling or projective warp is present.
     */
    bool isShiftOnly() const
    {
        return hasRegistration &&
               rotation == 0.0 &&
               scaleX == 1.0 && scaleY == 1.0 &&
               H[0][0] == 1.0 && H[0][1] == 0.0 &&
               H[1][0] == 0.0 && H[1][1] == 1.0 &&
               H[2][0] == 0.0 && H[2][1] == 0.0 && H[2][2] == 1.0;
    }
};

/**
 * @brief Per-frame image quality metrics.
 */
struct ImageQuality {
    double fwhm         = 0.0;  ///< Average Full Width at Half Maximum (pixels).
    double weightedFwhm = 0.0;  ///< FWHM weighted (divided) by roundness.
    double roundness    = 0.0;  ///< Average stellar roundness (0 = elongated, 1 = circular).
    double background   = 0.0;  ///< Median background level.
    int    starCount    = 0;    ///< Number of detected stars.
    double quality      = 0.0;  ///< Composite quality score.
    double noise        = 0.0;  ///< Estimated noise sigma.
    bool   hasMetrics   = false;
};

/**
 * @brief Pixel rejection statistics for a completed stacking run.
 */
struct RejectionStats {
    int64_t totalPixels   = 0;
    int64_t lowRejected   = 0;
    int64_t highRejected  = 0;
    int64_t totalRejected = 0;

    /** @brief Percentage of pixels that were rejected. */
    double rejectionPercentage() const
    {
        return totalPixels > 0 ? (100.0 * totalRejected / totalPixels) : 0.0;
    }
};

/**
 * @brief Complete set of parameters controlling a stacking operation.
 */
struct StackingParams {

    // -- Combination method --------------------------------------------------
    Method              method        = Method::Mean;
    Rejection           rejection     = Rejection::Winsorized;
    NormalizationMethod normalization = NormalizationMethod::AdditiveScaling;
    WeightingType       weighting     = WeightingType::None;

    // -- Rejection parameters ------------------------------------------------
    float sigmaLow              = 3.0f;     ///< Low-side rejection sigma / percentile.
    float sigmaHigh             = 3.0f;     ///< High-side rejection sigma / percentile.
    bool  createRejectionMaps   = false;    ///< Generate per-pixel rejection maps.
    bool  mergeRejectionMaps    = false;    ///< Merge low/high maps into a single map.

    // -- Normalization options ------------------------------------------------
    bool forceNormalization     = false;    ///< Force re-computation of coefficients.
    bool fastNormalization      = false;    ///< Use the fast (lite) estimator.
    bool outputNormalization    = true;     ///< Normalize the output to [0, 1].
    bool equalizeRGB            = false;    ///< Equalize channel medians in the output.

    // -- Advanced options -----------------------------------------------------
    bool              maximizeFraming    = false;    ///< Extend canvas to include all frames.
    bool              upscaleAtStacking  = false;    ///< 2x up-scale during integration.
    bool              drizzle            = false;    ///< Enable drizzle integration.
    double            drizzleScale       = 2.0;      ///< Drizzle output scale factor.
    double            drizzlePixFrac     = 0.9;      ///< Drizzle drop-size fraction.
    DrizzleKernelType drizzleKernel      = DrizzleKernelType::Square;
    bool              drizzleFast        = false;    ///< Use point kernel for speed.
    bool              force32Bit         = false;    ///< Force 32-bit float output.
    int               featherDistance    = 0;        ///< Edge-feathering width (pixels).
    bool              overlapNormalization = false;   ///< Normalise using overlap regions only.

    // -- Reference frame ------------------------------------------------------
    int refImageIndex = 0;                  ///< Index of the reference image.

    // -- Comet mode -----------------------------------------------------------
    bool    useCometMode = false;
    double  cometVx      = 0.0;             ///< Comet velocity X (pixels / hour).
    double  cometVy      = 0.0;             ///< Comet velocity Y (pixels / hour).
    QString refDate;                        ///< ISO-8601 date string for zero-shift reference.

    // -- Cosmetic correction --------------------------------------------------
    bool    useCosmetic       = false;      ///< Enable hot/cold pixel correction.
    float   cosmeticHotSigma  = 3.0f;       ///< Sigma threshold for hot pixels.
    float   cosmeticColdSigma = 3.0f;       ///< Sigma threshold for cold pixels.
    bool    cosmeticIsCFA     = true;        ///< Preserve Bayer pattern during correction.
    QString cosmeticMapFile;                ///< Optional external bad-pixel map.

    // -- Image filtering ------------------------------------------------------
    ImageFilter filter          = ImageFilter::Selected;
    FilterMode  filterMode      = FilterMode::Percentage;
    double      filterParameter = 100.0;    ///< Percentage (0-100) or k-sigma value.

    // -- Output ---------------------------------------------------------------
    QString outputFilename;
    bool    overwriteOutput = false;

    // -- Registration layer ---------------------------------------------------
    int registrationLayer = 0;              ///< Channel index carrying registration data.

    // -- On-the-fly debayering ------------------------------------------------
    bool                              debayer       = false;
    Preprocessing::BayerPattern       bayerPattern  = Preprocessing::BayerPattern::None;
    Preprocessing::DebayerAlgorithm   debayerMethod = Preprocessing::DebayerAlgorithm::VNG;

    // -- Convenience predicates -----------------------------------------------

    /** @brief True when the current method + rejection settings imply pixel rejection. */
    bool hasRejection() const
    {
        return method == Method::Mean && rejection != Rejection::None;
    }

    /** @brief True when the current method + normalisation settings imply normalisation. */
    bool hasNormalization() const
    {
        return (method == Method::Mean || method == Method::Median) &&
               normalization != NormalizationMethod::None;
    }
};

// ============================================================================
// Callback typedefs
// ============================================================================

/**
 * @brief Progress reporting callback.
 *
 * @param message  Human-readable status string.
 * @param progress Fractional progress in [0.0, 1.0], or -1 for indeterminate.
 */
using ProgressCallback = std::function<void(const QString&, double)>;

/**
 * @brief Cancellation polling callback.
 *
 * The stacking engine calls this periodically; returning true requests
 * cancellation of the current operation.
 */
using CancelCheck = std::function<bool()>;

/**
 * @brief Log-message callback.
 *
 * @param message Human-readable log text.
 * @param colour  Optional colour hint for the UI (e.g. "red", "green").
 */
using LogCallback = std::function<void(const QString&, const QString&)>;

// ============================================================================
// Compile-time constants
// ============================================================================

/** @brief Overlap normalisation is impractical above this image count. */
constexpr int MAX_IMAGES_FOR_OVERLAP = 30;

/** @brief Minimum stack depth required for rejection algorithms to operate. */
constexpr int MIN_IMAGES_FOR_REJECTION = 3;

/** @brief Upper limit on parallel block count (memory-management bound). */
constexpr int MAX_PARALLEL_BLOCKS = 256;

// ============================================================================
// String conversion helpers
// ============================================================================

/** @brief Human-readable name for a stacking Method. */
inline QString methodToString(Method m)
{
    switch (m) {
    case Method::Sum:    return QStringLiteral("Sum");
    case Method::Mean:   return QStringLiteral("Mean");
    case Method::Median: return QStringLiteral("Median");
    case Method::Max:    return QStringLiteral("Maximum");
    case Method::Min:    return QStringLiteral("Minimum");
    default:             return QStringLiteral("Unknown");
    }
}

/** @brief Human-readable name for a Rejection algorithm. */
inline QString rejectionToString(Rejection r)
{
    switch (r) {
    case Rejection::None:           return QStringLiteral("None");
    case Rejection::Percentile:     return QStringLiteral("Percentile Clipping");
    case Rejection::Sigma:          return QStringLiteral("Sigma Clipping");
    case Rejection::MAD:            return QStringLiteral("MAD Clipping");
    case Rejection::SigmaMedian:    return QStringLiteral("Sigma-Median Clipping");
    case Rejection::Winsorized:     return QStringLiteral("Winsorized Sigma");
    case Rejection::LinearFit:      return QStringLiteral("Linear Fit Clipping");
    case Rejection::GESDT:          return QStringLiteral("Generalized ESD Test");
    case Rejection::Biweight:       return QStringLiteral("Biweight Estimator");
    case Rejection::ModifiedZScore: return QStringLiteral("Modified Z-Score");
    default:                        return QStringLiteral("Unknown");
    }
}

/** @brief Human-readable name for a NormalizationMethod. */
inline QString normalizationToString(NormalizationMethod n)
{
    switch (n) {
    case NormalizationMethod::None:                   return QStringLiteral("None");
    case NormalizationMethod::Additive:               return QStringLiteral("Additive");
    case NormalizationMethod::Multiplicative:         return QStringLiteral("Multiplicative");
    case NormalizationMethod::AdditiveScaling:        return QStringLiteral("Additive + Scaling");
    case NormalizationMethod::MultiplicativeScaling:  return QStringLiteral("Multiplicative + Scaling");
    default:                                          return QStringLiteral("Unknown");
    }
}

/** @brief Human-readable name for a WeightingType. */
inline QString weightingToString(WeightingType w)
{
    switch (w) {
    case WeightingType::None:         return QStringLiteral("None");
    case WeightingType::StarCount:    return QStringLiteral("Star Count");
    case WeightingType::WeightedFWHM: return QStringLiteral("Weighted FWHM");
    case WeightingType::Noise:        return QStringLiteral("Noise");
    case WeightingType::Roundness:    return QStringLiteral("Roundness");
    case WeightingType::Quality:      return QStringLiteral("Quality");
    case WeightingType::StackCount:   return QStringLiteral("Stack Count");
    default:                          return QStringLiteral("Unknown");
    }
}

} // namespace Stacking

#endif // STACKING_TYPES_H
