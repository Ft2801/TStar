#include "ImageViewer.h"
#include <QWheelEvent>
#include <QScrollBar>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene> 
#include "widgets/CustomMdiSubWindow.h"
#include <QToolButton>
#include <QResizeEvent> 
#include <QIcon>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "ImageBufferDelta.h"


ImageViewer::ImageViewer(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    
    // Initialize delta-based history manager
    m_historyManager = std::make_unique<ImageHistoryManager>();
    
    m_imageItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_imageItem); // Add empty item initially
    
    // Enable dragging
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QBrush(QColor(30, 30, 30)));
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter); // Center the image as requested
    
    // Zoom at Mouse Cursor
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    
    // Accept drops for View Linking (Ctrl + Drag and Drop)
    setAcceptDrops(true);
    if (viewport()) viewport()->setAcceptDrops(true);
    
    // Init Crop Item (Hidden)
    m_cropItem = new QGraphicsRectItem();
    m_cropItem->setPen(QPen(Qt::yellow, 2, Qt::DashLine));
    m_cropItem->setBrush(QBrush(QColor(255, 255, 0, 30)));
    m_cropItem->setVisible(false);
    m_cropItem->setZValue(10); // On top
    m_scene->addItem(m_cropItem);


    // Init Rect Item
    m_queryRectItem = new QGraphicsRectItem();
    m_queryRectItem->setPen(QPen(Qt::yellow, 2, Qt::DashLine));
    m_queryRectItem->setBrush(QBrush(QColor(255, 255, 0, 30)));
    m_queryRectItem->setZValue(20);
    m_scene->addItem(m_queryRectItem);
    m_queryRectItem->setVisible(false);
}

ImageViewer::~ImageViewer() {
    if (m_isLinked) emit unlinked();
}

void ImageViewer::setInteractionMode(InteractionMode mode) {
    m_interactionMode = mode;
    if (mode == Mode_Selection) {
        setDragMode(QGraphicsView::NoDrag); // Draw by default
        setCursor(Qt::CrossCursor); 
    } else {
        setDragMode(QGraphicsView::ScrollHandDrag);
        setCursor(Qt::ArrowCursor);
        m_queryRectItem->setVisible(false);
    }
}

void ImageViewer::setPreviewLUT(const std::vector<std::vector<float>>& luts) {
    m_previewLUT = luts;
    // Use full resolution preview - no downsampling
    const std::vector<std::vector<float>>* pLut = m_previewLUT.empty() ? nullptr : &m_previewLUT;
    QImage img = m_buffer.getDisplayImage(m_displayMode, m_displayLinked, pLut, 0, 0, m_showMaskOverlay, m_displayInverted, m_displayFalseColor);
    setImage(img, true); // PRESERVE VIEW
}

void ImageViewer::setPreviewImage(const QImage& img) {
    if (img.isNull()) return;
    
    // Update pixmap
    m_imageItem->setPixmap(QPixmap::fromImage(img));
    
    // Scale to fit the original scene rect (assuming scene rect matches full image)
    QRectF sceneR = m_scene->sceneRect();
    if (sceneR.width() > 0 && sceneR.height() > 0) {
        qreal sx = sceneR.width() / img.width();
        qreal sy = sceneR.height() / img.height();
        
        // Reset transform and apply scale
        m_imageItem->setTransform(QTransform::fromScale(sx, sy));
    }
}

void ImageViewer::clearPreviewLUT() {
    m_previewLUT.clear();
    // Refresh with current state
    refreshDisplay(true);
}

void ImageViewer::setDisplayState(ImageBuffer::DisplayMode mode, bool linked) {
    m_displayMode = mode;
    m_displayLinked = linked;
    refreshDisplay(true);
}

void ImageViewer::setInverted(bool inverted) {
    if (m_displayInverted == inverted) return;
    m_displayInverted = inverted;
    refreshDisplay(true);
}

void ImageViewer::setFalseColor(bool falseColor) {
    if (m_displayFalseColor == falseColor) return;
    m_displayFalseColor = falseColor;
    refreshDisplay(true);
}

QRectF ImageViewer::getSelectionRect() const {
    if (m_queryRectItem && m_queryRectItem->isVisible()) {
        return m_queryRectItem->rect();
    }
    return QRectF();
}

