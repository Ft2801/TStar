#ifndef STACKING_TYPES_H
#define STACKING_TYPES_H

#include "../preprocessing/PreprocessingTypes.h"
#include <QPointF>

namespace Stacking {


//=============================================================================
// ENUMERATIONS
//=============================================================================

enum class Method {
    Sum = 0,      ///< Sum all pixel values and normalize
    Mean,         ///< Average with optional rejection
    Median,       ///< Take median value (robust against outliers)
    Max,          ///< Take maximum value (star trails, etc.)
    Min           ///< Take minimum value (remove bright artifacts)
};

/**
 * @brief Pixel rejection algorithm enumeration
 * 
 * These algorithms identify and exclude outlier pixels before
 * computing the final stacked value. Only applicable to Mean stacking.
 */
enum class Rejection {
    None = 0,        ///< No rejection - use all pixels
    Percentile,      ///< Reject based on percentage from median
    Sigma,           ///< Standard sigma clipping
    MAD,             ///< Median Absolute Deviation clipping
    SigmaMedian,     ///< Replace rejected with median (not remove)
    Winsorized,      ///< Winsorized sigma clipping (more robust)
    LinearFit,       ///< Linear fit clipping
    GESDT            ///< Generalized Extreme Studentized Deviate Test
};

/**
 * @brief Normalization type enumeration
 * 
 * Defines how images are normalized before stacking to account
 * for differences in background level and scale.
 */
/**
 * @brief Normalization type enumeration
 * 
 * Defines how images are normalized before stacking to account
 * for differences in background level and scale.
 */
enum class NormalizationMethod {
    None = 0,              ///< No normalization
    Additive,              ///< Additive normalization (matching medians)
    Multiplicative,        ///< Multiplicative normalization (matching medians)
    AdditiveScaling,       ///< Additive + Scaling (matching median + dispersion)
    MultiplicativeScaling, ///< Multiplicative + Scaling
    GradientRemoval        ///< Polynomial Gradient removal
};

/**
 * @brief Framing mode enumeration
 * 
 * Defines how the output frame size is determined.
 */
enum class FramingMode {
    Reference,    ///< Output size matches Reference frame
    Union,        ///< Output is union of all frames (Max size)
    Intersection  ///< Output is intersection of all frames (Min size)
};

/**
 * @brief Image weighting type enumeration
 * 
 * Defines how individual images are weighted during stacking.
 */
enum class WeightingType {
    None = 0,       ///< Equal weight for all images
    StarCount,      ///< Weight by number of detected stars
    WeightedFWHM,   ///< Weight by inverse of FWHM * roundness
    Noise,          ///< Weight by inverse of noise level
    Roundness,      ///< Weight by star roundness
    Quality,        ///< Weight by computed quality metric
    StackCount      ///< Weight by prior stack count (for stacking stacks)
};

/**
 * @brief Image selection filter type
 * 
 * Defines criteria for selecting which images to include in stacking.
 */
enum class ImageFilter {
    All = 0,           ///< Use all images
    Selected,          ///< Use only manually selected images
    BestFWHM,          ///< Best N% by FWHM
    BestWeightedFWHM,  ///< Best N% by weighted FWHM
    BestRoundness,     ///< Best N% by roundness
    BestBackground,    ///< Best N% by background level
    BestStarCount,     ///< Best N% by star count
    BestQuality        ///< Best N% by overall quality score
};

/**
 * @brief Filter mode (percentage or k-sigma)
 */
enum class FilterMode {
    Percentage = 0,  ///< Filter by percentage (e.g., best 90%)
    KSigma           ///< Filter by k-sigma clipping
};

/**
 * @brief Pixel reconstruction kernel for Drizzle
 */
enum class DrizzleKernelType {
    Point,
    Square,
    Gaussian,
    Lanczos
};

//=============================================================================
// ERROR CODES
//=============================================================================

/**
 * @brief Stacking operation result codes
 */
enum class StackResult {
    OK = 0,              ///< Success
    GenericError = -1,   ///< Generic error
    SequenceError = -2,  ///< Error reading sequence
    CancelledError = -9, ///< Operation cancelled by user
    AllocError = -10     ///< Memory allocation error
};

//=============================================================================
// STRUCTURES
//=============================================================================

/**
 * @brief Normalization coefficients for a sequence
 * 
 * Contains pre-computed normalization factors for each image and channel.
 */
struct NormCoefficients {
    std::vector<double> offset;   ///< Additive offset per image
    std::vector<double> mul;      ///< Multiplicative factor per image
    std::vector<double> scale;    ///< Scale factor per image
    
