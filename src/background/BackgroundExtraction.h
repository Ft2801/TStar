#ifndef BACKGROUNDEXTRACTION_H
#define BACKGROUNDEXTRACTION_H

// ============================================================================
// BackgroundExtraction.h
// Background model extraction via polynomial or RBF surface fitting.
// Supports adaptive sample grid generation, per-channel model fitting,
// and subtraction/division-based correction of large-scale gradients.
// ============================================================================

#include "../ImageBuffer.h"

#include <vector>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>

namespace Background {

// ----------------------------------------------------------------------------
// Enumerations
// ----------------------------------------------------------------------------

/** Polynomial surface order for background fitting. */
enum class PolyOrder {
    Order1 = 1,
    Order2 = 2,
    Order3 = 3,
    Order4 = 4
};

/** Method used to correct the background gradient from the image. */
enum class CorrectionType {
    Subtraction,
    Division
};

/** Surface interpolation method for background modeling. */
enum class FittingMethod {
    Polynomial,
    RBF
};

// ----------------------------------------------------------------------------
// Sample Point
// ----------------------------------------------------------------------------

/** Represents a single grid sample with spatial position and per-channel median. */
struct Sample {
    float x, y;
    std::vector<float> median;  ///< Per-channel median intensities
    bool  valid = true;
};

// ----------------------------------------------------------------------------
// BackgroundExtractor
// ----------------------------------------------------------------------------

/**
 * @brief Extracts and removes large-scale background gradients from images.
 *
 * Workflow:
 *   1. generateGrid()   -- create an adaptive sample grid over the image
 *   2. computeModel()   -- fit a surface model (polynomial or RBF) per channel
 *   3. apply()          -- subtract or divide the model from the source image
 */
class BackgroundExtractor {
public:
    BackgroundExtractor();
    ~BackgroundExtractor();

    // Disable copying/assignment unless I want to implement deep cloning of GSL vectors.
    // Given the usage in scripts, preventing unintended shallow copies is enough, 
    // or I can implement a proper deep copy. Let's do a deep copy to be robust.
    BackgroundExtractor(const BackgroundExtractor& other);
    BackgroundExtractor& operator=(const BackgroundExtractor& other);

    /**
     * @brief Configure the fitting parameters.
     * @param degree     Polynomial degree [1..4], clamped internally.
     * @param tolerance  Outlier rejection tolerance in MAD units.
     * @param smoothing  RBF smoothing factor [0..1].
     */
    void setParameters(int degree, float tolerance, float smoothing = 0.5f);

    /**
     * @brief Generate a uniform sample grid and reject outlier samples.
     * @param img            Source image to sample from.
     * @param samplesPerLine Number of sample boxes along the horizontal axis.
     */
    void generateGrid(const ImageBuffer& img, int samplesPerLine = 20);

    /** Fit the surface model to the current sample grid for all channels. */
    bool computeModel();

    /**
     * @brief Apply the computed background model to produce a corrected image.
     * @param src  Source image.
     * @param dst  Destination image (output).
     * @param type Subtraction or division correction.
     */
    bool apply(const ImageBuffer& src, ImageBuffer& dst, CorrectionType type);

private:
    // -- Automatic sample generation ------------------------------------------
    void generateSamplesAuto(const ImageBuffer& img);

    // -- Model fitting --------------------------------------------------------
    bool fitPolynomial(int channel);
    bool fitRBF(int channel);

    // -- Model evaluation -----------------------------------------------------
    float evaluatePolynomial(float x, float y, const gsl_vector* coeffs);
    float evaluateRBF(float x, float y, int channel);

    // -- Utility --------------------------------------------------------------
    std::vector<float> computeLuminance(const ImageBuffer& img);

    // -- Parameters -----------------------------------------------------------
    int   m_degree    = 2;
    float m_tolerance = 3.0f;
    float m_smoothing = 0.5f;

    FittingMethod m_method = FittingMethod::Polynomial;

    // -- Sample data ----------------------------------------------------------
    std::vector<Sample> m_samples;
    int m_width    = 0;
    int m_height   = 0;
    int m_channels = 0;

    // -- Per-channel fitted model data ----------------------------------------
    struct ChannelModel {
        gsl_vector*         polyCoeffs = nullptr;
        std::vector<float>  rbfWeights;
        std::vector<Sample> rbfCenters;
    };

    std::vector<ChannelModel> m_models;

    /** Release all GSL resources held by channel models. */
    void clearModels();
};

} // namespace Background

#endif // BACKGROUNDEXTRACTION_H