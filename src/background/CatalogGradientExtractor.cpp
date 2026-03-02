#include "CatalogGradientExtractor.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <cmath>
#include <cstring>

namespace Background {

static cv::Mat extractLargeScale(const cv::Mat& src, int blurSigma, bool protectStars) {
    cv::Mat map;
    if (protectStars) {
        // Downsample → morphological open (removes stars) → blur → upsample
        constexpr int kDownscale = 4;
        cv::Mat down;
        cv::resize(src, down, cv::Size(), 1.0 / kDownscale, 1.0 / kDownscale, cv::INTER_AREA);

        int morphRadius = std::max(1, blurSigma / kDownscale / 4);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
                                                    cv::Size(2 * morphRadius + 1, 2 * morphRadius + 1));
        cv::Mat opened;
        cv::morphologyEx(down, opened, cv::MORPH_OPEN, kernel);

        int sigma = std::max(1, blurSigma / kDownscale);
        cv::GaussianBlur(opened, opened, cv::Size(0, 0), sigma);
        cv::resize(opened, map, src.size(), 0, 0, cv::INTER_CUBIC);
    } else {
        cv::GaussianBlur(src, map, cv::Size(0, 0), blurSigma);
    }
    return map;
}

static cv::Mat toLuminance(const cv::Mat& src) {
    if (src.channels() == 1) return src;
    cv::Mat gray;
    // ImageBuffer is RGB-interleaved, NOT BGR
    cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
    return gray;
}

bool CatalogGradientExtractor::extract(ImageBuffer& target,
                                       const ImageBuffer& reference,
                                       const Options& opts,
                                       std::function<void(int)> /*progress*/,
                                       const bool* /*cancelFlag*/)
{
    if (target.width() != reference.width() || target.height() != reference.height())
        return false;

    ImageBuffer gradMap = computeGradientMap(target, reference, opts);
    if (!gradMap.isValid()) return false;

    if (opts.outputGradientMap) {
        target = gradMap;
        return true;
    }

    // Subtract gradient map from target, clamping to [0, 1]
    auto& tgtData = target.data();
    const auto& gradData = gradMap.data();
    const size_t n = tgtData.size();
    for (size_t i = 0; i < n; ++i) {
        tgtData[i] = std::max(0.0f, std::min(1.0f, tgtData[i] - gradData[i]));
    }
    return true;
}

ImageBuffer CatalogGradientExtractor::computeGradientMap(const ImageBuffer& target,
                                                          const ImageBuffer& reference,
                                                          const Options& opts)
{
    ImageBuffer result;
    int W = target.width();
    int H = target.height();
    if (W != reference.width() || H != reference.height()) return result;

    int tgtCh = target.channels();
    int refCh = reference.channels();

    // Wrap ImageBuffer data as cv::Mat (no copy)
    int cvTypeTgt = tgtCh == 3 ? CV_32FC3 : CV_32FC1;
    cv::Mat tgtMat(H, W, cvTypeTgt, const_cast<float*>(target.data().data()));

    int cvTypeRef = refCh == 3 ? CV_32FC3 : CV_32FC1;
    cv::Mat refMat(H, W, cvTypeRef, const_cast<float*>(reference.data().data()));

    // Extract luminance for both images (ImageBuffer is RGB, not BGR)
    cv::Mat refLuma = toLuminance(refMat);
    cv::Mat tgtLuma = toLuminance(tgtMat);

    // Generate large-scale (low-frequency) maps
    cv::Mat refLargeScale = extractLargeScale(refLuma, opts.blurScale, opts.protectStars);
    cv::Mat tgtLumaLarge  = extractLargeScale(tgtLuma, opts.blurScale, opts.protectStars);

    // Compute affine intensity matching: matched = a * (ref - refMean) + tgtMean
    // This properly aligns both the mean and the contrast of the reference to the target
    cv::Scalar tgtMeanS, tgtStdS, refMeanS, refStdS;
    cv::meanStdDev(tgtLumaLarge, tgtMeanS, tgtStdS);
    cv::meanStdDev(refLargeScale, refMeanS, refStdS);

    double a = (refStdS[0] > 1e-8) ? (tgtStdS[0] / refStdS[0]) : 1.0;
    double tgtMean = tgtMeanS[0];
    double refMean = refMeanS[0];

    // refMatched = a * (refLargeScale - refMean) + tgtMean
    cv::Mat refMatched;
    refLargeScale.convertTo(refMatched, -1, a, tgtMean - a * refMean);

    // Build per-channel gradient
    // For each target channel: extract its large-scale, then grad = tgtChanLarge - refMatched
    // This uses the SAME matched reference for all channels → preserves inter-channel ratios
    std::vector<cv::Mat> tgtChannels;
    if (tgtCh == 3) {
        cv::split(tgtMat, tgtChannels);
    } else {
        tgtChannels.push_back(tgtMat);
    }

    std::vector<cv::Mat> gradChannels;
    for (int c = 0; c < tgtCh; ++c) {
        cv::Mat chanLarge = extractLargeScale(tgtChannels[c], opts.blurScale, opts.protectStars);

        // Per-channel affine match against channel's own mean, using luminance-derived scale
        // This ensures channels share the same gradient shape but preserve individual offsets
        cv::Scalar chanMeanS, chanStdS;
        cv::meanStdDev(chanLarge, chanMeanS, chanStdS);

        // Offset the matched reference to this channel's mean level
        cv::Mat refChanMatched;
        refLargeScale.convertTo(refChanMatched, -1, a, chanMeanS[0] - a * refMean);

        cv::Mat grad = chanLarge - refChanMatched;

        // Smooth to suppress any residual high-frequency artifacts
        int smoothSigma = std::max(1, opts.blurScale / 2);
        cv::GaussianBlur(grad, grad, cv::Size(0, 0), smoothSigma);

        gradChannels.push_back(grad);
    }

    cv::Mat mergedGrad;
    if (gradChannels.size() == 3) {
        cv::merge(gradChannels, mergedGrad);
    } else {
        mergedGrad = gradChannels[0];
    }

    // Copy into result ImageBuffer
    result.resize(W, H, tgtCh);
    std::memcpy(result.data().data(), mergedGrad.ptr<float>(), result.size() * sizeof(float));

    return result;
}

} // namespace Background
