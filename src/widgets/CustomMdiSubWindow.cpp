#include "CustomMdiSubWindow.h"
#include "ImageViewer.h"
#include "MainWindowCallbacks.h"
#include "Icons.h"

#include <QStyle>
#include <QMenu>
#include <QScreen>
#include <QDebug>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QCoreApplication>
#include <QMdiArea>
#include <QEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QIcon>
#include <QSvgRenderer>
#include <QPixmap>
#include <QPainter>
#include <QHoverEvent>
#include <QMessageBox>
#include <QAbstractScrollArea>
#include <QInputDialog>
#include <QPointer>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QElapsedTimer>

// ---------------------------------------------------------------------------
// FixedUI
// Compile-time UI constants shared across all subwindow components.
// ---------------------------------------------------------------------------
namespace FixedUI {
    constexpr int sidebarWidth    = 20;
    constexpr int titleBarHeight  = 25;
    constexpr int buttonSize      = 24;
    constexpr int iconSize        = 16;
    constexpr int dragPixmapSize  = 32;
    constexpr int minShadedWidth  = 300;
    constexpr int borderWidth     = 2;
    constexpr int minWindowWidth  = 250;
    constexpr int minWindowHeight = 350;
    constexpr int resizeMargin    = 8;
}

// ---------------------------------------------------------------------------
// iconFromSvg
// Rasterises an SVG string into a QIcon at the standard icon size.
// ---------------------------------------------------------------------------
static QIcon iconFromSvg(const QString& svg, QWidget* widget = nullptr)
{
    Q_UNUSED(widget);
    QByteArray ba = svg.toUtf8();
    QSvgRenderer renderer(ba);

    const int size = FixedUI::iconSize;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    renderer.render(&painter);

    return QIcon(pix);
}

// ===========================================================================
// NameStrip
// ===========================================================================

NameStrip::NameStrip(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(FixedUI::sidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true);

    // Store translated "NAME" text
    m_title = QCoreApplication::translate("CustomMdiSubWindow", "NAME");

    // Calculate fixed height based on longest text among NAME, LINK, LINKED, ADAPT
    const QFontMetrics fm(QFont("Segoe UI", 9));
    const QString linkText = QCoreApplication::translate("CustomMdiSubWindow", "LINK");
    const QString linkedText = QCoreApplication::translate("CustomMdiSubWindow", "LINKED");
    const QString adaptText = QCoreApplication::translate("CustomMdiSubWindow", "ADAPT");
    
    const int nameLen = fm.horizontalAdvance(m_title);
    const int linkLen = fm.horizontalAdvance(linkText);
    const int linkedLen = fm.horizontalAdvance(linkedText);
    const int adaptLen = fm.horizontalAdvance(adaptText);
    const int maxLen = std::max({nameLen, linkLen, linkedLen, adaptLen});
    
    setFixedHeight(maxLen + 10);  // 5px padding each side
}

void NameStrip::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link")      ||
        event->mimeData()->hasFormat("application/x-tstar-adapt")     ||
        event->mimeData()->hasFormat("application/x-tstar-duplicate"))
    {
        event->acceptProposedAction();
    }
}

int NameStrip::preferredHeight() const
{
    return height();
}

void NameStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#3a3a3a"));
    p.setPen(Qt::white);
    p.setFont(QFont("Segoe UI", 9));

    // Rotate the painter to render the title text vertically (bottom-to-top)
    p.save();
    p.translate(0, height());
    p.rotate(-90);
    p.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, m_title);
    p.restore();
}

void NameStrip::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit renameRequested();
}

void NameStrip::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link") ||
        event->mimeData()->hasFormat("application/x-tstar-adapt"))
    {
        event->acceptProposedAction();
    }
}

void NameStrip::dropEvent(QDropEvent* event)
{
    // Walk the parent chain to find the owning CustomMdiSubWindow
    QWidget* widget = this;
    while (widget && !widget->inherits("CustomMdiSubWindow"))
        widget = widget->parentWidget();

    if (auto* win = qobject_cast<CustomMdiSubWindow*>(widget))
        win->handleDrop(event);
}

void NameStrip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragStartPos = event->pos();
}

void NameStrip::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance())
        return;

    QWidget* widget = this;
    while (widget && !widget->inherits("CustomMdiSubWindow"))
        widget = widget->parentWidget();

    auto* sourceWin = qobject_cast<CustomMdiSubWindow*>(widget);
    if (!sourceWin) return;

    QMimeData* mimeData = new QMimeData();
    mimeData->setData("application/x-tstar-duplicate",
                      QByteArray::number((quintptr)sourceWin, 16));

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    QPixmap pix(FixedUI::dragPixmapSize, FixedUI::dragPixmapSize);
    pix.fill(Qt::cyan);    // Cyan identifies a duplicate drag
    drag->setPixmap(pix);
    drag->exec(Qt::CopyAction);
}

// ===========================================================================
// LinkStrip
// ===========================================================================

LinkStrip::LinkStrip(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(FixedUI::sidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true);

    // Cache translated strings
    m_linkText = QCoreApplication::translate("CustomMdiSubWindow", "LINK");
    m_linkedText = QCoreApplication::translate("CustomMdiSubWindow", "LINKED");

    // Calculate fixed height based on longest text among NAME, LINK, LINKED, ADAPT
    const QFontMetrics fm(QFont("Segoe UI", 9));
    const QString nameText = QCoreApplication::translate("CustomMdiSubWindow", "NAME");
    const QString adaptText = QCoreApplication::translate("CustomMdiSubWindow", "ADAPT");
    
    const int nameLen = fm.horizontalAdvance(nameText);
    const int linkLen = fm.horizontalAdvance(m_linkText);
    const int linkedLen = fm.horizontalAdvance(m_linkedText);
    const int adaptLen = fm.horizontalAdvance(adaptText);
    const int maxLen = std::max({nameLen, linkLen, linkedLen, adaptLen});
    
    setFixedHeight(maxLen + 10);  // 5px padding each side
}

void LinkStrip::setLinked(bool linked)
{
    if (m_linked == linked) return;
    m_linked = linked;
    update();
}

void LinkStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), m_linked ? QColor("#2a5a2a") : QColor("#3a3a3a"));
    p.setPen(m_linked ? Qt::green : Qt::white);
    p.setFont(QFont("Segoe UI", 9));

    p.save();
    p.translate(0, height());
    p.rotate(-90);
    p.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter,
               m_linked ? m_linkedText : m_linkText);
    p.restore();
}

void LinkStrip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragging     = false;
    }
}

