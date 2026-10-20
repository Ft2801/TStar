/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

// =============================================================================
// WavescaleHDRDialog.cpp
// Implements the wavelet-based HDR compression dialog. The processing pipeline:
//   1. Convert RGB to CIE Lab colour space.
//   2. Decompose the L channel via a trous wavelets.
//   3. Build a brightness mask from the original L channel.
//   4. Modulate each wavelet plane by the mask and a per-scale decay factor.
//   5. Reconstruct L and align midtones to the original.
//   6. Convert back to RGB with an optional dimming gamma curve.
// =============================================================================

#include "WavescaleHDRDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QMessageBox>
#include <QGroupBox>
#include <QTimer>
#include <QShowEvent>

#include <cmath>
#include <algorithm>

// =============================================================================
// CIE Lab colour space conversion helpers (file-local)
// =============================================================================

namespace {

struct LabPixel { float L, a, b; };
struct RGBPixel { float r, g, b; };

// D65 illuminant reference white point
constexpr float D65_Xn = 0.95047f;
constexpr float D65_Yn = 1.00000f;
constexpr float D65_Zn = 1.08883f;

/// XYZ-to-Lab forward pivot function.
float pivotXyz(float n)
{
    return (n > 0.008856f) ? std::cbrt(n) : (7.787f * n + 16.0f / 116.0f);
}

/// Converts sRGB [0,1] to CIE Lab.
LabPixel rgb2lab(float r, float g, float b)
{
    // Linearize sRGB
    auto lin = [](float c) -> float {
        return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    float R = lin(r);
    float G = lin(g);
    float B = lin(b);

    // sRGB -> XYZ (D65)
    float X = 0.4124564f * R + 0.3575761f * G + 0.1804375f * B;
    float Y = 0.2126729f * R + 0.7151522f * G + 0.0721750f * B;
    float Z = 0.0193339f * R + 0.1191920f * G + 0.9503041f * B;

    // XYZ -> Lab
    float fx = pivotXyz(X / D65_Xn);
    float fy = pivotXyz(Y / D65_Yn);
    float fz = pivotXyz(Z / D65_Zn);

    float Lv = 116.0f * fy - 16.0f;
    float av = 500.0f * (fx - fy);
    float bv = 200.0f * (fy - fz);

    return { std::max(0.0f, Lv), av, bv };
}

/// Lab-to-XYZ inverse pivot function.
float pivotInv(float n)
{
    float n3 = n * n * n;
    return (n3 > 0.008856f) ? n3 : ((n - 16.0f / 116.0f) / 7.787f);
}

/// Converts CIE Lab to sRGB [0,1] (clamped).
RGBPixel lab2rgb(float L, float a, float b)
{
    float fy = (L + 16.0f) / 116.0f;
    float fx = a / 500.0f + fy;
    float fz = fy - b / 200.0f;

    float X = D65_Xn * pivotInv(fx);
    float Y = D65_Yn * pivotInv(fy);
    float Z = D65_Zn * pivotInv(fz);

    // XYZ -> linear RGB
    float R =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    float B =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

    // Linear -> sRGB gamma
    auto gam = [](float c) -> float {
        return (c <= 0.0031308f) ? (12.92f * c)
                                 : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    };

    return { std::max(0.0f, std::min(1.0f, gam(R))),
             std::max(0.0f, std::min(1.0f, gam(G))),
             std::max(0.0f, std::min(1.0f, gam(B))) };
}

/// Estimates the median of a float vector using subsampled nth_element.
float estimateMedian(std::vector<float>& d)
{
    if (d.empty()) return 0.0f;
    std::vector<float> sub;
    int step = static_cast<int>(d.size()) / 10000 + 1;
    for (size_t i = 0; i < d.size(); i += step) {
        sub.push_back(d[i]);
    }
    size_t n = sub.size() / 2;
    std::nth_element(sub.begin(), sub.begin() + static_cast<long>(n), sub.end());
    return sub[n];
}

} // anonymous namespace

// =============================================================================
// WavescaleHDRWorker
// =============================================================================

WavescaleHDRWorker::WavescaleHDRWorker(QObject* parent)
    : QThread(parent)
{
}

void WavescaleHDRWorker::setup(const ImageBuffer& src,
                               int   scales,
                               float compression,
                               float maskGamma,
                               float dimmingGamma)
{
    m_src           = src;
    m_scales        = scales;
    m_compression   = compression;
    m_maskGamma     = maskGamma;
    m_dimmingGamma  = dimmingGamma;
}

// -----------------------------------------------------------------------------
// run  --  Background processing pipeline:
//   1. RGB -> Lab conversion
//   2. A trous wavelet decomposition of L
//   3. Brightness-mask-weighted modulation of wavelet planes
//   4. Wavelet reconstruction with midtone alignment
//   5. Lab -> RGB conversion with dimming gamma
// -----------------------------------------------------------------------------
void WavescaleHDRWorker::run()
{
    if (!m_src.isValid()) return;

    emit progress(5);

    int w  = m_src.width();
    int h  = m_src.height();
    int ch = m_src.channels();
    const std::vector<float>& srcData = m_src.data();

    // --- Step 1: Convert to CIE Lab ---
    std::vector<float> L(w * h);
    std::vector<float> A(w * h);
    std::vector<float> B(w * h);

    for (int i = 0; i < w * h; ++i) {
        float r = srcData[i * ch + 0];
        float g = (ch > 1) ? srcData[i * ch + 1] : r;
        float b = (ch > 2) ? srcData[i * ch + 2] : r;

        LabPixel lab = rgb2lab(r, g, b);
        L[i] = lab.L;
        A[i] = lab.a;
        B[i] = lab.b;
    }

    emit progress(20);

    // --- Step 2: A trous wavelet decomposition of L channel ---
    std::vector<std::vector<float>> planes =
        ImageBuffer::atrousDecompose(L, w, h, m_scales);

    emit progress(50);

    // --- Step 3: Build brightness mask and modulate wavelet planes ---

    // Construct the mask from the original luminance
    std::vector<float> mask(w * h);
    for (int i = 0; i < w * h; ++i) {
        float val = std::clamp(L[i] / 100.0f, 0.0f, 1.0f);
        if (m_maskGamma != 1.0f) {
            val = std::pow(val, m_maskGamma);
        }
        mask[i] = val;
    }

    // Modulate each wavelet plane (excluding the residual) by the mask,
    // with exponentially decaying influence at coarser scales
    for (int s = 0; s < static_cast<int>(planes.size()) - 1; ++s) {
        float decay = std::pow(0.5f, s);
        for (int i = 0; i < w * h; ++i) {
            float scaleFactor =
                (1.0f + (m_compression - 1.0f) * mask[i] * decay) * 2.0f;
            planes[s][i] *= scaleFactor;
        }
    }

    // --- Step 4: Reconstruct L and align midtones ---
    std::vector<float> Lr = ImageBuffer::atrousReconstruct(planes, w, h);

    // Align the reconstructed median to the original to preserve midtone
    // brightness
    float med0  = estimateMedian(L);
    float med1  = estimateMedian(Lr);
    if (med1 < 1e-5f) med1 = 1.0f;
    float ratio = med0 / med1;

    for (float& v : Lr) {
        v = std::clamp(v * ratio, 0.0f, 100.0f);
    }

    emit progress(80);

    // --- Step 5: Convert back to RGB with dimming gamma ---
    float dimGamma = m_dimmingGamma;

    ImageBuffer outBuf;
    outBuf.resize(w, h, 3);
    std::vector<float>& outData = outBuf.data();

    for (int i = 0; i < w * h; ++i) {
        RGBPixel rgb = lab2rgb(Lr[i], A[i], B[i]);
        outData[i * 3 + 0] = std::pow(std::clamp(rgb.r, 0.0f, 1.0f), dimGamma);
        outData[i * 3 + 1] = std::pow(std::clamp(rgb.g, 0.0f, 1.0f), dimGamma);
        outData[i * 3 + 2] = std::pow(std::clamp(rgb.b, 0.0f, 1.0f), dimGamma);
    }

    // Build a grayscale mask buffer for the mask preview widget
    ImageBuffer maskBuf;
    maskBuf.resize(w, h, 3);
    std::vector<float>& mData = maskBuf.data();
    for (int i = 0; i < w * h; ++i) {
        float v = mask[i];
        mData[i * 3 + 0] = v;
        mData[i * 3 + 1] = v;
        mData[i * 3 + 2] = v;
    }

    emit progress(100);
    emit finished(outBuf, maskBuf);
}

// =============================================================================
// WavescaleHDRDialog
// =============================================================================

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
WavescaleHDRDialog::WavescaleHDRDialog(QWidget* parent, ImageViewer* targetViewer)
    : DialogBase(parent, tr("Wavescale HDR"), 1100, 550)
    , m_targetViewer(targetViewer)
{
    // Snapshot the source image for preview and undo
    if (m_targetViewer && m_targetViewer->getBuffer().isValid()) {
        m_originalBuffer = m_targetViewer->getBuffer();
    }
    m_previewBuffer = m_originalBuffer;

    createUI();

    // Worker thread setup
    m_worker = new WavescaleHDRWorker(this);
    connect(m_worker, &WavescaleHDRWorker::progress,
            m_progressBar, &QProgressBar::setValue);
    connect(m_worker, &WavescaleHDRWorker::finished,
            this, &WavescaleHDRDialog::onWorkerFinished);

    // Build a downscaled luminance cache (max 400px on the longest side)
    // for fast interactive mask preview
    if (m_originalBuffer.isValid()) {
        int w = m_originalBuffer.width();
        int h = m_originalBuffer.height();

        float scale = std::min(1.0f, 400.0f / static_cast<float>(std::max(w, h)));
        m_cacheW = std::max(1, static_cast<int>(w * scale));
        m_cacheH = std::max(1, static_cast<int>(h * scale));

        m_L_channel_cache.resize(m_cacheW * m_cacheH);
        int          ch = m_originalBuffer.channels();
        const float* d  = m_originalBuffer.data().data();

#ifdef _OPENMP
        #pragma omp parallel for schedule(static)
#endif
        for (int y = 0; y < m_cacheH; ++y) {
            for (int x = 0; x < m_cacheW; ++x) {
                int sx = std::min(static_cast<int>(x / scale), w - 1);
                int sy = std::min(static_cast<int>(y / scale), h - 1);

                size_t idx = (static_cast<size_t>(sy) * w + sx) * ch;
                float r = d[idx + 0];
                float g = (ch > 1) ? d[idx + 1] : r;
                float b = (ch > 2) ? d[idx + 2] : r;

                // Fast Rec.709 luminance approximation (avoids expensive Lab conversion)
                m_L_channel_cache[y * m_cacheW + x] =
                    (0.2126f * r + 0.7152f * g + 0.0722f * b) * 100.0f;
            }
        }
    }

    // Set the initial buffer so the preview viewer is not empty
    m_viewer->setBuffer(m_originalBuffer, tr("Original"), false);
    m_viewer->setModified(false);

    // Defer initial mask and preview updates until after the dialog is fully
    // constructed and the show animation completes
    QTimer::singleShot(300, this, &WavescaleHDRDialog::updateQuickMask);
    QTimer::singleShot(300, this, &WavescaleHDRDialog::startPreview);
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
WavescaleHDRDialog::~WavescaleHDRDialog()
{
    if (m_worker->isRunning()) {
        m_worker->terminate();
        m_worker->wait();
    }
}

// -----------------------------------------------------------------------------
// setViewer  --  Switches the source image and regenerates the luminance cache.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::setViewer(ImageViewer* v)
{
    if (m_targetViewer == v) return;
    m_targetViewer = v;

    if (m_targetViewer && m_targetViewer->getBuffer().isValid()) {
        m_originalBuffer = m_targetViewer->getBuffer();

        // Regenerate the downscaled luminance cache
        int   w     = m_originalBuffer.width();
        int   h     = m_originalBuffer.height();
        float scale = std::min(1.0f, 400.0f / static_cast<float>(std::max(w, h)));
        m_cacheW    = std::max(1, static_cast<int>(w * scale));
        m_cacheH    = std::max(1, static_cast<int>(h * scale));

        m_L_channel_cache.resize(m_cacheW * m_cacheH);
        int          ch = m_originalBuffer.channels();
        const float* d  = m_originalBuffer.data().data();

#ifdef _OPENMP
        #pragma omp parallel for schedule(static)
#endif
        for (int y = 0; y < m_cacheH; ++y) {
            for (int x = 0; x < m_cacheW; ++x) {
                int sx = std::min(static_cast<int>(x / scale), w - 1);
                int sy = std::min(static_cast<int>(y / scale), h - 1);

                size_t idx = (static_cast<size_t>(sy) * w + sx) * ch;
                float r = d[idx + 0];
                float g = (ch > 1) ? d[idx + 1] : r;
                float b = (ch > 2) ? d[idx + 2] : r;

                m_L_channel_cache[y * m_cacheW + x] =
                    (0.2126f * r + 0.7152f * g + 0.0722f * b) * 100.0f;
            }
        }

        updateQuickMask();
    }
}

// -----------------------------------------------------------------------------
// createUI  --  Builds the two-column layout:
//   Left  : embedded preview viewer
//   Right : HDR controls, mask preview, opacity, buttons
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::createUI()
{
    QVBoxLayout* mainLayout    = new QVBoxLayout(this);
    QHBoxLayout* contentLayout = new QHBoxLayout();

    // =========================================================================
    // Left: Embedded preview viewer
    // =========================================================================
    m_viewer = new ImageViewer(this);
    m_viewer->setProperty("isPreview", true);
    m_viewer->setMaskOverlay(false);
    m_viewer->setMinimumWidth(400);
    contentLayout->addWidget(m_viewer, 1);

    // =========================================================================
    // Right: Controls panel
    // =========================================================================
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(10, 0, 0, 0);

    QWidget* rightContainer = new QWidget(this);
    rightContainer->setLayout(rightLayout);
    rightContainer->setMinimumWidth(400);

    // --- HDR parameter sliders ---
    QGroupBox*   grp  = new QGroupBox(tr("HDR Controls"), this);
    QFormLayout* form = new QFormLayout(grp);

    // Scales
    m_scalesSlider = new QSlider(Qt::Horizontal);
    m_scalesSlider->setRange(2, 10);
    m_scalesSlider->setValue(5);
    m_scalesLabel = new QLabel("5");
    connect(m_scalesSlider, &QSlider::valueChanged, [this](int v) {
        m_scalesLabel->setText(QString::number(v));
    });

    // Compression
    m_compSlider = new QSlider(Qt::Horizontal);
    m_compSlider->setRange(10, 500);
    m_compSlider->setValue(150);
    m_compLabel = new QLabel("1.50");
    connect(m_compSlider, &QSlider::valueChanged, [this](int v) {
        m_compLabel->setText(QString::number(v / 100.0, 'f', 2));
    });

    // Mask gamma
    m_gammaSlider = new QSlider(Qt::Horizontal);
    m_gammaSlider->setRange(10, 1000);
    m_gammaSlider->setValue(500);
    m_gammaLabel = new QLabel("5.00");
    connect(m_gammaSlider, &QSlider::valueChanged, this,
            [this]([[maybe_unused]] int v) { updateQuickMask(); });

    form->addRow(tr("Scales:"),      m_scalesSlider);
    form->addRow("",                 m_scalesLabel);
    form->addRow(tr("Compression:"), m_compSlider);
    form->addRow("",                 m_compLabel);
    form->addRow(tr("Mask Gamma:"),  m_gammaSlider);
    form->addRow("",                 m_gammaLabel);

    // Dimming gamma
    m_dimmingSlider = new QSlider(Qt::Horizontal);
    m_dimmingSlider->setRange(50, 300);   // 0.50 to 3.00
    m_dimmingSlider->setValue(100);        // Default 1.00
    m_dimmingLabel = new QLabel("1.00");
    connect(m_dimmingSlider, &QSlider::valueChanged, [this](int v) {
        m_dimmingLabel->setText(QString::number(v / 100.0, 'f', 2));
    });

    form->addRow(tr("Dimming Gamma:"), m_dimmingSlider);
    form->addRow("",                   m_dimmingLabel);

    rightLayout->addWidget(grp);

    // --- Mask preview thumbnail ---
    QGroupBox*   maskGrp = new QGroupBox(tr("Mask Preview"), this);
    QVBoxLayout* maskLay = new QVBoxLayout(maskGrp);

    m_maskLabel = new QLabel(this);
    m_maskLabel->setFixedSize(350, 200);
    m_maskLabel->setScaledContents(false);
    m_maskLabel->setAlignment(Qt::AlignCenter);
    maskLay->addWidget(m_maskLabel, 0, Qt::AlignCenter);

    rightLayout->addWidget(maskGrp);

    // --- Opacity slider ---
    QGroupBox*   opacityGrp = new QGroupBox(tr("Opacity:"), this);
    QHBoxLayout* opacityLay = new QHBoxLayout(opacityGrp);

    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);

    m_opacityLabel = new QLabel("100%");
    m_opacityLabel->setMinimumWidth(36);

    connect(m_opacitySlider, &QSlider::valueChanged, [this](int val) {
        m_opacityLabel->setText(QString("%1%").arg(val));
        applyOpacityBlend();
    });

    opacityLay->addWidget(m_opacitySlider, 1);
    opacityLay->addWidget(m_opacityLabel);
    rightLayout->addWidget(opacityGrp);

    rightLayout->addStretch();

    // --- Compare checkbox ---
    m_showOriginalCheck = new QCheckBox(tr("Compare (Show Original)"), this);
    connect(m_showOriginalCheck, &QCheckBox::toggled,
            this, &WavescaleHDRDialog::toggleOriginal);
    rightLayout->addWidget(m_showOriginalCheck);

    // --- Update Preview button ---
    m_previewBtn = new QPushButton(tr("Update Preview"), this);
    connect(m_previewBtn, &QPushButton::clicked,
            this, &WavescaleHDRDialog::onPreviewClicked);
    rightLayout->addWidget(m_previewBtn);

    // --- Progress bar ---
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    rightLayout->addWidget(m_progressBar);

    // --- Bottom action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QLabel* copyLabel = new QLabel(tr("(C) 2026 SetiAstro"));
    copyLabel->setStyleSheet("color: #888; font-size: 10px;");
    btnLayout->addWidget(copyLabel);
    btnLayout->addStretch();

    m_closeBtn = new QPushButton(tr("Close"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_applyBtn = new QPushButton(tr("Apply"), this);
    connect(m_applyBtn, &QPushButton::clicked,
            this, &WavescaleHDRDialog::onApplyClicked);

    btnLayout->addWidget(m_closeBtn);
    btnLayout->addWidget(m_applyBtn);
    rightLayout->addLayout(btnLayout);

    contentLayout->addWidget(rightContainer);
    mainLayout->addLayout(contentLayout);
}

// =============================================================================
// Preview and processing slots
// =============================================================================

// -----------------------------------------------------------------------------
// startPreview  --  Launches the background worker with current slider values.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::startPreview()
{
    if (m_worker->isRunning()) return;

    m_previewBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);
    m_progressBar->setValue(0);

    int   scales   = m_scalesSlider->value();
    float comp     = m_compSlider->value()    / 100.0f;
    float gamma    = m_gammaSlider->value()   / 100.0f;
    float dimGamma = m_dimmingSlider->value() / 100.0f;

    m_worker->setup(m_originalBuffer, scales, comp, gamma, dimGamma);
    m_worker->start();
}

// -----------------------------------------------------------------------------
// updateQuickMask  --  Generates a fast mask preview from the cached downscaled
// luminance data and the current gamma slider value.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::updateQuickMask()
{
    if (m_L_channel_cache.empty() || m_cacheW <= 0 || m_cacheH <= 0) return;

    float gamma = m_gammaSlider->value() / 100.0f;
    m_gammaLabel->setText(QString::number(gamma, 'f', 2));

    QImage img(m_cacheW, m_cacheH, QImage::Format_Grayscale8);

    for (int y = 0; y < m_cacheH; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < m_cacheW; ++x) {
            float raw = m_L_channel_cache[y * m_cacheW + x] / 100.0f;
            float val = std::clamp(raw, 0.0f, 1.0f);

            if (std::abs(gamma - 1.0f) > 0.01f) {
                val = std::pow(val, gamma);
            }

            line[x] = static_cast<uchar>(val * 255.0f);
        }
    }

    QPixmap pix = QPixmap::fromImage(img);
    m_maskLabel->setPixmap(
        pix.scaled(m_maskLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_maskLabel->update();
}

void WavescaleHDRDialog::onPreviewClicked()
{
    startPreview();
}

// -----------------------------------------------------------------------------
// onApplyClicked  --  Commits the processed result to the target viewer
// with undo support.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::onApplyClicked()
{
    if (m_targetViewer && m_previewBuffer.isValid()) {
        m_targetViewer->pushUndo(tr("Wavescale HDR"));

        // The preview buffer is already mask-blended and opacity-adjusted
        m_targetViewer->setBuffer(
            m_previewBuffer, m_targetViewer->windowTitle(), true);
        m_targetViewer->refreshDisplay();

        if (auto mw = getCallbacks()) {
            mw->logMessage(tr("Wavescale HDR applied."), 1);
        }

        // Prevent the internal viewer from triggering an "unsaved changes" prompt
        if (m_viewer) {
            m_viewer->setModified(false);
        }

        emit applyInternal(m_previewBuffer);
        accept();
    }
}

// -----------------------------------------------------------------------------
// onWorkerFinished  --  Receives the worker output and updates the preview.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::onWorkerFinished(ImageBuffer result, ImageBuffer mask)
{
    m_rawResult  = result;
    m_maskBuffer = mask;

    m_previewBtn->setEnabled(true);
    m_applyBtn->setEnabled(true);
    m_progressBar->setValue(100);

    // Blend with mask and opacity, then refresh the preview viewer
    applyOpacityBlend();
}

// -----------------------------------------------------------------------------
// applyOpacityBlend  --  Blends the raw worker result with the original buffer
// using the current opacity value and any active mask.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::applyOpacityBlend()
{
    if (!m_rawResult.isValid()) return;

    float opacity = m_opacitySlider ? m_opacitySlider->value() / 100.0f : 1.0f;
    ImageBuffer blended = m_rawResult;

    if (m_originalBuffer.hasMask()) {
        // Blend through the image mask with scaled opacity
        MaskLayer ml = *m_originalBuffer.getMask();
        ml.opacity *= opacity;
        blended.setMask(ml);
        blended.blendResult(m_originalBuffer);
    } else if (opacity < 1.0f) {
        // Simple linear opacity blend
        const auto& orig = m_originalBuffer.data();
        auto&       bd   = blended.data();
        for (size_t i = 0; i < bd.size(); ++i) {
            bd[i] = bd[i] * opacity + orig[i] * (1.0f - opacity);
        }
    }

    m_previewBuffer = blended;

    if (!m_showOriginalCheck->isChecked()) {
        m_viewer->setBuffer(m_previewBuffer, tr("Preview"), true);
        m_viewer->setModified(false);
    }
}

// -----------------------------------------------------------------------------
// toggleOriginal  --  Switches the preview between the processed result and
// the original image for comparison.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::toggleOriginal(bool showOriginal)
{
    if (showOriginal) {
        m_viewer->setBuffer(m_originalBuffer, tr("Original"), true);
    } else {
        m_viewer->setBuffer(m_previewBuffer, tr("Preview"), true);
    }
    m_viewer->setModified(false);
}

// -----------------------------------------------------------------------------
// updateMaskPreview  --  Displays a worker-produced mask buffer in the mask
// preview label. Falls back to a solid gray placeholder on invalid input.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::updateMaskPreview(const ImageBuffer& mask)
{
    if (!mask.isValid()) {
        QPixmap placeholder(m_maskLabel->size());
        placeholder.fill(Qt::gray);
        m_maskLabel->setPixmap(placeholder);
        m_maskLabel->repaint();
        return;
    }

    QImage img = mask.getDisplayImage(
        ImageBuffer::Display_Linear, false, nullptr, 0, 0);

    if (img.isNull()) return;

    QPixmap pix = QPixmap::fromImage(img);
    if (!pix.isNull()) {
        m_maskLabel->setPixmap(
            pix.scaled(m_maskLabel->size(), Qt::KeepAspectRatio,
                       Qt::SmoothTransformation));
        m_maskLabel->repaint();
    }
}

// -----------------------------------------------------------------------------
// showEvent  --  Fits the preview image to the viewer on the first display.
// -----------------------------------------------------------------------------
void WavescaleHDRDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    if (m_isFirstShow && m_viewer) {
        m_viewer->fitToWindow();
        m_isFirstShow = false;
    }
}