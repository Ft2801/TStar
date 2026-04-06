// ============================================================================
// ChannelOps.cpp
// Implementation of channel-level image operations for astronomical
// image processing: channel extraction/combination, luminance,
// debayering, continuum subtraction, multiscale decomposition,
// narrowband normalization, and NB-to-RGB star synthesis.
// ============================================================================

#include "ChannelOps.h"
#include "StatisticalStretch.h"
#include "../photometry/StarDetector.h"
#include "../preprocessing/Debayer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <random>
#include <numeric>

#ifdef _OPENMP
#include <omp.h>
#endif

// ============================================================================
// Anonymous namespace: file-local helper functions
// ============================================================================
namespace {

/**
 * @brief Extract a mono channel from an ImageBuffer.
 *        Uses the red channel if the source is multi-channel.
 */
std::vector<float> extractMono(const ImageBuffer& img)
{
    int w  = img.width();
    int h  = img.height();
    int ch = img.channels();
    const float* d = img.data().data();

    std::vector<float> mono(w * h);
    if (ch == 1) {
        for (int i = 0; i < w * h; ++i) {
            mono[i] = d[i];
        }
    } else {
        for (int i = 0; i < w * h; ++i) {
            mono[i] = d[i * ch];  // Red channel
        }
    }
    return mono;
}

/**
 * @brief Compute the median of a float vector.
 */
float computeMedian(const std::vector<float>& v)
{
    if (v.empty()) return 0.0f;

    std::vector<float> tmp(v);
    size_t mid = tmp.size() / 2;
    std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
    return tmp[mid];
}

/**
 * @brief Compute the median of a specific channel in interleaved RGB data.
 */
float channelMedian(const std::vector<float>& rgb, int w, int h, int ch)
{
    size_t n = static_cast<size_t>(w) * h;
    std::vector<float> vals(n);
    for (size_t i = 0; i < n; ++i) {
        vals[i] = rgb[i * 3 + ch];
    }
    return computeMedian(vals);
}

/**
 * @brief Compute the Mean Absolute Deviation (MAD) of a channel
 *        in interleaved RGB data.
 */
float channelMAD(const std::vector<float>& rgb, int w, int h, int ch)
{
    size_t n = static_cast<size_t>(w) * h;

    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += rgb[i * 3 + ch];
    }
    float mean = static_cast<float>(sum / n);

    double mad = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mad += std::abs(rgb[i * 3 + ch] - mean);
    }
    return static_cast<float>(mad / n);
}

} // anonymous namespace

// ============================================================================
// Channel Extraction and Combination
// ============================================================================

std::vector<ImageBuffer> ChannelOps::extractChannels(const ImageBuffer& src)
{
    std::vector<ImageBuffer> channels;
    if (!src.isValid()) return channels;

    ImageBuffer::ReadLock lock(&src);

    int width      = src.width();
    int height     = src.height();
    int srcChannels = src.channels();

    if (srcChannels < 3) {
        return {};
    }

    const float* srcPtr = src.data().data();

    // Create one mono buffer per RGB channel
    for (int i = 0; i < 3; ++i) {
        std::vector<float> data(width * height);
        for (int p = 0; p < width * height; ++p) {
            data[p] = srcPtr[p * srcChannels + i];
        }

        ImageBuffer mono;
        mono.setData(width, height, 1, data);
        mono.setMetadata(src.metadata());
        channels.push_back(std::move(mono));
    }

    return channels;
}

ImageBuffer ChannelOps::combineChannels(const ImageBuffer& r,
                                        const ImageBuffer& g,
                                        const ImageBuffer& b)
{
    if (!r.isValid() || !g.isValid() || !b.isValid()) {
        return ImageBuffer();
    }

    ImageBuffer::ReadLock lockR(&r);
    ImageBuffer::ReadLock lockG(&g);
    ImageBuffer::ReadLock lockB(&b);

    // Validate dimension consistency
    if (r.width() != g.width()  || r.width()  != b.width() ||
        r.height() != g.height() || r.height() != b.height()) {
        std::cerr << "ChannelOps::combineChannels: dimension mismatch" << std::endl;
        return ImageBuffer();
    }

    int width  = r.width();
    int height = r.height();

    const float* rPtr = r.data().data();
    const float* gPtr = g.data().data();
    const float* bPtr = b.data().data();

    // Interleave RGB
    std::vector<float> outData(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        outData[i * 3 + 0] = rPtr[i];
        outData[i * 3 + 1] = gPtr[i];
        outData[i * 3 + 2] = bPtr[i];
    }

    ImageBuffer out;
    out.setData(width, height, 3, outData);
    out.setMetadata(r.metadata());
    return out;
}

// ============================================================================
// Noise Estimation
// ============================================================================

std::vector<float> ChannelOps::estimateNoiseSigma(const ImageBuffer& src)
{
    if (!src.isValid()) return {};

    ImageBuffer::ReadLock lock(&src);

    int w = src.width();
    int h = src.height();
    int c = src.channels();

    std::vector<float> sigmas(c);

    // Sub-sample stride for performance (every 4th pixel)
    int step = 4;
    if (w < 10 || h < 10) step = 1;

    const float* s = src.data().data();
    std::vector<float> sub;
    sub.reserve((w / step + 1) * (h / step + 1));

    for (int ch = 0; ch < c; ++ch) {
        sub.clear();

        for (int y = 0; y < h; y += step) {
            for (int x = 0; x < w; x += step) {
                sub.push_back(s[(y * w + x) * c + ch]);
            }
        }

        if (sub.empty()) {
            sigmas[ch] = 1e-5f;
            continue;
        }

        // Compute median
        size_t n = sub.size();
        std::nth_element(sub.begin(), sub.begin() + n / 2, sub.end());
        float med = sub[n / 2];

        // Compute MAD (Median Absolute Deviation)
        std::vector<float> absDevs(n);
        for (size_t i = 0; i < n; ++i) {
            absDevs[i] = std::abs(sub[i] - med);
        }
        std::nth_element(absDevs.begin(), absDevs.begin() + n / 2, absDevs.end());
        float mad = absDevs[n / 2];

        // Convert MAD to Gaussian sigma estimate
        float sigma = 1.4826f * mad;
        if (sigma < 1e-12f) sigma = 1e-12f;

        sigmas[ch] = sigma;
    }

    return sigmas;
}

// ============================================================================
// Luminance Computation
// ============================================================================