void LinkStrip::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance())
        return;

    m_dragging = true;

    QWidget* widget = this;
    while (widget && !widget->inherits("CustomMdiSubWindow"))
        widget = widget->parentWidget();

    auto* sourceWin = qobject_cast<CustomMdiSubWindow*>(widget);
    if (!sourceWin) return;

    QMimeData* mimeData = new QMimeData();
    mimeData->setData("application/x-tstar-link",
                      QByteArray::number((quintptr)sourceWin, 16));

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    QPixmap pix(FixedUI::dragPixmapSize, FixedUI::dragPixmapSize);
    pix.fill(Qt::green);   // Green identifies a link drag
    drag->setPixmap(pix);
    drag->exec(Qt::LinkAction);

    m_dragging = false;
}

void LinkStrip::mouseReleaseEvent(QMouseEvent* event)
{
    // A pure click (no drag) on a linked strip requests unlinking
    if (event->button() == Qt::LeftButton && !m_dragging && m_linked)
        emit linkToggled();

    m_dragging = false;
}

void LinkStrip::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link") ||
        event->mimeData()->hasFormat("application/x-tstar-adapt"))
    {
        event->acceptProposedAction();
    }
}

void LinkStrip::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link") ||
        event->mimeData()->hasFormat("application/x-tstar-adapt"))
    {
        event->acceptProposedAction();
    }
}

void LinkStrip::dropEvent(QDropEvent* event)
{
    QWidget* widget = this;
    while (widget && !widget->inherits("CustomMdiSubWindow"))
        widget = widget->parentWidget();

    if (auto* win = qobject_cast<CustomMdiSubWindow*>(widget))
        win->handleDrop(event);
}

// ===========================================================================
// AdaptStrip
// ===========================================================================

AdaptStrip::AdaptStrip(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(FixedUI::sidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true);

    // Cache translated string
    m_adaptText = QCoreApplication::translate("CustomMdiSubWindow", "ADAPT");

    // Calculate fixed height based on longest text among NAME, LINK, LINKED, ADAPT
    const QFontMetrics fm(QFont("Segoe UI", 9));
    const QString nameText = QCoreApplication::translate("CustomMdiSubWindow", "NAME");
    const QString linkText = QCoreApplication::translate("CustomMdiSubWindow", "LINK");
    const QString linkedText = QCoreApplication::translate("CustomMdiSubWindow", "LINKED");
    
    const int nameLen = fm.horizontalAdvance(nameText);
    const int linkLen = fm.horizontalAdvance(linkText);
    const int linkedLen = fm.horizontalAdvance(linkedText);
    const int adaptLen = fm.horizontalAdvance(m_adaptText);
    const int maxLen = std::max({nameLen, linkLen, linkedLen, adaptLen});
    
    setFixedHeight(maxLen + 10);  // 5px padding each side
}

void AdaptStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#3a3a3a"));
    p.setPen(Qt::white);
    p.setFont(QFont("Segoe UI", 9));

    p.save();
    p.translate(0, height());
    p.rotate(-90);
    p.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, m_adaptText);
    p.restore();
}

void AdaptStrip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragStartPos = event->pos();
}

void AdaptStrip::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance())
        return;

    QWidget* widget = this;
    while (widget && !widget->inherits("CustomMdiSubWindow"))
        widget = widget->parentWidget();

    auto* sourceWin = qobject_cast<CustomMdiSubWindow*>(widget);
    if (!sourceWin) return;

    QMimeData* mimeData = new QMimeData();
    mimeData->setData("application/x-tstar-adapt",
                      QByteArray::number((quintptr)sourceWin, 16));

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    QPixmap pix(FixedUI::dragPixmapSize, FixedUI::dragPixmapSize);
    pix.fill(Qt::yellow);  // Yellow identifies an adapt drag
    drag->setPixmap(pix);
    drag->exec(Qt::MoveAction);
}

void AdaptStrip::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link") ||
        event->mimeData()->hasFormat("application/x-tstar-adapt"))
    {
        event->acceptProposedAction();
    }
}

void AdaptStrip::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link") ||
        event->mimeData()->hasFormat("application/x-tstar-adapt"))
    {
        event->acceptProposedAction();
    }
}

void AdaptStrip::dropEvent(QDropEvent* event)
{
    QWidget* widget = this;
    while (widget && !widget->inherits("CustomMdiSubWindow"))
        widget = widget->parentWidget();

    if (auto* win = qobject_cast<CustomMdiSubWindow*>(widget))
        win->handleDrop(event);
}

// ===========================================================================
// CustomTitleBar
// ===========================================================================

CustomTitleBar::CustomTitleBar(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(FixedUI::titleBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_active = false;

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 0, 0, 2);
    layout->setSpacing(4);

    m_titleLabel = new QLabel("", this);
    m_titleLabel->setStyleSheet(
        "color: #ddd; font-weight: bold; font-family: 'Segoe UI'; "
        "font-size: 11px; background: transparent;"
    );

    m_zoomLabel = new QLabel("", this);
    m_zoomLabel->setStyleSheet(
        "QLabel { color: #aaa; font-family: 'Segoe UI'; font-size: 10px; "
        "background-color: transparent; margin-right: 5px; border: none; }"
    );
    m_zoomLabel->setVisible(false);

    const int btnSize = FixedUI::buttonSize;
    const int icnSize = FixedUI::iconSize;

    auto configBtn = [&](QPushButton* btn, const QString& svg, const QString& hoverColor) {
        btn->setFixedSize(btnSize, btnSize);
        btn->setIcon(iconFromSvg(svg, this));
        btn->setIconSize(QSize(icnSize, icnSize));
        btn->setFlat(true);
        btn->setStyleSheet(QString(
            "QPushButton { border: none; background: transparent; padding: 2px; }"
            "QPushButton:hover { background: %1; border-radius: 2px; }"
        ).arg(hoverColor));
    };

    m_minBtn   = new QPushButton(this);
    configBtn(m_minBtn,   Icons::WIN_SHADE,   "#4a6b7b");
    m_maxBtn   = new QPushButton(this);
    configBtn(m_maxBtn,   Icons::WIN_RESTORE, "#4a6b7b");
    m_closeBtn = new QPushButton(this);
    configBtn(m_closeBtn, Icons::WIN_CLOSE,   "#c00");

    m_rightSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_zoomLabel);
    layout->addSpacerItem(m_rightSpacer);
    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    connect(m_closeBtn, &QPushButton::clicked, this, &CustomTitleBar::closeClicked);
    connect(m_minBtn,   &QPushButton::clicked, this, &CustomTitleBar::minimizeClicked);
    connect(m_maxBtn,   &QPushButton::clicked, this, &CustomTitleBar::maximizeClicked);

    updateStyle();
}

