#include "PerfectPaletteRunner.h"
#include <algorithm>
#include <cmath>
#include <numeric>

PerfectPaletteRunner::PerfectPaletteRunner(QObject* parent) : QObject(parent) {}

void PerfectPaletteRunner::applyStatisticalStretch(ImageBuffer& buffer, float targetMedian) {
    if (!buffer.isValid()) return;
    
    int w = buffer.width();
    int h = buffer.height();
    int c = buffer.channels();
    size_t numPixels = (size_t)w * h;
    float* data = buffer.data().data();

    // Loop over channels
    for (int ch = 0; ch < c; ++ch) {
        // 1. Get Median via Histogram (Approximate but fast)
        // ImageBuffer::getChannelMedian uses stepping for speed on large images
        float med = buffer.getChannelMedian(ch);
        
        // 2. We still need detailed stats for stdDev, but maybe we can just scan
        // Scan for min, accumulate mean/stdDev
        // Ideally we do this in one pass or use sampled stats. 
        // For speed, let's use a strided sample if the image is huge
        
        const int MAX_SAMPLES = 200000;

        
        // Gather samples for StdDev & Min
        std::vector<float> samples;
        samples.reserve(MAX_SAMPLES + 1000);
        
        // Efficient parallel accumulation for statistical accuracy on large images.
        // O(N) complexity avoids sorting overhead.
        
        float minVal = 1.0f;
        double sum = 0.0;
        double sq_sum = 0.0;
        
        #pragma omp parallel for reduction(min:minVal) reduction(+:sum, sq_sum)
        for (size_t i = 0; i < numPixels; ++i) {
            float v = data[i * c + ch];
            if (v < minVal) minVal = v;
            sum += v;
            sq_sum += (double)v * v;
        }
        
        float mean = (float)(sum / numPixels);
        float stdDev = std::sqrt(std::max(0.0, sq_sum / numPixels - (double)mean * mean));

        // Black Point: BP = max(min, median - 2.8 * stdDev)
        float bp = std::max(minVal, med - 2.8f * stdDev);
        float denom = 1.0f - bp;
        if (std::abs(denom) < 1e-6f) denom = 1e-6f;

        // Rescale: (val - bp) / (1 - bp)
        #pragma omp parallel for
        for (size_t i = 0; i < numPixels; ++i) {
            float val = (data[i * c + ch] - bp) / denom;
            data[i * c + ch] = std::max(0.0f, std::min(1.0f, val));
        }

        // Re-calculate Median for MTF
        // Because the transform is monotonic, newMedian = (oldMedian - bp) / denom
        float newMed = (med - bp) / denom;
        newMed = std::max(0.0f, std::min(1.0f, newMed));

        if (newMed > 0 && newMed < 1.0f) {
            // Find 'm' that maps newMed to targetMedian
            float m = (newMed * (targetMedian - 1.0f)) / (newMed * (2.0f * targetMedian - 1.0f) - targetMedian);
            m = std::max(0.0001f, std::min(0.9999f, m));
            float m_1 = m - 1.0f;
            float m2_1 = 2.0f * m - 1.0f;
            
            #pragma omp parallel for
            for (size_t i = 0; i < numPixels; ++i) {
                float x = data[i * c + ch];
                // MTF(x, m) = (m-1)x / ((2m-1)x - m)
                // Compute MTF: (m-1)x / ((2m-1)x - m)
                // Check denominator for singularity.
                float den = m2_1 * x - m;
                if (std::abs(den) < 1e-9f) den = 1e-9f; // Avoid divide by zero
                data[i * c + ch] = (m_1 * x) / den;
            }
        }
    }
}

