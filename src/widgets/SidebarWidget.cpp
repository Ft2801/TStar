#include "SidebarWidget.h"
#include <QIcon>
#include <QLabel>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QDateTime>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QScrollArea>
#include <QPainter>
#include <QStyleOptionButton>
#include <QMouseEvent>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QSettings>

SidebarWidget::SidebarWidget(QWidget* parent) : QWidget(parent) {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Core property
    setStyleSheet("background-color: transparent;"); // Prevent gray box
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    // 1. Tab Strip (Left)
    m_tabContainer = new QWidget(this);
    m_tabContainer->setFixedWidth(32); // Narrow strip
    m_tabContainer->setStyleSheet("background-color: rgba(43, 43, 43, 128); border-right: 1px solid #3a3a3a;");
    
    m_tabLayout = new QVBoxLayout(m_tabContainer);
    m_tabLayout->setContentsMargins(2, 5, 2, 5);
    m_tabLayout->setSpacing(5);
    m_tabLayout->addStretch();
    
    // 2. Content Area (Right) - Use ScrollArea for Clipping/Slide Effect
    // m_contentContainer in header is QWidget*, so we cast or assign compatible type.
    QScrollArea* scrollArea = new QScrollArea(this);
    m_contentContainer = scrollArea; 
    
    scrollArea->setFixedWidth(0); // Start collapsed
    scrollArea->setStyleSheet("background-color: rgba(0, 0, 0, 128); border: none;");
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Essential for clipping
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    m_stack = new QStackedWidget();
    // CRITICAL: Content must have fixed/min width equal to expanded width to prevent compression
    m_stack->setMinimumWidth(m_expandedWidth); 
    
    scrollArea->setWidget(m_stack);
    scrollArea->setWidgetResizable(true); 
    
    mainLayout->addWidget(m_tabContainer);
    mainLayout->addWidget(m_contentContainer);
    m_resizeHandle = new ResizeHandle(this);
    mainLayout->addWidget(m_resizeHandle); 
    
    // Animation
    m_widthAnim = new QVariantAnimation(this);
    m_widthAnim->setDuration(250);
    m_widthAnim->setEasingCurve(QEasingCurve::OutQuad);
    connect(m_widthAnim, &QVariantAnimation::valueChanged, [this](const QVariant& val){
        m_contentContainer->setFixedWidth(val.toInt());
        // Important: Since we are overlay/absolute, we must resize ourself to fit content
        resize(totalVisibleWidth(), height());
    });
    
    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);
    connect(m_tabGroup, &QButtonGroup::idClicked, this, &SidebarWidget::onTabClicked);
}

int SidebarWidget::totalVisibleWidth() const {
    int w = m_tabContainer->width() + m_contentContainer->width();
    if (m_resizeHandle) w += m_resizeHandle->width();
    return w;
}

void SidebarWidget::addPanel(const QString& name, const QString& iconPath, QWidget* panel) {
    if (!panel) return;
    
    QWidget* widgetToAdd = panel;
    
    // Special case for Console: Wrap it with a top bar
    if (name == "Console" || name == tr("Console")) {
        m_console = panel->findChild<QTextEdit*>();
        if (!m_console) m_console = qobject_cast<QTextEdit*>(panel);

        if (m_console) {
            // Container Widget
            QWidget* container = new QWidget();
            QVBoxLayout* layout = new QVBoxLayout(container);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            
            // Top Bar (Auto-open Checkbox)
            QWidget* topContainer = new QWidget(container);
            topContainer->setFixedHeight(26);
            topContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); // Enforce fixed height
            topContainer->setStyleSheet("background-color: #202020; border-bottom: 1px solid #1a1a1a;");
            
            QHBoxLayout* topLayout = new QHBoxLayout(topContainer);
            topLayout->setContentsMargins(8, 0, 8, 0);
            topLayout->setSpacing(0);
            
            QCheckBox* chk = new QCheckBox(tr("Auto-open console on log"), topContainer);
            chk->setStyleSheet("QCheckBox { color: #aaa; font-size: 11px; } QCheckBox::indicator { width: 12px; height: 12px; }");
            
            // Load state
            QSettings settings;
            m_autoOpenConsole = settings.value("Console/autoOpen", true).toBool();
            chk->setChecked(m_autoOpenConsole);
            
            connect(chk, &QCheckBox::toggled, [this](bool checked){
                m_autoOpenConsole = checked;
                QSettings settings;
                settings.setValue("Console/autoOpen", checked);
                emit autoOpenToggled(checked);
            });
            
            topLayout->addWidget(chk);
            topLayout->addStretch();
            
            // Add to container layout
            layout->addWidget(topContainer, 0, Qt::AlignTop); // Distinctly at top
            
            // Console Panel
            panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            // Ensure no margins on the panel itself if possible
            if (panel->layout()) panel->layout()->setContentsMargins(0,0,0,0);
            
            layout->addWidget(panel, 1); // Expand to fill rest
            
            widgetToAdd = container;
        }
    }
    
    int id = m_stack->count();
    m_stack->addWidget(widgetToAdd);
    
    createTab(name, iconPath, id);
    m_idToName[id] = name;
    m_nameToId[name] = id;
}

