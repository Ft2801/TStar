// ============================================================================
// BackgroundExtraction.cpp
// Implementation of polynomial and RBF background surface extraction.
// ============================================================================

#include "BackgroundExtraction.h"
#include "../core/RobustStatistics.h"
#include "../stacking/Statistics.h"

#include <gsl/gsl_statistics_float.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include <opencv2/opencv.hpp>

#include <cmath>
#include <algorithm>
#include <omp.h>
#include <QDebug>

namespace Background {

// ============================================================================
// Construction / Destruction
// ============================================================================

BackgroundExtractor::BackgroundExtractor()
{
}

// Deep copy for GSL vectors
BackgroundExtractor::BackgroundExtractor(const BackgroundExtractor& other)
    : m_degree(other.m_degree)
    , m_tolerance(other.m_tolerance)
    , m_smoothing(other.m_smoothing)
    , m_method(other.m_method)
    , m_samples(other.m_samples)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_channels(other.m_channels)
{
    m_models.resize(other.m_models.size());
    for (size_t i = 0; i < other.m_models.size(); ++i) {
        m_models[i].rbfWeights = other.m_models[i].rbfWeights;
        m_models[i].rbfCenters = other.m_models[i].rbfCenters;
        if (other.m_models[i].polyCoeffs) {
            m_models[i].polyCoeffs = gsl_vector_alloc(other.m_models[i].polyCoeffs->size);
            gsl_vector_memcpy(m_models[i].polyCoeffs, other.m_models[i].polyCoeffs);
        } else {
            m_models[i].polyCoeffs = nullptr;
        }
    }
}

BackgroundExtractor& BackgroundExtractor::operator=(const BackgroundExtractor& other)
{
    if (this == &other) return *this;

    clearModels();
    
    m_degree    = other.m_degree;
    m_tolerance = other.m_tolerance;
    m_smoothing = other.m_smoothing;
    m_method    = other.m_method;
    m_samples   = other.m_samples;
    m_width     = other.m_width;
    m_height    = other.m_height;
    m_channels  = other.m_channels;

    m_models.resize(other.m_models.size());
    for (size_t i = 0; i < other.m_models.size(); ++i) {
        m_models[i].rbfWeights = other.m_models[i].rbfWeights;
        m_models[i].rbfCenters = other.m_models[i].rbfCenters;
        if (other.m_models[i].polyCoeffs) {
            m_models[i].polyCoeffs = gsl_vector_alloc(other.m_models[i].polyCoeffs->size);
            gsl_vector_memcpy(m_models[i].polyCoeffs, other.m_models[i].polyCoeffs);
        } else {
            m_models[i].polyCoeffs = nullptr;
        }
    }

    return *this;
}

BackgroundExtractor::~BackgroundExtractor()
{
    clearModels();
}

// ============================================================================
// Model Lifecycle
// ============================================================================

void BackgroundExtractor::clearModels()
{
    for (auto& model : m_models) {
        if (model.polyCoeffs) {
            gsl_vector_free(model.polyCoeffs);
        }
    }
    m_models.clear();
}

// ============================================================================
// Parameter Configuration
// ============================================================================

void BackgroundExtractor::setParameters(int degree, float tolerance, float smoothing)
{
    m_degree    = std::clamp(degree, 1, 4);
    m_tolerance = tolerance;
    m_smoothing = smoothing;
}

// ============================================================================
// Luminance Computation
// ============================================================================

/**
 * @brief Compute per-pixel luminance using Rec. 709 coefficients.
 * @param img Source image buffer.
 * @return Flat vector of luminance values (one per pixel).
 */
std::vector<float> BackgroundExtractor::computeLuminance(const ImageBuffer& img)
{
    const int w  = img.width();
    const int h  = img.height();
    const int ch = img.channels();

    std::vector<float> luma(w * h);
    const float* data = img.data().data();

    #pragma omp parallel for
    for (int i = 0; i < w * h; ++i) {
        if (ch >= 3) {
            // ITU-R BT.709 luminance weights
            luma[i] = 0.2126f * data[i * ch]
                    + 0.7152f * data[i * ch + 1]
                    + 0.0722f * data[i * ch + 2];
        } else {
            luma[i] = data[i * ch];
        }
    }

    return luma;
}

// ============================================================================
// Sample Grid Generation
// ============================================================================

/**
 * @brief Generate a uniform grid of sample patches and reject outliers.
 *
 * The grid is spaced evenly across the image. Each sample patch computes the
 * per-channel median. Samples whose luminance deviates beyond (median + MAD *
 * tolerance) are excluded to avoid fitting bright objects into the background.
 */
void BackgroundExtractor::generateGrid(const ImageBuffer& img, int samplesPerLine)
{
    m_width    = img.width();
    m_height   = img.height();
    m_channels = img.channels();
    m_samples.clear();

    // -- Step 1: Compute global luminance median ------------------------------
    std::vector<float> luma = computeLuminance(img);
    std::vector<float> lumaCopy = luma;
    float median = Stacking::Statistics::quickMedian(lumaCopy);
    if (median <= 0.0f) {
        median = 1e-6f;
    }

    // -- Step 2: Determine grid layout ----------------------------------------
    const int size   = 25;           // Sample patch side length in pixels
    const int radius = size / 2;
    const int nx     = samplesPerLine;

    const int   boxesWidth = nx * size + 2;
    const float spacing    = static_cast<float>(m_width - boxesWidth)
                           / static_cast<float>(std::max(1, nx - 1));

    // Compute vertical grid count
    int ny = 1;
    while (ny * size + std::round((ny - 1) * spacing) < (m_height - 2)) {
        ny++;
    }
    ny--;
    if (ny <= 0) {
        ny = 1;
    }

    const int totalGridHeight = ny * size + (ny - 1) * static_cast<int>(std::round(spacing));
    const int yOffset         = (m_height - 2 - totalGridHeight) / 2 + 1;

    // -- Step 3: Collect candidate samples ------------------------------------
    std::vector<Sample> candidates;
    std::vector<float>  diffs;

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            const int cx = static_cast<int>(std::round(i * (spacing + size))) + radius + 1;
            const int cy = yOffset + static_cast<int>(std::round(j * (spacing + size))) + radius;

            // Boundary check
            if (cx < radius || cx >= m_width - radius ||
                cy < radius || cy >= m_height - radius) {
                continue;
            }

            Sample s;
            s.x = static_cast<float>(cx);
            s.y = static_cast<float>(cy);
            s.median.resize(m_channels);

            // Compute per-channel median within the patch
            for (int c = 0; c < m_channels; ++c) {
                std::vector<float> patch;
                patch.reserve(size * size);

                for (int py = cy - radius; py <= cy + radius; ++py) {
                    for (int px = cx - radius; px <= cx + radius; ++px) {
                        patch.push_back(
                            img.data()[(py * m_width + px) * m_channels + c]);
                    }
                }

                s.median[c] = Stacking::Statistics::quickMedian(patch);
            }

            // Compute luminance of the sample
            float sampleLuma = 0.0f;
            if (m_channels >= 3) {
                sampleLuma = 0.2126f * s.median[0]
                           + 0.7152f * s.median[1]
                           + 0.0722f * s.median[2];
            } else {
                sampleLuma = s.median[0];
            }

            diffs.push_back(std::abs(sampleLuma - median));
            candidates.push_back(s);
        }
    }

    // -- Step 4: Reject outlier samples using robust threshold -----------------
    const float mad       = RobustStatistics::getMedian(diffs);
    const float threshold = median + mad * m_tolerance;

    for (auto& s : candidates) {
        float sampleLuma = (m_channels >= 3)
            ? (0.2126f * s.median[0] + 0.7152f * s.median[1] + 0.0722f * s.median[2])
            : s.median[0];

        if (sampleLuma > 0.0f && sampleLuma < threshold) {
            m_samples.push_back(s);
        }
    }
}

