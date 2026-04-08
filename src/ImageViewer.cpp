// =============================================================================
// ImageViewer.cpp
//
// Implementation of the ImageViewer widget, a QGraphicsView-based image display
// component supporting pan/zoom, crop, lasso selection, pixel inspection,
// magnifier loupe, undo/redo history, view linking, and drag-and-drop.
// =============================================================================

#include "ImageViewer.h"

#include <QWheelEvent>
#include <QScrollBar>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QToolButton>
#include <QResizeEvent>
#include <QIcon>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QSettings>

#include "widgets/CustomMdiSubWindow.h"
#include "ImageBufferDelta.h"
#include "core/Logger.h"


// =============================================================================
// Constructor / Destructor
// =============================================================================

ImageViewer::ImageViewer(QWidget* parent)
    : QGraphicsView(parent)
{
    // Scene setup
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    // Delta-based undo/redo history manager
    m_historyManager = std::make_unique<ImageHistoryManager>();

    // Primary image item; initially holds an empty pixmap
    m_imageItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_imageItem);

    // View configuration
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QBrush(QColor(30, 30, 30)));
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);
    setMouseTracking(true);

    // Zoom anchored under the mouse cursor
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    // Accept drops for view-linking via Ctrl+Drag
    setAcceptDrops(true);
    if (viewport())
        viewport()->setAcceptDrops(true);

    // ---------------------------------------------------------------------------
    // Crop overlay item (dashed yellow rectangle, hidden by default)
    // ---------------------------------------------------------------------------
    m_cropItem = new QGraphicsRectItem();
    m_cropItem->setPen(QPen(Qt::yellow, 2, Qt::DashLine));
    m_cropItem->setBrush(QBrush(QColor(255, 255, 0, 30)));
    m_cropItem->setVisible(false);
    m_cropItem->setZValue(10);
    m_scene->addItem(m_cropItem);

    // ---------------------------------------------------------------------------
    // Query / selection rectangle overlay (dashed yellow, hidden by default)
    // ---------------------------------------------------------------------------
    m_queryRectItem = new QGraphicsRectItem();
    m_queryRectItem->setPen(QPen(Qt::yellow, 2, Qt::DashLine));
    m_queryRectItem->setBrush(QBrush(QColor(255, 255, 0, 30)));
    m_queryRectItem->setZValue(20);
    m_queryRectItem->setVisible(false);
    m_scene->addItem(m_queryRectItem);

    // Enable mouse tracking on the viewport for magnifier and pixel info
    if (viewport())
        viewport()->setMouseTracking(true);
}

ImageViewer::~ImageViewer()
{
    qDebug() << "[ImageViewer::~ImageViewer] Destroying viewer:" << this;

    if (m_isLinked)
        emit unlinked();
}


// =============================================================================
// Interaction Mode
// =============================================================================

void ImageViewer::setInteractionMode(InteractionMode mode)
{
    m_interactionMode = mode;

    if (mode == Mode_Selection)
    {
        setDragMode(QGraphicsView::NoDrag);
        setCursor(Qt::CrossCursor);
    }
    else
    {
        setDragMode(QGraphicsView::ScrollHandDrag);
        setCursor(Qt::ArrowCursor);
        m_queryRectItem->setVisible(false);
    }
}


// =============================================================================
// Display / Buffer Management
// =============================================================================

void ImageViewer::setPreviewLUT(const std::vector<std::vector<float>>& luts)
{
    m_previewLUT = luts;

    // Use full-resolution preview without downsampling
    const std::vector<std::vector<float>>* pLut =
        m_previewLUT.empty() ? nullptr : &m_previewLUT;

    QImage img = m_buffer.getDisplayImage(
        m_displayMode, m_displayLinked, pLut, 0, 0,
        m_showMaskOverlay, m_displayInverted,
        m_displayFalseColor, m_autoStretchMedian, m_channelView);

    setImage(img, true);
}

void ImageViewer::setPreviewImage(const QImage& img)
{
    if (img.isNull())
        return;

    m_imageItem->setPixmap(QPixmap::fromImage(img));

    // Scale the preview to match the current scene rectangle so it aligns
    // with the original image geometry
    QRectF sceneR = m_scene->sceneRect();
    if (sceneR.width() > 0 && sceneR.height() > 0)
    {
        qreal sx = sceneR.width()  / img.width();
        qreal sy = sceneR.height() / img.height();
        m_imageItem->setTransform(QTransform::fromScale(sx, sy));
    }
}

void ImageViewer::clearPreviewLUT()
{
    m_previewLUT.clear();
    refreshDisplay(true);
}

void ImageViewer::setDisplayState(ImageBuffer::DisplayMode mode, bool linked)
{
    m_displayMode   = mode;
    m_displayLinked = linked;
    refreshDisplay(true);
}

void ImageViewer::restoreState(const ImageBuffer& buffer,
                               ImageBuffer::DisplayMode mode,
                               bool linked)
{
    m_buffer        = buffer;
    m_displayMode   = mode;
    m_displayLinked = linked;
    m_previewLUT.clear();
    refreshDisplay(true);
}

void ImageViewer::setAutoStretchMedian(float median)
{
    if (qFuzzyCompare(m_autoStretchMedian, median))
        return;

    m_autoStretchMedian = median;
    refreshDisplay(true);
}

void ImageViewer::setInverted(bool inverted)
{
    if (m_displayInverted == inverted)
        return;

    m_displayInverted = inverted;
    refreshDisplay(true);
}

void ImageViewer::setFalseColor(bool falseColor)
{
    if (m_displayFalseColor == falseColor)
        return;

    m_displayFalseColor = falseColor;
    refreshDisplay(true);
}

void ImageViewer::setChannelView(ImageBuffer::ChannelView cv)
{
    if (m_channelView == cv)
        return;

    m_channelView = cv;
    refreshDisplay(true);
}

void ImageViewer::setImage(const QImage& image, bool preserveView)
{
    m_displayImage = image;
    m_imageItem->setPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(image.rect());

    if (!preserveView)
        fitToWindow();
    else
        m_scaleFactor = transform().m11();
}