bool PerfectPaletteRunner::run(const ImageBuffer* ha, const ImageBuffer* oiii, const ImageBuffer* sii,
                              ImageBuffer& output, const PerfectPaletteParams& params, QString* errorMsg) {
    if (!oiii || (!ha && !sii)) {
        if (errorMsg) *errorMsg = "OIII and either Ha or SII are required.";
        return false;
    }

    // Determine target dimensions
    int w = 0, h = 0;
    if (ha) { w = ha->width(); h = ha->height(); }
    else if (oiii) { w = oiii->width(); h = oiii->height(); }
    else if (sii) { w = sii->width(); h = sii->height(); }

    output.resize(w, h, 3);
    
    // Create working copies if stretching is needed
    ImageBuffer fHa, fOiii, fSii;
    if (ha) fHa = *ha;
    if (oiii) fOiii = *oiii;
    if (sii) fSii = *sii;

    if (params.applyStatisticalStretch) {
        if (ha) applyStatisticalStretch(fHa);
        if (oiii) applyStatisticalStretch(fOiii);
        if (sii) applyStatisticalStretch(fSii);
    }
    
    if (params.haFactor != 1.0f && fHa.isValid()) fHa.multiply(params.haFactor);
    if (params.oiiiFactor != 1.0f && fOiii.isValid()) fOiii.multiply(params.oiiiFactor);
    if (params.siiFactor != 1.0f && fSii.isValid()) fSii.multiply(params.siiFactor);

    // Substitute mission channels
    if (!ha && sii) fHa = fSii;
    if (!sii && ha) fSii = fHa;

    if (params.paletteName == "SHO") mapSHO(fHa, fOiii, fSii, output);
    else if (params.paletteName == "HOO") mapGeneric(fHa, fOiii, fOiii, output);
    else if (params.paletteName == "HSO") mapGeneric(fHa, fSii, fOiii, output);
    else if (params.paletteName == "HOS") mapGeneric(fHa, fOiii, fSii, output);
    else if (params.paletteName == "OSS") mapGeneric(fOiii, fSii, fSii, output);
    else if (params.paletteName == "OHH") mapGeneric(fOiii, fHa, fHa, output);
    else if (params.paletteName == "OSH") mapGeneric(fOiii, fSii, fHa, output);
    else if (params.paletteName == "OHS") mapGeneric(fOiii, fHa, fSii, output);
    else if (params.paletteName == "HSS") mapGeneric(fHa, fSii, fSii, output);
    else if (params.paletteName == "Foraxx") mapForaxx(fHa, fOiii, fSii, output);
    else if (params.paletteName == "Realistic1") mapRealistic1(fHa, fOiii, fSii, output);
    else if (params.paletteName == "Realistic2") mapRealistic2(fHa, fOiii, fSii, output);
    else {
        // Default to SHO
        mapSHO(fHa, fOiii, fSii, output);
    }

    return true;
}

void PerfectPaletteRunner::mapSHO(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out) {
    // SHO -> R=SII, G=Ha, B=OIII
    mapGeneric(sii, ha, oiii, out);
}

void PerfectPaletteRunner::mapGeneric(const ImageBuffer& rCh, const ImageBuffer& gCh, const ImageBuffer& bCh, ImageBuffer& out) {
    int w = out.width();
    int h = out.height();
    float* data = out.data().data();
    #pragma omp parallel for
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        data[i * 3 + 0] = rCh.getPixelFlat(i, 0);
        data[i * 3 + 1] = gCh.getPixelFlat(i, 0);
        data[i * 3 + 2] = bCh.getPixelFlat(i, 0);
    }
}

void PerfectPaletteRunner::mapForaxx(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out) {
    int w = out.width();
    int h = out.height();
    float* data = out.data().data();
    #pragma omp parallel for
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        float hVal = ha.getPixelFlat(i, 0);
        float oVal = oiii.getPixelFlat(i, 0);
        float sVal = sii.getPixelFlat(i, 0);

        // Foraxx approximation: Mix Ha and SII based on OIII intensity
        float t = std::pow(std::max(1e-6f, oVal), 1.0f - std::max(1e-6f, oVal));
        float r = t * sVal + (1.0f - t) * hVal;
        
        float t2 = hVal * oVal;
        float g = std::pow(t2, 1.0f - t2) * hVal + (1.0f - std::pow(t2, 1.0f - t2)) * oVal;
        float b = oVal;

        data[i * 3 + 0] = r;
        data[i * 3 + 1] = g;
        data[i * 3 + 2] = b;
    }
}

void PerfectPaletteRunner::mapRealistic1(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out) {
    int w = out.width();
    int h = out.height();
    float* data = out.data().data();
    #pragma omp parallel for
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        float hVal = ha.getPixelFlat(i, 0);
        float oVal = oiii.getPixelFlat(i, 0);
        float sVal = sii.getPixelFlat(i, 0);

        data[i * 3 + 0] = (hVal + sVal) * 0.5f;
        data[i * 3 + 1] = 0.3f * hVal + 0.7f * oVal;
        data[i * 3 + 2] = 0.9f * oVal + 0.1f * hVal;
    }
}

void PerfectPaletteRunner::mapRealistic2(const ImageBuffer& ha, const ImageBuffer& oiii, const ImageBuffer& sii, ImageBuffer& out) {
    int w = out.width();
    int h = out.height();
    float* data = out.data().data();
    #pragma omp parallel for
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        float hVal = ha.getPixelFlat(i, 0);
        float oVal = oiii.getPixelFlat(i, 0);
        float sVal = sii.getPixelFlat(i, 0);

        data[i * 3 + 0] = 0.7f * hVal + 0.3f * sVal;
        data[i * 3 + 1] = 0.3f * sVal + 0.7f * oVal;
        data[i * 3 + 2] = oVal;
    }
}