ImageBuffer ChannelOps::computeLuminance(
    const ImageBuffer& src,
    LumaMethod method,
    const std::vector<float>& customWeights,
    const std::vector<float>& customNoiseSigma)
{
    if (!src.isValid()) return ImageBuffer();

    ImageBuffer::ReadLock lock(&src);

    int w = src.width();
    int h = src.height();
    int c = src.channels();

    std::vector<float> dst(w * h);
    const float* s = src.data().data();

    // Fast path for single-channel input
    if (c == 1) {
        std::copy(s, s + w * h, dst.begin());
        ImageBuffer out;
        out.setData(w, h, 1, dst);
        out.setMetadata(src.metadata());
        return out;
    }

    // ---- Determine per-channel weights ----
    std::vector<float> weights(c, 0.0f);

    if (method == LumaMethod::MAX || method == LumaMethod::MEDIAN) {
        // These methods do not use weights
    } else if (method == LumaMethod::SNR) {
        // Inverse-variance weighting based on noise estimates
        std::vector<float> sigmas = customNoiseSigma;
        if (static_cast<int>(sigmas.size()) != c) {
            sigmas = estimateNoiseSigma(src);
        }

        float sumW = 0.0f;
        for (int i = 0; i < c; ++i) {
            weights[i] = 1.0f / (sigmas[i] * sigmas[i] + 1e-12f);
            sumW += weights[i];
        }

        if (sumW > 0.0f) {
            for (int i = 0; i < c; ++i) weights[i] /= sumW;
        } else {
            for (int i = 0; i < c; ++i) weights[i] = 1.0f / c;
        }
    } else {
        // Standard weighted luminance
        if (c >= 3) {
            weights[0] = getLumaWeightR(method, customWeights);
            weights[1] = getLumaWeightG(method, customWeights);
            weights[2] = getLumaWeightB(method, customWeights);
        } else if (c == 2) {
            weights[0] = 0.5f;
            weights[1] = 0.5f;
        }
    }

    // ---- Compute luminance per pixel ----
    for (int i = 0; i < w * h; ++i) {
        float val = 0.0f;

        if (method == LumaMethod::MAX) {
            val = s[i * c];
            for (int k = 1; k < c; ++k) {
                val = std::max(val, s[i * c + k]);
            }

        } else if (method == LumaMethod::MEDIAN) {
            // Optimized path for 3-channel median (sorting network)
            if (c == 3) {
                float v[3] = { s[i * c], s[i * c + 1], s[i * c + 2] };
                if (v[0] > v[1]) std::swap(v[0], v[1]);
                if (v[1] > v[2]) std::swap(v[1], v[2]);
                if (v[0] > v[1]) std::swap(v[0], v[1]);
                val = v[1];
            } else {
                std::vector<float> v(c);
                for (int k = 0; k < c; ++k) v[k] = s[i * c + k];
                std::nth_element(v.begin(), v.begin() + c / 2, v.end());
                val = v[c / 2];
            }

        } else {
            // Weighted sum
            for (int k = 0; k < c; ++k) {
                val += s[i * c + k] * weights[k];
            }
        }

        dst[i] = std::clamp(val, 0.0f, 1.0f);
    }

    ImageBuffer out;
    out.setData(w, h, 1, dst);
    out.setMetadata(src.metadata());
    return out;
}

// ============================================================================
// Luminance Recombination
// ============================================================================

bool ChannelOps::recombineLuminance(
    ImageBuffer& target,
    const ImageBuffer& sourceL,
    ColorSpaceMode csMode,
    float blend)
{
    if (!target.isValid() || !sourceL.isValid()) return false;
    if (sourceL.channels() != 1) return false;

    ImageBuffer::WriteLock lockTarget(&target);
    ImageBuffer::ReadLock  lockSource(&sourceL);

    if (target.width() != sourceL.width() || target.height() != sourceL.height()) {
        return false;
    }

    int w = target.width();
    int h = target.height();
    int c = target.channels();
    if (c < 3) return false;

    float*       rgb = target.data().data();
    const float* L   = sourceL.data().data();

    // Find maximum luminance for normalization
    float lumMax = 0.0f;
    for (int i = 0; i < w * h; ++i) {
        if (L[i] > lumMax) lumMax = L[i];
    }
    if (lumMax < 1e-12f) lumMax = 1.0f;

    // ---- Per-pixel luminance replacement ----
    for (int i = 0; i < w * h; ++i) {
        float r = rgb[i * c + 0];
        float g = rgb[i * c + 1];
        float b = rgb[i * c + 2];

        float newL = L[i] / lumMax;  // Normalized new luminance
        float nr, ng, nb;

        switch (csMode) {

        // ---- HSL color space ----
        case ColorSpaceMode::HSL: {
            float maxC = std::max({ r, g, b });
            float minC = std::min({ r, g, b });
            float vm   = maxC - minC;
            float hl = 0.0f, sl = 0.0f, ll = (maxC + minC) * 0.5f;

            if (vm > 0.0f) {
                sl = (ll <= 0.5f) ? (vm / (maxC + minC))
                                  : (vm / (2.0f - maxC - minC));

                float r2 = (maxC - r) / vm;
                float g2 = (maxC - g) / vm;
                float b2 = (maxC - b) / vm;

                if (r == maxC)
                    hl = (g == minC) ? 5.0f + b2 : 1.0f - g2;
                else if (g == maxC)
                    hl = (b == minC) ? 1.0f + r2 : 3.0f - b2;
                else
                    hl = (r == minC) ? 3.0f + g2 : 5.0f - r2;
                hl /= 6.0f;
            }

            // Replace lightness with blended new luminance
            float newll = newL;
            if (blend < 1.0f) {
                newll = ll * (1.0f - blend) + newL * blend;
            }

            // HSL to RGB conversion
            float v = (newll <= 0.5f) ? (newll * (1.0f + sl))
                                      : (newll + sl - newll * sl);
            if (v <= 0.0f) {
                nr = ng = nb = 0.0f;
            } else {
                float m   = newll + newll - v;
                float sv  = (v - m) / v;
                float h6  = hl * 6.0f;
                if (h6 >= 6.0f) h6 -= 6.0f;

                int   sextant = static_cast<int>(h6);
                float fract   = h6 - sextant;
                float vsf     = v * sv * fract;
                float mid1    = m + vsf;
                float mid2    = v - vsf;

                switch (sextant) {
                    case 0: nr = v;    ng = mid1; nb = m;    break;
                    case 1: nr = mid2; ng = v;    nb = m;    break;
                    case 2: nr = m;    ng = v;    nb = mid1; break;
                    case 3: nr = m;    ng = mid2; nb = v;    break;
                    case 4: nr = mid1; ng = m;    nb = v;    break;
                    case 5: nr = v;    ng = m;    nb = mid2; break;
                    default: nr = r;   ng = g;    nb = b;    break;
                }
            }
            break;
        }

        // ---- HSV color space ----
        case ColorSpaceMode::HSV: {
            float maxC = std::max({ r, g, b });
            float minC = std::min({ r, g, b });
            float vm   = maxC - minC;
            float hv = 0.0f, sv = 0.0f, vv = maxC;

            if (maxC > 0.0f) {
                sv = vm / maxC;
            }

            if (vm > 0.0f) {
                if (r == maxC)
                    hv = (g - b) / vm;
                else if (g == maxC)
                    hv = 2.0f + (b - r) / vm;
                else
                    hv = 4.0f + (r - g) / vm;
                hv /= 6.0f;
                if (hv < 0.0f) hv += 1.0f;
            }

            // Replace value with blended new luminance
            float newvv = newL;
            if (blend < 1.0f) {
                newvv = vv * (1.0f - blend) + newL * blend;
            }

            // HSV to RGB conversion
            if (sv <= 0.0f) {
                nr = ng = nb = newvv;
            } else {
                float h6 = hv * 6.0f;
                if (h6 >= 6.0f) h6 -= 6.0f;

                int   sector = static_cast<int>(h6);
                float fract  = h6 - sector;
                float p = newvv * (1.0f - sv);
                float q = newvv * (1.0f - sv * fract);
                float t = newvv * (1.0f - sv * (1.0f - fract));

                switch (sector) {
                    case 0: nr = newvv; ng = t;     nb = p;     break;
                    case 1: nr = q;     ng = newvv; nb = p;     break;
                    case 2: nr = p;     ng = newvv; nb = t;     break;
                    case 3: nr = p;     ng = q;     nb = newvv; break;
                    case 4: nr = t;     ng = p;     nb = newvv; break;
                    case 5: nr = newvv; ng = p;     nb = q;     break;
                    default: nr = r;    ng = g;     nb = b;     break;
                }
            }
            break;
        }

        // ---- CIE L*a*b* color space ----
        case ColorSpaceMode::CIELAB: {
            // RGB to XYZ (sRGB, D65 illuminant)
            float X = r * 0.4124564f + g * 0.3575761f + b * 0.1804375f;
            float Y = r * 0.2126729f + g * 0.7151522f + b * 0.0721750f;
            float Z = r * 0.0193339f + g * 0.1191920f + b * 0.9503041f;

            // XYZ to Lab
            const float Xn = 0.95047f, Yn = 1.0f, Zn = 1.08883f;
            auto labF = [](float t) -> float {
                return (t > 0.008856f) ? std::cbrt(t)
                                       : (7.787f * t + 16.0f / 116.0f);
            };

            float fx = labF(X / Xn);
            float fy = labF(Y / Yn);
            float fz = labF(Z / Zn);

            float astar = 500.0f * (fx - fy);
            float bstar = 200.0f * (fy - fz);

            // Replace L* with new luminance (scaled to 0-100 range)
            float newLstar = newL * 100.0f;
            if (blend < 1.0f) {
                float origLstar = 116.0f * fy - 16.0f;
                newLstar = origLstar * (1.0f - blend) + newLstar * blend;
            }

            // Lab to XYZ
            auto labFInv = [](float t) -> float {
                return (t > 0.206893f) ? (t * t * t)
                                       : ((t - 16.0f / 116.0f) / 7.787f);
            };

            float fy2 = (newLstar + 16.0f) / 116.0f;
            float fx2 = astar / 500.0f + fy2;
            float fz2 = fy2 - bstar / 200.0f;

            float X2 = Xn * labFInv(fx2);
            float Y2 = Yn * labFInv(fy2);
            float Z2 = Zn * labFInv(fz2);

            // XYZ to RGB (sRGB, D65 illuminant)
            nr =  3.2404542f * X2 - 1.5371385f * Y2 - 0.4985314f * Z2;
            ng = -0.9692660f * X2 + 1.8760108f * Y2 + 0.0415560f * Z2;
            nb =  0.0556434f * X2 - 0.2040259f * Y2 + 1.0572252f * Z2;
            break;
        }

        default:
            nr = r; ng = g; nb = b;
            break;
        }

        rgb[i * c + 0] = std::clamp(nr, 0.0f, 1.0f);
        rgb[i * c + 1] = std::clamp(ng, 0.0f, 1.0f);
        rgb[i * c + 2] = std::clamp(nb, 0.0f, 1.0f);
    }

    target.setModified(true);
    return true;
}

