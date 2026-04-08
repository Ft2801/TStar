// =============================================================================
// ImageViewer.h
//
// QGraphicsView-based image viewer widget for TStar. Provides pan, zoom,
// interactive crop, ABE polygon selection, point/region picking, mask overlay,
// view linking across MDI windows, and undo/redo history management.
// =============================================================================

#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

// ---- Qt headers -------------------------------------------------------------
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QScrollBar>

// ---- Standard library -------------------------------------------------------
#include <vector>
#include <functional>
#include <memory>

// ---- Qt containers ----------------------------------------------------------
#include <QVector>
#include <QPolygonF>

// ---- Project headers --------------------------------------------------------
#include "ImageBuffer.h"

// Forward declarations
class ImageHistoryManager;

// =============================================================================
// ImageViewer class
// =============================================================================

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(QWidget* parent = nullptr);
    virtual ~ImageViewer();

    // ---- Image display ------------------------------------------------------

    /// Set a pre-rendered QImage for display.
    void setImage(const QImage& image, bool preserveView = false);

    /// Replace a downscaled preview image while keeping scene geometry intact.
    void setPreviewImage(const QImage& img);

    // ---- Zoom controls ------------------------------------------------------

    void zoomIn();
    void zoomOut();
    void zoom1to1();
    void fitToWindow();

    // ---- Buffer access & MDI support ----------------------------------------

    ImageBuffer&       getBuffer()       { return m_buffer; }
    const ImageBuffer& getBuffer() const { return m_buffer; }

    QString getHeaderValue(const QString& key) const {
        return m_buffer.getHeaderValue(key);
    }

    void setBuffer(const ImageBuffer& buffer,
                   const QString& name = QString(),
                   bool preserveView = false);

    void refreshDisplay(bool preserveView = true);
    void refresh() { refreshDisplay(true); }

    // ---- Mask overlay -------------------------------------------------------

    void setMaskOverlay(bool show) {
        m_showMaskOverlay = show;
        refreshDisplay(true);
    }
    bool isMaskOverlayEnabled() const { return m_showMaskOverlay; }

    // ---- Undo / redo --------------------------------------------------------

    void pushUndo(const QString& description = QString());
    void undo(bool emitSignals = true);
    void redo(bool emitSignals = true);
    void jumpToHistoryState(int index);
    bool    canUndo() const;
    bool    canRedo() const;
    QString getUndoDescription() const;
    QString getRedoDescription() const;
    const std::vector<QString>& undoDescriptions() const { return m_undoDescriptions; }
    const std::vector<QString>& redoDescriptions() const { return m_redoDescriptions; }
    
    // ---- Crop mode ----------------------------------------------------------

    void setCropMode(bool active);
    void setCropAngle(float angle);
    void setAspectRatio(float ratio);
    void getCropState(float& cx, float& cy, float& w, float& h, float& angle) const;

    // ---- ABE (Automatic Background Extraction) mode -------------------------

    void setAbeMode(bool enable);
    void clearAbePolygons();
    std::vector<QPolygonF> getAbePolygons() const;

    // ---- Background sample points -------------------------------------------

    void setBackgroundSamples(const std::vector<QPointF>& points);
    std::vector<QPointF> getBackgroundSamples() const;
    void clearBackgroundSamples();

    // ---- Point / region picking ---------------------------------------------

    void setPickMode(bool active);
    void setRectQueryMode(bool active);

    // ---- Callback for region selection (bypasses MOC) ------------------------

    using RegionCallback = std::function<void(QRectF)>;
    void setRegionSelectedCallback(RegionCallback cb) { m_regionCallback = cb; }

    // ---- Interaction modes --------------------------------------------------

    enum InteractionMode {
        Mode_PanZoom,     ///< Default: drag = pan, wheel = zoom
        Mode_Selection,   ///< Drag = rectangular selection, Ctrl+Drag = pan
        Mode_Crop,
        Mode_ABE
    };
    void setInteractionMode(InteractionMode mode);

    // ---- GHS / LUT preview --------------------------------------------------

    /// Set a per-channel preview LUT (3 channels x 65536 entries).
    void setPreviewLUT(const std::vector<std::vector<float>>& luts);
    void clearPreviewLUT();

    // ---- Display state accessors --------------------------------------------

    ImageBuffer::DisplayMode getDisplayMode()    const { return m_displayMode; }
    bool  isDisplayLinked()      const { return m_displayLinked; }
    bool  isDisplayInverted()    const { return m_displayInverted; }
    bool  isDisplayFalseColor()  const { return m_displayFalseColor; }
    float getAutoStretchMedian() const { return m_autoStretchMedian; }
    ImageBuffer::ChannelView channelView() const { return m_channelView; }

    void setDisplayState(ImageBuffer::DisplayMode mode, bool linked);
    void setAutoStretchMedian(float median);
    void setInverted(bool inverted);
    void setFalseColor(bool falseColor);
    void setChannelView(ImageBuffer::ChannelView cv);

    /// Restore buffer and display state in a single call to avoid redundant refreshes.
    void restoreState(const ImageBuffer& buffer,
                      ImageBuffer::DisplayMode mode, bool linked);

    QImage getCurrentDisplayImage() const { return m_displayImage; }

    // ---- Source path tracking ------------------------------------------------

    QString filePath() const             { return m_filePath; }
    void    setFilePath(const QString& path) { m_filePath = path; }

    // ---- Snapshot accessors for project persistence -------------------------

    QVector<ImageBuffer> undoHistory() const;
    QVector<ImageBuffer> redoHistory() const;
    void setHistory(const QVector<ImageBuffer>& undoHistory,
                    const QVector<ImageBuffer>& redoHistory);

    // ---- Modification state -------------------------------------------------

    bool isModified() const { return m_isModified; }
    void setModified(bool modified);

    // ---- Selection ----------------------------------------------------------

    void   clearSelection();
    QRectF getSelectionRect() const;

    // ---- View linking -------------------------------------------------------

    bool isLinked() const { return m_isLinked; }
    void setLinked(bool linked);

    // ---- Zoom & scroll accessors (for view synchronization) -----------------

    float getScale()   const { return static_cast<float>(m_scaleFactor); }
    int   getHBarLoc() const;
    int   getVBarLoc() const;

    // ---- Annotation support -------------------------------------------------

    double  zoomFactor()  const { return m_scaleFactor; }
    double  pixelScale()  const;  ///< arcsec/pixel from WCS metadata
    QPointF mapToScene(const QPoint& widgetPos) const {
        return QGraphicsView::mapToScene(widgetPos);
    }
    QPointF mapFromScene(const QPointF& scenePos) const {
        return QGraphicsView::mapFromScene(scenePos).toPointF();
    }

