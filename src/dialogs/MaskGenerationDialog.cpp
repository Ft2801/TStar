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

#include "MaskGenerationDialog.h"
#include "MaskCanvas.h"
#include "LivePreviewDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QtMath>
#include <QtConcurrent/QtConcurrent>
#include <QMessageBox>
#include <QUuid>
#include <opencv2/opencv.hpp>

// =============================================================================
// Constructor
// =============================================================================

MaskGenerationDialog::MaskGenerationDialog(const ImageBuffer& image, QWidget* parent)
    : DialogBase(parent, tr("Mask Generation"), 1024, 700)
    , m_sourceImage(image)
    , m_livePreview(nullptr)
{
    setWindowFlag(Qt::Window);
    setupUI();

    if (parentWidget())
        move(parentWidget()->window()->geometry().center() - rect().center());
}

// =============================================================================
// UI Construction
// =============================================================================

void MaskGenerationDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // -------------------------------------------------------------------------
    // Mode toolbar: drawing mode selection and zoom controls
    // -------------------------------------------------------------------------
    QHBoxLayout* modeBar = new QHBoxLayout();

    m_freeBtn    = new QPushButton(tr("Freehand"));
    m_ellipseBtn = new QPushButton(tr("Ellipse"));
    m_selectBtn  = new QPushButton(tr("Select Entire Image"));
    m_moveBtn    = new QPushButton(tr("Move/Select"));

    for (QPushButton* btn : { m_freeBtn, m_ellipseBtn, m_selectBtn, m_moveBtn })
        btn->setCheckable(true);

    m_freeBtn->setChecked(true);

    QButtonGroup* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_freeBtn);
    modeGroup->addButton(m_ellipseBtn);
    modeGroup->addButton(m_selectBtn);
    modeGroup->addButton(m_moveBtn);
    modeGroup->setExclusive(true);

    const QString btnStyle =
        "QPushButton { padding:6px; border:1px solid #888; border-radius:4px;"
        "  background:transparent; color:#e0e0e0; }"
        "QPushButton:checked { background-color:#0078d4; color:white; border-color:#005a9e; }";

    for (QPushButton* btn : { m_freeBtn, m_ellipseBtn, m_selectBtn, m_moveBtn })
        btn->setStyleSheet(btnStyle);

    connect(m_freeBtn,    &QPushButton::clicked, [this]() { setMode("polygon"); });
    connect(m_ellipseBtn, &QPushButton::clicked, [this]() { setMode("ellipse"); });
    connect(m_selectBtn,  &QPushButton::clicked, [this]() { setMode("select");  });
    connect(m_moveBtn,    &QPushButton::clicked, [this]() { setMode("move");    });

    modeBar->addWidget(m_freeBtn);
    modeBar->addWidget(m_ellipseBtn);
    modeBar->addWidget(m_selectBtn);
    modeBar->addWidget(m_moveBtn);
    modeBar->addStretch();

    QPushButton* zOut = new QPushButton(tr("Zoom Out"));
    QPushButton* zIn  = new QPushButton(tr("Zoom In"));
    QPushButton* zFit = new QPushButton(tr("Fit"));

    connect(zOut, &QPushButton::clicked, [this]() { m_canvas->zoomOut();    });
    connect(zIn,  &QPushButton::clicked, [this]() { m_canvas->zoomIn();     });
    connect(zFit, &QPushButton::clicked, [this]() { m_canvas->fitToView();  });

    modeBar->addWidget(zOut);
    modeBar->addWidget(zIn);
    modeBar->addWidget(zFit);

    mainLayout->addLayout(modeBar);

    // -------------------------------------------------------------------------
    // Canvas: displays the background image and hosts drawn shapes
    // -------------------------------------------------------------------------
    QImage bg = m_sourceImage.getDisplayImage(ImageBuffer::Display_Linear, true);
    m_canvas = new MaskCanvas(bg, this);
    m_canvas->setFixedHeight(400);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_canvas, &MaskCanvas::maskContentChanged,
            this,     &MaskGenerationDialog::updateLivePreview);

    mainLayout->addWidget(m_canvas);

    // -------------------------------------------------------------------------
    // Mask type and global blur controls
    // -------------------------------------------------------------------------
    QHBoxLayout* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(tr("Mask Type:")));

    m_typeCombo = new QComboBox();
    m_typeCombo->addItem(tr("Binary"),          "Binary");
    m_typeCombo->addItem(tr("Range Selection"), "Range Selection");
    m_typeCombo->addItem(tr("Lightness"),        "Lightness");
    m_typeCombo->addItem(tr("Chrominance"),      "Chrominance");
    m_typeCombo->addItem(tr("Star Mask"),        "Star Mask");
    m_typeCombo->addItem(tr("Color: Red"),       "Color: Red");
    m_typeCombo->addItem(tr("Color: Orange"),    "Color: Orange");
    m_typeCombo->addItem(tr("Color: Yellow"),    "Color: Yellow");
    m_typeCombo->addItem(tr("Color: Green"),     "Color: Green");
    m_typeCombo->addItem(tr("Color: Cyan"),      "Color: Cyan");
    m_typeCombo->addItem(tr("Color: Blue"),      "Color: Blue");
    m_typeCombo->addItem(tr("Color: Violet"),    "Color: Violet");
    m_typeCombo->addItem(tr("Color: Magenta"),   "Color: Magenta");

    connect(m_typeCombo, &QComboBox::currentTextChanged,
            this, &MaskGenerationDialog::onTypeChanged);
    connect(m_typeCombo, &QComboBox::currentTextChanged,
            this, &MaskGenerationDialog::updateLivePreview);

    controls->addWidget(m_typeCombo);

    controls->addWidget(new QLabel(tr("Edge Blur (px):")));
    m_blurSlider = new QSlider(Qt::Horizontal);
    m_blurSlider->setRange(0, 300);
    m_blurLabel  = new QLabel("0");

    connect(m_blurSlider, &QSlider::valueChanged,
            [this](int v) { m_blurLabel->setText(QString::number(v)); });
    connect(m_blurSlider, &QSlider::valueChanged,
            this, &MaskGenerationDialog::updateLivePreview);

    controls->addWidget(m_blurSlider);
    controls->addWidget(m_blurLabel);

    mainLayout->addLayout(controls);

    // -------------------------------------------------------------------------
    // Range selection group (visible only when mask type == Range Selection)
    // -------------------------------------------------------------------------
    m_rangeGroup = new QGroupBox(tr("Range Selection"));
    QGridLayout* rangeGrid = new QGridLayout(m_rangeGroup);

    // Helper lambda: create a labelled slider row and connect it to the live preview
    auto addSlider = [&](int row, const QString& name, int max, QLabel*& lblInfo) -> QSlider* {
        rangeGrid->addWidget(new QLabel(name + ":"), row, 0);
        QSlider* s = new QSlider(Qt::Horizontal);
        s->setRange(0, max);
        lblInfo = new QLabel("0.00");
        rangeGrid->addWidget(s,      row, 1);
        rangeGrid->addWidget(lblInfo, row, 2);
        connect(s, &QSlider::valueChanged, [max, lblInfo](int v) {
            lblInfo->setText(QString::number(static_cast<double>(v) / max, 'f', 2));
        });
        connect(s, &QSlider::valueChanged,
                this, &MaskGenerationDialog::updateLivePreview);
        return s;
    };

    m_lowerSl = addSlider(0, tr("Lower Limit"), 100, m_lowerLbl);
    m_upperSl = addSlider(1, tr("Upper Limit"), 100, m_upperLbl);
    m_fuzzSl  = addSlider(2, tr("Fuzziness"),   100, m_fuzzLbl);

    rangeGrid->addWidget(new QLabel(tr("Internal Blur:")), 3, 0);
    m_smoothSl = new QSlider(Qt::Horizontal);
    m_smoothSl->setRange(0, 200);
    m_smoothSl->setValue(3);
    m_smoothLbl = new QLabel("3 px");
    rangeGrid->addWidget(m_smoothSl,  3, 1);
    rangeGrid->addWidget(m_smoothLbl, 3, 2);
    connect(m_smoothSl, &QSlider::valueChanged,
            [this](int v) { m_smoothLbl->setText(QString("%1 px").arg(v)); });
    connect(m_smoothSl, &QSlider::valueChanged,
            this, &MaskGenerationDialog::updateLivePreview);

    // Link Limits: keeps lower and upper sliders in sync
    m_linkCb = new QCheckBox(tr("Link Limits"));
    rangeGrid->addWidget(m_linkCb, 0, 3);
    connect(m_linkCb, &QCheckBox::toggled, [this](bool checked) {
        if (checked) m_upperSl->setValue(m_lowerSl->value());
    });
    connect(m_lowerSl, &QSlider::valueChanged, [this](int v) {
        if (m_linkCb->isChecked()) m_upperSl->setValue(v);
    });

    m_screenCb = new QCheckBox(tr("Screening"));
    rangeGrid->addWidget(m_screenCb, 1, 3);
    connect(m_screenCb, &QCheckBox::toggled,
            this, &MaskGenerationDialog::updateLivePreview);

    m_lightCb = new QCheckBox(tr("Use Lightness"));
    m_lightCb->setChecked(true);
    rangeGrid->addWidget(m_lightCb, 2, 3);
    connect(m_lightCb, &QCheckBox::toggled,
            this, &MaskGenerationDialog::updateLivePreview);

    m_invertCb = new QCheckBox(tr("Invert Range"));
    rangeGrid->addWidget(m_invertCb, 3, 3);
    connect(m_invertCb, &QCheckBox::toggled,
            this, &MaskGenerationDialog::updateLivePreview);

    m_rangeGroup->setVisible(false);
    mainLayout->addWidget(m_rangeGroup);

    // -------------------------------------------------------------------------
    // Color mask group (visible only for Color: * mask types)
    // -------------------------------------------------------------------------
    m_colorGroup = new QGroupBox(tr("Color Mask"));
    QGridLayout* colorGrid = new QGridLayout(m_colorGroup);

    colorGrid->addWidget(new QLabel(tr("Fuzziness (deg):")), 0, 0);
    m_colorFuzzSl = new QSlider(Qt::Horizontal);
    m_colorFuzzSl->setRange(0, 60);
    m_colorFuzzSl->setValue(0);
    m_colorFuzzLbl = new QLabel("0 deg");
    colorGrid->addWidget(m_colorFuzzSl,  0, 1);
    colorGrid->addWidget(m_colorFuzzLbl, 0, 2);
    connect(m_colorFuzzSl, &QSlider::valueChanged,
            [this](int v) { m_colorFuzzLbl->setText(QString("%1 deg").arg(v)); });
    connect(m_colorFuzzSl, &QSlider::valueChanged,
            this, &MaskGenerationDialog::updateLivePreview);

    m_colorGroup->setVisible(false);
    mainLayout->addWidget(m_colorGroup);

    // Default: upper limit at 1.0
    m_upperSl->setValue(100);

    // -------------------------------------------------------------------------
    // Preview visualization: stretch mode and resolution controls
    // -------------------------------------------------------------------------
    QGroupBox*   previewGroup  = new QGroupBox(tr("Preview Visualization"));
    QHBoxLayout* previewLayout = new QHBoxLayout(previewGroup);

    previewLayout->addWidget(new QLabel(tr("Stretch:")));
    m_previewStretchCombo = new QComboBox();
    m_previewStretchCombo->addItem(tr("Linear"),       ImageBuffer::Display_Linear);
    m_previewStretchCombo->addItem(tr("Auto Stretch"), ImageBuffer::Display_AutoStretch);
    m_previewStretchCombo->addItem(tr("Histogram"),    ImageBuffer::Display_Histogram);
    m_previewStretchCombo->addItem(tr("ArcSinh"),      ImageBuffer::Display_ArcSinh);
    m_previewStretchCombo->addItem(tr("Sqrt"),         ImageBuffer::Display_Sqrt);
    m_previewStretchCombo->addItem(tr("Log"),          ImageBuffer::Display_Log);
    m_previewStretchCombo->setCurrentIndex(0);

    // Build the downsampled luma cache for fast preview generation
    m_maxPreviewDim = 1024;
    const int w = m_sourceImage.width();
    const int h = m_sourceImage.height();
    float scale = 1.0f;
    if (w > m_maxPreviewDim || h > m_maxPreviewDim)
        scale = std::min(static_cast<float>(m_maxPreviewDim) / w,
                         static_cast<float>(m_maxPreviewDim) / h);

    m_smallW = static_cast<int>(w * scale);
    m_smallH = static_cast<int>(h * scale);

    {
        std::vector<float> L = getComponentLightness();
        cv::Mat matL(h, w, CV_32FC1, L.data());
        cv::Mat smallMatL;
        cv::resize(matL, smallMatL, cv::Size(m_smallW, m_smallH), 0, 0, cv::INTER_AREA);
        m_smallLuma.resize(m_smallW * m_smallH);
        std::memcpy(m_smallLuma.data(), smallMatL.data, m_smallLuma.size() * sizeof(float));
    }

    connect(m_previewStretchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this]() {
                updateLivePreview();
                if (m_canvas) {
                    const auto mode = static_cast<ImageBuffer::DisplayMode>(
                        m_previewStretchCombo->currentData().toInt());
                    m_canvas->setBackgroundImage(m_sourceImage.getDisplayImage(mode, true));
                }
            });

    previewLayout->addWidget(m_previewStretchCombo);
    previewLayout->addWidget(new QLabel(tr("Size:")));

    m_previewSizeCombo = new QComboBox();
    m_previewSizeCombo->addItem("512px",  512);
    m_previewSizeCombo->addItem("1024px", 1024);
    m_previewSizeCombo->setCurrentIndex(1);

    connect(m_previewSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this]() {
                const int dim  = m_previewSizeCombo->currentData().toInt();
                m_maxPreviewDim = (dim == 0) ? 99999 : dim;

                const int w2 = m_sourceImage.width();
                const int h2 = m_sourceImage.height();
                float sc = 1.0f;
                if (w2 > m_maxPreviewDim || h2 > m_maxPreviewDim)
                    sc = std::min(static_cast<float>(m_maxPreviewDim) / w2,
                                  static_cast<float>(m_maxPreviewDim) / h2);

                m_smallW = static_cast<int>(w2 * sc);
                m_smallH = static_cast<int>(h2 * sc);

                std::vector<float> L = getComponentLightness();
                cv::Mat matL(h2, w2, CV_32FC1, L.data());
                cv::Mat smallMatL;
                cv::resize(matL, smallMatL, cv::Size(m_smallW, m_smallH), 0, 0, cv::INTER_AREA);
                m_smallLuma.resize(m_smallW * m_smallH);
                std::memcpy(m_smallLuma.data(), smallMatL.data, m_smallLuma.size() * sizeof(float));

                updateLivePreview();
            });

    previewLayout->addWidget(m_previewSizeCombo);
    mainLayout->addWidget(previewGroup);

    // -------------------------------------------------------------------------
    // Action buttons: Preview Mask, Clear Shapes, Cancel, OK
    // -------------------------------------------------------------------------
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* previewBtn = new QPushButton(tr("Preview Mask"));
    QPushButton* clearBtn   = new QPushButton(tr("Clear Shapes"));
    connect(previewBtn, &QPushButton::clicked, this, &MaskGenerationDialog::generatePreview);
    connect(clearBtn,   &QPushButton::clicked, this, &MaskGenerationDialog::clearShapes);
    btnLayout->addWidget(previewBtn);
    btnLayout->addWidget(clearBtn);
    mainLayout->addLayout(btnLayout);

    QHBoxLayout* btnLayout2 = new QHBoxLayout();
    btnLayout2->addStretch();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    QPushButton* okBtn     = new QPushButton(tr("OK"));
    okBtn->setDefault(true);
    btnLayout2->addWidget(cancelBtn);
    btnLayout2->addWidget(okBtn);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addLayout(btnLayout2);
}

