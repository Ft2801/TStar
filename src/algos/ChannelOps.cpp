#include "ChannelOps.h"
#include <algorithm>
#include <cmath>
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
