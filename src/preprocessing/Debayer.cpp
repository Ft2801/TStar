
#include "Debayer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Preprocessing {

//=============================================================================
// PATTERN UTILITIES
//=============================================================================

BayerPattern Debayer::getPatternForCrop(BayerPattern original, int x, int y) {
    if (original == BayerPattern::None) return BayerPattern::None;
    
    bool flipX = (x % 2 != 0);
    bool flipY = (y % 2 != 0);
    
    if (!flipX && !flipY) return original;
    
    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(original, redRow, redCol, blueRow, blueCol);
    
    if (flipX) {
        redCol = 1 - redCol;
        blueCol = 1 - blueCol;
    }
    if (flipY) {
        redRow = 1 - redRow;
        blueRow = 1 - blueRow;
    }
    
    // Determine new pattern from modified offsets
    if (redRow == 0 && redCol == 0) return BayerPattern::RGGB;
    if (redRow == 1 && redCol == 1) return BayerPattern::BGGR;
    if (redRow == 1 && redCol == 0) return BayerPattern::GBRG;
    if (redRow == 0 && redCol == 1) return BayerPattern::GRBG;
    
    return original; 
}

void Debayer::getPatternOffsets(BayerPattern pattern,
                                int& redRow, int& redCol,
                                int& blueRow, int& blueCol) {
    switch (pattern) {
        case BayerPattern::RGGB:
            redRow = 0; redCol = 0;
            blueRow = 1; blueCol = 1;
            break;
        case BayerPattern::BGGR:
            redRow = 1; redCol = 1;
            blueRow = 0; blueCol = 0;
            break;
        case BayerPattern::GBRG:
            redRow = 1; redCol = 0;
            blueRow = 0; blueCol = 1;
            break;
        case BayerPattern::GRBG:
            redRow = 0; redCol = 1;
            blueRow = 1; blueCol = 0;
            break;
        default:
            redRow = 0; redCol = 0;
            blueRow = 1; blueCol = 1;
    }
}

bool Debayer::isRed(int x, int y, int redRow, int redCol) {
    return (y % 2 == redRow) && (x % 2 == redCol);
}

bool Debayer::isBlue(int x, int y, int blueRow, int blueCol) {
    return (y % 2 == blueRow) && (x % 2 == blueCol);
}

bool Debayer::isGreen(int x, int y, int redRow, int redCol, int blueRow, int blueCol) {
    return !isRed(x, y, redRow, redCol) && !isBlue(x, y, blueRow, blueCol);
}

//=============================================================================
// BILINEAR INTERPOLATION
//=============================================================================

bool Debayer::bilinear(const ImageBuffer& input, ImageBuffer& output,
                       BayerPattern pattern) {
    if (input.channels() != 1) {
        return false;  // Input must be mono CFA
    }
    
    int width = input.width();
    int height = input.height();
    
    output.resize(width, height, 3);
    
    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(pattern, redRow, redCol, blueRow, blueCol);
    
    const float* src = input.data().data();
    float* dst = output.data().data();
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r = 0, g = 0, b = 0;
            float val = src[y * width + x];
            
            // Determine which color this pixel is
            if (isRed(x, y, redRow, redCol)) {
                r = val;
                // Interpolate green from 4 neighbors
                int count = 0;
                if (x > 0) { g += src[y * width + (x-1)]; count++; }
                if (x < width-1) { g += src[y * width + (x+1)]; count++; }
                if (y > 0) { g += src[(y-1) * width + x]; count++; }
                if (y < height-1) { g += src[(y+1) * width + x]; count++; }
                g = count > 0 ? g / count : 0;
                
                // Interpolate blue from 4 diagonal neighbors
                count = 0;
                if (x > 0 && y > 0) { b += src[(y-1) * width + (x-1)]; count++; }
                if (x < width-1 && y > 0) { b += src[(y-1) * width + (x+1)]; count++; }
                if (x > 0 && y < height-1) { b += src[(y+1) * width + (x-1)]; count++; }
                if (x < width-1 && y < height-1) { b += src[(y+1) * width + (x+1)]; count++; }
                b = count > 0 ? b / count : 0;
            }
            else if (isBlue(x, y, blueRow, blueCol)) {
                b = val;
                // Interpolate green from 4 neighbors
                int count = 0;
                if (x > 0) { g += src[y * width + (x-1)]; count++; }
                if (x < width-1) { g += src[y * width + (x+1)]; count++; }
                if (y > 0) { g += src[(y-1) * width + x]; count++; }
                if (y < height-1) { g += src[(y+1) * width + x]; count++; }
                g = count > 0 ? g / count : 0;
                
                // Interpolate red from 4 diagonal neighbors
                count = 0;
                if (x > 0 && y > 0) { r += src[(y-1) * width + (x-1)]; count++; }
                if (x < width-1 && y > 0) { r += src[(y-1) * width + (x+1)]; count++; }
                if (x > 0 && y < height-1) { r += src[(y+1) * width + (x-1)]; count++; }
                if (x < width-1 && y < height-1) { r += src[(y+1) * width + (x+1)]; count++; }
                r = count > 0 ? r / count : 0;
            }
            else {
                // Green pixel
                g = val;
                
                // Determine if R is horizontal or vertical
                bool redHorizontal = (y % 2 == redRow);
                
                if (redHorizontal) {
                    // Red is on same row
                    int count = 0;
                    if (x > 0) { r += src[y * width + (x-1)]; count++; }
                    if (x < width-1) { r += src[y * width + (x+1)]; count++; }
                    r = count > 0 ? r / count : 0;
                    
                    // Blue is on same column
                    count = 0;
                    if (y > 0) { b += src[(y-1) * width + x]; count++; }
                    if (y < height-1) { b += src[(y+1) * width + x]; count++; }
                    b = count > 0 ? b / count : 0;
                } else {
                    // Blue is on same row
                    int count = 0;
                    if (x > 0) { b += src[y * width + (x-1)]; count++; }
                    if (x < width-1) { b += src[y * width + (x+1)]; count++; }
                    b = count > 0 ? b / count : 0;
                    
                    // Red is on same column
                    count = 0;
                    if (y > 0) { r += src[(y-1) * width + x]; count++; }
                    if (y < height-1) { r += src[(y+1) * width + x]; count++; }
                    r = count > 0 ? r / count : 0;
                }
            }
            
            int idx = (y * width + x) * 3;
            dst[idx] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
        }
    }
    
    return true;
}

