#include "DrizzleStacking.h"
#include "Statistics.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

namespace Stacking {

// Ramp LUT for feathering
std::vector<float> MosaicFeathering::s_rampLUT;

void MosaicFeathering::initRampLUT() {
    if (!s_rampLUT.empty()) return;
    
    const int N = 1001;
    s_rampLUT.resize(N);
    
    for (int i = 0; i < N; i++) {
        float r = static_cast<float>(i) / 1000.0f;
        s_rampLUT[i] = r * r * r * (6.0f * r * r - 15.0f * r + 10.0f);
    }
}

// ============== DrizzleStacking ==============

DrizzleStacking::DrizzleWeight DrizzleStacking::computeWeightMap(
    const ImageBuffer& input,
    const RegistrationData& reg,
    int outputWidth, int outputHeight,
    const DrizzleParams& params)
{
    DrizzleWeight result;
    result.width = outputWidth;
    result.height = outputHeight;
    result.weight.resize(static_cast<size_t>(outputWidth) * outputHeight, 0.0f);
    
    double scale = params.scaleFactor;
    double drop = params.dropSize;
    
    int inW = input.width();
    int inH = input.height();
    
    // For each input pixel, compute its contribution to output pixels
    #pragma omp parallel for
    for (int y = 0; y < inH; y++) {
        for (int x = 0; x < inW; x++) {
            // 1. Define input pixel corners
            double px[] = {static_cast<double>(x), static_cast<double>(x)+1.0, 
                           static_cast<double>(x)+1.0, static_cast<double>(x)};
            double py[] = {static_cast<double>(y), static_cast<double>(y), 
                           static_cast<double>(y)+1.0, static_cast<double>(y)+1.0};
            
            // 2. Transform
            Polygon quad(4);
            double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
            
            for(int i=0; i<4; ++i) {
                QPointF pt = reg.transform(px[i], py[i]);
                double tx = pt.x();
                double ty = pt.y();
                quad[i] = {tx * scale, ty * scale};
            }
            
            // 3. Shrink
            if (drop < 1.0 - 1e-5) {
                quad = shrinkPolygon(quad, drop);
            }
            
            // 4. Bounding Box
            for(const auto& p : quad) {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }
            
            int x0 = static_cast<int>(std::floor(minX));
            int x1 = static_cast<int>(std::floor(maxX));
            int y0 = static_cast<int>(std::floor(minY));
            int y1 = static_cast<int>(std::floor(maxY));
            
            // Clamp
            x0 = std::max(0, x0);
            x1 = std::min(outputWidth - 1, x1);
            y0 = std::max(0, y0);
            y1 = std::min(outputHeight - 1, y1);
            
            // 5. Scan Output
            for (int oy = y0; oy <= y1; oy++) {
                for (int ox = x0; ox <= x1; ox++) {
                    double oxD = static_cast<double>(ox);
                    double oyD = static_cast<double>(oy);
                    
                    Polygon clipped = clipPolygon(quad, oxD, oyD, oxD+1.0, oyD+1.0);
                    double area = computePolygonArea(clipped);
                    
                    if (area > 1e-6) {
                        float weight = static_cast<float>(area);
                        size_t idx = static_cast<size_t>(oy) * outputWidth + ox;
                        
                        #pragma omp atomic
                        result.weight[idx] += weight;
                    }
                }
            }
        }
    }
    
    return result;
}

void DrizzleStacking::drizzleFrame(
    const ImageBuffer& input,
    const RegistrationData& reg,
    std::vector<double>& accum,
    std::vector<double>& weightAccum,
    int outputWidth, int outputHeight,
    const DrizzleParams& params)
{
    double scale = params.scaleFactor;
    double drop = params.dropSize;
    
    int inW = input.width();
    int inH = input.height();
    int channels = input.channels();
    size_t outPixels = static_cast<size_t>(outputWidth) * outputHeight;
    
    #pragma omp parallel for
    for (int y = 0; y < inH; y++) {
        for (int x = 0; x < inW; x++) {
            // 1. Define input pixel corners (integer coords are boundaries)
            double px[] = {static_cast<double>(x), static_cast<double>(x)+1.0, 
                           static_cast<double>(x)+1.0, static_cast<double>(x)};
            double py[] = {static_cast<double>(y), static_cast<double>(y), 
                           static_cast<double>(y)+1.0, static_cast<double>(y)+1.0};
            
            // 2. Transform to output space
            Polygon quad(4);
            double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
            
            for(int i=0; i<4; ++i) {
                // Note: reg transform might expect pixel centers or specific convention.
                // Assuming it handles coordinates linearly.
                QPointF pt = reg.transform(px[i], py[i]);
                double tx = pt.x();
                double ty = pt.y();
                quad[i] = {tx * scale, ty * scale};
            }
            
            // 3. Shrink polygon (simulate drop size)
            if (drop < 1.0 - 1e-5) {
                quad = shrinkPolygon(quad, drop);
            }
            
            // 4. Compute Bounding Box
            for(const auto& p : quad) {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }
            
            int x0 = static_cast<int>(std::floor(minX));
            int x1 = static_cast<int>(std::floor(maxX));
            int y0 = static_cast<int>(std::floor(minY));
            int y1 = static_cast<int>(std::floor(maxY));
            
            // Clamp to output
            x0 = std::max(0, x0);
            x1 = std::min(outputWidth - 1, x1);
            y0 = std::max(0, y0);
            y1 = std::min(outputHeight - 1, y1);
            
            // 5. Scan Output Pixels
            for (int oy = y0; oy <= y1; oy++) {
                for (int ox = x0; ox <= x1; ox++) {
                    // Clip quad against output pixel square
                    double oxD = static_cast<double>(ox);
                    double oyD = static_cast<double>(oy);
                    
                    Polygon clipped = clipPolygon(quad, oxD, oyD, oxD+1.0, oyD+1.0);
                    double area = computePolygonArea(clipped);
                    
                    if (area > 1e-6) {
                        float weight = static_cast<float>(area);
                        
                        // Apply Kernel Modulation if not Point
                        if (m_currentKernel != DrizzleKernelType::Point) {
                            // Map output pixel center back to input space? 
                            // Or Map Input Pixel Center to Output Space?
                            // Drizzle kernel is usually defined around the "drop" center in Output Space.
                            // The drop center is the transformed input pixel center.
                            
                            // Re-compute transformed center for this input pixel (x,y)
                            // We need to do this outside the loop for efficiency, but let's do it here for correctness first.
                            // Actually, 'quad' is the transformed polygon. Ideally its centroid is the center.
                            // But simpler: transform (x+0.5, y+0.5).
                            
                            double cxIn = static_cast<double>(x) + 0.5;
                            double cyIn = static_cast<double>(y) + 0.5;
                            QPointF centerOut = reg.transform(cxIn, cyIn);
                            double tx = centerOut.x() * scale;
                            double ty = centerOut.y() * scale;
                            
                            double centerX = static_cast<double>(ox) + 0.5;
                            double centerY = static_cast<double>(oy) + 0.5;
                            double dx = centerX - tx; 
                            double dy = centerY - ty;
                            
                            weight *= getKernelWeight(dx, dy);
                        }
                        
                         size_t idx = static_cast<size_t>(oy) * outputWidth + ox;
                         
                         for (int c = 0; c < channels; c++) {
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

// Helper Implementations

double DrizzleStacking::computePolygonArea(const Polygon& p) {
    double area = 0.0;
    size_t j = p.size() - 1;
    for (size_t i = 0; i < p.size(); i++) {
        area += (p[j].x + p[i].x) * (p[j].y - p[i].y);
        j = i;
    }
    return std::abs(area) * 0.5;
}

DrizzleStacking::Polygon DrizzleStacking::shrinkPolygon(const Polygon& p, double factor) {
    if (p.empty()) return p;
    
    // Compute Centroid
    double cx = 0, cy = 0;
    for(const auto& pt : p) {
        cx += pt.x;
        cy += pt.y;
    }
    cx /= p.size();
    cy /= p.size();
    
    Polygon out;
    out.reserve(p.size());
    for(const auto& pt : p) {
        out.push_back({
            cx + (pt.x - cx) * factor,
            cy + (pt.y - cy) * factor
        });
    }
    return out;
}

DrizzleStacking::Polygon DrizzleStacking::clipPolygon(const Polygon& subject, 
                                                      double xMin, double yMin, 
                                                      double xMax, double yMax) {
    if (subject.empty()) return subject;
    
    Polygon output = subject;
    
    // Clip against 4 edges
    
    auto clipEdge = [&](double cEdge, bool isVertical, bool isMax) {
        Polygon input = output;
        output.clear();
        
        if (input.empty()) return;
        
        Point S = input.back();
        
        for (const auto& E : input) {
            // Check if points are inside
            bool E_in, S_in;
            
            if (isVertical) { // Clipping against X
                if (isMax) { E_in = E.x <= cEdge; S_in = S.x <= cEdge; }
                else       { E_in = E.x >= cEdge; S_in = S.x >= cEdge; }
            } else { // Clipping against Y
                if (isMax) { E_in = E.y <= cEdge; S_in = S.y <= cEdge; }
                else       { E_in = E.y >= cEdge; S_in = S.y >= cEdge; }
            }
            
            if (E_in) {
                if (!S_in) {
                    // Compute intersection
                    double t;
                    if (isVertical) t = (cEdge - S.x) / (E.x - S.x);
                    else            t = (cEdge - S.y) / (E.y - S.y);
                    
                    output.push_back({S.x + t * (E.x - S.x), S.y + t * (E.y - S.y)});
                }
                output.push_back(E);
            } else if (S_in) {
                double t;
                if (isVertical) t = (cEdge - S.x) / (E.x - S.x);
                else            t = (cEdge - S.y) / (E.y - S.y);
                
                output.push_back({S.x + t * (E.x - S.x), S.y + t * (E.y - S.y)});
            }
            S = E;
        }
    };
    
    // Left (xMin)
    clipEdge(xMin, true, false);
    // Right (xMax)
    clipEdge(xMax, true, true);
    // Top (yMin)
    clipEdge(yMin, false, false);
    // Bottom (yMax)
    clipEdge(yMax, false, true);
    
    return output;
}

void DrizzleStacking::finalizeStack(
    const std::vector<double>& accum,
    const std::vector<double>& weightAccum,
    ImageBuffer& output)
{
    int width = output.width();
    int height = output.height();
    int channels = output.channels();
    size_t pixelCount = static_cast<size_t>(width) * height;
    
    float* data = output.data().data();
    
    #pragma omp parallel for
    for (int c = 0; c < channels; c++) {
        for (size_t i = 0; i < pixelCount; i++) {
            double w = weightAccum[i];
            
            // Interleaved Output Index
            // i is pixel index (0..W*H-1)
            // outIdx = i * channels + c
            size_t outIdx = i * channels + c;
            
            if (w > 0) {
                // accum uses Planar layout: [Channel0][Channel1][Channel2]
                double val = accum[c * pixelCount + i] / w;
                data[outIdx] = static_cast<float>(val);
            } else {
                data[outIdx] = 0.0f;
            }
        }
    }
}

ImageBuffer DrizzleStacking::upscale2x(const ImageBuffer& input) {
    int inW = input.width();
    int inH = input.height();
    int channels = input.channels();
    
    ImageBuffer output(inW * 2, inH * 2, channels);
    
    // float* outData = output.data().data(); // Not used directly to avoid confusion
    
    #pragma omp parallel for
    for (int y = 0; y < inH; y++) {
        for (int x = 0; x < inW; x++) {
            for (int c = 0; c < channels; c++) {
                float val = input.value(x, y, c);
                // Map to 2x2 block
                // (2x, 2y), (2x+1, 2y), (2x, 2y+1), (2x+1, 2y+1)
                
                int outY = y * 2;
                int outX = x * 2;
                int outW = inW * 2;
                
                // Interleaved Indexing
                // Idx = (y * W + x) * ch + c
                
                auto setPixel = [&](int ox, int oy) {
                    output.data()[(oy * outW + ox) * channels + c] = val;
                };
                
                setPixel(outX, outY);
                setPixel(outX + 1, outY);
                setPixel(outX, outY + 1);
                setPixel(outX + 1, outY + 1);
            }
        }
    }
    
    return output;
}

RegistrationData DrizzleStacking::scaleRegistration(const RegistrationData& reg, double factor) {
    RegistrationData scaled = reg;
    scaled.H[0][2] *= factor;
    scaled.H[1][2] *= factor;
    return scaled;
}

// ============== MosaicFeathering ==============

std::vector<float> MosaicFeathering::computeFeatherMask(
    const ImageBuffer& input,
    const FeatherParams& params)
{
    initRampLUT();
    
    int width = input.width();
    int height = input.height();
    size_t pixelCount = static_cast<size_t>(width) * height;
    
    // Create binary mask from non-zero pixels
    std::vector<uint8_t> binary(pixelCount);
    
    #pragma omp parallel for
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float val = input.value(x, y, 0);
            size_t idx = static_cast<size_t>(y) * width + x;
            binary[idx] = (val > 0.0f) ? 255 : 0;
        }
    }
    
    // Compute downscaled distance mask
    int outW = static_cast<int>(width * params.maskScale);
    int outH = static_cast<int>(height * params.maskScale);
    if (outW < 1) outW = 1;
    if (outH < 1) outH = 1;
    
    std::vector<float> smallMask;
    computeDistanceMask(binary, width, height, outW, outH, smallMask);
    
    // Upscale back to full size
    std::vector<float> mask(pixelCount);
    
    double scaleX = static_cast<double>(outW) / width;
    double scaleY = static_cast<double>(outH) / height;
    
    #pragma omp parallel for
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Bilinear interpolation from small mask
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
                v10 * fx * (1 - fy) +
                v01 * (1 - fx) * fy +
                v11 * fx * fy
            );
            
            mask[y * width + x] = params.smoothRamp ? smoothRamp(val) : val;
        }
    }
    
    return mask;
}

void MosaicFeathering::computeDistanceMask(
    const std::vector<uint8_t>& binary,
    int width, int height,
    int outWidth, int outHeight,
    std::vector<float>& output)
{
    // Use OpenCV's efficient O(N) Distance Transform
    // Map existing binary data to cv::Mat
    // binary: 255 = content, 0 = void.
    // distanceTransform calculates distance to nearest ZERO pixel.
    
    cv::Mat binMat(height, width, CV_8UC1, (void*)binary.data());
    cv::Mat distMat;
    
    // DIST_L2 = Euclidean distance
    // DIST_MASK_5 = 5x5 mask for precise distance
    cv::distanceTransform(binMat, distMat, cv::DIST_L2, cv::DIST_MASK_5);
    
    // Resize to output size
    cv::Mat smallDist;
    cv::resize(distMat, smallDist, cv::Size(outWidth, outHeight), 0, 0, cv::INTER_AREA);
    
    // Normalize and copy to float vector
    // Existing logic normalized such that 50px = 1.0
    // We should maintain this "ramp width" logic.
    // The previous implementation used a fixed 50 pixel ramp *in original scale*?
    // "float normalizedDist = static_cast<float>(minDist) / 50.0f;"
    // Yes.
    
    output.resize(static_cast<size_t>(outWidth) * outHeight);
    float* outIdx = output.data();
    
    // Since we resized DISTANCE map, the values are still in "Original Pixels" units (roughly).
    // Actually cv::resize interpolates values. If we shrink image 2x, values are still 0..MaxDist.
    // So threshold of 50.0f still applies to the values inside smallDist.
    
    float* sPtr = (float*)smallDist.data;
    size_t count = static_cast<size_t>(outWidth) * outHeight;
    
    #pragma omp parallel for
    for (size_t i = 0; i < count; ++i) {
        float d = sPtr[i];
        outIdx[i] = std::min(1.0f, d / 50.0f);
    }
}

void MosaicFeathering::blendImages(
    const ImageBuffer& imgA, const std::vector<float>& maskA,
    const ImageBuffer& imgB, const std::vector<float>& maskB,
    ImageBuffer& output)
{
    int width = imgA.width();
    int height = imgA.height();
    int channels = imgA.channels();
    
    output = ImageBuffer(width, height, channels);
    
    size_t pixelCount = static_cast<size_t>(width) * height;
    
    #pragma omp parallel for
    for (int c = 0; c < channels; c++) {
        for (size_t i = 0; i < pixelCount; i++) {
            float wA = maskA[i];
            float wB = maskB[i];
            float wSum = wA + wB;
            
            if (wSum > 0) {
                float valA = imgA.data()[c * pixelCount + i];
                float valB = imgB.data()[c * pixelCount + i];
                output.data()[c * pixelCount + i] = (valA * wA + valB * wB) / wSum;
            } else {
                output.data()[c * pixelCount + i] = 0.0f;
            }
        }
    }
}

// ============== LinearFitRejection ==============

int LinearFitRejection::reject(
    float* stack, int N, float sigLow, float sigHigh,
    int* rejected, int& lowReject, int& highReject)
{
    if (N < 4) return N;
    
    int remaining = N;
    bool changed;
    
    // Prepare x values (indices)
    std::vector<float> x(N), y(N);
    
    do {
        changed = false;
        
        // Sort and prepare data
        std::sort(stack, stack + remaining);
        for (int i = 0; i < remaining; i++) {
            x[i] = static_cast<float>(i);
            y[i] = stack[i];
        }
        
        // Fit line
        float intercept, slope;
        fitLine(x.data(), y.data(), remaining, intercept, slope);
        
        // Compute sigma (mean absolute deviation from line)
        float sigma = 0.0f;
        for (int i = 0; i < remaining; i++) {
            sigma += std::abs(stack[i] - (slope * i + intercept));
        }
        sigma /= remaining;
        
        // Check for rejections
        for (int i = 0; i < remaining; i++) {
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
        
        // Compact array
        int output = 0;
        for (int i = 0; i < remaining; i++) {
            if (rejected[i] == 0) {
                if (i != output) stack[output] = stack[i];
                output++;
            }
        }
        
        changed = (output != remaining);
        remaining = output;
        
    } while (changed && remaining > 3);
    
    return remaining;
}

void LinearFitRejection::fitLine(const float* x, const float* y, int N,
                                  float& intercept, float& slope)
{
    // Simple linear regression
    float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    
    for (int i = 0; i < N; i++) {
        sumX += x[i];
        sumY += y[i];
        sumXY += x[i] * y[i];
        sumX2 += x[i] * x[i];
    }
    
    float denom = N * sumX2 - sumX * sumX;
    if (std::abs(denom) < 1e-10f) {
        slope = 0;
        intercept = sumY / N;
    } else {
        slope = (N * sumXY - sumX * sumY) / denom;
        intercept = (sumY - slope * sumX) / N;
    }
}

// ============== GESDTRejection ==============

float GESDTRejection::grubbsStat(const float* data, int N, int& maxIndex) {
    // Compute mean and std
    float sum = 0;
    for (int i = 0; i < N; i++) sum += data[i];
    float mean = sum / N;
    
    float sumSq = 0;
    for (int i = 0; i < N; i++) sumSq += (data[i] - mean) * (data[i] - mean);
    float sd = std::sqrt(sumSq / (N - 1));
    
    if (sd < 1e-10f) {
        maxIndex = 0;
        return 0.0f;
    }
    
    // Data should be sorted; check ends for max deviation
    float devLow = mean - data[0];
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
    
    // Sort data
    std::sort(stack, stack + N);
    
    // Compute median for classifying rejections
    float median = stack[N / 2];
    
    // Working copy
    std::vector<float> work(stack, stack + N);
    std::vector<GESDTRejection::ESDOutlier> outliers(maxOutliers);
    
    int coldCount = 0;
    for (int iter = 0; iter < maxOutliers; iter++) {
        int size = N - iter;
        if (size < 4) break;
        
        int maxIndex;
        float gStat = grubbsStat(work.data(), size, maxIndex);
        
        outliers[iter].isOutlier = (gStat > criticalValues[iter]);
        outliers[iter].value = work[maxIndex];
        outliers[iter].originalIndex = (maxIndex == 0) ? coldCount++ : maxIndex;
        
        // Remove element
        for (int i = maxIndex; i < size - 1; i++) {
            work[i] = work[i + 1];
        }
    }
    
    // Mark rejections
    std::fill(rejected, rejected + N, 0);
    for (int i = 0; i < maxOutliers; i++) {
        if (outliers[i].isOutlier) {
            // Find original index in sorted stack
            for (int j = 0; j < N; j++) {
                if (std::abs(stack[j] - outliers[i].value) < 1e-10f && rejected[j] == 0) {
                    rejected[j] = (outliers[i].value < median) ? -1 : 1;
                    if (rejected[j] < 0) lowReject++;
                    else highReject++;
                    break;
                }
            }
        }
    }
    
    // Compact
    int output = 0;
    for (int i = 0; i < N; i++) {
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
    
    for (int i = 0; i < maxOutliers; i++) {
        int n = N - i;
        if (n < 4) {
            output[i] = 1e10f; // No rejection possible
            continue;
        }
        
        // t critical value (approximation using normal for large N)
        double p = 1.0 - alpha / (2.0 * n);
        
        // Approximate t_{p,n-2} using normal approximation
        // For proper implementation, use boost::math or GSL
        double tCrit = 1.645; // Default for ~0.95
        if (p > 0.99) tCrit = 2.576;
        else if (p > 0.975) tCrit = 1.96;
        
        // Grubbs critical value
        double gCrit = ((n - 1) / std::sqrt(static_cast<double>(n))) * 
                       std::sqrt(tCrit * tCrit / (n - 2 + tCrit * tCrit));
        
        output[i] = static_cast<float>(gCrit);
    }
}

// ============== Drizzle Kernel LUT ==============

void DrizzleStacking::initKernel(DrizzleKernelType type, double param) {
    m_currentKernel = type;
    if (type == DrizzleKernelType::Point) return;
    
    m_kernelLUT.resize(LUT_SIZE + 1);
    
    // Define max radius for LUT
    double maxRadius = 2.0;
    if (type == DrizzleKernelType::Lanczos) maxRadius = (param > 0) ? param : 3.0; // param is 'a'
    else if (type == DrizzleKernelType::Gaussian) maxRadius = 3.0; // 3 sigma?
    
    m_lutScale = static_cast<float>(LUT_SIZE) / static_cast<float>(maxRadius);
    
    for (int i = 0; i <= LUT_SIZE; ++i) {
        double x = static_cast<double>(i) / m_lutScale;
        double val = 0.0;
        
        if (type == DrizzleKernelType::Gaussian) {
             // param = sigma. Default to 0.5?
             double sigma = (param > 0) ? param : 1.0;
             // Gaussian: exp(-x^2 / (2*sigma^2))
             // Normalize peak to 1.0 (weight normalization happens via weightAccum)
             val = std::exp(-(x*x) / (2.0 * sigma * sigma));
        } else if (type == DrizzleKernelType::Lanczos) {
            // Lanczos-a
            double a = (param > 0) ? param : 3.0;
            if (x < 1e-9) val = 1.0;
            else if (x >= a) val = 0.0;
            else {
                double pi_x = 3.14159265359 * x;
                val = (a * std::sin(pi_x) * std::sin(pi_x / a)) / (pi_x * pi_x);
            }
        }
        
        m_kernelLUT[i] = static_cast<float>(val);
    }
}

float DrizzleStacking::getKernelWeight(double dx, double dy) const {
    if (m_currentKernel == DrizzleKernelType::Point) return 1.0f;
    
    // Separable kernel: W(x,y) = K(x) * K(y)
    // Works for Gaussian and Lanczos
    
    auto lookup = [&](double d) -> float {
        d = std::abs(d);
        float idx = static_cast<float>(d) * m_lutScale;
        int i = static_cast<int>(idx);
        
        if (i >= LUT_SIZE) return 0.0f;
        
        // Linear interpolation
        float t = idx - i;
        return m_kernelLUT[i] * (1.0f - t) + m_kernelLUT[i+1] * t;
    };
    
    return lookup(dx) * lookup(dy);
}

// ============== Stateful API Implementation ==============

void DrizzleStacking::initialize(int inputWidth, int inputHeight, int channels, const DrizzleParams& params) {
    m_params = params;
    m_channels = channels;
    
    // Compute output dimensions
    m_outWidth = static_cast<int>(inputWidth * params.scaleFactor);
    m_outHeight = static_cast<int>(inputHeight * params.scaleFactor);
    
    size_t outPixels = static_cast<size_t>(m_outWidth) * m_outHeight;
    
    // Allocate allocators (zero initialized)
    m_accum.assign(outPixels * channels, 0.0);
    m_weightAccum.assign(outPixels, 0.0);
    
    // Init kernel if needed
    initKernel(static_cast<DrizzleKernelType>(params.kernelType));
}

void DrizzleStacking::addImage(
    const ImageBuffer& img, 
    const RegistrationData& reg, 
    const std::vector<float>& /*weights*/, 
    const float* /*rejectionMap*/)
{
    if (m_accum.empty()) return; // Not initialized
    
    // For now we ignore external weights/rejection in this basic pass
    // or we implement a modified drizzleFrame that accepts them.
    // The static drizzleFrame does not take rejectionMap yet.
    // We'll call the static method for now.
    
    // Note: reg might need scaling if not handled inside drizzleFrame?
    // drizzleFrame takes params.scaleFactor and applies it.
    // So reg should be the original registration (Image -> Reference).
    
    drizzleFrame(img, reg, m_accum, m_weightAccum, m_outWidth, m_outHeight, m_params);
}

bool DrizzleStacking::resolve(ImageBuffer& output) {
    if (m_accum.empty()) return false;
    
    output = ImageBuffer(m_outWidth, m_outHeight, m_channels);
    finalizeStack(m_accum, m_weightAccum, output);
    return true;
}

} // namespace Stacking
