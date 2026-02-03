#include "ChannelOps.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

std::vector<ImageBuffer> ChannelOps::extractChannels(const ImageBuffer& src) {
    std::vector<ImageBuffer> channels;
    if (!src.isValid()) return channels;

    int width = src.width();
    int height = src.height();

    // Create 3 mono buffers
    for (int i = 0; i < 3; ++i) {
        ImageBuffer mono;
        std::vector<float> data(width * height);
        
        const float* srcPtr = src.data().data();
        int srcChannels = src.channels();
        if (srcChannels < 3) {
             return {}; 
        }

        // Plain loop
        for (int p = 0; p < width * height; ++p) {
            data[p] = srcPtr[p * srcChannels + i];
        }
        mono.setData(width, height, 1, data);
        channels.push_back(std::move(mono));
    }
    return channels;
}

ImageBuffer ChannelOps::combineChannels(const ImageBuffer& r, const ImageBuffer& g, const ImageBuffer& b) {
    if (!r.isValid() || !g.isValid() || !b.isValid()) return ImageBuffer();
    
    if (r.width() != g.width() || r.width() != b.width() ||
        r.height() != g.height() || r.height() != b.height()) {
        std::cerr << "ChannelOps::combineChannels: Dimensions mismatch" << std::endl;
        return ImageBuffer();
    }

    int width = r.width();
    int height = r.height();
    std::vector<float> outData(width * height * 3);
    
    const float* rPtr = r.data().data();
    const float* gPtr = g.data().data();
    const float* bPtr = b.data().data();

    for (int i = 0; i < width * height; ++i) {
        outData[i * 3 + 0] = rPtr[i];
        outData[i * 3 + 1] = gPtr[i];
        outData[i * 3 + 2] = bPtr[i];
    }

    ImageBuffer out;
    out.setData(width, height, 3, outData);
    out.setMetadata(r.metadata()); // Preserve WCS from R channel
    return out;
}

// Helper: Estimate Noise Sigma (MAD based)
std::vector<float> ChannelOps::estimateNoiseSigma(const ImageBuffer& src) {
    if (!src.isValid()) return {};
    int w = src.width();
    int h = src.height();
    int c = src.channels();
    std::vector<float> sigmas(c);
    
    // Subsample for speed (step 4 pixels)
    int tempW = w;
    int tempH = h;
    int step = 4;
    
    if (tempW < 10 || tempH < 10) step = 1;

    const float* s = src.data().data();
    std::vector<float> sub;
    sub.reserve((w/step + 1) * (h/step + 1));

    for (int ch = 0; ch < c; ++ch) {
        sub.clear();
        for (int y = 0; y < h; y+=step) {
             for (int x = 0; x < w; x+=step) {
                 sub.push_back(s[(y * w + x) * c + ch]);
             }
        }
        
        if (sub.empty()) { sigmas[ch] = 1e-5f; continue; }
        
        // Median
        size_t n = sub.size();
        std::nth_element(sub.begin(), sub.begin() + n/2, sub.end());
        float med = sub[n/2];
        
        // MAD
        std::vector<float> absDevs(n);
        for(size_t i=0; i<n; ++i) absDevs[i] = std::abs(sub[i] - med);
        
        std::nth_element(absDevs.begin(), absDevs.begin() + n/2, absDevs.end());
        float mad = absDevs[n/2];
        
        float sigma = 1.4826f * mad;
        if (sigma < 1e-12f) sigma = 1e-12f;
        sigmas[ch] = sigma;
    }
    return sigmas;
}

