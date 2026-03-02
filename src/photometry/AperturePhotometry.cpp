#include "AperturePhotometry.h"
#include "PsfFitter.h"
#include <algorithm>
#include <numeric>
#include <cmath>

// Minimum sky pixels for valid photometry (Standard: MIN_SKY = 5)
static constexpr int MIN_SKY = 5;

AperturePhotometry::AperturePhotometry() {}

void AperturePhotometry::setConfig(const ApertureConfig& cfg) {
    m_config = cfg;
}

// Standard robustmean implementation using Hampel function
double AperturePhotometry::robustMean(std::vector<double>& data, double* stdev) {
    if (data.empty()) {
        if (stdev) *stdev = 0.0;
        return 0.0;
    }
    if (data.size() == 1) {
        if (stdev) *stdev = 0.0;
        return data[0];
    }
    
    // Quick median using partial sort
    size_t n = data.size();
    size_t mid = n / 2;
    std::nth_element(data.begin(), data.begin() + mid, data.end());
    double median = data[mid];
    
    // Compute MAD for scale estimate
    std::vector<double> absdev(n);
    for (size_t i = 0; i < n; ++i) {
        absdev[i] = std::abs(data[i] - median);
    }
    std::nth_element(absdev.begin(), absdev.begin() + mid, absdev.end());
    double mad = absdev[mid];
    double scale = mad / 0.6745;  // MAD to sigma conversion
    
    if (scale < 1e-10) {
        // All values nearly identical
        if (stdev) *stdev = 0.0;
        return median;
    }
    
    // Hampel M-estimator iterations (Standard robustmean)
    constexpr double hampel_a = 1.7;
    constexpr double hampel_b = 3.4;
    constexpr double hampel_c = 8.5;
    constexpr int max_iter = 50;
    
    auto hampel = [](double x) -> double {
        double ax = std::abs(x);
        if (ax < hampel_a) return x;
        if (ax < hampel_b) return (x >= 0 ? hampel_a : -hampel_a);
        if (ax < hampel_c) return hampel_a * (x >= 0 ? 1 : -1) * (hampel_c - ax) / (hampel_c - hampel_b);
        return 0.0;
    };
    
    auto dhampel = [](double x) -> double {
        double ax = std::abs(x);
        if (ax < hampel_a) return 1.0;
        if (ax < hampel_b) return 0.0;
        if (ax < hampel_c) return hampel_a / (hampel_b - hampel_c);
        return 0.0;
    };
    
    double a = median;
    double s = scale;
    double c_factor = s * s * n * n / (n - 1);
    double dt = 0;
    
    for (int iter = 0; iter < max_iter; ++iter) {
        double sum1 = 0, sum2 = 0, sum3 = 0;
        for (size_t i = 0; i < n; ++i) {
            double r = (data[i] - a) / s;
            double psi = hampel(r);
            sum1 += psi;
            sum2 += dhampel(r);
            sum3 += psi * psi;
        }
        
        if (std::abs(sum2) < 1e-10) break;
        
        double d = s * sum1 / sum2;
        a += d;
        dt = c_factor * sum3 / (sum2 * sum2);
        
        if (iter > 2 && (d * d < 1e-4 * dt || std::abs(d) < 1e-8)) break;
    }
    
    if (stdev) *stdev = (dt > 0 ? std::sqrt(dt) : 0.0);
    return a;
}

double AperturePhotometry::getMagnitude(double intensity) {
    if (intensity <= 0) return 99.999;
    return -2.5 * std::log10(intensity);
}

double AperturePhotometry::getMagError(double intensity, double area, int nsky, 
                                        double skysig, double gain, double* snr) {
    // err1 = sky variance × aperture area
    // err2 = photon noise (intensity / camera gain)
    // err3 = sky uncertainty (skyvar/nsky × area²)
    if (intensity <= 0 || area <= 0 || nsky <= 0) {
        if (snr) *snr = 0.0;
        return 9.999;
    }
    double skyvar = skysig * skysig;
    double sigsq  = skyvar / nsky;
    double err1   = area   * skyvar;
    double err2   = intensity / gain;
    double err3   = sigsq  * area * area;
    double noise  = std::sqrt(err1 + err2 + err3);
    if (noise <= 0) { if (snr) *snr = 9999.0; return 0.0; }
    // SNR in dB (10 × log10) 
    if (snr) *snr = 10.0 * std::log10(intensity / noise);
    // Magnitude error: 1.0857 = 2.5 / ln(10)
    return std::fmin(9.999, 1.0857 * noise / intensity);
}

