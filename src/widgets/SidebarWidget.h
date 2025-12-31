#ifndef SIDEBARWIDGET_H
#define SIDEBARWIDGET_H

#include <QWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QVariantAnimation>
#include <QButtonGroup>
#include <QMap>
#include <QMouseEvent>

class QEnterEvent;
class QEvent;

class SidebarWidget : public QWidget {
    Q_OBJECT
    friend class ResizeHandle; 
public:
    class VerticalButton : public QPushButton {
    public:
        VerticalButton(const QString& text, QWidget* parent = nullptr);
        QSize sizeHint() const override;
    protected:
        void paintEvent(QPaintEvent* event) override;
    };

    void setExpandedWidth(int width);
    
    // Helper to get total desired width (tab + content + handle)
    int totalVisibleWidth() const;
    
    explicit SidebarWidget(QWidget* parent = nullptr);
    
    // Adds a panel to the sidebar. 
    // icon: resource path or QIcon fallback
    // name: Display name and internal ID
    void addPanel(const QString& name, const QString& iconPath, QWidget* panel);
    
    // Returns the panel widget by name
    QWidget* getPanel(const QString& name);
    
    // Programmatically open a panel
    void openPanel(const QString& name);
    
    // Add text to the Console panel (assumed to be named "Console")
    void logToConsole(const QString& htmlMsg);

    // Public control for collapse/expand
    void collapse() { setExpanded(false); }
    bool isExpanded() const { return m_expanded; }

    // Reliable check if the user is currently using the sidebar
    bool isInteracting() const;

    QString currentPanel() const;

    bool isAutoOpenEnabled() const { return m_autoOpenConsole; }

signals:
    void interactionStarted();
    void interactionEnded();
    // Signal to track visual expansion state
    void expandedToggled(bool expanded);
    void autoOpenToggled(bool enabled);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onTabClicked(int id);
    void onAnimationFinished();

private:
    // Layouts
    QWidget* m_tabContainer;
    QVBoxLayout* m_tabLayout;
    
    QWidget* m_contentContainer;
    QStackedWidget* m_stack;
    
    // Animation
    QVariantAnimation* m_widthAnim;
    bool m_expanded = false;
    int m_expandedWidth = 350; // Default width
    
    // Tabs
    QButtonGroup* m_tabGroup;
    QMap<int, QString> m_idToName;
    QMap<QString, int> m_nameToId;
    
    int m_currentId = -1;
    bool m_autoOpenConsole = true;
    
    // Console ref (if present)
    class QTextEdit* m_console = nullptr;
    
    // Resize Handle
    class ResizeHandle* m_resizeHandle = nullptr;
    
    void createTab(const QString& name, const QString& iconPath, int id);
    void setExpanded(bool expanded);
};

// Internal Resize Handle
class ResizeHandle : public QWidget {
public:
    ResizeHandle(SidebarWidget* parent) : QWidget(parent), m_sidebar(parent) {
        setFixedWidth(5);
        setCursor(Qt::SizeHorCursor);
        setStyleSheet("background: transparent;");
    }
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_startX = e->globalPosition().x();
            m_startWidth = m_sidebar->m_expandedWidth;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_dragging) {
            int delta = e->globalPosition().x() - m_startX;
            int newWidth = m_startWidth + delta;
            m_sidebar->setExpandedWidth(newWidth);
        }
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        m_dragging = false;
    }
    void enterEvent(QEnterEvent*) override {
        setStyleSheet("background: #444;"); // Highlight
    }
    void leaveEvent(QEvent*) override {
        setStyleSheet("background: transparent;");
    }

private:
    SidebarWidget* m_sidebar;
    bool m_dragging = false;
    int m_startX = 0;
    int m_startWidth = 0;
};

#endif // SIDEBARWIDGET_H
