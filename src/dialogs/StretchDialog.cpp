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
// StretchDialog.cpp
// Implements the statistical stretch dialog. The stretch operates on the
// original buffer via performTrueStretch() and writes the result directly
// to the viewer for both preview and apply operations.
// =============================================================================

#include "StretchDialog.h"

#include <QFormLayout>
#include <QDialogButtonBox>
#include <QIcon>
#include <QScrollArea>
#include <QDebug>
#include <QCloseEvent>

#include "../ImageViewer.h"

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
StretchDialog::StretchDialog(QWidget* parent)
    : DialogBase(parent, tr("Statistical Stretch"), 600, 400)
{
    setMinimumWidth(450);
    setupUI();
    setupConnections();
}

// -----------------------------------------------------------------------------
// Destructor  --  Restores the original buffer and display state if the
// dialog is destroyed without applying.
// -----------------------------------------------------------------------------
StretchDialog::~StretchDialog()
{
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        m_viewer->clearPreviewLUT();
    }
}

// =============================================================================
// UI Construction
// =============================================================================

void StretchDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Scrollable container for all parameter groups
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget*     container     = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(container);
    contentLayout->setSpacing(12);

    // =========================================================================
    // Group 1: Basic statistical parameters and black point
    // =========================================================================
    QGroupBox*   basicGroup  = new QGroupBox(tr("Statistical Parameters"));
    QFormLayout* basicLayout = new QFormLayout(basicGroup);

    // Target median
    m_targetSpin = new QDoubleSpinBox(this);
    m_targetSpin->setRange(0.01, 0.99);
    m_targetSpin->setSingleStep(0.01);
    m_targetSpin->setValue(0.25);
    m_targetSpin->setToolTip(
        tr("Target median brightness after stretch (0.25 = typical astro)"));
    basicLayout->addRow(tr("Target Median:"), m_targetSpin);

    // Black point sigma
    m_blackpointSigmaSpin = new QDoubleSpinBox(this);
    m_blackpointSigmaSpin->setRange(0.0, 10.0);
    m_blackpointSigmaSpin->setSingleStep(0.5);
    m_blackpointSigmaSpin->setValue(5.0);
    m_blackpointSigmaSpin->setToolTip(
        tr("Sigma multiplier for black point calculation (higher = darker backgrounds)"));
    basicLayout->addRow(tr("Black Point Sigma:"), m_blackpointSigmaSpin);

    // Channel linking and clipping options
    QHBoxLayout* checkLayout = new QHBoxLayout();

    m_linkedCheck = new QCheckBox(tr("Link Channels"), this);
    m_linkedCheck->setChecked(true);

    m_noBlackClipCheck = new QCheckBox(tr("No Clipping"), this);
    m_noBlackClipCheck->setChecked(false);

    m_normalizeCheck = new QCheckBox(tr("Normalize"), this);
    m_normalizeCheck->setChecked(false);

    checkLayout->addWidget(m_linkedCheck);
    checkLayout->addWidget(m_noBlackClipCheck);
    checkLayout->addWidget(m_normalizeCheck);
    basicLayout->addRow("", checkLayout);

    contentLayout->addWidget(basicGroup);

    // =========================================================================
    // Group 2: Curves and HDR compression
    // =========================================================================
    m_curvesGroup = new QGroupBox(tr("Curves & HDR"));
    QFormLayout* enhanceLayout = new QFormLayout(m_curvesGroup);

    // S-curve boost
    m_curvesCheck = new QCheckBox(tr("Apply S-Curve Boost"), this);
    enhanceLayout->addRow("", m_curvesCheck);

    m_boostSpin = new QDoubleSpinBox(this);
    m_boostSpin->setRange(0.0, 1.0);
    m_boostSpin->setSingleStep(0.1);
    m_boostSpin->setValue(0.0);
    m_boostSpin->setEnabled(false);
    enhanceLayout->addRow(tr("Curve Strength:"), m_boostSpin);

    enhanceLayout->addRow(new QLabel("<hr>"));

    // HDR highlight compression sub-group
    m_hdrGroup = new QGroupBox(tr("HDR Highlight Compression"));
    m_hdrGroup->setCheckable(true);
    m_hdrGroup->setChecked(false);
    QFormLayout* hdrLayout = new QFormLayout(m_hdrGroup);

    m_hdrAmountSpin = new QDoubleSpinBox(this);
    m_hdrAmountSpin->setRange(0.0, 1.0);
    m_hdrAmountSpin->setSingleStep(0.1);
    m_hdrAmountSpin->setValue(0.5);
    hdrLayout->addRow(tr("Amount:"), m_hdrAmountSpin);

    m_hdrKneeSpin = new QDoubleSpinBox(this);
    m_hdrKneeSpin->setRange(0.5, 0.99);
    m_hdrKneeSpin->setSingleStep(0.05);
    m_hdrKneeSpin->setValue(0.75);
    hdrLayout->addRow(tr("Knee Point:"), m_hdrKneeSpin);

    enhanceLayout->addRow(m_hdrGroup);
    contentLayout->addWidget(m_curvesGroup);

    // =========================================================================
    // Group 3: Advanced modes
    // =========================================================================

    // Luminance-only mode
    m_lumaOnlyGroup = new QGroupBox(tr("Luminance-Only Mode"));
    m_lumaOnlyGroup->setCheckable(true);
    m_lumaOnlyGroup->setChecked(false);
    QFormLayout* lumaLayout = new QFormLayout(m_lumaOnlyGroup);

    m_lumaModeCombo = new QComboBox(this);
    m_lumaModeCombo->addItem(tr("Rec.709 (sRGB)"), 0);
    m_lumaModeCombo->addItem(tr("Rec.601 (SD)"),   1);
    m_lumaModeCombo->addItem(tr("Rec.2020 (HDR)"), 2);
    lumaLayout->addRow(tr("Formula:"), m_lumaModeCombo);

    contentLayout->addWidget(m_lumaOnlyGroup);

    // High-range rescaling (VeraLux)
    m_highRangeGroup = new QGroupBox(tr("High-Range Rescaling (VeraLux)"));
    m_highRangeGroup->setCheckable(true);
    m_highRangeGroup->setChecked(false);
    QFormLayout* hrLayout = new QFormLayout(m_highRangeGroup);

    m_hrPedestalSpin = new QDoubleSpinBox(this);
    m_hrPedestalSpin->setRange(0.0, 0.05);
    m_hrPedestalSpin->setSingleStep(0.001);
    m_hrPedestalSpin->setDecimals(3);
    m_hrPedestalSpin->setValue(0.001);
    hrLayout->addRow(tr("Pedestal:"), m_hrPedestalSpin);

    m_hrSoftCeilSpin = new QDoubleSpinBox(this);
    m_hrSoftCeilSpin->setRange(90.0, 99.9);
    m_hrSoftCeilSpin->setSingleStep(0.5);
    m_hrSoftCeilSpin->setValue(99.0);
    hrLayout->addRow(tr("Soft Ceiling %:"), m_hrSoftCeilSpin);

    m_hrHardCeilSpin = new QDoubleSpinBox(this);
    m_hrHardCeilSpin->setRange(99.0, 100.0);
    m_hrHardCeilSpin->setSingleStep(0.01);
    m_hrHardCeilSpin->setDecimals(2);
    m_hrHardCeilSpin->setValue(99.99);
    hrLayout->addRow(tr("Hard Ceiling %:"), m_hrHardCeilSpin);

    m_hrSoftclipSpin = new QDoubleSpinBox(this);
    m_hrSoftclipSpin->setRange(0.90, 0.999);
    m_hrSoftclipSpin->setSingleStep(0.01);
    m_hrSoftclipSpin->setDecimals(3);
    m_hrSoftclipSpin->setValue(0.98);
    hrLayout->addRow(tr("Softclip Start:"), m_hrSoftclipSpin);

    contentLayout->addWidget(m_highRangeGroup);

    scrollArea->setWidget(container);
    mainLayout->addWidget(scrollArea);

    // =========================================================================
    // Bottom button row
    // =========================================================================
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* previewBtn = new QPushButton(tr("Preview"), this);
    previewBtn->setFixedWidth(100);

    QLabel* copyLabel = new QLabel(tr("(C) 2026 SetiAstro"));
    copyLabel->setStyleSheet("color: #888; font-size: 10px; margin-left: 10px;");

    QPushButton* applyBtn = new QPushButton(tr("Apply"), this);
    applyBtn->setDefault(true);
    applyBtn->setFixedWidth(100);
    applyBtn->setStyleSheet(
        "QPushButton { background-color: #3a7d44; color: white; font-weight: bold; }");

    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setFixedWidth(100);

    btnLayout->addWidget(previewBtn);
    btnLayout->addWidget(copyLabel);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(applyBtn);

    connect(previewBtn, &QPushButton::clicked, this, &StretchDialog::onPreview);
    connect(applyBtn,   &QPushButton::clicked, this, &StretchDialog::onApply);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);

    mainLayout->addLayout(btnLayout);
}

