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
// DrizzleStacking -- Coordinate mapping and algorithms
// ============================================================================

Stacking::DrizzleMap DrizzleStacking::computeDrizzleMap(
    const ImageBuffer& input,
    const RegistrationData& reg,
    int outputWidth, int outputHeight,
    double scale, double offsetX, double offsetY)
{
    const int inW = input.width();
    const int inH = input.height();
    Stacking::DrizzleMap result;

    result.width  = inW;
    result.height = inH;
    result.xMap.resize(static_cast<size_t>(inW) * inH);
    result.yMap.resize(static_cast<size_t>(inW) * inH);

    double H[3][3];
    reg.getDrizzleMatrix(scale, offsetX, offsetY, H);

    #pragma omp parallel for

    for (int y = 0; y < inH; ++y) {
        for (int x = 0; x < inW; ++x) {
            float ox, oy;
            projectPoint(H, static_cast<double>(x), static_cast<double>(y), ox, oy);
            
            size_t idx = static_cast<size_t>(y) * inW + x;
            result.xMap[idx] = ox;
            result.yMap[idx] = oy;
        }
    }

    // Unused but kept for validation if needed
    (void)outputWidth;
    (void)outputHeight;

    return result;
}


bool DrizzleStacking::interpolatePoint(
    const Stacking::DrizzleMap& map, float xin, float yin,
    float& xout, float& yout)
{
    // Bilinear fallback for arbitrary points
    int i0 = static_cast<int>(std::floor(xin));
    int j0 = static_cast<int>(std::floor(yin));

    if (i0 < 0 || i0 >= map.width - 1 || j0 < 0 || j0 >= map.height - 1)
        return false;

    float fx = xin - i0;
    float fy = yin - j0;

    auto getX = [&](int ix, int iy) { return map.xMap[static_cast<size_t>(iy) * map.width + ix]; };
    auto getY = [&](int ix, int iy) { return map.yMap[static_cast<size_t>(iy) * map.width + ix]; };

    float f00 = getX(i0, j0), g00 = getY(i0, j0);
    float f10 = getX(i0 + 1, j0), g10 = getY(i0 + 1, j0);
    float f01 = getX(i0, j0 + 1), g01 = getY(i0, j0 + 1);
    float f11 = getX(i0 + 1, j0 + 1), g11 = getY(i0 + 1, j0 + 1);

    xout = f00 * (1 - fx) * (1 - fy) + f10 * fx * (1 - fy) + f01 * (1 - fx) * fy + f11 * fx * fy;
    yout = g00 * (1 - fx) * (1 - fy) + g10 * fx * (1 - fy) + g01 * (1 - fx) * fy + g11 * fx * fy;

    return true;
}

bool DrizzleStacking::interpolateFourPoints(
    const Stacking::DrizzleMap& map, int i, int j,
    float h, float xout[4], float yout[4])
{
    // Correct vertex summation for a 3x3 neighborhood.
    if (i < 1 || i >= map.width - 1 || j < 1 || j >= map.height - 1)
        return false;

    float f[3][3], g[3][3];
    for (int jj = 0; jj <= 2; ++jj) {
        for (int ii = 0; ii <= 2; ++ii) {
            int ix = i - 1 + ii;
            int iy = j - 1 + jj;
            size_t idx = static_cast<size_t>(iy) * map.width + ix;
            
            float fac;
            if (ii == 1 && jj == 1) fac = (1.0f - h) * (1.0f - h);
            else if (ii == 1 || jj == 1) fac = (1.0f - h) * h;
            else fac = h * h;

            f[ii][jj] = map.xMap[idx] * fac;
            g[ii][jj] = map.yMap[idx] * fac;
        }
    }

    // Order in y: +h, +h, -h, -h; order in x: -h, +h, +h, -h
    // This correctly handles the local perspective distortion.
    xout[0] = f[0][1] + f[1][1] + f[0][2] + f[1][2];
    yout[0] = g[0][1] + g[1][1] + g[0][2] + g[1][2];

    xout[1] = f[1][1] + f[2][1] + f[1][2] + f[2][2];
    yout[1] = g[1][1] + g[2][1] + g[1][2] + g[2][2];

    xout[2] = f[1][0] + f[2][0] + f[1][1] + f[2][1];
    yout[2] = g[1][0] + g[2][0] + g[1][1] + g[2][1];

    xout[3] = f[0][0] + f[1][0] + f[0][1] + f[1][1];
    yout[3] = g[0][0] + g[1][0] + g[0][1] + g[1][1];

    return true;
}

