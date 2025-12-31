#ifndef MASKCANVAS_H
#define MASKCANVAS_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsEllipseItem>
#include <QWheelEvent>
#include <memory>
#include <vector>
#include <QTimer>
#include "../ImageBuffer.h"

// Forward decls for handles
class InteractiveEllipseItem;
class HandleItem;

// Custom Ellipse with handles
class InteractiveEllipseItem : public QGraphicsEllipseItem {
public:
    InteractiveEllipseItem(const QRectF& rect);
    void updateHandles();
    void interactiveResize(const QString& role, float dx, float dy);
    
protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    std::vector<HandleItem*> m_handles;
    bool m_resizing = false;
};

// Handle Item
class HandleItem : public QGraphicsRectItem {
public:
    HandleItem(const QString& role, InteractiveEllipseItem* parent);
    QString role;
    InteractiveEllipseItem* parentEllipse;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QPointF m_lastPos;
    // Rotate state
    QPointF m_centerScene;
    double m_startAngle = 0;
    double m_startRotation = 0;
};

class MaskCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit MaskCanvas(const QImage& bgImage, QWidget* parent = nullptr);
    
    // Mode: "polygon", "ellipse", "select"
    void setMode(const QString& mode);
    void clearShapes();
    void setBackgroundImage(const QImage& bgImage);
    void selectEntireImage();
    
    signals:
        void maskContentChanged();

    public:
    // Zoom API
    void setZoom(float zoom);
    void zoomIn();
    void zoomOut();
    void fitToView();

    // Mask Generation (Returns 0.0-1.0 float buffer)
    std::vector<float> createMask();
    std::vector<float> createMask(int w, int h);
    
protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_bgItem = nullptr;
    
    float m_zoom = 1.0f;
    const float MIN_ZOOM = 0.05f;
    const float MAX_ZOOM = 8.0f;
    
    QString m_mode = "polygon"; // polygon, ellipse, select
    
    // Temporary drawing
    QGraphicsPathItem* m_tempPath = nullptr;
    QGraphicsEllipseItem* m_tempEllipse = nullptr;
    QVector<QPointF> m_polyPoints;
    QPointF m_ellipseOrigin;
    
    // Final shapes
    QList<QGraphicsItem*> m_shapes;
    
    bool m_drawing = false;
};

#endif // MASKCANVAS_H