void ImageViewer::setBuffer(const ImageBuffer& buffer,
                            const QString& name,
                            bool preserveView)
{
    m_buffer = buffer;

    // Clear any stale preview LUT when the buffer is replaced.
    // A leftover LUT from a previous state would cause double-processing
    // if refreshDisplay() is invoked later (e.g. on resize).
    m_previewLUT.clear();

    QImage img = m_buffer.getDisplayImage(
        m_displayMode, m_displayLinked, nullptr, 0, 0,
        m_showMaskOverlay, m_displayInverted,
        m_displayFalseColor, m_autoStretchMedian, m_channelView);

    if (!name.isEmpty())
        setWindowTitle(name);

    setImage(img, preserveView);
}

void ImageViewer::refreshDisplay(bool preserveView)
{
    const std::vector<std::vector<float>>* pLut =
        m_previewLUT.empty() ? nullptr : &m_previewLUT;

    QImage img = m_buffer.getDisplayImage(
        m_displayMode, m_displayLinked, pLut, 0, 0,
        m_showMaskOverlay, m_displayInverted,
        m_displayFalseColor, m_autoStretchMedian, m_channelView);

    setImage(img, preserveView);
}


// =============================================================================
// Selection Rectangle
// =============================================================================

QRectF ImageViewer::getSelectionRect() const
{
    if (m_queryRectItem && m_queryRectItem->isVisible())
        return m_queryRectItem->rect();

    return QRectF();
}

void ImageViewer::clearSelection()
{
    if (m_queryRectItem)
    {
        m_queryRectItem->setRect(QRectF());
        m_queryRectItem->setVisible(false);
    }
}


// =============================================================================
// Crop Mode
// =============================================================================

void ImageViewer::setCropMode(bool active)
{
    m_cropMode = active;

    if (active)
    {
        setDragMode(QGraphicsView::NoDrag);
        m_cropItem->setRect(0, 0, 0, 0);
        m_cropItem->setRotation(0);
        m_cropAngle = 0;
        m_cropItem->setVisible(true);
    }
    else
    {
        setDragMode(QGraphicsView::ScrollHandDrag);
        m_cropItem->setVisible(false);
    }
}

void ImageViewer::setCropAngle(float angle)
{
    m_cropAngle = angle;

    if (m_cropItem->isVisible())
    {
        QRectF r = m_cropItem->rect();
        m_cropItem->setTransformOriginPoint(r.center());
        m_cropItem->setRotation(angle);
    }
}

void ImageViewer::getCropState(float& cx, float& cy,
                               float& w,  float& h,
                               float& angle) const
{
    if (!m_cropItem)
        return;

    QRectF   r = m_cropItem->rect();
    QPointF  c = m_cropItem->mapToScene(r.center());

    cx    = c.x();
    cy    = c.y();
    w     = r.width();
    h     = r.height();
    angle = m_cropAngle;
}

void ImageViewer::setAspectRatio(float ratio)
{
    m_aspectRatio = ratio;

    // Immediately adjust the current crop rectangle if one is active
    if (!m_cropMode || !m_cropItem->isVisible() ||
        m_cropItem->rect().width() <= 0 || ratio <= 0.0f)
        return;

    QRectF  r = m_cropItem->rect();
    float   w = r.width();
    float   h = r.height();

    if (w / h > ratio)
        w = h * ratio;      // Rectangle is too wide; constrain width
    else
        h = w / ratio;      // Rectangle is too tall; constrain height

    QPointF c = r.center();
    QRectF  newRect(0, 0, w, h);
    newRect.moveCenter(c);

    // Clamp the adjusted rectangle to the image bounds (unrotated approximation)
    if (m_imageItem)
    {
        QRectF imgRect = m_imageItem->boundingRect();

        if (newRect.width() > imgRect.width())
        {
            newRect.setWidth(imgRect.width());
            newRect.setHeight(imgRect.width() / ratio);
        }
        if (newRect.height() > imgRect.height())
        {
            newRect.setHeight(imgRect.height());
            newRect.setWidth(imgRect.height() * ratio);
        }

        newRect.moveCenter(c);

        if (newRect.left()   < imgRect.left())   newRect.moveLeft(imgRect.left());
        if (newRect.right()  > imgRect.right())  newRect.moveRight(imgRect.right());
        if (newRect.top()    < imgRect.top())     newRect.moveTop(imgRect.top());
        if (newRect.bottom() > imgRect.bottom()) newRect.moveBottom(imgRect.bottom());
    }

    m_cropItem->setRect(newRect);
    m_cropItem->setTransformOriginPoint(newRect.center());
}

ImageViewer::CropDragMode ImageViewer::getCropDragMode(const QPointF& itemPos,
                                                       const QRectF&  rect,
                                                       float          tolerance) const
{
    if (!rect.isValid() || rect.isEmpty())
        return CropDrag_None;

    // Scale the hit-test tolerance to remain visually consistent at any zoom level
    float scaledTol = tolerance / m_scaleFactor;

    QRectF outer = rect.adjusted(-scaledTol, -scaledTol, scaledTol, scaledTol);
    if (!outer.contains(itemPos))
        return CropDrag_None;

    bool left   = std::abs(itemPos.x() - rect.left())   <= scaledTol;
    bool right  = std::abs(itemPos.x() - rect.right())  <= scaledTol;
    bool top    = std::abs(itemPos.y() - rect.top())    <= scaledTol;
    bool bottom = std::abs(itemPos.y() - rect.bottom()) <= scaledTol;

    if (top    && left)  return CropDrag_TopLeft;
    if (top    && right) return CropDrag_TopRight;
    if (bottom && left)  return CropDrag_BottomLeft;
    if (bottom && right) return CropDrag_BottomRight;
    if (left)            return CropDrag_Left;
    if (right)           return CropDrag_Right;
    if (top)             return CropDrag_Top;
    if (bottom)          return CropDrag_Bottom;
    if (rect.contains(itemPos)) return CropDrag_Move;

    return CropDrag_None;
}

