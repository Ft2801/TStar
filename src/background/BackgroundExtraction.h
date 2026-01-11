#ifndef BACKGROUNDEXTRACTION_H
#define BACKGROUNDEXTRACTION_H

#include "../ImageBuffer.h"
#include <vector>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>

namespace Background {

enum class PolyOrder {
    Order1 = 1,
    Order2 = 2,
    Order3 = 3,
    Order4 = 4
};

enum class CorrectionType {
    Subtraction,
    Division
};

enum class FittingMethod {
    Polynomial,
    RBF
};

struct Sample {
    float x, y;
    float median[3]; // Median per channel
    bool valid = true;
};

class BackgroundExtractor {
public:
    BackgroundExtractor();
    ~BackgroundExtractor();

    void setParameters(int degree, float tolerance, float smoothing = 0.5f);
    void generateGrid(const ImageBuffer& img, int samplesPerLine = 20);
    bool computeModel();
    bool apply(const ImageBuffer& src, ImageBuffer& dst, CorrectionType type);

private:
    void generateSamplesAuto(const ImageBuffer& img);
    
    // Fitting
    bool fitPolynomial(int channel);
    bool fitRBF(int channel);
    
    // Evaluation
    float evaluatePolynomial(float x, float y, const gsl_vector* coeffs);
    float evaluateRBF(float x, float y, int channel);

    // Helpers
    std::vector<float> computeLuminance(const ImageBuffer& img);
    
    int m_degree = 2;
    float m_tolerance = 3.0f;
    float m_smoothing = 0.5f;
    FittingMethod m_method = FittingMethod::Polynomial;
    
    std::vector<Sample> m_samples;
    int m_width = 0;
    int m_height = 0;
    int m_channels = 0;

    // Model data
    struct ChannelModel {
        gsl_vector* polyCoeffs = nullptr;
        // RBF data
        std::vector<float> rbfWeights;
        std::vector<Sample> rbfCenters;
    };
    std::vector<ChannelModel> m_models;
    
    void clearModels();
};

} // namespace Background

#endif // BACKGROUNDEXTRACTION_H