// =============================================================================
// Mode and Shape Management
// =============================================================================

void MaskGenerationDialog::setMode(const QString& mode)
{
    if (m_canvas) m_canvas->setMode(mode);
}

void MaskGenerationDialog::clearShapes()
{
    if (m_canvas) m_canvas->clearShapes();
}

// Show or hide the range / color parameter groups based on the selected mask type.
// Preserves the dialog width while allowing vertical resize.
void MaskGenerationDialog::onTypeChanged(const QString& /*type*/)
{
    const QString id   = m_typeCombo->currentData().toString();
    const bool isRange = (id == "Range Selection");
    const bool isColor = id.startsWith("Color:");
    const int  curW    = width();

    m_rangeGroup->setVisible(isRange);
    m_colorGroup->setVisible(isColor);

    updateGeometry();
    adjustSize();
    resize(curW, height());
}

// =============================================================================
// Mask Generation
// =============================================================================

MaskLayer MaskGenerationDialog::getGeneratedMask(int requestedW, int requestedH) const
{
    const QString t      = m_typeCombo->currentData().toString();
    const int     targetW = (requestedW > 0) ? requestedW : m_sourceImage.width();
    const int     targetH = (requestedH > 0) ? requestedH : m_sourceImage.height();
    const bool    isPreview = (requestedW > 0);

    // Obtain the geometric base mask (canvas shapes rasterized to target resolution)
    std::vector<float> base = isPreview
        ? m_canvas->createMask(targetW, targetH)
        : const_cast<MaskGenerationDialog*>(this)->generateBaseMask();

    // For analytical mask types, an empty canvas implies the full image is selected
    const bool baseIsEmpty = std::all_of(base.begin(), base.end(),
                                         [](float v) { return v == 0.0f; });

    static const QString ALL_IMAGE_TYPES[] = {
        "Range Selection", "Lightness", "Chrominance", "Star Mask",
        "Color: Red", "Color: Orange", "Color: Yellow", "Color: Green",
        "Color: Cyan", "Color: Blue", "Color: Violet", "Color: Magenta"
    };

    if (baseIsEmpty) {
        for (const QString& ait : ALL_IMAGE_TYPES) {
            if (t == ait || t.startsWith("Color:")) {
                base.assign(targetW * targetH, 1.0f);
                break;
            }
        }
    }

    std::vector<float> finalMask;

    if (t == "Binary") {
        finalMask = base;

    } else if (t == "Range Selection") {
        std::vector<float> comp = getLightness(targetW, targetH);

        const float L     = m_lowerSl->value() / 100.0f;
        const float U     = m_upperSl->value() / 100.0f;
        const float fuzz  = m_fuzzSl->value()  / 100.0f;
        int         smooth = m_smoothSl->value();
        const bool  screen = m_screenCb->isChecked();
        const bool  inv    = m_invertCb->isChecked();

        // Scale the blur radius proportionally when generating a preview
        if (isPreview) {
            const float sc = static_cast<float>(targetW) / m_sourceImage.width();
            smooth = std::max(0, static_cast<int>(smooth * sc));
        }

        const float smoothSigma = (smooth > 0) ? smooth / 3.0f : 0.0f;
        finalMask.resize(base.size());

        #pragma omp parallel for
        for (size_t i = 0; i < comp.size(); ++i) {
            const float v = comp[i];
            float val     = 0.0f;

            if (v >= L && v <= U) {
                val = 1.0f;
            } else if (fuzz > 1e-6f) {
                if      (v >= L - fuzz && v < L) val = (v - (L - fuzz)) / fuzz;
                else if (v >  U        && v <= U + fuzz) val = ((U + fuzz) - v) / fuzz;
            }

            val = std::clamp(val, 0.0f, 1.0f);
            if (screen) val *= v;
            finalMask[i] = base[i] * val;
        }

        if (smoothSigma > 0.0f) {
            cv::Mat mat(targetH, targetW, CV_32FC1, finalMask.data());
            cv::GaussianBlur(mat, mat, cv::Size(0, 0), smoothSigma);
        }
        if (inv)
            for (float& v : finalMask) v = 1.0f - v;

    } else if (t == "Lightness") {
        std::vector<float> L = getLightness(targetW, targetH);
        finalMask.resize(base.size());
        for (size_t i = 0; i < base.size(); ++i)
            finalMask[i] = (base[i] > 0) ? L[i] : 0.0f;

    } else if (t == "Chrominance") {
        std::vector<float> C = getChrominance(targetW, targetH);
        finalMask.resize(base.size());
        for (size_t i = 0; i < base.size(); ++i)
            finalMask[i] = (base[i] > 0) ? C[i] : 0.0f;

    } else if (t == "Star Mask") {
        std::vector<float> S = getStarMask(targetW, targetH);
        finalMask.resize(base.size());
        for (size_t i = 0; i < base.size(); ++i)
            finalMask[i] = (base[i] > 0) ? S[i] : 0.0f;

    } else if (t.startsWith("Color:")) {
        const QString color    = t.mid(7);
        const float   fuzzDeg  = static_cast<float>(m_colorFuzzSl->value());
        std::vector<float> C   = getColorMask(color, targetW, targetH, fuzzDeg);
        finalMask.resize(base.size());
        for (size_t i = 0; i < base.size(); ++i)
            finalMask[i] = (base[i] > 0) ? C[i] : 0.0f;
    }

    // Apply the global edge-blur (scaled for preview)
    int blur = m_blurSlider->value();
    if (isPreview) {
        const float sc = static_cast<float>(targetW) / m_sourceImage.width();
        blur = std::max(0, static_cast<int>(blur * sc));
    }
    const float blurSigma = (blur > 0) ? blur / 3.0f : 0.0f;
    if (blurSigma > 0.0f && !finalMask.empty()) {
        cv::Mat m(targetH, targetW, CV_32FC1, finalMask.data());
        cv::GaussianBlur(m, m, cv::Size(0, 0), blurSigma);
    }

    MaskLayer layer;
    layer.data    = finalMask;
    layer.width   = targetW;
    layer.height  = targetH;
    layer.name    = m_typeCombo->currentText();
    layer.visible = true;
    layer.id      = QUuid::createUuid().toString();
    layer.mode    = "replace";
    layer.opacity = 1.0f;

    return layer;
}