void ImageViewer::clearSelection() {
    if (m_queryRectItem) {
        m_queryRectItem->setRect(QRectF());
        m_queryRectItem->setVisible(false);
    }
}


void ImageViewer::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    emit resized();  // Notify listeners that viewer has been resized
}

void ImageViewer::setLinked(bool linked) {
    if (m_isLinked == linked) return;
    m_isLinked = linked;
    update(); 
    emit viewChanged(m_scaleFactor, horizontalScrollBar()->value(), verticalScrollBar()->value());
    if (!linked) emit unlinked();
}

void ImageViewer::setImage(const QImage& image, bool preserveView) {
    m_displayImage = image; // Store for retrieval
    m_imageItem->setPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(image.rect());
    
    if (!preserveView) {
        fitToWindow();
    } else {
        m_scaleFactor = transform().m11();
    }
}

void ImageViewer::setBuffer(const ImageBuffer& buffer, const QString& name, bool preserveView) {
    m_buffer = buffer;
    
    // Use current stored display state with all parameters
    QImage img = m_buffer.getDisplayImage(m_displayMode, m_displayLinked, nullptr, 0, 0, m_showMaskOverlay, m_displayInverted, m_displayFalseColor);
    if (name != "Untitled") setWindowTitle(name);
    
    setImage(img, preserveView);
    
    // Assume process update; history managed by caller or pushUndo
}

void ImageViewer::refreshDisplay(bool preserveView) {
    const std::vector<std::vector<float>>* pLut = m_previewLUT.empty() ? nullptr : &m_previewLUT;
    QImage img = m_buffer.getDisplayImage(m_displayMode, m_displayLinked, pLut, 0, 0, m_showMaskOverlay, m_displayInverted, m_displayFalseColor);
    setImage(img, preserveView);
}

void ImageViewer::setCropMode(bool active) {
    m_cropMode = active;
    if (active) {
        setDragMode(QGraphicsView::NoDrag);
        m_cropItem->setRect(0, 0, 0, 0);
        m_cropItem->setRotation(0);
        m_cropAngle = 0;
        m_cropItem->setVisible(true);
    } else {
        setDragMode(QGraphicsView::ScrollHandDrag);
        m_cropItem->setVisible(false);
    }
}

void ImageViewer::setCropAngle(float angle) {
    m_cropAngle = angle;
    if (m_cropItem->isVisible()) {
        QRectF r = m_cropItem->rect(); // Local rect
        m_cropItem->setTransformOriginPoint(r.center());
        m_cropItem->setRotation(angle);
    }
}

void ImageViewer::getCropState(float& cx, float& cy, float& w, float& h, float& angle) const {
    if (!m_cropItem) return;
    QRectF r = m_cropItem->rect();
    // Item position in scene might be 0,0 if we only changed rect.
    // If user moved it... wait, we only implement drawing for now.
    // So pos is 0,0, rect defines geometry.
    // But rotation is around center.
    QPointF c = m_cropItem->mapToScene(r.center());
    cx = c.x();
    cy = c.y();
    w = r.width();
    h = r.height();
    angle = m_cropAngle;
}

void ImageViewer::setAspectRatio(float ratio) {
    m_aspectRatio = ratio;
    
    // Auto-update existing crop if valid
    if (m_cropMode && m_cropItem->isVisible() && m_cropItem->rect().width() > 0 && ratio > 0.0f) {
        QRectF r = m_cropItem->rect();
        float w = r.width(); // Preserve Width
        float h = w / ratio;
        
        QPointF c = r.center();
        QRectF newRect(0, 0, w, h);
        newRect.moveCenter(c);
        
        m_cropItem->setRect(newRect);
        m_cropItem->setTransformOriginPoint(newRect.center()); 
    }
}

void ImageViewer::setAbeMode(bool active) {
    m_abeMode = active;
    if (active) {
        setDragMode(QGraphicsView::NoDrag); // We handle drawing
        setCursor(Qt::CrossCursor);
        // Show existing items
        for(auto* item : m_abeItems) item->setVisible(true);
    } else {
        setDragMode(QGraphicsView::ScrollHandDrag);
        setCursor(Qt::ArrowCursor);
        // Hide items when mode inactive
        for(auto* item : m_abeItems) item->setVisible(false);
    }
}

