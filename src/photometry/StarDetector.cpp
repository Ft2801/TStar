#include "StarDetector.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_sort.h>
#include <omp.h>

StarDetector::StarDetector() {}

StarDetector::~StarDetector() {}

void StarDetector::setThresholdSigma(float sigma) {
    m_sigma = sigma;
}

void StarDetector::setMinFWHM(float fwhm) {
    m_minFWHM = fwhm;
}

void StarDetector::setMaxStars(int max) {
    m_maxStars = max;
}

void StarDetector::computeBackgroundStats(const ImageBuffer& img, int channel, double& median, double& stdev) {
    // Collect a sample of pixels to estimate background
    // We sample every Nth pixel to speed up
    const int step = 10;
    std::vector<double> sample;
    sample.reserve((img.width() * img.height()) / (step * step));

    for (int y = 0; y < img.height(); y += step) {
        for (int x = 0; x < img.width(); x += step) {
            float val = img.getPixelValue(x, y, channel);
            if (val > 0.0f) { // Ignore dead pixels or borders if any
                sample.push_back(static_cast<double>(val));
            }
        }
    }

    if (sample.empty()) {
        median = 0;
        stdev = 0;
        return;
    }

    // Sort to find median
    gsl_sort(sample.data(), 1, sample.size());
    median = gsl_stats_median_from_sorted_data(sample.data(), 1, sample.size());
    
    // Estimate noise using MAD (Median Absolute Deviation)
    // MAD = median(|x_i - median|)
    std::vector<double> absDevs;
    absDevs.reserve(sample.size());
    for (double v : sample) {
        absDevs.push_back(std::abs(v - median));
    }
    gsl_sort(absDevs.data(), 1, absDevs.size());
    double mad = gsl_stats_median_from_sorted_data(absDevs.data(), 1, absDevs.size());
    
    // Sigma approximation from MAD for Gaussian distribution: sigma ~ 1.4826 * MAD
    stdev = 1.4826 * mad;
}

// Helper for Separable Gaussian Blur
[[maybe_unused]] static void applySeparableBlur(const float* src, float* dst, int w, int h, int ch, int targetChannel, float sigma) {
    if (sigma <= 0) {
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) dst[i] = src[i * ch + targetChannel];
        return;
    }

    int kernelSize = static_cast<int>(ceil(sigma * 3)) * 2 + 1;
    std::vector<float> kernel(kernelSize);
    float sum = 0;
    int mid = kernelSize / 2;
    for (int i = 0; i < kernelSize; ++i) {
        float x = static_cast<float>(i - mid);
        kernel[i] = exp(-(x * x) / (2 * sigma * sigma));
        sum += kernel[i];
    }
    for (float& k : kernel) k /= sum;

    std::vector<float> temp(w * h);

    // X Blur
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float val = 0;
            for (int k = 0; k < kernelSize; ++k) {
                int nx = std::clamp(x + k - mid, 0, w - 1);
                val += src[(y * w + nx) * ch + targetChannel] * kernel[k];
            }
            temp[y * w + x] = val;
        }
    }

    // Y Blur
    #pragma omp parallel for
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            float val = 0;
            for (int k = 0; k < kernelSize; ++k) {
                int ny = std::clamp(y + k - mid, 0, h - 1);
                val += temp[ny * w + x] * kernel[k];
            }
            dst[y * h + x] = val; // Error here: dst should be addressed as [y * w + x]
        }
    }
}

