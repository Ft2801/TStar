#ifndef STARDETECTOR_H
#define STARDETECTOR_H

#include "../ImageBuffer.h"
#include <vector>

struct DetectedStar {
    double x; // Centroid X
    double y; // Centroid Y
    double flux;
    double peak;
    double background;
    float fwhm; // Approximate
    bool saturated;
};

class StarDetector {
public:
    StarDetector();
    ~StarDetector();

    // Configuration
    void setThresholdSigma(float sigma);
    void setMinFWHM(float fwhm);
    void setMaxStars(int max);

    // Main Detection
    std::vector<DetectedStar> detect(const ImageBuffer& image, int channel = 1); // Default to Green or Mono

private:
    float m_sigma = 2.5f;
    float m_minFWHM = 1.5f;
    int m_maxStars = 2000;

    // Helper functions
    void computeBackgroundStats(const ImageBuffer& img, int channel, double& median, double& stdev);
};

#endif // STARDETECTOR_H