std::vector<float> MaskGenerationDialog::generateBaseMask()
{
    return m_canvas->createMask();
}

// =============================================================================
// Image Component Helpers
// =============================================================================

// Compute per-pixel luminance using standard BT.601 coefficients.
std::vector<float> MaskGenerationDialog::getComponentLightness() const
{
    const std::vector<float>& d = m_sourceImage.data();
    const int ch  = m_sourceImage.channels();
    const int n   = m_sourceImage.width() * m_sourceImage.height();
    std::vector<float> L(n);

    #pragma omp parallel for
    for (int i = 0; i < n; ++i) {
        if (ch == 1) {
            L[i] = d[i];
        } else {
            L[i] = 0.2989f * d[i * 3 + 0]
                 + 0.5870f * d[i * 3 + 1]
                 + 0.1140f * d[i * 3 + 2];
        }
    }
    return L;
}

// Return the cached downsampled luma if dimensions match; otherwise compute on the fly.
std::vector<float> MaskGenerationDialog::getLightness(int w, int h) const
{
    if (w == m_smallW && h == m_smallH && !m_smallLuma.empty())
        return m_smallLuma;

    if (w == m_sourceImage.width() && h == m_sourceImage.height())
        return getComponentLightness();

    std::vector<float> L = getComponentLightness();
    std::vector<float> res(w * h);
    cv::Mat src(m_sourceImage.height(), m_sourceImage.width(), CV_32FC1, L.data());
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(w, h), 0, 0, cv::INTER_AREA);
    std::memcpy(res.data(), dst.data, res.size() * sizeof(float));
    return res;
}