PSFResult AperturePhotometry::fitPSF(const float* data, int width, int height, int channels,
                                      int channel, int starX, int starY, int boxRadius) {
    PSFResult result;

    // Clamp box to image boundaries
    int bx0 = std::max(0, starX - boxRadius);
    int by0 = std::max(0, starY - boxRadius);
    int bx1 = std::min(width  - 1, starX + boxRadius);
    int by1 = std::min(height - 1, starY + boxRadius);
    int boxw = bx1 - bx0 + 1;
    int boxh = by1 - by0 + 1;
    if (boxw < 3 || boxh < 3) return result;

    // Extract box into a double buffer for PsfFitter
    std::vector<double> box((size_t)boxw * boxh);
    for (int jj = 0; jj < boxh; jj++) {
        for (int ii = 0; ii < boxw; ii++) {
            int ix = bx0 + ii, iy = by0 + jj;
            box[(size_t)jj * boxw + ii] =
                (double)data[(iy * width + ix) * channels + channel];
        }
    }

    // Estimate background from box border pixels (robust median)
    std::vector<double> bgPix;
    bgPix.reserve(2 * (boxw + boxh));
    for (int ii = 0; ii < boxw; ii++) {
        bgPix.push_back(box[ii]);
        bgPix.push_back(box[(size_t)(boxh-1)*boxw+ii]);
    }
    for (int jj = 1; jj < boxh - 1; jj++) {
        bgPix.push_back(box[(size_t)jj*boxw]);
        bgPix.push_back(box[(size_t)jj*boxw+boxw-1]);
    }
    double bg = 0.0;
    if (!bgPix.empty()) {
        std::sort(bgPix.begin(), bgPix.end());
        bg = bgPix[bgPix.size() / 2];
    }

    // PSF fit using Gaussian model (PsfFitter — Levenberg-Marquardt trust region)
    PsfError err = PsfError::OK;
    PsfStar* psf = PsfFitter::fit(box.data(), (size_t)boxh, (size_t)boxw,
                                  bg, 1.0,   // sat = 1.0 (no saturation masking)
                                  1,         // convergence factor
                                  false,     // not called from peaker
                                  PsfProfile::Gaussian, &err);
    if (!psf) {
        // Fallback: moment-based centroid + FWHM estimate
        double sumI = 0, sumIX = 0, sumIY = 0, maxVal = bg;
        for (int jj = 0; jj < boxh; jj++) {
            for (int ii = 0; ii < boxw; ii++) {
                double v = box[(size_t)jj*boxw+ii];
                double intensity = v - bg;
                if (intensity > 0) { sumI += intensity; sumIX += intensity*(bx0+ii); sumIY += intensity*(by0+jj); }
                if (v > maxVal) maxVal = v;
            }
        }
        if (sumI > 0) {
            result.x0 = sumIX / sumI;
            result.y0 = sumIY / sumI;
            result.amplitude  = maxVal - bg;
            result.background = bg;
            // Second-moment FWHM estimate
            double s2xx = 0, s2yy = 0;
            for (int jj = 0; jj < boxh; jj++) {
                for (int ii = 0; ii < boxw; ii++) {
                    double v = box[(size_t)jj*boxw+ii] - bg;
                    if (v > 0) {
                        s2xx += v * ((bx0+ii) - result.x0) * ((bx0+ii) - result.x0);
                        s2yy += v * ((by0+jj) - result.y0) * ((by0+jj) - result.y0);
                    }
                }
            }
            result.fwhmx = 2.355 * std::sqrt(s2xx / sumI);
            result.fwhmy = 2.355 * std::sqrt(s2yy / sumI);
            result.valid = (result.fwhmx > 0.5 && result.amplitude > 0);
        }
        return result;
    }

    // Populate result from PsfStar 
    result.x0         = bx0 + psf->x0;
    result.y0         = by0 + psf->y0;
    result.fwhmx      = psf->fwhmx;
    result.fwhmy      = psf->fwhmy;
    result.amplitude  = psf->A;
    result.background = psf->B;
    result.rmse       = psf->rmse;
    result.beta       = psf->beta;
    result.valid      = std::isfinite(psf->fwhmx) && psf->fwhmx > 0.0
                     && std::isfinite(psf->fwhmy) && psf->fwhmy > 0.0
                     && psf->A > 0.0;
    result.psf        = std::shared_ptr<PsfStar>(psf);
    return result;
}

