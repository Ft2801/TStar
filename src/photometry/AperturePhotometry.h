#pragma once

#include <vector>
#include <cmath>

// Aperture Photometry parameters (matching Standard config)
struct ApertureConfig {
    double aperture = 8.0;        // Aperture radius
    double inner = 14.0;          // Sky annulus inner radius
    double outer = 24.0;          // Sky annulus outer radius
    double auto_aperture_factor = 2.0;  // Factor for auto aperture from FWHM
    bool force_radius = false;    // Force fixed aperture (else auto from FWHM)
    double gain = 1.0;            // Camera gain for SNR calculation
    double minval = 0.0;          // Min valid pixel value
    double maxval = 1.0;          // Max valid pixel value
};

// Photometry result (matching Standard photometry struct)
struct PhotometryResult {
    double mag = 0.0;        // Instrumental magnitude
    double mag_error = 0.0;  // Magnitude error
    double snr = 0.0;        // Signal to noise ratio
    double flux = 0.0;       // Linear flux (10^(-0.4*mag))
    bool valid = false;
};

// Star PSF result
struct PSFResult {
    double x0 = 0.0;         // Centroid X
    double y0 = 0.0;         // Centroid Y
    double fwhmx = 0.0;      // FWHM X
    double fwhmy = 0.0;      // FWHM Y
    double amplitude = 0.0;  // Peak amplitude
    double background = 0.0; // Local background
    bool valid = false;
};

class AperturePhotometry {
public:
    AperturePhotometry();
    
    // Set configuration
    void setConfig(const ApertureConfig& cfg);
    
    // Perform PSF fitting on a star at given position
    PSFResult fitPSF(const float* data, int width, int height, int channels,
                     int channel, int starX, int starY, int boxRadius = 10);
    
    // Perform aperture photometry using PSF result
    PhotometryResult measure(const float* data, int width, int height, int channels,
                             int channel, const PSFResult& psf);
    
    // Combined: fit PSF then measure photometry
    PhotometryResult measureStar(const float* data, int width, int height, int channels,
                                 int channel, int starX, int starY);

private:
    ApertureConfig m_config;
    
    // Background estimation using robust mean (Standard robustmean)
    double robustMean(std::vector<double>& data, double* stdev = nullptr);
    
    // Get magnitude from intensity
    static double getMagnitude(double intensity);
    
    // Get magnitude error with SNR
    static double getMagError(double intensity, double area, int nsky, 
                              double skysig, double gain, double* snr = nullptr);
};