// Compute the chrominance mask at a specific resolution (resizes source if needed).
std::vector<float> MaskGenerationDialog::getChrominance(int w, int h) const
{
    if (w == m_sourceImage.width() && h == m_sourceImage.height())
        return getComponentChrominance();

    cv::Mat fullSrc(m_sourceImage.height(), m_sourceImage.width(), CV_32FC3,
                    const_cast<float*>(m_sourceImage.data().data()));
    cv::Mat src;
    cv::resize(fullSrc, src, cv::Size(w, h), 0, 0, cv::INTER_AREA);

    cv::Mat ycrcb;
    cv::cvtColor(src, ycrcb, cv::COLOR_RGB2YCrCb);

    std::vector<cv::Mat> chans;
    cv::split(ycrcb, chans);

    const cv::Scalar meanCr = cv::mean(chans[1]);
    const cv::Scalar meanCb = cv::mean(chans[2]);

    cv::Mat diffCr = chans[1] - meanCr;
    cv::Mat diffCb = chans[2] - meanCb;
    cv::Mat dist;
    cv::sqrt(diffCr.mul(diffCr) + diffCb.mul(diffCb), dist);
    cv::normalize(dist, dist, 0, 1, cv::NORM_MINMAX);

    std::vector<float> res(w * h);
    std::memcpy(res.data(), dist.data, res.size() * sizeof(float));
    return res;
}

