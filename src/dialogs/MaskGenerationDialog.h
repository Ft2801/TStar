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

#ifndef MASKGENERATIONDIALOG_H
#define MASKGENERATIONDIALOG_H

#include <QDialog>
#include <QImage>
#include <vector>

#include "core/MaskLayer.h"
#include "../ImageBuffer.h"
#include "DialogBase.h"

class QComboBox;
class QSlider;
class QLabel;
class QGroupBox;
class QCheckBox;
class QPushButton;
class QVBoxLayout;
class MaskCanvas;
class LivePreviewDialog;

// =============================================================================
// MaskGenerationDialog
//
// Provides an interactive UI for defining a mask region (via freehand polygon,
// ellipse, or full-image selection) and computing a float mask layer from it.
// Multiple mask types are supported: Binary, Range Selection, Lightness,
// Chrominance, Star Mask, and per-colour masks.
// =============================================================================
class MaskGenerationDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit MaskGenerationDialog(const ImageBuffer& image, QWidget* parent = nullptr);

    // Generate and return the mask at the requested resolution.
    // Pass (0, 0) to use the source image's native resolution.
    MaskLayer getGeneratedMask(int requestedW = 0, int requestedH = 0) const;

private slots:
    void onTypeChanged(const QString& type);
    void updateLivePreview();
    void generatePreview();
    void clearShapes();
    void setMode(const QString& mode);

private:
    void setupUI();

    // Generate the geometric base mask from the canvas shapes
    std::vector<float> generateBaseMask();

    // Generate a range-selection mask from the current slider values
    std::vector<float> generateRangeMask(const std::vector<float>& base);

    // --- Canvas & core data ---
    MaskCanvas*    m_canvas;
    const ImageBuffer& m_sourceImage;
    MaskLayer      m_resultLayer;
    LivePreviewDialog* m_livePreview;

    // --- Mask type controls ---
    QComboBox* m_typeCombo;
    QSlider*   m_blurSlider;
    QLabel*    m_blurLabel;

    // --- Range selection group ---
    QGroupBox* m_rangeGroup;
    QSlider*   m_lowerSl;
    QSlider*   m_upperSl;
    QSlider*   m_fuzzSl;
    QSlider*   m_smoothSl;
    QLabel*    m_lowerLbl;
    QLabel*    m_upperLbl;
    QLabel*    m_fuzzLbl;
    QLabel*    m_smoothLbl;
    QCheckBox* m_linkCb;
    QCheckBox* m_screenCb;
    QCheckBox* m_lightCb;
    QCheckBox* m_invertCb;

    // --- Color mask group ---
    QGroupBox* m_colorGroup;
    QSlider*   m_colorFuzzSl;
    QLabel*    m_colorFuzzLbl;

    // --- Preview visualization controls ---
    QComboBox* m_previewStretchCombo;
    QComboBox* m_previewSizeCombo;
    int        m_maxPreviewDim = 1024;

    // --- Mode toolbar buttons ---
    QPushButton* m_freeBtn;
    QPushButton* m_ellipseBtn;
    QPushButton* m_selectBtn;
    QPushButton* m_moveBtn;

    // --- Downsampled luma cache for fast preview ---
    std::vector<float> m_smallLuma;
    int m_smallW = 0;
    int m_smallH = 0;

    // --- Image component helpers ---
    std::vector<float> getComponentLightness()  const;
    std::vector<float> getComponentChrominance() const;
    std::vector<float> getComponentHue()         const;
    std::vector<float> getStarMask()             const;
    std::vector<float> getStarMask(int w, int h) const;
    std::vector<float> getChrominance(int w, int h) const;
    std::vector<float> getLightness  (int w, int h) const;
    std::vector<float> getColorMask(const QString& color,
                                    float fuzzDeg = 0.0f) const;
    std::vector<float> getColorMask(const QString& color,
                                    int w, int h,
                                    float fuzzDeg = 0.0f) const;
};

#endif // MASKGENERATIONDIALOG_H