ImageBuffer ChannelOps::computeLuminance(const ImageBuffer& src, LumaMethod method, 
                                        const std::vector<float>& customWeights, 
                                        const std::vector<float>& customNoiseSigma) 
{
    if (!src.isValid()) return ImageBuffer();
    
    int w = src.width();
    int h = src.height();
    int c = src.channels();
    
    std::vector<float> dst(w * h);
    const float* s = src.data().data();

    // Single Channel optimization
    if (c == 1) {
        std::copy(s, s + w * h, dst.begin());
        ImageBuffer out;
        out.setData(w, h, 1, dst);
        out.setMetadata(src.metadata());
        return out;
    }

    // Generic channel weights
    std::vector<float> weights(c, 0.0f);
    
    if (method == LumaMethod::MAX || method == LumaMethod::MEDIAN) {
        // No weights needed
    } else if (method == LumaMethod::SNR) {
        std::vector<float> sigmas = customNoiseSigma;
        if ((int)sigmas.size() != c) {
            sigmas = estimateNoiseSigma(src);
        }
        float sumW = 0.0f;
        for(int i=0; i<c; ++i) {
            weights[i] = 1.0f / (sigmas[i] * sigmas[i] + 1e-12f);
            sumW += weights[i];
        }
        if (sumW > 0) {
           for(int i=0; i<c; ++i) weights[i] /= sumW;
        } else {
           // Fallback: equal weights
           for(int i=0; i<c; ++i) weights[i] = 1.0f / c;
        }
    } else {
        // R, G, B weights specific logic
        // If c >= 3, map 0->R, 1->G, 2->B. Others 0?
        if (c >= 3) {
            weights[0] = getLumaWeightR(method, customWeights);
            weights[1] = getLumaWeightG(method, customWeights);
            weights[2] = getLumaWeightB(method, customWeights);
            // Renormalize if necessary? Usually luma weights sum to 1.
        } else if (c == 2) {
             // 2 Channels. Just avg or use R/G weights?
             // Let's use 50/50 for safety unless custom
             weights[0] = 0.5f; weights[1] = 0.5f;
        }
    }
    
    // Compute
    for (int i = 0; i < w * h; ++i) {
        float val = 0.0f;
        if (method == LumaMethod::MAX) {
            val = s[i*c];
            for(int k=1; k<c; ++k) val = std::max(val, s[i*c+k]);
        } else if (method == LumaMethod::MEDIAN) {
             // Small optimization for common c=3
             if (c==3) {
                 float v[3] = {s[i*c+0], s[i*c+1], s[i*c+2]};
                 if (v[0] > v[1]) std::swap(v[0], v[1]);
                 if (v[1] > v[2]) std::swap(v[1], v[2]);
                 if (v[0] > v[1]) std::swap(v[0], v[1]);
                 val = v[1];
             } else {
                 std::vector<float> v(c);
                 for(int k=0; k<c; ++k) v[k] = s[i*c+k];
                 std::nth_element(v.begin(), v.begin() + c/2, v.end());
                 val = v[c/2];
             }
        } else {
            // Weighted Sum
            for(int k=0; k<c; ++k) {
                val += s[i*c+k] * weights[k];
            }
        }
        dst[i] = std::clamp(val, 0.0f, 1.0f);
    }
    
    ImageBuffer out;
    out.setData(w, h, 1, dst);
    out.setMetadata(src.metadata());
    return out;
}

bool ChannelOps::recombineLuminance(ImageBuffer& target, const ImageBuffer& sourceL, 
                                   LumaMethod method, 
                                   float blend, float softKnee,
                                   const std::vector<float>& customWeights)
{
    if (!target.isValid() || !sourceL.isValid()) return false;
    if (sourceL.channels() != 1) return false;
    
    if (target.width() != sourceL.width() || target.height() != sourceL.height()) return false;
    
    int w = target.width();
    int h = target.height();
    int c = target.channels();
    if (c < 3) return false;
    
    float wr, wg, wb;
    if (method == LumaMethod::REC601 || method == LumaMethod::REC2020 || 
        method == LumaMethod::REC709 || method == LumaMethod::CUSTOM || method == LumaMethod::AVERAGE) {
        wr = getLumaWeightR(method, customWeights);
        wg = getLumaWeightG(method, customWeights);
        wb = getLumaWeightB(method, customWeights);
    } else {
        // Fallback for non-linear methods (Max/Median/SNR) - Use Rec709 as the "Linear Basis" for recombination scaling
        wr = 0.2126f; wg = 0.7152f; wb = 0.0722f;
    }
    
    float* rgb = target.data().data();
    const float* L = sourceL.data().data();
    const float eps = 1e-6f;
    
    for (int i = 0; i < w * h; ++i) {
        float r = rgb[i*c+0];
        float g = rgb[i*c+1];
        float b = rgb[i*c+2];
        
        // Current Y (Linear Estimate)
        float Y = r*wr + g*wg + b*wb; 
        
        // Target L
        float newL = L[i];
        
        // Scale factor
        float s = newL / (Y + eps);
        
        // Soft knee (compression of extreme highlights)
        if (softKnee > 0.0f) {
            float k = std::clamp(softKnee, 0.0f, 1.0f);
            s = s / (1.0f + k * (s - 1.0f));
        }
        
        // Apply
        float nr = r * s;
        float ng = g * s;
        float nb = b * s;
        
        // Blend
        if (blend < 1.0f && blend >= 0.0f) {
            nr = r * (1.0f - blend) + nr * blend;
            ng = g * (1.0f - blend) + ng * blend;
            nb = b * (1.0f - blend) + nb * blend;
        }
        
        rgb[i*c+0] = std::clamp(nr, 0.0f, 1.0f);
        rgb[i*c+1] = std::clamp(ng, 0.0f, 1.0f);
        rgb[i*c+2] = std::clamp(nb, 0.0f, 1.0f);
    }
    
    target.setModified(true);
    return true;
}