float DrizzleStacking::sgarea(float x1, float y1, float x2, float y2) {
    float xlo, xhi, ylo, yhi, xtop;
    float dx, dy, det, sgn_dx;

    dx = x2 - x1;
    dy = y2 - y1;

    // Trap vertical line
    if (dx == 0.0f) return 0.0f;

    if (dx < 0.0f) {
        sgn_dx = -1.0f; xlo = x2; xhi = x1;
    } else {
        sgn_dx = 1.0f; xlo = x1; xhi = x2;
    }

    if (xlo >= 1.0f || xhi <= 0.0f) return 0.0f;

    xlo = std::max(xlo, 0.0f);
    xhi = std::min(xhi, 1.0f);

    float slope = dy / dx;
    ylo = y1 + slope * (xlo - x1);
    yhi = y1 + slope * (xhi - x1);

    // Trap segment entirely below axis
    if (ylo <= 0.0f && yhi <= 0.0f) return 0.0f;

    // Line segment entirely above square
    if (ylo >= 1.0f && yhi >= 1.0f) {
        return sgn_dx * (xhi - xlo);
    }

    det = x1 * y2 - y1 * x2;

    // Adjust bounds if segment crosses axis
    if (ylo < 0.0f) {
        ylo = 0.0f;
        xlo = det / dy;
    }
    if (yhi < 0.0f) {
        yhi = 0.0f;
        xhi = det / dy;
    }

    if (ylo <= 1.0f) {
        if (yhi <= 1.0f) {
            // Segment entirely within square
            return sgn_dx * 0.5f * (xhi - xlo) * (yhi + ylo);
        }
        // Crosses top
        xtop = (dx + det) / dy;
        return sgn_dx * (0.5f * (xtop - xlo) * (1.0f + ylo) + xhi - xtop);
    }

    xtop = (dx + det) / dy;
    return sgn_dx * (0.5f * (xhi - xtop) * (1.0f + yhi) + xtop - xlo);
}


float DrizzleStacking::boxer(float is, float js, const float x[4], const float y[4]) {
    float area = 0.0f;
    float tx[4], ty[4];
    float x_off = is - 0.5f;
    float y_off = js - 0.5f;

    for (int i = 0; i < 4; ++i) {
        tx[i] = x[i] - x_off;
        ty[i] = y[i] - y_off;
    }

    for (int i = 0; i < 4; ++i) {
        int next = (i + 1) % 4;
        area += sgarea(tx[i], ty[i], tx[next], ty[next]);
    }

    return area;
}