void SidebarWidget::createTab(const QString& name, const QString& iconPath, int id) {
    // vertical button with text
    VerticalButton* btn = new VerticalButton(name, this);
    btn->setCheckable(true);
    btn->setToolTip(name);
    
    // Prioritize vertical text as requested ("console" and "header viewer")
    
    btn->setFixedSize(30, 120); // Taller for vertical text
    
    // Stylesheet might conflict with custom paint? 
    // Button Logic handles check state in PaintEvent, so minimal style needed for border/bg
    // actually paintEvent handles bg.
    // Just ensure transparent base
    btn->setStyleSheet("border: none;");

    m_tabGroup->addButton(btn, id);
    m_tabLayout->insertWidget(m_tabLayout->count() - 1, btn); // Insert before stretch
}

void SidebarWidget::onTabClicked(int id) {
    bool sameTab = (id == m_currentId) && m_expanded;
    
    if (sameTab) {
        // Collapse
        setExpanded(false);
        m_tabGroup->button(id)->setChecked(false);
        m_currentId = -1;
    } else {
        // Switch or Open
        m_stack->setCurrentIndex(id);
        m_currentId = id;
        if (!m_expanded) setExpanded(true);
    }
}

void SidebarWidget::setExpanded(bool expanded) {
    if (m_expanded == expanded) return;
    m_expanded = expanded;
    emit expandedToggled(expanded);
    
    if (!expanded) {
        // When collapsing, ensure no button is visually active/checked
        if (m_tabGroup->checkedButton()) {
            m_tabGroup->setExclusive(false);
            m_tabGroup->checkedButton()->setChecked(false);
            m_tabGroup->setExclusive(true);
        }
        m_currentId = -1;
    }
    
    m_widthAnim->stop();
    m_widthAnim->setStartValue(m_contentContainer->width());
    m_widthAnim->setEndValue(expanded ? m_expandedWidth : 0);
    m_widthAnim->start();
}

void SidebarWidget::onAnimationFinished() {
    // Optional cleanup
}

void SidebarWidget::openPanel(const QString& name) {
    if (!m_nameToId.contains(name)) return;
    int id = m_nameToId[name];
    
    // Programmatic open: avoid toggle logic in onTabClicked
    m_stack->setCurrentIndex(id);
    m_currentId = id;
    
    if (m_tabGroup->button(id)) {
        m_tabGroup->button(id)->setChecked(true);
    }
    
    if (!m_expanded) {
        setExpanded(true);
    }
}

QWidget* SidebarWidget::getPanel(const QString& name) {
    if (!m_nameToId.contains(name)) return nullptr;
    return m_stack->widget(m_nameToId[name]);
}


void SidebarWidget::setExpandedWidth(int width) {
    if (width < 100) width = 100;
    if (width > 800) width = 800;
    m_expandedWidth = width;
    if (m_expanded) {
         m_contentContainer->setFixedWidth(m_expandedWidth);
         // CRITICAL Fix: Resize the parent widget immediately to avoid clipping
         resize(totalVisibleWidth(), height());
    }
    // Sync stack width to ensure "slide" effect works
    if (m_stack) m_stack->setMinimumWidth(m_expandedWidth);
}

bool SidebarWidget::isInteracting() const {
    // Check if the cursor is anywhere within the sidebar's current area
    // We use global position to be cross-platform/reliable regardless of focus
    QPoint globalCursor = QCursor::pos();
    QRect globalRect = QRect(mapToGlobal(QPoint(0,0)), size());
    
    bool underMouse = globalRect.contains(globalCursor);
    bool hasFocusedChild = hasFocus() || (m_console && m_console->hasFocus());

    if (underMouse) return true;
    
    // Fallback: check focus
    if (hasFocusedChild) return true;
    
    return false;
}

void SidebarWidget::enterEvent(QEnterEvent* event) {
    emit interactionStarted();
    QWidget::enterEvent(event);
}

void SidebarWidget::leaveEvent(QEvent* event) {
    emit interactionEnded();
    QWidget::leaveEvent(event);
}

QString SidebarWidget::currentPanel() const {
    if (m_currentId != -1 && m_idToName.contains(m_currentId)) {
        return m_idToName[m_currentId];
    }
    return QString();
}

void SidebarWidget::logToConsole(const QString& htmlMsg) {
    if (!m_console) return;
    m_console->append(htmlMsg);
    m_console->verticalScrollBar()->setValue(m_console->verticalScrollBar()->maximum());
}

SidebarWidget::VerticalButton::VerticalButton(const QString& text, QWidget* parent) : QPushButton(text, parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize SidebarWidget::VerticalButton::sizeHint() const {
    QSize s = QPushButton::sizeHint();
    return QSize(s.height(), s.width());
}

void SidebarWidget::VerticalButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    QStyleOptionButton opt;
    initStyleOption(&opt);
    
    // Find SidebarWidget iteratively to handle layout re-parenting
    SidebarWidget* sb = nullptr;
    QWidget* current = parentWidget();
    while (current) {
        sb = qobject_cast<SidebarWidget*>(current);
        if (sb) break;
        current = current->parentWidget();
    }
    
    bool isSidebarExpanded = sb ? sb->m_expanded : false;

    if (isChecked() && isSidebarExpanded) p.fillRect(rect(), QColor("#0055aa"));
    else if (underMouse()) p.fillRect(rect(), QColor("#444"));
    
    p.translate(0, height());
    p.rotate(-90);
    
    p.setPen((isChecked() && isSidebarExpanded) ? Qt::white : QColor("#ccc"));
    QRect textRect(0, 0, height(), width());
    p.drawText(textRect, Qt::AlignCenter, text());
}