float ChannelOps::getLumaWeightR(LumaMethod method, const std::vector<float>& customWeights) {
    if (method == LumaMethod::CUSTOM && customWeights.size() >= 3) return customWeights[0];
    switch (method) {
        case LumaMethod::REC601: return 0.299f;
        case LumaMethod::REC2020: return 0.2627f;
        case LumaMethod::AVERAGE: return 0.3333f;
        case LumaMethod::REC709: default: return 0.2126f;
    }
}
float ChannelOps::getLumaWeightG(LumaMethod method, const std::vector<float>& customWeights) {
    if (method == LumaMethod::CUSTOM && customWeights.size() >= 3) return customWeights[1];
    switch (method) {
        case LumaMethod::REC601: return 0.587f;
        case LumaMethod::REC2020: return 0.6780f;
        case LumaMethod::AVERAGE: return 0.3333f;
        case LumaMethod::REC709: default: return 0.7152f;
    }
}
float ChannelOps::getLumaWeightB(LumaMethod method, const std::vector<float>& customWeights) {
    if (method == LumaMethod::CUSTOM && customWeights.size() >= 3) return customWeights[2];
    switch (method) {
        case LumaMethod::REC601: return 0.114f;
        case LumaMethod::REC2020: return 0.0593f;
        case LumaMethod::AVERAGE: return 0.3333f;
        case LumaMethod::REC709: default: return 0.0722f;
    }
}

