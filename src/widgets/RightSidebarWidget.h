#ifndef RIGHTSIDEBARWIDGET_H
#define RIGHTSIDEBARWIDGET_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPushButton>
#include <QVariantAnimation>
#include <QLabel>
#include <QMap>
#include <QCheckBox>

class CustomMdiSubWindow;
class ImageStatsWidget;
class ScriptConsoleWidget;
class QEnterEvent;
class QEvent;
class QAction;
class QStackedWidget;
class QLineEdit;

namespace Scripting { class JSRuntime; }

/**
 * @brief Collapsible right-side panel that displays thumbnail previews of
 *        shaded (collapsed) image subwindows.
 *
 * A narrow tab strip on the right edge toggles the content area, which slides
 * in from the right using a QVariantAnimation. Each thumbnail is rendered at a
 * fixed size (THUMB_W x THUMB_H) so that varying title lengths do not affect
 * the layout. The panel's right edge is anchored to the viewport boundary via
 * setAnchorGeometry(), which must be called from MainWindow::resizeEvent().
 */
class RightSidebarWidget : public QWidget
{
    Q_OBJECT

public:
    static constexpr int THUMB_W = 160;  ///< Fixed thumbnail display width  (px)
    static constexpr int THUMB_H = 120;  ///< Fixed thumbnail display height (px)

    explicit RightSidebarWidget(QWidget* parent = nullptr);

    /** Returns the total widget width when fully expanded (tab strip + content). */
    int totalVisibleWidth() const;

    /**
     * @brief Anchors the right edge of this widget to a fixed viewport coordinate.
     *
     * Call this from MainWindow::resizeEvent() whenever the MDI area changes size.
     * @param rightX  X coordinate of the right anchor edge in parent coordinates.
     * @param topY    Y coordinate of the top edge.
     * @param h       Desired widget height.
     */
    void setAnchorGeometry(int rightX, int topY, int h);

    /**
     * @brief Registers a shaded subwindow and adds a thumbnail tile to the list.
     * @param sub               The subwindow that was shaded.
     * @param thumb             Pixmap snapshot taken just before shading.
     * @param title             Display name shown below the thumbnail.
     * @param creationSortIndex Optional sort key; tiles are ordered ascending.
     */
    void addThumbnail(CustomMdiSubWindow* sub,
                      const QPixmap&      thumb,
                      const QString&      title,
                      int                 creationSortIndex = -1);

    /**
     * @brief Removes the thumbnail tile associated with a subwindow.
     *
     * Called when a shaded subwindow is restored or closed.
     */
    void removeThumbnail(CustomMdiSubWindow* sub);

    /** Updates the statistics widget with the currently active window. */
    void setActiveWindow(CustomMdiSubWindow* sub);

    /** Registers a tool action to be searchable via the Search tab. */
    void registerToolAction(QAction* action);

    /** Set the JSRuntime for the scripting console tab. */
    void setScriptRuntime(Scripting::JSRuntime* runtime);

    /** Programmatically collapses the content panel. */
    void collapse() { setExpanded(false); }

    bool isExpanded()                const { return m_expanded; }
    bool isHideMinimizedViewsEnabled() const { return m_hideMinimizedViews; }

signals:
    /** Emitted whenever the panel is expanded or collapsed. */
    void expandedToggled(bool expanded);

    /** Emitted when the user clicks a thumbnail tile. */
    void thumbnailActivated(CustomMdiSubWindow* sub);

    /** Emitted when the "Hide minimized views" checkbox state changes. */
    void hideMinimizedViewsToggled(bool hidden);

private slots:
    void onTabClicked();
    void onSearchTabClicked();
    void onStatsTabClicked();
    void onScriptTabClicked();
    void onSearchTextChanged(const QString& text);

private:
    void setExpanded(bool expanded);
    void switchToTab(int index); // 0 = Previews, 1 = Search, 2 = Stats, 3 = Script
    void populateSearchResults(const QString& filter = "");

    // Tab strip (right edge column)
    QWidget*     m_tabContainer = nullptr;
    QPushButton* m_tabBtn       = nullptr;
    QPushButton* m_searchTabBtn = nullptr;
    QPushButton* m_statsTabBtn  = nullptr;
    QPushButton* m_scriptTabBtn = nullptr;

    // Sliding content area
    QWidget*      m_contentWrapper   = nullptr;
    QStackedWidget* m_stackedWidget  = nullptr;

    // Page 0: Previews
    QWidget*      m_previewsPage     = nullptr;
    QScrollArea*  m_contentContainer = nullptr;
    QWidget*      m_listWidget       = nullptr;
    QVBoxLayout*  m_listLayout       = nullptr;

    // Page 1: Search
    QWidget*      m_searchPage       = nullptr;
    QLineEdit*    m_searchBox        = nullptr;
    QScrollArea*  m_searchScrollArea = nullptr;
    QWidget*      m_searchResultsWidget= nullptr;
    QVBoxLayout*  m_searchResultsLayout= nullptr;

    // Page 2: Stats
    QWidget*          m_statsPage    = nullptr;
    ImageStatsWidget* m_statsWidget  = nullptr;

    // Page 3: Script Console
    QWidget*              m_scriptPage    = nullptr;
    ScriptConsoleWidget*  m_scriptConsole = nullptr;

    // Top bar within the previews area
    QWidget*   m_topContainer        = nullptr;
    QCheckBox* m_hideMinimizedViewsCb = nullptr;

    // Slide animation
    QVariantAnimation* m_widthAnim = nullptr;

    bool m_expanded           = false;
    bool m_hideMinimizedViews = false;
    int  m_expandedWidth      = 175;  ///< Content area width when fully open (px)
    int  m_anchorRight        = -1;   ///< Right-edge X anchor in parent coordinates
    int  m_currentTabIndex    = 0;

    /** Maps each tracked subwindow to its thumbnail tile widget. */
    QMap<CustomMdiSubWindow*, QWidget*> m_items;

    /** Stored searchable tools */
    QList<QAction*> m_searchableActions;
};

#endif // RIGHTSIDEBARWIDGET_H