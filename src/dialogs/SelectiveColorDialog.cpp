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

#include "SelectiveColorDialog.h"

#include "../MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
#include <QTimer>

#include <opencv2/opencv.hpp>
#include <cmath>
#include <algorithm>

// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------

SelectiveColorDialog::SelectiveColorDialog(QWidget* parent)
    : DialogBase(parent, tr("Selective Color Correction"), 950, 700)
    , m_updatingPreset(false)
{
    // Define the built-in hue range presets
    m_presets = {
        { "Custom",  0,   360 },
        { "Red",     340, 20  }, // Wraps around 360 degrees
        { "Orange",  15,  45  },
        { "Yellow",  45,  70  },
        { "Green",   70,  170 },
        { "Cyan",    170, 200 },
        { "Blue",    200, 270 },
        { "Magenta", 270, 340 }
    };

    // Acquire the current image from the active viewer for previewing
    if (MainWindowCallbacks* mw = getCallbacks())
    {
        if (mw->getCurrentViewer() && mw->getCurrentViewer()->getBuffer().isValid())
            m_sourceImage = mw->getCurrentViewer()->getBuffer();
    }

    setupUi();

    // Defer the initial preview render until after the dialog's fade-in animation
    QTimer::singleShot(300, this, &SelectiveColorDialog::onReset);

    if (parentWidget())
        move(parentWidget()->window()->geometry().center() - rect().center());
}

SelectiveColorDialog::~SelectiveColorDialog() {}

// ----------------------------------------------------------------------------
// Public Methods
// ----------------------------------------------------------------------------

void SelectiveColorDialog::setSource(const ImageBuffer& img)
{
    m_sourceImage = img;
    updateMask();
}

// ----------------------------------------------------------------------------
// Private Methods - UI Setup
// ----------------------------------------------------------------------------

