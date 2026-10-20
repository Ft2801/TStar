#include "RightSidebarWidget.h"
#include "CustomMdiSubWindow.h"
#include "ImageStatsWidget.h"
#include "ScriptConsoleWidget.h"
#include "../scripting/JSRuntime.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVariantAnimation>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QSettings>
#include <QStackedWidget>
#include <QLineEdit>
#include <QAction>
#include <QMenu>


// ============================================================================
// ThumbnailItem - internal widget: fixed-size preview tile with elided title
// ============================================================================

class ThumbnailItem : public QWidget
{
    Q_OBJECT

public:
    ThumbnailItem(const QPixmap& thumb,
                  const QString& title,
                  CustomMdiSubWindow* sub,
                  int creationIdx  = -1,
                  QWidget* parent  = nullptr)
        : QWidget(parent)
        , m_sub(sub)
        , m_creationIndex(creationIdx)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet("background: transparent;");
        setCursor(Qt::PointingHandCursor);

        auto* vl = new QVBoxLayout(this);
        vl->setContentsMargins(4, 4, 4, 4);
        vl->setSpacing(3);

        // Thumbnail image label - fixed height to prevent layout jitter
        m_thumbLabel = new QLabel(this);
        m_thumbLabel->setFixedHeight(RightSidebarWidget::THUMB_H);
        m_thumbLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_thumbLabel->setAlignment(Qt::AlignCenter);
        m_thumbLabel->setStyleSheet(
            "background: transparent; border: 1px solid #000000ff;");
        setThumb(thumb);
        vl->addWidget(m_thumbLabel);

        // Title label - fixed height with text elision to prevent overflow
        m_titleLabel = new QLabel(this);
        m_titleLabel->setFixedHeight(18);
        m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setStyleSheet(
            "color: #ccc; font-size: 10px; background: transparent;");
        setTitle(title);
        vl->addWidget(m_titleLabel);
    }

    void setThumb(const QPixmap& px)
    {
        if (px.isNull()) {
            m_thumbLabel->clear();
        } else {
            m_thumbLabel->setPixmap(
                px.scaled(RightSidebarWidget::THUMB_W,
                          RightSidebarWidget::THUMB_H,
                          Qt::KeepAspectRatio,
                          Qt::SmoothTransformation));
        }
    }

    void setTitle(const QString& title)
    {
        QFontMetrics fm(m_titleLabel->font());
        m_titleLabel->setText(
            fm.elidedText(title, Qt::ElideRight,
                          RightSidebarWidget::THUMB_W - 4));
        m_titleLabel->setToolTip(title);
    }

    CustomMdiSubWindow* sub()          const { return m_sub; }
    int                 creationIndex() const { return m_creationIndex; }

signals:
    void clicked(CustomMdiSubWindow* sub);

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton)
            emit clicked(m_sub);
        QWidget::mousePressEvent(e);
    }

    void enterEvent(QEnterEvent* e) override
    {
        setStyleSheet("background: #333; border-radius: 3px;");
        QWidget::enterEvent(e);
    }

    void leaveEvent(QEvent* e) override
    {
        setStyleSheet("background: transparent;");
        QWidget::leaveEvent(e);
    }

private:
    CustomMdiSubWindow* m_sub;
    int                 m_creationIndex;
    QLabel*             m_thumbLabel;
    QLabel*             m_titleLabel;
};


// ============================================================================
// VerticalButton - internal widget: tab button with rotated text label
// ============================================================================

class VerticalButton : public QPushButton
{
public:
    VerticalButton(const QString& text, QWidget* parent = nullptr)
        : QPushButton(text, parent)
    {}

    QSize sizeHint() const override
    {
        QSize s = QPushButton::sizeHint();
        return QSize(s.height(), s.width());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (isChecked())
            p.fillRect(rect(), QColor("#0055aa"));
        else if (underMouse())
            p.fillRect(rect(), QColor("#444"));

        p.save();
        p.translate(width(), 0);
        p.rotate(90);
        p.setPen(isChecked() ? Qt::white : QColor("#ccc"));
        p.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, text());
        p.restore();
    }
};


// ============================================================================
// RightSidebarWidget
// ============================================================================