    // Per-layer coefficients (for RGB)
    std::vector<double> poffset[3];
    std::vector<double> pmul[3];
    std::vector<double> pscale[3];
    
    // Gradient coefficients (Plane: z = Ax + By + C)
    std::vector<double> pgradA[3];
    std::vector<double> pgradB[3];
    std::vector<double> pgradC[3];
    
    /**
     * @brief Initialize coefficients for N images
     */
    void init(int nbImages, int nbLayers) {
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

    void set(int imgIndex, int layer, double slopeVal, double interceptVal) {
        if (imgIndex < 0 || imgIndex >= (int)scale.size()) return;
        if (layer < 0 || layer >= 3) {
            scale[imgIndex] = slopeVal;
            offset[imgIndex] = interceptVal;
        } else {
            pscale[layer][imgIndex] = slopeVal;
            poffset[layer][imgIndex] = interceptVal;
            if(layer == 1) {
                 scale[imgIndex] = slopeVal;
                 offset[imgIndex] = interceptVal;
            }
        }
    }
    
    /**
     * @brief Clear all coefficients
     */
    void clear() {
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
 * @brief Registration data for a single image
 */
struct RegistrationData {
    double shiftX = 0.0;        ///< X translation
    double shiftY = 0.0;        ///< Y translation
    double rotation = 0.0;      ///< Rotation angle (radians)
    
    // Comet Data (for comet stacking)
    double cometX = 0.0;        ///< Comet X position in this image
    double cometY = 0.0;        ///< Comet Y position in this image
    
    double scaleX = 1.0;        ///< X scale factor
    double scaleY = 1.0;        ///< Y scale factor
    bool hasRegistration = false;
    
    // Homography matrix (3x3) for full transformation
    double H[3][3] = {
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1}
    };

    // SIP Distortion Coefficients
    bool hasDistortion = false;
    int sipOrder = 0;
    std::vector<std::vector<double>> sipA;  // Forward X distortion
    std::vector<std::vector<double>> sipB;  // Forward Y distortion
    std::vector<std::vector<double>> sipAP; // Reverse X distortion
    std::vector<std::vector<double>> sipBP; // Reverse Y distortion
    
    /**
     * @brief Transform a point using the homography matrix
     */
    QPointF transform(double x, double y) const {
        double z = H[2][0] * x + H[2][1] * y + H[2][2];
        double scale = (std::abs(z) > 1e-9) ? 1.0 / z : 1.0;
        return QPointF(
            (H[0][0] * x + H[0][1] * y + H[0][2]) * scale,
            (H[1][0] * x + H[1][1] * y + H[1][2]) * scale
        );
    }

    /**
     * @brief Check if registration is simple shift only
     */
    bool isShiftOnly() const {
        return hasRegistration && 
               rotation == 0.0 && 
               scaleX == 1.0 && scaleY == 1.0 &&
               H[0][0] == 1.0 && H[0][1] == 0.0 &&
               H[1][0] == 0.0 && H[1][1] == 1.0 &&
               H[2][0] == 0.0 && H[2][1] == 0.0 && H[2][2] == 1.0;
    }
};

/**
 * @brief Image quality metrics
 */
struct ImageQuality {
    double fwhm = 0.0;           ///< Full Width at Half Maximum
    double weightedFwhm = 0.0;   ///< FWHM weighted by roundness
    double roundness = 0.0;      ///< Star roundness (0-1)
    double background = 0.0;     ///< Background level
    int starCount = 0;           ///< Number of detected stars
    double quality = 0.0;        ///< Overall quality score
    double noise = 0.0;          ///< Noise estimate
    bool hasMetrics = false;
};

/**
 * @brief Rejection statistics for a stacking operation
 */
struct RejectionStats {
    int64_t totalPixels = 0;
    int64_t lowRejected = 0;
    int64_t highRejected = 0;
    int64_t totalRejected = 0;
    
    double rejectionPercentage() const {
        return totalPixels > 0 ? (100.0 * totalRejected / totalPixels) : 0.0;
    }
};

/**
 * @brief Main stacking parameters structure
 * 
 * Contains all configuration options for a stacking operation.
 */
struct StackingParams {
    // Method selection
    Method method = Method::Mean;
    Rejection rejection = Rejection::Winsorized;
    NormalizationMethod normalization = NormalizationMethod::AdditiveScaling;
    WeightingType weighting = WeightingType::None;
    
    // Rejection parameters
    float sigmaLow = 3.0f;       ///< Low rejection sigma/percentile
    float sigmaHigh = 3.0f;      ///< High rejection sigma/percentile
    bool createRejectionMaps = false;  ///< Generate rejection maps
    bool mergeRejectionMaps = false;   ///< Merge low/high maps
    
    // Normalization options
    bool forceNormalization = false;   ///< Force recalculation
    bool fastNormalization = false;     ///< Use fast (lite) mode
    bool outputNormalization = true;   ///< Normalize output to [0,1]
    bool equalizeRGB = false;           ///< Equalize RGB channels
    
    // Advanced options
    bool maximizeFraming = false;      ///< Extend to include all images
    bool upscaleAtStacking = false;    ///< 2x upscale during stacking
    bool drizzle = false;              ///< Enable Drizzle stacking
    double drizzleScale = 2.0;         ///< Drizzle output scale
    double drizzlePixFrac = 0.9;       ///< Drizzle pixel fraction
    DrizzleKernelType drizzleKernel = DrizzleKernelType::Square; ///< Drizzle kernel type
    bool force32Bit = false;           ///< Force 32-bit float output
    int featherDistance = 0;           ///< Feather pixels for blending
    bool overlapNormalization = false; ///< Normalize on overlap regions
    
    // Reference image
    int refImageIndex = 0;             ///< Index of reference image

    // Comet Registration
    bool useCometMode = false;
    double cometVx = 0.0; // Pixels per hour
    double cometVy = 0.0;
    QString refDate; // Reference date (ISO string) for zero shift

    // Cosmetic Correction
    bool useCosmetic = false;          ///< Enable Cosmetic Correction
    float cosmeticHotSigma = 3.0f;     ///< Sigma for Hot Pixels
    float cosmeticColdSigma = 3.0f;    ///< Sigma for Cold Pixels
    bool cosmeticIsCFA = true;         ///< Treat as CFA (preserve Bayer pattern)
    QString cosmeticMapFile;           ///< Path to Bad Pixel Map (optional)
    
    // Image filtering
    ImageFilter filter = ImageFilter::Selected;
    FilterMode filterMode = FilterMode::Percentage;
    double filterParameter = 100.0;    ///< Percentage or k-sigma value
    
    // Output
    QString outputFilename;
    bool overwriteOutput = false;
    
    // Registration layer (for multi-channel sequence)
    int registrationLayer = 0;  ///< Which layer has registration data
    
    // Debayering (On-the-fly)
    // Note: Depends on Preprocessing types
    bool debayer = false;
    Preprocessing::BayerPattern bayerPattern = Preprocessing::BayerPattern::None;
    Preprocessing::DebayerAlgorithm debayerMethod = Preprocessing::DebayerAlgorithm::VNG;
    
    /**
     * @brief Check if rejection is applicable for current method
     */
    bool hasRejection() const {
        return method == Method::Mean && rejection != Rejection::None;
    }
    
    /**
     * @brief Check if normalization is applicable for current method
     */
    bool hasNormalization() const {
        return (method == Method::Mean || method == Method::Median) &&
               normalization != NormalizationMethod::None;
    }
};

/**
 * @brief Progress callback function type
 * 
 * Parameters: message, progress (0.0-1.0, or -1 for indeterminate)
 */
using ProgressCallback = std::function<void(const QString&, double)>;

/**
 * @brief Cancellation check function type
 * 
 * Returns true if operation should be cancelled
 */
using CancelCheck = std::function<bool()>;

/**
 * @brief Log callback function type
 * 
 * Parameters: message, color (optional)
 */
using LogCallback = std::function<void(const QString&, const QString&)>;

//=============================================================================
// CONSTANTS
//=============================================================================

/**
 * @brief Maximum number of images for overlap normalization warning
 */
constexpr int MAX_IMAGES_FOR_OVERLAP = 30;

/**
 * @brief Minimum images required for rejection algorithms
 */
constexpr int MIN_IMAGES_FOR_REJECTION = 3;

/**
 * @brief Maximum parallel blocks for memory management
 */
constexpr int MAX_PARALLEL_BLOCKS = 256;

//=============================================================================
// HELPER FUNCTIONS
//=============================================================================

/**
 * @brief Convert Method enum to string
 */
inline QString methodToString(Method m) {
    switch (m) {
        case Method::Sum:    return QStringLiteral("Sum");
        case Method::Mean:   return QStringLiteral("Mean");
        case Method::Median: return QStringLiteral("Median");
        case Method::Max:    return QStringLiteral("Maximum");
        case Method::Min:    return QStringLiteral("Minimum");
        default:             return QStringLiteral("Unknown");
    }
}

/**
 * @brief Convert Rejection enum to string
 */
inline QString rejectionToString(Rejection r) {
    switch (r) {
        case Rejection::None:        return QStringLiteral("None");
        case Rejection::Percentile:  return QStringLiteral("Percentile Clipping");
        case Rejection::Sigma:       return QStringLiteral("Sigma Clipping");
        case Rejection::MAD:         return QStringLiteral("MAD Clipping");
        case Rejection::SigmaMedian: return QStringLiteral("Sigma-Median Clipping");
        case Rejection::Winsorized:  return QStringLiteral("Winsorized Sigma");
        case Rejection::LinearFit:   return QStringLiteral("Linear Fit Clipping");
        case Rejection::GESDT:       return QStringLiteral("Generalized ESD Test");
        default:                     return QStringLiteral("Unknown");
    }
}

/**
 * @brief Convert Normalization enum to string
 */
inline QString normalizationToString(NormalizationMethod n) {
    switch (n) {
        case NormalizationMethod::None:                  return QStringLiteral("None");
        case NormalizationMethod::Additive:              return QStringLiteral("Additive");
        case NormalizationMethod::Multiplicative:        return QStringLiteral("Multiplicative");
        case NormalizationMethod::AdditiveScaling:       return QStringLiteral("Additive + Scaling");
        case NormalizationMethod::MultiplicativeScaling: return QStringLiteral("Multiplicative + Scaling");
        default:                                         return QStringLiteral("Unknown");
    }
}

/**
 * @brief Convert Weighting enum to string
 */
inline QString weightingToString(WeightingType w) {
    switch (w) {
        case WeightingType::None:        return QStringLiteral("None");
        case WeightingType::StarCount:   return QStringLiteral("Star Count");
        case WeightingType::WeightedFWHM:return QStringLiteral("Weighted FWHM");
        case WeightingType::Noise:       return QStringLiteral("Noise");
        case WeightingType::Roundness:   return QStringLiteral("Roundness");
        case WeightingType::Quality:     return QStringLiteral("Quality");
        case WeightingType::StackCount:  return QStringLiteral("Stack Count");
        default:                     return QStringLiteral("Unknown");
    }
}

} // namespace Stacking

#endif // STACKING_TYPES_H