void SelectiveColorDialog::setupUi()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // Left panel: all adjustment controls
    QVBoxLayout* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(8);

    // Hue Selection Group
    QGroupBox*   maskGroup  = new QGroupBox(tr("Hue Selection"));
    QGridLayout* maskLayout = new QGridLayout(maskGroup);

    maskLayout->addWidget(new QLabel(tr("Preset:")), 0, 0);
    m_presetCombo = new QComboBox();
    for (const auto& p : m_presets)
        m_presetCombo->addItem(tr(p.name.toUtf8().constData()));
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SelectiveColorDialog::onPresetChanged);
    maskLayout->addWidget(m_presetCombo, 0, 1, 1, 3);

    // Hue range start
    maskLayout->addWidget(new QLabel(tr("Hue Start (deg):")), 1, 0);
    m_hueStartSpin = new QDoubleSpinBox();
    m_hueStartSpin->setRange(0, 360);
    m_hueStartSpin->setDecimals(0);
    m_hueStartSpin->setValue(0);
    connect(m_hueStartSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_hueStartSpin, 1, 1);

    // Hue range end
    maskLayout->addWidget(new QLabel(tr("Hue End (deg):")), 1, 2);
    m_hueEndSpin = new QDoubleSpinBox();
    m_hueEndSpin->setRange(0, 360);
    m_hueEndSpin->setDecimals(0);
    m_hueEndSpin->setValue(360);
    connect(m_hueEndSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_hueEndSpin, 1, 3);

    // Feathering smoothness
    maskLayout->addWidget(new QLabel(tr("Smoothness (deg):")), 2, 0);
    m_smoothnessSpin = new QDoubleSpinBox();
    m_smoothnessSpin->setRange(0, 60);
    m_smoothnessSpin->setValue(10);
    connect(m_smoothnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_smoothnessSpin, 2, 1);

    // Minimum chroma threshold (suppresses near-gray pixels)
    maskLayout->addWidget(new QLabel(tr("Min Chroma:")), 2, 2);
    m_minChromaSpin = new QDoubleSpinBox();
    m_minChromaSpin->setRange(0, 1);
    m_minChromaSpin->setSingleStep(0.05);
    m_minChromaSpin->setValue(0.05);
    connect(m_minChromaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_minChromaSpin, 2, 3);

    // Mask intensity multiplier
    maskLayout->addWidget(new QLabel(tr("Intensity:")), 3, 0);
    m_intensitySpin = new QDoubleSpinBox();
    m_intensitySpin->setRange(0, 2);
    m_intensitySpin->setSingleStep(0.1);
    m_intensitySpin->setValue(1.0);
    connect(m_intensitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SelectiveColorDialog::updatePreview);
    maskLayout->addWidget(m_intensitySpin, 3, 1);

    m_invertCheck = new QCheckBox(tr("Invert"));
    connect(m_invertCheck, &QCheckBox::toggled, this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_invertCheck, 3, 2);

    m_showMaskCheck = new QCheckBox(tr("Show Mask"));
    connect(m_showMaskCheck, &QCheckBox::toggled, this, &SelectiveColorDialog::updatePreview);
    maskLayout->addWidget(m_showMaskCheck, 3, 3);

    leftLayout->addWidget(maskGroup);

    // Helper lambda for creating a consistently configured horizontal slider
    auto createSlider = [](int min, int max, int val) -> QSlider* {
        QSlider* s = new QSlider(Qt::Horizontal);
        s->setRange(min, max);
        s->setValue(val);
        return s;
    };

    // CMY Adjustments Group
    QGroupBox*   cmyGroup  = new QGroupBox(tr("CMY Adjustments"));
    QFormLayout* cmyLayout = new QFormLayout(cmyGroup);

    m_cyanSlider = createSlider(-100, 100, 0);
    connect(m_cyanSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    cmyLayout->addRow(tr("Cyan:"), m_cyanSlider);

    m_magentaSlider = createSlider(-100, 100, 0);
    connect(m_magentaSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    cmyLayout->addRow(tr("Magenta:"), m_magentaSlider);

    m_yellowSlider = createSlider(-100, 100, 0);
    connect(m_yellowSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    cmyLayout->addRow(tr("Yellow:"), m_yellowSlider);

    leftLayout->addWidget(cmyGroup);

    // RGB Adjustments Group
    QGroupBox*   rgbGroup  = new QGroupBox(tr("RGB Adjustments"));
    QFormLayout* rgbLayout = new QFormLayout(rgbGroup);

    m_redSlider = createSlider(-100, 100, 0);
    connect(m_redSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    rgbLayout->addRow(tr("Red:"), m_redSlider);

    m_greenSlider = createSlider(-100, 100, 0);
    connect(m_greenSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    rgbLayout->addRow(tr("Green:"), m_greenSlider);

    m_blueSlider = createSlider(-100, 100, 0);
    connect(m_blueSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    rgbLayout->addRow(tr("Blue:"), m_blueSlider);

    leftLayout->addWidget(rgbGroup);

    // Luminance / Saturation / Contrast Group
    QGroupBox*   lscGroup  = new QGroupBox(tr("Luminance / Saturation / Contrast"));
    QFormLayout* lscLayout = new QFormLayout(lscGroup);

    m_luminanceSlider = createSlider(-100, 100, 0);
    connect(m_luminanceSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    lscLayout->addRow(tr("Luminance:"), m_luminanceSlider);

    m_saturationSlider = createSlider(-100, 100, 0);
    connect(m_saturationSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    lscLayout->addRow(tr("Saturation:"), m_saturationSlider);

    m_contrastSlider = createSlider(-100, 100, 0);
    connect(m_contrastSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    lscLayout->addRow(tr("Contrast:"), m_contrastSlider);

    leftLayout->addWidget(lscGroup);

    // Action buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* resetBtn = new QPushButton(tr("Reset"));
    connect(resetBtn, &QPushButton::clicked, this, &SelectiveColorDialog::onReset);
    btnLayout->addWidget(resetBtn);
    btnLayout->addStretch();

    QPushButton* closeBtn = new QPushButton(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);

    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    connect(applyBtn, &QPushButton::clicked, this, &SelectiveColorDialog::onApply);
    btnLayout->addWidget(applyBtn);

    leftLayout->addLayout(btnLayout);
    mainLayout->addLayout(leftLayout);

    // Right panel: preview display
    QVBoxLayout* rightLayout = new QVBoxLayout();
    m_scene = new QGraphicsScene(this);
    m_view  = new QGraphicsView(m_scene);
    m_view->setMinimumSize(400, 400);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);

    rightLayout->addWidget(m_view);
    mainLayout->addLayout(rightLayout, 1);
}

// ----------------------------------------------------------------------------
// Private Slots
// ----------------------------------------------------------------------------

void SelectiveColorDialog::onPresetChanged(int index)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size()))
        return;

    // Block signals to prevent updateMask from being called mid-change
    m_updatingPreset = true;
    const auto& p = m_presets[index];
    m_hueStartSpin->setValue(p.hueStart);
    m_hueEndSpin->setValue(p.hueEnd);
    m_updatingPreset = false;

    updateMask();
}

void SelectiveColorDialog::onReset()
{
    m_presetCombo->setCurrentIndex(1); // Default to "Red" preset

    m_cyanSlider->setValue(0);
    m_magentaSlider->setValue(0);
    m_yellowSlider->setValue(0);
    m_redSlider->setValue(0);
    m_greenSlider->setValue(0);
    m_blueSlider->setValue(0);
    m_luminanceSlider->setValue(0);
    m_saturationSlider->setValue(0);
    m_contrastSlider->setValue(0);

    m_intensitySpin->setValue(1.0);
    m_smoothnessSpin->setValue(10.0);
    m_minChromaSpin->setValue(0.05);
    m_invertCheck->setChecked(false);
    m_showMaskCheck->setChecked(false);

    updateMask();
}

void SelectiveColorDialog::onApply()
{
    MainWindowCallbacks* mw = getCallbacks();
    if (!mw)
        return;

    ImageViewer* v = mw->getCurrentViewer();
    if (!v)
        return;

    v->pushUndo(tr("Selective Color"));

    ImageBuffer& buffer   = v->getBuffer();
    ImageBuffer  original = buffer; // Preserve a copy for mask blending

    // Recompute the hue mask based on the live buffer at the time of apply
    const std::vector<float> currentMask = computeHueMask(buffer);
    ImageBuffer result = applyAdjustments(buffer, currentMask);

    // Propagate the original mask onto the result so masked regions are respected
    if (original.hasMask())
    {
        result.setMask(*original.getMask());
        result.blendResult(original);
    }

    v->setBuffer(result, buffer.name(), true);

    if (mw)
        mw->logMessage(tr("Selective Color applied."), 1);

    accept();
}

// ----------------------------------------------------------------------------
// Private Methods - Image Processing
// ----------------------------------------------------------------------------

std::vector<float> SelectiveColorDialog::computeHueMask(const ImageBuffer& src)
{
    // For mono or invalid images, return a uniform full-weight mask
    if (!src.isValid() || src.channels() < 3)
        return std::vector<float>(src.width() * src.height(), 1.0f);

    const int   w         = src.width();
    const int   h         = src.height();
    const int   c         = src.channels();
    const float hueStart  = static_cast<float>(m_hueStartSpin->value());
    const float hueEnd    = static_cast<float>(m_hueEndSpin->value());
    const float smooth    = static_cast<float>(m_smoothnessSpin->value());
    const float minChroma = static_cast<float>(m_minChromaSpin->value());
    const bool  invert    = m_invertCheck->isChecked();

    // Convert the source buffer to an 8-bit BGR OpenCV image for HSV conversion
    cv::Mat rgb(h, w, CV_8UC3);
    const float* srcData = src.data().data();

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int idx = (y * w + x) * c;
            // Remap float [0,1] RGB to uint8 BGR for OpenCV
            rgb.at<cv::Vec3b>(y, x)[2] = static_cast<uint8_t>(std::clamp(srcData[idx],     0.0f, 1.0f) * 255);
            rgb.at<cv::Vec3b>(y, x)[1] = static_cast<uint8_t>(std::clamp(srcData[idx + 1], 0.0f, 1.0f) * 255);
            rgb.at<cv::Vec3b>(y, x)[0] = static_cast<uint8_t>(std::clamp(srcData[idx + 2], 0.0f, 1.0f) * 255);
        }
    }

    cv::Mat hsv;
    cv::cvtColor(rgb, hsv, cv::COLOR_BGR2HSV);

    std::vector<float> mask(w * h, 0.0f);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const cv::Vec3b pixel = hsv.at<cv::Vec3b>(y, x);
            const float H = pixel[0] * 2.0f; // OpenCV H range is [0, 180]; scale to [0, 360]
            const float S = pixel[1] / 255.0f;
            const float V = pixel[2] / 255.0f;

            // Skip pixels with insufficient chroma (near-neutral colors)
            const float chroma = S * V;
            if (chroma < minChroma)
            {
                mask[y * w + x] = invert ? 1.0f : 0.0f;
                continue;
            }

            // Compute the arc length of the hue band (handles wrap-around at 360 degrees)
            float L = std::fmod(hueEnd - hueStart + 360.0f, 360.0f);
            if (L <= 0.0f) L = 360.0f;

            const float fwd = std::fmod(H - hueStart + 360.0f, 360.0f);

            float bandWeight = 0.0f;

            if (fwd <= L)
            {
                bandWeight = 1.0f;
            }
            else if (smooth > 0)
            {
                // Feather the trailing edge of the band
                if (fwd < L + smooth)
                    bandWeight = 1.0f - (fwd - L) / smooth;

                // Feather the leading edge of the band
                const float bwd = std::fmod(hueStart - H + 360.0f, 360.0f);
                if (bwd < smooth)
                    bandWeight = std::max(bandWeight, 1.0f - bwd / smooth);
            }

            bandWeight = std::clamp(bandWeight, 0.0f, 1.0f);
            if (invert) bandWeight = 1.0f - bandWeight;

            mask[y * w + x] = bandWeight;
        }
    }

    return mask;
}