RightSidebarWidget::RightSidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setStyleSheet("background-color: transparent;");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // ------------------------------------------------------------------
    // Content area (slides in from the right when the panel is expanded)
    // ------------------------------------------------------------------
    m_contentWrapper = new QWidget(this);
    m_contentWrapper->setFixedWidth(0); // Start collapsed
    m_contentWrapper->setStyleSheet("background-color: rgba(0, 0, 0, 128);");

    auto* wrapperLayout = new QVBoxLayout(m_contentWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);

    m_stackedWidget = new QStackedWidget(m_contentWrapper);
    wrapperLayout->addWidget(m_stackedWidget);

    // ==========================================
    // Page 0: Previews
    // ==========================================
    m_previewsPage = new QWidget();
    auto* previewsLayout = new QVBoxLayout(m_previewsPage);
    previewsLayout->setContentsMargins(0, 0, 0, 0);
    previewsLayout->setSpacing(0);

    // Top bar containing the "Hide minimized views" checkbox
    m_topContainer = new QWidget(m_previewsPage);
    m_topContainer->setFixedHeight(26);
    m_topContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_topContainer->setStyleSheet(
        "background-color: #202020;"
        "border-bottom: 1px solid #1a1a1a;"
        "border-left: none; border-right: none; border-top: none;");

    auto* topLayout = new QHBoxLayout(m_topContainer);
    topLayout->setContentsMargins(8, 0, 8, 0);
    topLayout->setSpacing(0);

    m_hideMinimizedViewsCb = new QCheckBox(tr("Hide minimized views"), m_topContainer);
    m_hideMinimizedViewsCb->setStyleSheet(
        "QCheckBox { color: #aaa; font-size: 11px; }"
        "QCheckBox::indicator { width: 12px; height: 12px; }");

    // Restore persisted checkbox state
    QSettings settings;
    m_hideMinimizedViews = settings.value("RightSidebar/hideMinimizedViews", false).toBool();
    m_hideMinimizedViewsCb->setChecked(m_hideMinimizedViews);

    connect(m_hideMinimizedViewsCb, &QCheckBox::toggled, [this](bool checked) {
        m_hideMinimizedViews = checked;
        QSettings s;
        s.setValue("RightSidebar/hideMinimizedViews", checked);
        emit hideMinimizedViewsToggled(checked);
    });

    topLayout->addWidget(m_hideMinimizedViewsCb);
    previewsLayout->addWidget(m_topContainer);

    // Scrollable thumbnail list
    m_contentContainer = new QScrollArea(m_previewsPage);
    m_contentContainer->setStyleSheet("background-color: transparent; border: none;");
    m_contentContainer->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentContainer->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_listWidget = new QWidget();
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(0, 8, 0, 8);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch();

    m_listWidget->setMinimumWidth(m_expandedWidth);
    m_listWidget->setStyleSheet("background: transparent;");

    m_contentContainer->setWidget(m_listWidget);
    m_contentContainer->setWidgetResizable(true);
    previewsLayout->addWidget(m_contentContainer);

    m_stackedWidget->addWidget(m_previewsPage);

    // ==========================================
    // Page 1: Search
    // ==========================================
    m_searchPage = new QWidget();
    auto* searchPageLayout = new QVBoxLayout(m_searchPage);
    searchPageLayout->setContentsMargins(0, 0, 0, 0);
    searchPageLayout->setSpacing(0);

    // Search box top area
    QWidget* searchTop = new QWidget(m_searchPage);
    searchTop->setFixedHeight(26);
    searchTop->setStyleSheet(
        "background-color: #202020;"
        "border-bottom: 1px solid #1a1a1a;"
        "border-left: none; border-right: none; border-top: none;"
    );
    auto* stLayout = new QHBoxLayout(searchTop);
    stLayout->setContentsMargins(8, 0, 8, 0);
    stLayout->setSpacing(0);
    
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText(tr("Search functions..."));
    m_searchBox->setStyleSheet(
        "QLineEdit { background-color: #333; color: #ccc; border: 1px solid #555; border-radius: 2px; padding: 0 4px; font-size: 11px; }"
    );
    stLayout->addWidget(m_searchBox);
    searchPageLayout->addWidget(searchTop);

    m_searchScrollArea = new QScrollArea(m_searchPage);
    m_searchScrollArea->setStyleSheet("background-color: transparent; border: none;");
    m_searchScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_searchScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_searchResultsWidget = new QWidget();
    m_searchResultsWidget->setStyleSheet("background: transparent;");
    m_searchResultsLayout = new QVBoxLayout(m_searchResultsWidget);
    m_searchResultsLayout->setContentsMargins(0, 8, 0, 8);
    m_searchResultsLayout->setSpacing(0);
    m_searchResultsLayout->addStretch();
    
    m_searchScrollArea->setWidget(m_searchResultsWidget);
    m_searchScrollArea->setWidgetResizable(true);
    searchPageLayout->addWidget(m_searchScrollArea);

    m_stackedWidget->addWidget(m_searchPage);

    // ==========================================
    // Page 2: Stats
    // ==========================================
    m_statsPage = new QWidget();
    auto* statsPageLayout = new QVBoxLayout(m_statsPage);
    statsPageLayout->setContentsMargins(0, 0, 0, 0);
    statsPageLayout->setSpacing(0);
    
    m_statsWidget = new ImageStatsWidget(m_statsPage);
    m_statsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    statsPageLayout->addWidget(m_statsWidget);

    m_stackedWidget->addWidget(m_statsPage);

    // ==========================================
    // Page 3: Script Console
    // ==========================================
    m_scriptPage = new QWidget();
    auto* scriptPageLayout = new QVBoxLayout(m_scriptPage);
    scriptPageLayout->setContentsMargins(0, 0, 0, 0);
    scriptPageLayout->setSpacing(0);

    m_scriptConsole = new ScriptConsoleWidget(m_scriptPage);
    m_scriptConsole->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scriptPageLayout->addWidget(m_scriptConsole);

    m_stackedWidget->addWidget(m_scriptPage);

    connect(m_searchBox, &QLineEdit::textChanged, this, &RightSidebarWidget::onSearchTextChanged);

    // ------------------------------------------------------------------
    // Tab strip (fixed narrow column on the right edge)
    // ------------------------------------------------------------------
    m_tabContainer = new QWidget(this);
    m_tabContainer->setFixedWidth(32);
    m_tabContainer->setStyleSheet(
        "background-color: #252525; border-left: 1px solid #1a1a1a;");

    auto* tabLayout = new QVBoxLayout(m_tabContainer);
    tabLayout->setContentsMargins(2, 5, 2, 5);
    tabLayout->setSpacing(5);

    m_tabBtn = new VerticalButton(tr("Previews"), m_tabContainer);
    m_tabBtn->setCheckable(true);
    m_tabBtn->setFixedSize(30, 100);
    m_tabBtn->setToolTip(tr("Collapsed Views"));
    m_tabBtn->setStyleSheet("border: none;");
    connect(m_tabBtn, &QPushButton::clicked,
            this, &RightSidebarWidget::onTabClicked);

    tabLayout->addWidget(m_tabBtn);

    m_searchTabBtn = new VerticalButton(tr("Search"), m_tabContainer);
    m_searchTabBtn->setCheckable(true);
    m_searchTabBtn->setFixedSize(30, 100);
    m_searchTabBtn->setToolTip(tr("Search Functions"));
    m_searchTabBtn->setStyleSheet("border: none;");
    connect(m_searchTabBtn, &QPushButton::clicked,
            this, &RightSidebarWidget::onSearchTabClicked);

    tabLayout->addWidget(m_searchTabBtn);

    m_statsTabBtn = new VerticalButton(tr("Stats"), m_tabContainer);
    m_statsTabBtn->setCheckable(true);
    m_statsTabBtn->setFixedSize(30, 100);
    m_statsTabBtn->setToolTip(tr("Image Statistics"));
    m_statsTabBtn->setStyleSheet("border: none;");
    connect(m_statsTabBtn, &QPushButton::clicked,
            this, &RightSidebarWidget::onStatsTabClicked);

    tabLayout->addWidget(m_statsTabBtn);

    m_scriptTabBtn = new VerticalButton(tr("Script"), m_tabContainer);
    m_scriptTabBtn->setCheckable(true);
    m_scriptTabBtn->setFixedSize(30, 100);
    m_scriptTabBtn->setToolTip(tr("JavaScript Console"));
    m_scriptTabBtn->setStyleSheet("border: none;");
    connect(m_scriptTabBtn, &QPushButton::clicked,
            this, &RightSidebarWidget::onScriptTabClicked);

    // Hide script button
    m_scriptTabBtn->setHidden(true);

    tabLayout->addWidget(m_scriptTabBtn);
    tabLayout->addStretch();

    mainLayout->addWidget(m_contentWrapper);
    mainLayout->addWidget(m_tabContainer);

    // ------------------------------------------------------------------
    // Slide animation for the content wrapper width
    // ------------------------------------------------------------------
    m_widthAnim = new QVariantAnimation(this);
    m_widthAnim->setDuration(250);
    m_widthAnim->setEasingCurve(QEasingCurve::OutQuad);

    connect(m_widthAnim, &QVariantAnimation::valueChanged,
            [this](const QVariant& val) {
        m_contentWrapper->setFixedWidth(val.toInt());
        int newW = totalVisibleWidth();
        resize(newW, height());

        // Keep the right edge anchored to the viewport boundary
        if (m_anchorRight >= 0)
            move(m_anchorRight - newW + 1, y());
    });
}

