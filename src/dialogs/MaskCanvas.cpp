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

#include "MaskCanvas.h"
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QtMath>
#include <opencv2/opencv.hpp>
#include <QDebug>

// =============================================================================
// InteractiveEllipseItem
// =============================================================================

InteractiveEllipseItem::InteractiveEllipseItem(const QRectF& rect)
    : QGraphicsEllipseItem(rect)
{
    setFlags(
        QGraphicsItem::ItemIsMovable |
        QGraphicsItem::ItemIsSelectable |
        QGraphicsItem::ItemSendsGeometryChanges
    );
    setTransformOriginPoint(rect.center());

    QPen pen(Qt::green);
    pen.setWidth(2);
    pen.setCosmetic(true);
    setPen(pen);
    setBrush(QColor(0, 255, 0, 50));

    // Instantiate handles for each cardinal resize direction plus rotation
    const QString roles[] = { "top", "bottom", "left", "right", "rotate" };
    for (const QString& role : roles)
        m_handles.push_back(new HandleItem(role, this));

    updateHandles();
}

// Reposition all handles to match the current ellipse geometry.
void InteractiveEllipseItem::updateHandles()
{
    QRectF r = rect();
    QPointF c = r.center();

    for (HandleItem* h : m_handles) {
        h->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);

        QPointF p;
        if      (h->role == "top")    p = QPointF(c.x(), r.top());
        else if (h->role == "bottom") p = QPointF(c.x(), r.bottom());
        else if (h->role == "left")   p = QPointF(r.left(), c.y());
        else if (h->role == "right")  p = QPointF(r.right(), c.y());
        else if (h->role == "rotate") p = QPointF(c.x(), r.top() - 20);

        h->setPos(p);
        h->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    }
}

// Defer handle updates via a zero-timer to avoid re-entrant geometry changes.
QVariant InteractiveEllipseItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange  ||
        change == ItemTransformChange ||
        change == ItemSelectedHasChanged)
    {
        QTimer::singleShot(0, [this]() { updateHandles(); });
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

// Apply an incremental resize delta for the given edge handle role.
// Guards against re-entrant calls via m_resizing.
void InteractiveEllipseItem::interactiveResize(const QString& role, float dx, float dy)
{
    if (m_resizing) return;

    QRectF r = rect();
    if      (role == "top")    r.setTop   (r.top()    + dy);
    else if (role == "bottom") r.setBottom(r.bottom() + dy);
    else if (role == "left")   r.setLeft  (r.left()   + dx);
    else if (role == "right")  r.setRight (r.right()  + dx);

    m_resizing = true;
    prepareGeometryChange();
    setRect(r.normalized());
    m_resizing = false;
}

// =============================================================================
// HandleItem
// =============================================================================

HandleItem::HandleItem(const QString& r, InteractiveEllipseItem* parent)
    : QGraphicsRectItem(-4, -4, 8, 8, parent)
    , role(r)
    , parentEllipse(parent)
{
    setBrush(Qt::red);
    setFlag(ItemIgnoresTransformations, true);

    // Assign cursor based on handle function
    if      (role == "top" || role == "bottom") setCursor(Qt::SizeVerCursor);
    else if (role == "left" || role == "right") setCursor(Qt::SizeHorCursor);
    else if (role == "rotate")                  setCursor(Qt::OpenHandCursor);
}

void HandleItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (role == "rotate") {
        // Record the starting angle and rotation for incremental delta computation
        m_centerScene = parentEllipse->mapToScene(parentEllipse->rect().center());
        const QPointF p = event->scenePos();
        m_startAngle    = qRadiansToDegrees(qAtan2(p.y() - m_centerScene.y(),
                                                    p.x() - m_centerScene.x()));
        m_startRotation = parentEllipse->rotation();
        setCursor(Qt::ClosedHandCursor);
    }

    m_lastPos = event->scenePos();
    event->accept();
}

void HandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (role == "rotate") {
        const QPointF p    = event->scenePos();
        const double angle = qRadiansToDegrees(qAtan2(p.y() - m_centerScene.y(),
                                                       p.x() - m_centerScene.x()));
        parentEllipse->setRotation(m_startRotation + (angle - m_startAngle));
    } else {
        // Transform the scene-space delta into the ellipse's local coordinate space
        const QPointF deltaScene = event->scenePos() - m_lastPos;
        const QTransform t       = parentEllipse->sceneTransform().inverted();
        const QPointF deltaLocal = t.map(deltaScene) - t.map(QPointF(0, 0));

        parentEllipse->interactiveResize(role, deltaLocal.x(), deltaLocal.y());
    }

    m_lastPos = event->scenePos();
    event->accept();
}

void HandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (role == "rotate")
        setCursor(Qt::OpenHandCursor);

    event->accept();
}