void ImageViewer::clearAbePolygons() {
    for(auto* item : m_abeItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_abeItems.clear();
    if(m_currentLassoItem) {
        m_scene->removeItem(m_currentLassoItem);
        delete m_currentLassoItem;
        m_currentLassoItem = nullptr;
    }
}

std::vector<QPolygonF> ImageViewer::getAbePolygons() const {
    std::vector<QPolygonF> polys;
    for(auto* item : m_abeItems) {
        polys.push_back(item->polygon());
    }
    return polys;
}

void ImageViewer::setBackgroundSamples(const std::vector<QPointF>& points) {
    clearBackgroundSamples();
    
    for (const auto& p : points) {
        // Draw green cross or circle
        // Using circle for now
        QGraphicsEllipseItem* item = m_scene->addEllipse(
            p.x() - 5, p.y() - 5, 10, 10, 
            QPen(Qt::green, 1), QBrush(QColor(0, 255, 0, 100)));
        item->setZValue(30); // Above image/overlays
        m_sampleItems.push_back(item);
    }
}

void ImageViewer::clearBackgroundSamples() {
    for (auto* item : m_sampleItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_sampleItems.clear();
}

void ImageViewer::mousePressEvent(QMouseEvent* event) {
    if (m_pickMode && event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());
        emit pointPicked(scenePos);
        return; 
    }



    
    // Shift+Drag for New View Logic
    if ((event->modifiers() & Qt::ShiftModifier) && event->button() == Qt::LeftButton) {
        // Enforce Selection Mode behavior temporarily
        setDragMode(QGraphicsView::NoDrag);
        m_startPoint = mapToScene(event->pos());
        m_drawing = true;
        
        // Re-use query rect item (green rect)
        // Ensure it exists (ctor creates it)
        m_queryRectItem->setPen(QPen(Qt::yellow, 2, Qt::DashLine)); // Standard Yellow
        m_queryRectItem->setRect(QRectF(m_startPoint, m_startPoint));
        m_queryRectItem->setVisible(true);
        return;
    }

    if (m_interactionMode == Mode_Selection && event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ControlModifier) {
            // Pan
            setDragMode(QGraphicsView::ScrollHandDrag);
            QGraphicsView::mousePressEvent(event); // Default Pan
            return;
        } else {
            // Selection
            setDragMode(QGraphicsView::NoDrag);
            m_startPoint = mapToScene(event->pos());
            m_drawing = true;
            m_queryRectItem->setRect(QRectF(m_startPoint, m_startPoint));
            m_queryRectItem->setVisible(true);
            return;
        }
    }
    
    if (m_rectQueryMode && event->button() == Qt::LeftButton) {
        // ... Legacy logic if needed, or map to Selection Mode
        m_startPoint = mapToScene(event->pos());
        m_drawing = true;
        m_queryRectItem->setRect(QRectF(m_startPoint, m_startPoint));
        m_queryRectItem->setVisible(true);
        return;
    }


    if (m_cropMode && event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());
        QPointF itemPos = m_cropItem->mapFromScene(scenePos);
        
        // Hit test for Move (inside rect)
        // Make sure we check against the local rect which defines the shape
        if (m_cropItem->isVisible() && m_cropItem->contains(itemPos)) {
            m_moving = true;
            m_lastPos = scenePos;
            setCursor(Qt::SizeAllCursor);
        } else {
            // New Draw
            m_startPoint = scenePos;
            m_drawing = true;
            m_cropItem->setRect(QRectF(scenePos, scenePos)); // Zero size
            m_cropItem->setPos(0, 0); // Reset translation if any (important!)
            m_cropItem->setRotation(m_cropAngle); 
            setCursor(Qt::CrossCursor);
        }
        return; 
    }

    
    if (m_abeMode && event->button() == Qt::LeftButton) {
        // Start Lasso
        QPointF scenePos = mapToScene(event->pos());
        m_lassoDrawing = true;
        m_currentLassoPoly.clear();
        m_currentLassoPoly << scenePos;
        
        if (m_currentLassoItem) {
            m_scene->removeItem(m_currentLassoItem);
            delete m_currentLassoItem;
        }
        
        m_currentLassoItem = m_scene->addPolygon(m_currentLassoPoly, QPen(Qt::yellow, 2, Qt::DashLine), QBrush(QColor(255, 255, 0, 30)));
        m_currentLassoItem->setZValue(20);
        return;
    } else if (m_abeMode && event->button() == Qt::RightButton) {
        if (m_lassoDrawing) {
            m_lassoDrawing = false;
            if(m_currentLassoItem) {
                m_scene->removeItem(m_currentLassoItem);
                delete m_currentLassoItem;
                m_currentLassoItem = nullptr;
            }
        }
        return;
    }
    
    QGraphicsView::mousePressEvent(event);
}