int RightSidebarWidget::totalVisibleWidth() const
{
    return m_tabContainer->width() + m_contentWrapper->width();
}

void RightSidebarWidget::setAnchorGeometry(int rightX, int topY, int h)
{
    m_anchorRight = rightX;
    int newW = totalVisibleWidth();
    move(rightX - newW + 1, topY);
    resize(newW, h);
}

void RightSidebarWidget::onTabClicked()
{
    switchToTab(0);
}

void RightSidebarWidget::onSearchTabClicked()
{
    switchToTab(1);
}

void RightSidebarWidget::onStatsTabClicked()
{
    switchToTab(2);
}

void RightSidebarWidget::onScriptTabClicked()
{
    switchToTab(3);
}

void RightSidebarWidget::switchToTab(int index)
{
    if (m_expanded && m_currentTabIndex == index) {
        // Toggle off
        setExpanded(false);
    } else {
        bool wasExpanded = m_expanded;
        m_currentTabIndex = index;
        m_stackedWidget->setCurrentIndex(index);
        
        if (wasExpanded) {
            m_tabBtn->setChecked(m_currentTabIndex == 0);
            m_searchTabBtn->setChecked(m_currentTabIndex == 1);
            m_statsTabBtn->setChecked(m_currentTabIndex == 2);
            m_scriptTabBtn->setChecked(m_currentTabIndex == 3);
            
            int targetWidth = (m_currentTabIndex >= 1) ? static_cast<int>(m_expandedWidth * 1.5) : m_expandedWidth;
            // Script tab needs even more width for the code editor
            if (m_currentTabIndex == 3) targetWidth = static_cast<int>(m_expandedWidth * 3.0);
            m_widthAnim->stop();
            m_widthAnim->setStartValue(m_contentWrapper->width());
            m_widthAnim->setEndValue(targetWidth);
            m_widthAnim->start();
        } else {
            setExpanded(true);
        }
        
        if (index == 1) {
            populateSearchResults(m_searchBox->text());
            m_searchBox->setFocus();
        }
    }
}