// -----------------------------------------------------------------------------
// setupConnections  --  Wires toggle signals for dependent widget enable states.
// -----------------------------------------------------------------------------
void StretchDialog::setupConnections()
{
    connect(m_curvesCheck,    &QCheckBox::toggled,
            m_boostSpin,     &QWidget::setEnabled);
    connect(m_hdrGroup,       &QGroupBox::toggled,
            this,            &StretchDialog::onHdrToggled);
    connect(m_highRangeGroup, &QGroupBox::toggled,
            this,            &StretchDialog::onHighRangeToggled);
    connect(m_lumaOnlyGroup,  &QGroupBox::toggled,
            this,            &StretchDialog::onLumaOnlyToggled);
}

// =============================================================================
// Toggle handlers  --  Enable/disable dependent controls and refresh preview.
// =============================================================================

void StretchDialog::onHdrToggled(bool enabled)
{
    m_hdrAmountSpin->setEnabled(enabled);
    m_hdrKneeSpin->setEnabled(enabled);
    updatePreview();
}

void StretchDialog::onHighRangeToggled(bool enabled)
{
    m_hrPedestalSpin->setEnabled(enabled);
    m_hrSoftCeilSpin->setEnabled(enabled);
    m_hrHardCeilSpin->setEnabled(enabled);
    m_hrSoftclipSpin->setEnabled(enabled);
    updatePreview();
}