void ImageViewer::mouseMoveEvent(QMouseEvent* event) {
    QPointF scenePos = mapToScene(event->pos());
    
    if (m_cropMode && m_moving) {
        QPointF delta = scenePos - m_lastPos;
        m_cropItem->moveBy(delta.x(), delta.y());
        m_lastPos = scenePos;
        return;
    }
    
    if (m_cropMode && m_drawing) {
         QPointF scenePos = mapToScene(event->pos());
         QRectF r(m_startPoint, scenePos);
         
        // Calculate raw vector
        float w = scenePos.x() - m_startPoint.x();
        float h = scenePos.y() - m_startPoint.y();
        
        // Enforce Aspect Ratio
        if (m_aspectRatio > 0.0f) {
            [[maybe_unused]] float signW = (w >= 0) ? 1.0f : -1.0f;
            float signH = (h >= 0) ? 1.0f : -1.0f;
            float absW = std::abs(w);
            float newH = absW / m_aspectRatio;
            h = newH * signH;
        }
        
        QRectF newRect = QRectF(m_startPoint.x(), m_startPoint.y(), w, h).normalized();
        m_cropItem->setRect(newRect);
        m_cropItem->setTransformOriginPoint(newRect.center());
        // Rotation is maintained via setRotation called earlier or persists
        return;
    }

    
    // Shift+Drag Update: same as Selection Mode
    if ((event->modifiers() & Qt::ShiftModifier) && m_drawing && m_queryRectItem) {
        QPointF scenePos = mapToScene(event->pos());
        QRectF newRect(m_startPoint, scenePos);
        m_queryRectItem->setRect(newRect.normalized());
        return;
    }

    // Selection Mode: update rectangle while dragging
    if (m_interactionMode == Mode_Selection && m_drawing && m_queryRectItem) {
        QPointF scenePos = mapToScene(event->pos());
        QRectF newRect(m_startPoint, scenePos);
        m_queryRectItem->setRect(newRect.normalized());
        return;
    }
    
    if (m_abeMode && m_lassoDrawing && m_currentLassoItem) {
         QPointF scenePos = mapToScene(event->pos());
         m_currentLassoPoly << scenePos;
         m_currentLassoItem->setPolygon(m_currentLassoPoly);
         return;
    }
    
    QGraphicsView::mouseMoveEvent(event);
}

