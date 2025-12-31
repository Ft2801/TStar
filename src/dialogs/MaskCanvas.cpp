#include "MaskCanvas.h"
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QtMath>
#include <opencv2/opencv.hpp>
#include <QDebug>

// --- Interactive Ellipse Item ---
InteractiveEllipseItem::InteractiveEllipseItem(const QRectF& rect) : QGraphicsEllipseItem(rect) {
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
    setTransformOriginPoint(rect.center());
    
    QPen pen(Qt::green);
    pen.setWidth(2);
    pen.setCosmetic(true);
    setPen(pen);
    setBrush(QColor(0, 255, 0, 50));
    
    // Create handles
    QString roles[] = {"top", "bottom", "left", "right", "rotate"};
    for (const QString& role : roles) {
        m_handles.push_back(new HandleItem(role, this));
    }
    updateHandles();
}

void InteractiveEllipseItem::updateHandles() {
    QRectF r = rect();
    QPointF c = r.center();
    
    for (HandleItem* h : m_handles) {
        h->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);
        
        QPointF p;
        if (h->role == "top") p = QPointF(c.x(), r.top());
        else if (h->role == "bottom") p = QPointF(c.x(), r.bottom());
        else if (h->role == "left") p = QPointF(r.left(), c.y());
        else if (h->role == "right") p = QPointF(r.right(), c.y());
        else if (h->role == "rotate") p = QPointF(c.x(), r.top() - 20);
        
        h->setPos(p);
        h->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    }
}

QVariant InteractiveEllipseItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange || change == ItemTransformChange || change == ItemSelectedHasChanged) {
        QTimer::singleShot(0, [this](){ updateHandles(); });
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

void InteractiveEllipseItem::interactiveResize(const QString& role, float dx, float dy) {
    if (m_resizing) return;
    
    QRectF r = rect();
    if (role == "top") r.setTop(r.top() + dy);
    else if (role == "bottom") r.setBottom(r.bottom() + dy);
    else if (role == "left") r.setLeft(r.left() + dx);
    else if (role == "right") r.setRight(r.right() + dx);
    
    m_resizing = true;
    prepareGeometryChange();
    setRect(r.normalized());
    // updateHandles(); // Optimization: defer?
    m_resizing = false;
}

// --- Handle Item ---
HandleItem::HandleItem(const QString& r, InteractiveEllipseItem* parent) 
    : QGraphicsRectItem(-4, -4, 8, 8, parent), role(r), parentEllipse(parent) 
{
    setBrush(Qt::red);
    // setFlag(ItemIsMovable, false); // We handle movement manually
    setFlag(ItemIgnoresTransformations, true); // Keep size constant on zoom? No, 'cosmetic' maybe better?  
    
    if (role == "top" || role == "bottom") setCursor(Qt::SizeVerCursor);
    else if (role == "left" || role == "right") setCursor(Qt::SizeHorCursor);
    else if (role == "rotate") setCursor(Qt::OpenHandCursor);
}

void HandleItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (role == "rotate") {
        m_centerScene = parentEllipse->mapToScene(parentEllipse->rect().center());
        QPointF p = event->scenePos();
        double dx = p.x() - m_centerScene.x();
        double dy = p.y() - m_centerScene.y();
        m_startAngle = qRadiansToDegrees(qAtan2(dy, dx));
        m_startRotation = parentEllipse->rotation();
        setCursor(Qt::ClosedHandCursor);
    } 
    m_lastPos = event->scenePos();
    event->accept();
}

void HandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (role == "rotate") {
        QPointF p = event->scenePos();
        double dx = p.x() - m_centerScene.x();
        double dy = p.y() - m_centerScene.y();
        double currentAngle = qRadiansToDegrees(qAtan2(dy, dx));
        double delta = currentAngle - m_startAngle;
        
        parentEllipse->setRotation(m_startRotation + delta);
    } else {
        QPointF p = event->scenePos();
        QPointF deltaScene = p - m_lastPos;
        QTransform t = parentEllipse->sceneTransform().inverted();
        QPointF deltaLocal = t.map(deltaScene) - t.map(QPointF(0,0));
        
        parentEllipse->interactiveResize(role, deltaLocal.x(), deltaLocal.y());
    }
    m_lastPos = event->scenePos();
    event->accept();
}

void HandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (role == "rotate") setCursor(Qt::OpenHandCursor);
    event->accept();
}

// --- Mask Canvas ---

