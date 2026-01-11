#include "BackgroundExtraction.h"
#include "../core/RobustStatistics.h"
#include "../stacking/Statistics.h"
#include <gsl/gsl_statistics_float.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include <cmath>
#include <algorithm>
#include <omp.h>
#include <opencv2/opencv.hpp>
#include <QDebug>

namespace Background {

BackgroundExtractor::BackgroundExtractor() {
}

BackgroundExtractor::~BackgroundExtractor() {
    clearModels();
}

void BackgroundExtractor::clearModels() {
    for (auto& m : m_models) {
        if (m.polyCoeffs) gsl_vector_free(m.polyCoeffs);
    }
    m_models.clear();
}

void BackgroundExtractor::setParameters(int degree, float tolerance, float smoothing) {
    m_degree = std::clamp(degree, 1, 4);
    m_tolerance = tolerance;
    m_smoothing = smoothing;
}

std::vector<float> BackgroundExtractor::computeLuminance(const ImageBuffer& img) {
    int w = img.width();
    int h = img.height();
    int ch = img.channels();
    std::vector<float> luma(w * h);
    const float* data = img.data().data();

    #pragma omp parallel for
    for (int i = 0; i < w * h; ++i) {
        if (ch >= 3) {
            // Rec. 709
            luma[i] = 0.2126f * data[i * ch] + 0.7152f * data[i * ch + 1] + 0.0722f * data[i * ch + 2];
        } else {
            luma[i] = data[i * ch];
        }
    }
    return luma;
}

void BackgroundExtractor::generateGrid(const ImageBuffer& img, int samplesPerLine) {
    m_width = img.width();
    m_height = img.height();
    m_channels = img.channels();
    m_samples.clear();

    // 1. Compute image median of luminance
    std::vector<float> luma = computeLuminance(img);
    std::vector<float> copy = luma;
    float median = Stacking::Statistics::quickMedian(copy);
    if (median <= 0.0f) median = 1e-6f;

    // 2. Initial grid
    int size = 25; // Sample size
    int radius = size / 2;
    int nx = samplesPerLine;
    
    int boxes_width = nx * size + 2;
    float spacing = (m_width - boxes_width) / (float)std::max(1, nx - 1);

    int ny = 1;
    while (ny * size + std::round((ny - 1) * spacing) < (m_height - 2)) ny++;
    ny--;
    if (ny <= 0) ny = 1;

    int total_grid_height = ny * size + (ny - 1) * std::round(spacing);
    int y_offset = (m_height - 2 - total_grid_height) / 2 + 1;

    std::vector<Sample> candidates;
    std::vector<float> diffs;

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            int cx = std::round(i * (spacing + size)) + radius + 1;
            int cy = y_offset + std::round(j * (spacing + size)) + radius;

            if (cx < radius || cx >= m_width - radius || cy < radius || cy >= m_height - radius) continue;

            Sample s;
            s.x = cx;
            s.y = cy;

            // Median of the patch (per channel)
            for (int c = 0; c < m_channels; ++c) {
                std::vector<float> patch;
                patch.reserve(size * size);
                for (int py = cy - radius; py <= cy + radius; ++py) {
                    for (int px = cx - radius; px <= cx + radius; ++px) {
                        patch.push_back(img.data()[(py * m_width + px) * m_channels + c]);
                    }
                }
                s.median[c] = Stacking::Statistics::quickMedian(patch);
            }

            // Luminance median for this sample
            float sLuma = 0;
            if (m_channels >= 3) sLuma = 0.2126f * s.median[0] + 0.7152f * s.median[1] + 0.0722f * s.median[2];
            else sLuma = s.median[0];

            diffs.push_back(std::abs(sLuma - median));
            candidates.push_back(s);
        }
    }

    // In TStar we use RobustStatistics
    float mad = RobustStatistics::getMedian(diffs);
    float threshold = median + mad * m_tolerance;

    for (auto& s : candidates) {
        float sLuma = (m_channels >= 3) ? (0.2126f * s.median[0] + 0.7152f * s.median[1] + 0.0722f * s.median[2]) : s.median[0];
        if (sLuma > 0 && sLuma < threshold) {
            m_samples.push_back(s);
        }
    }
}