void ImageViewer::updateCropCursor(CropDragMode mode, float rotation)
{
    if (mode == CropDrag_None)
    {
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (mode == CropDrag_Move)
    {
        setCursor(Qt::SizeAllCursor);
        return;
    }

    // Map each handle to a base angle (degrees, unrotated)
    float baseAngle = 0.0f;
    switch (mode)
    {
        case CropDrag_Top:         baseAngle =   0.0f; break;
        case CropDrag_TopRight:    baseAngle =  45.0f; break;
        case CropDrag_Right:       baseAngle =  90.0f; break;
        case CropDrag_BottomRight: baseAngle = 135.0f; break;
        case CropDrag_Bottom:      baseAngle = 180.0f; break;
        case CropDrag_BottomLeft:  baseAngle = 225.0f; break;
        case CropDrag_Left:        baseAngle = 270.0f; break;
        case CropDrag_TopLeft:     baseAngle = 315.0f; break;
        default: break;
    }

    float totalAngle = std::fmod(baseAngle + rotation, 360.0f);
    if (totalAngle < 0.0f)
        totalAngle += 360.0f;

    // Collapse to a 0-180 range and select the closest axis-aligned cursor
    float a = std::fmod(totalAngle, 180.0f);

    if      (a <  22.5f || a >= 157.5f) setCursor(Qt::SizeVerCursor);
    else if (a <  67.5f)                setCursor(Qt::SizeBDiagCursor);
    else if (a < 112.5f)                setCursor(Qt::SizeHorCursor);
    else                                setCursor(Qt::SizeFDiagCursor);
}


// =============================================================================
// ABE (Adaptive Background Extraction) Polygon Mode
// =============================================================================

void ImageViewer::setAbeMode(bool active)
{
    m_abeMode = active;

    if (active)
    {
        setDragMode(QGraphicsView::NoDrag);
        setCursor(Qt::CrossCursor);
        for (auto* item : m_abeItems)
            item->setVisible(true);
    }
    else
    {
        setDragMode(QGraphicsView::ScrollHandDrag);
        setCursor(Qt::ArrowCursor);
        for (auto* item : m_abeItems)
            item->setVisible(false);
    }
}

void ImageViewer::clearAbePolygons()
{
    for (auto* item : m_abeItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_abeItems.clear();

    if (m_currentLassoItem)
    {
        m_scene->removeItem(m_currentLassoItem);
        delete m_currentLassoItem;
        m_currentLassoItem = nullptr;
    }
}

std::vector<QPolygonF> ImageViewer::getAbePolygons() const
{
    std::vector<QPolygonF> polys;
    polys.reserve(m_abeItems.size());

    for (auto* item : m_abeItems)
        polys.push_back(item->polygon());

    return polys;
}


// =============================================================================
// Background Sample Points
// =============================================================================

void ImageViewer::setBackgroundSamples(const std::vector<QPointF>& points)
{
    clearBackgroundSamples();

    for (const auto& p : points)
    {
        QGraphicsEllipseItem* item = m_scene->addEllipse(
            p.x() - 5, p.y() - 5, 10, 10,
            QPen(Qt::green, 1),
            QBrush(QColor(0, 255, 0, 100)));

        item->setZValue(30);
        m_sampleItems.push_back(item);
    }
}

std::vector<QPointF> ImageViewer::getBackgroundSamples() const
{
    std::vector<QPointF> points;
    points.reserve(m_sampleItems.size());

    for (auto* item : m_sampleItems)
        points.push_back(item->mapToScene(item->rect().center()));

    return points;
}

void ImageViewer::clearBackgroundSamples()
{
    for (auto* item : m_sampleItems)
    {
        m_scene->removeItem(item);
        delete item;
    }
    m_sampleItems.clear();
}


// =============================================================================
// View Linking
// =============================================================================

void ImageViewer::setLinked(bool linked)
{
    if (m_isLinked == linked)
        return;

    m_isLinked = linked;
    update();

    emit viewChanged(m_scaleFactor,
                     horizontalScrollBar()->value(),
                     verticalScrollBar()->value());

    if (!linked)
        emit unlinked();
}

void ImageViewer::syncView(float scale, float hVal, float vVal)
{
    QSignalBlocker blocker(this);

    if (std::abs(m_scaleFactor - scale) > 0.0001f)
    {
        setTransform(QTransform::fromScale(scale, scale));
        m_scaleFactor = scale;
    }

    horizontalScrollBar()->setValue(hVal);
    verticalScrollBar()->setValue(vVal);

    if (viewport())
        viewport()->update();
}


// =============================================================================
// Zoom Controls
// =============================================================================

void ImageViewer::zoomIn()
{
    qreal currentScale = transform().m11();
    qreal nextScale    = currentScale * 1.25;

    if (nextScale > ZOOM_MAX)
        nextScale = ZOOM_MAX;

    qreal factor = nextScale / currentScale;
    scale(factor, factor);

    m_scaleFactor = transform().m11();
    emit viewChanged(m_scaleFactor,
                     horizontalScrollBar()->value(),
                     verticalScrollBar()->value());
}

void ImageViewer::zoomOut()
{
    scale(0.8, 0.8);

    m_scaleFactor = transform().m11();
    emit viewChanged(m_scaleFactor,
                     horizontalScrollBar()->value(),
                     verticalScrollBar()->value());
}

void ImageViewer::fitToWindow()
{
    if (m_imageItem->pixmap().isNull())
        return;

    fitInView(m_imageItem, Qt::KeepAspectRatio);

    // Apply a small safety margin so the image does not render edge-to-edge
    // inside a subwindow, which would impair readability
    scale(0.98, 0.98);

    m_scaleFactor = transform().m11();

    if (m_scaleFactor > ZOOM_MAX)
    {
        qreal factor = ZOOM_MAX / m_scaleFactor;
        scale(factor, factor);
        m_scaleFactor = transform().m11();
    }

    emit viewChanged(m_scaleFactor,
                     horizontalScrollBar()->value(),
                     verticalScrollBar()->value());
}

void ImageViewer::zoom1to1()
{
    setTransform(QTransform());
    m_scaleFactor = 1.0;

    emit viewChanged(m_scaleFactor,
                     horizontalScrollBar()->value(),
                     verticalScrollBar()->value());
}


// =============================================================================
// Pick / Rect-Query Modes
// =============================================================================

void ImageViewer::setPickMode(bool active)
{
    m_pickMode = active;

    if (active)
    {
        setRectQueryMode(false);    // Mutually exclusive with rect-query
        setCursor(Qt::CrossCursor);
    }
    else if (!m_rectQueryMode)
    {
        setCursor(Qt::ArrowCursor);
    }
}

void ImageViewer::setRectQueryMode(bool active)
{
    m_rectQueryMode = active;

    if (active)
    {
        setPickMode(false);
        setCursor(Qt::CrossCursor);

        if (!m_queryRectItem)
        {
            m_queryRectItem = m_scene->addRect(0, 0, 0, 0,
                                               QPen(Qt::cyan, 1, Qt::DashLine));
            m_queryRectItem->setZValue(50);
        }
        m_queryRectItem->setVisible(true);
    }
    else
    {
        if (m_queryRectItem)
            m_queryRectItem->setVisible(false);
        setCursor(Qt::ArrowCursor);
    }
}


// =============================================================================
// Undo / Redo
// =============================================================================

void ImageViewer::pushUndo(const QString& description)
{
    // Enforce a maximum history depth of 20 states
    auto trimStack = [](auto& stack, auto& descs)
    {
        if (stack.size() >= 20)
        {
            stack.erase(stack.begin());
            descs.erase(descs.begin());
        }
    };

    if (m_useDeltaHistory && m_historyManager)
    {
        m_historyManager->pushUndo(m_buffer, description);

        // Maintain the legacy stack in parallel until the delta system
        // fully handles ImageBuffer content restoration
        trimStack(m_undoStack, m_undoDescriptions);
    }
    else
    {
        trimStack(m_undoStack, m_undoDescriptions);
    }

    if (!description.isEmpty())
        m_buffer.addHistoryAction(description);

    m_undoStack.push_back(m_buffer);
    m_undoDescriptions.push_back(description);
    m_redoStack.clear();
    m_redoDescriptions.clear();

    setModified(true);
    emit historyChanged();
}

void ImageViewer::undo(bool emitSignals)
{
    int oldW = m_buffer.width();
    int oldH = m_buffer.height();

    if (m_useDeltaHistory && m_historyManager && m_historyManager->canUndo())
    {
        m_redoStack.push_back(m_buffer);
        
        // Ensure we retrieve the description from the viewer's synchronized list
        QString desc = m_undoDescriptions.empty() ? m_historyManager->getUndoDescription() : m_undoDescriptions.back();
        m_redoDescriptions.push_back(desc);
        
        m_historyManager->performUndo();

        if (!m_undoStack.empty())
        {
            m_buffer = m_undoStack.back();
            m_undoStack.pop_back();
            m_undoDescriptions.pop_back();
            refreshDisplay(true);
        }
    }
    else if (!m_undoStack.empty())
    {
        m_redoStack.push_back(m_buffer);
        m_redoDescriptions.push_back(m_undoDescriptions.back());

        m_buffer = m_undoStack.back();
        m_undoStack.pop_back();
        m_undoDescriptions.pop_back();
        refreshDisplay(true);
    }

    if (emitSignals && (m_buffer.width() != oldW || m_buffer.height() != oldH))
        fitToWindow();

    setModified(true);
    if (emitSignals) {
        emit bufferChanged();
        emit historyChanged();
    }
}

void ImageViewer::redo(bool emitSignals)
{
    int oldW = m_buffer.width();
    int oldH = m_buffer.height();

    if (m_useDeltaHistory && m_historyManager && m_historyManager->canRedo())
    {
        m_undoStack.push_back(m_buffer);

        // Fetch description carefully from synchronized list
        QString desc = m_redoDescriptions.empty() ? m_historyManager->getRedoDescription() : m_redoDescriptions.back();
        m_undoDescriptions.push_back(desc);

        m_historyManager->performRedo();

        if (!m_redoStack.empty())
        {
            m_buffer = m_redoStack.back();
            m_redoStack.pop_back();
            m_redoDescriptions.pop_back();
            refreshDisplay(true);
        }
    }
    else if (!m_redoStack.empty())
    {
        m_undoStack.push_back(m_buffer);
        m_undoDescriptions.push_back(m_redoDescriptions.back());

        m_buffer = m_redoStack.back();
        m_redoStack.pop_back();
        m_redoDescriptions.pop_back();
        refreshDisplay(true);
    }

    if (emitSignals && (m_buffer.width() != oldW || m_buffer.height() != oldH))
        fitToWindow();

    setModified(true);
    if (emitSignals) {
        emit bufferChanged();
        emit historyChanged();
    }
}

void ImageViewer::jumpToHistoryState(int index)
{
    if (index < 0) return;

    int currentStateIndex = static_cast<int>(m_undoDescriptions.size());
    int maxIndex = currentStateIndex + static_cast<int>(m_redoDescriptions.size());
    if (index > maxIndex) {
        index = maxIndex; // Clamp to safe max
    }

    if (index == currentStateIndex) {
        return;
    }

    if (index < currentStateIndex) {
        int steps = currentStateIndex - index;
        for (int i = 0; i < steps; ++i) {
            undo(false);
        }
    } else {
        int steps = index - currentStateIndex;
        for (int i = 0; i < steps; ++i) {
            redo(false);
        }
    }

    fitToWindow();
    emit bufferChanged();
    emit historyChanged();
}

QString ImageViewer::getUndoDescription() const
{
    if (m_undoDescriptions.empty())
        return QString();

    return m_undoDescriptions.back();
}

QString ImageViewer::getRedoDescription() const
{
    if (m_redoDescriptions.empty())
        return QString();

    return m_redoDescriptions.back();
}

bool ImageViewer::canUndo() const { return !m_undoStack.empty(); }
bool ImageViewer::canRedo() const { return !m_redoStack.empty(); }

QVector<ImageBuffer> ImageViewer::undoHistory() const
{
    QVector<ImageBuffer> out;
    out.reserve(static_cast<int>(m_undoStack.size()));
    for (const auto& item : m_undoStack)
        out.push_back(item);
    return out;
}

QVector<ImageBuffer> ImageViewer::redoHistory() const
{
    QVector<ImageBuffer> out;
    out.reserve(static_cast<int>(m_redoStack.size()));
    for (const auto& item : m_redoStack)
        out.push_back(item);
    return out;
}

void ImageViewer::setHistory(const QVector<ImageBuffer>& undoHistory,
                             const QVector<ImageBuffer>& redoHistory)
{
    m_undoStack.clear();
    m_redoStack.clear();

    m_undoStack.reserve(static_cast<size_t>(undoHistory.size()));
    for (const auto& item : undoHistory)
        m_undoStack.push_back(item);

    m_redoStack.reserve(static_cast<size_t>(redoHistory.size()));
    for (const auto& item : redoHistory)
        m_redoStack.push_back(item);

    emit historyChanged();
}


// =============================================================================
// Accessors
// =============================================================================

int    ImageViewer::getHBarLoc()   const { return horizontalScrollBar()->value(); }
int    ImageViewer::getVBarLoc()   const { return verticalScrollBar()->value();   }

double ImageViewer::pixelScale() const
{
    // Derive the plate scale in arcseconds per pixel from the WCS CD matrix
    const auto& meta = m_buffer.metadata();
    double scale = std::sqrt(meta.cd1_1 * meta.cd1_1 + meta.cd2_1 * meta.cd2_1);
    return scale * 3600.0;  // Convert degrees to arcseconds
}

void ImageViewer::setModified(bool modified)
{
    if (m_isModified == modified)
        return;

    m_isModified = modified;
    emit modifiedChanged(m_isModified);
}


// =============================================================================
// Qt Event Overrides - Window / Scroll
// =============================================================================

void ImageViewer::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    emit resized();
}

void ImageViewer::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewChanged(m_scaleFactor,
                     horizontalScrollBar()->value(),
                     verticalScrollBar()->value());
}