// Compute chrominance at native source resolution.
std::vector<float> MaskGenerationDialog::getComponentChrominance() const
{
    const int w = m_sourceImage.width();
    const int h = m_sourceImage.height();
    cv::Mat src(h, w, CV_32FC3, const_cast<float*>(m_sourceImage.data().data()));

    cv::Mat ycrcb;
    cv::cvtColor(src, ycrcb, cv::COLOR_RGB2YCrCb);

    std::vector<cv::Mat> chans;
    cv::split(ycrcb, chans);

    const cv::Scalar meanCr = cv::mean(chans[1]);
    const cv::Scalar meanCb = cv::mean(chans[2]);

    cv::Mat diffCr = chans[1] - meanCr;
    cv::Mat diffCb = chans[2] - meanCb;
    cv::Mat dist;
    cv::sqrt(diffCr.mul(diffCr) + diffCb.mul(diffCb), dist);
    cv::normalize(dist, dist, 0, 1, cv::NORM_MINMAX);

    std::vector<float> res(w * h);
    std::memcpy(res.data(), dist.data, res.size() * sizeof(float));
    return res;
}

// Detect stars at full resolution then generate the star mask at the requested size.
std::vector<float> MaskGenerationDialog::getStarMask(int w, int h) const
{
    auto stars = m_sourceImage.detectStarsHQ();
    return m_sourceImage.generateHQStarMask(stars, w, h);
}

