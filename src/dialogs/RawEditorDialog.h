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
 * This file is inspired by or incorporates code/design from RapidRAW
 * (hosted at https://github.com/CyberTimon/RapidRAW), which is
 * licensed under AGPL-3.0.
 * Inspired by the work of Timon Käch.
 * Modified by Ft2801 in 2026.
 * -------------------------------------------------------------------
 */

#ifndef RAWEDITORDIALOG_H
#define RAWEDITORDIALOG_H

/**
 * @file RawEditorDialog.h
 * @brief Non-destructive light and color editor dialog.
 *
 * Provides a comprehensive set of photographic adjustments (exposure,
 * contrast, HSL, color grading, vignette, etc.) with real-time preview
 * on a downsampled buffer. Adjustments are applied at full resolution
 * only when the user clicks Apply.
 *
 * Architecture:
 *  - RawEditorPreviewWorker: background thread for preview rendering.
 *  - RawEditorCanvas: zoomable/pannable image display widget.
 *  - RawEditorControl: slider + spinbox pair linked to a parameter.
 *  - RawEditorDialog: main dialog coordinating all components.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "DialogBase.h"
#include "../algos/RawEditorProcessor.h"
#include "../ImageBuffer.h"

#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QThread>
#include <QMutex>
#include <QVector>
#include <QScrollArea>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QKeyEvent>

class ImageViewer;

// ============================================================================
// Preview Worker
// ============================================================================

/**
 * @class RawEditorPreviewWorker
 * @brief Background thread for rendering editor preview images.
 *
 * Caches a downsampled copy of the source buffer (set once from the main
 * thread), then re-applies the latest parameters on each run() invocation
 * to produce a preview QImage.
 */
class RawEditorPreviewWorker : public QThread {
    Q_OBJECT

public:
    explicit RawEditorPreviewWorker(QObject* parent = nullptr);

    /**
     * @brief Cache the source image (must be called from the main thread).
     *
     * Optionally downsamples the source to maxDim pixels on its largest
     * dimension for responsive preview rendering.
     */
    void setSource(const ImageBuffer& source, int maxDim);

    /**
     * @brief Queue rendering with updated parameters.
     *
     * Must be called from the main thread before start().
     */
    void requestPreview(const RawEditor::Params& params);

    void run() override;

signals:
    /** @brief Emitted when the preview image is ready. */
    void previewReady(const QImage& image);

private:
    ImageBuffer        m_sourceBuffer;  ///< Cached (downsampled) source
    RawEditor::Params  m_params;
    QMutex             m_mutex;
};

// ============================================================================
// Canvas Widget
// ============================================================================

/**
 * @class RawEditorCanvas
 * @brief Zoomable and pannable image display widget.
 *
 * Supports mouse-wheel zoom, click-drag panning, and toggling between
 * the edited preview and the original image for comparison.
 */
class RawEditorCanvas : public QWidget {
    Q_OBJECT

public:
    explicit RawEditorCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void setOriginalImage(const QImage& img);
    void fitToView();
    void zoomIn();
    void zoomOut();
    void setShowOriginal(bool show);

signals:
    void zoomChanged(float zoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /** @brief Apply a zoom factor centered on a viewport position. */
    void zoomToPoint(float factor, QPointF viewPos);

    QImage  m_image;
    QImage  m_originalImage;

    // Shared zoom/pan state (synchronized between original and preview)
    float   m_zoom = 1.0f;
    QPointF m_panOffset;

    QPointF m_lastMousePos;
    bool    m_dragging     = false;
    bool    m_showOriginal = false;
    bool    m_fitOnNext    = true;
};

// ============================================================================
// Slider + SpinBox Control Descriptor
// ============================================================================

/**
 * @brief Describes a linked slider/spinbox control bound to a float parameter.
 */
struct RawEditorControl {
    QSlider*        slider      = nullptr;
    QDoubleSpinBox* spinBox     = nullptr;
    QLabel*         label       = nullptr;
    float*          paramPtr    = nullptr;  ///< Points into RawEditor::Params
    float           minVal      = 0;
    float           maxVal      = 1;
    float           defaultVal  = 0;
    int             sliderSteps = 200;
};

// ============================================================================
// Main Dialog
// ============================================================================

/**
 * @class RawEditorDialog
 * @brief Non-destructive image editor with real-time preview.
 *
 * Coordinates the preview worker, canvas, and control panel. Supports
 * undo/redo of parameter changes and comparison with the original image.
 */
class RawEditorDialog : public DialogBase {
    Q_OBJECT

public:
    explicit RawEditorDialog(ImageViewer* viewer, QWidget* parent = nullptr);
    ~RawEditorDialog();

private slots:
    void onPreviewReady(const QImage& img);
    void onParamChanged();
    void onApply();
    void onReset();
    void onUndo();
    void onRedo();
    void comparePressed();
    void compareReleased();

private:
    // -- UI construction --
    void     buildUI();
    QWidget* buildControlPanel();
    QWidget* buildSection(
        const QString& title,
        const std::vector<std::tuple<QString, float*, float, float, float, int>>& controls);
    QWidget* buildHSLSection();
    QWidget* buildColorGradingSection();
    QWidget* buildToolbar();

    // -- Control management --
    RawEditorControl createControl(const QString& name, float* param,
                                   float minVal, float maxVal, float defVal,
                                   int steps, QWidget* parent);
    void connectControl(RawEditorControl& ctrl);
    void updateControlsFromParams();

    // -- Preview pipeline --
    void requestPreview();

    // -- Undo/redo history --
    void pushHistory();

    // -- Keyboard shortcuts --
    void keyPressEvent(QKeyEvent* event) override;

    // -- Core references --
    ImageViewer*            m_viewer  = nullptr;
    RawEditorCanvas*        m_canvas  = nullptr;
    RawEditorPreviewWorker* m_worker  = nullptr;
    RawEditor::Params       m_params;
    QImage                  m_originalPreview;

    // -- Undo/redo state --
    QVector<RawEditor::Params> m_history;
    int                        m_historyIndex = -1;
    QPushButton*               m_undoBtn      = nullptr;
    QPushButton*               m_redoBtn      = nullptr;

    // -- All controls for bulk sync operations --
    QVector<RawEditorControl>  m_controls;

    // -- Render queue flag --
    bool m_previewQueued = false;

    // -- Flags to prevent recursive signal handling --
    bool m_inUndoRedo    = false;
    bool m_blockSignals  = false;
};

#endif // RAWEDITORDIALOG_H