PhotometryResult AperturePhotometry::measure(const float* data, int width, int height, int channels,
                                              int channel, const PSFResult& psf) {
    PhotometryResult result;
    
    if (!psf.valid) return result;
    
    double xc = psf.x0;
    double yc = psf.y0;
    
    // Compute aperture radius (Standard uses auto from FWHM or fixed)
    double appRadius = m_config.force_radius ? 
        m_config.aperture : 
        0.5 * std::max(psf.fwhmx, psf.fwhmy) * m_config.auto_aperture_factor;
    
    double r1 = m_config.inner;
    double r2 = m_config.outer;
    
    // Bounding box for outer radius
    int x1 = static_cast<int>(std::max(0.0, xc - r2));
    int x2 = static_cast<int>(std::min(static_cast<double>(width - 1), xc + r2));
    int y1 = static_cast<int>(std::max(0.0, yc - r2));
    int y2 = static_cast<int>(std::min(static_cast<double>(height - 1), yc + r2));
    
    if (x2 <= x1 || y2 <= y1) return result;
    
    // Square the radii for distance checks
    double r1_sq = r1 * r1;
    double r2_sq = r2 * r2;
    double rmin_sq = (appRadius - 0.5) * (appRadius - 0.5);
    
    std::vector<double> skyPixels;
    double apmag = 0.0;
    double area = 0.0;
    
    // Aperture photometry loop (exact standard algorithm)
    for (int y = y1; y <= y2; ++y) {
        double dy = y - yc;
        double dy_sq = dy * dy;
        
        for (int x = x1; x <= x2; ++x) {
            double dx = x - xc;
            double r_sq = dy_sq + dx * dx;
            
            double pixel = data[(y * width + x) * channels + channel];
            
            // Check pixel validity
            if (pixel > m_config.minval && pixel < m_config.maxval) {
                // Aperture contribution (with sub-pixel weighting at edge)
                double r = std::sqrt(r_sq);
                double f = (r_sq < rmin_sq) ? 1.0 : appRadius - r + 0.5;
                
                if (f > 0) {
                    area += f;
                    apmag += pixel * f;
                }
                
                // Sky annulus
                if (r_sq < r2_sq && r_sq > r1_sq) {
                    skyPixels.push_back(pixel);
                }
            }
        }
    }
    
    if (area < 1.0 || skyPixels.size() < MIN_SKY) return result;
    
    // Compute sky background using robust mean
    double skyMean = 0, skyStdev = 0;
    skyMean = robustMean(skyPixels, &skyStdev);
    
    // Signal intensity = aperture sum - background * area
    double signalIntensity = apmag - (area * skyMean);
    
    if (signalIntensity <= 0) return result;
    
    // Compute magnitude and error
    result.mag = getMagnitude(signalIntensity);
    result.mag_error = getMagError(signalIntensity, area, static_cast<int>(skyPixels.size()),
                                    skyStdev, m_config.gain, &result.snr);
    
    // Convert magnitude to linear flux (Standard uses: flux = 10^(-0.4 * mag))
    result.flux = std::pow(10.0, -0.4 * result.mag);
    result.valid = (result.mag_error < 9.999 && result.snr > 0);
    
    return result;
}

PhotometryResult AperturePhotometry::measureStar(const float* data, int width, int height, int channels,
                                                  int channel, int starX, int starY) {
    // First fit PSF to get centroid
    PSFResult psf = fitPSF(data, width, height, channels, channel, starX, starY, 15);
    
    // Then do aperture photometry
    return measure(data, width, height, channels, channel, psf);
}