// ============================================================================
// Luminance Weight Helpers
// ============================================================================

float ChannelOps::getLumaWeightR(LumaMethod method,
                                 const std::vector<float>& customWeights)
{
    if (method == LumaMethod::CUSTOM && customWeights.size() >= 3) {
        return customWeights[0];
    }
    switch (method) {
        case LumaMethod::REC601:  return 0.299f;
        case LumaMethod::REC2020: return 0.2627f;
        case LumaMethod::AVERAGE: return 0.3333f;
        case LumaMethod::REC709:
        default:                  return 0.2126f;
    }
}

float ChannelOps::getLumaWeightG(LumaMethod method,
                                 const std::vector<float>& customWeights)
{
    if (method == LumaMethod::CUSTOM && customWeights.size() >= 3) {
        return customWeights[1];
    }
    switch (method) {
        case LumaMethod::REC601:  return 0.587f;
        case LumaMethod::REC2020: return 0.6780f;
        case LumaMethod::AVERAGE: return 0.3333f;
        case LumaMethod::REC709:
        default:                  return 0.7152f;
    }
}

float ChannelOps::getLumaWeightB(LumaMethod method,
                                 const std::vector<float>& customWeights)
{
    if (method == LumaMethod::CUSTOM && customWeights.size() >= 3) {
        return customWeights[2];
    }
    switch (method) {
        case LumaMethod::REC601:  return 0.114f;
        case LumaMethod::REC2020: return 0.0593f;
        case LumaMethod::AVERAGE: return 0.3333f;
        case LumaMethod::REC709:
        default:                  return 0.0722f;
    }
}

// ============================================================================
// Debayering
// ============================================================================

ImageBuffer ChannelOps::debayer(const ImageBuffer& mosaic,
                                const std::string& pattern,
                                const std::string& method)
{
    if (!mosaic.isValid() || mosaic.channels() != 1) {
        return ImageBuffer();
    }

    // Map string pattern to Preprocessing::BayerPattern
    Preprocessing::BayerPattern p = Preprocessing::BayerPattern::RGGB;
    if (pattern == "RGGB")      p = Preprocessing::BayerPattern::RGGB;
    else if (pattern == "BGGR") p = Preprocessing::BayerPattern::BGGR;
    else if (pattern == "GBRG") p = Preprocessing::BayerPattern::GBRG;
    else if (pattern == "GRBG") p = Preprocessing::BayerPattern::GRBG;
    else return ImageBuffer();

    ImageBuffer out;
    bool success = false;

    // Map method string to Debayer algorithm
    if (method == "edge") {
        success = Preprocessing::Debayer::edgeAware(mosaic, out, p);
    } else if (method == "bilinear") {
        success = Preprocessing::Debayer::bilinear(mosaic, out, p);
    } else if (method == "vng") {
        success = Preprocessing::Debayer::vng(mosaic, out, p);
    } else if (method == "ahd") {
        success = Preprocessing::Debayer::ahd(mosaic, out, p);
    } else if (method == "rcd") {
        success = Preprocessing::Debayer::rcd(mosaic, out, p);
    } else {
        // Default to edgeAware if unspecified or unknown
        success = Preprocessing::Debayer::edgeAware(mosaic, out, p);
    }

    if (!success) return ImageBuffer();

    return out;
}

float ChannelOps::computeDebayerScore(const ImageBuffer& rgb)
{
    if (!rgb.isValid() || rgb.channels() < 3) {
        return std::numeric_limits<float>::max();
    }

    ImageBuffer::ReadLock lock(&rgb);

    int w = rgb.width();
    int h = rgb.height();
    const float* data = rgb.data().data();

    // Score based on inter-channel gradient differences (zipper artifact metric)
    float score = 0.0f;
    int   count = 0;

    for (int y = 1; y < h - 1; y += 2) {
        for (int x = 1; x < w - 1; x += 2) {
            // Horizontal R-G gradient difference
            int idxL = (y * w + x - 1) * 3;
            int idxR = (y * w + x + 1) * 3;
            float rgDiffH = std::abs((data[idxL] - data[idxL + 1]) -
                                     (data[idxR] - data[idxR + 1]));

            // Vertical B-G gradient difference
            int idxT = ((y - 1) * w + x) * 3;
            int idxB = ((y + 1) * w + x) * 3;
            float bgDiffV = std::abs((data[idxT + 2] - data[idxT + 1]) -
                                     (data[idxB + 2] - data[idxB + 1]));

            score += rgDiffH + bgDiffV;
            count++;
        }
    }

    return (count > 0) ? score / count : 0.0f;
}

// ============================================================================
// Continuum Subtraction - Simple Legacy
// ============================================================================

ImageBuffer ChannelOps::continuumSubtract(const ImageBuffer& narrowband,
                                          const ImageBuffer& continuum,
                                          float qFactor)
{
    if (!narrowband.isValid() || !continuum.isValid()) {
        return ImageBuffer();
    }

    ImageBuffer::ReadLock lockNB(&narrowband);
    ImageBuffer::ReadLock lockCont(&continuum);

    if (narrowband.width()  != continuum.width() ||
        narrowband.height() != continuum.height()) {
        return ImageBuffer();
    }

    int w = narrowband.width();
    int h = narrowband.height();

    const float* nbData   = narrowband.data().data();
    const float* contData = continuum.data().data();
    int nbCh   = narrowband.channels();
    int contCh = continuum.channels();

    // Extract mono representation of each input
    std::vector<float> nb(w * h), cont(w * h);
    for (int i = 0; i < w * h; ++i) {
        nb[i]   = (nbCh   == 1) ? nbData[i]   : nbData[i * nbCh];
        cont[i] = (contCh == 1) ? contData[i]  : contData[i * contCh];
    }

    // Compute continuum median
    std::vector<float> contCopy = cont;
    std::nth_element(contCopy.begin(), contCopy.begin() + contCopy.size() / 2,
                     contCopy.end());
    float contMedian = contCopy[contCopy.size() / 2];

    // Subtract: result = NB - Q * (continuum - median)
    std::vector<float> result(w * h);
    for (int i = 0; i < w * h; ++i) {
        float val = nb[i] - qFactor * (cont[i] - contMedian);
        result[i] = std::clamp(val, 0.0f, 1.0f);
    }

    ImageBuffer out;
    out.setData(w, h, 1, result);
    out.setMetadata(narrowband.metadata());
    return out;
}

