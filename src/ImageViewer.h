#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <vector>
#include <QPolygonF>
#include <QScrollBar> // Added
#include "ImageBuffer.h"
#include <memory>

class ImageHistoryManager;  // Forward declaration

class ImageViewer : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageViewer(QWidget* parent = nullptr);
    virtual ~ImageViewer();
    
    void setImage(const QImage& image, bool preserveView = false);
    void zoomIn();
    void zoomOut();
    void zoom1to1(); // New 1:1 Zoom
    void fitToWindow();

    // MDI / History Support
    ImageBuffer& getBuffer() { return m_buffer; }
    const ImageBuffer& getBuffer() const { return m_buffer; }
    QString getHeaderValue(const QString &key) const { return m_buffer.getHeaderValue(key); }
    void setBuffer(const ImageBuffer& buffer, const QString& name = "Untitled", bool preserveView = false);
    void refreshDisplay(bool preserveView = true);
    void refresh() { refreshDisplay(true); } // Alias

    // Mask Support
    void setMaskOverlay(bool show) { m_showMaskOverlay = show; refreshDisplay(true); }
    bool isMaskOverlayEnabled() const { return m_showMaskOverlay; }

    void pushUndo();
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // Crop Mode
    void setCropMode(bool active);
    void setCropAngle(float angle);
    void setAspectRatio(float ratio);
    void getCropState(float& cx, float& cy, float& w, float& h, float& angle) const;

    // ABE Mode
    void setAbeMode(bool enable);
    void clearAbePolygons();
    std::vector<QPolygonF> getAbePolygons() const;
    
    // Pick Mode
    void setPickMode(bool active);
    void setRectQueryMode(bool active); // For Area Mean

    enum InteractionMode {
        Mode_PanZoom,   // Default: Drag=Pan, Wheel=Zoom
        Mode_Selection, // Drag=Rect Select, Ctrl+Drag=Pan
        Mode_Crop,
        Mode_ABE
    };
    void setInteractionMode(InteractionMode mode);
    InteractionMode getInteractionMode() const { return m_interactionMode; }
    
    // GHS Preview
    void setPreviewLUT(const std::vector<std::vector<float>>& luts); // 3 channels x 65536
    void clearPreviewLUT();
    
    // Display State
    ImageBuffer::DisplayMode getDisplayMode() const { return m_displayMode; }
    bool isDisplayLinked() const { return m_displayLinked; }
    bool isDisplayInverted() const { return m_displayInverted; }
    bool isDisplayFalseColor() const { return m_displayFalseColor; }
    void setDisplayState(ImageBuffer::DisplayMode mode, bool linked);
    void setInverted(bool inverted);
    void setFalseColor(bool falseColor);
    QImage getCurrentDisplayImage() const { return m_displayImage; }
    
    // Modification State
    bool isModified() const { return m_isModified; }
    void setModified(bool modified);
    
    void clearSelection();
    QRectF getSelectionRect() const; // Returns current selection in scene coords

signals:
    void pointPicked(QPointF p); // Scene coordinates (pixels)
    void rectSelected(QRectF r);
    void requestNewView(const ImageBuffer& img, const QString& title);
    void bufferChanged(); // buffer content updated (e.g. undo/redo)
    void historyChanged(); // New: undo/redo stacks updated
    
    // Linking Signals
    void viewChanged(float scale, float hVal, float vVal);
    void unlinked();
    void modifiedChanged(bool modified);
    
public slots:
    void syncView(float scale, float hVal, float vVal);

public:
    // Accessors for Initial Sync
    float getScale() const { return (float)m_scaleFactor; }
    int getHBarLoc() const;
    int getVBarLoc() const;
    
    // Annotation Support
    double zoomFactor() const { return m_scaleFactor; }
    double pixelScale() const;  // arcsec/pixel from WCS
    QPointF mapToScene(const QPoint& widgetPos) const { return QGraphicsView::mapToScene(widgetPos); }
    QPointF mapFromScene(const QPointF& scenePos) const { return QGraphicsView::mapFromScene(scenePos).toPointF(); } 

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

public:
    bool isLinked() const { return m_isLinked; }
    void setLinked(bool linked);

private:
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_imageItem;
    QGraphicsRectItem* m_cropItem = nullptr;
    
    // Link State
    bool m_isLinked = false;
    
    // Data & History
    ImageBuffer m_buffer;
    std::vector<ImageBuffer> m_undoStack;  // Backward-compat: stores full copies (legacy mode)
    std::vector<ImageBuffer> m_redoStack;  // Backward-compat: stores full copies (legacy mode)
    
    // Delta-based history (new, memory-efficient)
    std::unique_ptr<ImageHistoryManager> m_historyManager;
    bool m_useDeltaHistory = true;  // Feature flag: use delta compression

    QImage m_displayImage;
    double m_scaleFactor = 1.0;
    
    float m_zoom = 1.0f;
    
    float m_panX = 0.0f, m_panY = 0.0f;
    QPointF m_lastMousePos;
    bool m_dragging = false;

    // Crop State
    bool m_cropMode = false;
    // ... (Crop members)
    QPointF m_startPoint;
    QPointF m_endPoint;
    bool m_drawing = false;
    bool m_pickMode = false;
    bool m_rectQueryMode = false;
    QGraphicsRectItem* m_queryRectItem = nullptr;
    
    // Advanced Crop
    bool m_moving = false;
    QPointF m_lastPos; // Used for moving crop box
    
    float m_cropAngle = 0.0f;
    
    // ABE State
    bool m_abeMode = false;
    std::vector<class QGraphicsPolygonItem*> m_abeItems;
    class QGraphicsPolygonItem* m_currentLassoItem = nullptr;
    QPolygonF m_currentLassoPoly;
    bool m_lassoDrawing = false;
    
    float m_aspectRatio = -1.0f; // -1 = Free
    
    InteractionMode m_interactionMode = Mode_PanZoom;
    std::vector<std::vector<float>> m_previewLUT; // Empty if no preview
    
    ImageBuffer::DisplayMode m_displayMode = ImageBuffer::Display_Linear;
    bool m_displayLinked = true;
    bool m_displayInverted = false;
    bool m_displayFalseColor = false;
    bool m_isModified = false;
    bool m_showMaskOverlay = true;
};

#endif // IMAGEVIEWER_H