// ============================================================================
// Polynomial Surface Fitting
// ============================================================================

/**
 * @brief Fit a 2D polynomial surface to the sample grid for a single channel.
 *
 * Constructs the Vandermonde-like design matrix for the requested polynomial
 * degree and solves via GSL weighted linear least squares.
 *
 * @param channel Channel index to fit.
 * @return true if the fit succeeded.
 */
bool BackgroundExtractor::fitPolynomial(int channel)
{
    const int n = static_cast<int>(m_samples.size());

    // Number of polynomial coefficients for the given degree
    int p = 0;
    switch (m_degree) {
        case 1: p = 3;  break;
        case 2: p = 6;  break;
        case 3: p = 10; break;
        case 4: p = 15; break;
    }

    if (n < p) {
        return false;
    }

    // Allocate GSL structures
    gsl_matrix* J   = gsl_matrix_alloc(n, p);
    gsl_vector* y   = gsl_vector_alloc(n);
    gsl_vector* w   = gsl_vector_alloc(n);
    gsl_vector* c   = gsl_vector_alloc(p);
    gsl_matrix* cov = gsl_matrix_alloc(p, p);

    // Populate design matrix and observation vector
    for (int i = 0; i < n; ++i) {
        const double col = m_samples[i].x;
        const double row = m_samples[i].y;
        const double val = m_samples[i].median[channel];

        // Degree 1: constant, x, y
        gsl_matrix_set(J, i, 0, 1.0);
        gsl_matrix_set(J, i, 1, col);
        gsl_matrix_set(J, i, 2, row);

        // Degree 2: x^2, xy, y^2
        if (m_degree >= 2) {
            gsl_matrix_set(J, i, 3, col * col);
            gsl_matrix_set(J, i, 4, col * row);
            gsl_matrix_set(J, i, 5, row * row);
        }

        // Degree 3: x^3, x^2*y, x*y^2, y^3
        if (m_degree >= 3) {
            gsl_matrix_set(J, i, 6, col * col * col);
            gsl_matrix_set(J, i, 7, col * col * row);
            gsl_matrix_set(J, i, 8, col * row * row);
            gsl_matrix_set(J, i, 9, row * row * row);
        }

        // Degree 4: x^4, x^3*y, x^2*y^2, x*y^3, y^4
        if (m_degree >= 4) {
            gsl_matrix_set(J, i, 10, col * col * col * col);
            gsl_matrix_set(J, i, 11, col * col * col * row);
            gsl_matrix_set(J, i, 12, col * col * row * row);
            gsl_matrix_set(J, i, 13, col * row * row * row);
            gsl_matrix_set(J, i, 14, row * row * row * row);
        }

        gsl_vector_set(y, i, val);
        gsl_vector_set(w, i, 1.0);
    }

    // Solve weighted linear least squares
    gsl_multifit_linear_workspace* work = gsl_multifit_linear_alloc(n, p);
    double chisq;
    int status = gsl_multifit_wlinear(J, w, y, c, cov, &chisq, work);
    gsl_multifit_linear_free(work);

    if (status == GSL_SUCCESS) {
        m_models[channel].polyCoeffs = c;
    } else {
        gsl_vector_free(c);
    }

    // Clean up
    gsl_matrix_free(J);
    gsl_vector_free(y);
    gsl_vector_free(w);
    gsl_matrix_free(cov);

    return (status == GSL_SUCCESS);
}