int DrizzleStacking::getCFAChannel(int x, int y, const QString& pattern) {
    if (pattern.isEmpty()) return 0;
    // Standard patterns: "RGGB", "BGGR", "GBRG", "GRBG"
    char p = pattern.toUpper().at((y % 2) * 2 + (x % 2)).toLatin1();
    if (p == 'R') return 0;
    if (p == 'G') return 1;
    if (p == 'B') return 2;
    return 0;
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
    double offsetX, double offsetY,
    const std::vector<float>& weights,
    const float* rejectionMap)
{
    const double scale = params.scaleFactor;
    const double drop  = params.dropSize;
    const int inW      = input.width();
    const int inH      = input.height();
    const int inChans  = input.channels();
    const size_t outPixels = static_cast<size_t>(outputWidth) * outputHeight;

    // Use unified Drizzle Map (Center-to-Center Matrix Projection)
    Stacking::DrizzleMap dmap = computeDrizzleMap(input, reg, outputWidth, outputHeight, scale, offsetX, offsetY);
    if (!dmap.isValid()) return;

    // Shrunken pixel radius in input coordinates (pixfrac * 0.5)
    const float dh = static_cast<float>(drop * 0.5);

    #pragma omp parallel for
    for (int y = 0; y < inH; ++y) {
        for (int x = 0; x < inW; ++x) {
            // Outlier rejection
            if (rejectionMap && rejectionMap[y * inW + x] > 0.5f)
                continue;

            // Compute output quadrilateral vertices for the shrunken pixel
            float xout[4], yout[4];
            if (!interpolateFourPoints(dmap, x, y, dh, xout, yout))
                continue;

            double jacobian = 0.5 * ((xout[1] - xout[3]) * (yout[0] - yout[2]) - 
                                     (xout[0] - xout[2]) * (yout[1] - yout[3]));
            
            if (std::abs(jacobian) < 1e-9) continue;
            const double invJaco = 1.0 / jacobian;

            float minX = xout[0], maxX = xout[0], minY = yout[0], maxY = yout[0];
            for (int i = 1; i < 4; ++i) {
                if (xout[i] < minX) minX = xout[i];
                if (xout[i] > maxX) maxX = xout[i];
                if (yout[i] < minY) minY = yout[i];
                if (yout[i] > maxY) maxY = yout[i];
            }

            int ox0 = std::clamp(static_cast<int>(std::floor(minX + 0.5f)) - 1, 0, outputWidth - 1);
            int ox1 = std::clamp(static_cast<int>(std::ceil(maxX + 0.5f)) + 1, 0, outputWidth - 1);
            int oy0 = std::clamp(static_cast<int>(std::floor(minY + 0.5f)) - 1, 0, outputWidth - 1);
            int oy1 = std::clamp(static_cast<int>(std::ceil(maxY + 0.5f)) + 1, 0, outputHeight - 1);

            float frameWeight = (weights.size() > 0) ? weights[0] : 1.0f;
            bool isRawDriz = !params.bayerPattern.isEmpty() && inChans == 1;
            int cfaChan = isRawDriz ? getCFAChannel(x, y, params.bayerPattern) : 0;

            for (int oy = oy0; oy <= oy1; ++oy) {
                for (int ox = ox0; ox <= ox1; ++ox) {
                    float overlapArea = boxer(static_cast<float>(ox), static_cast<float>(oy), xout, yout);
                    if (std::abs(overlapArea) < 1e-6f) continue;

                    if (isRawDriz) {
                        float chanWeight = (cfaChan < static_cast<int>(weights.size())) ? weights[cfaChan] : frameWeight;
                        double finalWeight = (double)(overlapArea * chanWeight) * invJaco;
                        double val = static_cast<double>(input.value(x, y, 0)) * finalWeight;
                        size_t outIdx = static_cast<size_t>(oy) * outputWidth + ox;
                        #pragma omp atomic
                        accum[cfaChan * outPixels + outIdx] += val;
                        #pragma omp atomic
                        weightAccum[cfaChan * outPixels + outIdx] += finalWeight;
                    } else {
                        for (int c = 0; c < inChans; ++c) {
                            float chanWeight = (c < static_cast<int>(weights.size())) ? weights[c] : frameWeight;
                            double finalWeight = (double)(overlapArea * chanWeight) * invJaco;
                            double val = static_cast<double>(input.value(x, y, c)) * finalWeight;
                            size_t outIdx = static_cast<size_t>(oy) * outputWidth + ox;
                            #pragma omp atomic
                            accum[c * outPixels + outIdx] += val;
                            #pragma omp atomic
                            weightAccum[c * outPixels + outIdx] += finalWeight;
                        }
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
    double offsetX, double offsetY,
    const std::vector<float>& weights,
    const float* rejectionMap)
{
    const int w        = input.width();
    const int h        = input.height();
    const int channels = input.channels();
    const double scale = params.scaleFactor;
    const size_t outPixels = static_cast<size_t>(outputWidth) * outputHeight;

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Reject outliers 
            if (rejectionMap && rejectionMap[y * w + x] > 0.5f)
                continue;

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
                    float fw = (c < static_cast<int>(weights.size())) ? weights[c] : 1.0f;
                    double val = static_cast<double>(input.value(x, y, c)) * weight * fw;
                    #pragma omp atomic
                    accum[c * outPixels + idx] += val;
                    #pragma omp atomic
                    weightAccum[c * outPixels + idx] += weight * fw;
                }
            }
        }
    }
}

// ============================================================================
// DrizzleStacking -- Stack finalisation
// ============================================================================

void DrizzleStacking::finalizeStack(
    const std::vector<double>& accum,
    const std::vector<double>& weightAccum,
    ImageBuffer& output,
    int channels)
{
    const int width      = output.width();
    const int height     = output.height();
    const size_t pixelCount = static_cast<size_t>(width) * height;
    float* data = output.data().data();

    // Robust weighting
    #pragma omp parallel for
    for (int c = 0; c < channels; ++c) {
        for (size_t i = 0; i < pixelCount; ++i) {
            double w = weightAccum[c * pixelCount + i];
            size_t outIdx = i * channels + c; // Interleaved output

            if (w > 1e-12) {
                // val = Sum(val_i * weight_i) / Sum(weight_i)
                double val = accum[c * pixelCount + i] / w;
                data[outIdx] = std::clamp(static_cast<float>(val), 0.0f, 1.0f);
            } else {
                // Gap filling (interpolation) for dead pixels with zero weight.
                // Search in a 3x3 window for neighbors that do have weight.
                double sumVal = 0.0, sumW = 0.0;
                int    py = i / width, px = i % width;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int ny = py + dy, nx = px + dx;
                        if (ny < 0 || ny >= height || nx < 0 || nx >= width) continue;
                        
                        size_t nIdx = ny * width + nx;
                        double nw   = weightAccum[c * pixelCount + nIdx];
                        if (nw > 1e-12) {
                            sumVal += accum[c * pixelCount + nIdx] / nw;
                            sumW   += 1.0;
                        }
                    }
                }

                if (sumW > 0.0) {
                    data[outIdx] = static_cast<float>(sumVal / sumW);
                } else {
                    // Larger 5x5 fallback for persistent holes
                    double fVal = 0.0, fW = 0.0;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            int ny = py + dy, nx = px + dx;
                            if (ny < 0 || ny >= height || nx < 0 || nx >= width) continue;
                            size_t nIdx = ny * width + nx;
                            double nw   = weightAccum[c * pixelCount + nIdx];
                            if (nw > 1e-12) {
                                double dist = std::sqrt(dx*dx + dy*dy);
                                double weight = 1.0 / (1.0 + dist);
                                fVal += (accum[c * pixelCount + nIdx] / nw) * weight;
                                fW   += weight;
                            }
                        }
                    }
                    data[outIdx] = fW > 0.0 ? static_cast<float>(fVal / fW) : 0.0f;
                }
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
    m_offsetX   = offsetX;
    m_offsetY   = offsetY;
    m_outWidth  = static_cast<int>(std::round(inputWidth  * params.scaleFactor));
    m_outHeight = static_cast<int>(std::round(inputHeight * params.scaleFactor));

    // For Raw Drizzle (CFA), the output has 3 channels even if input has 1
    bool isRawDriz = !m_params.bayerPattern.isEmpty() && channels == 1;
    m_channels = isRawDriz ? 3 : channels;

    size_t outPixels = static_cast<size_t>(m_outWidth) * m_outHeight;

    // Zero-initialise the planar accumulators
    m_accum.assign(outPixels * m_channels, 0.0);
    m_weightAccum.assign(outPixels * m_channels, 0.0);

    // Build kernel LUT if requested
    initKernel(static_cast<DrizzleKernelType>(params.kernelType));
}

void DrizzleStacking::addImage(
    const ImageBuffer& img,
    const RegistrationData& reg,
    const std::vector<float>& weights,
    const float* rejectionMap)
{
    if (m_accum.empty()) return;   // Not initialised

    if (m_params.fastMode) {
        fastDrizzleFrame(img, reg, m_accum, m_weightAccum,
                         m_outWidth, m_outHeight, m_params,
                         m_offsetX, m_offsetY, weights, rejectionMap);
    } else {
        drizzleFrame(img, reg, m_accum, m_weightAccum,
                     m_outWidth, m_outHeight, m_params,
                     m_offsetX, m_offsetY, weights, rejectionMap);
    }
}

bool DrizzleStacking::resolve(ImageBuffer& output)
{
    if (m_accum.empty()) return false;

    output = ImageBuffer(m_outWidth, m_outHeight, m_channels);
    finalizeStack(m_accum, m_weightAccum, output, m_channels);
    return true;
}

} // namespace Stacking