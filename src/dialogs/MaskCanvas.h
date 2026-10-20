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

#ifndef MASKCANVAS_H
#define MASKCANVAS_H

// =============================================================================
// MaskCanvas.h
// Interactive graphics canvas for drawing elliptical and freehand-polygon
// mask regions on top of a background image. Supports interactive resize,
// rotation, zoom/pan, and rasterization of drawn shapes to a floating-point
// mask buffer via OpenCV.
// =============================================================================

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsEllipseItem>
#include <QWheelEvent>
#include <QTimer>

#include <memory>
#include <vector>

#include "../ImageBuffer.h"

// Forward declarations.
class InteractiveEllipseItem;
class HandleItem;

// =============================================================================
// InteractiveEllipseItem
// Ellipse shape with draggable resize handles and a rotation handle.
// =============================================================================

class InteractiveEllipseItem : public QGraphicsEllipseItem {
public:
    InteractiveEllipseItem(const QRectF& rect);

    // Reposition all handles to match the current ellipse geometry.
    void updateHandles();

    // Adjust the ellipse boundary in response to a handle drag.
    void interactiveResize(const QString& role, float dx, float dy);

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    std::vector<HandleItem*> m_handles;
    bool m_resizing = false;
};

// =============================================================================
// HandleItem
// Small rectangular grip attached to an InteractiveEllipseItem that enables
// resize (top/bottom/left/right) or rotation.
// =============================================================================

class HandleItem : public QGraphicsRectItem {
public:
    HandleItem(const QString& role, InteractiveEllipseItem* parent);

    QString                 role;
    InteractiveEllipseItem* parentEllipse;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QPointF m_lastPos;

    // Rotation-specific state.
    QPointF m_centerScene;
    double  m_startAngle    = 0;
    double  m_startRotation = 0;
};

// =============================================================================
// MaskCanvas
// QGraphicsView subclass providing drawing tools (polygon, ellipse, select)
// and mask rasterization.
// =============================================================================

class MaskCanvas : public QGraphicsView {
    Q_OBJECT

public:
    explicit MaskCanvas(const QImage& bgImage, QWidget* parent = nullptr);

    // Set the active drawing mode: "polygon", "ellipse", or "select".
    void setMode(const QString& mode);

    // Remove all drawn shapes from the canvas.
    void clearShapes();

    // Replace the background reference image.
    void setBackgroundImage(const QImage& bgImage);

    // Create a polygon covering the entire image area.
    void selectEntireImage();

signals:
    // Emitted whenever a shape is added, moved, or removed.
    void maskContentChanged();

public:
    // Zoom controls.
    void setZoom(float zoom);
    void zoomIn();
    void zoomOut();
    void fitToView();

    // Rasterize all drawn shapes into a [0.0, 1.0] float mask buffer.
    std::vector<float> createMask();
    std::vector<float> createMask(int w, int h);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QGraphicsScene*      m_scene  = nullptr;
    QGraphicsPixmapItem* m_bgItem = nullptr;

    float m_zoom = 1.0f;
    static constexpr float MIN_ZOOM = 0.05f;
    static constexpr float MAX_ZOOM = 8.0f;

    QString m_mode = "polygon"; // Active drawing mode.

    // Temporary items during active drawing operations.
    QGraphicsPathItem*    m_tempPath    = nullptr;
    QGraphicsEllipseItem* m_tempEllipse = nullptr;
    QVector<QPointF>      m_polyPoints;
    QPointF               m_ellipseOrigin;

    // Finalized shapes (owned by the scene, tracked here for enumeration).
    QList<QGraphicsItem*> m_shapes;

    bool m_drawing = false;
};

#endif // MASKCANVAS_H