ImageBuffer ChannelOps::debayer(const ImageBuffer& mosaic, const std::string& pattern, const std::string& method) {
    if (!mosaic.isValid() || mosaic.channels() != 1) return ImageBuffer();
    
    int w = mosaic.width();
    int h = mosaic.height();
    const float* src = mosaic.data().data();
    
    std::vector<float> dst(w * h * 3);
    
    // Pattern offsets: [0]=R, [1]=G1, [2]=G2, [3]=B positions in 2x2 block
    // Format: {Rx, Ry, G1x, G1y, G2x, G2y, Bx, By}
    int offsets[8];
    if (pattern == "RGGB") {
        offsets[0]=0; offsets[1]=0; offsets[2]=1; offsets[3]=0;
        offsets[4]=0; offsets[5]=1; offsets[6]=1; offsets[7]=1;
    } else if (pattern == "BGGR") {
        offsets[0]=1; offsets[1]=1; offsets[2]=0; offsets[3]=1;
        offsets[4]=1; offsets[5]=0; offsets[6]=0; offsets[7]=0;
    } else if (pattern == "GRBG") {
        offsets[0]=1; offsets[1]=0; offsets[2]=0; offsets[3]=0;
        offsets[4]=1; offsets[5]=1; offsets[6]=0; offsets[7]=1;
    } else if (pattern == "GBRG") {
        offsets[0]=0; offsets[1]=1; offsets[2]=1; offsets[3]=1;
        offsets[4]=0; offsets[5]=0; offsets[6]=1; offsets[7]=0;
    } else {
        return ImageBuffer();
    }
    
    bool useEdge = (method == "edge");
    
    // Helper to get pixel with boundary check
    auto getPixel = [&](int x, int y) -> float {
        x = std::max(0, std::min(w-1, x));
        y = std::max(0, std::min(h-1, y));
        return src[y * w + x];
    };
    
    // Determine color type at position based on pattern
    auto getColorType = [&](int x, int y) -> int {
        int py = y % 2;
        if (x % 2 == offsets[0] && py == offsets[1]) return 0; // R
        if (x % 2 == offsets[6] && py == offsets[7]) return 2; // B
        return 1; // G
    };
    
    // Edge-aware weight
    auto edgeWeight = [&](int x1, int y1, int x2, int y2) -> float {
        if (!useEdge) return 1.0f;
        float diff = std::abs(getPixel(x1, y1) - getPixel(x2, y2));
        return 1.0f / (1.0f + diff * 10.0f);
    };
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 3;
            float r = 0, g = 0, b = 0;
            int ct = getColorType(x, y);
            
            if (ct == 0) { // R pixel
                r = getPixel(x, y);
                // Interpolate G from 4 neighbors
                float w1 = edgeWeight(x, y, x-1, y);
                float w2 = edgeWeight(x, y, x+1, y);
                float w3 = edgeWeight(x, y, x, y-1);
                float w4 = edgeWeight(x, y, x, y+1);
                float wsum = w1 + w2 + w3 + w4;
                g = (getPixel(x-1, y)*w1 + getPixel(x+1, y)*w2 + 
                     getPixel(x, y-1)*w3 + getPixel(x, y+1)*w4) / wsum;
                // Interpolate B from 4 diagonal neighbors
                w1 = edgeWeight(x, y, x-1, y-1);
                w2 = edgeWeight(x, y, x+1, y-1);
                w3 = edgeWeight(x, y, x-1, y+1);
                w4 = edgeWeight(x, y, x+1, y+1);
                wsum = w1 + w2 + w3 + w4;
                b = (getPixel(x-1, y-1)*w1 + getPixel(x+1, y-1)*w2 + 
                     getPixel(x-1, y+1)*w3 + getPixel(x+1, y+1)*w4) / wsum;
            }
            else if (ct == 2) { // B pixel
                b = getPixel(x, y);
                // Interpolate G from 4 neighbors
                float w1 = edgeWeight(x, y, x-1, y);
                float w2 = edgeWeight(x, y, x+1, y);
                float w3 = edgeWeight(x, y, x, y-1);
                float w4 = edgeWeight(x, y, x, y+1);
                float wsum = w1 + w2 + w3 + w4;
                g = (getPixel(x-1, y)*w1 + getPixel(x+1, y)*w2 + 
                     getPixel(x, y-1)*w3 + getPixel(x, y+1)*w4) / wsum;
                // Interpolate R from 4 diagonal neighbors
                w1 = edgeWeight(x, y, x-1, y-1);
                w2 = edgeWeight(x, y, x+1, y-1);
                w3 = edgeWeight(x, y, x-1, y+1);
                w4 = edgeWeight(x, y, x+1, y+1);
                wsum = w1 + w2 + w3 + w4;
                r = (getPixel(x-1, y-1)*w1 + getPixel(x+1, y-1)*w2 + 
                     getPixel(x-1, y+1)*w3 + getPixel(x+1, y+1)*w4) / wsum;
            }
            else { // G pixel
                g = getPixel(x, y);
                // Determine R/B axis from pattern position
                int py = y % 2;
                bool rOnRow = (py == offsets[1]); // R is on this row
                
                if (rOnRow) {
                    // R on this row, B on column
                    float w1 = edgeWeight(x, y, x-1, y);
                    float w2 = edgeWeight(x, y, x+1, y);
                    r = (getPixel(x-1, y)*w1 + getPixel(x+1, y)*w2) / (w1 + w2);
                    w1 = edgeWeight(x, y, x, y-1);
                    w2 = edgeWeight(x, y, x, y+1);
                    b = (getPixel(x, y-1)*w1 + getPixel(x, y+1)*w2) / (w1 + w2);
                } else {
                    // B on this row, R on column
                    float w1 = edgeWeight(x, y, x-1, y);
                    float w2 = edgeWeight(x, y, x+1, y);
                    b = (getPixel(x-1, y)*w1 + getPixel(x+1, y)*w2) / (w1 + w2);
                    w1 = edgeWeight(x, y, x, y-1);
                    w2 = edgeWeight(x, y, x, y+1);
                    r = (getPixel(x, y-1)*w1 + getPixel(x, y+1)*w2) / (w1 + w2);
                }
            }
            
            dst[idx + 0] = std::clamp(r, 0.0f, 1.0f);
            dst[idx + 1] = std::clamp(g, 0.0f, 1.0f);
            dst[idx + 2] = std::clamp(b, 0.0f, 1.0f);
        }
    }
    
    ImageBuffer out;
    out.setData(w, h, 3, dst);
    out.setMetadata(mosaic.metadata()); // Preserve WCS and other metadata
    return out;
}