std::vector<float> MaskGenerationDialog::getStarMask() const
{
    return getStarMask(m_sourceImage.width(), m_sourceImage.height());
}

// Build a range-selection mask at full source resolution using current slider values.
std::vector<float> MaskGenerationDialog::generateRangeMask([[maybe_unused]] const std::vector<float>& base)
{
    std::vector<float> comp = getComponentLightness();

    const float L    = m_lowerSl->value() / 100.0f;
    const float U    = m_upperSl->value() / 100.0f;
    const float fuzz = m_fuzzSl->value()  / 100.0f;
    const int smooth = m_smoothSl->value();
    const bool screen = m_screenCb->isChecked();
    const bool inv    = m_invertCb->isChecked();

    const int          size = static_cast<int>(comp.size());
    std::vector<float> m(size, 0.0f);

    #pragma omp parallel for
    for (int i = 0; i < size; ++i) {
        const float v = comp[i];
        float val = 0.0f;

        if (v >= L && v <= U) {
            val = 1.0f;
        } else if (fuzz > 1e-6f) {
            if      (v >= L - fuzz && v < L) val = (v - (L - fuzz)) / fuzz;
            else if (v >  U        && v <= U + fuzz) val = ((U + fuzz) - v) / fuzz;
        }

        val = std::clamp(val, 0.0f, 1.0f);
        if (screen) val *= v;
        m[i] = val;
    }

    if (smooth > 0) {
        const float sigma = smooth / 3.0f;
        cv::Mat mat(m_sourceImage.height(), m_sourceImage.width(), CV_32FC1, m.data());
        cv::GaussianBlur(mat, mat, cv::Size(0, 0), sigma);
    }

    if (inv)
        for (float& v : m) v = 1.0f - v;

    return m;
}