// ============================================================================
// Pedestal Removal
// ============================================================================

void ChannelOps::removePedestal(ImageBuffer& img)
{
    if (!img.isValid()) return;

    ImageBuffer::WriteLock lock(&img);

    int w = img.width();
    int h = img.height();
    int c = img.channels();
    float* data = img.data().data();
    size_t total = static_cast<size_t>(w) * h;

    if (total == 0) return;

    // Find per-channel minimum values
    std::vector<float> mins(c, std::numeric_limits<float>::max());
    for (size_t i = 0; i < total; ++i) {
        for (int ch = 0; ch < c; ++ch) {
            float val = data[i * c + ch];
            if (val < mins[ch]) mins[ch] = val;
        }
    }

    // Subtract per-channel minimums
    #pragma omp parallel for
    for (size_t i = 0; i < total; ++i) {
        for (int ch = 0; ch < c; ++ch) {
            float val = data[i * c + ch] - mins[ch];
            data[i * c + ch] = std::max(0.0f, val);
        }
    }
}

// ============================================================================
// Continuum Subtraction - Full Pipeline Sub-steps
// ============================================================================

void ChannelOps::assembleNBContRGB(const ImageBuffer& nb,
                                   const ImageBuffer& cont,
                                   std::vector<float>& rgbOut,
                                   int& w, int& h)
{
    ImageBuffer::ReadLock lockNB(&nb);
    ImageBuffer::ReadLock lockCont(&cont);

    w = nb.width();
    h = nb.height();
    size_t n = static_cast<size_t>(w) * h;

    std::vector<float> nbMono  = extractMono(nb);
    std::vector<float> cMono   = extractMono(cont);

    // Normalize integer-range data to [0,1] if necessary
    auto normalizeIfNeeded = [](std::vector<float>& v) {
        float mx = *std::max_element(v.begin(), v.end());
        if (mx > 1.5f) {
            float scale = (mx > 60000.0f) ? 65535.0f : 255.0f;
            for (float& x : v) x /= scale;
        }
    };
    normalizeIfNeeded(nbMono);
    normalizeIfNeeded(cMono);

    // Assemble: R = narrowband, G = continuum, B = continuum
    rgbOut.resize(n * 3);
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        rgbOut[i * 3 + 0] = nbMono[i];
        rgbOut[i * 3 + 1] = cMono[i];
        rgbOut[i * 3 + 2] = cMono[i];
    }
}

void ChannelOps::computeBackgroundPedestal(const std::vector<float>& rgb,
                                           int w, int h,
                                           float pedestal[3])
{
    // Walking dark-box algorithm: 200 random boxes of 25x25 pixels,
    // 25 iterations walking each box toward the darkest neighboring region.

    const int numBoxes   = 200;
    const int boxSize    = 25;
    const int iterations = 25;
    size_t n = static_cast<size_t>(w) * h;

    pedestal[0] = pedestal[1] = pedestal[2] = 0.0f;
    if (w < boxSize + 2 || h < boxSize + 2 || n == 0) return;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> distY(0, h - boxSize - 1);
    std::uniform_int_distribution<int> distX(0, w - boxSize - 1);

    struct Box {
        int y, x;
        float bestMedian;
    };

    std::vector<Box> boxes(numBoxes);
    for (int i = 0; i < numBoxes; ++i) {
        boxes[i].y = distY(rng);
        boxes[i].x = distX(rng);
        boxes[i].bestMedian = std::numeric_limits<float>::max();
    }

    // Compute combined-channel median of a patch
    auto patchMedian = [&](int py, int px) -> float {
        if (py < 0 || py + boxSize > h || px < 0 || px + boxSize > w) {
            return std::numeric_limits<float>::max();
        }

        std::vector<float> vals;
        vals.reserve(boxSize * boxSize * 3);
        for (int dy = 0; dy < boxSize; ++dy) {
            for (int dx = 0; dx < boxSize; ++dx) {
                size_t idx = (static_cast<size_t>(py + dy) * w + (px + dx)) * 3;
                vals.push_back(rgb[idx]);
                vals.push_back(rgb[idx + 1]);
                vals.push_back(rgb[idx + 2]);
            }
        }

        size_t mid = vals.size() / 2;
        std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
        return vals[mid];
    };

    // Iteratively walk each box toward the darkest neighborhood
    for (int iter = 0; iter < iterations; ++iter) {
        for (int i = 0; i < numBoxes; ++i) {
            float med = patchMedian(boxes[i].y, boxes[i].x);
            boxes[i].bestMedian = std::min(boxes[i].bestMedian, med);

            // Search 3x3 neighborhood for a darker box
            float bestNeighbor = std::numeric_limits<float>::max();
            int   bestDy = 0, bestDx = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int ny = boxes[i].y + dy * boxSize;
                    int nx = boxes[i].x + dx * boxSize;
                    float nm = patchMedian(ny, nx);
                    if (nm < bestNeighbor) {
                        bestNeighbor = nm;
                        bestDy = dy;
                        bestDx = dx;
                    }
                }
            }

            boxes[i].y += bestDy * boxSize;
            boxes[i].x += bestDx * boxSize;
        }
    }

    // Find the overall darkest box
    float darkest = std::numeric_limits<float>::max();
    int   darkIdx = 0;
    for (int i = 0; i < numBoxes; ++i) {
        float med = patchMedian(boxes[i].y, boxes[i].x);
        if (med < darkest) {
            darkest = med;
            darkIdx = i;
        }
    }

    // Compute per-channel medians of the darkest patch as background reference
    float bgRef[3] = { 0.0f, 0.0f, 0.0f };
    {
        int py = boxes[darkIdx].y, px = boxes[darkIdx].x;
        std::vector<float> chVals[3];
        for (int c = 0; c < 3; ++c) {
            chVals[c].reserve(boxSize * boxSize);
        }

        for (int dy = 0; dy < boxSize; ++dy) {
            for (int dx = 0; dx < boxSize; ++dx) {
                size_t idx = (static_cast<size_t>(py + dy) * w + (px + dx)) * 3;
                chVals[0].push_back(rgb[idx]);
                chVals[1].push_back(rgb[idx + 1]);
                chVals[2].push_back(rgb[idx + 2]);
            }
        }

        for (int c = 0; c < 3; ++c) {
            bgRef[c] = computeMedian(chVals[c]);
        }
    }

    // Compute per-channel image medians
    float chanMed[3];
    for (int c = 0; c < 3; ++c) {
        chanMed[c] = channelMedian(rgb, w, h, c);
    }

    // Pedestal: lift each channel toward its global median
    for (int c = 0; c < 3; ++c) {
        pedestal[c] = std::max(0.0f, chanMed[c] - bgRef[c]);
    }

    // Additionally lift G and B if their background is below the R reference
    float rRef = bgRef[0];
    for (int c = 1; c < 3; ++c) {
        if (bgRef[c] < rRef) {
            pedestal[c] += (rRef - bgRef[c]);
        }
    }
}

void ChannelOps::applyPedestal(std::vector<float>& rgb, int w, int h,
                               const float pedestal[3])
{
    size_t n = static_cast<size_t>(w) * h;
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        rgb[i * 3 + 0] = std::clamp(rgb[i * 3 + 0] + pedestal[0], 0.0f, 1.0f);
        rgb[i * 3 + 1] = std::clamp(rgb[i * 3 + 1] + pedestal[1], 0.0f, 1.0f);
        rgb[i * 3 + 2] = std::clamp(rgb[i * 3 + 2] + pedestal[2], 0.0f, 1.0f);
    }
}