void CustomTitleBar::setTitle(const QString& title)
{
    m_titleLabel->setProperty("fullTitle", title);

    if (m_shaded) {
        // When shaded the window has a fixed narrow width; elide the title to fit
        const QFontMetrics fm(m_titleLabel->font());
        const int buttonsW  = 3 * FixedUI::buttonSize + 20;
        const int available = static_cast<int>(1.5 * FixedUI::minShadedWidth) - buttonsW - 16;
        m_titleLabel->setText(fm.elidedText(title, Qt::ElideRight, std::max(available, 20)));
    } else {
        m_titleLabel->setText(title);
    }
}

void CustomTitleBar::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    updateStyle();
}

void CustomTitleBar::setShaded(bool shaded)
{
    m_shaded = shaded;
    m_minBtn->setIcon(iconFromSvg(m_shaded ? Icons::WIN_UNSHADE : Icons::WIN_SHADE));
    setMaximizeButtonVisible(!shaded);

    // Re-apply title with correct elision for the new width constraint
    QString fullTitle = m_titleLabel->property("fullTitle").toString();
    if (fullTitle.isEmpty()) fullTitle = m_titleLabel->text();
    setTitle(fullTitle);
}

void CustomTitleBar::setZoom(int percent)
{
    if (percent <= 0) {
        m_zoomLabel->setVisible(false);
    } else {
        m_zoomLabel->setText(QString("%1%").arg(percent));
        m_zoomLabel->setVisible(true);
    }
}

void CustomTitleBar::setMaximized(bool maximized)
{
    m_maximized = maximized;
    m_maxBtn->setIcon(iconFromSvg(m_maximized ? Icons::WIN_MAXIMIZE : Icons::WIN_RESTORE));
}

void CustomTitleBar::setMaximizeButtonVisible(bool visible)
{
    m_maxBtn->setVisible(visible);
}

void CustomTitleBar::updateStyle()
{
    const QString bg = m_active ? "#3a4b5b" : "#2a2a2a";
    setStyleSheet(QString("background-color: %1;").arg(bg));
}

void CustomTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPos = event->globalPosition().toPoint();
        event->accept();
    }
}

void CustomTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) return;

    // Walk the parent chain to find the QMdiSubWindow and move it
    QWidget* p = parentWidget();
    while (p) {
        QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(p);
        if (sub && !sub->isMaximized()) {
            const QPoint delta = event->globalPosition().toPoint() - m_dragPos;
            sub->move(sub->pos() + delta);
            m_dragPos = event->globalPosition().toPoint();
            event->accept();
            return;
        }
        p = p->parentWidget();
    }
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit minimizeClicked();
}

// ===========================================================================
// CustomMdiSubWindow
// ===========================================================================

CustomMdiSubWindow::CustomMdiSubWindow(QWidget* parent) : QMdiSubWindow(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
    setAttribute(Qt::WA_DeleteOnClose);
    setAcceptDrops(true);
    installEventFilter(this);

    // -- Container frame (draws the 2px border) --------------------------
    m_container = new QFrame(this);
    m_container->setObjectName("SubWindowContainer");
    m_container->setStyleSheet(
        "#SubWindowContainer { border: 2px solid #444; background: #202020; padding: 2px; }"
    );

    m_mainLayout = new QVBoxLayout(m_container);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // -- Title bar -------------------------------------------------------
    m_titleBar = new CustomTitleBar(m_container);
    connect(m_titleBar, &CustomTitleBar::closeClicked,    this, &CustomMdiSubWindow::animateClose);
    connect(m_titleBar, &CustomTitleBar::minimizeClicked, this, &CustomMdiSubWindow::showMinimized);

    connect(m_titleBar, &CustomTitleBar::maximizeClicked, [this]() {
        if (m_isMaximized) {
            // Restore from maximised state, unshading first if required
            if (m_shaded) {
                m_shaded = false;
                m_titleBar->setShaded(false);
                setMinimumWidth(0);
                setMaximumWidth(16777215);
                setMaximumHeight(16777215);
                setMinimumHeight(0);
                m_titleBar->show();
            }

            showNormal();
            m_isMaximized = false;
            m_titleBar->setMaximized(false);

            if (!m_validNormalGeometry.isNull()) {
                setGeometry(m_validNormalGeometry);
            } else {
                resize(800, 600);
                if (QMdiArea* area = mdiArea()) {
                    setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                                    size(), area->viewport()->rect()));
                }
            }

            if (m_contentArea) {
                m_contentArea->show();
                m_contentArea->resize(width(), height() - m_titleBar->height());
            }

            if (ImageViewer* v = viewer())
                QTimer::singleShot(50, v, &ImageViewer::fitToWindow);

        } else {
            // Maximise, handling the shaded state gracefully
            if (!m_shaded)
                m_validNormalGeometry = geometry();

            if (m_shaded) {
                m_shaded = false;
                m_titleBar->setShaded(false);
                setMinimumWidth(0);
                setMaximumWidth(16777215);
                setMaximumHeight(16777215);
                setMinimumHeight(0);
                m_titleBar->show();
                m_contentArea->show();

                if (QMdiArea* area = mdiArea()) {
                    if (QWidget* vp = area->viewport()) setGeometry(vp->rect());
                    else                                 setGeometry(area->rect());
                } else {
                    setGeometry(0, 0, 1920, 1080);
                }

                // Defer the actual maximise call to allow the unshade resize to settle
                QTimer::singleShot(50, this, [this]() {
                    showMaximized();
                    m_isMaximized = true;
                    m_titleBar->setMaximized(true);
                    if (m_contentArea) {
                        m_contentArea->show();
                        m_contentArea->resize(width(), height() - m_titleBar->height());
                    }
                    if (ImageViewer* v = viewer()) {
                        v->fitToWindow();
                        QTimer::singleShot(10, v, &ImageViewer::fitToWindow);
                    }
                });
                return;
            }

            showMaximized();
            m_isMaximized = true;
            m_titleBar->setMaximized(true);

            if (ImageViewer* v = viewer())
                QTimer::singleShot(50, v, &ImageViewer::fitToWindow);
        }
    });

    m_mainLayout->addWidget(m_titleBar);

    // -- Content area ----------------------------------------------------
    m_contentArea = new QWidget(m_container);
    m_contentArea->setStyleSheet("background: #202020;");
    m_contentLayout = new QHBoxLayout(m_contentArea);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);

    // Left strip container (holds NameStrip, LinkStrip, AdaptStrip)
    QWidget*     leftStrip  = new QWidget(m_contentArea);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftStrip);
    leftStrip->setFixedWidth(FixedUI::sidebarWidth);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_nameStrip = new NameStrip(leftStrip);
    connect(m_nameStrip, &NameStrip::renameRequested,
            this, &CustomMdiSubWindow::requestRename);

    m_linkStrip = new LinkStrip(leftStrip);
    connect(m_linkStrip, &LinkStrip::linkToggled, [this]() {
        // Unlink this viewer from all peers when the user clicks LINKED
        ImageViewer* v = m_contentArea->findChild<ImageViewer*>();
        if (v && v->isLinked()) {
            if (QMdiArea* area = mdiArea()) {
                QVector<QPair<ImageViewer*, CustomMdiSubWindow*>> others;
                for (QMdiSubWindow* sub : area->subWindowList()) {
                    if (sub == this) continue;
                    auto* otherWin = qobject_cast<CustomMdiSubWindow*>(sub);
                    if (!otherWin) continue;
                    ImageViewer* ov = otherWin->viewer();
                    if (ov && ov->isLinked())
                        others.append({ov, otherWin});
                }
                for (auto& pair : others) {
                    disconnect(v,          &ImageViewer::viewChanged,
                               pair.first, &ImageViewer::syncView);
                    disconnect(pair.first, &ImageViewer::viewChanged,
                               v,          &ImageViewer::syncView);
                }
                v->setLinked(false);

                // Unlink peers that no longer have any linked partner
                for (auto& pair : others) {
                    bool hasPeer = false;
                    for (auto& q : others) {
                        if (q.first != pair.first && q.first->isLinked()) {
                            hasPeer = true;
                            break;
                        }
                    }
                    if (!hasPeer) {
                        pair.first->setLinked(false);
                        if (pair.second->m_linkStrip)
                            pair.second->m_linkStrip->setLinked(false);
                    }
                }
            } else {
                v->setLinked(false);
            }
        }
        m_linkStrip->setLinked(false);
    });

    m_adaptStrip = new AdaptStrip(leftStrip);

    // Button that resizes the subwindow to match the image at its current zoom level
    m_fitWindowBtn = new QPushButton(leftStrip);
    m_fitWindowBtn->setFixedSize(FixedUI::sidebarWidth, FixedUI::sidebarWidth);
    m_fitWindowBtn->setFlat(true);
    m_fitWindowBtn->setToolTip(tr("Resize the window so it matches the image at its current zoom level"));
    m_fitWindowBtn->setStyleSheet(
        "QPushButton { border: none; background: transparent; color: white; "
        "              font-size: 14px; padding: 2px; }"
        "QPushButton:hover { background: #444; border-radius: 4px; }"
    );
    m_fitWindowBtn->setIcon(iconFromSvg(Icons::RESIZE));
    m_fitWindowBtn->setIconSize(QSize(FixedUI::iconSize, FixedUI::iconSize));
    connect(m_fitWindowBtn, &QPushButton::clicked,
            this, &CustomMdiSubWindow::fitWindowToCurrentImageSize);

    leftLayout->addWidget(m_nameStrip);
    leftLayout->addSpacing(2);
    leftLayout->addWidget(m_linkStrip);
    leftLayout->addSpacing(2);
    leftLayout->addWidget(m_adaptStrip);
    leftLayout->addStretch();
    leftLayout->addWidget(m_fitWindowBtn);

    m_contentLayout->addWidget(leftStrip);
    m_mainLayout->addWidget(m_contentArea, 1);

    QMdiSubWindow::setWidget(m_container);
    setAcceptDrops(true);
    installRecursiveFilter(m_container);
    setMouseTracking(true);
}

