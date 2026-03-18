/*
 * Deconvolution.cpp  —  Richardson-Lucy, RLTV, and Wiener deconvolution
 */

#include "Deconvolution.h"
#include "photometry/StarDetector.h"
#include "photometry/PsfFitter.h"
#include "core/Logger.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <cassert>
#include <cstring>
#include <omp.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

// ─── PSF building ─────────────────────────────────────────────────────────────

cv::Mat Deconvolution::buildPSF(const DeconvParams& p, int ksize)
{
    if (p.psfSource == PSFSource::Custom && !p.customKernel.empty()) {
        cv::Mat k;
        p.customKernel.convertTo(k, CV_32F);
        double s = cv::sum(k)[0];
        if (s > 1e-12) k /= s;
        return k;
    }

    if (ksize <= 0)
        ksize = static_cast<int>(std::ceil(p.psfFWHM * 4.0));
    if (ksize % 2 == 0) ksize++;
    ksize = std::max(ksize, 3);

    cv::Mat psf(ksize, ksize, CV_32F, cv::Scalar(0.0f));
    const double cx = (ksize - 1) * 0.5;
    const double cy = (ksize - 1) * 0.5;
    const double ang = p.psfAngle * M_PI / 180.0;
    const double ca  = std::cos(ang), sa = std::sin(ang);
    const double rnd = std::max(0.01, std::min(1.0, p.psfRoundness));

    if (p.psfSource == PSFSource::Gaussian) {
        // σ from FWHM: σ² = FWHM² / (8 ln2)
        const double sig2x = (p.psfFWHM * p.psfFWHM) / (8.0 * M_LN2);
        const double sig2y = sig2x * rnd * rnd;
        for (int y = 0; y < ksize; y++) {
            for (int x = 0; x < ksize; x++) {
                double dx = ca*(x - cx) - sa*(y - cy);
                double dy = sa*(x - cx) + ca*(y - cy);
                psf.at<float>(y, x) = static_cast<float>(
                    std::exp(-(dx*dx / (2.0*sig2x) + dy*dy / (2.0*sig2y))));
            }
        }
    } else if (p.psfSource == PSFSource::Moffat) {
        // Moffat: PSF = (1 + r²/α²)^(-β)
        // FWHM = 2α√(2^(1/β)−1)  →  α = FWHM / (2√(2^(1/β)−1))
        const double beta = p.psfBeta;
        const double alpha2 = (p.psfFWHM * p.psfFWHM) /
                              (4.0 * (std::pow(2.0, 1.0/beta) - 1.0));
        const double alpha2y = alpha2 * rnd * rnd;
        for (int y = 0; y < ksize; y++) {
            for (int x = 0; x < ksize; x++) {
                double dx = ca*(x - cx) - sa*(y - cy);
                double dy = sa*(x - cx) + ca*(y - cy);
                double r2 = dx*dx / alpha2 + dy*dy / alpha2y;
                psf.at<float>(y, x) = static_cast<float>(std::pow(1.0 + r2, -beta));
            }
        }
    } else if (p.psfSource == PSFSource::Disk) {
        const double radius = p.psfFWHM / 2.0;
        const double solidradsq = std::pow(radius - 0.5, 2.0);
        const double zeroradsq  = std::pow(radius + 0.5, 2.0);
        for (int y = 0; y < ksize; y++) {
            for (int x = 0; x < ksize; x++) {
                double dx = x - cx, dy = y - cy;
                double r2 = dx*dx + dy*dy;
                if (r2 < solidradsq) {
                    psf.at<float>(y, x) = 1.0f;
                } else if (r2 > zeroradsq) {
                    psf.at<float>(y, x) = 0.0f;
                } else {
                    psf.at<float>(y, x) = static_cast<float>(0.5 + radius - std::sqrt(r2));
                }
            }
        }
    } else if (p.psfSource == PSFSource::Airy) {
        double wl = p.airyWavelength * 1e-9;
        double D  = p.airyAperture * 1e-3;
        double f  = p.airyFocalLen * 1e-3;
        double px = p.airyPixelSize * 1e-6;
        double obs= std::max(0.0, std::min(0.99, p.airyObstruction));
        double obscorr = (obs > 0.0) ? 1.0 / std::pow(1.0 - obs*obs, 2.0) : 1.0;
        double constant = (2.0 * M_PI * (D / 2.0) / wl) * (1.0 / f);
        for (int y = 0; y < ksize; y++) {
            for (int x = 0; x < ksize; x++) {
                double dx = (x - cx) * px;
                double dy = (y - cy) * px;
                double q = constant * std::sqrt(dx*dx + dy*dy);
                if (q == 0.0) {
                    psf.at<float>(y, x) = 1.0f;
                } else {
                    double bessel = std::cyl_bessel_j(1, q);
                    if (obs == 0.0) {
                        psf.at<float>(y, x) = static_cast<float>(std::pow(2.0 * bessel / q, 2.0));
                    } else {
                        psf.at<float>(y, x) = static_cast<float>(obscorr * std::pow(2.0 / q * (bessel - obs * std::cyl_bessel_j(1, obs * q)), 2.0));
                    }
                }
            }
        }
    }

    // Normalise so kernel sums to 1
    double s = cv::sum(psf)[0];
    if (s > 1e-12) psf /= static_cast<float>(s);
    return psf;
}

