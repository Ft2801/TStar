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

#include "CosmicClarityDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLibrary>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

// ============================================================================
// GPU detection helper
// ============================================================================

/**
 * @brief Checks at runtime whether a supported GPU compute library is available.
 *
 * Probes for CUDA (NVIDIA) via nvcuda and DirectML (AMD/Intel) via DirectML.
 * Both checks are non-destructive - the libraries are loaded and immediately
 * released if found; failure is silently ignored.
 *
 * @return True if at least one GPU compute provider is available.
 */
static bool detectGpuAvailable()
{
    const bool hasCuda     = QLibrary("nvcuda").load();
    const bool hasDirectML = QLibrary("DirectML").load();
    return hasCuda || hasDirectML;
}

// ============================================================================
// Construction
// ============================================================================

CosmicClarityDialog::CosmicClarityDialog(QWidget* parent)
    : DialogBase(parent, tr("Cosmic Clarity"), 500, 500)
{
    setWindowIcon(QIcon(":/images/Logo.png"));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QGroupBox*  grp  = new QGroupBox(tr("Parameters"), this);
    QGridLayout* grid = new QGridLayout(grp);

    // --- Mode ---
    grid->addWidget(new QLabel(tr("Mode:"), this), 0, 0);
    m_cmbMode = new QComboBox(this);
    m_cmbMode->addItem(tr("Sharpen"),          "Sharpen");
    m_cmbMode->addItem(tr("Denoise"),          "Denoise");
    m_cmbMode->addItem(tr("Both"),             "Both");
    m_cmbMode->addItem(tr("Super Resolution"), "Super Resolution");
    connect(m_cmbMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CosmicClarityDialog::updateUI);
    grid->addWidget(m_cmbMode, 0, 1, 1, 2);

    // --- GPU selection - default based on hardware detection ---
    grid->addWidget(new QLabel(tr("Use GPU:"), this), 1, 0);
    m_cmbGpu = new QComboBox(this);
    m_cmbGpu->addItem(tr("Yes"), true);
    m_cmbGpu->addItem(tr("No"),  false);
    m_cmbGpu->setCurrentIndex(detectGpuAvailable() ? 0 : 1);
    grid->addWidget(m_cmbGpu, 1, 1);

    // --- Sharpen parameters ---
    m_lblShMode = new QLabel(tr("Sharpening Mode:"), this);
    m_cmbShMode = new QComboBox(this);
    m_cmbShMode->addItem(tr("Both"),            "Both");
    m_cmbShMode->addItem(tr("Stellar Only"),    "Stellar Only");
    m_cmbShMode->addItem(tr("Non-Stellar Only"),"Non-Stellar Only");
    grid->addWidget(m_lblShMode, 2, 0);
    grid->addWidget(m_cmbShMode, 2, 1);

    m_chkShSep  = new QCheckBox(tr("Sharpen Channels Separately"), this);
    m_chkAutoPsf = new QCheckBox(tr("Auto Detect PSF"), this);
    m_chkAutoPsf->setChecked(true);
    grid->addWidget(m_chkShSep,   3, 0);
    grid->addWidget(m_chkAutoPsf, 3, 1);

    m_lblPsf = new QLabel(tr("Non-Stellar PSF: 3.0"), this);
    m_sldPsf = new QSlider(Qt::Horizontal, this);
    m_sldPsf->setRange(10, 80);
    m_sldPsf->setValue(30);
    connect(m_sldPsf, &QSlider::valueChanged, [this](int v) {
        m_lblPsf->setText(tr("Non-Stellar PSF: %1").arg(v / 10.0, 0, 'f', 1));
    });
    grid->addWidget(m_lblPsf, 4, 0, 1, 3);
    grid->addWidget(m_sldPsf, 5, 0, 1, 3);

    m_lblStAmt = new QLabel(tr("Stellar Amount: 0.50"), this);
    m_sldStAmt = new QSlider(Qt::Horizontal, this);
    m_sldStAmt->setRange(0, 100);
    m_sldStAmt->setValue(50);
    connect(m_sldStAmt, &QSlider::valueChanged, [this](int v) {
        m_lblStAmt->setText(tr("Stellar Amount: %1").arg(v / 100.0, 0, 'f', 2));
    });
    grid->addWidget(m_lblStAmt, 6, 0, 1, 3);
    grid->addWidget(m_sldStAmt, 7, 0, 1, 3);

    m_lblNstAmt = new QLabel(tr("Non-Stellar Amount: 0.50"), this);
    m_sldNstAmt = new QSlider(Qt::Horizontal, this);
    m_sldNstAmt->setRange(0, 100);
    m_sldNstAmt->setValue(50);
    connect(m_sldNstAmt, &QSlider::valueChanged, [this](int v) {
        m_lblNstAmt->setText(tr("Non-Stellar Amount: %1").arg(v / 100.0, 0, 'f', 2));
    });
    grid->addWidget(m_lblNstAmt, 8, 0, 1, 3);
    grid->addWidget(m_sldNstAmt, 9, 0, 1, 3);

    // --- Denoise parameters ---
    m_lblDnLum = new QLabel(tr("Luminance Denoise: 0.50"), this);
    m_sldDnLum = new QSlider(Qt::Horizontal, this);
    m_sldDnLum->setRange(0, 100);
    m_sldDnLum->setValue(50);
    connect(m_sldDnLum, &QSlider::valueChanged, [this](int v) {
        m_lblDnLum->setText(tr("Luminance Denoise: %1").arg(v / 100.0, 0, 'f', 2));
    });
    grid->addWidget(m_lblDnLum, 10, 0, 1, 3);
    grid->addWidget(m_sldDnLum, 11, 0, 1, 3);

    m_lblDnCol = new QLabel(tr("Color Denoise: 0.50"), this);
    m_sldDnCol = new QSlider(Qt::Horizontal, this);
    m_sldDnCol->setRange(0, 100);
    m_sldDnCol->setValue(50);
    connect(m_sldDnCol, &QSlider::valueChanged, [this](int v) {
        m_lblDnCol->setText(tr("Color Denoise: %1").arg(v / 100.0, 0, 'f', 2));
    });
    grid->addWidget(m_lblDnCol, 12, 0, 1, 3);
    grid->addWidget(m_sldDnCol, 13, 0, 1, 3);

    m_lblDnMode = new QLabel(tr("Denoise Mode:"), this);
    m_cmbDnMode = new QComboBox(this);
    m_cmbDnMode->addItem(tr("full"),       "full");
    m_cmbDnMode->addItem(tr("luminance"),  "luminance");
    grid->addWidget(m_lblDnMode, 14, 0);
    grid->addWidget(m_cmbDnMode, 14, 1);

    m_chkDnSep = new QCheckBox(tr("Process RGB Separately"), this);
    grid->addWidget(m_chkDnSep, 15, 1);

    // --- Super Resolution parameters ---
    m_lblScale = new QLabel(tr("Scale:"), this);
    m_cmbScale = new QComboBox(this);
    m_cmbScale->addItems({ "2x", "3x", "4x" });
    grid->addWidget(m_lblScale, 16, 0);
    grid->addWidget(m_cmbScale, 16, 1);

    mainLayout->addWidget(grp);

    // --- Bottom bar ---
    QHBoxLayout* bottomLayout = new QHBoxLayout();

    QLabel* copyright = new QLabel("(C) 2026 SetiAstro", this);
    copyright->setStyleSheet("color: gray; font-size: 10px;");
    bottomLayout->addWidget(copyright);
    bottomLayout->addStretch();

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    QPushButton* okBtn     = new QPushButton(tr("OK"),     this);
    okBtn->setDefault(true);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);

    bottomLayout->addLayout(btnLayout);
    mainLayout->addLayout(bottomLayout);

    updateUI();
}