void RightSidebarWidget::setExpanded(bool expanded)
{
    if (m_expanded == expanded) return;
    m_expanded = expanded;
    emit expandedToggled(expanded);

    if (!expanded) {
        m_tabBtn->setChecked(false);
        m_searchTabBtn->setChecked(false);
        m_statsTabBtn->setChecked(false);
        m_scriptTabBtn->setChecked(false);
    } else {
        m_tabBtn->setChecked(m_currentTabIndex == 0);
        m_searchTabBtn->setChecked(m_currentTabIndex == 1);
        m_statsTabBtn->setChecked(m_currentTabIndex == 2);
        m_scriptTabBtn->setChecked(m_currentTabIndex == 3);
    }

    int targetWidth = (m_currentTabIndex >= 1) ? static_cast<int>(m_expandedWidth * 1.5) : m_expandedWidth;
    if (m_currentTabIndex == 3) targetWidth = static_cast<int>(m_expandedWidth * 3.0);

    m_widthAnim->stop();
    m_widthAnim->setStartValue(m_contentWrapper->width());
    m_widthAnim->setEndValue(expanded ? targetWidth : 0);
    m_widthAnim->start();
}

void RightSidebarWidget::addThumbnail(CustomMdiSubWindow* sub,
                                      const QPixmap& thumb,
                                      const QString& title,
                                      int creationSortIndex)
{
    if (!sub || m_items.contains(sub)) return;

    auto* item = new ThumbnailItem(thumb, title, sub, creationSortIndex, m_listWidget);
    connect(item, &ThumbnailItem::clicked,
            this, &RightSidebarWidget::thumbnailActivated);

    // Insert the tile at the correct position according to creationSortIndex
    int insertIdx = 0;
    int count     = m_listLayout->count() - 1; // Exclude the trailing stretch
    for (; insertIdx < count; ++insertIdx) {
        auto* existing = qobject_cast<ThumbnailItem*>(
            m_listLayout->itemAt(insertIdx)->widget());
        if (existing && creationSortIndex != -1 &&
            existing->creationIndex() > creationSortIndex)
            break;
    }

    m_listLayout->insertWidget(insertIdx, item);
    m_items[sub] = item;

    // Auto-expand when the first thumbnail is added
    if (m_items.size() == 1 && !m_expanded) {
        switchToTab(0);
    }
}