// Compute a hue-based colour mask with optional angular fuzziness.
// Pixels with saturation below 0.1 are excluded as effectively neutral.
std::vector<float> MaskGenerationDialog::getColorMask(const QString& color,
                                                       int w, int h,
                                                       float fuzzDeg) const
{
    if (m_sourceImage.channels() < 3)
        return std::vector<float>(w * h, 0.0f);

    // Build an 8-bit RGB matrix at the requested resolution
    cv::Mat rgb8(h, w, CV_8UC3);

    if (w == m_sourceImage.width() && h == m_sourceImage.height()) {
        const std::vector<float>& data = m_sourceImage.data();
        #pragma omp parallel for
        for (int i = 0; i < h * w; ++i) {
            rgb8.at<cv::Vec3b>(i)[0] = static_cast<uchar>(std::clamp(data[i*3+0] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[1] = static_cast<uchar>(std::clamp(data[i*3+1] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[2] = static_cast<uchar>(std::clamp(data[i*3+2] * 255.0f, 0.0f, 255.0f));
        }
    } else {
        cv::Mat fullSrc(m_sourceImage.height(), m_sourceImage.width(), CV_32FC3,
                        const_cast<float*>(m_sourceImage.data().data()));
        cv::Mat smallSrc;
        cv::resize(fullSrc, smallSrc, cv::Size(w, h), 0, 0, cv::INTER_AREA);
        #pragma omp parallel for
        for (int i = 0; i < h * w; ++i) {
            const float* p = reinterpret_cast<float*>(smallSrc.data) + i * 3;
            rgb8.at<cv::Vec3b>(i)[0] = static_cast<uchar>(std::clamp(p[0] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[1] = static_cast<uchar>(std::clamp(p[1] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[2] = static_cast<uchar>(std::clamp(p[2] * 255.0f, 0.0f, 255.0f));
        }
    }

    cv::Mat hls;
    cv::cvtColor(rgb8, hls, cv::COLOR_RGB2HLS);

    // Hue ranges in degrees [0, 360].
    // Blue is narrowed to 195-255; Violet occupies 255-300; Magenta from 300.
    struct Range { float lo; float hi; };
    std::vector<Range> ranges;

    if      (color == "Red")     ranges = {{ 0, 15}, {345, 360}};
    else if (color == "Orange")  ranges = {{15, 45}};
    else if (color == "Yellow")  ranges = {{45, 75}};
    else if (color == "Green")   ranges = {{75, 165}};
    else if (color == "Cyan")    ranges = {{165, 195}};
    else if (color == "Blue")    ranges = {{195, 255}};
    else if (color == "Violet")  ranges = {{255, 300}};
    else if (color == "Magenta") ranges = {{300, 345}};
    else return std::vector<float>(w * h, 0.0f);

    // Returns 1.0 inside the hue range; linearly falls to 0 at ±fuzzDeg outside.
    // Uses circular angular distance to handle the 0/360 wrap correctly.
    auto hueFuzzyVal = [](float H, float lo, float hi, float fuzz) -> float {
        if (H >= lo && H <= hi) return 1.0f;
        if (fuzz < 1e-3f) return 0.0f;
        auto circDist = [](float a, float b) {
            const float d = std::fabs(a - b);
            return std::min(d, 360.0f - d);
        };
        const float dist = std::min(circDist(H, lo), circDist(H, hi));
        return (dist < fuzz) ? 1.0f - dist / fuzz : 0.0f;
    };

    std::vector<float> mask(w * h, 0.0f);

    #pragma omp parallel for
    for (int i = 0; i < h * w; ++i) {
        const float hue = (hls.at<cv::Vec3b>(i)[0] / 180.0f) * 360.0f;
        const float sat =  hls.at<cv::Vec3b>(i)[2] / 255.0f;

        if (sat < 0.1f) continue;

        float maxVal = 0.0f;
        for (const auto& r : ranges)
            maxVal = std::max(maxVal, hueFuzzyVal(hue, r.lo, r.hi, fuzzDeg));

        mask[i] = std::clamp(maxVal, 0.0f, 1.0f);
    }

    return mask;
}

std::vector<float> MaskGenerationDialog::getColorMask(const QString& color, float fuzzDeg) const
{
    return getColorMask(color, m_sourceImage.width(), m_sourceImage.height(), fuzzDeg);
}

// =============================================================================
// Live Preview
// =============================================================================

// Refresh the live preview dialog if it is currently visible.
// Uses the downsampled resolution for responsiveness.
void MaskGenerationDialog::updateLivePreview()
{
    if (!m_livePreview || !m_livePreview->isVisible()) return;

    const MaskLayer m = (m_smallW > 0 && m_smallH > 0)
        ? getGeneratedMask(m_smallW, m_smallH)
        : getGeneratedMask();

    const auto mode = static_cast<ImageBuffer::DisplayMode>(
        m_previewStretchCombo->currentData().toInt());

    m_livePreview->updateMask(m.data, m.width, m.height, mode, false, false);
}

// Open (or re-use) the live preview dialog and populate it with the current mask.
void MaskGenerationDialog::generatePreview()
{
    if (!m_livePreview) {
        m_livePreview = new LivePreviewDialog(m_sourceImage.width(),
                                              m_sourceImage.height(), this);
        connect(m_livePreview, &QDialog::finished, []() {});
    }

    const MaskLayer m = (m_smallW > 0 && m_smallH > 0)
        ? getGeneratedMask(m_smallW, m_smallH)
        : getGeneratedMask();

    if (m.data.empty() || m.width == 0 || m.height == 0) {
        QMessageBox::warning(this, tr("Preview"),
            tr("No mask data generated. Please draw shapes or select a mask type."));
        return;
    }

    const auto mode = static_cast<ImageBuffer::DisplayMode>(
        m_previewStretchCombo->currentData().toInt());

    m_livePreview->updateMask(m.data, m.width, m.height, mode, false, false);

    if (!m_livePreview->isVisible())
        m_livePreview->show();

    m_livePreview->raise();
    m_livePreview->activateWindow();
}