// =============================================================================
// MaskCanvas
// =============================================================================

MaskCanvas::MaskCanvas(const QImage& bgImage, QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    // Add background image as a non-interactive pixmap item
    m_bgItem = new QGraphicsPixmapItem(QPixmap::fromImage(bgImage));
    m_scene->addItem(m_bgItem);
    m_scene->setSceneRect(m_bgItem->boundingRect());

    setRenderHint(QPainter::Antialiasing);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    // Defer fit-to-view until the widget has a valid geometry
    QTimer::singleShot(0, this, &MaskCanvas::fitToView);
}

// Switch the active drawing mode and, when entering select mode, auto-select the full image.
void MaskCanvas::setMode(const QString& mode)
{
    m_mode = mode;
    if (m_mode == "select")
        selectEntireImage();
}

void MaskCanvas::setBackgroundImage(const QImage& bgImage)
{
    if (m_bgItem) {
        m_bgItem->setPixmap(QPixmap::fromImage(bgImage));
        m_scene->setSceneRect(m_bgItem->boundingRect());
    }
}

// Remove all user-drawn shapes from the scene and clear the internal list.
void MaskCanvas::clearShapes()
{
    for (QGraphicsItem* item : m_shapes) {
        m_scene->removeItem(item);
        delete item;
    }
    m_shapes.clear();
    emit maskContentChanged();
}

// Replace all shapes with a single polygon covering the entire image extent.
void MaskCanvas::selectEntireImage()
{
    clearShapes();

    const QRectF r = m_bgItem->boundingRect();
    QPolygonF poly;
    poly << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();

    QGraphicsPolygonItem* pItem = new QGraphicsPolygonItem(poly);
    pItem->setBrush(QColor(0, 255, 0, 50));

    QPen pen(Qt::green);
    pen.setWidth(2);
    pen.setCosmetic(true);
    pItem->setPen(pen);
    pItem->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);

    m_scene->addItem(pItem);
    m_shapes.append(pItem);
    emit maskContentChanged();
}

// Clamp and apply a new zoom level, then rebuild the view transform.
void MaskCanvas::setZoom(float zoom)
{
    m_zoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, zoom));
    resetTransform();
    scale(m_zoom, m_zoom);
}

void MaskCanvas::zoomIn()  { setZoom(m_zoom * 1.25f); }
void MaskCanvas::zoomOut() { setZoom(m_zoom / 1.25f); }

void MaskCanvas::fitToView()
{
    if (!m_bgItem) return;
    fitInView(m_bgItem->boundingRect(), Qt::KeepAspectRatio);
    m_zoom = transform().m11();
}

// Zoom via Ctrl+Wheel; delegate all other scroll events to the base class.
void MaskCanvas::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        (event->angleDelta().y() > 0) ? zoomIn() : zoomOut();
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void MaskCanvas::mousePressEvent(QMouseEvent* event)
{
    const QPointF pt = mapToScene(event->pos());

    // In ellipse mode, let Qt handle interactions with existing scene items
    // so the user can move or resize them without starting a new ellipse.
    if (m_mode == "ellipse" && event->button() == Qt::LeftButton) {
        for (QGraphicsItem* item : items(event->pos())) {
            if (item != m_bgItem) {
                QGraphicsView::mousePressEvent(event);
                return;
            }
        }
    }

    // Freehand polygon: begin a new path on press
    if (m_mode == "polygon" && event->button() == Qt::LeftButton) {
        m_polyPoints.clear();
        m_polyPoints.append(pt);

        QPainterPath path(pt);
        m_tempPath = new QGraphicsPathItem(path);

        QPen pen(Qt::red);
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        m_tempPath->setPen(pen);
        m_scene->addItem(m_tempPath);
        return;
    }

    // Begin drawing a new ellipse (only reached when not over an existing item)
    if (m_mode == "ellipse" && event->button() == Qt::LeftButton) {
        m_ellipseOrigin = pt;
        m_tempEllipse   = new QGraphicsEllipseItem(QRectF(pt, pt));

        QPen pen(Qt::green);
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        m_tempEllipse->setPen(pen);
        m_scene->addItem(m_tempEllipse);
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void MaskCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pt = mapToScene(event->pos());

    if (m_mode == "ellipse" && m_tempEllipse) {
        m_tempEllipse->setRect(QRectF(m_ellipseOrigin, pt).normalized());
    } else if (m_mode == "polygon" && m_tempPath) {
        m_polyPoints.append(pt);

        QPainterPath path(m_polyPoints[0]);
        for (int i = 1; i < m_polyPoints.size(); ++i)
            path.lineTo(m_polyPoints[i]);

        m_tempPath->setPath(path);
    }

    QGraphicsView::mouseMoveEvent(event);
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_mode == "ellipse" && m_tempEllipse) {
        const QRectF finalRect = m_tempEllipse->rect().normalized();
        m_scene->removeItem(m_tempEllipse);
        delete m_tempEllipse;
        m_tempEllipse = nullptr;

        // Discard degenerate ellipses (accidental clicks)
        if (finalRect.width() > 4 && finalRect.height() > 4) {
            InteractiveEllipseItem* item = new InteractiveEllipseItem(finalRect);
            m_scene->addItem(item);
            m_shapes.append(item);
        }
    } else if (m_mode == "polygon" && m_tempPath) {
        m_scene->removeItem(m_tempPath);
        delete m_tempPath;
        m_tempPath = nullptr;

        // Require at least three points for a valid polygon
        if (m_polyPoints.size() > 2) {
            QGraphicsPolygonItem* polyItem = new QGraphicsPolygonItem(QPolygonF(m_polyPoints));
            polyItem->setBrush(QColor(0, 255, 0, 50));

            QPen pen(Qt::green);
            pen.setWidth(2);
            pen.setCosmetic(true);
            polyItem->setPen(pen);
            polyItem->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);

            m_scene->addItem(polyItem);
            m_shapes.append(polyItem);
        }

        m_polyPoints.clear();
    }

    QGraphicsView::mouseReleaseEvent(event);

    // Notify listeners that the mask content has changed
    if (m_drawing || (m_mode == "ellipse" && !m_tempEllipse) || (m_mode == "polygon" && !m_tempPath))
        emit maskContentChanged();
}