// ─── FFT convolution ──────────────────────────────────────────────────────────

cv::Mat Deconvolution::convolveFFT(const cv::Mat& src, const cv::Mat& psf)
{
    // Pad to optimal DFT size
    int dft_rows = cv::getOptimalDFTSize(src.rows + psf.rows - 1);
    int dft_cols = cv::getOptimalDFTSize(src.cols + psf.cols - 1);

    cv::Mat src_pad, psf_pad;
    cv::copyMakeBorder(src, src_pad, 0, dft_rows - src.rows,
                       0, dft_cols - src.cols, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::copyMakeBorder(psf, psf_pad, 0, dft_rows - psf.rows,
                       0, dft_cols - psf.cols, cv::BORDER_CONSTANT, cv::Scalar::all(0));

    cv::Mat Fsrc, Fpsf;
    cv::dft(src_pad, Fsrc, cv::DFT_COMPLEX_OUTPUT);
    cv::dft(psf_pad, Fpsf, cv::DFT_COMPLEX_OUTPUT);

    cv::Mat Fprod;
    cv::mulSpectrums(Fsrc, Fpsf, Fprod, 0);

    cv::Mat result;
    cv::idft(Fprod, result, cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);

    // Shift and crop to original size
    int shift_r = psf.rows / 2, shift_c = psf.cols / 2;
    cv::Mat shifted = result(cv::Rect(shift_c, shift_r, src.cols, src.rows)).clone();
    return shifted;
}

// ─── Wiener filter (frequency domain) ────────────────────────────────────────

cv::Mat Deconvolution::wienerFilter(const cv::Mat& src, const cv::Mat& psf, double K)
{
    int dft_rows = cv::getOptimalDFTSize(src.rows + psf.rows - 1);
    int dft_cols = cv::getOptimalDFTSize(src.cols + psf.cols - 1);

    cv::Mat src_pad, psf_pad;
    cv::copyMakeBorder(src, src_pad, 0, dft_rows - src.rows,
                       0, dft_cols - src.cols, cv::BORDER_REFLECT);
    cv::copyMakeBorder(psf, psf_pad, 0, dft_rows - psf.rows,
                       0, dft_cols - psf.cols, cv::BORDER_CONSTANT, cv::Scalar::all(0));

    cv::Mat Fg, Fh;
    cv::dft(src_pad, Fg, cv::DFT_COMPLEX_OUTPUT);
    cv::dft(psf_pad, Fh, cv::DFT_COMPLEX_OUTPUT);

    // W = conj(H) / (|H|² + K)
    cv::Mat Fw(Fh.size(), Fh.type());
    for (int y = 0; y < Fh.rows; y++) {
        const float* ph = Fh.ptr<float>(y);
        const float* pg = Fg.ptr<float>(y);
        float*       pw = Fw.ptr<float>(y);
        for (int c = 0; c < Fh.cols; ++c) {
            int x = c * 2;
            float hr = ph[x], hi = ph[x+1];
            float gr = pg[x], gi = pg[x+1];
            float denom = hr*hr + hi*hi + static_cast<float>(K);
            // W·G: (hr·gr + hi·gi)/denom,  (hr·gi - hi·gr)/denom
            pw[x]   = (hr*gr + hi*gi) / denom;
            pw[x+1] = (hr*gi - hi*gr) / denom;
        }
    }

    cv::Mat result;
    cv::idft(Fw, result, cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);

    int shift_r = psf.rows / 2, shift_c = psf.cols / 2;
    return result(cv::Rect(shift_c, shift_r, src.cols, src.rows)).clone();
}

// ─── TV gradient ─────────────────────────────────────────────────────────────

cv::Mat Deconvolution::tvGradient(const cv::Mat& u, double eps)
{
    const int rows = u.rows, cols = u.cols;
    cv::Mat grad(rows, cols, CV_32F, cv::Scalar(0.f));

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < rows; y++) {
        const float* up  = (y > 0)      ? u.ptr<float>(y-1) : u.ptr<float>(y);
        const float* uc  = u.ptr<float>(y);
        const float* un  = (y < rows-1) ? u.ptr<float>(y+1) : u.ptr<float>(y);
        float*       gp  = grad.ptr<float>(y);

        for (int x = 0; x < cols; x++) {
            int xp = std::max(0, x-1), xn = std::min(cols-1, x+1);
            double dx  = uc[xn] - uc[xp];
            double dy  = un[x]  - up[x];
            double dxf = uc[xn] - uc[x];
            double dyf = un[x]  - uc[x];
            double norm = std::sqrt(dxf*dxf + dyf*dyf + eps*eps);
            // div(∇u / |∇u|_ε)  ≈  laplacian with Huber
            double div_x = (x < cols-1 ? (uc[xn] - uc[x]) / norm : 0.0) -
                           (x > 0      ? (uc[x] - uc[xp])  / norm : 0.0);
            double div_y = (y < rows-1 ? (un[x] - uc[x])   / norm : 0.0) -
                           (y > 0      ? (uc[x] - up[x])    / norm : 0.0);
            (void)dx; (void)dy; // suppress unused warning
            gp[x] = static_cast<float>(div_x + div_y);
        }
    }
    return grad;
}