// ============================================================================
// Polynomial Evaluation
// ============================================================================

/**
 * @brief Evaluate the fitted polynomial surface at a given (x, y) position.
 * @param x      Horizontal pixel coordinate.
 * @param y      Vertical pixel coordinate.
 * @param coeffs GSL coefficient vector from fitPolynomial().
 * @return Interpolated background value.
 */
float BackgroundExtractor::evaluatePolynomial(float x, float y,
                                              const gsl_vector* coeffs)
{
    if (!coeffs) {
        return 0.0f;
    }

    auto C = [&](int i) { return gsl_vector_get(coeffs, i); };

    double val = C(0) + C(1) * x + C(2) * y;

    if (m_degree >= 2) {
        val += C(3) * x * x + C(4) * x * y + C(5) * y * y;
    }
    if (m_degree >= 3) {
        val += C(6) * x * x * x + C(7) * x * x * y
             + C(8) * x * y * y + C(9) * y * y * y;
    }
    if (m_degree >= 4) {
        val += C(10) * x * x * x * x + C(11) * x * x * x * y
             + C(12) * x * x * y * y + C(13) * x * y * y * y
             + C(14) * y * y * y * y;
    }

    return static_cast<float>(val);
}

// ============================================================================
// RBF (Thin-Plate Spline) Fitting
// ============================================================================

/**
 * @brief Fit a thin-plate spline (RBF) surface to the sample grid.
 *
 * Uses the thin-plate spline kernel: phi(r) = 0.5 * r^2 * ln(r^2).
 * A Tikhonov regularization term (lambda) is added to the diagonal to
 * control smoothness and numerical stability.
 *
 * @param channel Channel index to fit.
 * @return true if the fit succeeded.
 */
bool BackgroundExtractor::fitRBF(int channel)
{
    const int n = static_cast<int>(m_samples.size());
    if (n < 4) {
        return false;
    }

    // Normalize coordinates for numerical stability
    const float scale = 1.0f / static_cast<float>(std::max(m_width, m_height));

    // Allocate kernel matrix (n+1 x n+1) to include the constant bias term
    gsl_matrix* K    = gsl_matrix_calloc(n + 1, n + 1);
    gsl_vector* f    = gsl_vector_calloc(n + 1);
    gsl_vector* coef = gsl_vector_calloc(n + 1);

    double kernelMean = 0.0;

    for (int i = 0; i < n; ++i) {
        const float xi = m_samples[i].x * scale;
        const float yi = m_samples[i].y * scale;

        gsl_vector_set(f, i, m_samples[i].median[channel]);
        gsl_matrix_set(K, i, n, 1.0);   // Bias column
        gsl_matrix_set(K, n, i, 1.0);   // Bias row

        for (int j = 0; j < n; ++j) {
            const float xj = m_samples[j].x * scale;
            const float yj = m_samples[j].y * scale;
            const double r2 = std::pow(xi - xj, 2) + std::pow(yi - yj, 2);
            const double kernel = (r2 > 1e-9) ? (0.5 * r2 * std::log(r2)) : 0.0;

            gsl_matrix_set(K, i, j, kernel);
            kernelMean += kernel;
        }
    }

    kernelMean /= (static_cast<double>(n) * n);

    // Tikhonov regularization scaled by user smoothing parameter
    const float lambda = 1e-4f * std::pow(10.0f, (m_smoothing - 0.5f) * 3.0f);
    for (int i = 0; i < n; ++i) {
        gsl_matrix_set(K, i, i,
                       gsl_matrix_get(K, i, i) + lambda * kernelMean);
    }

    // Solve via LU decomposition
    gsl_permutation* p = gsl_permutation_alloc(n + 1);
    int signum;
    gsl_linalg_LU_decomp(K, p, &signum);
    gsl_linalg_LU_solve(K, p, f, coef);

    // Store results
    m_models[channel].rbfWeights.resize(n + 1);
    for (int i = 0; i <= n; ++i) {
        m_models[channel].rbfWeights[i] =
            static_cast<float>(gsl_vector_get(coef, i));
    }
    m_models[channel].rbfCenters = m_samples;

    // Clean up
    gsl_permutation_free(p);
    gsl_matrix_free(K);
    gsl_vector_free(f);
    gsl_vector_free(coef);

    return true;
}