//=============================================================================
// VNG (Variable Number of Gradients)
//=============================================================================

void Debayer::computeVNGGradients(const float* data, int width, int height,
                                  int x, int y, float gradients[8]) {
    // VNG uses 8 directions: N, NE, E, SE, S, SW, W, NW
    // Compute gradient in each direction as absolute difference
    
    auto get = [&](int dx, int dy) -> float {
        int nx = x + dx;
        int ny = y + dy;
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            return data[y * width + x];  // Return center value for out of bounds
        }
        return data[ny * width + nx];
    };
    
    float center = data[y * width + x];
    
    // N (0, -2)
    gradients[0] = std::abs(center - get(0, -2)) + std::abs(get(0, -1) - get(0, -3));
    // NE (2, -2)
    gradients[1] = std::abs(center - get(2, -2)) + std::abs(get(1, -1) - get(3, -3));
    // E (2, 0)
    gradients[2] = std::abs(center - get(2, 0)) + std::abs(get(1, 0) - get(3, 0));
    // SE (2, 2)
    gradients[3] = std::abs(center - get(2, 2)) + std::abs(get(1, 1) - get(3, 3));
    // S (0, 2)
    gradients[4] = std::abs(center - get(0, 2)) + std::abs(get(0, 1) - get(0, 3));
    // SW (-2, 2)
    gradients[5] = std::abs(center - get(-2, 2)) + std::abs(get(-1, 1) - get(-3, 3));
    // W (-2, 0)
    gradients[6] = std::abs(center - get(-2, 0)) + std::abs(get(-1, 0) - get(-3, 0));
    // NW (-2, -2)
    gradients[7] = std::abs(center - get(-2, -2)) + std::abs(get(-1, -1) - get(-3, -3));
}