// ============================================================================
// Slots
// ============================================================================

void CosmicClarityDialog::updateUI()
{
    const int  idx    = m_cmbMode->currentIndex();
    const bool showSh = (idx == 0 || idx == 2);
    const bool showDn = (idx == 1 || idx == 2);
    const bool showSr = (idx == 3);

    // Sharpen controls
    m_lblShMode->setVisible(showSh);  m_cmbShMode->setVisible(showSh);
    m_chkShSep->setVisible(showSh);   m_chkAutoPsf->setVisible(showSh);
    m_lblPsf->setVisible(showSh);     m_sldPsf->setVisible(showSh);
    m_lblStAmt->setVisible(showSh);   m_sldStAmt->setVisible(showSh);
    m_lblNstAmt->setVisible(showSh);  m_sldNstAmt->setVisible(showSh);

    // Denoise controls
    m_lblDnLum->setVisible(showDn);   m_sldDnLum->setVisible(showDn);
    m_lblDnCol->setVisible(showDn);   m_sldDnCol->setVisible(showDn);
    m_lblDnMode->setVisible(showDn);  m_cmbDnMode->setVisible(showDn);
    m_chkDnSep->setVisible(showDn);

    // Super Resolution controls
    m_lblScale->setVisible(showSr);   m_cmbScale->setVisible(showSr);

    // GPU option is not relevant for Super Resolution.
    m_cmbGpu->setVisible(!showSr);
}

// ============================================================================
// Parameter extraction
// ============================================================================

CosmicClarityParams CosmicClarityDialog::getParams() const
{
    CosmicClarityParams p;

    const int idx = m_cmbMode->currentIndex();
    if      (idx == 0) p.mode = CosmicClarityParams::Mode_Sharpen;
    else if (idx == 1) p.mode = CosmicClarityParams::Mode_Denoise;
    else if (idx == 2) p.mode = CosmicClarityParams::Mode_Both;
    else               p.mode = CosmicClarityParams::Mode_SuperRes;

    p.useGpu = m_cmbGpu->currentData().toBool();

    p.sharpenMode              = m_cmbShMode->currentData().toString();
    p.separateChannelsSharpen  = m_chkShSep->isChecked();
    p.autoPSF                  = m_chkAutoPsf->isChecked();
    p.nonStellarPSF            = m_sldPsf->value()    / 10.0f;
    p.stellarAmount            = m_sldStAmt->value()  / 100.0f;
    p.nonStellarAmount         = m_sldNstAmt->value() / 100.0f;

    p.denoiseLum               = m_sldDnLum->value()  / 100.0f;
    p.denoiseColor             = m_sldDnCol->value()  / 100.0f;
    p.denoiseMode              = m_cmbDnMode->currentData().toString();
    p.separateChannelsDenoise  = m_chkDnSep->isChecked();

    p.scaleFactor              = m_cmbScale->currentText();

    return p;
}