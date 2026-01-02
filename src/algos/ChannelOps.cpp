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

ImageBuffer ChannelOps::computeLuminance(const ImageBuffer& src, LumaMethod method) {
    if (!src.isValid()) return ImageBuffer();
    
    int w = src.width();
    int h = src.height();
    int c = src.channels();
    
    std::vector<float> dst(w * h);
    const float* s = src.data().data();

    if (c == 1) {
        std::copy(s, s + w * h, dst.begin());
        ImageBuffer out;
        out.setData(w, h, 1, dst);
        out.setMetadata(src.metadata()); // Preserve WCS
        return out;
    }

    float wr = getLumaWeightR(method);
    float wg = getLumaWeightG(method);
    float wb = getLumaWeightB(method);

    for (int i = 0; i < w * h; ++i) {
        float val = 0.0f;
        if (method == LumaMethod::MAX) {
            float v1 = s[i*c+0];
            float v2 = s[i*c+1];
            float v3 = s[i*c+2];
            val = (v1 > v2) ? ((v1 > v3) ? v1 : v3) : ((v2 > v3) ? v2 : v3);
        } else if (method == LumaMethod::AVERAGE) {
            val = (s[i*c+0] + s[i*c+1] + s[i*c+2]) / 3.0f;
        } else {
            val = s[i*c+0] * wr + s[i*c+1] * wg + s[i*c+2] * wb;
        }
        dst[i] = val;
    }
    
    ImageBuffer out;
    out.setData(w, h, 1, dst);
    out.setMetadata(src.metadata()); // Preserve WCS
    return out;
}


float ChannelOps::getLumaWeightR(LumaMethod method) {
    switch (method) {
        case LumaMethod::REC601: return 0.299f;
        case LumaMethod::REC709: default: return 0.2126f;
    }
}
float ChannelOps::getLumaWeightG(LumaMethod method) {
    switch (method) {
        case LumaMethod::REC601: return 0.587f;
        case LumaMethod::REC709: default: return 0.7152f;
    }
}
float ChannelOps::getLumaWeightB(LumaMethod method) {
    switch (method) {
        case LumaMethod::REC601: return 0.114f;
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
        int px = x % 2, py = y % 2;
        if (px == offsets[0] && py == offsets[1]) return 0; // R
        if (px == offsets[6] && py == offsets[7]) return 2; // B
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
                int px = x % 2, py = y % 2;
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
            int idx = (y * w + x) * 3;
            float r = data[idx], g = data[idx + 1], b = data[idx + 2];
            
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