// ---------------------------------------------------------------------------
// installRecursiveFilter
// Installs this subwindow as an event filter on all descendant widgets so
// that resize-handle and drag-and-drop events can be intercepted regardless
// of which child widget receives them.
// ---------------------------------------------------------------------------
void CustomMdiSubWindow::installRecursiveFilter(QWidget* w)
{
    if (!w) return;

    w->installEventFilter(this);
    w->setMouseTracking(true);
    w->setAttribute(Qt::WA_Hover);

    // Viewport of scroll areas must be filtered separately
    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(w)) {
        if (QWidget* vp = scrollArea->viewport()) {
            vp->installEventFilter(this);
            vp->setMouseTracking(true);
            vp->setAttribute(Qt::WA_Hover);
        }
    }

    for (QObject* child : w->children()) {
        if (auto* cw = qobject_cast<QWidget*>(child))
            installRecursiveFilter(cw);
    }
}

// ---------------------------------------------------------------------------
// Mouse event handlers for manual edge resizing
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_shaded && !isMaximized()) {
        m_activeEdges = getResizeEdge(event->pos());
        if (m_activeEdges != None) {
            m_resizing          = true;
            m_dragStartPos      = event->globalPosition().toPoint();
            m_dragStartGeometry = geometry();
            event->accept();
            return;
        }
    }
    QMdiSubWindow::mousePressEvent(event);
}

void CustomMdiSubWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_resizing) {
        const QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        QRect newGeom      = m_dragStartGeometry;

        if (m_activeEdges & Left)   newGeom.setLeft(m_dragStartGeometry.left()     + delta.x());
        else if (m_activeEdges & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());
        if (m_activeEdges & Top)    newGeom.setTop(m_dragStartGeometry.top()       + delta.y());
        else if (m_activeEdges & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());

        // Enforce minimum dimensions
        const int minW = FixedUI::minWindowWidth;
        const int minH = FixedUI::minWindowHeight;
        if (newGeom.width()  < minW) {
            if (m_activeEdges & Left) newGeom.setLeft(newGeom.right()  - minW);
            else                      newGeom.setRight(newGeom.left()  + minW);
        }
        if (newGeom.height() < minH) {
            if (m_activeEdges & Top)  newGeom.setTop(newGeom.bottom()  - minH);
            else                      newGeom.setBottom(newGeom.top()  + minH);
        }

        setGeometry(newGeom);
        event->accept();
    } else {
        updateCursor(event->pos());
        QMdiSubWindow::mouseMoveEvent(event);
    }
}

void CustomMdiSubWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_resizing) {
        m_resizing    = false;
        m_activeEdges = None;
        unsetCursor();
        event->accept();
    } else {
        QMdiSubWindow::mouseReleaseEvent(event);
    }
}

