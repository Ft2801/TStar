#ifndef CUSTOMMDISUBWINDOW_H
#define CUSTOMMDISUBWINDOW_H

#include <QMdiSubWindow>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSpacerItem>

// ---------------------------------------------------------------------------
// NameStrip
// A narrow vertical sidebar widget that displays the subwindow title rotated
// 90 degrees.  Double-clicking triggers a rename dialog.  The strip also acts
// as a drag source for the "duplicate" drag-and-drop action.
// ---------------------------------------------------------------------------
class NameStrip : public QWidget {
    Q_OBJECT
public:
    explicit NameStrip(QWidget* parent = nullptr);

    int  preferredHeight() const;

signals:
    void renameRequested();

protected:
    void paintEvent(QPaintEvent* event)             override;
    void mouseDoubleClickEvent(QMouseEvent* event)  override;
    void mousePressEvent(QMouseEvent* event)        override;
    void mouseMoveEvent(QMouseEvent* event)         override;
    void dragEnterEvent(QDragEnterEvent* event)     override;
    void dragMoveEvent(QDragMoveEvent* event)       override;
    void dropEvent(QDropEvent* event)               override;

private:
    QString m_title;
    QPoint  m_dragStartPos;
};

// ---------------------------------------------------------------------------
// LinkStrip
// Indicates whether the associated ImageViewer is linked to others for
// synchronised pan/zoom.  Dragging from this strip initiates a link action;
// clicking when already linked requests an unlink.
// ---------------------------------------------------------------------------
class LinkStrip : public QWidget {
    Q_OBJECT
public:
    explicit LinkStrip(QWidget* parent = nullptr);

    void setLinked(bool linked);
    bool isLinked() const { return m_linked; }

signals:
    // Emitted when the user clicks the strip while linked, requesting an unlink
    void linkToggled();

protected:
    void paintEvent(QPaintEvent* event)         override;
    void mousePressEvent(QMouseEvent* event)    override;
    void mouseMoveEvent(QMouseEvent* event)     override;
    void mouseReleaseEvent(QMouseEvent* event)  override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event)   override;
    void dropEvent(QDropEvent* event)           override;

private:
    bool   m_linked       = false;
    bool   m_dragging     = false;
    QPoint m_dragStartPos;
    QString m_linkText;
    QString m_linkedText;
};

// ---------------------------------------------------------------------------
// AdaptStrip
// Drag source for the "adapt size" action.  Dropping this strip onto another
// subwindow causes the target to match the source window geometry.
// ---------------------------------------------------------------------------
class AdaptStrip : public QWidget {
    Q_OBJECT
public:
    explicit AdaptStrip(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event)         override;
    void mousePressEvent(QMouseEvent* event)    override;
    void mouseMoveEvent(QMouseEvent* event)     override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event)   override;
    void dropEvent(QDropEvent* event)           override;

private:
    QPoint m_dragStartPos;
    QString m_adaptText;
};

// ---------------------------------------------------------------------------
// CustomTitleBar
// Replaces the native MDI title bar with a custom widget that contains the
// window title, an optional zoom percentage label, and window control buttons
// (shade/unshade, maximize/restore, close).
// ---------------------------------------------------------------------------
class CustomTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit CustomTitleBar(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setActive(bool active);
    void setShaded(bool shaded);
    void setMaximized(bool maximized);
    void setZoom(int percent);
    void setMaximizeButtonVisible(bool visible);

signals:
    void closeClicked();
    void minimizeClicked();
    void maximizeClicked();

protected:
    void mousePressEvent(QMouseEvent* event)        override;
    void mouseMoveEvent(QMouseEvent* event)         override;
    void mouseDoubleClickEvent(QMouseEvent* event)  override;

private:
    void updateStyle();

    QLabel*      m_zoomLabel;
    QLabel*      m_titleLabel;
    QPushButton* m_minBtn;
    QPushButton* m_maxBtn;
    QPushButton* m_closeBtn;
    QSpacerItem* m_rightSpacer;

    QPoint m_dragPos;
    bool   m_active    = false;
    bool   m_shaded    = false;
    bool   m_maximized = false;
};