// Convenience overload: generate a mask at the background image's native resolution.
std::vector<float> MaskCanvas::createMask()
{
    if (!m_bgItem) return {};
    return createMask(m_bgItem->pixmap().width(), m_bgItem->pixmap().height());
}

// Rasterize all drawn shapes into a binary float mask at the specified resolution.
// Ellipses and polygons are mapped from scene coordinates to output pixel coordinates.
std::vector<float> MaskCanvas::createMask(int w, int h)
{
    if (!m_bgItem || w <= 0 || h <= 0) return {};

    const int   origW  = m_bgItem->pixmap().width();
    const int   origH  = m_bgItem->pixmap().height();
    const float scaleX = static_cast<float>(w) / origW;
    const float scaleY = static_cast<float>(h) / origH;

    cv::Mat mask = cv::Mat::zeros(h, w, CV_8UC1);

    for (QGraphicsItem* item : m_shapes) {
        InteractiveEllipseItem* ell  = dynamic_cast<InteractiveEllipseItem*>(item);
        QGraphicsPolygonItem*   poly = dynamic_cast<QGraphicsPolygonItem*>(item);

        if (ell) {
            // Derive centre and semi-axes in scene space, then scale to output resolution
            const QRectF r = ell->rect();
            const QPointF c     = ell->mapToScene(r.center());
            const QPointF edgeX = ell->mapToScene(QPointF(r.center().x() + r.width()  / 2.0f, r.center().y()));
            const QPointF edgeY = ell->mapToScene(QPointF(r.center().x(),                      r.center().y() + r.height() / 2.0f));

            const float axesX = std::sqrt(std::pow(edgeX.x() - c.x(), 2) + std::pow(edgeX.y() - c.y(), 2));
            const float axesY = std::sqrt(std::pow(edgeY.x() - c.x(), 2) + std::pow(edgeY.y() - c.y(), 2));

            cv::ellipse(
                mask,
                cv::Point(qRound(c.x() * scaleX), qRound(c.y() * scaleY)),
                cv::Size (qRound(axesX  * scaleX), qRound(axesY  * scaleY)),
                ell->rotation(), 0, 360, cv::Scalar(1), -1
            );
        } else if (poly) {
            std::vector<cv::Point> pts;
            for (const QPointF& p : poly->polygon()) {
                const QPointF ps = poly->mapToScene(p);
                pts.push_back(cv::Point(qRound(ps.x() * scaleX), qRound(ps.y() * scaleY)));
            }
            if (!pts.empty()) {
                std::vector<std::vector<cv::Point>> contours = { pts };
                cv::fillPoly(mask, contours, cv::Scalar(1));
            }
        }
    }

    // Convert the 8-bit mask to a flat float vector (0.0 / 1.0)
    std::vector<float> result(w * h);
    for (int row = 0; row < h; ++row) {
        const uchar* rowPtr = mask.ptr<uchar>(row);
        for (int col = 0; col < w; ++col)
            result[row * w + col] = (rowPtr[col] > 0) ? 1.0f : 0.0f;
    }

    return result;
}

// Defer fit-to-view slightly on show to ensure the viewport geometry is valid.
void MaskCanvas::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    QTimer::singleShot(100, this, &MaskCanvas::fitToView);
}