MaskCanvas::MaskCanvas(const QImage& bgImage, QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    
    // Background
    m_bgItem = new QGraphicsPixmapItem(QPixmap::fromImage(bgImage));
    m_scene->addItem(m_bgItem);
    m_scene->setSceneRect(m_bgItem->boundingRect());
    
    setRenderHint(QPainter::Antialiasing);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate); // Or SmartViewportUpdate
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    
    // Fit to view initially
    QTimer::singleShot(0, this, &MaskCanvas::fitToView);
}

void MaskCanvas::setMode(const QString& mode) {
    m_mode = mode;
    if (m_mode == "select") {
        selectEntireImage();
    }
}

void MaskCanvas::setBackgroundImage(const QImage& bgImage) {
    if (m_bgItem) {
        m_bgItem->setPixmap(QPixmap::fromImage(bgImage));
        m_scene->setSceneRect(m_bgItem->boundingRect());
    }
}

void MaskCanvas::clearShapes() {
    for (QGraphicsItem* item : m_shapes) {
        m_scene->removeItem(item);
        delete item;
    }
    m_shapes.clear();
    emit maskContentChanged();
}

void MaskCanvas::selectEntireImage() {
    clearShapes();
    QRectF rect = m_bgItem->boundingRect();
    QPolygonF poly;
    poly << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
    
    QGraphicsPolygonItem* pItem = new QGraphicsPolygonItem(poly);
    pItem->setBrush(QColor(0, 255, 0, 50));
    QPen pen(Qt::green); pen.setWidth(2); pen.setCosmetic(true);
    pItem->setPen(pen);
    pItem->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    
    m_scene->addItem(pItem);
    m_shapes.append(pItem);
    emit maskContentChanged();
}

void MaskCanvas::setZoom(float zoom) {
    m_zoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, zoom));
    resetTransform();
    scale(m_zoom, m_zoom);
}

void MaskCanvas::zoomIn() { setZoom(m_zoom * 1.25f); }
void MaskCanvas::zoomOut() { setZoom(m_zoom / 1.25f); }

void MaskCanvas::fitToView() {
    if (!m_bgItem) return;
    QRectF total = m_bgItem->boundingRect();
    fitInView(total, Qt::KeepAspectRatio);
    // update internal zoom
    m_zoom = transform().m11();
}

void MaskCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) zoomIn();
        else zoomOut();
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void MaskCanvas::mousePressEvent(QMouseEvent* event) {
    QPointF pt = mapToScene(event->pos());
    
    if (m_mode == "ellipse" && event->button() == Qt::LeftButton) {
        // Check if hitting any existing items
        QList<QGraphicsItem*> itemsUnderCursor = items(event->pos());
        for (QGraphicsItem* item : itemsUnderCursor) {
            if (item != m_bgItem) { // Ignore background
                // Let Qt handle the interaction (move, resize, etc.)
                QGraphicsView::mousePressEvent(event);
                return;
            }
        }
    }
    
    // Freehand polygon: start on press
    if (m_mode == "polygon" && event->button() == Qt::LeftButton) {
        m_polyPoints.clear();  // Start fresh
        m_polyPoints.append(pt);
        
        QPainterPath path(pt);
        m_tempPath = new QGraphicsPathItem(path);
        QPen pen(Qt::red); pen.setStyle(Qt::DashLine); pen.setCosmetic(true);
        m_tempPath->setPen(pen);
        m_scene->addItem(m_tempPath);
        return;
    }
    
    // Create new ellipse (only if not hitting existing items)
    if (m_mode == "ellipse" && event->button() == Qt::LeftButton) {
        m_ellipseOrigin = pt;
        m_tempEllipse = new QGraphicsEllipseItem(QRectF(pt, pt));
        QPen pen(Qt::green); pen.setStyle(Qt::DashLine); pen.setCosmetic(true);
        m_tempEllipse->setPen(pen);
        m_scene->addItem(m_tempEllipse);
        return;
    }
    
    QGraphicsView::mousePressEvent(event);
}

