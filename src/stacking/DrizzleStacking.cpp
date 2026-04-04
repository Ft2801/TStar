/**
 * @file DrizzleStacking.cpp
 * @brief Implementation of drizzle integration, mosaic feathering, and
 *        auxiliary rejection algorithms.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "DrizzleStacking.h"
#include "Statistics.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

namespace Stacking {

// ============================================================================
// MosaicFeathering -- Ramp LUT initialisation
// ============================================================================

std::vector<float> MosaicFeathering::s_rampLUT;

void MosaicFeathering::initRampLUT()
{
    if (!s_rampLUT.empty()) return;

    const int N = 1001;
    s_rampLUT.resize(N);

    for (int i = 0; i < N; ++i) {
        float r = static_cast<float>(i) / 1000.0f;
        // Quintic smooth-step: 6r^5 - 15r^4 + 10r^3
        s_rampLUT[i] = r * r * r * (6.0f * r * r - 15.0f * r + 10.0f);
    }
}

// ============================================================================
// DrizzleStacking -- Weight map computation
// ============================================================================

DrizzleStacking::DrizzleWeight DrizzleStacking::computeWeightMap(
    const ImageBuffer& input,
    const RegistrationData& reg,
    int outputWidth, int outputHeight,
    const DrizzleParams& params)
{
    DrizzleWeight result;
    result.width  = outputWidth;
    result.height = outputHeight;
    result.weight.resize(static_cast<size_t>(outputWidth) * outputHeight, 0.0f);

    const double scale = params.scaleFactor;
    const double drop  = params.dropSize;
    const int inW = input.width();
    const int inH = input.height();

    // For each input pixel, project its four corners into output space,
    // optionally shrink the resulting quad, then accumulate overlap area.
    #pragma omp parallel for
    for (int y = 0; y < inH; ++y) {
        for (int x = 0; x < inW; ++x) {

            // Define the four corners of the input pixel
            double px[] = { static_cast<double>(x),     static_cast<double>(x) + 1.0,
                            static_cast<double>(x) + 1.0, static_cast<double>(x) };
            double py[] = { static_cast<double>(y),     static_cast<double>(y),
                            static_cast<double>(y) + 1.0, static_cast<double>(y) + 1.0 };

            // Transform corners to output space
            Polygon quad(4);
            double minX =  1e9, maxX = -1e9;
            double minY =  1e9, maxY = -1e9;

            for (int i = 0; i < 4; ++i) {
                QPointF pt = reg.transform(px[i], py[i]);
                quad[i] = { pt.x() * scale, pt.y() * scale };
            }

            // Shrink the quad towards its centroid to simulate the drop size
            if (drop < 1.0 - 1e-5) {
                quad = shrinkPolygon(quad, drop);
            }

            // Compute the bounding box of the transformed quad
            for (const auto& p : quad) {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }

            int x0 = std::max(0, static_cast<int>(std::floor(minX)));
            int x1 = std::min(outputWidth  - 1, static_cast<int>(std::floor(maxX)));
            int y0 = std::max(0, static_cast<int>(std::floor(minY)));
            int y1 = std::min(outputHeight - 1, static_cast<int>(std::floor(maxY)));

            // Scan every output pixel that the bounding box touches
            for (int oy = y0; oy <= y1; ++oy) {
                for (int ox = x0; ox <= x1; ++ox) {
                    double oxD = static_cast<double>(ox);
                    double oyD = static_cast<double>(oy);

                    Polygon clipped = clipPolygon(quad, oxD, oyD, oxD + 1.0, oyD + 1.0);
                    double area = computePolygonArea(clipped);

                    if (area > 1e-6) {
                        size_t idx = static_cast<size_t>(oy) * outputWidth + ox;
                        #pragma omp atomic
                        result.weight[idx] += static_cast<float>(area);
                    }
                }
            }
        }
    }

    return result;
}

// ============================================================================
// DrizzleStacking -- Full polygon-clipping drizzle
// ============================================================================

void DrizzleStacking::drizzleFrame(
    const ImageBuffer& input,
    const RegistrationData& reg,
    std::vector<double>& accum,
    std::vector<double>& weightAccum,
    int outputWidth, int outputHeight,
    const DrizzleParams& params,
    double offsetX, double offsetY)
{
    const double scale = params.scaleFactor;
    const double drop  = params.dropSize;
    const int inW      = input.width();
    const int inH      = input.height();
    const int channels = input.channels();
    const size_t outPixels = static_cast<size_t>(outputWidth) * outputHeight;

    #pragma omp parallel for
    for (int y = 0; y < inH; ++y) {
        for (int x = 0; x < inW; ++x) {

            // Define pixel corner coordinates
            double px[] = { static_cast<double>(x),     static_cast<double>(x) + 1.0,
                            static_cast<double>(x) + 1.0, static_cast<double>(x) };
            double py[] = { static_cast<double>(y),     static_cast<double>(y),
                            static_cast<double>(y) + 1.0, static_cast<double>(y) + 1.0 };

            // Transform to output space
            Polygon quad(4);
            double minX =  1e9, maxX = -1e9;
            double minY =  1e9, maxY = -1e9;

            for (int i = 0; i < 4; ++i) {
                QPointF pt = reg.transform(px[i], py[i]);
                quad[i] = { (pt.x() - offsetX) * scale, (pt.y() - offsetY) * scale };
            }

            // Shrink polygon to simulate the drop size
            if (drop < 1.0 - 1e-5) {
                quad = shrinkPolygon(quad, drop);
            }

            // Bounding box of transformed quad
            for (const auto& p : quad) {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }

            int x0 = std::max(0, static_cast<int>(std::floor(minX)));
            int x1 = std::min(outputWidth  - 1, static_cast<int>(std::floor(maxX)));
            int y0 = std::max(0, static_cast<int>(std::floor(minY)));
            int y1 = std::min(outputHeight - 1, static_cast<int>(std::floor(maxY)));

            // Scatter flux into every overlapping output pixel
            for (int oy = y0; oy <= y1; ++oy) {
                for (int ox = x0; ox <= x1; ++ox) {
                    double oxD = static_cast<double>(ox);
                    double oyD = static_cast<double>(oy);

                    Polygon clipped = clipPolygon(quad, oxD, oyD, oxD + 1.0, oyD + 1.0);
                    double area = computePolygonArea(clipped);

                    if (area > 1e-6) {
                        float weight = static_cast<float>(area);

                        // Modulate weight with the convolution kernel (if not Point)
                        if (m_currentKernel != DrizzleKernelType::Point) {
                            double cxIn = static_cast<double>(x) + 0.5;
                            double cyIn = static_cast<double>(y) + 0.5;
                            QPointF centerOut = reg.transform(cxIn, cyIn);
                            double tx = (centerOut.x() - offsetX) * scale;
                            double ty = (centerOut.y() - offsetY) * scale;

                            double dx = (static_cast<double>(ox) + 0.5) - tx;
                            double dy = (static_cast<double>(oy) + 0.5) - ty;
                            weight *= getKernelWeight(dx, dy);
                        }

                        size_t idx = static_cast<size_t>(oy) * outputWidth + ox;

                        for (int c = 0; c < channels; ++c) {
                            double val = static_cast<double>(input.value(x, y, c)) * weight;
                            #pragma omp atomic
                            accum[c * outPixels + idx] += val;
                        }

                        #pragma omp atomic
                        weightAccum[idx] += weight;
                    }
                }
            }
        }
    }
}

// ============================================================================
// DrizzleStacking -- Fast 1x point-kernel drizzle
// ============================================================================

void DrizzleStacking::fastDrizzleFrame(
    const ImageBuffer& input,
    const RegistrationData& reg,
    std::vector<double>& accum,
    std::vector<double>& weightAccum,
    int outputWidth, int outputHeight,
    const DrizzleParams& params,
    double offsetX, double offsetY)
{
    const int w        = input.width();
    const int h        = input.height();
    const int channels = input.channels();
    const double scale = params.scaleFactor;
    const size_t outPixels = static_cast<size_t>(outputWidth) * outputHeight;

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {

            // Transform the centre of the input pixel
            double cxIn = static_cast<double>(x) + 0.5;
            double cyIn = static_cast<double>(y) + 0.5;

            QPointF centerOut = reg.transform(cxIn, cyIn);
            double tx = (centerOut.x() - offsetX) * scale;
            double ty = (centerOut.y() - offsetY) * scale;

            // Nearest-neighbour output pixel
            int ox = static_cast<int>(std::floor(tx));
            int oy = static_cast<int>(std::floor(ty));

            if (ox >= 0 && ox < outputWidth && oy >= 0 && oy < outputHeight) {
                size_t idx = static_cast<size_t>(oy) * outputWidth + ox;
                const double weight = 1.0;

                for (int c = 0; c < channels; ++c) {
                    double val = static_cast<double>(input.value(x, y, c)) * weight;
                    #pragma omp atomic
                    accum[c * outPixels + idx] += val;
                }

                #pragma omp atomic
                weightAccum[idx] += weight;
            }
        }
    }
}

// ============================================================================
// DrizzleStacking -- Polygon geometry helpers
// ============================================================================

double DrizzleStacking::computePolygonArea(const Polygon& p)
{
    double area = 0.0;
    size_t j = p.size() - 1;
    for (size_t i = 0; i < p.size(); ++i) {
        area += (p[j].x + p[i].x) * (p[j].y - p[i].y);
        j = i;
    }
    return std::abs(area) * 0.5;
}

DrizzleStacking::Polygon DrizzleStacking::shrinkPolygon(const Polygon& p,
                                                         double factor)
{
    if (p.empty()) return p;

    // Compute the centroid
    double cx = 0.0, cy = 0.0;
    for (const auto& pt : p) {
        cx += pt.x;
        cy += pt.y;
    }
    cx /= static_cast<double>(p.size());
    cy /= static_cast<double>(p.size());

    // Scale each vertex towards the centroid
    Polygon out;
    out.reserve(p.size());
    for (const auto& pt : p) {
        out.push_back({
            cx + (pt.x - cx) * factor,
            cy + (pt.y - cy) * factor
        });
    }
    return out;
}

DrizzleStacking::Polygon DrizzleStacking::clipPolygon(
    const Polygon& subject,
    double xMin, double yMin,
    double xMax, double yMax)
{
    if (subject.empty()) return subject;

    Polygon output = subject;

    // Sutherland-Hodgman clipping against each of the four edges
    auto clipEdge = [&](double cEdge, bool isVertical, bool isMax) {
        Polygon input = output;
        output.clear();
        if (input.empty()) return;

        Point S = input.back();

        for (const auto& E : input) {
            bool E_in, S_in;

            if (isVertical) {
                if (isMax) { E_in = E.x <= cEdge; S_in = S.x <= cEdge; }
                else       { E_in = E.x >= cEdge; S_in = S.x >= cEdge; }
            } else {
                if (isMax) { E_in = E.y <= cEdge; S_in = S.y <= cEdge; }
                else       { E_in = E.y >= cEdge; S_in = S.y >= cEdge; }
            }

            if (E_in) {
                if (!S_in) {
                    double t;
                    if (isVertical) t = (cEdge - S.x) / (E.x - S.x);
                    else            t = (cEdge - S.y) / (E.y - S.y);
                    output.push_back({ S.x + t * (E.x - S.x),
                                       S.y + t * (E.y - S.y) });
                }
                output.push_back(E);
            } else if (S_in) {
                double t;
                if (isVertical) t = (cEdge - S.x) / (E.x - S.x);
                else            t = (cEdge - S.y) / (E.y - S.y);
                output.push_back({ S.x + t * (E.x - S.x),
                                   S.y + t * (E.y - S.y) });
            }
            S = E;
        }
    };

    clipEdge(xMin, true,  false);   // Left
    clipEdge(xMax, true,  true);    // Right
    clipEdge(yMin, false, false);   // Top
    clipEdge(yMax, false, true);    // Bottom

    return output;
}

// ============================================================================
// DrizzleStacking -- Stack finalisation
// ============================================================================

void DrizzleStacking::finalizeStack(
    const std::vector<double>& accum,
    const std::vector<double>& weightAccum,
    ImageBuffer& output)
{
    const int width      = output.width();
    const int height     = output.height();
    const int channels   = output.channels();
    const size_t pixelCount = static_cast<size_t>(width) * height;
    float* data = output.data().data();

    #pragma omp parallel for
    for (int c = 0; c < channels; ++c) {
        for (size_t i = 0; i < pixelCount; ++i) {
            double w = weightAccum[i];

            // Output is stored in interleaved layout: pixel_i * channels + c
            size_t outIdx = i * channels + c;

            if (w > 0.0) {
                // Accumulators use planar layout: [ch0 | ch1 | ch2]
                double val = accum[c * pixelCount + i] / w;
                data[outIdx] = static_cast<float>(val);
            } else {
                data[outIdx] = 0.0f;
            }
        }
    }
}

// ============================================================================
// DrizzleStacking -- 2x nearest-neighbour upscale
// ============================================================================

ImageBuffer DrizzleStacking::upscale2x(const ImageBuffer& input)
{
    const int inW      = input.width();
    const int inH      = input.height();
    const int channels = input.channels();

    ImageBuffer output(inW * 2, inH * 2, channels);

    #pragma omp parallel for
    for (int y = 0; y < inH; ++y) {
        for (int x = 0; x < inW; ++x) {
            for (int c = 0; c < channels; ++c) {
                float val  = input.value(x, y, c);
                int   outY = y * 2;
                int   outX = x * 2;
                int   outW = inW * 2;

                auto setPixel = [&](int ox, int oy) {
                    output.data()[(oy * outW + ox) * channels + c] = val;
                };

                setPixel(outX,     outY);
                setPixel(outX + 1, outY);
                setPixel(outX,     outY + 1);
                setPixel(outX + 1, outY + 1);
            }
        }
    }

    return output;
}

// ============================================================================
// DrizzleStacking -- Registration scaling
// ============================================================================

RegistrationData DrizzleStacking::scaleRegistration(
    const RegistrationData& reg, double factor)
{
    RegistrationData scaled = reg;
    scaled.H[0][2] *= factor;
    scaled.H[1][2] *= factor;
    return scaled;
}

// ============================================================================
// MosaicFeathering -- Feather mask computation
// ============================================================================

std::vector<float> MosaicFeathering::computeFeatherMask(
    const ImageBuffer& input,
    const FeatherParams& params)
{
    initRampLUT();

    const int width  = input.width();
    const int height = input.height();
    const size_t pixelCount = static_cast<size_t>(width) * height;

    // Build a binary mask: 255 where there is content, 0 where void
    std::vector<uint8_t> binary(pixelCount);

    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float val  = input.value(x, y, 0);
            size_t idx = static_cast<size_t>(y) * width + x;
            binary[idx] = (val > 0.0f) ? 255 : 0;
        }
    }

    // Compute the distance transform at reduced resolution
    int outW = static_cast<int>(width  * params.maskScale);
    int outH = static_cast<int>(height * params.maskScale);
    if (outW < 1) outW = 1;
    if (outH < 1) outH = 1;

    std::vector<float> smallMask;
    computeDistanceMask(binary, width, height, outW, outH, smallMask);

    // Bilinearly upscale the small mask back to full resolution
    std::vector<float> mask(pixelCount);

    double scaleX = static_cast<double>(outW) / width;
    double scaleY = static_cast<double>(outH) / height;

    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {

            double sx = x * scaleX - 0.5;
            double sy = y * scaleY - 0.5;

            int x0 = static_cast<int>(std::floor(sx));
            int y0 = static_cast<int>(std::floor(sy));
            int x1 = x0 + 1;
            int y1 = y0 + 1;

            x0 = std::clamp(x0, 0, outW - 1);
            x1 = std::clamp(x1, 0, outW - 1);
            y0 = std::clamp(y0, 0, outH - 1);
            y1 = std::clamp(y1, 0, outH - 1);

            double fx = sx - std::floor(sx);
            double fy = sy - std::floor(sy);

            float v00 = smallMask[y0 * outW + x0];
            float v10 = smallMask[y0 * outW + x1];
            float v01 = smallMask[y1 * outW + x0];
            float v11 = smallMask[y1 * outW + x1];

            float val = static_cast<float>(
                v00 * (1 - fx) * (1 - fy) +
                v10 * fx       * (1 - fy) +
                v01 * (1 - fx) * fy +
                v11 * fx       * fy);

            mask[y * width + x] = params.smoothRamp ? smoothRamp(val) : val;
        }
    }

    return mask;
}

// ============================================================================
// MosaicFeathering -- Distance mask via OpenCV
// ============================================================================

void MosaicFeathering::computeDistanceMask(
    const std::vector<uint8_t>& binary,
    int width, int height,
    int outWidth, int outHeight,
    std::vector<float>& output)
{
    // OpenCV distance transform: computes the Euclidean distance from each
    // non-zero pixel to the nearest zero pixel.
    cv::Mat binMat(height, width, CV_8UC1, (void*)binary.data());
    cv::Mat distMat;
    cv::distanceTransform(binMat, distMat, cv::DIST_L2, cv::DIST_MASK_5);

    // Resize to the target resolution
    cv::Mat smallDist;
    cv::resize(distMat, smallDist, cv::Size(outWidth, outHeight),
               0, 0, cv::INTER_AREA);

    // Normalise distances so that 50 pixels of clearance maps to 1.0
    output.resize(static_cast<size_t>(outWidth) * outHeight);
    float* outPtr   = output.data();
    float* srcPtr   = reinterpret_cast<float*>(smallDist.data);
    size_t count    = static_cast<size_t>(outWidth) * outHeight;

    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        outPtr[i] = std::min(1.0f, srcPtr[i] / 50.0f);
    }
}

// ============================================================================
// MosaicFeathering -- Two-image blend
// ============================================================================

void MosaicFeathering::blendImages(
    const ImageBuffer& imgA, const std::vector<float>& maskA,
    const ImageBuffer& imgB, const std::vector<float>& maskB,
    ImageBuffer& output)
{
    const int width      = imgA.width();
    const int height     = imgA.height();
    const int channels   = imgA.channels();
    const size_t pixelCount = static_cast<size_t>(width) * height;

    output = ImageBuffer(width, height, channels);

    #pragma omp parallel for
    for (int c = 0; c < channels; ++c) {
        for (size_t i = 0; i < pixelCount; ++i) {
            float wA   = maskA[i];
            float wB   = maskB[i];
            float wSum = wA + wB;

            if (wSum > 0.0f) {
                float valA = imgA.data()[c * pixelCount + i];
                float valB = imgB.data()[c * pixelCount + i];
                output.data()[c * pixelCount + i] = (valA * wA + valB * wB) / wSum;
            } else {
                output.data()[c * pixelCount + i] = 0.0f;
            }
        }
    }
}

// ============================================================================
// LinearFitRejection
// ============================================================================

int LinearFitRejection::reject(
    float* stack, int N, float sigLow, float sigHigh,
    int* rejected, int& lowReject, int& highReject)
{
    if (N < 4) return N;

    int remaining = N;
    bool changed;
    std::vector<float> x(N), y(N);

    do {
        changed = false;

        // Sort and assign index values for the linear fit
        std::sort(stack, stack + remaining);
        for (int i = 0; i < remaining; ++i) {
            x[i] = static_cast<float>(i);
            y[i] = stack[i];
        }

        // Fit a line through the sorted values
        float intercept, slope;
        fitLine(x.data(), y.data(), remaining, intercept, slope);

        // Compute mean absolute deviation from the fitted line
        float sigma = 0.0f;
        for (int i = 0; i < remaining; ++i) {
            sigma += std::abs(stack[i] - (slope * i + intercept));
        }
        sigma /= remaining;

        // Flag outliers that deviate beyond the sigma thresholds
        for (int i = 0; i < remaining; ++i) {
            if (remaining <= 4) {
                rejected[i] = 0;
            } else {
                float expected = slope * i + intercept;
                if (expected - stack[i] > sigma * sigLow) {
                    rejected[i] = -1;
                    lowReject++;
                } else if (stack[i] - expected > sigma * sigHigh) {
                    rejected[i] = 1;
                    highReject++;
                } else {
                    rejected[i] = 0;
                }
            }
        }

        // Compact the array by removing rejected pixels
        int output = 0;
        for (int i = 0; i < remaining; ++i) {
            if (rejected[i] == 0) {
                if (i != output) stack[output] = stack[i];
                output++;
            }
        }

        changed   = (output != remaining);
        remaining = output;

    } while (changed && remaining > 3);

    return remaining;
}

void LinearFitRejection::fitLine(const float* x, const float* y, int N,
                                  float& intercept, float& slope)
{
    // Ordinary least-squares linear regression
    float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumX2 = 0.0f;

    for (int i = 0; i < N; ++i) {
        sumX  += x[i];
        sumY  += y[i];
        sumXY += x[i] * y[i];
        sumX2 += x[i] * x[i];
    }

    float denom = N * sumX2 - sumX * sumX;
    if (std::abs(denom) < 1e-10f) {
        slope     = 0.0f;
        intercept = sumY / N;
    } else {
        slope     = (N * sumXY - sumX * sumY) / denom;
        intercept = (sumY - slope * sumX) / N;
    }
}

// ============================================================================
// GESDTRejection
// ============================================================================

float GESDTRejection::grubbsStat(const float* data, int N, int& maxIndex)
{
    // Compute sample mean and standard deviation
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) sum += data[i];
    float mean = sum / N;

    float sumSq = 0.0f;
    for (int i = 0; i < N; ++i) sumSq += (data[i] - mean) * (data[i] - mean);
    float sd = std::sqrt(sumSq / (N - 1));

    if (sd < 1e-10f) {
        maxIndex = 0;
        return 0.0f;
    }

    // Data is sorted; check only the extremes for maximum deviation
    float devLow  = mean - data[0];
    float devHigh = data[N - 1] - mean;

    if (devHigh > devLow) {
        maxIndex = N - 1;
        return devHigh / sd;
    } else {
        maxIndex = 0;
        return devLow / sd;
    }
}

int GESDTRejection::reject(
    float* stack, int N, int maxOutliers,
    const float* criticalValues,
    int* rejected, int& lowReject, int& highReject)
{
    if (N < 4) return N;

    std::sort(stack, stack + N);
    float median = stack[N / 2];

    // Working copy for iterative removal
    std::vector<float> work(stack, stack + N);
    std::vector<ESDOutlier> outliers(maxOutliers);

    int coldCount = 0;
    for (int iter = 0; iter < maxOutliers; ++iter) {
        int size = N - iter;
        if (size < 4) break;

        int maxIndex;
        float gStat = grubbsStat(work.data(), size, maxIndex);

        outliers[iter].isOutlier    = (gStat > criticalValues[iter]);
        outliers[iter].value        = work[maxIndex];
        outliers[iter].originalIndex = (maxIndex == 0) ? coldCount++ : maxIndex;

        // Remove the flagged element from the working array
        for (int i = maxIndex; i < size - 1; ++i) {
            work[i] = work[i + 1];
        }
    }

    // Propagate outlier flags back to the sorted stack
    std::fill(rejected, rejected + N, 0);
    for (int i = 0; i < maxOutliers; ++i) {
        if (outliers[i].isOutlier) {
            for (int j = 0; j < N; ++j) {
                if (std::abs(stack[j] - outliers[i].value) < 1e-10f &&
                    rejected[j] == 0) {
                    rejected[j] = (outliers[i].value < median) ? -1 : 1;
                    if (rejected[j] < 0) lowReject++;
                    else                 highReject++;
                    break;
                }
            }
        }
    }

    // Compact
    int output = 0;
    for (int i = 0; i < N; ++i) {
        if (rejected[i] == 0) {
            if (i != output) stack[output] = stack[i];
            output++;
        }
    }

    return output;
}

void GESDTRejection::computeCriticalValues(
    int N, double alpha, int maxOutliers,
    std::vector<float>& output)
{
    output.resize(maxOutliers);

    for (int i = 0; i < maxOutliers; ++i) {
        int n = N - i;
        if (n < 4) {
            output[i] = 1e10f;   // Effectively disable rejection
            continue;
        }

        // Approximate the t critical value using the normal distribution
        double p = 1.0 - alpha / (2.0 * n);

        double tCrit = 1.645;                  // Default ~0.95
        if      (p > 0.99)  tCrit = 2.576;
        else if (p > 0.975) tCrit = 1.96;

        // Grubbs critical value formula
        double gCrit = ((n - 1) / std::sqrt(static_cast<double>(n))) *
                        std::sqrt(tCrit * tCrit / (n - 2 + tCrit * tCrit));

        output[i] = static_cast<float>(gCrit);
    }
}

// ============================================================================
// DrizzleStacking -- Kernel LUT
// ============================================================================

void DrizzleStacking::initKernel(DrizzleKernelType type, double param)
{
    m_currentKernel = type;
    if (type == DrizzleKernelType::Point) return;

    m_kernelLUT.resize(LUT_SIZE + 1);

    // Determine the maximum radius covered by the LUT
    double maxRadius = 2.0;
    if (type == DrizzleKernelType::Lanczos)
        maxRadius = (param > 0.0) ? param : 3.0;
    else if (type == DrizzleKernelType::Gaussian)
        maxRadius = 3.0;   // 3-sigma cutoff

    m_lutScale = static_cast<float>(LUT_SIZE) / static_cast<float>(maxRadius);

    for (int i = 0; i <= LUT_SIZE; ++i) {
        double x   = static_cast<double>(i) / m_lutScale;
        double val = 0.0;

        if (type == DrizzleKernelType::Gaussian) {
            double sigma = (param > 0.0) ? param : 1.0;
            val = std::exp(-(x * x) / (2.0 * sigma * sigma));

        } else if (type == DrizzleKernelType::Lanczos) {
            double a = (param > 0.0) ? param : 3.0;
            if (x < 1e-9)       val = 1.0;
            else if (x >= a)    val = 0.0;
            else {
                double pi_x = 3.14159265358979323846 * x;
                val = (a * std::sin(pi_x) * std::sin(pi_x / a)) / (pi_x * pi_x);
            }
        }

        m_kernelLUT[i] = static_cast<float>(val);
    }
}

float DrizzleStacking::getKernelWeight(double dx, double dy) const
{
    if (m_currentKernel == DrizzleKernelType::Point) return 1.0f;

    // Separable kernel: W(x,y) = K(|x|) * K(|y|)
    auto lookup = [&](double d) -> float {
        d = std::abs(d);
        float idx = static_cast<float>(d) * m_lutScale;
        int   i   = static_cast<int>(idx);

        if (i >= LUT_SIZE) return 0.0f;

        // Linear interpolation between adjacent LUT entries
        float t = idx - i;
        return m_kernelLUT[i] * (1.0f - t) + m_kernelLUT[i + 1] * t;
    };

    return lookup(dx) * lookup(dy);
}

// ============================================================================
// DrizzleStacking -- Stateful API
// ============================================================================

void DrizzleStacking::initialize(int inputWidth, int inputHeight, int channels,
                                 const DrizzleParams& params,
                                 double offsetX, double offsetY)
{
    m_params    = params;
    m_channels  = channels;
    m_offsetX   = offsetX;
    m_offsetY   = offsetY;
    m_outWidth  = static_cast<int>(inputWidth  * params.scaleFactor);
    m_outHeight = static_cast<int>(inputHeight * params.scaleFactor);

    size_t outPixels = static_cast<size_t>(m_outWidth) * m_outHeight;

    // Zero-initialise the accumulators
    m_accum.assign(outPixels * channels, 0.0);
    m_weightAccum.assign(outPixels, 0.0);

    // Build kernel LUT if a non-point kernel is requested
    initKernel(static_cast<DrizzleKernelType>(params.kernelType));
}

void DrizzleStacking::addImage(
    const ImageBuffer& img,
    const RegistrationData& reg,
    const std::vector<float>& /*weights*/,
    const float* /*rejectionMap*/)
{
    if (m_accum.empty()) return;   // Not initialised

    if (m_params.fastMode) {
        fastDrizzleFrame(img, reg, m_accum, m_weightAccum,
                         m_outWidth, m_outHeight, m_params,
                         m_offsetX, m_offsetY);
    } else {
        drizzleFrame(img, reg, m_accum, m_weightAccum,
                     m_outWidth, m_outHeight, m_params,
                     m_offsetX, m_offsetY);
    }
}

bool DrizzleStacking::resolve(ImageBuffer& output)
{
    if (m_accum.empty()) return false;

    output = ImageBuffer(m_outWidth, m_outHeight, m_channels);
    finalizeStack(m_accum, m_weightAccum, output);
    return true;
}

} // namespace Stacking