void CustomMdiSubWindow::leaveEvent(QEvent* event)
{
    if (!m_resizing) unsetCursor();
    QMdiSubWindow::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// getResizeEdge
// Maps a local mouse position to a bitmask of resize edge flags.
// ---------------------------------------------------------------------------
int CustomMdiSubWindow::getResizeEdge(const QPoint& pos)
{
    int edge         = None;
    const int margin = FixedUI::resizeMargin;

    if (pos.x() < margin)          edge |= Left;
    if (pos.x() > width() - margin) edge |= Right;
    if (pos.y() < margin)           edge |= Top;
    if (pos.y() > height() - margin) edge |= Bottom;

    return edge;
}

// ---------------------------------------------------------------------------
// updateCursor
// Sets the appropriate resize cursor based on the edge under the pointer.
// ---------------------------------------------------------------------------
void CustomMdiSubWindow::updateCursor(const QPoint& pos)
{
    if (m_shaded || m_isMaximized) {
        unsetCursor();
        return;
    }

    const int edge = getResizeEdge(pos);

    if      (edge == (Left | Top)    || edge == (Right | Bottom)) setCursor(Qt::SizeFDiagCursor);
    else if (edge == (Left | Bottom) || edge == (Right | Top))    setCursor(Qt::SizeBDiagCursor);
    else if (edge & Left  || edge & Right)                         setCursor(Qt::SizeHorCursor);
    else if (edge & Top   || edge & Bottom)                        setCursor(Qt::SizeVerCursor);
    else                                                            unsetCursor();
}

// ---------------------------------------------------------------------------
// Shading (collapse to title bar only)
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::showMinimized()
{
    toggleShade();
}

void CustomMdiSubWindow::showMaximized()
{
    m_isMaximized = true;
    m_titleBar->setMaximized(true);
    QMdiSubWindow::showMaximized();
    emit layoutChanged();
}

void CustomMdiSubWindow::showNormal()
{
    m_isMaximized = false;
    m_titleBar->setMaximized(false);
    QMdiSubWindow::showNormal();
    emit layoutChanged();
}

void CustomMdiSubWindow::toggleShade()
{
    // Debounce: ignore rapid consecutive calls within 200 ms
    static QElapsedTimer lastCallTimer;
    if (lastCallTimer.isValid() && lastCallTimer.elapsed() < 200) return;
    lastCallTimer.start();

    m_shaded = !m_shaded;
    m_titleBar->setShaded(m_shaded);

    const QPoint center = geometry().center();

    if (m_shaded) {
        if (!m_isMaximized)
            m_validNormalGeometry = geometry();

        m_wasMaximized  = m_isMaximized;
        m_originalHeight = height();
        m_originalWidth  = width();

        // Grab a thumbnail of the content before hiding it
        ImageViewer* v = viewer();
        QPixmap thumb = (v && v->viewport())
            ? v->viewport()->grab()
            : (m_contentArea->isVisible() ? m_contentArea->grab() : QPixmap());

        m_contentArea->hide();

        const int newH  = m_titleBar->height() + FixedUI::borderWidth;
        const int totalW = static_cast<int>(1.5 * FixedUI::minShadedWidth);
        setGeometry(center.x() - totalW / 2, geometry().top(), totalW, newH);

        emit shadingChanged(true, thumb);

    } else {
        setMinimumWidth(0);
        setMaximumWidth(16777215);
        setMaximumHeight(16777215);
        setMinimumHeight(0);
        m_titleBar->show();
        m_contentArea->show();

        const bool restoreMax = m_wasMaximized;
        if (isMaximized()) QMdiSubWindow::showNormal();

        if (restoreMax) {
            if (QMdiArea* area = mdiArea()) {
                if (QWidget* vp = area->viewport()) setGeometry(vp->rect());
                else                                setGeometry(area->rect());
            } else {
                setGeometry(0, 0, 1920, 1080);
            }

            QTimer::singleShot(50, this, [this]() {
                showMaximized();
                if (m_contentArea) {
                    m_contentArea->show();
                    m_contentArea->resize(width(), height() - m_titleBar->height());
                }
                if (width() < 500) {
                    if (QMdiArea* area = mdiArea())
                        setGeometry(area->viewport()->rect());
                }
                if (ImageViewer* v = viewer()) {
                    v->fitToWindow();
                    QTimer::singleShot(10, v, &ImageViewer::fitToWindow);
                }
            });
        } else {
            setGeometry(center.x() - m_originalWidth / 2,
                        geometry().top(),
                        m_originalWidth, m_originalHeight);
        }

        emit shadingChanged(false, QPixmap());
    }

    emit layoutChanged();
}

// ---------------------------------------------------------------------------
// Event and Event Filter
// ---------------------------------------------------------------------------

bool CustomMdiSubWindow::event(QEvent* event)
{
    return QMdiSubWindow::event(event);
}

bool CustomMdiSubWindow::eventFilter(QObject* obj, QEvent* event)
{
    auto* sender = qobject_cast<QWidget*>(obj);

    // Install the filter on newly added child widgets automatically
    if (event->type() == QEvent::ChildAdded) {
        auto* ce = static_cast<QChildEvent*>(event);
        if (auto* cw = qobject_cast<QWidget*>(ce->child()))
            installRecursiveFilter(cw);
    }

    // Restrict further handling to widgets inside the container
    if (!sender || (!m_container->isAncestorOf(sender) && sender != m_container))
        return QMdiSubWindow::eventFilter(obj, event);

    // Never intercept button events; they are handled by their own connections
    if (qobject_cast<QPushButton*>(sender)) return false;

    auto mapToSubWindow = [this, sender](const QPoint& localPos) {
        return mapFromGlobal(sender->mapToGlobal(localPos));
    };

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && !m_shaded && !isMaximized()) {
            const QPoint posInSub = mapToSubWindow(me->pos());
            m_activeEdges = getResizeEdge(posInSub);
            if (m_activeEdges != None) {
                m_resizing          = true;
                m_dragStartPos      = me->globalPosition().toPoint();
                m_dragStartGeometry = geometry();
                return true;
            }
        }

    } else if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (m_resizing) {
            const QPoint delta = me->globalPosition().toPoint() - m_dragStartPos;
            QRect newGeom      = m_dragStartGeometry;

            if (m_activeEdges & Left)   newGeom.setLeft(m_dragStartGeometry.left()     + delta.x());
            else if (m_activeEdges & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());
            if (m_activeEdges & Top)    newGeom.setTop(m_dragStartGeometry.top()       + delta.y());
            else if (m_activeEdges & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());

            const int minW = FixedUI::minWindowWidth;
            const int minH = FixedUI::minWindowHeight;
            if (newGeom.width()  < minW) {
                if (m_activeEdges & Left) newGeom.setLeft(newGeom.right()  - minW);
                else                      newGeom.setRight(newGeom.left()  + minW);
            }
            if (newGeom.height() < minH) {
                if (m_activeEdges & Top)  newGeom.setTop(newGeom.bottom()  - minH);
                else                      newGeom.setBottom(newGeom.top()  + minH);
            }

            setGeometry(newGeom);
            return true;
        } else {
            updateCursor(mapToSubWindow(me->pos()));
        }

    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (m_resizing) {
            m_resizing    = false;
            m_activeEdges = None;
            unsetCursor();
            return true;
        }

    } else if (event->type() == QEvent::HoverMove) {
        auto* he = static_cast<QHoverEvent*>(event);
        updateCursor(mapToSubWindow(he->position().toPoint()));

    } else if (event->type() == QEvent::DragEnter ||
               event->type() == QEvent::DragMove)
    {
        auto* de = static_cast<QDragMoveEvent*>(event);
        if (de->mimeData()->hasFormat("application/x-tstar-link")) {
            de->acceptProposedAction();
            return true;
        }

    } else if (event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        if (de->mimeData()->hasFormat("application/x-tstar-link")) {
            handleDrop(de);
            return true;
        }

    } else if (event->type() == QEvent::DragLeave) {
        event->accept();
        return true;
    }

    return QMdiSubWindow::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// Tool-window mode (hides the left sidebar strips)
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::setToolWindow(bool isTool)
{
    m_isToolWindow = isTool;
    if (m_nameStrip && m_nameStrip->parentWidget())
        m_nameStrip->parentWidget()->setVisible(!isTool);
}

