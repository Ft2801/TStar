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

#ifndef CORRECTIONBRUSHDIALOG_H
#define CORRECTIONBRUSHDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QRunnable>
#include <QThreadPool>
#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;

// ============================================================================
// CorrectionMethod
// ============================================================================

/**
 * @brief Selects the algorithm used to fill a blemish area.
 */
enum class CorrectionMethod
{
    Standard,     ///< Median-based fill using nearby source patches.
    ContentAware  ///< Seamless-clone inpainting (higher quality, slower).
};

// ============================================================================
// CorrectionWorker
//
// Executes a single blemish-removal operation on a QThreadPool thread.
// Inherits QObject for signal support and QRunnable for pool scheduling.
// setAutoDelete(true) is set in the constructor so the pool owns the lifetime.
// ============================================================================
class CorrectionWorker : public QObject, public QRunnable
{
    Q_OBJECT

public:
    CorrectionWorker(const ImageBuffer&       img,
                     int                      x,
                     int                      y,
                     int                      radius,
                     float                    feather,
                     float                    opacity,
                     const std::vector<int>&  channels,
                     CorrectionMethod         method);

    void run() override;

signals:
    void finished(ImageBuffer result);

private:
    /**
     * @brief Executes the selected blemish-removal algorithm.
     *
     * Content-Aware: performs masked template matching in the surrounding
     * neighbourhood and blends the best matching patch using seamlessClone.
     * Falls back to Standard on boundary failures.
     *
     * Standard: locates the three nearest median-similar patches arranged at
     * 60-degree intervals and uses a weighted median blend.
     */
    ImageBuffer removeBlemish(const ImageBuffer&      img,
                              int                     x,
                              int                     y,
                              int                     radius,
                              float                   feather,
                              float                   opacity,
                              const std::vector<int>& channels,
                              CorrectionMethod        method);

    /**
     * @brief Computes the median pixel value within a circular region.
     */
    float medianCircle(const ImageBuffer&      img,
                       int                     cx,
                       int                     cy,
                       int                     radius,
                       const std::vector<int>& channels);

    // Input parameters
    ImageBuffer      m_image;
    int              m_x, m_y, m_radius;
    float            m_feather, m_opacity;
    std::vector<int> m_channels;
    CorrectionMethod m_method;
};

// ============================================================================
// CorrectionBrushDialog
//
// Interactive blemish-removal tool with a zoomable image canvas, undo/redo
// history, and a choice of correction algorithms.  Right-click pans the view;
// left-click applies the brush at the cursor position.
// ============================================================================
class CorrectionBrushDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit CorrectionBrushDialog(QWidget* parent = nullptr);
    ~CorrectionBrushDialog();

    /**
     * @brief Sets the source image for editing.
     *        If an active session with changes exists, the call is ignored to
     *        prevent loss of unsaved brush work when MDI focus changes.
     */
    void setSource(const ImageBuffer& img);

protected:
    bool eventFilter(QObject* src, QEvent* ev) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void onWorkerFinished(ImageBuffer result);
    void onApply();
    void onUndo();
    void onRedo();
    void onZoomIn();
    void onZoomOut();
    void onFit();
    void updateDisplay();

private:
    // --- Canvas ---
    QGraphicsView*        m_view;
    QGraphicsScene*       m_scene;
    QGraphicsPixmapItem*  m_pixItem;
    QGraphicsEllipseItem* m_cursorItem;

    // --- Brush controls ---
    QSlider*        m_radiusSlider;
    QSlider*        m_featherSlider;
    QSlider*        m_opacitySlider;
    QCheckBox*      m_autoStretchCheck;
    QDoubleSpinBox* m_targetMedianSpin;
    QCheckBox*      m_linkedCheck;
    class QComboBox* m_methodCombo;

    // --- Action buttons ---
    QPushButton* m_undoBtn;
    QPushButton* m_redoBtn;

    // --- Image data ---
    ImageBuffer              m_currentImage;
    std::vector<ImageBuffer> m_undoStack;
    std::vector<ImageBuffer> m_redoStack;

    // --- View state ---
    float   m_zoom       = 1.0f;
    QPointF m_lastPanPos;             ///< Last right-click position for pan delta.

    void setZoom(float z);

    /**
     * @brief Dispatches a blemish-removal operation at the given scene coordinate.
     *        Pushes the current image onto the undo stack and starts a worker.
     */
    void healAt(QPointF scenePos);

    // --- Flags ---
    bool m_busy      = false; ///< Prevents overlapping worker invocations.
    bool m_sourceSet = false; ///< True after the first valid setSource() call.
};

#endif // CORRECTIONBRUSHDIALOG_H