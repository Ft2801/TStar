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

#ifndef SELECTIVECOLORDIALOG_H
#define SELECTIVECOLORDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"

#include <QImage>
#include <vector>

class QLabel;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QSlider;
class MainWindowCallbacks;

/**
 * @brief Dialog for hue-selective color correction.
 *
 * Allows the user to isolate a range of hues using a mask computed from
 * the source image's HSV representation, then apply independent adjustments
 * to CMY, RGB, luminance, saturation, and contrast within that masked region.
 * A real-time preview is displayed via a QGraphicsView.
 */
class SelectiveColorDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit SelectiveColorDialog(QWidget* parent = nullptr);
    ~SelectiveColorDialog();

    /**
     * @brief Replaces the source image used for mask computation and preview.
     * @param img New source image buffer.
     */
    void setSource(const ImageBuffer& img);

private slots:
    void onPresetChanged(int index);
    void updateMask();
    void updatePreview();
    void onApply();
    void onReset();

private:
    void setupUi();

    /**
     * @brief Applies all active slider adjustments to the source image,
     *        weighted by the provided per-pixel hue mask.
     * @param src  Source image buffer.
     * @param mask Per-pixel weight mask in the range [0.0, 1.0].
     * @return     Adjusted image buffer.
     */
    ImageBuffer applyAdjustments(const ImageBuffer& src, const std::vector<float>& mask);

    /**
     * @brief Computes a per-pixel hue selection mask from the source image
     *        based on the current hue range, smoothness, and chroma settings.
     * @param src Source image buffer.
     * @return    Per-pixel mask values in the range [0.0, 1.0].
     */
    std::vector<float> computeHueMask(const ImageBuffer& src);

    // Image data
    ImageBuffer         m_sourceImage;
    ImageBuffer         m_previewImage;
    std::vector<float>  m_mask;

    /**
     * @brief Describes a named hue range preset.
     */
    struct HuePreset
    {
        QString name;
        float   hueStart; ///< Start of the hue range in degrees [0, 360].
        float   hueEnd;   ///< End of the hue range in degrees [0, 360].
    };
    std::vector<HuePreset> m_presets;

    // Mask controls
    QComboBox*      m_presetCombo;
    QDoubleSpinBox* m_hueStartSpin;
    QDoubleSpinBox* m_hueEndSpin;
    QDoubleSpinBox* m_smoothnessSpin;
    QDoubleSpinBox* m_minChromaSpin;
    QDoubleSpinBox* m_intensitySpin;
    QCheckBox*      m_invertCheck;
    QCheckBox*      m_showMaskCheck;

    // CMY adjustment sliders
    QSlider* m_cyanSlider;
    QSlider* m_magentaSlider;
    QSlider* m_yellowSlider;

    // RGB adjustment sliders
    QSlider* m_redSlider;
    QSlider* m_greenSlider;
    QSlider* m_blueSlider;

    // Luminance / Saturation / Contrast sliders
    QSlider* m_luminanceSlider;
    QSlider* m_saturationSlider;
    QSlider* m_contrastSlider;

    // Preview panel
    QGraphicsView*      m_view;
    QGraphicsScene*     m_scene;
    QGraphicsPixmapItem* m_pixmapItem;

    bool m_updatingPreset; ///< Guard flag to prevent recursive preset change signals.
};

#endif // SELECTIVECOLORDIALOG_H