// ---------------------------------------------------------------------------
// Active state (highlighted border)
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::setActiveState(bool active)
{
    if (m_titleBar) m_titleBar->setActive(active);

    m_container->setStyleSheet(active
        ? "#SubWindowContainer { border: 2px solid #00aaff; background: #202020; padding: 2px; }"
        : "#SubWindowContainer { border: 2px solid #444;    background: #202020; padding: 2px; }"
    );
}

// ---------------------------------------------------------------------------
// setWidget
// Attaches a content widget (typically an ImageViewer) to the subwindow and
// connects the relevant signals for zoom display, title modification markers,
// and view synchronisation.
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::setWidget(QWidget* widget)
{
    if (!widget) return;
    
    m_contentLayout->addWidget(widget, 1);
    widget->setParent(m_contentArea);
    widget->show();
    installRecursiveFilter(widget);

    if (auto* v = qobject_cast<ImageViewer*>(widget)) {

        // Once the layout is settled and fitToWindow() has run, resize the
        // subwindow to match the image at the zoom level chosen by fitToWindow()
        connect(v, &ImageViewer::resized, this, [this]() {
            QTimer::singleShot(50, this, [this]() {
                fitWindowToCurrentImageSize();
            });
        }, Qt::SingleShotConnection);

        connect(v, &ImageViewer::unlinked, [this]() {
            if (m_linkStrip) m_linkStrip->setLinked(false);
        });

        connect(v, &ImageViewer::modifiedChanged, [this](bool mod) {
            QString title = windowTitle();
            if (mod) {
                if (!title.endsWith("*")) setSubWindowTitle(title + "*");
            } else {
                if (title.endsWith("*"))
                    setSubWindowTitle(title.left(title.length() - 1));
            }
        });

        connect(v, &ImageViewer::viewChanged, [this](float scale, float, float) {
            if (m_titleBar)
                m_titleBar->setZoom(static_cast<int>(scale * 100.0f + 0.5f));
        });
        
        if (m_titleBar)
            m_titleBar->setZoom(static_cast<int>(v->getScale() * 100.0f + 0.5f));
    }
}

ImageViewer* CustomMdiSubWindow::viewer() const
{
    return m_contentArea->findChild<ImageViewer*>();
}

// ---------------------------------------------------------------------------
// adjustToImageSize
// Resizes the subwindow so that the viewer area has exactly the same aspect
// ratio as the image buffer, eliminating black bands on first open.
// The resulting window size is clamped between the minimum window constants
// and 90% of the parent MDI area.
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::adjustToImageSize()
{
    ImageViewer* v = m_contentArea->findChild<ImageViewer*>();
    if (!v) return;

    const ImageBuffer& buf = v->getBuffer();
    if (!buf.isValid()) return;

    const int imgW = buf.width();
    const int imgH = buf.height();
    if (imgW <= 0 || imgH <= 0) return;

    // Fixed UI chrome that surrounds the viewer area
    const int reservedW = FixedUI::sidebarWidth
                        + FixedUI::borderWidth * 2
                        + 4; // container padding: 2px per side
    const int reservedH = FixedUI::titleBarHeight
                        + FixedUI::borderWidth * 2
                        + 4; // container padding: 2px per side

    // Minimum viewer area derived from the window minimum size constants
    const int minViewerW = FixedUI::minWindowWidth  - reservedW;
    const int minViewerH = FixedUI::minWindowHeight - reservedH;

    // Maximum viewer area available (90% of the MDI area minus reserved chrome)
    int maxViewerW = 800;
    int maxViewerH = 600;
    if (parentWidget()) {
        const QSize area = parentWidget()->size();
        maxViewerW = static_cast<int>(area.width()  * 0.9) - reservedW;
        maxViewerH = static_cast<int>(area.height() * 0.9) - reservedH;
    }

    const double aspectRatio = static_cast<double>(imgW) / imgH;

    int viewerW = imgW;
    int viewerH = imgH;

    // Scale down uniformly if the image exceeds the maximum viewer area
    if (viewerW > maxViewerW) {
        viewerW = maxViewerW;
        viewerH = static_cast<int>(viewerW / aspectRatio);
    }
    if (viewerH > maxViewerH) {
        viewerH = maxViewerH;
        viewerW = static_cast<int>(viewerH * aspectRatio);
    }

    // Scale up uniformly if the image is smaller than the minimum viewer area
    if (viewerW < minViewerW) {
        viewerW = minViewerW;
        viewerH = static_cast<int>(viewerW / aspectRatio);
    }
    if (viewerH < minViewerH) {
        viewerH = minViewerH;
        viewerW = static_cast<int>(viewerH * aspectRatio);
    }

    resize(viewerW + reservedW, viewerH + reservedH);
}

