#ifndef RAWEDITORDIALOG_H
#define RAWEDITORDIALOG_H

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

class ImageViewer;
class ImageViewer;

// ═══════════════════════════════════════════════════════════════════════════════
// Preview Worker (background thread for processing)
// ═══════════════════════════════════════════════════════════════════════════════

class RawEditorPreviewWorker : public QThread {
    Q_OBJECT
public:
    explicit RawEditorPreviewWorker(QObject* parent = nullptr);

    // Must be called from the main thread.
    // Caches the source image once (optionally downsampled when maxDim > 0).
    void setSource(const ImageBuffer& source, int maxDim);

    // Must be called from the main thread.
    // Queues rendering with the latest parameters using the cached source.
    void requestPreview(const RawEditor::Params& params);

    void run() override;

signals:
    void previewReady(const QImage& image);

private:
    ImageBuffer       m_sourceBuffer;  // cached source for repeated previews
    RawEditor::Params m_params;
    QMutex            m_mutex;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Canvas (image display with zoom/pan)
// ═══════════════════════════════════════════════════════════════════════════════

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
    void zoomToPoint(float factor, QPointF viewPos);
    
    QImage m_image;
    QImage m_originalImage;
    
    // Shared zoom/pan state for both images (keeps them synchronized)
    float  m_zoom = 1.0f;
    QPointF m_panOffset;
    
    QPointF m_lastMousePos;
    bool    m_dragging = false;
    bool    m_showOriginal = false;
    bool    m_fitOnNext = true;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Slider+SpinBox Helper
// ═══════════════════════════════════════════════════════════════════════════════

struct RawEditorControl {
    QSlider*        slider;
    QDoubleSpinBox* spinBox;
    QLabel*         label;
    float*          paramPtr;   // Points into RawEditor::Params
    float           minVal;
    float           maxVal;
    float           defaultVal;
    int             sliderSteps;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Main Dialog
// ═══════════════════════════════════════════════════════════════════════════════

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
    // UI Setup
    void buildUI();
    QWidget* buildControlPanel();
    QWidget* buildSection(const QString& title, const std::vector<std::tuple<QString, float*, float, float, float, int>>& controls);
    QWidget* buildHSLSection();
    QWidget* buildColorGradingSection();
    // No curves section for v1 — uses default identity curves

    // Toolbar
    QWidget* buildToolbar();

    // Control management
    RawEditorControl createControl(const QString& name, float* param,
                                   float minVal, float maxVal, float defVal,
                                   int steps, QWidget* parent);
    void connectControl(RawEditorControl& ctrl);
    void updateControlsFromParams();

    // Preview
    void requestPreview();

    // History
    void pushHistory();

    // Keyboard shortcuts
    void keyPressEvent(QKeyEvent* event) override;

    ImageViewer*            m_viewer;
    RawEditorCanvas*        m_canvas;
    RawEditorPreviewWorker* m_worker;
    RawEditor::Params       m_params;
    QImage                  m_originalPreview; // Original image at preview resolution

    // Undo/Redo
    QVector<RawEditor::Params> m_history;
    int                        m_historyIndex = -1;
    QPushButton*               m_undoBtn = nullptr;
    QPushButton*               m_redoBtn = nullptr;

    // All controls for bulk operations
    QVector<RawEditorControl> m_controls;

    // If parameters change while worker is running, queue exactly one rerender
    // with latest values after current render completes.
    bool m_previewQueued = false;

    // Flag to track if we're in undo/redo mode to avoid creating duplicate history entries
    bool m_inUndoRedo = false;

    bool m_blockSignals = false;
};

#endif // RAWEDITORDIALOG_H