// ─── RGB ↔ YCbCr conversion (ITU-R BT.601) ──────────────────────────────────

void Deconvolution::rgbToYcbcr(const std::vector<float>& rgb, std::vector<float>& ycbcr,
                               int w, int h)
{
    ycbcr.resize(rgb.size());
    const size_t npixels = static_cast<size_t>(w) * h;
    
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(npixels); ++i) {
        const size_t base = static_cast<size_t>(i) * 3;
        float r = rgb[base + 0];
        float g = rgb[base + 1];
        float b = rgb[base + 2];
        
        // ITU-R BT.601 full-range (interleaved RGB input/output)
        float y  = 0.299f * r + 0.587f * g + 0.114f * b;
        float cb = 0.5f + (-0.168736f * r - 0.331264f * g + 0.5f * b);
        float cr = 0.5f + (0.5f * r - 0.418688f * g - 0.081312f * b);
        
        ycbcr[base + 0] = std::max(0.0f, std::min(1.0f, y));
        ycbcr[base + 1] = std::max(0.0f, std::min(1.0f, cb));
        ycbcr[base + 2] = std::max(0.0f, std::min(1.0f, cr));
    }
}

void Deconvolution::ycbcrToRgb(const std::vector<float>& ycbcr, std::vector<float>& rgb,
                               int w, int h)
{
    rgb.resize(ycbcr.size());
    const size_t npixels = static_cast<size_t>(w) * h;
    
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(npixels); ++i) {
        const size_t base = static_cast<size_t>(i) * 3;
        float y  = ycbcr[base + 0];
        float cb = ycbcr[base + 1] - 0.5f;
        float cr = ycbcr[base + 2] - 0.5f;
        
        // Inverse BT.601
        float r = y + 1.402f * cr;
        float g = y - 0.34414f * cb - 0.71414f * cr;
        float b = y + 1.772f * cb;
        
        // Clamp to [0, 1]
        r = std::max(0.0f, std::min(1.0f, r));
        g = std::max(0.0f, std::min(1.0f, g));
        b = std::max(0.0f, std::min(1.0f, b));
        
        rgb[base + 0] = r;
        rgb[base + 1] = g;
        rgb[base + 2] = b;
    }
}