void ImageViewer::mouseReleaseEvent(QMouseEvent* event) {
    // Shift+Drag Release
    if ((event->modifiers() & Qt::ShiftModifier) && m_drawing) {
        m_drawing = false;
        if (m_queryRectItem && !m_queryRectItem->rect().isEmpty()) {
            QRectF r = m_queryRectItem->rect().normalized();
            QRect rect = r.toRect();
            
            // Create New View
            if (rect.width() > 0 && rect.height() > 0) {
                 // Crop buffer
                 ImageBuffer newBuf = m_buffer; // Copy
                 newBuf.cropRotated(r.center().x(), r.center().y(), rect.width(), rect.height(), 0); // Crop NO rotation
                 emit requestNewView(newBuf, tr("Selection"));
            }
        }
        m_queryRectItem->setVisible(false); // Hide after use
        setDragMode(QGraphicsView::ScrollHandDrag); // Restore default
        return;
    }

    // Selection Mode: finalize selection
    if (m_interactionMode == Mode_Selection && m_drawing) {
        m_drawing = false;
        if (m_queryRectItem && !m_queryRectItem->rect().isEmpty()) {
            emit rectSelected(m_queryRectItem->rect());
        }
        return;
    }
    
    if (m_cropMode) {
        m_drawing = false;
        m_moving = false;
        setCursor(Qt::ArrowCursor); // Restore cursor
        return;
    }

    
    if (m_abeMode && m_lassoDrawing) {
        m_lassoDrawing = false;
        
        // Finalize if valid
        if (m_currentLassoPoly.size() > 3) {
            // Make permanent
            QGraphicsPolygonItem* finalItem = m_scene->addPolygon(m_currentLassoPoly, QPen(Qt::yellow, 2, Qt::DashLine), QBrush(QColor(255, 255, 0, 30)));
            finalItem->setZValue(15);
            m_abeItems.push_back(finalItem);
        }
        
        // Remove temp red
        if (m_currentLassoItem) {
            m_scene->removeItem(m_currentLassoItem);
            delete m_currentLassoItem;
            m_currentLassoItem = nullptr;
        }
        return;
    }
    

    
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageViewer::zoomIn() {
    scale(1.25, 1.25);
    m_scaleFactor = transform().m11();
    emit viewChanged(m_scaleFactor, horizontalScrollBar()->value(), verticalScrollBar()->value());
}

void ImageViewer::zoomOut() {
    scale(0.8, 0.8);
    m_scaleFactor = transform().m11();
    emit viewChanged(m_scaleFactor, horizontalScrollBar()->value(), verticalScrollBar()->value());
}

void ImageViewer::fitToWindow() {
    if (!m_imageItem->pixmap().isNull()) {
        fitInView(m_imageItem, Qt::KeepAspectRatio);
        m_scaleFactor = transform().m11();
        emit viewChanged(m_scaleFactor, horizontalScrollBar()->value(), verticalScrollBar()->value());
    }
}

void ImageViewer::zoom1to1() {
    setTransform(QTransform()); // Reset to identity (scale 1.0)
    m_scaleFactor = 1.0;
    emit viewChanged(m_scaleFactor, horizontalScrollBar()->value(), verticalScrollBar()->value());
}

void ImageViewer::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else {
        zoomOut();
    }
    event->accept();
    // Signal emitted by zoomIn/zoomOut
}
    
void ImageViewer::syncView(float scale, float hVal, float vVal) {
    QSignalBlocker blocker(this);
    
    if (std::abs(m_scaleFactor - scale) > 0.0001f) {
         setTransform(QTransform::fromScale(scale, scale));
         m_scaleFactor = scale;
    }
    
    horizontalScrollBar()->setValue(hVal);
    verticalScrollBar()->setValue(vVal);
    
    // Optional: force immediate visual update to prevent lag
    if (viewport()) viewport()->update();
}

void ImageViewer::setPickMode(bool active) {
    m_pickMode = active;
    if (active) {
        setRectQueryMode(false); // Exclusive
        setCursor(Qt::CrossCursor);
    } else if (!m_rectQueryMode) {
        setCursor(Qt::ArrowCursor);
    }
}

void ImageViewer::setRectQueryMode(bool active) {
    m_rectQueryMode = active;
    if (active) {
        setPickMode(false);
        setCursor(Qt::CrossCursor);
        if (!m_queryRectItem) {
            m_queryRectItem = m_scene->addRect(0,0,0,0, QPen(Qt::cyan, 1, Qt::DashLine));
            m_queryRectItem->setZValue(50);
        }
        m_queryRectItem->setVisible(true);
    } else {
        if (m_queryRectItem) m_queryRectItem->setVisible(false);
        setCursor(Qt::ArrowCursor);
    }
}

void ImageViewer::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewChanged(m_scaleFactor, horizontalScrollBar()->value(), verticalScrollBar()->value());
}