bool BackgroundExtractor::fitPolynomial(int channel) {
    int n = m_samples.size();
    int p = 0;
    switch (m_degree) {
        case 1: p = 3; break;
        case 2: p = 6; break;
        case 3: p = 10; break;
        case 4: p = 15; break;
    }

    if (n < p) return false;

    gsl_matrix* J = gsl_matrix_alloc(n, p);
    gsl_vector* y = gsl_vector_alloc(n);
    gsl_vector* w = gsl_vector_alloc(n);
    gsl_vector* c = gsl_vector_alloc(p);
    gsl_matrix* cov = gsl_matrix_alloc(p, p);

    for (int i = 0; i < n; ++i) {
        double col = m_samples[i].x;
        double row = m_samples[i].y;
        double val = m_samples[i].median[channel];

        gsl_matrix_set(J, i, 0, 1.0);
        gsl_matrix_set(J, i, 1, col);
        gsl_matrix_set(J, i, 2, row);
        if (m_degree >= 2) {
            gsl_matrix_set(J, i, 3, col * col);
            gsl_matrix_set(J, i, 4, col * row);
            gsl_matrix_set(J, i, 5, row * row);
        }
        if (m_degree >= 3) {
            gsl_matrix_set(J, i, 6, col * col * col);
            gsl_matrix_set(J, i, 7, col * col * row);
            gsl_matrix_set(J, i, 8, col * row * row);
            gsl_matrix_set(J, i, 9, row * row * row);
        }
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

    gsl_multifit_linear_workspace* work = gsl_multifit_linear_alloc(n, p);
    double chisq;
    int status = gsl_multifit_wlinear(J, w, y, c, cov, &chisq, work);
    gsl_multifit_linear_free(work);

    if (status == GSL_SUCCESS) {
        m_models[channel].polyCoeffs = c;
    } else {
        gsl_vector_free(c);
    }

    gsl_matrix_free(J);
    gsl_vector_free(y);
    gsl_vector_free(w);
    gsl_matrix_free(cov);

    return status == GSL_SUCCESS;
}

float BackgroundExtractor::evaluatePolynomial(float x, float y, const gsl_vector* coeffs) {
    if (!coeffs) return 0;
    auto C = [&](int i) { return gsl_vector_get(coeffs, i); };
    
    double val = C(0) + C(1) * x + C(2) * y;
    if (m_degree >= 2) {
        val += C(3) * x * x + C(4) * x * y + C(5) * y * y;
    }
    if (m_degree >= 3) {
        val += C(6) * x * x * x + C(7) * x * x * y + C(8) * x * y * y + C(9) * y * y * y;
    }
    if (m_degree >= 4) {
        val += C(10) * x * x * x * x + C(11) * x * x * x * y + C(12) * x * x * y * y + C(13) * x * y * y * y + C(14) * y * y * y * y;
    }
    return static_cast<float>(val);
}

bool BackgroundExtractor::fitRBF(int channel) {
    int n = m_samples.size();
    if (n < 4) return false;

    // We scale coordinates for numerical stability
    float scale = 1.0f / std::max(m_width, m_height);

    gsl_matrix* K = gsl_matrix_calloc(n + 1, n + 1);
    gsl_vector* f = gsl_vector_calloc(n + 1);
    gsl_vector* coef = gsl_vector_calloc(n + 1);

    double kernelMean = 0;
    for (int i = 0; i < n; i++) {
        float xi = m_samples[i].x * scale;
        float yi = m_samples[i].y * scale;
        gsl_vector_set(f, i, m_samples[i].median[channel]);
        gsl_matrix_set(K, i, n, 1.0);
        gsl_matrix_set(K, n, i, 1.0);

        for (int j = 0; j < n; j++) {
            float xj = m_samples[j].x * scale;
            float yj = m_samples[j].y * scale;
            double r2 = std::pow(xi - xj, 2) + std::pow(yi - yj, 2);
            double kernel = (r2 > 1e-9) ? (0.5 * r2 * std::log(r2)) : 0.0;
            gsl_matrix_set(K, i, j, kernel);
            kernelMean += kernel;
        }
    }
    kernelMean /= (n * n);
    
    float lambda = 1e-4f * std::pow(10.0f, (m_smoothing - 0.5f) * 3.0f);
    for (int i = 0; i < n; i++) {
        gsl_matrix_set(K, i, i, gsl_matrix_get(K, i, i) + lambda * kernelMean);
    }

    gsl_permutation* p = gsl_permutation_alloc(n + 1);
    int s;
    gsl_linalg_LU_decomp(K, p, &s);
    gsl_linalg_LU_solve(K, p, f, coef);

    m_models[channel].rbfWeights.resize(n + 1);
    for (int i = 0; i <= n; i++) m_models[channel].rbfWeights[i] = gsl_vector_get(coef, i);
    m_models[channel].rbfCenters = m_samples;

    gsl_permutation_free(p);
    gsl_matrix_free(K);
    gsl_vector_free(f);
    gsl_vector_free(coef);

    return true;
}

float BackgroundExtractor::evaluateRBF(float x, float y, int channel) {
    const auto& model = m_models[channel];
    if (model.rbfWeights.empty()) return 0;

    float scale = 1.0f / std::max(m_width, m_height);
    float nx = x * scale;
    float ny = y * scale;
    int n = model.rbfCenters.size();
    
    double val = model.rbfWeights[n]; // Constant term
    for (int i = 0; i < n; i++) {
        float cx = model.rbfCenters[i].x * scale;
        float cy = model.rbfCenters[i].y * scale;
        double r2 = std::pow(nx - cx, 2) + std::pow(ny - cy, 2);
        if (r2 > 1e-9) val += model.rbfWeights[i] * (0.5 * r2 * std::log(r2));
    }
    return static_cast<float>(val);
}

bool BackgroundExtractor::computeModel() {
    if (m_samples.empty()) return false;
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

bool BackgroundExtractor::apply(const ImageBuffer& src, ImageBuffer& dst, CorrectionType type) {
    if (!src.isValid()) return false;
    dst = src;
    float* data = dst.data().data();
    int w = src.width();
    int h = src.height();
    int ch = src.channels();

    // Average background level for preserving brightness in Subtraction
    std::vector<float> bgMeans(ch, 0.0f);
    for (int c = 0; c < ch; ++c) {
        double sum = 0;
        for (const auto& s : m_samples) sum += s.median[c];
        bgMeans[c] = sum / m_samples.size();
    }

    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < ch; ++c) {
                float bg = (m_method == FittingMethod::Polynomial) ? 
                            evaluatePolynomial(x, y, m_models[c].polyCoeffs) :
                            evaluateRBF(x, y, c);
                size_t idx = (static_cast<size_t>(y) * w + x) * ch + c;
                if (type == CorrectionType::Subtraction) {
                    data[idx] = std::clamp(data[idx] - bg + bgMeans[c], 0.0f, 1.0f);
                } else {
                    data[idx] = std::clamp(bg > 0 ? (data[idx] / bg * bgMeans[c]) : data[idx], 0.0f, 1.0f);
                }
            }
        }
    }

    return true;
}

} // namespace Background