void StretchDialog::onLumaOnlyToggled(bool enabled)
{
    m_lumaModeCombo->setEnabled(enabled);
    // Luminance-only mode implicitly unlinks channels
    m_linkedCheck->setEnabled(!enabled);
    updatePreview();
}

// =============================================================================
// Event overrides
// =============================================================================

void StretchDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    qDebug() << "Statistical Stretch tool opened.";
}

void StretchDialog::reject()
{
    // Restore the original buffer and display mode on cancellation
    if (m_viewer) {
        m_viewer->clearPreviewLUT();
        if (m_originalBuffer.isValid() && !m_applied) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
            m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
        }
    }
    QDialog::reject();
}

void StretchDialog::closeEvent(QCloseEvent* event)
{
    // Handle the window close (X) button identically to reject
    if (!m_applied && m_viewer) {
        m_viewer->clearPreviewLUT();
        if (m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
            m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
        }
    }
    QDialog::closeEvent(event);
}

// =============================================================================
// Viewer management
// =============================================================================

void StretchDialog::setViewer(ImageViewer* v)
{
    if (m_viewer == v) return;

    // Restore the previous viewer if un-applied
    if (m_viewer && !m_applied) {
        if (m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
            m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
        }
        m_viewer->clearPreviewLUT();
    }

    m_viewer         = v;
    m_applied        = false;
    m_originalBuffer = ImageBuffer();

    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer       = m_viewer->getBuffer();
        m_originalDisplayMode  = m_viewer->getDisplayMode();
        m_originalDisplayLinked = m_viewer->isDisplayLinked();
        m_viewer->clearPreviewLUT();
    }
}

// =============================================================================
// Preview and apply logic
// =============================================================================

void StretchDialog::updatePreview()
{
    if (!m_viewer || !m_originalBuffer.isValid()) return;
    onPreview();
}

void StretchDialog::triggerPreview()
{
    onPreview();
}