// ---------------------------------------------------------------------------
// fitWindowToCurrentImageSize
// Resizes the subwindow so that the viewer area matches the image as it is
// currently displayed, taking the active zoom level into account.
// If the resulting size exceeds 90% of the MDI area or falls below the
// minimum window size, the window is clamped to those bounds and the image
// zoom is adjusted so no black bands appear.
// ---------------------------------------------------------------------------
void CustomMdiSubWindow::fitWindowToCurrentImageSize()
{
    ImageViewer* v = m_contentArea->findChild<ImageViewer*>();
    if (!v) return;

    const QGraphicsPixmapItem* item = v->getImageItem();
    if (!item || item->pixmap().isNull()) return;

    const float  scale       = v->getScale();
    const int    displayedW  = static_cast<int>(item->pixmap().width()  * scale);
    const int    displayedH  = static_cast<int>(item->pixmap().height() * scale);
    const double aspectRatio = static_cast<double>(displayedW) / displayedH;

    // Padding kept around the image inside the viewer area
    static constexpr int viewerPadding = 5 * 2; // 5px per side, both axes

    // Fixed UI chrome surrounding the viewer
    const int reservedW = FixedUI::sidebarWidth
                        + FixedUI::borderWidth * 2
                        + 4                  // container padding: 2px per side
                        + viewerPadding;
    const int reservedH = FixedUI::titleBarHeight
                        + FixedUI::borderWidth * 2
                        + 4                  // container padding: 2px per side
                        + viewerPadding;

    // Viewer area bounds
    const int minViewerW = FixedUI::minWindowWidth  - reservedW;
    const int minViewerH = FixedUI::minWindowHeight - reservedH;

    int maxViewerW = 800;
    int maxViewerH = 600;
    if (parentWidget()) {
        const QSize area = parentWidget()->size();
        maxViewerW = static_cast<int>(area.width()  * 0.9) - reservedW;
        maxViewerH = static_cast<int>(area.height() * 0.9) - reservedH;
    }

    int viewerW = displayedW;
    int viewerH = displayedH;

    // Clamp to maximum bounds, preserving aspect ratio
    if (viewerW > maxViewerW) {
        viewerW = maxViewerW;
        viewerH = static_cast<int>(viewerW / aspectRatio);
    }
    if (viewerH > maxViewerH) {
        viewerH = maxViewerH;
        viewerW = static_cast<int>(viewerH * aspectRatio);
    }

    // Clamp to minimum bounds, preserving aspect ratio
    if (viewerW < minViewerW) {
        viewerW = minViewerW;
        viewerH = static_cast<int>(viewerW / aspectRatio);
    }
    if (viewerH < minViewerH) {
        viewerH = minViewerH;
        viewerW = static_cast<int>(viewerH * aspectRatio);
    }

    resize(viewerW + reservedW, viewerH + reservedH);

    // If the viewer area was clamped, update the image zoom so it fills
    // the viewer exactly with no black bands
    const float newScaleW = static_cast<float>(viewerW) / item->pixmap().width();
    const float newScaleH = static_cast<float>(viewerH) / item->pixmap().height();
    const float newScale  = std::min(newScaleW, newScaleH);

    if (std::abs(newScale - scale) > 1e-4f)
        v->setTransform(QTransform::fromScale(newScale, newScale));
}

// ---------------------------------------------------------------------------
// requestRename
// Prompts the user for a new window title via an input dialog.
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::requestRename()
{
    bool    ok;
    QString text = QInputDialog::getText(this, tr("Rename View"), tr("New Name:"),
                                         QLineEdit::Normal, windowTitle(), &ok);
    if (ok && !text.isEmpty()) {
        setSubWindowTitle(text);
        if (auto* v = m_contentArea->findChild<ImageViewer*>())
            v->getBuffer().setName(text);
    }
}

void CustomMdiSubWindow::setSubWindowTitle(const QString& title)
{
    setWindowTitle(title);
    if (m_titleBar)  m_titleBar->setTitle(title);
    emit layoutChanged();
}

// ---------------------------------------------------------------------------
// Layout change propagation
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::resizeEvent(QResizeEvent* event)
{
    QMdiSubWindow::resizeEvent(event);
    emit layoutChanged();
}

void CustomMdiSubWindow::moveEvent(QMoveEvent* event)
{
    QMdiSubWindow::moveEvent(event);
    emit layoutChanged();
}

// ---------------------------------------------------------------------------
// Drag-and-drop forwarding
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link")      ||
        event->mimeData()->hasFormat("application/x-tstar-adapt")     ||
        event->mimeData()->hasFormat("application/x-tstar-duplicate"))
    {
        event->acceptProposedAction();
    } else {
        QMdiSubWindow::dragEnterEvent(event);
    }
}

void CustomMdiSubWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link")      ||
        event->mimeData()->hasFormat("application/x-tstar-adapt")     ||
        event->mimeData()->hasFormat("application/x-tstar-duplicate"))
    {
        event->acceptProposedAction();
    } else {
        QMdiSubWindow::dragMoveEvent(event);
    }
}

void CustomMdiSubWindow::dropEvent(QDropEvent* event)
{
    handleDrop(event);
}

