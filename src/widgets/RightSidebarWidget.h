#ifndef RIGHTSIDEBARWIDGET_H
#define RIGHTSIDEBARWIDGET_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPushButton>
#include <QVariantAnimation>
#include <QLabel>
#include <QMap>

class CustomMdiSubWindow;
class QEnterEvent;
class QEvent;

/**
 * @brief Collapsible right-side panel that displays thumbnails of shaded (collapsed)
 *        image subwindows. Its content width is half the left sidebar's default width.
 *
 * Behaviour mirrors the left SidebarWidget: a narrow tab strip on the right edge
 * toggles the content area. The content area slides in from the right.
 *
 * Thumbnail safe-zone: each thumbnail is rendered at a fixed size (THUMB_W x THUMB_H)
 * so that varying filename lengths have no effect on the layout.
 */
class RightSidebarWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int THUMB_W = 120;
    static constexpr int THUMB_H = 90;

    explicit RightSidebarWidget(QWidget* parent = nullptr);

    // Returns the full width this widget wants when expanded (tab + content)
    int totalVisibleWidth() const;

    // Called by MainWindow when a subwindow is shaded
    void addThumbnail(CustomMdiSubWindow* sub, const QPixmap& thumb, const QString& title);
    // Called by MainWindow when a subwindow is unshaded or closed
    void removeThumbnail(CustomMdiSubWindow* sub);

    void collapse() { setExpanded(false); }
    bool isExpanded() const { return m_expanded; }

    /**
     * @brief Update the anchor position so the right edge stays fixed.
     * Call this from MainWindow::resizeEvent.
     */
    void setAnchorGeometry(int rightX, int topY, int h);

signals:
    void expandedToggled(bool expanded);
    void thumbnailActivated(CustomMdiSubWindow* sub);

private slots:
    void onTabClicked();

private:
    void setExpanded(bool expanded);

    // Tab strip (right edge)
    QWidget*      m_tabContainer;
    QPushButton*  m_tabBtn;

    // Content area (slides in from right)
    QScrollArea*  m_contentContainer;
    QWidget*      m_listWidget;
    QVBoxLayout*  m_listLayout;

    // Animation
    QVariantAnimation* m_widthAnim;
    bool m_expanded  = false;
    int  m_expandedWidth = 175; // Half of left sidebar default (350)
    int  m_anchorRight   = -1;  // Right-edge X anchor (set via setAnchorGeometry)

    // Thumbnail tracking: subwindow -> container widget
    QMap<CustomMdiSubWindow*, QWidget*> m_items;
};

#endif // RIGHTSIDEBARWIDGET_H