float ChannelOps::computeDebayerScore(const ImageBuffer& rgb) {
    if (!rgb.isValid() || rgb.channels() < 3) return std::numeric_limits<float>::max();
    
    int w = rgb.width();
    int h = rgb.height();
    const float* data = rgb.data().data();
    
    // Score based on color channel gradient differences (zipper artifact detection)
    float score = 0.0f;
    int count = 0;
    
    for (int y = 1; y < h - 1; y += 2) {
        for (int x = 1; x < w - 1; x += 2) {
            // Compute score at this position
            
            // Horizontal gradient for R-G difference
            int idxL = (y * w + x - 1) * 3;
            int idxR = (y * w + x + 1) * 3;
            float rgDiffH = std::abs((data[idxL] - data[idxL+1]) - (data[idxR] - data[idxR+1]));
            
            // Vertical gradient for B-G difference
            int idxT = ((y-1) * w + x) * 3;
            int idxB = ((y+1) * w + x) * 3;
            float bgDiffV = std::abs((data[idxT+2] - data[idxT+1]) - (data[idxB+2] - data[idxB+1]));
            
            score += rgDiffH + bgDiffV;
            count++;
        }
    }
    
    return count > 0 ? score / count : 0.0f;
}

ImageBuffer ChannelOps::continuumSubtract(const ImageBuffer& narrowband, const ImageBuffer& continuum, float qFactor) {
    if (!narrowband.isValid() || !continuum.isValid()) return ImageBuffer();
    if (narrowband.width() != continuum.width() || narrowband.height() != continuum.height()) {
        return ImageBuffer();
    }
    
    int w = narrowband.width();
    int h = narrowband.height();
    
    // Convert to mono if needed
    const float* nbData = narrowband.data().data();
    const float* contData = continuum.data().data();
    int nbCh = narrowband.channels();
    int contCh = continuum.channels();
    
    // Get luminance or first channel for each
    std::vector<float> nb(w * h), cont(w * h);
    
    for (int i = 0; i < w * h; ++i) {
        if (nbCh == 1) {
            nb[i] = nbData[i];
        } else {
            nb[i] = nbData[i * nbCh]; // Use R channel
        }
        
        if (contCh == 1) {
            cont[i] = contData[i];
        } else {
            cont[i] = contData[i * contCh]; // Use R channel
        }
    }
    
    // Compute median of continuum
    std::vector<float> contCopy = cont;
    std::nth_element(contCopy.begin(), contCopy.begin() + contCopy.size()/2, contCopy.end());
    float contMedian = contCopy[contCopy.size() / 2];
    
    // Perform subtraction: result = nb - Q * (cont - median)
    std::vector<float> result(w * h);
    for (int i = 0; i < w * h; ++i) {
        float val = nb[i] - qFactor * (cont[i] - contMedian);
        result[i] = std::clamp(val, 0.0f, 1.0f);
    }
    
    ImageBuffer out;
    out.setData(w, h, 1, result);
    out.setMetadata(narrowband.metadata()); // Preserve WCS and other metadata
    return out;
}

void ChannelOps::removePedestal(ImageBuffer& img) {
    if (!img.isValid()) return;
    int w = img.width();
    int h = img.height();
    int c = img.channels();
    float* data = img.data().data();
    size_t total = (size_t)w * h;
    
    if (total == 0) return;
    
    std::vector<float> mins(c, std::numeric_limits<float>::max());
    
    // 1. Find minimums
    for (size_t i = 0; i < total; ++i) {
        for (int ch = 0; ch < c; ++ch) {
            float val = data[i * c + ch];
            if (val < mins[ch]) mins[ch] = val;
        }
    }
    
    // 2. Subtract
    #pragma omp parallel for
    for (size_t i = 0; i < total; ++i) {
        for (int ch = 0; ch < c; ++ch) {
            float val = data[i * c + ch] - mins[ch];
            data[i * c + ch] = std::max(0.0f, val);
        }
    }
}