void SelectiveColorDialog::updateMask()
{
    if (!m_sourceImage.isValid())
        return;

    m_mask = computeHueMask(m_sourceImage);
    updatePreview();
}

ImageBuffer SelectiveColorDialog::applyAdjustments(const ImageBuffer& src, const std::vector<float>& mask)
{
    if (!src.isValid())
        return src;

    const int w = src.width();
    const int h = src.height();
    const int c = src.channels();

    const float intensity = static_cast<float>(m_intensitySpin->value());

    // Read all slider values and normalize to the [-1.0, +1.0] range
    const float cyan    = m_cyanSlider->value()       / 100.0f;
    const float magenta = m_magentaSlider->value()    / 100.0f;
    const float yellow  = m_yellowSlider->value()     / 100.0f;
    const float red     = m_redSlider->value()        / 100.0f;
    const float green   = m_greenSlider->value()      / 100.0f;
    const float blue    = m_blueSlider->value()       / 100.0f;
    const float lum     = m_luminanceSlider->value()  / 100.0f;
    const float sat     = m_saturationSlider->value() / 100.0f;
    const float con     = m_contrastSlider->value()   / 100.0f;

    ImageBuffer    result  = src;
    float*         dstData = result.data().data();
    const float*   srcData = src.data().data();

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int   idx = (y * w + x) * c;
            const float m   = std::clamp(mask[y * w + x] * intensity, 0.0f, 1.0f);

            // Skip pixels outside the masked region
            if (m < 0.001f)
                continue;

            float R = srcData[idx];
            float G = srcData[idx + 1];
            float B = srcData[idx + 2];

            // CMY adjustments: each channel is reduced by its complementary CMY value
            R = std::clamp(R - cyan    * m, 0.0f, 1.0f);
            G = std::clamp(G - magenta * m, 0.0f, 1.0f);
            B = std::clamp(B - yellow  * m, 0.0f, 1.0f);

            // Direct RGB additive adjustments
            R = std::clamp(R + red   * m, 0.0f, 1.0f);
            G = std::clamp(G + green * m, 0.0f, 1.0f);
            B = std::clamp(B + blue  * m, 0.0f, 1.0f);

            // Luminance: uniform additive offset across all channels
            if (std::abs(lum) > 0.001f)
            {
                R = std::clamp(R + lum * m, 0.0f, 1.0f);
                G = std::clamp(G + lum * m, 0.0f, 1.0f);
                B = std::clamp(B + lum * m, 0.0f, 1.0f);
            }

            // Contrast: scales each channel around the midpoint (0.5)
            if (std::abs(con) > 0.001f)
            {
                const float factor = 1.0f + con * m;
                R = std::clamp((R - 0.5f) * factor + 0.5f, 0.0f, 1.0f);
                G = std::clamp((G - 0.5f) * factor + 0.5f, 0.0f, 1.0f);
                B = std::clamp((B - 0.5f) * factor + 0.5f, 0.0f, 1.0f);
            }

            // Saturation: scales the distance of each channel from the luminance value
            if (std::abs(sat) > 0.001f)
            {
                const float gray   = 0.299f * R + 0.587f * G + 0.114f * B;
                const float factor = 1.0f + sat * m;
                R = std::clamp(gray + (R - gray) * factor, 0.0f, 1.0f);
                G = std::clamp(gray + (G - gray) * factor, 0.0f, 1.0f);
                B = std::clamp(gray + (B - gray) * factor, 0.0f, 1.0f);
            }

            dstData[idx]     = R;
            dstData[idx + 1] = G;
            dstData[idx + 2] = B;
        }
    }

    return result;
}

void SelectiveColorDialog::updatePreview()
{
    if (!m_sourceImage.isValid())
        return;

    if (m_showMaskCheck->isChecked())
    {
        // Display the hue mask as a grayscale image for diagnostic purposes
        const int w = m_sourceImage.width();
        const int h = m_sourceImage.height();

        QImage qimg(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const uint8_t v = static_cast<uint8_t>(m_mask[y * w + x] * 255);
                qimg.setPixel(x, y, qRgb(v, v, v));
            }
        }

        m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    }
    else
    {
        // Display the color-adjusted image result
        m_previewImage = applyAdjustments(m_sourceImage, m_mask);
        const QImage qimg = m_previewImage.getDisplayImage();
        m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    }

    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
}