void RightSidebarWidget::removeThumbnail(CustomMdiSubWindow* sub)
{
    if (!sub || !m_items.contains(sub)) return;

    QWidget* w = m_items.take(sub);
    m_listLayout->removeWidget(w);
    w->deleteLater();

    // Auto-collapse when the list becomes empty
    if (m_items.isEmpty() && m_expanded && m_currentTabIndex == 0) {
        setExpanded(false);
    }
}

void RightSidebarWidget::registerToolAction(QAction* action)
{
    if (!action || action->text().isEmpty() || m_searchableActions.contains(action)) return;
    
    // Ignore actions without a trigger or acts as a menu
    if (action->menu()) return;

    m_searchableActions.append(action);

    // Re-sort alphabetically
    std::sort(m_searchableActions.begin(), m_searchableActions.end(), [](QAction* a, QAction* b) {
        QString textA = a->text().remove('&');
        QString textB = b->text().remove('&');
        return textA.compare(textB, Qt::CaseInsensitive) < 0;
    });

    if (m_expanded && m_currentTabIndex == 1) {
        populateSearchResults(m_searchBox->text());
    }
}

void RightSidebarWidget::onSearchTextChanged(const QString& text)
{
    populateSearchResults(text);
}

void RightSidebarWidget::populateSearchResults(const QString& filter)
{
    // Clear layout except stretch
    while (QLayoutItem* item = m_searchResultsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        else if (item->spacerItem()) {
            m_searchResultsLayout->addItem(item); // Put stretch back
            break;
        }
    }

    QString f = filter.trimmed();

    for (QAction* act : m_searchableActions) {
        QString cleanText = act->text().remove('&');
        if (!f.isEmpty() && !cleanText.contains(f, Qt::CaseInsensitive)) {
            continue;
        }

        QPushButton* btn = new QPushButton(cleanText);
        btn->setStyleSheet(
            "QPushButton { text-align: left; padding: 6px 10px; background: transparent; color: #ccc; border: none; border-bottom: 1px solid #3a3a3a; font-size: 11px; }"
            "QPushButton:hover { background: #444; border-bottom: 1px solid #3a3a3a; }"
        );
        btn->setCursor(Qt::PointingHandCursor);
        if (!act->icon().isNull()) {
            btn->setIcon(act->icon());
            btn->setIconSize(QSize(16, 16));
        }

        connect(btn, &QPushButton::clicked, act, &QAction::trigger);
        
        // Insert before stretch
        int count = m_searchResultsLayout->count();
        m_searchResultsLayout->insertWidget(count > 0 ? count - 1 : 0, btn);
    }
}

void RightSidebarWidget::setActiveWindow(CustomMdiSubWindow* sub)
{
    if (m_statsWidget) {
        m_statsWidget->setActiveWindow(sub);
    }
}

void RightSidebarWidget::setScriptRuntime(Scripting::JSRuntime* runtime)
{
    if (m_scriptConsole) {
        m_scriptConsole->setRuntime(runtime);
    }
}

// Required for Qt's AUTOMOC to process Q_OBJECT classes defined in this .cpp file
#include "RightSidebarWidget.moc"