// Corrected Y Blur index
static void applySeparableBlurFixed(const float* src, float* dst, int w, int h, int ch, int targetChannel, float sigma) {
    if (sigma <= 0) {
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) dst[i] = src[i * ch + targetChannel];
        return;
    }

    int kernelSize = static_cast<int>(ceil(sigma * 3)) * 2 + 1;
    std::vector<float> kernel(kernelSize);
    float sum = 0;
    int mid = kernelSize / 2;
    for (int i = 0; i < kernelSize; ++i) {
        float x = static_cast<float>(i - mid);
        kernel[i] = exp(-(x * x) / (2 * sigma * sigma));
        sum += kernel[i];
    }
    for (float& k : kernel) k /= sum;

    std::vector<float> temp(w * h);

    // Horizontal pass
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float val = 0;
            for (int k = 0; k < kernelSize; ++k) {
                int nx = std::clamp(x + k - mid, 0, w - 1);
                val += src[(y * w + nx) * ch + targetChannel] * kernel[k];
            }
            temp[y * w + x] = val;
        }
    }

    // Vertical pass
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float val = 0;
            for (int k = 0; k < kernelSize; ++k) {
                int ny = std::clamp(y + k - mid, 0, h - 1);
                val += temp[ny * w + x] * kernel[k];
            }
            dst[y * w + x] = val;
        }
    }
}

std::vector<DetectedStar> StarDetector::detect(const ImageBuffer& image, int channel) {
    int w = image.width();
    int h = image.height();
    const float* rawData = image.data().data();
    int ch = image.channels();
    
    // Performance: Allocate blurred image once
    std::vector<float> blurred(w * h);
    applySeparableBlurFixed(rawData, blurred.data(), w, h, ch, channel, 1.5f); // Sigma 1.5 for robust detection

    double bgMedian = 0, bgSigma = 0;
    computeBackgroundStats(image, channel, bgMedian, bgSigma);
    
    // Use 4.0 * sigma as robust starting threshold
    float threshold = static_cast<float>(bgMedian + 4.0 * bgSigma);
    
    int border = 10;
    std::vector<std::vector<DetectedStar>> threadStars;
    int maxThreads = omp_get_max_threads();
    threadStars.resize(maxThreads);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        std::vector<DetectedStar>& localStars = threadStars[tid];
        localStars.reserve(1000);

        #pragma omp for schedule(dynamic, 64)
        for (int y = border; y < h - border; ++y) {
            const float* row = blurred.data() + (y * w);
            const float* prevRow = row - w;
            const float* nextRow = row + w;
            
            for (int x = border; x < w - border; ++x) {
                float val = row[x];
                if (val > threshold) {
                    // 8-neighbor local max
                    if (val > prevRow[x-1] && val > prevRow[x] && val > prevRow[x+1] &&
                        val > row[x-1]     &&                 val > row[x+1]     &&
                        val > nextRow[x-1] && val > nextRow[x] && val > nextRow[x+1]) 
                    {
                        // Centroiding on original data for accuracy
                        double sumF = 0, sumFX = 0, sumFY = 0;
                        int r = 4; // 9x9 window for robustness
                        
                        for (int dy = -r; dy <= r; ++dy) {
                            const float* origRow = rawData + ((y + dy) * w * ch) + channel;
                            for (int dx = -r; dx <= r; ++dx) {
                                float v = origRow[(x + dx) * ch];
                                double f = v - bgMedian;
                                if (f > 0) {
                                    sumF += f;
                                    sumFX += f * (x + dx);
                                    sumFY += f * (y + dy);
                                }
                            }
                        }
                        
                        if (sumF > 0) {
                            DetectedStar s;
                            s.x = sumFX / sumF;
                            s.y = sumFY / sumF;
                            s.flux = sumF;
                            s.peak = val;
                            s.background = bgMedian;
                            s.saturated = (rawData[(y * w + x) * ch + channel] >= 0.99f);
                            localStars.push_back(s);
                        }
                    }
                }
            }
        }
    }
    
    std::vector<DetectedStar> stars;
    for (const auto& ls : threadStars) stars.insert(stars.end(), ls.begin(), ls.end());
    
    std::sort(stars.begin(), stars.end(), [](const DetectedStar& a, const DetectedStar& b) {
        return a.flux > b.flux;
    });
    
    if ((int)stars.size() > m_maxStars) stars.resize(m_maxStars);
    
    return stars;
}