// -----------------------------------------------------------------------------
// onPreview  --  Performs the stretch on a copy of the original buffer and
// displays the result. Uses direct float computation rather than a LUT to
// avoid banding artifacts on linear astronomical data (where a 65536-entry
// LUT yields only ~65 effective levels for pixel values in [0, 0.001]).
// -----------------------------------------------------------------------------
void StretchDialog::onPreview()
{
    if (!m_viewer || !m_originalBuffer.isValid()) return;

    ImageBuffer::StretchParams p = getParams();

    // Temporarily disable the annotation overlay during processing to
    // prevent concurrent-access crashes
    QWidget* overlay = m_viewer->findChild<QWidget*>(
        "AnnotationOverlay", Qt::FindDirectChildrenOnly);
    if (!overlay) overlay = m_viewer->findChild<QWidget*>();

    if (p.lumaOnly || p.highRange || p.hdrCompress) {
        if (overlay) overlay->setProperty("isProcessing", true);

        ImageBuffer temp = m_originalBuffer;
        temp.performTrueStretch(p);

        // Switch to linear display before setting the buffer so the
        // stretched preview is shown without auto-stretch interference
        m_viewer->setDisplayState(ImageBuffer::Display_Linear, m_originalDisplayLinked);
        m_viewer->setBuffer(temp, m_viewer->windowTitle(), true);

        if (overlay) {
            overlay->setProperty("isProcessing", false);
            overlay->update();
        }
    } else {
        // Standard path: direct float computation on a temporary buffer
        m_viewer->clearPreviewLUT();

        ImageBuffer temp = m_originalBuffer;
        temp.performTrueStretch(p);

        m_viewer->setDisplayState(ImageBuffer::Display_Linear, m_originalDisplayLinked);
        m_viewer->setBuffer(temp, m_viewer->windowTitle(), true);
    }
}

// -----------------------------------------------------------------------------
// onApply  --  Commits the stretch to the viewer buffer with undo support,
// then switches to linear display.
// -----------------------------------------------------------------------------
void StretchDialog::onApply()
{
    if (m_viewer && m_originalBuffer.isValid()) {
        // Temporarily disable overlay drawing
        QWidget* overlay = m_viewer->findChild<QWidget*>(
            "AnnotationOverlay", Qt::FindDirectChildrenOnly);
        if (!overlay) overlay = m_viewer->findChild<QWidget*>();
        if (overlay) overlay->setProperty("isProcessing", true);

        m_viewer->pushUndo(tr("Stretch applied"));
        m_viewer->clearPreviewLUT();

        // Restore the original, then stretch in-place
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

        ImageBuffer::StretchParams p = getParams();
        m_viewer->getBuffer().performTrueStretch(p);

        // Switch to linear display to prevent auto-stretch from being
        // re-applied to already-stretched data
        m_viewer->setDisplayState(ImageBuffer::Display_Linear, m_originalDisplayLinked);
        m_viewer->refreshDisplay();

        if (overlay) {
            overlay->setProperty("isProcessing", false);
            overlay->update();
        }

        m_applied = true;

        QString msg = tr("Statistical Stretch applied (M=%1, BP=%2?)")
                          .arg(p.targetMedian,    0, 'f', 2)
                          .arg(p.blackpointSigma, 0, 'f', 1);
        emit applied(msg);
    }
    accept();
}

// -----------------------------------------------------------------------------
// getParams  --  Collects all parameter values from the UI controls into
// an ImageBuffer::StretchParams structure.
// -----------------------------------------------------------------------------
ImageBuffer::StretchParams StretchDialog::getParams() const
{
    ImageBuffer::StretchParams p;

    p.targetMedian      = static_cast<float>(m_targetSpin->value());
    p.linked            = m_linkedCheck->isChecked();
    p.normalize         = m_normalizeCheck->isChecked();
    p.blackpointSigma   = static_cast<float>(m_blackpointSigmaSpin->value());
    p.noBlackClip       = m_noBlackClipCheck->isChecked();

    p.applyCurves       = m_curvesCheck->isChecked();
    p.curvesBoost       = static_cast<float>(m_boostSpin->value());

    p.hdrCompress       = m_hdrGroup->isChecked();
    p.hdrAmount         = static_cast<float>(m_hdrAmountSpin->value());
    p.hdrKnee           = static_cast<float>(m_hdrKneeSpin->value());

    p.lumaOnly          = m_lumaOnlyGroup->isChecked();
    p.lumaMode          = m_lumaModeCombo->currentData().toInt();

    p.highRange         = m_highRangeGroup->isChecked();
    p.hrPedestal        = static_cast<float>(m_hrPedestalSpin->value());
    p.hrSoftCeilPct     = static_cast<float>(m_hrSoftCeilSpin->value());
    p.hrHardCeilPct     = static_cast<float>(m_hrHardCeilSpin->value());
    p.hrSoftclipThreshold = static_cast<float>(m_hrSoftclipSpin->value());

    return p;
}