signals:
    void pointPicked(QPointF p);                               ///< Scene coordinates
    void samplesMoved(const std::vector<QPointF>& points);     ///< After sample drag
    void requestNewView(const ImageBuffer& img, const QString& title);
    void bufferChanged();                                       ///< Buffer content updated
    void historyChanged();                                      ///< Undo/redo stacks changed
    void resized();                                             ///< Widget geometry changed

    // View linking signals
    void viewChanged(float scale, float hVal, float vVal);
    void unlinked();
    void modifiedChanged(bool modified);
    void pixelInfoUpdated(const QString& info);

public slots:
    /// Synchronize this viewer's zoom and scroll to match another viewer.
    void syncView(float scale, float hVal, float vVal);

protected:
    // ---- Event overrides ----------------------------------------------------

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // ---- Scene items --------------------------------------------------------

    QGraphicsScene*       m_scene;
    QGraphicsPixmapItem*  m_imageItem;
    QGraphicsRectItem*    m_cropItem = nullptr;

    // ---- Link state ---------------------------------------------------------

    bool m_isLinked = false;

    // ---- Image data & history -----------------------------------------------

    ImageBuffer m_buffer;

    // Legacy full-copy history (backward compatibility)
    std::vector<ImageBuffer> m_undoStack;
    std::vector<ImageBuffer> m_redoStack;
    std::vector<QString>     m_undoDescriptions;
    std::vector<QString>     m_redoDescriptions;

    // Delta-compressed history (preferred, memory-efficient)
    std::unique_ptr<ImageHistoryManager> m_historyManager;
    bool m_useDeltaHistory = true;

    // ---- Display state ------------------------------------------------------

    QImage m_displayImage;
    double m_scaleFactor = 1.0;
    float  m_zoom        = 1.0f;

    // ---- Pan / drag state ---------------------------------------------------

    float    m_panX = 0.0f;
    float    m_panY = 0.0f;
    QPointF  m_lastMousePos;
    bool     m_dragging = false;

    // ---- Crop state ---------------------------------------------------------

    bool     m_cropMode    = false;
    float    m_cropAngle   = 0.0f;
    float    m_aspectRatio = -1.0f;  ///< -1 = free aspect ratio

    QPointF  m_startPoint;
    QPointF  m_endPoint;
    bool     m_drawing        = false;
    bool     m_previewActive  = false;

    bool     m_moving = false;
    QPointF  m_lastPos;

    enum CropDragMode {
        CropDrag_None,
        CropDrag_Move,
        CropDrag_Left,
        CropDrag_Right,
        CropDrag_Top,
        CropDrag_Bottom,
        CropDrag_TopLeft,
        CropDrag_TopRight,
        CropDrag_BottomLeft,
        CropDrag_BottomRight
    };

    CropDragMode m_cropDragMode = CropDrag_None;

    CropDragMode getCropDragMode(const QPointF& itemPos,
                                 const QRectF& rect,
                                 float tolerance = 10.0f) const;
    void updateCropCursor(CropDragMode mode, float rotation);

    // ---- Pick / selection state ---------------------------------------------

    RegionCallback       m_regionCallback;
    bool                 m_pickMode      = false;
    bool                 m_rectQueryMode = false;
    QGraphicsRectItem*   m_queryRectItem = nullptr;

    // ---- ABE state ----------------------------------------------------------

    bool                                            m_abeMode       = false;
    std::vector<class QGraphicsPolygonItem*>         m_abeItems;
    class QGraphicsPolygonItem*                      m_currentLassoItem = nullptr;
    QPolygonF                                        m_currentLassoPoly;
    bool                                            m_lassoDrawing  = false;

    // ---- Background sample state --------------------------------------------

    std::vector<class QGraphicsEllipseItem*>  m_sampleItems;
    QGraphicsEllipseItem*                     m_movingSample = nullptr;

    // ---- Interaction mode ---------------------------------------------------

    InteractionMode m_interactionMode = Mode_PanZoom;

    // ---- Preview LUT --------------------------------------------------------

    std::vector<std::vector<float>> m_previewLUT;

    // ---- Zoom limits --------------------------------------------------------

    static constexpr double ZOOM_MIN = 0.01;    ///< 1 % minimum zoom
    static constexpr double ZOOM_MAX = 120.0;   ///< 12000 % maximum zoom

    // ---- Magnifier state ----------------------------------------------------

    bool     m_magnifierVisible    = false;
    bool     m_cursorOverViewport  = false;
    QPointF  m_magnifierScenePos;
    QPoint   m_magnifierViewportPos;

    // ---- Display configuration ----------------------------------------------

    ImageBuffer::DisplayMode  m_displayMode       = ImageBuffer::Display_Linear;
    bool                      m_displayLinked      = true;
    bool                      m_displayInverted    = false;
    bool                      m_displayFalseColor  = false;
    float                     m_autoStretchMedian  = 0.25f;
    ImageBuffer::ChannelView  m_channelView        = ImageBuffer::ChannelRGB;

    // ---- Modification tracking ----------------------------------------------

    bool    m_isModified      = false;
    bool    m_showMaskOverlay = true;
    QString m_filePath;
};

#endif // IMAGEVIEWER_H