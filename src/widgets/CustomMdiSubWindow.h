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
#include <QSpacerItem>

// NameStrip: Displays vertical title, Double-click to rename
class NameStrip : public QWidget {
    Q_OBJECT
public:
    explicit NameStrip(QWidget *parent = nullptr);
    void setTitle(const QString& title);
    int preferredHeight() const;

signals:
    void renameRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QString m_title;
    QPoint m_dragStartPos;
};

// LinkStrip: Displays Link/Linked status, Drag source for linking
class LinkStrip : public QWidget {
    Q_OBJECT
public:
    explicit LinkStrip(QWidget *parent = nullptr);
    void setLinked(bool linked);
    bool isLinked() const { return m_linked; }

signals:
    void linkToggled(); // Request to unlink if clicked when linked

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool m_linked = false;
    bool m_dragging = false;
    QPoint m_dragStartPos;
};

// AdaptStrip: Drag source for adapting window sizes
class AdaptStrip : public QWidget {
    Q_OBJECT
public:
    explicit AdaptStrip(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QPoint m_dragStartPos;
};

class CustomTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit CustomTitleBar(QWidget *parent = nullptr);
    void setTitle(const QString& title);
    void setActive(bool active);
    void setShaded(bool shaded);
    void setMaximized(bool maximized);
    void setZoom(int percent); // NEW: Update zoom display
    void setMaximizeButtonVisible(bool visible); // NEW: Show/hide maximize button
    
signals:
    void closeClicked();
    void minimizeClicked();
    void maximizeClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void updateStyle();


    QLabel* m_zoomLabel; // NEW
    QLabel* m_titleLabel; 
    QPushButton* m_minBtn;
    QPushButton* m_maxBtn;
    QPushButton* m_closeBtn;
    QSpacerItem* m_leftSpacer;
    QSpacerItem* m_rightSpacer;
    QPoint m_dragPos;
    bool m_active = false;
    bool m_shaded = false;
    bool m_maximized = false;
};

class CustomMdiSubWindow : public QMdiSubWindow {
    Q_OBJECT
public:
    explicit CustomMdiSubWindow(QWidget *parent = nullptr);
    void setWidget(QWidget *widget);
    void setSubWindowTitle(const QString& title);
    void showMinimized(); // Shading instead of standard minimize
    void showMaximized(); // Override to track m_isMaximized
    void showNormal();    // Override to track m_isMaximized
    void animateClose(); // Public slot for fade-out close
    
    // Accessor for the embedded ImageViewer (if any)
    class ImageViewer* viewer() const;

    // Shading logic
    bool isShaded() const { return m_shaded; }
    
    // Toggle between reduced "shaded" state and normal
    void toggleShade();
    
    void setToolWindow(bool isTool); // Hides sidebars if true
    bool isToolWindow() const { return m_isToolWindow; }
    void setActiveState(bool active);
    
    // Custom drag/drop handler for linking (public so ImageViewer can call it)
    void handleDrop(QDropEvent* event);

    // Renaming support
    void requestRename();

    // Close logic
    void setSkipCloseAnimation(bool skip) { m_skipCloseAnimation = skip; }
    bool canClose(); 
    
protected:
    // Animations
    void startFadeIn();
    void startFadeOut();

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    
    // Manual Resizing
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void updateCursor(const QPoint& pos);
    int getResizeEdge(const QPoint& pos);
    void installRecursiveFilter(QWidget* w);
    void adjustToImageSize(); // Helper to request strict sizing
    
    QFrame* m_container;
    QVBoxLayout* m_mainLayout;
    
    CustomTitleBar* m_titleBar;
    
    QWidget* m_contentArea;
    QHBoxLayout* m_contentLayout;
    
    // Sidebars
    NameStrip* m_nameStrip;
    LinkStrip* m_linkStrip;
    AdaptStrip* m_adaptStrip;
    
    bool m_resizing = false;
    enum ResizeEdge { None = 0, Top = 0x1, Bottom = 0x2, Left = 0x4, Right = 0x8 };
    int m_activeEdges = None;
    QPoint m_dragStartPos;
    QRect m_dragStartGeometry;
    
    bool m_shaded = false;
    int m_originalHeight = 0;
    int m_originalWidth = 0;
    bool m_wasMaximized = false;
    bool m_isMaximized = false; // Manual tracking
    QRect m_validNormalGeometry; // Persistent normal geometry since isMaximized() unreliable for frameless
    
    bool m_isToolWindow = false;
    
    // Animation state
    bool m_isClosing = false;
    class QPropertyAnimation* m_anim = nullptr;
    class QGraphicsOpacityEffect* m_effect = nullptr;
    
    bool m_skipCloseAnimation = false;
};

#endif // CUSTOMMDISUBWINDOW_H