// ---------------------------------------------------------------------------
// CustomMdiSubWindow
// A frameless QMdiSubWindow with a custom title bar, collapsible ("shaded")
// state, manual edge-resize support, fade-in/fade-out animations, and
// drag-and-drop support for linking and adapting viewer windows.
// ---------------------------------------------------------------------------
class CustomMdiSubWindow : public QMdiSubWindow {
    Q_OBJECT

public:
    explicit CustomMdiSubWindow(QWidget* parent = nullptr);

    // Overrides that maintain internal tracking state
    void setWidget(QWidget* widget);
    void setSubWindowTitle(const QString& title);
    void showMinimized();   // Triggers shade rather than standard minimise
    void showMaximized();
    void showNormal();

    // Initiates a fade-out animation before closing
    void animateClose();

    // Returns the ImageViewer child widget if one is present
    class ImageViewer* viewer() const;

    // Shading state (collapsed to title bar height)
    bool isShaded()    const { return m_shaded; }
    void toggleShade();

    void setToolWindow(bool isTool);
    bool isToolWindow() const { return m_isToolWindow; }

    void setActiveState(bool active);

    // Public so that strip widgets can forward drop events here
    void handleDrop(QDropEvent* event);

    void requestRename();

    void setSkipCloseAnimation(bool skip) { m_skipCloseAnimation = skip; }
    bool canClose();

signals:
    // Emitted when the window is shaded (shaded=true) or unshaded.
    // The thumbnail pixmap is a grab of the content area taken before hiding it.
    void shadingChanged(bool shaded, const QPixmap& thumbnail);
    void layoutChanged();

protected:
    void startFadeIn();
    void startFadeOut();

    void showEvent(QShowEvent* event)           override;
    void closeEvent(QCloseEvent* event)         override;
    void moveEvent(QMoveEvent* event)           override;
    void resizeEvent(QResizeEvent* event)       override;
    bool event(QEvent* event)                   override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event)   override;
    void dropEvent(QDropEvent* event)           override;

    // Manual edge-resize implementation
    void mousePressEvent(QMouseEvent* event)    override;
    void mouseMoveEvent(QMouseEvent* event)     override;
    void mouseReleaseEvent(QMouseEvent* event)  override;
    void leaveEvent(QEvent* event)              override;

private:
    void updateCursor(const QPoint& pos);
    int  getResizeEdge(const QPoint& pos);
    void installRecursiveFilter(QWidget* w);

    // Resizes the subwindow so the viewer area matches the image aspect ratio
    void adjustToImageSize();

    // Resizes the subwindow to match the image at its current zoom level,
    // clamping to the minimum and maximum allowed window dimensions
    void fitWindowToCurrentImageSize();

    

    // -- Layout ----------------------------------------------------------
    QFrame*       m_container;
    QVBoxLayout*  m_mainLayout;
    CustomTitleBar* m_titleBar;
    QWidget*      m_contentArea;
    QHBoxLayout*  m_contentLayout;

    // -- Left sidebar strips ---------------------------------------------
    NameStrip*    m_nameStrip;
    LinkStrip*    m_linkStrip;
    AdaptStrip*   m_adaptStrip;
    QPushButton*  m_fitWindowBtn;

    // -- Manual resize state ---------------------------------------------
    bool m_resizing     = false;

    enum ResizeEdge { None = 0, Top = 0x1, Bottom = 0x2, Left = 0x4, Right = 0x8 };
    int    m_activeEdges         = None;
    QPoint m_dragStartPos;
    QRect  m_dragStartGeometry;

    // -- Window state ----------------------------------------------------
    bool  m_shaded               = false;
    int   m_originalHeight       = 0;
    int   m_originalWidth        = 0;
    bool  m_wasMaximized         = false;
    bool  m_isMaximized          = false;
    QRect m_validNormalGeometry;  // Cached because isMaximized() is unreliable for frameless windows

    bool  m_isToolWindow         = false;

    // -- Animation state -------------------------------------------------
    bool m_isClosing             = false;
    class QPropertyAnimation*      m_anim   = nullptr;
    class QGraphicsOpacityEffect*  m_effect = nullptr;
    bool  m_skipCloseAnimation   = false;
};

#endif // CUSTOMMDISUBWINDOW_H