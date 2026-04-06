/**
 * @file Debayer.cpp
 * @brief Implementation of Bayer CFA demosaicing algorithms.
 *
 * Contains bilinear, VNG, super-pixel, AHD, and RCD demosaicing
 * implementations, together with Bayer pattern helper utilities.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "Debayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <QDebug>

namespace Preprocessing {

// ============================================================================
//  Bayer Pattern Utilities
// ============================================================================

BayerPattern Debayer::getPatternForCrop(BayerPattern original, int x, int y)
{
    if (original == BayerPattern::None) {
        return BayerPattern::None;
    }

    const bool flipX = (x % 2 != 0);
    const bool flipY = (y % 2 != 0);

    if (!flipX && !flipY) {
        return original;
    }

    // Retrieve current red/blue offsets and apply the crop shift.
    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(original, redRow, redCol, blueRow, blueCol);

    if (flipX) {
        redCol  = 1 - redCol;
        blueCol = 1 - blueCol;
    }
    if (flipY) {
        redRow  = 1 - redRow;
        blueRow = 1 - blueRow;
    }

    // Map the modified red-pixel position back to a named pattern.
    if (redRow == 0 && redCol == 0) return BayerPattern::RGGB;
    if (redRow == 1 && redCol == 1) return BayerPattern::BGGR;
    if (redRow == 1 && redCol == 0) return BayerPattern::GBRG;
    if (redRow == 0 && redCol == 1) return BayerPattern::GRBG;

    return original;
}

void Debayer::getPatternOffsets(BayerPattern pattern,
                                int& redRow, int& redCol,
                                int& blueRow, int& blueCol)
{
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
            break;
    }
}

bool Debayer::isRed(int x, int y, int redRow, int redCol)
{
    return (y % 2 == redRow) && (x % 2 == redCol);
}

bool Debayer::isBlue(int x, int y, int blueRow, int blueCol)
{
    return (y % 2 == blueRow) && (x % 2 == blueCol);
}

bool Debayer::isGreen(int x, int y, int redRow, int redCol,
                      int blueRow, int blueCol)
{
    return !isRed(x, y, redRow, redCol) && !isBlue(x, y, blueRow, blueCol);
}

// ============================================================================
//  Bilinear Interpolation
// ============================================================================

bool Debayer::bilinear(const ImageBuffer& input, ImageBuffer& output,
                       BayerPattern pattern)
{
    if (input.channels() != 1) {
        return false;
    }

    const int width  = input.width();
    const int height = input.height();

    output.resize(width, height, 3);

    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(pattern, redRow, redCol, blueRow, blueCol);

    const float* src = input.data().data();
    float*       dst = output.data().data();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {

            float r = 0.0f, g = 0.0f, b = 0.0f;
            const float val = src[y * width + x];

            if (isRed(x, y, redRow, redCol)) {
                // ---- Red pixel site ----
                r = val;

                // Green: average of 4-connected (horizontal + vertical) neighbours.
                int count = 0;
                if (x > 0)          { g += src[y * width + (x - 1)]; ++count; }
                if (x < width - 1)  { g += src[y * width + (x + 1)]; ++count; }
                if (y > 0)          { g += src[(y - 1) * width + x]; ++count; }
                if (y < height - 1) { g += src[(y + 1) * width + x]; ++count; }
                g = (count > 0) ? g / count : 0.0f;

                // Blue: average of 4 diagonal neighbours.
                count = 0;
                if (x > 0 && y > 0)                    { b += src[(y - 1) * width + (x - 1)]; ++count; }
                if (x < width - 1 && y > 0)             { b += src[(y - 1) * width + (x + 1)]; ++count; }
                if (x > 0 && y < height - 1)            { b += src[(y + 1) * width + (x - 1)]; ++count; }
                if (x < width - 1 && y < height - 1)    { b += src[(y + 1) * width + (x + 1)]; ++count; }
                b = (count > 0) ? b / count : 0.0f;

            } else if (isBlue(x, y, blueRow, blueCol)) {
                // ---- Blue pixel site ----
                b = val;

                // Green: 4-connected neighbours.
                int count = 0;
                if (x > 0)          { g += src[y * width + (x - 1)]; ++count; }
                if (x < width - 1)  { g += src[y * width + (x + 1)]; ++count; }
                if (y > 0)          { g += src[(y - 1) * width + x]; ++count; }
                if (y < height - 1) { g += src[(y + 1) * width + x]; ++count; }
                g = (count > 0) ? g / count : 0.0f;

                // Red: 4 diagonal neighbours.
                count = 0;
                if (x > 0 && y > 0)                    { r += src[(y - 1) * width + (x - 1)]; ++count; }
                if (x < width - 1 && y > 0)             { r += src[(y - 1) * width + (x + 1)]; ++count; }
                if (x > 0 && y < height - 1)            { r += src[(y + 1) * width + (x - 1)]; ++count; }
                if (x < width - 1 && y < height - 1)    { r += src[(y + 1) * width + (x + 1)]; ++count; }
                r = (count > 0) ? r / count : 0.0f;

            } else {
                // ---- Green pixel site ----
                g = val;

                // Determine whether red neighbours are on the same row
                // (horizontal) or same column (vertical).
                const bool redHorizontal = (y % 2 == redRow);

                if (redHorizontal) {
                    // Red on same row, blue on same column.
                    int count = 0;
                    if (x > 0)          { r += src[y * width + (x - 1)]; ++count; }
                    if (x < width - 1)  { r += src[y * width + (x + 1)]; ++count; }
                    r = (count > 0) ? r / count : 0.0f;

                    count = 0;
                    if (y > 0)          { b += src[(y - 1) * width + x]; ++count; }
                    if (y < height - 1) { b += src[(y + 1) * width + x]; ++count; }
                    b = (count > 0) ? b / count : 0.0f;
                } else {
                    // Blue on same row, red on same column.
                    int count = 0;
                    if (x > 0)          { b += src[y * width + (x - 1)]; ++count; }
                    if (x < width - 1)  { b += src[y * width + (x + 1)]; ++count; }
                    b = (count > 0) ? b / count : 0.0f;

                    count = 0;
                    if (y > 0)          { r += src[(y - 1) * width + x]; ++count; }
                    if (y < height - 1) { r += src[(y + 1) * width + x]; ++count; }
                    r = (count > 0) ? r / count : 0.0f;
                }
            }

            // Write the interpolated RGB triplet.
            const int idx = (y * width + x) * 3;
            dst[idx + 0] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
        }
    }

    output.setMetadata(input.metadata());
    return true;
}

void Debayer::bilinearPixel(const float* src, int width, int height,
                            int x, int y, int redRow, int redCol,
                            int blueRow, int blueCol,
                            float& r, float& g, float& b)
{
    const int idx_base = y * width + x;
    const float val = src[idx_base];

    if (isRed(x, y, redRow, redCol)) {
        r = val;
        int countG = 0, countB = 0;
        float sumG = 0, sumB = 0;

        if (x > 0)          { sumG += src[y * width + (x - 1)]; ++countG; }
        if (x < width - 1)  { sumG += src[y * width + (x + 1)]; ++countG; }
        if (y > 0)          { sumG += src[(y - 1) * width + x]; ++countG; }
        if (y < height - 1) { sumG += src[(y + 1) * width + x]; ++countG; }
        g = (countG > 0) ? sumG / countG : 0.0f;

        if (x > 0 && y > 0)                  { sumB += src[(y - 1) * width + (x - 1)]; ++countB; }
        if (x < width - 1 && y > 0)          { sumB += src[(y - 1) * width + (x + 1)]; ++countB; }
        if (x > 0 && y < height - 1)         { sumB += src[(y + 1) * width + (x - 1)]; ++countB; }
        if (x < width - 1 && y < height - 1) { sumB += src[(y + 1) * width + (x + 1)]; ++countB; }
        b = (countB > 0) ? sumB / countB : 0.0f;

    } else if (isBlue(x, y, blueRow, blueCol)) {
        b = val;
        int countG = 0, countR = 0;
        float sumG = 0, sumR = 0;

        if (x > 0)          { sumG += src[y * width + (x - 1)]; ++countG; }
        if (x < width - 1)  { sumG += src[y * width + (x + 1)]; ++countG; }
        if (y > 0)          { sumG += src[(y - 1) * width + x]; ++countG; }
        if (y < height - 1) { sumG += src[(y + 1) * width + x]; ++countG; }
        g = (countG > 0) ? sumG / countG : 0.0f;

        if (x > 0 && y > 0)                  { sumR += src[(y - 1) * width + (x - 1)]; ++countR; }
        if (x < width - 1 && y > 0)          { sumR += src[(y - 1) * width + (x + 1)]; ++countR; }
        if (x > 0 && y < height - 1)         { sumR += src[(y + 1) * width + (x - 1)]; ++countR; }
        if (x < width - 1 && y < height - 1) { sumR += src[(y + 1) * width + (x + 1)]; ++countR; }
        r = (countR > 0) ? sumR / countR : 0.0f;

    } else {
        g = val;
        const bool redHorizontal = (y % 2 == redRow);
        int countR = 0, countB = 0;
        float sumR = 0, sumB = 0;

        if (redHorizontal) {
            if (x > 0)          { sumR += src[y * width + (x - 1)]; ++countR; }
            if (x < width - 1)  { sumR += src[y * width + (x + 1)]; ++countR; }
            if (y > 0)          { sumB += src[(y - 1) * width + x]; ++countB; }
            if (y < height - 1) { sumB += src[(y + 1) * width + x]; ++countB; }
        } else {
            if (x > 0)          { sumB += src[y * width + (x - 1)]; ++countB; }
            if (x < width - 1)  { sumB += src[y * width + (x + 1)]; ++countB; }
            if (y > 0)          { sumR += src[(y - 1) * width + x]; ++countR; }
            if (y < height - 1) { sumR += src[(y + 1) * width + x]; ++countR; }
        }
        r = (countR > 0) ? sumR / countR : 0.0f;
        b = (countB > 0) ? sumB / countB : 0.0f;
    }
}

// ============================================================================
//  VNG (Variable Number of Gradients)
// ============================================================================

void Debayer::computeVNGGradients(const float* data, int width, int height,
                                  int x, int y, float gradients[8])
{
    // Clamped pixel accessor that returns the centre value for
    // out-of-bounds coordinates.
    auto get = [&](int dx, int dy) -> float {
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            return data[y * width + x];
        }
        return data[ny * width + nx];
    };

    const float centre = data[y * width + x];

    // Gradient in each of the 8 compass directions.
    // Each gradient sums two absolute differences spanning 2-pixel steps.
    gradients[0] = std::abs(centre - get( 0, -2)) + std::abs(get( 0, -1) - get( 0, -3)); // N
    gradients[1] = std::abs(centre - get( 2, -2)) + std::abs(get( 1, -1) - get( 3, -3)); // NE
    gradients[2] = std::abs(centre - get( 2,  0)) + std::abs(get( 1,  0) - get( 3,  0)); // E
    gradients[3] = std::abs(centre - get( 2,  2)) + std::abs(get( 1,  1) - get( 3,  3)); // SE
    gradients[4] = std::abs(centre - get( 0,  2)) + std::abs(get( 0,  1) - get( 0,  3)); // S
    gradients[5] = std::abs(centre - get(-2,  2)) + std::abs(get(-1,  1) - get(-3,  3)); // SW
    gradients[6] = std::abs(centre - get(-2,  0)) + std::abs(get(-1,  0) - get(-3,  0)); // W
    gradients[7] = std::abs(centre - get(-2, -2)) + std::abs(get(-1, -1) - get(-3, -3)); // NW
}

void Debayer::vngInterpolate(const float* data, int width, int height,
                             int x, int y, int redRow, int redCol,
                             int blueRow, int blueCol,
                             float& r, float& g, float& b)
{
    // Clamped pixel accessor.
    auto get = [&](int dx, int dy) -> float {
        int nx = std::clamp(x + dx, 0, width  - 1);
        int ny = std::clamp(y + dy, 0, height - 1);
        return data[ny * width + nx];
    };

    const float centre = get(0, 0);

    // --- Compute and threshold gradients ---
    float gradients[8];
    computeVNGGradients(data, width, height, x, y, gradients);

    float minGrad = gradients[0];
    for (int i = 1; i < 8; ++i) {
        minGrad = std::min(minGrad, gradients[i]);
    }
    const float threshold = minGrad * 1.5f;

    // Select directions whose gradient does not exceed the threshold.
    bool  useDir[8];
    int   numDirs = 0;
    for (int i = 0; i < 8; ++i) {
        useDir[i] = (gradients[i] <= threshold);
        if (useDir[i]) ++numDirs;
    }

    if (numDirs == 0) {
        r = g = b = centre;
        return;
    }

    // Unit direction vectors for the 8 compass points.
    static const int dirX[8] = { 0,  1,  1,  1,  0, -1, -1, -1};
    static const int dirY[8] = {-1, -1,  0,  1,  1,  1,  0, -1};

    // --- Accumulate colour contributions from selected directions ---
    float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f;
    int   count = 0;

    if (isRed(x, y, redRow, redCol)) {
        r = centre;
        for (int i = 0; i < 8; ++i) {
            if (!useDir[i]) continue;
            const int dx = dirX[i];
            const int dy = dirY[i];
            sumG += (get(dx, 0) + get(0, dy)) * 0.5f;
            sumB += get(dx, dy);
            ++count;
        }
        g = (count > 0) ? sumG / count : centre;
        b = (count > 0) ? sumB / count : centre;

    } else if (isBlue(x, y, blueRow, blueCol)) {
        b = centre;
        for (int i = 0; i < 8; ++i) {
            if (!useDir[i]) continue;
            const int dx = dirX[i];
            const int dy = dirY[i];
            sumG += (get(dx, 0) + get(0, dy)) * 0.5f;
            sumR += get(dx, dy);
            ++count;
        }
        g = (count > 0) ? sumG / count : centre;
        r = (count > 0) ? sumR / count : centre;

    } else {
        // Green pixel site.
        g = centre;
        const bool redHorizontal = (y % 2 == redRow);

        for (int i = 0; i < 8; ++i) {
            if (!useDir[i]) continue;
            if (redHorizontal) {
                sumR += (get(-1, 0) + get(1, 0)) * 0.5f;
                sumB += (get(0, -1) + get(0, 1)) * 0.5f;
            } else {
                sumB += (get(-1, 0) + get(1, 0)) * 0.5f;
                sumR += (get(0, -1) + get(0, 1)) * 0.5f;
            }
            ++count;
        }
        r = (count > 0) ? sumR / count : centre;
        b = (count > 0) ? sumB / count : centre;
    }
}

bool Debayer::vng(const ImageBuffer& input, ImageBuffer& output,
                  BayerPattern pattern)
{
    if (input.channels() != 1) {
        return false;
    }

    const int width  = input.width();
    const int height = input.height();

    output.resize(width, height, 3);

    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(pattern, redRow, redCol, blueRow, blueCol);

    const float* src = input.data().data();
    float*       dst = output.data().data();

    // VNG requires a minimum 3-pixel border for full gradient support.
    static const int kBorder = 3;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {

            float r, g, b;

            if (x < kBorder || x >= width  - kBorder ||
                y < kBorder || y >= height - kBorder)
            {
                // Fall back to the raw CFA value for border pixels.
                const float val = src[y * width + x];
                r = g = b = val;
            } else {
                vngInterpolate(src, width, height, x, y,
                               redRow, redCol, blueRow, blueCol,
                               r, g, b);
            }

            const int idx = (y * width + x) * 3;
            dst[idx + 0] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
        }
    }

    output.setMetadata(input.metadata());
    return true;
}

// ============================================================================
//  Super-Pixel (2x2 Binning)
// ============================================================================

bool Debayer::superpixel(const ImageBuffer& input, ImageBuffer& output,
                         BayerPattern pattern)
{
    if (input.channels() != 1) {
        return false;
    }

    const int width     = input.width();
    const int height    = input.height();
    const int outWidth  = width  / 2;
    const int outHeight = height / 2;

    output.resize(outWidth, outHeight, 3);

    int redRow, redCol, blueRow, blueCol;
    getPatternOffsets(pattern, redRow, redCol, blueRow, blueCol);

    const float* src = input.data().data();
    float*       dst = output.data().data();

    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {

            const int srcX = x * 2;
            const int srcY = y * 2;

            // Read the four pixels of the 2x2 Bayer cell.
            const float p00 = src[ srcY      * width + srcX    ];
            const float p10 = src[ srcY      * width + srcX + 1];
            const float p01 = src[(srcY + 1) * width + srcX    ];
            const float p11 = src[(srcY + 1) * width + srcX + 1];

            float r, g, b;

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
                    break;
            }

            const int idx = (y * outWidth + x) * 3;
            dst[idx + 0] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
        }
    }

    output.setMetadata(input.metadata());
    return true;
}

// ============================================================================
//  AHD (Adaptive Homogeneity-Directed)
// ============================================================================

bool Debayer::ahd(const ImageBuffer& input, ImageBuffer& output,
                  BayerPattern pattern)
{
    if (input.channels() != 1) {
        return false;
    }

    const int width  = input.width();
    const int height = input.height();

    output.resize(width, height, 3);

    int rRow, rCol, bRow, bCol;
    getPatternOffsets(pattern, rRow, rCol, bRow, bCol);

    const float* src = input.data().data();
    float*       dst = output.data().data();

    // ------------------------------------------------------------------
    // Pre-pass: Baseline green interpolation via bilinear to avoid zeros
    //           at the boundaries of the advanced steps.
    // ------------------------------------------------------------------
    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r, g, b;
            bilinearPixel(src, width, height, x, y, rRow, rCol, bRow, bCol, r, g, b);
            const int idx = (y * width + x) * 3;
            dst[idx + 1] = g; // We only need green for initialization
        }
    }

    // ------------------------------------------------------------------
    // Step 1: Interpolate green channel in both horizontal and vertical
    //         directions using Laplacian-corrected linear interpolation.
    // ------------------------------------------------------------------
    std::vector<float> gH(width * height);
    std::vector<float> gV(width * height);

    #pragma omp parallel for
    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {
            const int idx = y * width + x;

            if (isGreen(x, y, rRow, rCol, bRow, bCol)) {
                gH[idx] = gV[idx] = src[idx];
            } else {
                gH[idx] = (src[idx - 1] + src[idx + 1]) * 0.5f
                         + (2.0f * src[idx] - src[idx - 2] - src[idx + 2]) * 0.25f;

                gV[idx] = (src[idx - width] + src[idx + width]) * 0.5f
                         + (2.0f * src[idx] - src[idx - 2 * width] - src[idx + 2 * width]) * 0.25f;
            }
        }
    }

    // ------------------------------------------------------------------
    // Step 2: Choose the direction with higher local homogeneity.
    // ------------------------------------------------------------------
    #pragma omp parallel for
    for (int y = 4; y < height - 4; ++y) {
        for (int x = 4; x < width - 4; ++x) {

            float hHomogeneity = 0.0f;
            float vHomogeneity = 0.0f;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nIdx = (y + dy) * width + (x + dx);
                    hHomogeneity += std::abs(gH[nIdx] - gH[nIdx + 1])
                                  + std::abs(gH[nIdx] - gH[nIdx + width]);
                    vHomogeneity += std::abs(gV[nIdx] - gV[nIdx + 1])
                                  + std::abs(gV[nIdx] - gV[nIdx + width]);
                }
            }

            const int idx = y * width + x;
            dst[idx * 3 + 1] = (hHomogeneity < vHomogeneity) ? gH[idx] : gV[idx];
        }
    }

    // ------------------------------------------------------------------
    // Step 3: Interpolate red and blue channels using green-guided
    //         ratio correction (colour-ratio constancy assumption).
    // ------------------------------------------------------------------
    #pragma omp parallel for
    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {

            const int idx = y * width + x;
            const float g  = dst[idx * 3 + 1];
            float r = 0.0f, b = 0.0f;

            if (isRed(x, y, rRow, rCol)) {
                r = src[idx];
                // Blue at red site: average ratio of 4 diagonal blue neighbours.
                const float bRatio =
                    (src[(y-1)*width + x-1] / dst[((y-1)*width + x-1)*3 + 1] +
                     src[(y-1)*width + x+1] / dst[((y-1)*width + x+1)*3 + 1] +
                     src[(y+1)*width + x-1] / dst[((y+1)*width + x-1)*3 + 1] +
                     src[(y+1)*width + x+1] / dst[((y+1)*width + x+1)*3 + 1]) * 0.25f;
                b = g * bRatio;

            } else if (isBlue(x, y, bRow, bCol)) {
                b = src[idx];
                // Red at blue site: average ratio of 4 diagonal red neighbours.
                const float rRatio =
                    (src[(y-1)*width + x-1] / dst[((y-1)*width + x-1)*3 + 1] +
                     src[(y-1)*width + x+1] / dst[((y-1)*width + x+1)*3 + 1] +
                     src[(y+1)*width + x-1] / dst[((y+1)*width + x-1)*3 + 1] +
                     src[(y+1)*width + x+1] / dst[((y+1)*width + x+1)*3 + 1]) * 0.25f;
                r = g * rRatio;

            } else {
                // Green site: red and blue are on adjacent rows/columns.
                const bool redOnRow = (y % 2 == rRow);
                if (redOnRow) {
                    r = g * (src[idx-1] / dst[(idx-1)*3+1] +
                             src[idx+1] / dst[(idx+1)*3+1]) * 0.5f;
                    b = g * (src[idx-width] / dst[(idx-width)*3+1] +
                             src[idx+width] / dst[(idx+width)*3+1]) * 0.5f;
                } else {
                    b = g * (src[idx-1] / dst[(idx-1)*3+1] +
                             src[idx+1] / dst[(idx+1)*3+1]) * 0.5f;
                    r = g * (src[idx-width] / dst[(idx-width)*3+1] +
                             src[idx+width] / dst[(idx+width)*3+1]) * 0.5f;
                }
            }

            dst[idx * 3 + 0] = r;
            dst[idx * 3 + 2] = b;
        }
    }

    // Border fallback (4-pixel strip)
    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x < 4 || x >= width - 4 || y < 4 || y >= height - 4) {
                float r, g, b;
                bilinearPixel(src, width, height, x, y, rRow, rCol, bRow, bCol, r, g, b);
                const int idx = (y * width + x) * 3;
                dst[idx + 0] = r;
                dst[idx + 1] = g;
                dst[idx + 2] = b;
            }
        }
    }

    output.setMetadata(input.metadata());
    return true;
}

// ============================================================================
//  RCD (Ratio Corrected Demosaicing)
// ============================================================================

bool Debayer::rcd(const ImageBuffer& input, ImageBuffer& output,
                  BayerPattern pattern)
{
    if (input.channels() != 1) {
        return false;
    }

    const int width  = input.width();
    const int height = input.height();

    output.resize(width, height, 3);

    int rRow, rCol, bRow, bCol;
    getPatternOffsets(pattern, rRow, rCol, bRow, bCol);

    const float* src = input.data().data();
    float*       dst = output.data().data();

    // ------------------------------------------------------------------
    // Pre-pass: Baseline green interpolation via bilinear to avoid zeros
    //           at the boundaries of the advanced steps.
    // ------------------------------------------------------------------
    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r, g, b;
            bilinearPixel(src, width, height, x, y, rRow, rCol, bRow, bCol, r, g, b);
            const int idx = (y * width + x) * 3;
            dst[idx + 1] = g; 
        }
    }

    // ------------------------------------------------------------------
    // Step 1: Green channel interpolation with directional edge detection.
    // ------------------------------------------------------------------
    #pragma omp parallel for
    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {

            const int idx = y * width + x;
            float g;

            if (isGreen(x, y, rRow, rCol, bRow, bCol)) {
                g = src[idx];
            } else {
                const float hGrad = std::abs(src[idx - 1]     - src[idx + 1]);
                const float vGrad = std::abs(src[idx - width] - src[idx + width]);

                if (hGrad < vGrad) {
                    g = (src[idx - 1] + src[idx + 1]) * 0.5f;
                } else if (vGrad < hGrad) {
                    g = (src[idx - width] + src[idx + width]) * 0.5f;
                } else {
                    g = (src[idx - 1] + src[idx + 1] +
                         src[idx - width] + src[idx + width]) * 0.25f;
                }
            }

            dst[idx * 3 + 1] = g;
        }
    }

    // ------------------------------------------------------------------
    // Step 2: Red and blue interpolation using neighbour colour-to-green
    //         ratios (ratio-corrected method).
    // ------------------------------------------------------------------
    #pragma omp parallel for
    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {

            const int idx = y * width + x;
            const float g = dst[idx * 3 + 1];
            float r, b;

            if (isRed(x, y, rRow, rCol)) {
                r = src[idx];
                // Interpolate blue via diagonal neighbour ratios.
                const float b00 = src[(y-1)*width + (x-1)] / dst[((y-1)*width + (x-1))*3 + 1];
                const float b01 = src[(y-1)*width + (x+1)] / dst[((y-1)*width + (x+1))*3 + 1];
                const float b10 = src[(y+1)*width + (x-1)] / dst[((y+1)*width + (x-1))*3 + 1];
                const float b11 = src[(y+1)*width + (x+1)] / dst[((y+1)*width + (x+1))*3 + 1];
                b = g * (b00 + b01 + b10 + b11) * 0.25f;

            } else if (isBlue(x, y, bRow, bCol)) {
                b = src[idx];
                // Interpolate red via diagonal neighbour ratios.
                const float r00 = src[(y-1)*width + (x-1)] / dst[((y-1)*width + (x-1))*3 + 1];
                const float r01 = src[(y-1)*width + (x+1)] / dst[((y-1)*width + (x+1))*3 + 1];
                const float r10 = src[(y+1)*width + (x-1)] / dst[((y+1)*width + (x-1))*3 + 1];
                const float r11 = src[(y+1)*width + (x+1)] / dst[((y+1)*width + (x+1))*3 + 1];
                r = g * (r00 + r01 + r10 + r11) * 0.25f;

            } else {
                // Green site: red and blue are axis-aligned neighbours.
                if (y % 2 == rRow) {
                    const float rL = src[idx - 1]     / dst[(idx - 1)    *3 + 1];
                    const float rR = src[idx + 1]     / dst[(idx + 1)    *3 + 1];
                    r = g * (rL + rR) * 0.5f;

                    const float bU = src[idx - width] / dst[(idx - width)*3 + 1];
                    const float bD = src[idx + width] / dst[(idx + width)*3 + 1];
                    b = g * (bU + bD) * 0.5f;
                } else {
                    const float bL = src[idx - 1]     / dst[(idx - 1)    *3 + 1];
                    const float bR = src[idx + 1]     / dst[(idx + 1)    *3 + 1];
                    b = g * (bL + bR) * 0.5f;

                    const float rU = src[idx - width] / dst[(idx - width)*3 + 1];
                    const float rD = src[idx + width] / dst[(idx + width)*3 + 1];
                    r = g * (rU + rD) * 0.5f;
                }
            }

            dst[idx * 3 + 0] = r;
            dst[idx * 3 + 2] = b;
        }
    }

    // Border fallback (2-pixel strip)
    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x < 2 || x >= width - 2 || y < 2 || y >= height - 2) {
                float r, g, b;
                bilinearPixel(src, width, height, x, y, rRow, rCol, bRow, bCol, r, g, b);
                const int idx = (y * width + x) * 3;
                dst[idx + 0] = r;
                dst[idx + 1] = g;
                dst[idx + 2] = b;
            }
        }
    }

    output.setMetadata(input.metadata());
    return true;
}

bool Debayer::edgeAware(const ImageBuffer& input, ImageBuffer& output,
                        BayerPattern pattern)
{
    if (input.channels() != 1) return false;

    const int width  = input.width();
    const int height = input.height();
    output.resize(width, height, 3);

    int rRow, rCol, bRow, bCol;
    getPatternOffsets(pattern, rRow, rCol, bRow, bCol);

    const float* src = input.data().data();
    float*       dst = output.data().data();

    // Boundary-safe pixel access (clamping)
    auto getPixel = [&](int x, int y) -> float {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return src[y * width + x];
    };

    // Edge-aware interpolation weight based on local intensity difference
    auto edgeWeight = [&](int x, int y, int nx, int ny) -> float {
        float diff = std::abs(getPixel(x, y) - getPixel(nx, ny));
        return 1.0f / (1.0f + diff * 10.0f);
    };

    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            const int idx = (y * width + x) * 3;

            if (isRed(x, y, rRow, rCol)) {
                r = getPixel(x, y);
                // Green: 4-way weighted neighbours
                float w1 = edgeWeight(x, y, x-1, y);
                float w2 = edgeWeight(x, y, x+1, y);
                float w3 = edgeWeight(x, y, x, y-1);
                float w4 = edgeWeight(x, y, x, y+1);
                g = (getPixel(x-1,y)*w1 + getPixel(x+1,y)*w2 + getPixel(x,y-1)*w3 + getPixel(x,y+1)*w4) / (w1+w2+w3+w4);

                // Blue: 4-way diagonal weighted neighbours
                w1 = edgeWeight(x, y, x-1, y-1);
                w2 = edgeWeight(x, y, x+1, y-1);
                w3 = edgeWeight(x, y, x-1, y+1);
                w4 = edgeWeight(x, y, x+1, y+1);
                b = (getPixel(x-1,y-1)*w1 + getPixel(x+1,y-1)*w2 + getPixel(x-1,y+1)*w3 + getPixel(x+1,y+1)*w4) / (w1+w2+w3+w4);

            } else if (isBlue(x, y, bRow, bCol)) {
                b = getPixel(x, y);
                // Green: 4-way weighted neighbours
                float w1 = edgeWeight(x, y, x-1, y);
                float w2 = edgeWeight(x, y, x+1, y);
                float w3 = edgeWeight(x, y, x, y-1);
                float w4 = edgeWeight(x, y, x, y+1);
                g = (getPixel(x-1,y)*w1 + getPixel(x+1,y)*w2 + getPixel(x,y-1)*w3 + getPixel(x,y+1)*w4) / (w1+w2+w3+w4);

                // Red: 4-way diagonal weighted neighbours
                w1 = edgeWeight(x, y, x-1, y-1);
                w2 = edgeWeight(x, y, x+1, y-1);
                w3 = edgeWeight(x, y, x-1, y+1);
                w4 = edgeWeight(x, y, x+1, y+1);
                r = (getPixel(x-1,y-1)*w1 + getPixel(x+1,y-1)*w2 + getPixel(x-1,y+1)*w3 + getPixel(x+1,y+1)*w4) / (w1+w2+w3+w4);

            } else {
                g = getPixel(x, y);
                bool rOnRow = (y % 2 == rRow);
                if (rOnRow) {
                    float w1 = edgeWeight(x, y, x-1, y);
                    float w2 = edgeWeight(x, y, x+1, y);
                    r = (getPixel(x-1,y)*w1 + getPixel(x+1,y)*w2) / (w1 + w2);
                    w1 = edgeWeight(x, y, x, y-1);
                    w2 = edgeWeight(x, y, x, y+1);
                    b = (getPixel(x,y-1)*w1 + getPixel(x,y+1)*w2) / (w1 + w2);
                } else {
                    float w1 = edgeWeight(x, y, x-1, y);
                    float w2 = edgeWeight(x, y, x+1, y);
                    b = (getPixel(x-1,y)*w1 + getPixel(x+1,y)*w2) / (w1 + w2);
                    w1 = edgeWeight(x, y, x, y-1);
                    w2 = edgeWeight(x, y, x, y+1);
                    r = (getPixel(x,y-1)*w1 + getPixel(x,y+1)*w2) / (w1 + w2);
                }
            }
            dst[idx + 0] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
        }
    }
    output.setMetadata(input.metadata());
    return true;
}

} // namespace Preprocessing