void ImageViewer::pushUndo() {
    if (m_useDeltaHistory && m_historyManager) {
        // Use new delta-based history system
        m_historyManager->pushUndo(m_buffer);
        // Also maintain legacy stack for compatibility
        if (m_undoStack.size() >= 20) m_undoStack.erase(m_undoStack.begin());
        m_undoStack.push_back(m_buffer);
        m_redoStack.clear();
    } else {
        // Legacy mode: full copies
        if (m_undoStack.size() >= 20) m_undoStack.erase(m_undoStack.begin());
        m_undoStack.push_back(m_buffer);
        m_redoStack.clear();
    }
    
    setModified(true);
    emit historyChanged();
}

void ImageViewer::undo() {
    int oldW = m_buffer.width();
    int oldH = m_buffer.height();

    if (m_useDeltaHistory && m_historyManager && m_historyManager->canUndo()) {
        // Use delta-based history
        if (!m_undoStack.empty()) {
            m_redoStack.push_back(m_buffer);
            m_buffer = m_undoStack.back();
            m_undoStack.pop_back();
            setImage(m_buffer.getDisplayImage(m_displayMode, m_displayLinked), true);
        }
    } else if (!m_undoStack.empty()) {
        // Legacy fallback
        m_redoStack.push_back(m_buffer);
        m_buffer = m_undoStack.back();
        m_undoStack.pop_back();
        setImage(m_buffer.getDisplayImage(m_displayMode, m_displayLinked), true);
    }
    
    if (m_buffer.width() != oldW || m_buffer.height() != oldH) {
        fitToWindow();
    }

    setModified(true);
    emit bufferChanged();
    emit historyChanged();
}

void ImageViewer::redo() {
    int oldW = m_buffer.width();
    int oldH = m_buffer.height();

    if (m_useDeltaHistory && m_historyManager && m_historyManager->canRedo()) {
        // Use delta-based history
        if (!m_redoStack.empty()) {
            m_undoStack.push_back(m_buffer);
            m_buffer = m_redoStack.back();
            m_redoStack.pop_back();
            setImage(m_buffer.getDisplayImage(m_displayMode, m_displayLinked), true);
        }
    } else if (!m_redoStack.empty()) {
        // Legacy fallback
        m_undoStack.push_back(m_buffer);
        m_buffer = m_redoStack.back();
        m_redoStack.pop_back();
        setImage(m_buffer.getDisplayImage(m_displayMode, m_displayLinked), true);
    }
    
    if (m_buffer.width() != oldW || m_buffer.height() != oldH) {
        fitToWindow();
    }

    setModified(true);
    emit bufferChanged();
    emit historyChanged();
}


bool ImageViewer::canUndo() const {
    return !m_undoStack.empty();
}

bool ImageViewer::canRedo() const {
    return !m_redoStack.empty();
}

int ImageViewer::getHBarLoc() const {
    return horizontalScrollBar()->value();
}

int ImageViewer::getVBarLoc() const {
    return verticalScrollBar()->value();
}

double ImageViewer::pixelScale() const {
    // Return arcsec/pixel from WCS CD matrix
    const auto& meta = m_buffer.metadata();
    double cd11 = meta.cd1_1;
    double cd21 = meta.cd2_1;
    double scale = std::sqrt(cd11 * cd11 + cd21 * cd21);
    return scale * 3600.0;  // Convert deg to arcsec
}

void ImageViewer::drawForeground([[maybe_unused]] QPainter* painter, [[maybe_unused]] const QRectF& rect) {
    // Legacy "Linked" indicator removed.
}
void ImageViewer::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        event->acceptProposedAction();
    } else {
        QGraphicsView::dragEnterEvent(event);
    }
}
void ImageViewer::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        event->acceptProposedAction();
    } else {
        QGraphicsView::dragMoveEvent(event);
    }
}


void ImageViewer::dropEvent(QDropEvent* event) {
    // Find parent CustomMdiSubWindow and delegate drop
    QWidget* p = parentWidget();
    while (p && !p->inherits("CustomMdiSubWindow")) {
        p = p->parentWidget();
    }
    
    if (p) {
        // We know it's a CustomMdiSubWindow from the search loop
        static_cast<CustomMdiSubWindow*>(p)->handleDrop(event);
    } else {
        QGraphicsView::dropEvent(event);
    }
}

void ImageViewer::setModified(bool modified) {
    if (m_isModified == modified) return;
    m_isModified = modified;
    emit modifiedChanged(m_isModified);
}