// ---------------------------------------------------------------------------
// handleDrop
// Processes the three supported drop actions:
//   - x-tstar-link:      Synchronise pan/zoom between two viewers
//   - x-tstar-adapt:     Copy geometry from the source window
//   - (fallback):        Delegate to the base class
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::handleDrop(QDropEvent* event)
{
    if (event->mimeData()->hasFormat("application/x-tstar-link")) {
        bool     ok;
        quintptr ptrVal    = event->mimeData()->data("application/x-tstar-link").toULongLong(&ok, 16);
        if (!ok) return;

        auto* sourceWin = reinterpret_cast<CustomMdiSubWindow*>(ptrVal);
        if (!sourceWin || sourceWin == this) return;

        ImageViewer* sourcePtr = sourceWin->findChild<ImageViewer*>();
        ImageViewer* targetPtr = this->findChild<ImageViewer*>();
        if (!sourcePtr || !targetPtr) return;

        // Collect all currently linked viewers into a group so the new link
        // inherits the existing synchronisation network
        struct Member { ImageViewer* viewer; CustomMdiSubWindow* win; };
        QVector<Member> group;
        group.append({sourcePtr, sourceWin});
        group.append({targetPtr, this});

        if (QMdiArea* area = mdiArea()) {
            for (QMdiSubWindow* sub : area->subWindowList()) {
                if (sub == sourceWin || sub == this) continue;
                auto* csw = qobject_cast<CustomMdiSubWindow*>(sub);
                if (!csw) continue;
                ImageViewer* v = csw->viewer();
                if (v && v->isLinked())
                    group.append({v, csw});
            }
        }

        // Connect every pair in the group bidirectionally
        for (int i = 0; i < group.size(); ++i) {
            for (int j = i + 1; j < group.size(); ++j) {
                connect(group[i].viewer, &ImageViewer::viewChanged,
                        group[j].viewer, &ImageViewer::syncView, Qt::UniqueConnection);
                connect(group[j].viewer, &ImageViewer::viewChanged,
                        group[i].viewer, &ImageViewer::syncView, Qt::UniqueConnection);
            }
        }

        // Synchronise all viewers to the source pan/zoom position
        const float srcScale = sourcePtr->getScale();
        const float srcH     = sourcePtr->getHBarLoc();
        const float srcV     = sourcePtr->getVBarLoc();
        for (int i = 1; i < group.size(); ++i)
            group[i].viewer->syncView(srcScale, srcH, srcV);

        // Mark all group members as linked
        for (auto& m : group) {
            m.viewer->setLinked(true);
            if (m.win->m_linkStrip) m.win->m_linkStrip->setLinked(true);
        }

        // Automatically unlink remaining group members when a viewer is destroyed
        QPointer<CustomMdiSubWindow> sourceWinSafe = sourceWin;
        for (int i = 1; i < group.size(); ++i) {
            QPointer<ImageViewer>          partnerSafe    = group[i].viewer;
            QPointer<CustomMdiSubWindow>   partnerWinSafe = group[i].win;
            QPointer<ImageViewer>          srcSafe        = sourcePtr;

            connect(sourcePtr, &QObject::destroyed,
                    [partnerSafe, partnerWinSafe, srcSafe]() {
                        if (!partnerSafe) return;
                        QMdiArea* area = partnerWinSafe ? partnerWinSafe->mdiArea() : nullptr;
                        bool hasOthers = false;
                        if (area) {
                            for (QMdiSubWindow* sub : area->subWindowList()) {
                                ImageViewer* v = sub->findChild<ImageViewer*>();
                                if (v && v != partnerSafe && v != srcSafe && v->isLinked()) {
                                    hasOthers = true;
                                    break;
                                }
                            }
                        }
                        if (!hasOthers) {
                            partnerSafe->setLinked(false);
                            if (partnerWinSafe && partnerWinSafe->m_linkStrip)
                                partnerWinSafe->m_linkStrip->setLinked(false);
                        }
                    });

            connect(group[i].viewer, &QObject::destroyed,
                    [srcSafe, sourceWinSafe, partnerSafe]() {
                        if (!srcSafe) return;
                        QMdiArea* area = sourceWinSafe ? sourceWinSafe->mdiArea() : nullptr;
                        bool hasOthers = false;
                        if (area) {
                            for (QMdiSubWindow* sub : area->subWindowList()) {
                                ImageViewer* v = sub->findChild<ImageViewer*>();
                                if (v && v != srcSafe && v != partnerSafe && v->isLinked()) {
                                    hasOthers = true;
                                    break;
                                }
                            }
                        }
                        if (!hasOthers) {
                            srcSafe->setLinked(false);
                            if (sourceWinSafe && sourceWinSafe->m_linkStrip)
                                sourceWinSafe->m_linkStrip->setLinked(false);
                        }
                    });
        }

        event->accept();

    } else if (event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        bool     ok;
        quintptr ptr = event->mimeData()->data("application/x-tstar-adapt").toULongLong(&ok, 16);
        if (ok) {
            auto* source = reinterpret_cast<CustomMdiSubWindow*>(ptr);
            if (source && source != this) {
                if (source->isMaximized()) showMaximized();
                else { showNormal(); resize(source->size()); }
                event->acceptProposedAction();
            }
        }
    } else {
        QMdiSubWindow::dropEvent(event);
    }
}

// ---------------------------------------------------------------------------
// canClose
// Checks whether the window can safely be closed, prompting the user if
// there are unsaved changes or if a processing tool is currently using the viewer.
// ---------------------------------------------------------------------------

bool CustomMdiSubWindow::canClose()
{
    if (property("bypassCloseChecks").toBool()) return true;
    if (m_isToolWindow)                          return true;

    ImageViewer* v = viewer();
    if (v) {
        // Check if a tool dialog is currently using this viewer
        MainWindowCallbacks* mw = nullptr;
        QWidget* p = this;
        while (p) {
            mw = dynamic_cast<MainWindowCallbacks*>(p);
            if (mw) break;
            p = p->parentWidget();
        }
        if (mw) {
            QString toolName;
            if (mw->isViewerInUse(v, &toolName)) {
                QMessageBox::warning(this, tr("View in use"),
                    tr("This view is currently in use by the '%1' tool. "
                       "Please close the tool or select different views before "
                       "closing this image.").arg(toolName));
                return false;
            }
        }

        // Prompt the user if the image has unsaved modifications
        if (v->isModified()) {
            const int ret = QMessageBox::question(this, tr("Unsaved Changes"),
                tr("The image '%1' has unsaved changes. Do you want to close it?")
                    .arg(windowTitle()),
                QMessageBox::Yes | QMessageBox::No);
            if (ret == QMessageBox::No) return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Animation: fade-out close
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::animateClose()
{
    if (m_isClosing) return;
    if (!canClose())  return;

    m_isClosing = true;

    if (m_anim) { m_anim->stop(); delete m_anim; m_anim = nullptr; }

    if (!m_effect) {
        m_effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(m_effect);
    }
    m_effect->setOpacity(1.0);

    m_anim = new QPropertyAnimation(m_effect, "opacity", this);
    m_anim->setDuration(150);
    m_anim->setStartValue(1.0);
    m_anim->setEndValue(0.0);
    m_anim->setEasingCurve(QEasingCurve::Linear);

    connect(m_anim, &QPropertyAnimation::finished, this, []() {});
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        QMdiSubWindow::close();
    });

    m_anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CustomMdiSubWindow::closeEvent(QCloseEvent* event)
{
    if (m_skipCloseAnimation) {
        if (canClose()) {
            event->accept();
            QMdiSubWindow::closeEvent(event);
        } else {
            event->ignore();
        }
        return;
    }

    if (m_isClosing) {
        QMdiSubWindow::closeEvent(event);
    } else {
        event->ignore();
        animateClose();
    }
}

// ---------------------------------------------------------------------------
// Animation: fade-in on show
// ---------------------------------------------------------------------------

void CustomMdiSubWindow::showEvent(QShowEvent* event)
{
    QMdiSubWindow::showEvent(event);
    startFadeIn();
}

void CustomMdiSubWindow::startFadeIn()
{
    if (m_anim) { m_anim->stop(); delete m_anim; }

    if (!m_effect) {
        m_effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(m_effect);
    }

    m_effect->setOpacity(0.0);
    m_anim = new QPropertyAnimation(m_effect, "opacity", this);
    m_anim->setDuration(250);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutQuad);

    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        setGraphicsEffect(nullptr);
        m_effect = nullptr;
    });

    m_anim->start();
}

void CustomMdiSubWindow::startFadeOut()
{
    animateClose();
}