void ChannelOps::normalizeRedToGreen(std::vector<float>& rgb, int w, int h,
                                     float& gain, float& offset)
{
    // Match red (narrowband) statistics to green (continuum) via MAD and median.
    // gain = MAD_green / MAD_red, offset = -gain * median_red + median_green
    float madR = channelMAD(rgb, w, h, 0);
    float madG = channelMAD(rgb, w, h, 1);
    float medR = channelMedian(rgb, w, h, 0);
    float medG = channelMedian(rgb, w, h, 1);

    gain   = (madR > 1e-9f) ? (madG / madR) : 1.0f;
    offset = -gain * medR + medG;

    size_t n = static_cast<size_t>(w) * h;
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        rgb[i * 3 + 0] = std::clamp(rgb[i * 3 + 0] * gain + offset, 0.0f, 1.0f);
    }
}

int ChannelOps::starBasedWhiteBalance(std::vector<float>& rgb, int w, int h,
                                      float threshold,
                                      float wbA[3], float wbB[3])
{
    // Create temporary ImageBuffer for star detection
    ImageBuffer tempBuf;
    tempBuf.setData(w, h, 3, rgb);

    // Detect stars in the green channel (continuum - most stars visible there)
    StarDetector detector;
    detector.setThresholdSigma(threshold);
    detector.setMaxStars(2000);
    auto stars = detector.detect(tempBuf, 1);  // channel 1 = green

    if (stars.size() < 3) {
        for (int c = 0; c < 3; ++c) { wbA[c] = 1.0f; wbB[c] = 0.0f; }
        return 0;
    }

    // ---- Measure per-channel flux via circular aperture photometry ----
    const int aperture = 5;  // Aperture radius in pixels
    size_t nStars = stars.size();

    std::vector<float> starFlux[3];
    for (auto& sf : starFlux) sf.reserve(nStars);

    for (const auto& star : stars) {
        int cx = static_cast<int>(star.x + 0.5);
        int cy = static_cast<int>(star.y + 0.5);

        // Skip stars too close to the edge or saturated
        if (cx - aperture < 0 || cx + aperture >= w ||
            cy - aperture < 0 || cy + aperture >= h) {
            continue;
        }
        if (star.saturated) continue;

        float chSum[3] = { 0.0f, 0.0f, 0.0f };
        int count = 0;

        for (int dy = -aperture; dy <= aperture; ++dy) {
            for (int dx = -aperture; dx <= aperture; ++dx) {
                if (dx * dx + dy * dy > aperture * aperture) continue;

                size_t idx = (static_cast<size_t>(cy + dy) * w + (cx + dx)) * 3;
                chSum[0] += rgb[idx + 0];
                chSum[1] += rgb[idx + 1];
                chSum[2] += rgb[idx + 2];
                count++;
            }
        }

        if (count > 0) {
            for (int c = 0; c < 3; ++c) {
                starFlux[c].push_back(chSum[c] / count);
            }
        }
    }

    int usedStars = static_cast<int>(starFlux[0].size());
    if (usedStars < 3) {
        for (int c = 0; c < 3; ++c) { wbA[c] = 1.0f; wbB[c] = 0.0f; }
        return usedStars;
    }

    // ---- Compute affine corrections to neutralize star colors ----
    // Fit y = a*x + b for each channel, where y = green reference
    for (int c = 0; c < 3; ++c) {
        if (c == 1) {
            wbA[c] = 1.0f; wbB[c] = 0.0f;
            continue;  // Green is the reference channel
        }

        double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
        for (int i = 0; i < usedStars; ++i) {
            double x = starFlux[c][i];
            double y = starFlux[1][i];
            sumX  += x;
            sumY  += y;
            sumXX += x * x;
            sumXY += x * y;
        }

        double det = usedStars * sumXX - sumX * sumX;
        if (std::abs(det) < 1e-12) {
            wbA[c] = 1.0f; wbB[c] = 0.0f;
        } else {
            wbA[c] = static_cast<float>((usedStars * sumXY - sumX * sumY) / det);
            wbB[c] = static_cast<float>((sumXX * sumY - sumX * sumXY) / det);
        }
    }

    // ---- Apply white balance and refit affine transform ----
    size_t n = static_cast<size_t>(w) * h;
    std::vector<float> corrected(rgb.size());

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        for (int c = 0; c < 3; ++c) {
            corrected[i * 3 + c] = std::clamp(
                rgb[i * 3 + c] * wbA[c] + wbB[c], 0.0f, 1.0f);
        }
    }

    // Recompute wbA/wbB as the affine fit from original to corrected
    for (int c = 0; c < 3; ++c) {
        int maxSamples = 100000;
        int stride = std::max(1, static_cast<int>(n / maxSamples));

        double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
        int cnt = 0;

        for (size_t i = 0; i < n; i += stride) {
            double x = rgb[i * 3 + c];
            double y = corrected[i * 3 + c];
            sumX  += x;
            sumY  += y;
            sumXX += x * x;
            sumXY += x * y;
            cnt++;
        }

        double det = cnt * sumXX - sumX * sumX;
        if (std::abs(det) < 1e-12) {
            wbA[c] = 1.0f; wbB[c] = 0.0f;
        } else {
            wbA[c] = static_cast<float>((cnt * sumXY - sumX * sumY) / det);
            wbB[c] = static_cast<float>((sumXX * sumY - sumX * sumXY) / det);
        }
    }

    rgb = std::move(corrected);
    return usedStars;
}

void ChannelOps::linearContinuumSubtract(const std::vector<float>& rgb,
                                         int w, int h,
                                         float Q, float greenMedian,
                                         std::vector<float>& result)
{
    size_t n = static_cast<size_t>(w) * h;
    result.resize(n);

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        float r   = rgb[i * 3 + 0];  // Narrowband
        float g   = rgb[i * 3 + 1];  // Continuum
        float val = r - Q * (g - greenMedian);
        result[i] = std::clamp(val, 0.0f, 1.0f);
    }
}

void ChannelOps::nonLinearFinalize(std::vector<float>& data, int w, int h,
                                   float targetMedian, float curvesBoost)
{
    // Three-step non-linear finalization:
    //   1. Statistical stretch to target median
    //   2. Subtract 70% of new median (residual pedestal removal)
    //   3. Curves adjustment for contrast enhancement

    size_t n = static_cast<size_t>(w) * h;

    // Step 1: Statistical stretch
    auto stats = StatisticalStretch::computeStats(data, 1, 0, 1, 5.0f, false);
    if (stats.denominator > 1e-12f) {
        float medRescaled = (stats.median - stats.blackpoint) / stats.denominator;
        medRescaled = std::clamp(medRescaled, 1e-6f, 1.0f - 1e-6f);

        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n); ++i) {
            float x = (data[i] - stats.blackpoint) / stats.denominator;
            x = std::clamp(x, 0.0f, 1.0f);
            data[i] = StatisticalStretch::stretchFormula(x, medRescaled,
                                                        targetMedian);
        }
    }

    // Step 2: Subtract 70% of median
    float newMedian = computeMedian(data);
    float subtract  = 0.7f * newMedian;

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        data[i] = std::clamp(data[i] - subtract, 0.0f, 1.0f);
    }

    // Step 3: Curves adjustment
    float curvesMedian = computeMedian(data);
    StatisticalStretch::applyCurvesAdjustment(data, curvesMedian, curvesBoost);
}

// ============================================================================
// Continuum Subtraction - Full Pipeline
// ============================================================================

