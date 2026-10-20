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

#ifndef STRETCHDIALOG_H
#define STRETCHDIALOG_H

// =============================================================================
// StretchDialog.h
// Statistical stretch dialog with configurable target median, black point,
// curves, HDR compression, luminance-only mode, and high-range rescaling.
// Provides live preview and undo on apply.
// =============================================================================

#include "DialogBase.h"
#include "ImageBuffer.h"

#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QPointer>

class ImageViewer;

class StretchDialog : public DialogBase {
    Q_OBJECT

public:
    explicit StretchDialog(QWidget* parent = nullptr);
    ~StretchDialog();

    void setViewer(ImageViewer* v);
    ImageViewer* viewer() const { return m_viewer; }

    /// Programmatically trigger a preview update.
    void triggerPreview();

signals:
    /// Emitted after a successful apply with a human-readable summary.
    void applied(const QString& msg);

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onApply();
    void onPreview();
    void onHdrToggled(bool enabled);
    void onHighRangeToggled(bool enabled);
    void onLumaOnlyToggled(bool enabled);
    void updatePreview();

    ImageBuffer::StretchParams getParams() const;

private:
    void setupUI();
    void setupConnections();

    // Viewer and state
    QPointer<ImageViewer>    m_viewer = nullptr;
    ImageBuffer              m_originalBuffer;
    bool                     m_applied = false;
    ImageBuffer::DisplayMode m_originalDisplayMode   = ImageBuffer::Display_Linear;
    bool                     m_originalDisplayLinked  = true;

    // --- Basic statistical parameters ---
    QDoubleSpinBox* m_targetSpin;
    QCheckBox*      m_linkedCheck;
    QCheckBox*      m_normalizeCheck;

    // --- Black point controls ---
    QDoubleSpinBox* m_blackpointSigmaSpin;
    QCheckBox*      m_noBlackClipCheck;

    // --- Curves group ---
    QGroupBox*      m_curvesGroup;
    QCheckBox*      m_curvesCheck;
    QDoubleSpinBox* m_boostSpin;

    // --- HDR compression group ---
    QGroupBox*      m_hdrGroup;
    QDoubleSpinBox* m_hdrAmountSpin;
    QDoubleSpinBox* m_hdrKneeSpin;

    // --- Luminance-only mode group ---
    QGroupBox*  m_lumaOnlyGroup;
    QComboBox*  m_lumaModeCombo;

    // --- High-range rescaling group ---
    QGroupBox*      m_highRangeGroup;
    QDoubleSpinBox* m_hrPedestalSpin;
    QDoubleSpinBox* m_hrSoftCeilSpin;
    QDoubleSpinBox* m_hrHardCeilSpin;
    QDoubleSpinBox* m_hrSoftclipSpin;
};

#endif // STRETCHDIALOG_H