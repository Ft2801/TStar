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

/**
 * @file PerfectPaletteDialog.cpp
 * @brief Narrowband palette composition dialog implementation.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "PerfectPaletteDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QPainter>
#include <QImage>
#include <QPixmap>
#include <QIcon>
#include <algorithm>

// ============================================================================
// Construction
// ============================================================================

PerfectPaletteDialog::PerfectPaletteDialog(QWidget* parent)
    : DialogBase(parent, tr("Perfect Palette Picker"), 1000, 650)
    , m_selectedPalette("SHO")
{
    m_mainWin = getCallbacks();
    createUI();
}

void PerfectPaletteDialog::setViewer(ImageViewer* v)
{
    m_viewer = v;
}

// ============================================================================
// UI construction
// ============================================================================

void PerfectPaletteDialog::createUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // ---- Left panel: controls ----
    QVBoxLayout* ctrlLayout = new QVBoxLayout();
    ctrlLayout->setContentsMargins(10, 10, 10, 10);

    ctrlLayout->addWidget(new QLabel(tr("<b>Narrowband Channels</b>")));

    // Channel load buttons with status labels
    auto addLoadBtn = [&](const QString& name, QLabel** lbl) {
        QPushButton* btn = new QPushButton(tr("Load %1...").arg(name));
        *lbl = new QLabel(tr("Not loaded"));
        (*lbl)->setStyleSheet("color: gray;");
        ctrlLayout->addWidget(btn);
        ctrlLayout->addWidget(*lbl);
        connect(btn, &QPushButton::clicked, this, [this, name]() {
            onLoadChannel(name);
        });
    };

    addLoadBtn("Ha",   &m_lblHa);
    addLoadBtn("OIII", &m_lblOiii);
    addLoadBtn("SII",  &m_lblSii);

    // Per-channel intensity sliders
    ctrlLayout->addSpacing(20);
    ctrlLayout->addWidget(new QLabel(tr("<b>Intensities</b>")));

    auto addSlider = [&](const QString& name, QSlider** sld, QLabel** val) {
        QHBoxLayout* hl = new QHBoxLayout();
        hl->addWidget(new QLabel(name + ":"));
        *sld = new QSlider(Qt::Horizontal);
        (*sld)->setRange(0, 200);
        (*sld)->setValue(100);
        *val = new QLabel("1.00");
        hl->addWidget(*sld);
        hl->addWidget(*val);
        ctrlLayout->addLayout(hl);
        connect(*sld, &QSlider::valueChanged,
                this, &PerfectPaletteDialog::onIntensityChanged);
    };

    addSlider("Ha",   &m_sliderHa,   &m_lblValHa);
    addSlider("OIII", &m_sliderOiii, &m_lblValOiii);
    addSlider("SII",  &m_sliderSii,  &m_lblValSii);

    // Stretch settings
    ctrlLayout->addSpacing(20);
    ctrlLayout->addWidget(new QLabel(tr("<b>Stretch Settings</b>")));

    m_chkAutoStretch = new QCheckBox(tr("Auto Stretch Channels"));
    m_chkAutoStretch->setChecked(true);
    m_chkAutoStretch->setToolTip(
        tr("Automatically stretch linear data to non-linear for preview/result."));
    ctrlLayout->addWidget(m_chkAutoStretch);

    QHBoxLayout* hlStretch = new QHBoxLayout();
    hlStretch->addWidget(new QLabel(tr("Strength:")));
    m_sliderStretch = new QSlider(Qt::Horizontal);
    m_sliderStretch->setRange(10, 50);  // Maps to 0.10 -- 0.50
    m_sliderStretch->setValue(25);       // Default 0.25
    m_lblStretchVal = new QLabel("0.25");
    hlStretch->addWidget(m_sliderStretch);
    hlStretch->addWidget(m_lblStretchVal);
    ctrlLayout->addLayout(hlStretch);

    connect(m_chkAutoStretch, &QCheckBox::toggled,
            this,             &PerfectPaletteDialog::onStretchChanged);
    connect(m_sliderStretch,  &QSlider::valueChanged,
            this,             &PerfectPaletteDialog::onStretchChanged);

    ctrlLayout->addStretch();

    // Apply button
    QPushButton* btnApply = new QPushButton(tr("Apply to New View"));
    btnApply->setFixedHeight(40);
    btnApply->setStyleSheet(
        "background-color: #2c3e50; color: white; font-weight: bold;");
    connect(btnApply, &QPushButton::clicked, this, &PerfectPaletteDialog::onApply);
    ctrlLayout->addWidget(btnApply);

    QLabel* copyright = new QLabel(tr("(C) 2026 SetiAstro"), this);
    copyright->setAlignment(Qt::AlignCenter);
    copyright->setStyleSheet("color: gray; font-size: 10px;");
    ctrlLayout->addWidget(copyright);

    mainLayout->addLayout(ctrlLayout, 1);

    // ---- Right panel: palette grid and preview ----
    QVBoxLayout* previewLayout = new QVBoxLayout();

    m_lblPreview = new QLabel();
    m_lblPreview->setAlignment(Qt::AlignCenter);
    m_lblPreview->setStyleSheet("background-color: black; border: 1px solid #333;");
    m_lblPreview->setMinimumSize(400, 300);
    previewLayout->addWidget(m_lblPreview, 3);

    // Palette selection grid
    QWidget* gridContainer = new QWidget();
    m_gridPalettes = new QGridLayout(gridContainer);
    m_gridPalettes->setSpacing(5);

    QStringList palettes = {
        "SHO", "HOO", "HSO", "HOS", "OSS",
        "OHH", "Realistic1", "Realistic2", "Foraxx"
    };

    int row = 0, col = 0;
    for (const QString& palName : palettes) {
        QPushButton* b = new QPushButton(palName);
        b->setFixedSize(90, 40);
        b->setCheckable(true);
        if (palName == "SHO")
            b->setChecked(true);
        m_gridPalettes->addWidget(b, row, col);

        PaletteThumb thumb;
        thumb.btn  = b;
        thumb.name = palName;
        m_thumbs.append(thumb);

        connect(b, &QPushButton::clicked, this, [this, palName]() {
            onPaletteSelected(palName);
        });

        col++;
        if (col > 2) { col = 0; row++; }
    }

    previewLayout->addWidget(gridContainer);
    mainLayout->addLayout(previewLayout, 3);
}

// ============================================================================
// Channel loading with nearest-neighbor downscaling for fast preview
// ============================================================================

void PerfectPaletteDialog::onLoadChannel(const QString& channel)
{
    if (!m_mainWin)
        return;

    // Gather all open image viewers
    QList<ImageViewer*> viewers =
        m_mainWin->getCurrentViewer()->window()->findChildren<ImageViewer*>();

    QStringList titles;
    QMap<QString, ImageViewer*> viewerMap;
    for (ImageViewer* v : viewers) {
        if (!v->windowTitle().isEmpty()) {
            titles << v->windowTitle();
            viewerMap[v->windowTitle()] = v;
        }
    }

    if (titles.isEmpty()) {
        QMessageBox::warning(this, tr("No Image"),
                             tr("Please select an image first."));
        return;
    }

    // Default to current viewer's title if available
    int initialIdx = 0;
    if (m_viewer && !m_viewer->windowTitle().isEmpty()) {
        initialIdx = titles.indexOf(m_viewer->windowTitle());
        if (initialIdx < 0) initialIdx = 0;
    }

    bool ok;
    QString item = QInputDialog::getItem(
        this, tr("Select View"),
        tr("Source for %1:").arg(channel),
        titles, initialIdx, false, &ok);

    if (!ok || item.isEmpty())
        return;

    ImageViewer* selectedViewer = viewerMap[item];

    // Helper: create a downscaled preview buffer (max 1024px) using nearest-neighbor
    auto makePreview = [](const ImageBuffer& src) -> ImageBuffer {
        float scale = 1024.0f / std::max(src.width(), src.height());
        if (scale >= 1.0f)
            return src;

        int newW = static_cast<int>(src.width()  * scale);
        int newH = static_cast<int>(src.height() * scale);
        ImageBuffer small;
        small.resize(newW, newH, src.channels());

        #pragma omp parallel for
        for (int y = 0; y < newH; ++y) {
            int sy = static_cast<int>(y / scale);
            for (int x = 0; x < newW; ++x) {
                int sx = static_cast<int>(x / scale);
                for (int c = 0; c < src.channels(); ++c) {
                    small.data()[(y * newW + x) * src.channels() + c] =
                        src.getPixelValue(sx, sy, c);
                }
            }
        }
        return small;
    };

    // Load full buffer and create preview for the selected channel
    if (channel == "Ha") {
        m_ha = selectedViewer->getBuffer();
        m_previewHa = makePreview(m_ha);
        m_lblHa->setText(item);
        m_lblHa->setStyleSheet("color: green;");
    } else if (channel == "OIII") {
        m_oiii = selectedViewer->getBuffer();
        m_previewOiii = makePreview(m_oiii);
        m_lblOiii->setText(item);
        m_lblOiii->setStyleSheet("color: green;");
    } else if (channel == "SII") {
        m_sii = selectedViewer->getBuffer();
        m_previewSii = makePreview(m_sii);
        m_lblSii->setText(item);
        m_lblSii->setStyleSheet("color: green;");
    }

    onCreatePalettes();
}

// ============================================================================
// Slider and palette callbacks
// ============================================================================

void PerfectPaletteDialog::onIntensityChanged()
{
    m_lblValHa->setText(  QString::number(m_sliderHa->value()   / 100.0, 'f', 2));
    m_lblValOiii->setText(QString::number(m_sliderOiii->value() / 100.0, 'f', 2));
    m_lblValSii->setText( QString::number(m_sliderSii->value()  / 100.0, 'f', 2));
    onCreatePalettes();
}

void PerfectPaletteDialog::onPaletteSelected(const QString& name)
{
    m_selectedPalette = name;
    for (auto& t : m_thumbs)
        t.btn->setChecked(t.name == name);
    onCreatePalettes();
}

void PerfectPaletteDialog::onStretchChanged()
{
    float val = m_sliderStretch->value() / 100.0f;
    m_lblStretchVal->setText(QString::number(val, 'f', 2));

    // Enable/disable strength slider based on auto-stretch checkbox
    m_sliderStretch->setEnabled(m_chkAutoStretch->isChecked());
    m_lblStretchVal->setEnabled(m_chkAutoStretch->isChecked());

    onCreatePalettes();
}

// ============================================================================
// Preview generation using downscaled buffers
// ============================================================================

void PerfectPaletteDialog::onCreatePalettes()
{
    if (!m_oiii.isValid() || (!m_ha.isValid() && !m_sii.isValid()))
        return;

    // Gather current parameters
    PerfectPaletteParams params;
    params.paletteName           = m_selectedPalette;
    params.haFactor              = m_sliderHa->value()      / 100.0f;
    params.oiiiFactor            = m_sliderOiii->value()     / 100.0f;
    params.siiFactor             = m_sliderSii->value()      / 100.0f;
    params.applyStatisticalStretch = m_chkAutoStretch->isChecked();
    params.targetMedian          = m_sliderStretch->value()  / 100.0f;

    ImageBuffer result;
    QString err;

    // Run the palette composition on downscaled preview buffers
    if (!m_runner.run(&m_previewHa, &m_previewOiii, &m_previewSii,
                      result, params, &err))
        return;

    // Convert the result buffer to a QImage for display
    int w = result.width();
    int h = result.height();
    QImage qimg(w, h, QImage::Format_RGB888);
    const float* data = result.data().data();

    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 3;
            qimg.setPixel(x, y, qRgb(
                std::min(255, std::max(0, static_cast<int>(data[idx + 0] * 255))),
                std::min(255, std::max(0, static_cast<int>(data[idx + 1] * 255))),
                std::min(255, std::max(0, static_cast<int>(data[idx + 2] * 255)))
            ));
        }
    }

    m_lblPreview->setPixmap(
        QPixmap::fromImage(qimg).scaled(
            m_lblPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// ============================================================================
// Apply at full resolution and create a new viewer window
// ============================================================================

void PerfectPaletteDialog::onApply()
{
    if (!m_oiii.isValid() || (!m_ha.isValid() && !m_sii.isValid()))
        return;

    PerfectPaletteParams params;
    params.paletteName           = m_selectedPalette;
    params.haFactor              = m_sliderHa->value()      / 100.0f;
    params.oiiiFactor            = m_sliderOiii->value()     / 100.0f;
    params.siiFactor             = m_sliderSii->value()      / 100.0f;
    params.applyStatisticalStretch = m_chkAutoStretch->isChecked();
    params.targetMedian          = m_sliderStretch->value()  / 100.0f;

    ImageBuffer result;
    QString err;

    if (m_runner.run(&m_ha, &m_oiii, &m_sii, result, params, &err)) {
        if (m_mainWin) {
            QString newName = "Palette_" + m_selectedPalette;
            m_mainWin->createResultWindow(result, newName);
            m_mainWin->logMessage(
                tr("Perfect Palette completed: %1").arg(newName), 1, true);
        }
        accept();
    } else {
        QMessageBox::critical(this, tr("Error"), err);
    }
}