ImageBuffer ChannelOps::continuumSubtractFull(
    const ImageBuffer& narrowband,
    const ImageBuffer& continuum,
    const ContinuumSubtractParams& params,
    ContinuumSubtractRecipe* recipe)
{
    if (!narrowband.isValid() || !continuum.isValid()) return ImageBuffer();
    if (narrowband.width()  != continuum.width() ||
        narrowband.height() != continuum.height()) {
        return ImageBuffer();
    }

    // Step 1: Assemble RGB composite (R=NB, G=Cont, B=Cont)
    std::vector<float> rgb;
    int w, h;
    assembleNBContRGB(narrowband, continuum, rgb, w, h);

    // Step 2: Background neutralization
    float ped[3];
    computeBackgroundPedestal(rgb, w, h, ped);
    applyPedestal(rgb, w, h, ped);

    // Step 3: Normalize red channel to green statistics
    float rnGain, rnOffset;
    normalizeRedToGreen(rgb, w, h, rnGain, rnOffset);

    // Step 4: Star-based white balance
    float wbA[3], wbB[3];
    int starCount = starBasedWhiteBalance(rgb, w, h, params.starThreshold,
                                          wbA, wbB);

    // Step 5: Linear continuum subtraction
    float greenMed = channelMedian(rgb, w, h, 1);
    std::vector<float> result;
    linearContinuumSubtract(rgb, w, h, params.qFactor, greenMed, result);

    // Save recipe if requested
    if (recipe) {
        for (int c = 0; c < 3; ++c) {
            recipe->pedestal[c] = ped[c];
            recipe->wbA[c]      = wbA[c];
            recipe->wbB[c]      = wbB[c];
        }
        recipe->rnormGain   = rnGain;
        recipe->rnormOffset = rnOffset;
        recipe->Q           = params.qFactor;
        recipe->greenMedian = greenMed;
        recipe->starCount   = starCount;
        recipe->valid       = true;
    }

    // Step 6: Optional non-linear finalization
    if (!params.outputLinear) {
        nonLinearFinalize(result, w, h, params.targetMedian, params.curvesBoost);
    }

    ImageBuffer out;
    out.setData(w, h, 1, result);
    out.setMetadata(narrowband.metadata());
    return out;
}

ImageBuffer ChannelOps::continuumSubtractWithRecipe(
    const ImageBuffer& narrowband,
    const ImageBuffer& continuum,
    const ContinuumSubtractRecipe& recipe,
    bool outputLinear,
    float targetMedian,
    float curvesBoost)
{
    if (!narrowband.isValid() || !continuum.isValid()) return ImageBuffer();
    if (!recipe.valid) return ImageBuffer();
    if (narrowband.width()  != continuum.width() ||
        narrowband.height() != continuum.height()) {
        return ImageBuffer();
    }

    // Assemble RGB composite
    std::vector<float> rgb;
    int w, h;
    assembleNBContRGB(narrowband, continuum, rgb, w, h);

    // Apply stored pedestal
    applyPedestal(rgb, w, h, recipe.pedestal);

    // Apply stored red normalization
    size_t n = static_cast<size_t>(w) * h;
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        rgb[i * 3 + 0] = std::clamp(
            rgb[i * 3 + 0] * recipe.rnormGain + recipe.rnormOffset,
            0.0f, 1.0f);
    }

    // Apply stored white balance
    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        for (int c = 0; c < 3; ++c) {
            rgb[i * 3 + c] = std::clamp(
                rgb[i * 3 + c] * recipe.wbA[c] + recipe.wbB[c],
                0.0f, 1.0f);
        }
    }

    // Linear subtraction with stored Q and green median
    std::vector<float> result;
    linearContinuumSubtract(rgb, w, h, recipe.Q, recipe.greenMedian, result);

    // Optional non-linear finalization
    if (!outputLinear) {
        nonLinearFinalize(result, w, h, targetMedian, curvesBoost);
    }

    ImageBuffer out;
    out.setData(w, h, 1, result);
    out.setMetadata(narrowband.metadata());
    return out;
}

// ============================================================================
// Multiscale Decomposition
// ============================================================================

void ChannelOps::gaussianBlur(const std::vector<float>& src,
                              std::vector<float>& dst,
                              int w, int h, int ch, float sigma)
{
    // Separable Gaussian blur: horizontal pass followed by vertical pass
    int ksize = std::max(3, 2 * static_cast<int>(std::round(3.0f * sigma)) + 1);
    int half  = ksize / 2;

    // Build normalized 1D Gaussian kernel
    std::vector<float> kernel(ksize);
    float ksum = 0.0f;
    for (int i = 0; i < ksize; ++i) {
        float x = static_cast<float>(i - half);
        kernel[i] = std::exp(-0.5f * x * x / (sigma * sigma));
        ksum += kernel[i];
    }
    for (int i = 0; i < ksize; ++i) {
        kernel[i] /= ksum;
    }

    size_t total = static_cast<size_t>(w) * h * ch;
    std::vector<float> temp(total);
    dst.resize(total);

    // Horizontal pass
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < ch; ++c) {
                float val = 0.0f;
                for (int k = -half; k <= half; ++k) {
                    int sx = std::clamp(x + k, 0, w - 1);
                    val += src[(static_cast<size_t>(y) * w + sx) * ch + c]
                           * kernel[k + half];
                }
                temp[(static_cast<size_t>(y) * w + x) * ch + c] = val;
            }
        }
    }

    // Vertical pass
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < ch; ++c) {
                float val = 0.0f;
                for (int k = -half; k <= half; ++k) {
                    int sy = std::clamp(y + k, 0, h - 1);
                    val += temp[(static_cast<size_t>(sy) * w + x) * ch + c]
                           * kernel[k + half];
                }
                dst[(static_cast<size_t>(y) * w + x) * ch + c] = val;
            }
        }
    }
}

void ChannelOps::multiscaleDecompose(const std::vector<float>& img,
                                     int w, int h, int ch,
                                     int layers, float baseSigma,
                                     std::vector<std::vector<float>>& details,
                                     std::vector<float>& residual)
{
    details.clear();
    details.reserve(layers);

    size_t total = static_cast<size_t>(w) * h * ch;
    std::vector<float> current = img;

    for (int k = 0; k < layers; ++k) {
        float sigma = baseSigma * std::pow(2.0f, static_cast<float>(k));
        std::vector<float> blurred;
        gaussianBlur(current, blurred, w, h, ch, sigma);

        // Detail layer = current - blurred
        std::vector<float> detail(total);
        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(total); ++i) {
            detail[i] = current[i] - blurred[i];
        }

        details.push_back(std::move(detail));
        current = std::move(blurred);
    }

    residual = std::move(current);
}

std::vector<float> ChannelOps::multiscaleReconstruct(
    const std::vector<std::vector<float>>& details,
    const std::vector<float>& residual,
    [[maybe_unused]] int pixelCount)
{
    std::vector<float> out = residual;

    for (const auto& d : details) {
        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(out.size()); ++i) {
            out[i] += d[i];
        }
    }

    return out;
}

void ChannelOps::softThreshold(std::vector<float>& data, float t)
{
    if (t <= 0.0f) return;

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(data.size()); ++i) {
        float a    = std::abs(data[i]);
        float sign = (data[i] > 0.0f) ? 1.0f : ((data[i] < 0.0f) ? -1.0f : 0.0f);
        data[i]    = sign * std::max(0.0f, a - t);
    }
}