// ─── RL (no regularisation) ──────────────────────────────────────────────────

DeconvResult Deconvolution::applyRL(cv::Mat& plane, const cv::Mat& psf,
                                     const cv::Mat& starMask, const DeconvParams& p)
{
    DeconvResult res;
    cv::Mat u = plane.clone();
    cv::Mat u_prev = u.clone();
    cv::Mat psf_flip;
    cv::flip(psf, psf_flip, -1); // PSF mirror for correlation step

    for (int it = 0; it < p.maxIter; it++) {
        // u_new = u · (psf* ⊛ (d / (psf ⊛ u)))
        cv::Mat conv_u = convolveFFT(u, psf);
        cv::threshold(conv_u, conv_u, 1e-6f, 0.0f, cv::THRESH_TOZERO);
        cv::Mat ratio;
        cv::divide(plane, conv_u + 1e-6f, ratio);
        cv::Mat corr = convolveFFT(ratio, psf_flip);
        u.copyTo(u_prev);
        cv::multiply(u, corr, u);
        cv::threshold(u, u, 0.0f, 0.0f, cv::THRESH_TOZERO);

        // Convergence
        if ((it % 10) == 9) {
            cv::Mat diff; cv::absdiff(u, u_prev, diff);
            double change = cv::mean(diff)[0] / (cv::mean(u)[0] + 1e-12);
            if (change < p.convergenceTol) {
                res.iterations  = it + 1;
                res.finalChange = change;
                break;
            }
        }
        res.iterations = it + 1;

        if (p.progressCallback && ((it % 5) == 0 || it + 1 == p.maxIter)) {
            const int pct = std::max(0, std::min(100, ((it + 1) * 100) / std::max(1, p.maxIter)));
            p.progressCallback(pct, QString("Deconvolution RL %1/%2").arg(it + 1).arg(p.maxIter));
        }
    }

    // Star mask blending
    if (!starMask.empty() && p.starMask.useMask) {
        float bf = static_cast<float>(p.starMask.blend);
        cv::Mat blend;
        cv::addWeighted(plane, 1.0f - bf, u, bf, 0.0f, blend);
        blend.copyTo(u);
        plane.copyTo(u, starMask);
        u.copyTo(plane);
    } else {
        u.copyTo(plane);
    }

    res.success = true;
    return res;
}

// ─── RLTV ────────────────────────────────────────────────────────────────────

DeconvResult Deconvolution::applyRLTV(cv::Mat& plane, const cv::Mat& psf,
                                       const cv::Mat& starMask, const DeconvParams& p)
{
    DeconvResult res;
    cv::Mat u = plane.clone();
    cv::Mat u_prev = u.clone();
    cv::Mat psf_flip; cv::flip(psf, psf_flip, -1);
    const float tv = static_cast<float>(p.tvRegWeight);

    for (int it = 0; it < p.maxIter; it++) {
        cv::Mat conv_u  = convolveFFT(u, psf);
        cv::threshold(conv_u, conv_u, 1e-6f, 0.0f, cv::THRESH_TOZERO);
        cv::Mat ratio;
        cv::divide(plane, conv_u + 1e-6f, ratio);
        cv::Mat corr = convolveFFT(ratio, psf_flip);

        // TV sub-gradient regulariser:  u_new = u · corr / (1 - λ_TV · div_TV) (Matching Siril!)
        cv::Mat tv_grad = tvGradient(u, p.tvEps);
        cv::Mat denom = cv::Mat::ones(u.size(), CV_32F) - tv_grad * tv; // Use minus sign like Siril
        cv::max(denom, 1e-6f, denom);
        u.copyTo(u_prev);
        cv::multiply(u, corr, u);
        cv::divide(u, denom, u);
        
        cv::threshold(u, u, 0.0f, 0.0f, cv::THRESH_TOZERO);

        if ((it % 10) == 9) {
            cv::Mat diff; cv::absdiff(u, u_prev, diff);
            double change = cv::mean(diff)[0] / (cv::mean(u)[0] + 1e-12);
            if (change < p.convergenceTol) {
                res.iterations  = it + 1;
                res.finalChange = change;
                break;
            }
        }
        res.iterations = it + 1;

        if (p.progressCallback && ((it % 5) == 0 || it + 1 == p.maxIter)) {
            const int pct = std::max(0, std::min(100, ((it + 1) * 100) / std::max(1, p.maxIter)));
            p.progressCallback(pct, QString("Deconvolution RLTV %1/%2").arg(it + 1).arg(p.maxIter));
        }
    }

    if (!starMask.empty() && p.starMask.useMask)
        plane.copyTo(u, starMask);

    u.copyTo(plane);
    res.success = true;
    return res;
}