void ImageViewer::enterEvent(QEnterEvent* event)
{
    m_cursorOverViewport = true;
    QGraphicsView::enterEvent(event);
}

void ImageViewer::leaveEvent(QEvent* event)
{
    m_cursorOverViewport = false;
    m_magnifierVisible   = false;

    viewport()->unsetCursor();
    viewport()->update();

    QGraphicsView::leaveEvent(event);
}


// =============================================================================
// Qt Event Overrides - Mouse
// =============================================================================

void ImageViewer::mousePressEvent(QMouseEvent* event)
{
    // -------------------------------------------------------------------------
    // Point-pick mode: emit the scene coordinate and consume the event
    // -------------------------------------------------------------------------
    if (m_pickMode && event->button() == Qt::LeftButton)
    {
        emit pointPicked(mapToScene(event->pos()));
        return;
    }

    // -------------------------------------------------------------------------
    // Sample-point dragging: allow the user to reposition an existing sample
    // when not in a conflicting drawing mode
    // -------------------------------------------------------------------------
    if (!m_sampleItems.empty()       &&
        event->button() == Qt::LeftButton &&
        !m_drawing && !m_lassoDrawing)
    {
        QPointF scenePos = mapToScene(event->pos());
        for (auto* item : m_sampleItems)
        {
            if (item->contains(item->mapFromScene(scenePos)))
            {
                m_movingSample = item;
                m_lastPos      = scenePos;
                setCursor(Qt::SizeAllCursor);
                return;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Shift+Drag: rubber-band a region and open it as a new view
    // -------------------------------------------------------------------------
    if ((event->modifiers() & Qt::ShiftModifier) &&
        event->button() == Qt::LeftButton)
    {
        setDragMode(QGraphicsView::NoDrag);
        m_startPoint = mapToScene(event->pos());
        m_drawing    = true;

        m_queryRectItem->setPen(QPen(Qt::yellow, 2, Qt::DashLine));
        m_queryRectItem->setRect(QRectF(m_startPoint, m_startPoint));
        m_queryRectItem->setVisible(true);
        return;
    }

    // -------------------------------------------------------------------------
    // Selection mode
    // -------------------------------------------------------------------------
    if (m_interactionMode == Mode_Selection && event->button() == Qt::LeftButton)
    {
        if (event->modifiers() & Qt::ControlModifier)
        {
            // Ctrl held: temporarily revert to panning
            setDragMode(QGraphicsView::ScrollHandDrag);
            QGraphicsView::mousePressEvent(event);
        }
        else
        {
            setDragMode(QGraphicsView::NoDrag);
            m_startPoint = mapToScene(event->pos());
            m_drawing    = true;
            m_queryRectItem->setRect(QRectF(m_startPoint, m_startPoint));
            m_queryRectItem->setVisible(true);
        }
        return;
    }

    // -------------------------------------------------------------------------
    // Legacy rect-query mode
    // -------------------------------------------------------------------------
    if (m_rectQueryMode && event->button() == Qt::LeftButton)
    {
        m_startPoint = mapToScene(event->pos());
        m_drawing    = true;
        m_queryRectItem->setRect(QRectF(m_startPoint, m_startPoint));
        m_queryRectItem->setVisible(true);
        return;
    }

    // -------------------------------------------------------------------------
    // Crop mode: either start a drag operation on an existing handle or begin
    // drawing a new crop rectangle
    // -------------------------------------------------------------------------
    if (m_cropMode && event->button() == Qt::LeftButton)
    {
        QPointF scenePos = mapToScene(event->pos());
        QPointF itemPos  = m_cropItem->mapFromScene(scenePos);

        if (m_cropItem->isVisible())
        {
            m_cropDragMode = getCropDragMode(itemPos, m_cropItem->rect());

            if (m_cropDragMode != CropDrag_None)
            {
                m_moving  = true;
                m_lastPos = scenePos;
                updateCropCursor(m_cropDragMode, m_cropAngle);
                return;
            }
        }

        // Click fell outside any existing handle: start a new rectangle
        m_startPoint   = scenePos;
        m_drawing      = true;
        m_cropDragMode = CropDrag_None;

        m_cropItem->setRect(QRectF(scenePos, scenePos));
        m_cropItem->setPos(0, 0);
        m_cropItem->setRotation(m_cropAngle);
        setCursor(Qt::CrossCursor);
        return;
    }

    // -------------------------------------------------------------------------
    // ABE lasso mode
    // -------------------------------------------------------------------------
    if (m_abeMode && event->button() == Qt::LeftButton)
    {
        QPointF scenePos = mapToScene(event->pos());
        m_lassoDrawing = true;
        m_currentLassoPoly.clear();
        m_currentLassoPoly << scenePos;

        if (m_currentLassoItem)
        {
            m_scene->removeItem(m_currentLassoItem);
            delete m_currentLassoItem;
        }

        m_currentLassoItem = m_scene->addPolygon(
            m_currentLassoPoly,
            QPen(Qt::yellow, 2, Qt::DashLine),
            QBrush(QColor(255, 255, 0, 30)));
        m_currentLassoItem->setZValue(20);
        return;
    }

    if (m_abeMode && event->button() == Qt::RightButton)
    {
        // Right-click cancels an in-progress lasso stroke
        if (m_lassoDrawing)
        {
            m_lassoDrawing = false;

            if (m_currentLassoItem)
            {
                m_scene->removeItem(m_currentLassoItem);
                delete m_currentLassoItem;
                m_currentLassoItem = nullptr;
            }
        }
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void ImageViewer::mouseMoveEvent(QMouseEvent* event)
{
    QPointF scenePos = mapToScene(event->pos());

    // -------------------------------------------------------------------------
    // Magnifier state: must be evaluated before any early returns so that the
    // loupe is correctly hidden during drawing and editing operations
    // -------------------------------------------------------------------------
    if (m_imageItem && !m_imageItem->pixmap().isNull())
    {
        bool overImage     = m_imageItem->boundingRect().contains(scenePos);
        bool inDrawingMode = m_drawing || m_moving || m_lassoDrawing || m_movingSample;

        bool hideMagnifier = QSettings().value("display/hide_magnifier", false).toBool();

        if (overImage && m_cursorOverViewport && !inDrawingMode && !hideMagnifier)
        {
            m_magnifierScenePos    = scenePos;
            m_magnifierViewportPos = event->pos();
            m_magnifierVisible     = true;
        }
        else
        {
            m_magnifierVisible = false;
        }

        // Update viewport cursor for PanZoom mode when not in any overlay mode
        bool baseMode = (m_interactionMode == Mode_PanZoom &&
                         !m_cropMode && !m_abeMode &&
                         !m_pickMode && !m_rectQueryMode);

        if (overImage && m_cursorOverViewport && !inDrawingMode)
        {
            if (baseMode) viewport()->setCursor(Qt::CrossCursor);
        }
        else
        {
            if (baseMode) viewport()->unsetCursor();
        }

        // Force an immediate redraw when entering a drawing state so the
        // magnifier disappears without delay
        if (inDrawingMode)
            viewport()->update();
    }

    // -------------------------------------------------------------------------
    // Sample-point drag
    // -------------------------------------------------------------------------
    if (m_movingSample)
    {
        QPointF delta = scenePos - m_lastPos;
        m_movingSample->moveBy(delta.x(), delta.y());
        m_lastPos = scenePos;
        return;
    }

    // -------------------------------------------------------------------------
    // Crop resize (any handle except Move)
    // -------------------------------------------------------------------------
    if (m_cropMode && m_moving &&
        m_cropDragMode != CropDrag_None &&
        m_cropDragMode != CropDrag_Move)
    {
        QPointF itemDelta = m_cropItem->mapFromScene(scenePos)
                          - m_cropItem->mapFromScene(m_lastPos);
        QRectF  rect = m_cropItem->rect();

        float dx = itemDelta.x();
        float dy = itemDelta.y();

        float newLeft   = rect.left();
        float newRight  = rect.right();
        float newTop    = rect.top();
        float newBottom = rect.bottom();

        if (m_cropDragMode == CropDrag_Left   ||
            m_cropDragMode == CropDrag_TopLeft ||
            m_cropDragMode == CropDrag_BottomLeft)
            newLeft += dx;

        if (m_cropDragMode == CropDrag_Right    ||
            m_cropDragMode == CropDrag_TopRight  ||
            m_cropDragMode == CropDrag_BottomRight)
            newRight += dx;

        if (m_cropDragMode == CropDrag_Top     ||
            m_cropDragMode == CropDrag_TopLeft  ||
            m_cropDragMode == CropDrag_TopRight)
            newTop += dy;

        if (m_cropDragMode == CropDrag_Bottom      ||
            m_cropDragMode == CropDrag_BottomLeft   ||
            m_cropDragMode == CropDrag_BottomRight)
            newBottom += dy;

        // Prevent the rectangle from collapsing or flipping
        if (newRight - newLeft < 1.0f)
        {
            bool leftSide = (m_cropDragMode == CropDrag_Left     ||
                             m_cropDragMode == CropDrag_TopLeft   ||
                             m_cropDragMode == CropDrag_BottomLeft);
            if (leftSide) newLeft  = newRight - 1.0f;
            else          newRight = newLeft  + 1.0f;
        }
        if (newBottom - newTop < 1.0f)
        {
            bool topSide = (m_cropDragMode == CropDrag_Top     ||
                            m_cropDragMode == CropDrag_TopLeft  ||
                            m_cropDragMode == CropDrag_TopRight);
            if (topSide) newTop    = newBottom - 1.0f;
            else         newBottom = newTop    + 1.0f;
        }

        QRectF newRect(QPointF(newLeft, newTop), QPointF(newRight, newBottom));

        // Enforce aspect ratio if one is set
        if (m_aspectRatio > 0.0f)
        {
            float w = newRect.width();
            float h = newRect.height();

            bool pulledX = (m_cropDragMode == CropDrag_Left  ||
                            m_cropDragMode == CropDrag_Right);
            bool pulledY = (m_cropDragMode == CropDrag_Top   ||
                            m_cropDragMode == CropDrag_Bottom);

            if (pulledY && !pulledX)
            {
                float targetW = h * m_aspectRatio;
                float diffW   = targetW - w;
                newRect.adjust(-diffW / 2.0f, 0, diffW / 2.0f, 0);
            }
            else
            {
                float targetH = w / m_aspectRatio;
                float diffH   = targetH - h;

                bool topSide = (m_cropDragMode == CropDrag_Top     ||
                                m_cropDragMode == CropDrag_TopLeft  ||
                                m_cropDragMode == CropDrag_TopRight);
                bool bottomSide = (m_cropDragMode == CropDrag_Bottom      ||
                                   m_cropDragMode == CropDrag_BottomLeft   ||
                                   m_cropDragMode == CropDrag_BottomRight);

                if      (topSide)    newRect.setTop(newRect.top()       - diffH);
                else if (bottomSide) newRect.setBottom(newRect.bottom() + diffH);
                else                 newRect.adjust(0, -diffH / 2.0f, 0, diffH / 2.0f);
            }
        }

        // When the rectangle is rotated, resizing shifts its scene-space center.
        // Compensate by translating the item so the opposite edge stays anchored.
        QPointF oldCenterScene = m_cropItem->mapToScene(rect.center());

        m_cropItem->setRect(newRect);
        m_cropItem->setTransformOriginPoint(newRect.center());

        QPointF newCenterScene = m_cropItem->mapToScene(newRect.center());
        QPointF centerShift    = newCenterScene - oldCenterScene;

        m_cropItem->moveBy(-centerShift.x(), -centerShift.y());

        m_lastPos = scenePos;
        return;
    }

    // -------------------------------------------------------------------------
    // Crop rectangle translation (Move handle)
    // -------------------------------------------------------------------------
    if (m_cropMode && m_moving && m_cropDragMode == CropDrag_Move)
    {
        QPointF delta = scenePos - m_lastPos;
        m_cropItem->moveBy(delta.x(), delta.y());
        m_lastPos = scenePos;
        return;
    }

    // -------------------------------------------------------------------------
    // Crop rectangle drawing (initial drag)
    // -------------------------------------------------------------------------
    if (m_cropMode && m_drawing)
    {
        float w = scenePos.x() - m_startPoint.x();
        float h = scenePos.y() - m_startPoint.y();

        if (m_aspectRatio > 0.0f)
        {
            float signH  = (h >= 0.0f) ? 1.0f : -1.0f;
            float absW   = std::abs(w);
            h = (absW / m_aspectRatio) * signH;
        }

        QRectF newRect = QRectF(m_startPoint.x(), m_startPoint.y(), w, h).normalized();
        m_cropItem->setRect(newRect);
        m_cropItem->setTransformOriginPoint(newRect.center());
        return;
    }

    // -------------------------------------------------------------------------
    // Shift+Drag rubber-band update
    // -------------------------------------------------------------------------
    if ((event->modifiers() & Qt::ShiftModifier) && m_drawing && m_queryRectItem)
    {
        m_queryRectItem->setRect(QRectF(m_startPoint, scenePos).normalized());
        return;
    }

    // -------------------------------------------------------------------------
    // Selection mode rubber-band update
    // -------------------------------------------------------------------------
    if (m_interactionMode == Mode_Selection && m_drawing && m_queryRectItem)
    {
        m_queryRectItem->setRect(QRectF(m_startPoint, scenePos).normalized());
        return;
    }

    // -------------------------------------------------------------------------
    // ABE lasso stroke extension
    // -------------------------------------------------------------------------
    if (m_abeMode && m_lassoDrawing && m_currentLassoItem)
    {
        m_currentLassoPoly << scenePos;
        m_currentLassoItem->setPolygon(m_currentLassoPoly);
        return;
    }

    // -------------------------------------------------------------------------
    // Pixel value readout
    // -------------------------------------------------------------------------
    int ix = static_cast<int>(std::floor(scenePos.x()));
    int iy = static_cast<int>(std::floor(scenePos.y()));

    if (ix >= 0 && ix < m_buffer.width() &&
        iy >= 0 && iy < m_buffer.height())
    {
        QString info;

        if (m_buffer.channels() == 3)
        {
            float r = m_buffer.value(ix, iy, 0);
            float g = m_buffer.value(ix, iy, 1);
            float b = m_buffer.value(ix, iy, 2);
            info = QString("x:%1 y:%2  R:%3 G:%4 B:%5")
                       .arg(ix).arg(iy)
                       .arg(r, 0, 'f', 4)
                       .arg(g, 0, 'f', 4)
                       .arg(b, 0, 'f', 4);
        }
        else
        {
            float k = m_buffer.value(ix, iy, 0);
            info = QString("x:%1 y:%2  K:%3")
                       .arg(ix).arg(iy)
                       .arg(k, 0, 'f', 4);
        }

        emit pixelInfoUpdated(info);
    }
    else
    {
        emit pixelInfoUpdated(QString());
    }

    // Update the crop-handle hover cursor while the mouse moves without dragging
    if (m_cropMode && !m_moving && !m_drawing && m_cropItem->isVisible())
    {
        QPointF    itemPos   = m_cropItem->mapFromScene(scenePos);
        CropDragMode hover   = getCropDragMode(itemPos, m_cropItem->rect());
        updateCropCursor(hover, m_cropAngle);
    }

    viewport()->update();
    QGraphicsView::mouseMoveEvent(event);
}

void ImageViewer::mouseReleaseEvent(QMouseEvent* event)
{
    // -------------------------------------------------------------------------
    // Sample-point drag release
    // -------------------------------------------------------------------------
    if (m_movingSample)
    {
        m_movingSample = nullptr;
        setCursor(Qt::ArrowCursor);
        emit samplesMoved(getBackgroundSamples());
        return;
    }

    // -------------------------------------------------------------------------
    // Shift+Drag release: crop buffer to selected region and open a new view
    // -------------------------------------------------------------------------
    if ((event->modifiers() & Qt::ShiftModifier) && m_drawing)
    {
        m_drawing = false;

        if (m_queryRectItem && !m_queryRectItem->rect().isEmpty())
        {
            QRectF r    = m_queryRectItem->rect().normalized();
            QRect  rect = r.toRect();

            if (rect.width() > 0 && rect.height() > 0)
            {
                ImageBuffer newBuf = m_buffer;
                newBuf.cropRotated(r.center().x(), r.center().y(),
                                   rect.width(), rect.height(), 0);
                newBuf.addHistoryAction(tr("Crop from Selection (ROI)"));
                emit requestNewView(newBuf, tr("Selection"));
            }
        }

        m_queryRectItem->setVisible(false);
        setDragMode(QGraphicsView::ScrollHandDrag);
        return;
    }

    // -------------------------------------------------------------------------
    // Selection mode release: invoke the registered region callback
    // -------------------------------------------------------------------------
    if (m_interactionMode == Mode_Selection && m_drawing)
    {
        m_drawing = false;

        if (m_queryRectItem && !m_queryRectItem->rect().isEmpty())
        {
            QRectF rCopy = m_queryRectItem->rect();
            qDebug() << "[ImageViewer::mouseReleaseEvent] Selection rect:" << rCopy;

            // Defer the callback to avoid re-entrant scene modifications
            QTimer::singleShot(0, this, [this, rCopy]()
            {
                if (m_regionCallback)
                    m_regionCallback(rCopy);
            });
        }
        return;
    }

    // -------------------------------------------------------------------------
    // Crop mode release
    // -------------------------------------------------------------------------
    if (m_cropMode)
    {
        m_drawing = false;
        m_moving  = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    // -------------------------------------------------------------------------
    // ABE lasso release: finalize the polygon if it contains enough vertices
    // -------------------------------------------------------------------------
    if (m_abeMode && m_lassoDrawing)
    {
        m_lassoDrawing = false;

        if (m_currentLassoPoly.size() > 3)
        {
            QGraphicsPolygonItem* finalItem = m_scene->addPolygon(
                m_currentLassoPoly,
                QPen(Qt::yellow, 2, Qt::DashLine),
                QBrush(QColor(255, 255, 0, 30)));
            finalItem->setZValue(15);
            m_abeItems.push_back(finalItem);
        }

        if (m_currentLassoItem)
        {
            m_scene->removeItem(m_currentLassoItem);
            delete m_currentLassoItem;
            m_currentLassoItem = nullptr;
        }
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}


// =============================================================================
// Qt Event Overrides - Wheel (Zoom)
// =============================================================================

void ImageViewer::wheelEvent(QWheelEvent* event)
{
    QPoint pixelDelta = event->pixelDelta();
    QPoint angleDelta = event->angleDelta();

    double scaleFactor = 1.0;

    if (!pixelDelta.isNull())
    {
        // Trackpad: apply continuous smooth deltas.
        // The asymmetric formula ensures that zoom-in and zoom-out are
        // exact inverses of each other.
        double dy = pixelDelta.y();
        if (dy > 0)
            scaleFactor = 1.0 + dy * 0.008;
        else
            scaleFactor = 1.0 / (1.0 - dy * 0.008);
    }
    else if (angleDelta.y() != 0)
    {
        // Mouse wheel: one discrete step per notch (120 angle units = 1 notch)
        scaleFactor = (angleDelta.y() > 0) ? 1.25 : (1.0 / 1.25);
    }

    // Clamp the resulting scale to the configured zoom limits
    double newScale = m_scaleFactor * scaleFactor;
    newScale = qBound(ZOOM_MIN, newScale, ZOOM_MAX);

    double actual = newScale / m_scaleFactor;
    if (qAbs(actual - 1.0) < 1e-9)
    {
        event->accept();
        return;
    }

    // QGraphicsView::scale() with AnchorUnderMouse keeps the point under the
    // cursor fixed in scene space; no manual scroll bar adjustment is required
    scale(actual, actual);
    m_scaleFactor = transform().m11();

    emit viewChanged(m_scaleFactor,
                     horizontalScrollBar()->value(),
                     verticalScrollBar()->value());
    event->accept();
}


// =============================================================================
// Qt Event Overrides - Drag and Drop
// =============================================================================

void ImageViewer::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link") ||
        event->mimeData()->hasFormat("application/x-tstar-adapt"))
        event->acceptProposedAction();
    else
        QGraphicsView::dragEnterEvent(event);
}

void ImageViewer::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link") ||
        event->mimeData()->hasFormat("application/x-tstar-adapt"))
        event->acceptProposedAction();
    else
        QGraphicsView::dragMoveEvent(event);
}

void ImageViewer::dropEvent(QDropEvent* event)
{
    // Walk up the widget hierarchy to locate the owning CustomMdiSubWindow
    // and delegate the drop handling to it
    QWidget* p = parentWidget();
    while (p && !p->inherits("CustomMdiSubWindow"))
        p = p->parentWidget();

    if (p)
        static_cast<CustomMdiSubWindow*>(p)->handleDrop(event);
    else
        QGraphicsView::dropEvent(event);
}


// =============================================================================
// Qt Event Overrides - Painting
// =============================================================================

void ImageViewer::drawForeground(QPainter* painter,
                                 [[maybe_unused]] const QRectF& rect)
{
    // -------------------------------------------------------------------------
    // Magnifier loupe
    //
    // Renders a 100x100-pixel loupe in viewport space, positioned to the
    // upper-right of the cursor with a small gap.  Each source pixel is drawn
    // as a solid colored square (nearest-neighbour, no interpolation).
    // The loupe is suppressed during any drawing or editing operation.
    // -------------------------------------------------------------------------
    if (!m_magnifierVisible  ||
        !m_cursorOverViewport ||
        !painter              ||
        !m_imageItem          ||
        m_imageItem->pixmap().isNull())
        return;

    static constexpr int   LOUPE_SIZE = 100;    // Loupe side length in pixels
    static constexpr float LOUPE_ZOOM = 4.0f;   // Source region = LOUPE_SIZE / LOUPE_ZOOM px
    static constexpr int   GAP        = 12;     // Gap between cursor tip and loupe edge

    // Position the loupe relative to the cursor (preferred: top-right)
    QPoint cur = m_magnifierViewportPos;
    QRect  loupe(cur.x() + GAP, cur.y() - GAP - LOUPE_SIZE,
                 LOUPE_SIZE, LOUPE_SIZE);

    // Mirror horizontally if the loupe would exceed the right viewport edge
    if (loupe.right() > viewport()->width() - 2)
        loupe.moveRight(cur.x() - GAP);

    // Mirror vertically if the loupe would exceed the top viewport edge
    if (loupe.top() < 2)
        loupe.moveTop(cur.y() + GAP);

    // Source rectangle in scene/image coordinates
    double halfSrc = LOUPE_SIZE / (2.0 * LOUPE_ZOOM);
    QRectF src(m_magnifierScenePos.x() - halfSrc,
               m_magnifierScenePos.y() - halfSrc,
               halfSrc * 2.0, halfSrc * 2.0);
    src = src.intersected(m_imageItem->boundingRect());

    painter->save();
    painter->setWorldMatrixEnabled(false);  // Draw in viewport (pixel) coordinates

    if (!src.isEmpty())
    {
        QPixmap cropped  = m_imageItem->pixmap().copy(src.toRect());
        QImage  srcImage = cropped.toImage();
        int     srcW     = srcImage.width();
        int     srcH     = srcImage.height();

        if (srcW > 0 && srcH > 0)
        {
            double pixSizeX = static_cast<double>(LOUPE_SIZE) / srcW;
            double pixSizeY = static_cast<double>(LOUPE_SIZE) / srcH;

            // Rasterize each source pixel as a filled rectangle so there is
            // no bilinear or bicubic interpolation in the loupe
            for (int y = 0; y < srcH; ++y)
            {
                for (int x = 0; x < srcW; ++x)
                {
                    QColor pixelColor(srcImage.pixel(x, y));
                    int x1 = loupe.left() + static_cast<int>(x       * pixSizeX);
                    int y1 = loupe.top()  + static_cast<int>(y       * pixSizeY);
                    int x2 = loupe.left() + static_cast<int>((x + 1) * pixSizeX);
                    int y2 = loupe.top()  + static_cast<int>((y + 1) * pixSizeY);
                    painter->fillRect(QRect(x1, y1, x2 - x1, y2 - y1), pixelColor);
                }
            }
        }
    }
    else
    {
        painter->fillRect(loupe, Qt::black);
    }

    // Loupe border
    painter->setPen(QPen(QColor(220, 220, 220), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(loupe);

    // Central crosshair
    int cx = loupe.center().x();
    int cy = loupe.center().y();
    painter->setPen(QPen(Qt::white, 1));
    painter->drawLine(cx - 5, cy,     cx + 5, cy);
    painter->drawLine(cx,     cy - 5, cx,     cy + 5);

    painter->restore();
}