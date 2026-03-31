#include "CatalogGradientExtractor.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <cmath>
#include <cstring>

namespace Background {

// ---------------------------------------------------------------------------
// fitPolynomialBackground
// Fits a 2D polynomial (up to Degree 3) to valid sky pixels to model true gradients.
// Used to perfectly interpolate background through the masked nebulas without bumps.
// ---------------------------------------------------------------------------
static cv::Mat fitPolynomialBackground(const cv::Mat& src, const cv::Mat& mask, int degree) {
    constexpr int kSize = 128; // heavy downsample for speed and robustness
    double scale = std::min(1.0, (double)kSize / std::max(src.cols, src.rows));
    
    cv::Mat smallSrc, smallMask;
    cv::resize(src, smallSrc, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::resize(mask, smallMask, cv::Size(), scale, scale, cv::INTER_NEAREST);
    
    // COLLECT POINTS: Use ONLY the pixels that are clean background.
    // We avoid blurring before resizing to prevent structural bleeding.
    std::vector<cv::Point> pts;
    std::vector<float> vals;
    pts.reserve(smallSrc.rows * smallSrc.cols);
    vals.reserve(smallSrc.rows * smallSrc.cols);
    
    for (int y = 0; y < smallSrc.rows; ++y) {
        for (int x = 0; x < smallSrc.cols; ++x) {
            if (smallMask.at<uchar>(y, x) > 128) {
                pts.push_back(cv::Point(x, y));
                vals.push_back(smallSrc.at<float>(y, x));
            }
        }
    }
    
    // Minimum points check
    size_t minPts = (degree == 1) ? 3 : (degree == 2) ? 6 : (degree == 3) ? 10 : 1;
    if (pts.size() < minPts) {
        cv::Scalar m = cv::mean(src, mask);
        return cv::Mat(src.size(), src.type(), m[0]);
    }

    // ROBUSTNESS: Discard outliers (stars or residuals) using Z-Score on the collected samples
    cv::Mat vMat(vals.size(), 1, CV_32F, vals.data());
    cv::Scalar meanV, stdDevV;
    cv::meanStdDev(vMat, meanV, stdDevV);
    
    std::vector<cv::Point> cleanPts;
    std::vector<float> cleanVals;
    float threshold = 2.0f * (float)stdDevV[0]; // 2 sigma rejection
    for (size_t i = 0; i < vals.size(); ++i) {
        if (std::abs(vals[i] - (float)meanV[0]) < threshold) {
            cleanPts.push_back(pts[i]);
            cleanVals.push_back(vals[i]);
        }
    }
    
    if (cleanPts.size() < minPts) {
        cleanPts = pts; cleanVals = vals;
    }

    int cols = (degree == 1) ? 3 : (degree == 2) ? 6 : (degree == 3) ? 10 : 15;
    cv::Mat A(cleanPts.size(), cols, CV_32FC1);
    cv::Mat B(cleanPts.size(), 1, CV_32FC1);
    
    float nx = 2.0f / (smallSrc.cols - 1);
    float ny = 2.0f / (smallSrc.rows - 1);
    
    for (size_t i = 0; i < cleanPts.size(); ++i) {
        float x = cleanPts[i].x * nx - 1.0f;
        float y = cleanPts[i].y * ny - 1.0f;
        B.at<float>(i, 0) = cleanVals[i];
        
        float* row = A.ptr<float>(i);
        int c = 0;
        row[c++] = 1.0f;
        if (degree >= 1) { row[c++] = x; row[c++] = y; }
        if (degree >= 2) { row[c++] = x*x; row[c++] = y*y; row[c++] = x*y; }
        if (degree >= 3) { row[c++] = x*x*x; row[c++] = y*y*y; row[c++] = x*x*y; row[c++] = x*y*y; }
    }
    
    cv::Mat coeffs;
    cv::solve(A, B, coeffs, cv::DECOMP_SVD);
    
    cv::Mat result(src.rows, src.cols, CV_32FC1);
    float Fnx = 2.0f / (src.cols - 1);
    float Fny = 2.0f / (src.rows - 1);
    const float* cPtr = coeffs.ptr<float>(0);
    
    for (int y = 0; y < src.rows; ++y) {
        float py = y * Fny - 1.0f;
        float py2 = py * py;
        float py3 = py2 * py;
        float* row = result.ptr<float>(y);
        for (int x = 0; x < src.cols; ++x) {
            float px = x * Fnx - 1.0f;
            float px2 = px * px;
            float px3 = px2 * px;
            
            float val = cPtr[0];
            int c = 1;
            if (degree >= 1) { val += cPtr[c]*px + cPtr[c+1]*py; c += 2; }
            if (degree >= 2) { val += cPtr[c]*px2 + cPtr[c+1]*py2 + cPtr[c+2]*px*py; c += 3; }
            if (degree >= 3) { val += cPtr[c]*px3 + cPtr[c+1]*py3 + cPtr[c+2]*px2*py + cPtr[c+3]*px*py2; c += 4; }
            
            row[x] = val;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Split an interleaved ImageBuffer channel into a CV_32FC1 mat (no copy when
// nChannels == 1; extracts one plane when nChannels == 3).
// ---------------------------------------------------------------------------
static cv::Mat extractChannel(const cv::Mat& mat, int ch) {
    if (mat.channels() == 1) return mat;
    cv::Mat out;
    cv::extractChannel(mat, out, ch);
    return out;
}

bool CatalogGradientExtractor::extract(ImageBuffer& target,
                                       const ImageBuffer& reference,
                                       const Options& opts,
                                       std::function<void(int)> progress,
                                       std::atomic<bool>* cancelFlag)
{
    if (progress) progress(5);
    ImageBuffer gradMap = computeGradientMap(target, reference, opts, cancelFlag);
    if (!gradMap.isValid()) return false;
    if (cancelFlag && cancelFlag->load()) return false;
    if (progress) progress(90);

    if (opts.outputGradientMap) {
        target = gradMap;
        return true;
    }

    // Subtract the gradient map from the target.
    // We do NOT hard-clamp to [0,1] here: that would clip dark pixels and destroy
    // relative channel brightness. The caller / display pipeline handles the range.
    // We only clamp at 0 to avoid negative values (physically meaningless flux).
    auto& tgtData  = target.data();
    const auto& gd = gradMap.data();
    const size_t n = tgtData.size();
    for (size_t i = 0; i < n; ++i)
        tgtData[i] = tgtData[i] - gd[i];   // unclamped: preserves shadows & colour balance

    if (progress) progress(100);
    return true;
}

ImageBuffer CatalogGradientExtractor::computeGradientMap(const ImageBuffer& target,
                                                          const ImageBuffer& reference,
                                                          const Options& /*opts*/,
                                                          std::atomic<bool>* cancelFlag)
{
    ImageBuffer result;
    const int W    = target.width();
    const int H    = target.height();
    const int tCh  = target.channels();   // 1 or 3

    // -------------------------------------------------------------------------
    // Wrap target data as cv::Mat (no copy — data owned by ImageBuffer)
    // -------------------------------------------------------------------------
    const int cvTgtType = (tCh == 3) ? CV_32FC3 : CV_32FC1;
    cv::Mat tgtMat(H, W, cvTgtType, const_cast<float*>(target.data().data()));

    // -------------------------------------------------------------------------
    // Prepare Reference Mat
    // -------------------------------------------------------------------------
    cv::Mat refMat;
    if (reference.isValid()) {
        const int rcvType = (reference.channels() == 3) ? CV_32FC3 : CV_32FC1;
        cv::Mat rawRef(reference.height(), reference.width(), rcvType, const_cast<float*>(reference.data().data()));
        if (rawRef.cols != W || rawRef.rows != H) {
            cv::resize(rawRef, refMat, cv::Size(W, H), 0, 0, cv::INTER_CUBIC);
        } else {
            refMat = rawRef;
        }
    }

    // -------------------------------------------------------------------------
    // Per-channel gradient extraction
    // -------------------------------------------------------------------------
    std::vector<cv::Mat> gradChannels;
    gradChannels.reserve(tCh);

    for (int c = 0; c < tCh; ++c) {
        if (cancelFlag && cancelFlag->load()) return {};
        cv::Mat tgtChan = extractChannel(tgtMat, c);  // CV_32FC1

        cv::Mat gradient;

        if (!refMat.empty()) {
            int refC = (refMat.channels() == 1) ? 0 : c;
            cv::Mat refChan = extractChannel(refMat, refC);

            // 1. Create a logical mask of the "sky" using the Reference image.
            // We want to identify the dark background vs the bright nebulas/galaxies.
            // We use a small blur to remove catalog noise before thresholding.
            cv::Mat rBg;
            cv::GaussianBlur(refChan, rBg, cv::Size(0, 0), 4.0);

            // Determine reference sky level (median) and spread
            cv::Mat r1D;
            rBg.reshape(1, 1).copyTo(r1D);
            cv::sort(r1D, r1D, cv::SORT_EVERY_ROW + cv::SORT_ASCENDING);
            float rMedian = r1D.at<float>(0, r1D.cols / 2);
            // MAD (Median Absolute Deviation) for robust noise estimation
            cv::Mat rDev = cv::abs(r1D - rMedian);
            cv::sort(rDev, rDev, cv::SORT_EVERY_ROW + cv::SORT_ASCENDING);
            float rMAD = rDev.at<float>(0, rDev.cols / 2);
            float rSigma = rMAD * 1.4826f;

            // Mask: 1 (255) for SKY, 0 for NEBULA/STARS
            // We use a slightly stricter threshold to truly isolate the deep background.
            cv::Mat skyMask;
            cv::threshold(rBg, skyMask, rMedian + 1.0f * rSigma, 255.0, cv::THRESH_BINARY_INV);
            skyMask.convertTo(skyMask, CV_8U);

            // Morphological erode to grow the protection area around nebulas/stars.
            // Using a larger kernel (15x15) to ensure we don't sample faint halo edges.
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(15, 15));
            cv::erode(skyMask, skyMask, kernel);

            // 2. Extract target background using true polynomial interpolation.
            // Using Degree 2 (Quadratic) by default as it is much more stable than Degree 3
            // and captures standard sky gradients (vignetting + linear LP) perfectly.
            gradient = fitPolynomialBackground(tgtChan, skyMask, 2);
            
            // 3. NORMALIZATION: Professional Sky-Median Alignment
            // We shift the gradient so its own value AT THE SKY PIXELS is zero.
            // This ensures that when target - gradient is performed, the background
            // stays at its original median level (maintaining 0.25 autostretch gray).
            cv::Scalar gSkyMean = cv::mean(gradient, skyMask);
            gradient = gradient - gSkyMean[0];
            
        } else {
            // Fallback: No reference, just quadratic blur/fit on the target
            gradient = fitPolynomialBackground(tgtChan, cv::Mat::ones(tgtChan.size(), CV_8U) * 255, 2);
            cv::Scalar gMedian = cv::mean(gradient);
            gradient = gradient - gMedian[0];
        }

        // NO CLAMPING to 0. We preserve the natural noise floor.
        gradChannels.push_back(gradient);
    }

    // -------------------------------------------------------------------------
    // Merge back into an interleaved float mat and copy to result ImageBuffer
    // -------------------------------------------------------------------------
    cv::Mat merged;
    if (tCh == 3) {
        cv::merge(gradChannels, merged);
    } else {
        merged = gradChannels[0];
    }

    result.resize(W, H, tCh);
    std::memcpy(result.data().data(), merged.ptr<float>(),
                static_cast<size_t>(W) * H * tCh * sizeof(float));
    return result;
}

} // namespace Background