float ChannelOps::robustSigma(const std::vector<float>& data)
{
    if (data.empty()) return 1e-6f;

    // Sub-sample for performance on large datasets
    const size_t maxSamples = 500000;
    std::vector<float> samples;

    if (data.size() > maxSamples) {
        samples.reserve(maxSamples);
        size_t step = data.size() / maxSamples;
        for (size_t i = 0; i < data.size(); i += step) {
            float v = data[i];
            if (std::isfinite(v)) samples.push_back(v);
        }
    } else {
        samples.reserve(data.size());
        for (float v : data) {
            if (std::isfinite(v)) samples.push_back(v);
        }
    }

    if (samples.empty()) return 1e-6f;

    // Compute MAD-based sigma estimate
    size_t n = samples.size();
    std::nth_element(samples.begin(), samples.begin() + n / 2, samples.end());
    float med = samples[n / 2];

    std::vector<float> absdev(n);
    for (size_t i = 0; i < n; ++i) {
        absdev[i] = std::abs(samples[i] - med);
    }
    std::nth_element(absdev.begin(), absdev.begin() + n / 2, absdev.end());
    float mad = absdev[n / 2];

    if (mad <= 0.0f) {
        // Fallback to standard deviation
        double sum = 0.0, sum2 = 0.0;
        for (float v : samples) {
            sum  += v;
            sum2 += static_cast<double>(v) * v;
        }
        double var = sum2 / n - (sum / n) * (sum / n);
        float s = static_cast<float>(std::sqrt(std::max(0.0, var)));
        return (s > 0.0f) ? s : 1e-6f;
    }

    float sigma = 1.4826f * mad;
    return (sigma > 0.0f) ? sigma : 1e-6f;
}

void ChannelOps::applyLayerOps(std::vector<float>& layer,
                               const LayerCfg& cfg,
                               float sigma, int layerIndex, int mode)
{
    // Disabled layer: zero out
    if (!cfg.enabled) {
        std::fill(layer.begin(), layer.end(), 0.0f);
        return;
    }

    // Linear mode: apply gain only
    if (mode == 1) {
        if (std::abs(cfg.biasGain - 1.0f) > 1e-6f) {
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(layer.size()); ++i) {
                layer[i] *= cfg.biasGain;
            }
        }
        return;
    }

    // ---- Sigma-based thresholding mode ----
    float sigmaF = (sigma > 0.0f) ? sigma : robustSigma(layer);

    // 1) MMT-style noise reduction
    if (cfg.denoise > 0.0f) {
        float scale = std::pow(std::pow(2.0f, static_cast<float>(layerIndex)), 0.5f);
        float t_dn  = cfg.denoise * 3.0f * scale * sigmaF;

        if (t_dn > 0.0f) {
            std::vector<float> denoised = layer;
            softThreshold(denoised, t_dn);

            float blend = cfg.denoise;
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(layer.size()); ++i) {
                layer[i] = (1.0f - blend) * layer[i] + blend * denoised[i];
            }
        }
    }

    // 2) Soft threshold with blending
    if (cfg.thr > 0.0f) {
        float t = cfg.thr * sigmaF;
        if (t > 0.0f) {
            std::vector<float> thresholded = layer;
            softThreshold(thresholded, t);

            float amt = cfg.amount;
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(layer.size()); ++i) {
                layer[i] = (1.0f - amt) * layer[i] + amt * thresholded[i];
            }
        }
    }

    // 3) Gain adjustment
    if (std::abs(cfg.biasGain - 1.0f) > 1e-6f) {
        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(layer.size()); ++i) {
            layer[i] *= cfg.biasGain;
        }
    }
}

// ============================================================================
// Narrowband Normalization
// ============================================================================

void ChannelOps::normalizeChannel(std::vector<float>& ch, int n,
                                  float blackpoint, int mode)
{
    if (n == 0) return;

    // Compute minimum and median
    std::vector<float> sorted = ch;
    std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
    float med = sorted[n / 2];
    float mn  = *std::min_element(ch.begin(), ch.end());

    // Black point: M = min + blackpoint * (median - min)
    float M = mn + blackpoint * (med - mn);

    if (mode == 1) {
        // Non-linear normalization: (x - M) / (1 - M), clamped to [0, 1]
        float denom = (1.0f - M);
        if (denom < 1e-9f) denom = 1e-9f;

        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n); ++i) {
            float v = (ch[i] - M) / denom;
            ch[i] = std::clamp(v, 0.0f, 1.0f);
        }
    } else {
        // Linear normalization: subtract M, rescale by maximum
        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n); ++i) {
            ch[i] = std::max(0.0f, ch[i] - M);
        }

        float mx = *std::max_element(ch.begin(), ch.end());
        if (mx > 1e-9f) {
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i) {
                ch[i] /= mx;
            }
        }
    }
}

std::vector<float> ChannelOps::normalizeNarrowband(
    const std::vector<float>& haIn,
    const std::vector<float>& oiiiIn,
    const std::vector<float>& siiIn,
    int w, int h,
    const NBNParams& params)
{
    size_t n = static_cast<size_t>(w) * h;

    // Work on copies to preserve original data
    std::vector<float> ha   = haIn;
    std::vector<float> oiii = oiiiIn;
    std::vector<float> sii  = siiIn;

    bool isHOO = (params.scenario == 3);

    // ---- Step 1: Normalize each channel (black point + mode) ----
    if (!ha.empty())   normalizeChannel(ha,   static_cast<int>(n), params.blackpoint, params.mode);
    if (!oiii.empty()) normalizeChannel(oiii, static_cast<int>(n), params.blackpoint, params.mode);
    if (!sii.empty())  normalizeChannel(sii,  static_cast<int>(n), params.blackpoint, params.mode);

    // ---- Step 2: Apply channel boost factors ----
    if (isHOO) {
        if (!oiii.empty() && std::abs(params.oiiiboost - 1.0f) > 1e-6f) {
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i)
                oiii[i] = std::clamp(oiii[i] * params.oiiiboost, 0.0f, 1.0f);
        }
    } else {
        if (!sii.empty() && std::abs(params.siiboost - 1.0f) > 1e-6f) {
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i)
                sii[i] = std::clamp(sii[i] * params.siiboost, 0.0f, 1.0f);
        }
        if (!oiii.empty() && std::abs(params.oiiiboost2 - 1.0f) > 1e-6f) {
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i)
                oiii[i] = std::clamp(oiii[i] * params.oiiiboost2, 0.0f, 1.0f);
        }
    }

    // ---- Step 3: Highlight recovery and reduction ----
    if (std::abs(params.hlrecover - 1.0f) > 1e-6f) {
        auto applyHL = [&](std::vector<float>& ch) {
            if (ch.empty()) return;
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i) {
                if (ch[i] > 0.5f) {
                    float excess = ch[i] - 0.5f;
                    ch[i] = 0.5f + excess * params.hlrecover;
                    ch[i] = std::clamp(ch[i], 0.0f, 1.0f);
                }
            }
        };
        applyHL(ha); applyHL(oiii); applyHL(sii);
    }

    if (std::abs(params.hlreduct - 1.0f) > 1e-6f) {
        auto applyHLR = [&](std::vector<float>& ch) {
            if (ch.empty()) return;
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i) {
                ch[i] = std::clamp(ch[i] * params.hlreduct, 0.0f, 1.0f);
            }
        };
        applyHLR(ha); applyHLR(oiii); applyHLR(sii);
    }

    // ---- Step 4: Brightness adjustment ----
    if (std::abs(params.brightness - 1.0f) > 1e-6f) {
        auto applyBright = [&](std::vector<float>& ch) {
            if (ch.empty()) return;
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i) {
                ch[i] = std::clamp(ch[i] * params.brightness, 0.0f, 1.0f);
            }
        };
        applyBright(ha); applyBright(oiii); applyBright(sii);
    }

    // ---- Step 5: Map channels to RGB based on scenario ----
    std::vector<float> rgb(n * 3);

    if (isHOO) {
        // HOO palette: R=Ha, G/B computed from blend mode
        float hab = params.hablend;

        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n); ++i) {
            float haV = ha.empty()   ? 0.0f : ha[i];
            float oV  = oiii.empty() ? 0.0f : oiii[i];

            rgb[i * 3 + 0] = haV;  // R = Ha

            float gVal = 0.0f, bVal = 0.0f;
            switch (params.blendmode) {
                case 0:  // Screen
                    gVal = 1.0f - (1.0f - oV) * (1.0f - haV * hab);
                    bVal = oV;
                    break;
                case 1:  // Add
                    gVal = std::clamp(oV + haV * hab, 0.0f, 1.0f);
                    bVal = oV;
                    break;
                case 2:  // Linear Dodge
                    gVal = std::clamp(oV + haV * hab, 0.0f, 1.0f);
                    bVal = oV;
                    break;
                case 3:  // Normal (weighted blend)
                default:
                    gVal = hab * haV + (1.0f - hab) * oV;
                    bVal = oV;
                    break;
            }

            rgb[i * 3 + 1] = std::clamp(gVal, 0.0f, 1.0f);
            rgb[i * 3 + 2] = std::clamp(bVal, 0.0f, 1.0f);
        }
    } else {
        // SHO / HSO / HOS palette mapping
        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n); ++i) {
            float haV = ha.empty()   ? 0.0f : ha[i];
            float oV  = oiii.empty() ? 0.0f : oiii[i];
            float sV  = sii.empty()  ? 0.0f : sii[i];

            float r, g, b;
            switch (params.scenario) {
                case 0:  // SHO: R=SII, G=Ha, B=OIII
                    r = sV; g = haV; b = oV; break;
                case 1:  // HSO: R=Ha, G=SII, B=OIII
                    r = haV; g = sV; b = oV; break;
                case 2:  // HOS: R=Ha, G=OIII, B=SII
                default:
                    r = haV; g = oV; b = sV; break;
            }

            rgb[i * 3 + 0] = std::clamp(r, 0.0f, 1.0f);
            rgb[i * 3 + 1] = std::clamp(g, 0.0f, 1.0f);
            rgb[i * 3 + 2] = std::clamp(b, 0.0f, 1.0f);
        }
    }

    // ---- Step 6: Lightness preservation (non-linear mode only) ----
    if (params.mode == 1 && params.lightness > 0) {
        std::vector<float>* lumSrc = nullptr;
        switch (params.lightness) {
            case 1: break;                                      // Use current RGB luminance
            case 2: lumSrc = &ha;   break;
            case 3: lumSrc = isHOO ? &oiii : &sii; break;
            case 4: lumSrc = &oiii; break;
        }

        if (params.lightness == 1 || (lumSrc && !lumSrc->empty())) {
            #pragma omp parallel for
            for (long i = 0; i < static_cast<long>(n); ++i) {
                float origL;
                if (params.lightness == 1) {
                    origL = 0.2126f * rgb[i*3+0] + 0.7152f * rgb[i*3+1]
                            + 0.0722f * rgb[i*3+2];
                } else {
                    origL = (*lumSrc)[i];
                }

                float curL = 0.2126f * rgb[i*3+0] + 0.7152f * rgb[i*3+1]
                             + 0.0722f * rgb[i*3+2];
                if (curL > 1e-9f) {
                    float ratio = origL / curL;
                    rgb[i*3+0] = std::clamp(rgb[i*3+0] * ratio, 0.0f, 1.0f);
                    rgb[i*3+1] = std::clamp(rgb[i*3+1] * ratio, 0.0f, 1.0f);
                    rgb[i*3+2] = std::clamp(rgb[i*3+2] * ratio, 0.0f, 1.0f);
                }
            }
        }
    }

    // ---- Step 7: SCNR (not applied to HOO) ----
    if (params.scnr && !isHOO) {
        applySCNR(rgb, w, h);
    }

    return rgb;
}

