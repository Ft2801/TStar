#ifndef CORRECTIONBRUSHDIALOG_H
#define CORRECTIONBRUSHDIALOG_H

#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsEllipseItem>
#include <QRunnable>
#include <QThreadPool>
#include <vector>
#include "../ImageBuffer.h"

class QSlider;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class MainWindow;

// Worker for background processing
enum class CorrectionMethod {
    Standard, // Median-based
    ContentAware // Telea inpainting (professional quality)
};

class CorrectionWorker : public QObject, public QRunnable {
    Q_OBJECT
public:
    CorrectionWorker(const ImageBuffer& img, int x, int y, int radius, float feather, float opacity, 
                     const std::vector<int>& channels, CorrectionMethod method);
    void run() override;

signals:
    void finished(ImageBuffer result);

private:
    ImageBuffer m_image;
    int m_x, m_y, m_radius;
    float m_feather, m_opacity;
    std::vector<int> m_channels;
    CorrectionMethod m_method;
    
    ImageBuffer removeBlemish(const ImageBuffer& img, int x, int y, int r, float f, float op, 
                              const std::vector<int>& chans, CorrectionMethod method);
    float medianCircle(const ImageBuffer& img, int cx, int cy, int radius, const std::vector<int>& channels);
};

class CorrectionBrushDialog : public QDialog {
    Q_OBJECT
public:
    explicit CorrectionBrushDialog(QWidget* parent = nullptr);
    ~CorrectionBrushDialog();
    
    void setSource(const ImageBuffer& img);

protected:
    bool eventFilter(QObject* src, QEvent* ev) override;

private slots:
    void onWorkerFinished(ImageBuffer result);
    void onApply();
    void onUndo();
    void onRedo();
    void onZoomIn();
    void onZoomOut();
    void onFit();
    void updateDisplay(); // Refresh pixmap from m_currentImage

private:
    MainWindow* m_mainWindow;
    
    // UI
    QGraphicsView* m_view;
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixItem;
    QGraphicsEllipseItem* m_cursorItem;
    
    QSlider* m_radiusSlider;
    QSlider* m_featherSlider;
    QSlider* m_opacitySlider;
    QCheckBox* m_autoStretchCheck;
    QDoubleSpinBox* m_targetMedianSpin;
    QCheckBox* m_linkedCheck;
    class QComboBox* m_methodCombo;
    
    QPushButton* m_undoBtn;
    QPushButton* m_redoBtn;
    
    // Data
    ImageBuffer m_currentImage; // float32 buffer
    std::vector<ImageBuffer> m_undoStack;
    std::vector<ImageBuffer> m_redoStack;
    
    float m_zoom = 1.0f;
    void setZoom(float z);
    
    // Logic
    void healAt(QPointF scenePos);
    QPointF m_lastPanPos; // For right-click panning
    
    bool m_busy = false; // Prevent overlapping ops
};

#endif // CORRECTIONBRUSHDIALOG_H
