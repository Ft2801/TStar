#ifndef ASTROSPIKEDIALOG_H
#define ASTROSPIKEDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QImage>
#include <QThread>
#include <QBasicTimer>
#include <QVector>
#include <QColor>
#include <QPointF>
#include <QMutex>
#include <QPointer>
#include "../ImageBuffer.h"

// Forward certs
class QLabel;
class QSlider;
class QCheckBox;
class QDoubleSpinBox;
class QScrollArea;
class QVBoxLayout;
class QPushButton;

// Data Structures
namespace AstroSpike {

    struct Star {
        float x = 0;
        float y = 0;
        float brightness = 0;
        float radius = 0;
        QColor color;
    };

    enum class ToolMode {
        None,
        Add,
        Erase
    };

    struct Config {
        // Detection
        float threshold = 100.0f; // 1-100 UI range
        float starAmount = 100.0f; // %
        float minStarSize = 0.0f;
        float maxStarSize = 100.0f;

        // Main Spikes
        float quantity = 4.0f;
        float length = 300.0f;
        float globalScale = 1.0f;
        float angle = 45.0f;
        float intensity = 1.0f;
        float spikeWidth = 1.0f;
        float sharpness = 0.5f;

        // Appearance
        float colorSaturation = 1.0f;
        float hueShift = 0.0f;

        // Secondary Spikes
        float secondaryIntensity = 0.5f;
        float secondaryLength = 120.0f;
        float secondaryOffset = 45.0f;

        // Soft Flare
        float softFlareIntensity = 3.0f;
        float softFlareSize = 15.0f;

        // Halo
        bool enableHalo = false;
        float haloIntensity = 0.5f;
        float haloScale = 5.0f;
        float haloWidth = 1.0f;
        float haloBlur = 0.5f;
        float haloSaturation = 1.0f;

        // Rainbow
        bool enableRainbow = false;
        bool rainbowSpikes = true;
        float rainbowIntensity = 0.8f;
        float rainbowFrequency = 1.0f;
        double rainbowLength = 0.8f; // Changed to double to match Python precision usage potentially? No, float is fine usually.
    };
}

// Canvas for Preview and Interaction
class AstroSpikeCanvas : public QWidget {
    Q_OBJECT
public:
    explicit AstroSpikeCanvas(QWidget* parent = nullptr);
    void setImage(const QImage& img);
    void setStars(const QVector<AstroSpike::Star>& stars);
    void setConfig(const AstroSpike::Config& config);
    void setToolMode(AstroSpike::ToolMode mode);
    
    // Explicit size setters
    void setStarInputRadius(float r) { m_brushRadius = r; update(); }
    void setEraserInputSize(float s) { m_eraserSize = s; update(); }

    const QVector<AstroSpike::Star>& getStars() const { return m_stars; }

    void zoomIn();
    void zoomOut();
    void fitToView();

    void render(QPainter& p, float scale, const QPointF& offset);

signals:
    void starsUpdated(const QVector<AstroSpike::Star>& stars);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void drawPreview(QPainter& p, float scale);
    QColor getStarColor(const AstroSpike::Star& star, float hueShift, float sat, float alpha);
    void createGlowSprite();
    void handleTool(const QPointF& imgPos);

    QImage m_image; // Display image (8-bit)
    QVector<AstroSpike::Star> m_stars;
    AstroSpike::Config m_config;
    AstroSpike::ToolMode m_toolMode = AstroSpike::ToolMode::None;

    float m_zoom = 1.0f;
    QPointF m_panOffset;
    QPoint m_lastMousePos;
    bool m_dragging = false;
    bool m_firstResize = true;

    float m_brushRadius = 4.0f;
    float m_eraserSize = 20.0f;
    
    QImage m_glowSprite; // Cached glow sprite
};

// Detection Thread
class StarDetectionThread : public QThread {
    Q_OBJECT
public:
    StarDetectionThread(const ImageBuffer& buffer, float threshold, QObject* parent = nullptr);
    void run() override;

signals:
    void detectionComplete(const QVector<AstroSpike::Star>& stars);

private:
    QImage m_bufferImage; // Using QImage for pixel access in thread for simplicity, or copy buffer data
    float m_threshold;
    int m_width;
    int m_height;
    // We need a copy of data to avoid race conditions if main thread touches buffer
    QVector<float> m_lumData; 
    QVector<uint8_t> m_rgbData;
};

// Main Dialog
#include "DialogBase.h"

class AstroSpikeDialog : public DialogBase {
    Q_OBJECT
public:
    explicit AstroSpikeDialog(class ImageViewer* viewer, QWidget* parent = nullptr);
    ~AstroSpikeDialog();
    
    void setViewer(ImageViewer* v);

protected:
    void closeEvent(QCloseEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void runDetection();
    void onStarsDetected(const QVector<AstroSpike::Star>& stars);
    void onCanvasStarsUpdated(const QVector<AstroSpike::Star>& stars);
    
    // Tools
    void setToolMode(AstroSpike::ToolMode mode);
    void onConfigChanged();
    void resetConfig();
    
    // Actions
    void applyToDocument();
    void saveImage();

private:
    void setupUI();
    void setupControls(QVBoxLayout* layout);
    QWidget* createSlider(const QString& label, float min, float max, float step, float initial, float* target, const QString& unit = "");

    class ImageViewer* m_viewer;
    AstroSpike::Config m_config;
    
    AstroSpikeCanvas* m_canvas;
    StarDetectionThread* m_thread = nullptr;
    QBasicTimer m_detectTimer;
    
    // Controls
    QScrollArea* m_controlsScroll;
    QLabel* m_statusLabel;
    
    // History for Undo/Redo
    QVector<QVector<AstroSpike::Star>> m_history;
    int m_historyIndex = -1;
    QPushButton* m_btnUndo;
    QPushButton* m_btnRedo;
    
    void pushHistory(const QVector<AstroSpike::Star>& stars);
    void undo();
    void redo();
    void updateHistoryButtons();
};

#endif // ASTROSPIKEDIALOG_H