// ============================================================================
// NB -> RGB Stars
// ============================================================================

void ChannelOps::applySCNR(std::vector<float>& rgb, int w, int h)
{
    // Average neutral protection: G = min(G, (R + B) / 2)
    size_t n = static_cast<size_t>(w) * h;

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        float r = rgb[i * 3 + 0];
        float g = rgb[i * 3 + 1];
        float b = rgb[i * 3 + 2];
        float neutral = (r + b) * 0.5f;
        rgb[i * 3 + 1] = std::min(g, neutral);
    }
}

void ChannelOps::adjustSaturation(std::vector<float>& rgb, int w, int h,
                                  float factor)
{
    if (std::abs(factor - 1.0f) < 1e-6f) return;

    size_t n = static_cast<size_t>(w) * h;

    #pragma omp parallel for
    for (long i = 0; i < static_cast<long>(n); ++i) {
        float r = rgb[i * 3 + 0];
        float g = rgb[i * 3 + 1];
        float b = rgb[i * 3 + 2];

        float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;

        rgb[i * 3 + 0] = std::clamp(lum + factor * (r - lum), 0.0f, 1.0f);
        rgb[i * 3 + 1] = std::clamp(lum + factor * (g - lum), 0.0f, 1.0f);
        rgb[i * 3 + 2] = std::clamp(lum + factor * (b - lum), 0.0f, 1.0f);
    }
}

std::vector<float> ChannelOps::combineNBtoRGBStars(
    const std::vector<float>& haIn,
    const std::vector<float>& oiiiIn,
    const std::vector<float>& siiIn,
    const std::vector<float>& oscIn,
    int w, int h, int oscChannels,
    const NBStarsParams& params)
{
    size_t n = static_cast<size_t>(w) * h;
    std::vector<float> rgb(n * 3);

    bool hasOSC  = !oscIn.empty();
    bool hasHa   = !haIn.empty();
    bool hasOiii = !oiiiIn.empty();
    bool hasSii  = !siiIn.empty();

    float ratio = params.ratio;

    if (hasOSC) {
        // Use OSC broadband as base, blend narrowband channels
        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n); ++i) {
            float oscR, oscG, oscB;
            if (oscChannels >= 3) {
                oscR = oscIn[i * 3 + 0];
                oscG = oscIn[i * 3 + 1];
                oscB = oscIn[i * 3 + 2];
            } else {
                oscR = oscG = oscB = oscIn[i];
            }

            float haV = hasHa   ? haIn[i]   : oscR;
            float oV  = hasOiii ? oiiiIn[i] : oscB;
            float sV  = hasSii  ? siiIn[i]  : oscR;

            rgb[i * 3 + 0] = std::clamp(0.5f * oscR + 0.5f * sV, 0.0f, 1.0f);
            rgb[i * 3 + 1] = std::clamp(ratio * haV + (1.0f - ratio) * oscG,
                                        0.0f, 1.0f);
            rgb[i * 3 + 2] = oV;
        }
    } else {
        // NB-only: R = 0.5*(Ha + SII), G = ratio*Ha + (1-ratio)*OIII, B = OIII
        const std::vector<float>& sRef = hasSii ? siiIn : haIn;

        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n); ++i) {
            float haV = haIn[i];
            float oV  = oiiiIn[i];
            float sV  = sRef[i];

            rgb[i * 3 + 0] = std::clamp(0.5f * haV + 0.5f * sV, 0.0f, 1.0f);
            rgb[i * 3 + 1] = std::clamp(ratio * haV + (1.0f - ratio) * oV,
                                        0.0f, 1.0f);
            rgb[i * 3 + 2] = oV;
        }
    }

    // Non-linear star stretch: f(x) = (3^k * x) / ((3^k - 1) * x + 1)
    if (params.starStretch) {
        float t   = std::pow(3.0f, params.stretchFactor);
        float tm1 = t - 1.0f;

        #pragma omp parallel for
        for (long i = 0; i < static_cast<long>(n * 3); ++i) {
            float x = rgb[i];
            rgb[i] = std::clamp((t * x) / (tm1 * x + 1.0f), 0.0f, 1.0f);
        }
    }

    // SCNR green noise reduction
    if (params.applySCNR) {
        applySCNR(rgb, w, h);
    }

    // Saturation adjustment
    adjustSaturation(rgb, w, h, params.saturation);

    return rgb;
}