// ─── Wiener ──────────────────────────────────────────────────────────────────

DeconvResult Deconvolution::applyWiener(cv::Mat& plane, const cv::Mat& psf,
                                         const DeconvParams& p)
{
    DeconvResult res;
    cv::Mat result = wienerFilter(plane, psf, p.wienerK);
    cv::threshold(result, result, 0.0f, 0.0f, cv::THRESH_TOZERO);
    cv::threshold(result, result, 1.0f, 1.0f, cv::THRESH_TRUNC);
    result.copyTo(plane);
    res.success    = true;
    res.iterations = 1;
    if (p.progressCallback) {
        p.progressCallback(100, "Deconvolution Wiener completed");
    }
    return res;
}

// ─── Star mask ────────────────────────────────────────────────────────────────

cv::Mat Deconvolution::buildStarMask(const cv::Mat& plane, const DeconvStarMask& m)
{
    cv::Mat mask;
    cv::threshold(plane, mask, m.threshold, 1.0f, cv::THRESH_BINARY);
    mask.convertTo(mask, CV_8U, 255.0);
    int ksize = std::max(3, static_cast<int>(m.radius * 2 + 1));
    if (ksize % 2 == 0) ksize++;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
                                                cv::Size(ksize, ksize));
    cv::dilate(mask, mask, kernel);
    return mask;
}

// ─── Auto FWHM estimation ─────────────────────────────────────────────────────

double Deconvolution::estimateFWHM(const ImageBuffer& buf)
{
    StarDetector det;
    auto stars = det.detect(buf, 0);
    if (stars.empty()) return 2.0;
    const std::vector<float>& data = buf.data();
    const int channels = buf.channels();

    // Use PsfFitter on the best 5 stars
    std::sort(stars.begin(), stars.end(), [](const auto& a, const auto& b) {
        return a.flux > b.flux;
    });

    double sum_fwhm = 0.0; int cnt = 0;
    for (int i = 0; i < std::min((int)stars.size(), 5); i++) {
        const auto& s = stars[i];
        int hw = static_cast<int>(s.fwhm * 4 + 2);
        int x0 = static_cast<int>(s.x) - hw;
        int y_start = static_cast<int>(s.y) - hw;
        int x1 = x0 + 2 * hw + 1;
        int y_end = y_start + 2 * hw + 1;
        x0 = std::max(0, x0);
        y_start = std::max(0, y_start);
        x1 = std::min(buf.width()-1,  x1);
        y_end = std::min(buf.height()-1, y_end);
        if (x1 <= x0 || y_end <= y_start) continue;
        int ww = x1 - x0;
        int hh = y_end - y_start;
        std::vector<double> patch(ww * hh);
        for (int y = y_start; y < y_end; y++)
            for (int x = x0; x < x1; x++)
                patch[(y - y_start) * ww + (x - x0)] =
                    data[(static_cast<size_t>(y) * buf.width() + x) * channels];

        PsfError err;
        PsfStar* psf = PsfFitter::fitMatrix(patch, hh, ww, 0.0, 1.0,
                                            1, false, PsfProfile::Moffat, &err);
        if (psf) {
            sum_fwhm += (psf->fwhmx + psf->fwhmy) * 0.5;
            cnt++;
            delete psf;
        }
    }
    return (cnt > 0) ? sum_fwhm / cnt : 2.0;
}