void MaskCanvas::mouseMoveEvent(QMouseEvent* event) {
    QPointF pt = mapToScene(event->pos());
    
    if (m_mode == "ellipse" && m_tempEllipse) {
        m_tempEllipse->setRect(QRectF(m_ellipseOrigin, pt).normalized());
    }
    // CRITICAL FIX: Freehand drawing - continuously append points during drag
    else if (m_mode == "polygon" && m_tempPath) {
        m_polyPoints.append(pt);
        
        QPainterPath path(m_polyPoints[0]);
        for (int i = 1; i < m_polyPoints.size(); ++i) {
            path.lineTo(m_polyPoints[i]);
        }
        m_tempPath->setPath(path);
    }
    
    QGraphicsView::mouseMoveEvent(event);
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_mode == "ellipse" && m_tempEllipse) {
        QRectF finalRect = m_tempEllipse->rect().normalized();
        m_scene->removeItem(m_tempEllipse);
        delete m_tempEllipse;
        m_tempEllipse = nullptr;
        
        if (finalRect.width() > 4 && finalRect.height() > 4) {
            InteractiveEllipseItem* item = new InteractiveEllipseItem(finalRect);
            m_scene->addItem(item);
            m_shapes.append(item);
        }
    }
    // Freehand polygon: finalize on release
    else if (m_mode == "polygon" && m_tempPath) {
        // Remove temp path
        m_scene->removeItem(m_tempPath);
        delete m_tempPath;
        m_tempPath = nullptr;
        
        // Create final polygon if we have enough points
        if (m_polyPoints.size() > 2) {
            QPolygonF polygon(m_polyPoints);
            QGraphicsPolygonItem* polyItem = new QGraphicsPolygonItem(polygon);
            polyItem->setBrush(QColor(0, 255, 0, 50));
            QPen pen(Qt::green); pen.setWidth(2); pen.setCosmetic(true);
            polyItem->setPen(pen);
            polyItem->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            m_scene->addItem(polyItem);
            m_shapes.append(polyItem);
        }
        
        m_polyPoints.clear();
    }
    
    QGraphicsView::mouseReleaseEvent(event);
    
    // Emit change if we were drawing (either ellipse finish or polygon finish)
    if (m_drawing || (m_mode == "ellipse" && !m_tempEllipse) || (m_mode == "polygon" && !m_tempPath)) {
         emit maskContentChanged();
    }
}

std::vector<float> MaskCanvas::createMask() {
    if (!m_bgItem) return {};
    return createMask(m_bgItem->pixmap().width(), m_bgItem->pixmap().height());
}

std::vector<float> MaskCanvas::createMask(int w, int h) {
    if (!m_bgItem || w <= 0 || h <= 0) return {};
    
    // Original dimensions for scaling
    int origW = m_bgItem->pixmap().width();
    int origH = m_bgItem->pixmap().height();
    float scaleX = (float)w / origW;
    float scaleY = (float)h / origH;
    
    cv::Mat mask = cv::Mat::zeros(h, w, CV_8UC1);
    
    // Fill shapes
    for (QGraphicsItem* item : m_shapes) {
        InteractiveEllipseItem* ell = dynamic_cast<InteractiveEllipseItem*>(item);
        QGraphicsPolygonItem* poly = dynamic_cast<QGraphicsPolygonItem*>(item);
        
        if (ell) {
            QRectF r = ell->rect();
            // Center is already in scene if we use mapToScene
            QPointF c = ell->mapToScene(r.center());
            QPointF edgeX = ell->mapToScene(QPointF(r.center().x() + r.width()/2.0f, r.center().y()));
            QPointF edgeY = ell->mapToScene(QPointF(r.center().x(), r.center().y() + r.height()/2.0f));
            
            float axesX = std::sqrt(std::pow(edgeX.x() - c.x(), 2) + std::pow(edgeX.y() - c.y(), 2));
            float axesY = std::sqrt(std::pow(edgeY.x() - c.x(), 2) + std::pow(edgeY.y() - c.y(), 2));
            float angle = ell->rotation();
            
            // Apply scale
            cv::Point cent(qRound(c.x() * scaleX), qRound(c.y() * scaleY));
            cv::Size axes(qRound(axesX * scaleX), qRound(axesY * scaleY));
            
            cv::ellipse(mask, cent, axes, angle, 0, 360, cv::Scalar(1), -1);
        } else if (poly) {
            QPolygonF qpts = poly->polygon();
            std::vector<cv::Point> pts;
            for (const QPointF& p : qpts) {
                // IMPORTANT FIX: Map local point to scene
                QPointF ps = poly->mapToScene(p);
                pts.push_back(cv::Point(qRound(ps.x() * scaleX), qRound(ps.y() * scaleY)));
            }
            if (!pts.empty()) {
                std::vector<std::vector<cv::Point>> contours = {pts};
                cv::fillPoly(mask, contours, cv::Scalar(1));
            }
        }
    }
    
    // Convert to float
    std::vector<float> result(w * h);
    for (int r = 0; r < h; ++r) {
        uchar* rowPtr = mask.ptr<uchar>(r);
        for (int c = 0; c < w; ++c) {
            result[r * w + c] = (rowPtr[c] > 0) ? 1.0f : 0.0f;
        }
    }
    
    return result;
}

void MaskCanvas::showEvent(QShowEvent* event) {
    QGraphicsView::showEvent(event);
    QTimer::singleShot(100, this, &MaskCanvas::fitToView);
}