void Debayer::vngInterpolate(const float* data, int width, int height,
                             int x, int y, int redRow, int redCol,
                             int blueRow, int blueCol,
                             float& r, float& g, float& b) {
    auto get = [&](int dx, int dy) -> float {
        int nx = x + dx;
        int ny = y + dy;
        if (nx < 0) nx = 0;
        if (nx >= width) nx = width - 1;
        if (ny < 0) ny = 0;
        if (ny >= height) ny = height - 1;
        return data[ny * width + nx];
    };
    
    float center = get(0, 0);
    
    // Compute gradients
    float gradients[8];
    computeVNGGradients(data, width, height, x, y, gradients);
    
    // Find minimum gradient and threshold
    float minGrad = gradients[0];
    for (int i = 1; i < 8; ++i) {
        minGrad = std::min(minGrad, gradients[i]);
    }
    float threshold = minGrad * 1.5f;
    
    // Directions that pass threshold
    bool useDir[8];
    int numDirs = 0;
    for (int i = 0; i < 8; ++i) {
        useDir[i] = gradients[i] <= threshold;
        if (useDir[i]) numDirs++;
    }
    
    if (numDirs == 0) {
        // Fallback to bilinear
        r = g = b = center;
        return;
    }
    
    // Direction offsets for averaging
    static const int dirX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int dirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    
    // Accumulate colors from valid directions
    float sumR = 0, sumG = 0, sumB = 0;
    int count = 0;
    
    if (isRed(x, y, redRow, redCol)) {
        r = center;
        // Interpolate G and B
        for (int i = 0; i < 8; ++i) {
            if (!useDir[i]) continue;
            
            int dx = dirX[i];
            int dy = dirY[i];
            
            // Get green from neighbors
            sumG += (get(dx, 0) + get(0, dy)) * 0.5f;
            
            // Get blue from diagonal
            sumB += get(dx, dy);
            count++;
        }
        g = count > 0 ? sumG / count : center;
        b = count > 0 ? sumB / count : center;
    }
    else if (isBlue(x, y, blueRow, blueCol)) {
        b = center;
        // Interpolate G and R
        for (int i = 0; i < 8; ++i) {
            if (!useDir[i]) continue;
            
            int dx = dirX[i];
            int dy = dirY[i];
            
            sumG += (get(dx, 0) + get(0, dy)) * 0.5f;
            sumR += get(dx, dy);
            count++;
        }
        g = count > 0 ? sumG / count : center;
        r = count > 0 ? sumR / count : center;
    }
    else {
        // Green pixel
        g = center;
        
        bool redHorizontal = (y % 2 == redRow);
        
        for (int i = 0; i < 8; ++i) {
            if (!useDir[i]) continue;
            
            if (redHorizontal) {
                sumR += (get(-1, 0) + get(1, 0)) * 0.5f;
                sumB += (get(0, -1) + get(0, 1)) * 0.5f;
            } else {
                sumB += (get(-1, 0) + get(1, 0)) * 0.5f;
                sumR += (get(0, -1) + get(0, 1)) * 0.5f;
            }
            count++;
        }
        r = count > 0 ? sumR / count : center;
        b = count > 0 ? sumB / count : center;
    }
}

bool Debayer::vng(const ImageBuffer& input, ImageBuffer& output,
                  BayerPattern pattern) {
    if (input.channels() != 1) {
        return false;
    }
    
    int width = input.width();
    int height = input.height();
    
    output.resize(width, height, 3);
    
    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(pattern, redRow, redCol, blueRow, blueCol);
    
    const float* src = input.data().data();
    float* dst = output.data().data();
    
    // VNG needs a 5-pixel border for full interpolation
    const int border = 3;
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r, g, b;
            
            if (x < border || x >= width - border || 
                y < border || y >= height - border) {
                // Use bilinear for border pixels
                float val = src[y * width + x];
                r = g = b = val;  // Simplified
            } else {
                vngInterpolate(src, width, height, x, y,
                              redRow, redCol, blueRow, blueCol,
                              r, g, b);
            }
            
            int idx = (y * width + x) * 3;
            dst[idx] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
        }
    }
    
    return true;
}

//=============================================================================
// SUPER PIXEL
//=============================================================================

bool Debayer::superpixel(const ImageBuffer& input, ImageBuffer& output,
                         BayerPattern pattern) {
    if (input.channels() != 1) {
        return false;
    }
    
    int width = input.width();
    int height = input.height();
    int outWidth = width / 2;
    int outHeight = height / 2;
    
    output.resize(outWidth, outHeight, 3);
    
    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(pattern, redRow, redCol, blueRow, blueCol);
    
    const float* src = input.data().data();
    float* dst = output.data().data();
    
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            int srcX = x * 2;
            int srcY = y * 2;
            
            // Get 2x2 block
            float p00 = src[srcY * width + srcX];
            float p10 = src[srcY * width + srcX + 1];
            float p01 = src[(srcY + 1) * width + srcX];
            float p11 = src[(srcY + 1) * width + srcX + 1];
            
            float r, g, b;
            
            // Map based on pattern
            switch (pattern) {
                case BayerPattern::RGGB:
                    r = p00;
                    g = (p10 + p01) * 0.5f;
                    b = p11;
                    break;
                case BayerPattern::BGGR:
                    b = p00;
                    g = (p10 + p01) * 0.5f;
                    r = p11;
                    break;
                case BayerPattern::GBRG:
                    g = (p00 + p11) * 0.5f;
                    b = p10;
                    r = p01;
                    break;
                case BayerPattern::GRBG:
                    g = (p00 + p11) * 0.5f;
                    r = p10;
                    b = p01;
                    break;
                default:
                    r = g = b = (p00 + p10 + p01 + p11) * 0.25f;
            }
            
            int idx = (y * outWidth + x) * 3;
            dst[idx] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
        }
    }
    
    return true;
}

//=============================================================================
// AHD (Placeholder for future implementation)
//=============================================================================

bool Debayer::ahd(const ImageBuffer& input, ImageBuffer& output,
                  BayerPattern pattern) {
    // AHD is complex - use VNG as fallback for now
    return vng(input, output, pattern);
}

//=============================================================================
// RCD (Placeholder for future implementation)
//=============================================================================

bool Debayer::rcd(const ImageBuffer& input, ImageBuffer& output,
                  BayerPattern pattern) {
    // RCD is complex - use VNG as fallback for now
    return vng(input, output, pattern);
}

} // namespace Preprocessing