// ─── Main entry ──────────────────────────────────────────────────────────────

DeconvResult Deconvolution::apply(ImageBuffer& buf, const DeconvParams& p)
{
    DeconvResult res;
    if (!buf.isValid()) {
        res.errorMsg = "Invalid image buffer.";
        return res;
    }

    // Build PSF kernel
    int ksize = p.kernelSize;
    if (ksize <= 0) ksize = static_cast<int>(std::ceil(p.psfFWHM * 4.0));
    if (ksize % 2 == 0) ksize++;
    ksize = std::max(ksize, 3);

    cv::Mat psf = buildPSF(p, ksize);

    const int w  = buf.width();
    const int h  = buf.height();
    const int ch = buf.channels();
    std::vector<float>& data = buf.data();

    // Deconvolve all channels independently (like Siril), no more YCbCr luminance-only extraction!
    // Luminance-only deconvolution on RGB leads to massive color fringing and chromatic ringing.
    std::vector<float> workData = data;

    DeconvResult last;
    
    // Process all 'ch' channels
    const int numChannelsToDeconv = ch;
    const int dataStride = ch;
    
    for (int c = 0; c < numChannelsToDeconv; c++) {
        std::vector<float> planeData(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = (static_cast<size_t>(y) * w + x) * dataStride + c;
                planeData[static_cast<size_t>(y) * w + x] = workData[idx];
            }
        }

        cv::Mat plane(h, w, CV_32F, planeData.data());

        // Build star protection mask for this channel
        cv::Mat starMask;
        if (p.starMask.useMask && p.algo != DeconvAlgorithm::Wiener)
            starMask = buildStarMask(plane, p.starMask);

        // Mirror-pad to reduce wrap artefacts
        cv::Mat padded;
        cv::copyMakeBorder(plane, padded, p.borderPad, p.borderPad,
                           p.borderPad, p.borderPad, cv::BORDER_REFLECT);

        cv::Mat padMask;
        if (!starMask.empty())
            cv::copyMakeBorder(starMask, padMask, p.borderPad, p.borderPad,
                               p.borderPad, p.borderPad, cv::BORDER_CONSTANT, cv::Scalar(0));

        DeconvParams channelParams = p;
        if (p.progressCallback) {
            channelParams.progressCallback = [p, c, numChannelsToDeconv](int channelPct, const QString& msg) {
                const int totalPct = ((c * 100) + std::max(0, std::min(100, channelPct))) / std::max(1, numChannelsToDeconv);
                p.progressCallback(totalPct, msg);
            };
        }

        DeconvResult r;
        switch (p.algo) {
            case DeconvAlgorithm::RichardsonLucy:
                r = applyRL(padded, psf, padMask, channelParams);   break;
            case DeconvAlgorithm::RLTV:
                r = applyRLTV(padded, psf, padMask, channelParams); break;
            case DeconvAlgorithm::Wiener:
                r = applyWiener(padded, psf, channelParams);         break;
        }
        if (!r.success) { res.errorMsg = r.errorMsg; return res; }

        // Crop back and write to buffer
        cv::Mat cropped = padded(cv::Rect(p.borderPad, p.borderPad, w, h));
        for (int y = 0; y < h; ++y) {
            const float* row = cropped.ptr<float>(y);
            for (int x = 0; x < w; ++x) {
                const size_t idx = (static_cast<size_t>(y) * w + x) * dataStride + c;
                workData[idx] = row[x];
            }
        }

        last = r;
    }

    // Overwrite the original data with the fully deconvolved channels
    data = workData;

    res.success     = true;
    res.iterations  = last.iterations;
    res.finalChange = last.finalChange;
    buf.setModified(true);
    if (p.progressCallback) {
        p.progressCallback(100, "Deconvolution completed");
    }
    return res;
}