// ============================================================================
// RBF Evaluation
// ============================================================================

/**
 * @brief Evaluate the fitted RBF surface at a given (x, y) position.
 * @param x       Horizontal pixel coordinate.
 * @param y       Vertical pixel coordinate.
 * @param channel Channel index.
 * @return Interpolated background value.
 */
float BackgroundExtractor::evaluateRBF(float x, float y, int channel)
{
    const auto& model = m_models[channel];
    if (model.rbfWeights.empty()) {
        return 0.0f;
    }

    const float scale = 1.0f / static_cast<float>(std::max(m_width, m_height));
    const float nx    = x * scale;
    const float ny    = y * scale;
    const int   n     = static_cast<int>(model.rbfCenters.size());

    // Start with the constant bias term
    double val = model.rbfWeights[n];

    for (int i = 0; i < n; ++i) {
        const float cx = model.rbfCenters[i].x * scale;
        const float cy = model.rbfCenters[i].y * scale;
        const double r2 = std::pow(nx - cx, 2) + std::pow(ny - cy, 2);

        if (r2 > 1e-9) {
            val += model.rbfWeights[i] * (0.5 * r2 * std::log(r2));
        }
    }

    return static_cast<float>(val);
}

// ============================================================================
// Model Computation (All Channels)
// ============================================================================

bool BackgroundExtractor::computeModel()
{
    if (m_samples.empty()) {
        return false;
    }

    clearModels();
    m_models.resize(m_channels);

    bool ok = true;
    for (int c = 0; c < m_channels; ++c) {
        if (m_method == FittingMethod::Polynomial) {
            if (!fitPolynomial(c)) ok = false;
        } else {
            if (!fitRBF(c)) ok = false;
        }
    }

    return ok;
}

// ============================================================================
// Background Correction Application
// ============================================================================

/**
 * @brief Apply the fitted background model to the source image.
 *
 * For subtraction mode, the mean background level is preserved to avoid
 * shifting the overall brightness. For division mode, the image is normalized
 * by the model surface, scaled to the mean background level.
 */
bool BackgroundExtractor::apply(const ImageBuffer& src, ImageBuffer& dst,
                                CorrectionType type)
{
    if (!src.isValid()) {
        return false;
    }

    dst = src;

    float*    data = dst.data().data();
    const int w    = src.width();
    const int h    = src.height();
    const int ch   = src.channels();

    // Compute average background level per channel for brightness preservation
    std::vector<float> bgMeans(ch, 0.0f);
    for (int c = 0; c < ch; ++c) {
        double sum = 0.0;
        for (const auto& s : m_samples) {
            sum += s.median[c];
        }
        bgMeans[c] = static_cast<float>(sum / m_samples.size());
    }

    // Apply correction pixel-by-pixel
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < ch; ++c) {
                const float bg = (m_method == FittingMethod::Polynomial)
                    ? evaluatePolynomial(static_cast<float>(x),
                                         static_cast<float>(y),
                                         m_models[c].polyCoeffs)
                    : evaluateRBF(static_cast<float>(x),
                                  static_cast<float>(y), c);

                const size_t idx =
                    (static_cast<size_t>(y) * w + x) * ch + c;

                if (type == CorrectionType::Subtraction) {
                    data[idx] = std::clamp(data[idx] - bg + bgMeans[c],
                                           0.0f, 1.0f);
                } else {
                    data[idx] = std::clamp(
                        bg > 0.0f ? (data[idx] / bg * bgMeans[c]) : data[idx],
                        0.0f, 1.0f);
                }
            }
        }
    }

    return true;
}

} // namespace Background