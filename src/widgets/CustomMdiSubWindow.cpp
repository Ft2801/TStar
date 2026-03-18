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

namespace FixedUI {
    constexpr int sidebarWidth = 26;
    constexpr int titleBarHeight = 30;
    constexpr int buttonSize = 24;
    constexpr int iconSize = 16;
    constexpr int dragPixmapSize = 32;
    constexpr int minShadedWidth = 200;
    constexpr int borderWidth = 2;
    constexpr int minWindowWidth = 200;
    constexpr int minWindowHeight = 150;
    constexpr int resizeMargin = 8;
}

// Helper to create QIcon from SVG string
static QIcon iconFromSvg(const QString& svg, QWidget* widget = nullptr) {
    Q_UNUSED(widget);
    QByteArray ba = svg.toUtf8();
    QSvgRenderer renderer(ba);
    int size = FixedUI::iconSize;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    renderer.render(&painter);
    return QIcon(pix);
}

// --- NameStrip ---
NameStrip::NameStrip(QWidget *parent) : QWidget(parent) {
    setFixedWidth(FixedUI::sidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true);
}

void NameStrip::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt") ||
        event->mimeData()->hasFormat("application/x-tstar-duplicate")) {
        event->acceptProposedAction();
    }
}

void NameStrip::setTitle(const QString& title) {
    // Truncate to max 15 chars with ellipsis to prevent layout issues
    const int maxChars = 15;
    if (title.length() > maxChars) {
        m_title = title.left(maxChars - 1) + QChar(0x2026); // Unicode ellipsis
    } else {
        m_title = title;
    }
    
    QFontMetrics fm(QFont("Segoe UI", 9));
    int textLen = fm.horizontalAdvance(m_title);
    int desiredHeight = textLen + 40;
    setFixedHeight(desiredHeight);
    update();
}

int NameStrip::preferredHeight() const {
    QFontMetrics fm(QFont("Segoe UI", 9));
    return fm.horizontalAdvance(m_title) + 40;
}

void NameStrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#3a3a3a")); // Standard gray
    p.setPen(Qt::white);
    p.setFont(QFont("Segoe UI", 9));
    p.save();
    p.translate(0, height()); 
    p.rotate(-90);
    QRect textRect(0, 0, height(), width());
    p.drawText(textRect, Qt::AlignCenter, m_title);
    p.restore();
    p.setPen(QColor("#222222"));
    p.drawRect(0,0,width()-1, height()-1);
}

void NameStrip::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit renameRequested();
    }
}

void NameStrip::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        event->acceptProposedAction();
    }
}

void NameStrip::dropEvent(QDropEvent *event) {
    QWidget* widget = this;
    while(widget && !widget->inherits("CustomMdiSubWindow")) {
        widget = widget->parentWidget();
    }
    CustomMdiSubWindow* win = qobject_cast<CustomMdiSubWindow*>(widget);
    if(win) win->handleDrop(event);
}

void NameStrip::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
    }
}

void NameStrip::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) return;
    
    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData();
    
    QWidget* widget = this;
    while(widget && !widget->inherits("CustomMdiSubWindow")) {
        widget = widget->parentWidget();
    }
    CustomMdiSubWindow* sourceWin = qobject_cast<CustomMdiSubWindow*>(widget);
    
    if (sourceWin) {
        // Use "duplicate" mime type as expected by MainWindow
        QByteArray ptrData = QByteArray::number((quintptr)sourceWin, 16);
        mimeData->setData("application/x-tstar-duplicate", ptrData);
        drag->setMimeData(mimeData);
        
        int dragSize = FixedUI::dragPixmapSize;
        QPixmap pix(dragSize, dragSize);
        pix.fill(Qt::cyan); // Cyan for duplicate
        drag->setPixmap(pix);
        drag->exec(Qt::CopyAction);
    }
}

// --- LinkStrip ---
LinkStrip::LinkStrip(QWidget *parent) : QWidget(parent) {
    setFixedWidth(FixedUI::sidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true);
}

void LinkStrip::setLinked(bool linked) {
    if (m_linked == linked) return;
    m_linked = linked;
    update();
}

void LinkStrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    QColor bg = m_linked ? QColor("#2a5a2a") : QColor("#3a3a3a");
    p.fillRect(rect(), bg);
    
    p.setPen(m_linked ? Qt::green : Qt::white);
    p.setFont(QFont("Segoe UI", 9));
    p.save();
    p.translate(0, height()); 
    p.rotate(-90);
    QRect textRect(0, 0, height(), width());
    p.drawText(textRect, Qt::AlignCenter, m_linked ? tr("LINKED") : tr("LINK"));
    p.restore();
    
    p.setPen(QColor("#222222"));
    p.drawRect(0,0,width()-1, height()-1);
}

void LinkStrip::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragging = false;
    }
}

void LinkStrip::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) return;

    m_dragging = true;

    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData();

    QWidget* widget = this;
    while(widget && !widget->inherits("CustomMdiSubWindow")) {
        widget = widget->parentWidget();
    }
    CustomMdiSubWindow* sourceWin = qobject_cast<CustomMdiSubWindow*>(widget);
    if (sourceWin) {
        QByteArray ptrData = QByteArray::number((quintptr)sourceWin, 16);
        mimeData->setData("application/x-tstar-link", ptrData);
        drag->setMimeData(mimeData);
        int dragSize = FixedUI::dragPixmapSize;
        QPixmap pix(dragSize, dragSize);
        pix.fill(Qt::green);
        drag->setPixmap(pix);
        drag->exec(Qt::LinkAction);
    }
    m_dragging = false;
}

void LinkStrip::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !m_dragging && m_linked) {
        emit linkToggled(); // Unlink only on pure click (no drag occurred)
    }
    m_dragging = false;
}

void LinkStrip::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        event->acceptProposedAction();
    }
}

void LinkStrip::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        event->acceptProposedAction();
    }
}

void LinkStrip::dropEvent(QDropEvent *event) {
    QWidget* widget = this;
    while(widget && !widget->inherits("CustomMdiSubWindow")) {
        widget = widget->parentWidget();
    }
    CustomMdiSubWindow* win = qobject_cast<CustomMdiSubWindow*>(widget);
    if(win) win->handleDrop(event);
}

// --- AdaptStrip ---
AdaptStrip::AdaptStrip(QWidget *parent) : QWidget(parent) {
    setFixedWidth(FixedUI::sidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true);
}

void AdaptStrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#3a3a3a"));
    
    p.setPen(Qt::white);
    p.setFont(QFont("Segoe UI", 9));
    p.save();
    p.translate(0, height()); 
    p.rotate(-90);
    QRect textRect(0, 0, height(), width());
    p.drawText(textRect, Qt::AlignCenter, tr("ADAPT"));
    p.restore();
    
    p.setPen(QColor("#222222"));
    p.drawRect(0,0,width()-1, height()-1);
}

void AdaptStrip::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
    }
}

void AdaptStrip::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) return;
    
    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData();
    
    QWidget* widget = this;
    while(widget && !widget->inherits("CustomMdiSubWindow")) {
        widget = widget->parentWidget();
    }
    CustomMdiSubWindow* sourceWin = qobject_cast<CustomMdiSubWindow*>(widget);
    
    if (sourceWin) {
        QByteArray ptrData = QByteArray::number((quintptr)sourceWin, 16);
        mimeData->setData("application/x-tstar-adapt", ptrData);
        drag->setMimeData(mimeData);
        
        int dragSize = FixedUI::dragPixmapSize;
        QPixmap pix(dragSize, dragSize);
        pix.fill(Qt::yellow);
        drag->setPixmap(pix);
        drag->exec(Qt::MoveAction);
    }
}

void AdaptStrip::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        event->acceptProposedAction();
    }
}

void AdaptStrip::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        event->acceptProposedAction();
    }
}

void AdaptStrip::dropEvent(QDropEvent *event) {
    QWidget* widget = this;
    while(widget && !widget->inherits("CustomMdiSubWindow")) {
        widget = widget->parentWidget();
    }
    CustomMdiSubWindow* win = qobject_cast<CustomMdiSubWindow*>(widget);
    if(win) win->handleDrop(event);
}

// --- Custom Title Bar ---

CustomTitleBar::CustomTitleBar(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true); // Ensure background is painted
    setFixedHeight(FixedUI::titleBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_active = false;
    
    QHBoxLayout* layout = new QHBoxLayout(this);
    int margin = 5;
    layout->setContentsMargins(margin, 0, margin, 4);
    layout->setSpacing(4);
    
    m_titleLabel = new QLabel("", this);
    m_titleLabel->setStyleSheet("color: #ddd; font-weight: bold; font-family: 'Segoe UI'; font-size: 11px; background: transparent;");
    
    // Zoom Label (Left of title)
    m_zoomLabel = new QLabel("", this);
    m_zoomLabel->setStyleSheet("QLabel { color: #aaa; font-family: 'Segoe UI'; font-size: 10px; background-color: transparent; margin-right: 5px; border: none; }");
    m_zoomLabel->setVisible(false); // Hide initially
    
    int btnSize = FixedUI::buttonSize;
    int icnSize = FixedUI::iconSize;
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
    
    m_minBtn = new QPushButton(this);
    configBtn(m_minBtn, Icons::WIN_SHADE, "#555");
    m_maxBtn = new QPushButton(this);
    configBtn(m_maxBtn, Icons::WIN_RESTORE, "#555");
    m_closeBtn = new QPushButton(this);
    configBtn(m_closeBtn, Icons::WIN_CLOSE, "#c00");

    // Right-aligned buttons
    m_rightSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_zoomLabel); 
    layout->addSpacerItem(m_rightSpacer);
    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);
    
    connect(m_closeBtn, &QPushButton::clicked, this, &CustomTitleBar::closeClicked);
    connect(m_minBtn, &QPushButton::clicked, this, &CustomTitleBar::minimizeClicked);
    connect(m_maxBtn, &QPushButton::clicked, this, &CustomTitleBar::maximizeClicked);
    
    updateStyle();
}

void CustomTitleBar::setTitle(const QString& title) {
    m_titleLabel->setProperty("fullTitle", title);
    if (m_shaded) {
        // Elide title to fit fixed shaded width
        QFontMetrics fm(m_titleLabel->font());
        int btnSize = FixedUI::buttonSize;
        int buttonsW = 3 * btnSize + 20;
        int available = 1.5 * FixedUI::minShadedWidth - buttonsW - 16;
        m_titleLabel->setText(fm.elidedText(title, Qt::ElideRight, std::max(available, 20)));
    } else {
        m_titleLabel->setText(title);
    }
}

void CustomTitleBar::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    updateStyle();
}

void CustomTitleBar::setShaded(bool shaded) {
    m_shaded = shaded;
    m_minBtn->setIcon(iconFromSvg(m_shaded ? Icons::WIN_UNSHADE : Icons::WIN_SHADE));
    // Hide maximize button when shaded
    setMaximizeButtonVisible(!shaded);
    // Re-apply title with correct elision for shaded state
    QString fullTitle = m_titleLabel->property("fullTitle").toString();
    if (fullTitle.isEmpty()) fullTitle = m_titleLabel->text();
    setTitle(fullTitle);
}

void CustomTitleBar::setZoom(int percent) {
    if (percent <= 0) {
        m_zoomLabel->setVisible(false);
    } else {
        m_zoomLabel->setText(QString("%1%").arg(percent));
        m_zoomLabel->setVisible(true);
    }
}

void CustomTitleBar::setMaximized(bool maximized) {
    m_maximized = maximized;
    // User requested swap: use arrows (RESTORE) for 'to maximize' and square (MAXIMIZE) for 'to restore'
    m_maxBtn->setIcon(iconFromSvg(m_maximized ? Icons::WIN_MAXIMIZE : Icons::WIN_RESTORE));
}

void CustomTitleBar::setMaximizeButtonVisible(bool visible) {
    m_maxBtn->setVisible(visible);
}

void CustomTitleBar::updateStyle() {
    QString bg = m_active ? "#3a4b5b" : "#2a2a2a";
    setStyleSheet(QString("background-color: %1; border-bottom: 1px solid #222;").arg(bg));
}

void CustomTitleBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragPos = event->globalPosition().toPoint();
        event->accept();
    }
}

void CustomTitleBar::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        QWidget* p = parentWidget(); 
        while (p) {
            QMdiSubWindow* sub = qobject_cast<QMdiSubWindow*>(p);
            if (sub && !sub->isMaximized()) {
                 QPoint delta = event->globalPosition().toPoint() - m_dragPos;
                 sub->move(sub->pos() + delta);
                 m_dragPos = event->globalPosition().toPoint();
                 event->accept();
                 return;
            }
            p = p->parentWidget();
        }
    }
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit minimizeClicked();
    }
}

// --- Custom MDI SubWindow ---

CustomMdiSubWindow::CustomMdiSubWindow(QWidget *parent) : QMdiSubWindow(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
    setAttribute(Qt::WA_DeleteOnClose);
    
    setAcceptDrops(true);
    installEventFilter(this);
    m_container = new QFrame(this);
    m_container->setObjectName("SubWindowContainer");
    m_container->setStyleSheet("#SubWindowContainer { border: 2px solid #444; background: #202020; padding: 2px; }");
    m_mainLayout = new QVBoxLayout(m_container);
    m_mainLayout->setContentsMargins(0, 0, 0, 0); 
    m_mainLayout->setSpacing(0);
    m_titleBar = new CustomTitleBar(m_container);
    connect(m_titleBar, &CustomTitleBar::closeClicked, this, &CustomMdiSubWindow::animateClose);
    connect(m_titleBar, &CustomTitleBar::minimizeClicked, this, &CustomMdiSubWindow::showMinimized);
    connect(m_titleBar, &CustomTitleBar::maximizeClicked, [this](){
        if (m_isMaximized) {
            // If shaded, we MUST unshade first before restoring to normal
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
            
            // Restore valid normal geometry if available
            if (!m_validNormalGeometry.isNull()) {
                setGeometry(m_validNormalGeometry);
            } else {
                resize(800, 600); // Reasonable fallback
                // Center on screen
                if (QMdiArea* area = mdiArea()) {
                     setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), area->viewport()->rect()));
                }
            }
            
            if (m_contentArea) {
                 m_contentArea->show();
                 m_contentArea->resize(width(), height() - m_titleBar->height());
            }
            
            // Auto-fit image to new window size
            if (ImageViewer* v = viewer()) {
                QTimer::singleShot(50, v, &ImageViewer::fitToWindow);
            }
        } else {
            // Save valid normal geometry before maximizing
            if (!m_shaded) {
                m_validNormalGeometry = geometry();
            }

            // If shaded, we need to unshade first with robust logic
            if (m_shaded) {
                m_shaded = false;
                m_titleBar->setShaded(false);
                setMinimumWidth(0);
                setMaximumWidth(16777215);
                setMaximumHeight(16777215);
                setMinimumHeight(0);
                m_titleBar->show();
                m_contentArea->show();
                 
                 // 2. Expand to full viewport to ensure maximize works visually
                 if (QMdiArea* area = mdiArea()) {
                     if (QWidget* vp = area->viewport()) setGeometry(vp->rect());
                     else setGeometry(area->rect());
                 } else {
                     setGeometry(0, 0, 1920, 1080);
                 }
                 
                 // 3. Defer maximize
                 QTimer::singleShot(50, this, [this](){
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
            // Auto-fit image to new window size
            if (ImageViewer* v = viewer()) {
                QTimer::singleShot(50, v, &ImageViewer::fitToWindow);
            }
        }
    });

    m_mainLayout->addWidget(m_titleBar);
    m_contentArea = new QWidget(m_container);
    m_contentArea->setStyleSheet("background: #202020;");
    m_contentLayout = new QHBoxLayout(m_contentArea);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    
    // Left Strip Container
    QWidget* leftStrip = new QWidget(m_contentArea);
    leftStrip->setFixedWidth(FixedUI::sidebarWidth);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftStrip);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    
    m_nameStrip = new NameStrip(leftStrip);
    connect(m_nameStrip, &NameStrip::renameRequested, this, &CustomMdiSubWindow::requestRename);
    
    m_linkStrip = new LinkStrip(leftStrip);
    connect(m_linkStrip, &LinkStrip::linkToggled, [this](){
        ImageViewer* v = m_contentArea->findChild<ImageViewer*>();
        if (v && v->isLinked()) {
            if (QMdiArea* area = mdiArea()) {
                QVector<QPair<ImageViewer*, CustomMdiSubWindow*>> others;
                for (QMdiSubWindow* sub : area->subWindowList()) {
                    if (sub == this) continue;
                    CustomMdiSubWindow* otherWin = qobject_cast<CustomMdiSubWindow*>(sub);
                    if (!otherWin) continue;
                    ImageViewer* ov = otherWin->viewer();
                    if (ov && ov->isLinked())
                        others.append({ov, otherWin});
                }

                for (auto& p : others) {
                    disconnect(v,       &ImageViewer::viewChanged, p.first, &ImageViewer::syncView);
                    disconnect(p.first, &ImageViewer::viewChanged, v,       &ImageViewer::syncView);
                }

                v->setLinked(false);

                for (auto& p : others) {
                    bool hasPeer = false;
                    for (auto& q : others) {
                        if (q.first != p.first && q.first->isLinked()) { hasPeer = true; break; }
                    }
                    if (!hasPeer) {
                        p.first->setLinked(false);
                        if (p.second->m_linkStrip) p.second->m_linkStrip->setLinked(false);
                    }
                }
            } else {
                v->setLinked(false);
            }
        }
        m_linkStrip->setLinked(false);
    });

    m_adaptStrip = new AdaptStrip(leftStrip);

    leftLayout->addWidget(m_nameStrip);
    leftLayout->addWidget(m_linkStrip);
    leftLayout->addWidget(m_adaptStrip);
    leftLayout->addStretch();  
    
    m_contentLayout->addWidget(leftStrip);
    m_mainLayout->addWidget(m_contentArea, 1);
    
    QMdiSubWindow::setWidget(m_container);
    setAcceptDrops(true);
    installRecursiveFilter(m_container);
    setMouseTracking(true);
}

void CustomMdiSubWindow::installRecursiveFilter(QWidget* w) {
    if (!w) return;
    w->installEventFilter(this);
    w->setMouseTracking(true);
    w->setAttribute(Qt::WA_Hover);
    
    if (QAbstractScrollArea* scrollArea = qobject_cast<QAbstractScrollArea*>(w)) {
        if (QWidget* vp = scrollArea->viewport()) {
            vp->installEventFilter(this);
            vp->setMouseTracking(true);
            vp->setAttribute(Qt::WA_Hover);
        }
    }
    
    for (QObject* child : w->children()) {
        if (QWidget* cw = qobject_cast<QWidget*>(child)) {
            installRecursiveFilter(cw);
        }
    }
}

void CustomMdiSubWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !m_shaded && !isMaximized()) {
        m_activeEdges = getResizeEdge(event->pos());
        if (m_activeEdges != None) {
            m_resizing = true;
            m_dragStartPos = event->globalPosition().toPoint();
            m_dragStartGeometry = geometry();
            event->accept();
            return;
        }
    }
    QMdiSubWindow::mousePressEvent(event);
}

void CustomMdiSubWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_resizing) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        QRect newGeom = m_dragStartGeometry;

        if (m_activeEdges & Left) {
            newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
        } else if (m_activeEdges & Right) {
            newGeom.setRight(m_dragStartGeometry.right() + delta.x());
        }

        if (m_activeEdges & Top) {
            newGeom.setTop(m_dragStartGeometry.top() + delta.y());
        } else if (m_activeEdges & Bottom) {
            newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());
        }

        int minW = FixedUI::minWindowWidth;
        int minH = FixedUI::minWindowHeight;
        if (newGeom.width() < minW) {
            if (m_activeEdges & Left) newGeom.setLeft(newGeom.right() - minW);
            else newGeom.setRight(newGeom.left() + minW);
        }
        if (newGeom.height() < minH) {
            if (m_activeEdges & Top) newGeom.setTop(newGeom.bottom() - minH);
            else newGeom.setBottom(newGeom.top() + minH);
        }

        setGeometry(newGeom);
        event->accept();
    } else {
        updateCursor(event->pos());
        QMdiSubWindow::mouseMoveEvent(event);
    }
}

void CustomMdiSubWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (m_resizing) {
        m_resizing = false;
        m_activeEdges = None;
        unsetCursor();
        event->accept();
    } else {
        QMdiSubWindow::mouseReleaseEvent(event);
    }
}

void CustomMdiSubWindow::leaveEvent(QEvent *event) {
    if (!m_resizing) unsetCursor();
    QMdiSubWindow::leaveEvent(event);
}

int CustomMdiSubWindow::getResizeEdge(const QPoint& pos) {
    int edge = None;
    int margin = FixedUI::resizeMargin;
    if (pos.x() < margin) edge |= Left;
    if (pos.x() > width() - margin) edge |= Right;
    if (pos.y() < margin) edge |= Top;
    if (pos.y() > height() - margin) edge |= Bottom;
    return edge;
}

void CustomMdiSubWindow::updateCursor(const QPoint& pos) {
    if (m_shaded || m_isMaximized) {
        unsetCursor();
        return;
    }
    int edge = getResizeEdge(pos);
    if (edge == (Left | Top) || edge == (Right | Bottom)) setCursor(Qt::SizeFDiagCursor);
    else if (edge == (Left | Bottom) || edge == (Right | Top)) setCursor(Qt::SizeBDiagCursor);
    else if (edge & Left || edge & Right) setCursor(Qt::SizeHorCursor);
    else if (edge & Top || edge & Bottom) setCursor(Qt::SizeVerCursor);
    else unsetCursor();
}

void CustomMdiSubWindow::showMinimized() {
    toggleShade();
}

void CustomMdiSubWindow::showMaximized() {
    m_isMaximized = true;
    m_titleBar->setMaximized(true);
    QMdiSubWindow::showMaximized();
    emit layoutChanged();
}

void CustomMdiSubWindow::showNormal() {
    m_isMaximized = false;
    m_titleBar->setMaximized(false);
    QMdiSubWindow::showNormal();
    emit layoutChanged();
}

void CustomMdiSubWindow::toggleShade() {
    static QElapsedTimer lastCallTimer;
    if (lastCallTimer.isValid() && lastCallTimer.elapsed() < 200) {
        return;
    }
    lastCallTimer.start();
    
    m_shaded = !m_shaded;
    m_titleBar->setShaded(m_shaded);
    
    QPoint center = geometry().center();
    
    if (m_shaded) {
        if (!m_isMaximized) {
            m_validNormalGeometry = geometry();
        }

        m_wasMaximized = m_isMaximized; 
        m_originalHeight = height();
        m_originalWidth = width(); 

        ImageViewer* v = viewer();
        QPixmap thumb = (v && v->viewport()) 
                        ? v->viewport()->grab() 
                        : (m_contentArea->isVisible() ? m_contentArea->grab() : QPixmap());
        
        m_contentArea->hide();
        int newH = m_titleBar->height() + FixedUI::borderWidth;
        
        int totalW = 1.5 * FixedUI::minShadedWidth; 
        
        int newX = center.x() - totalW / 2;
        int newY = geometry().top(); 
        
        setGeometry(newX, newY, totalW, newH);
        emit shadingChanged(true, thumb);
        
    } else {
        setMinimumWidth(0);
        setMaximumWidth(16777215);
        setMaximumHeight(16777215); 
        setMinimumHeight(0);
        
        m_titleBar->show();
        m_contentArea->show();
        
        bool restoreMax = m_wasMaximized;
        if (isMaximized()) QMdiSubWindow::showNormal();

        if (restoreMax) {
            if (QMdiArea* area = mdiArea()) {
                if (QWidget* vp = area->viewport()) {
                    setGeometry(vp->rect());
                } else {
                    setGeometry(area->rect());
                }
            } else {
                 setGeometry(0, 0, 1920, 1080); 
            }
            
            QTimer::singleShot(50, this, [this](){
                showMaximized();
                if (m_contentArea) {
                    m_contentArea->show(); 
                    m_contentArea->resize(width(), height() - m_titleBar->height());
                }
                
                if (width() < 500) {
                    if (QMdiArea* area = mdiArea()) setGeometry(area->viewport()->rect());
                }
                
                if (ImageViewer* v = viewer()) {
                    v->fitToWindow();
                    QTimer::singleShot(10, v, &ImageViewer::fitToWindow);
                }
            });
        } else {
            int newX = center.x() - m_originalWidth / 2;
            int newY = geometry().top();
            setGeometry(newX, newY, m_originalWidth, m_originalHeight);
        }
        emit shadingChanged(false, QPixmap());
    }

    emit layoutChanged();
}

bool CustomMdiSubWindow::event(QEvent *event) {
    return QMdiSubWindow::event(event);
}

bool CustomMdiSubWindow::eventFilter(QObject *obj, QEvent *event) {
    QWidget* sender = qobject_cast<QWidget*>(obj);
    
    if (event->type() == QEvent::ChildAdded) {
        QChildEvent* ce = static_cast<QChildEvent*>(event);
        if (QWidget* cw = qobject_cast<QWidget*>(ce->child())) {
            installRecursiveFilter(cw);
        }
    }

    if (!sender || (!m_container->isAncestorOf(sender) && sender != m_container)) 
        return QMdiSubWindow::eventFilter(obj, event);

    if (qobject_cast<QPushButton*>(sender)) return false;

    auto mapToSubWindow = [this, sender](const QPoint& localPos) {
        return mapFromGlobal(sender->mapToGlobal(localPos));
    };

    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && !m_shaded && !isMaximized()) {
            QPoint posInSub = mapToSubWindow(mouseEvent->pos());
            m_activeEdges = getResizeEdge(posInSub);
            if (m_activeEdges != None) {
                m_resizing = true;
                m_dragStartPos = mouseEvent->globalPosition().toPoint();
                m_dragStartGeometry = geometry();
                return true; 
            }
        }
    } else if (event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (m_resizing) {
            QPoint delta = mouseEvent->globalPosition().toPoint() - m_dragStartPos;
            QRect newGeom = m_dragStartGeometry;

            if (m_activeEdges & Left) newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
            else if (m_activeEdges & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());

            if (m_activeEdges & Top) newGeom.setTop(m_dragStartGeometry.top() + delta.y());
            else if (m_activeEdges & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());

            int minW = FixedUI::minWindowWidth;
            int minH = FixedUI::minWindowHeight;
            if (newGeom.width() < minW) {
                if (m_activeEdges & Left) newGeom.setLeft(newGeom.right() - minW);
                else newGeom.setRight(newGeom.left() + minW);
            }
            if (newGeom.height() < minH) {
                if (m_activeEdges & Top) newGeom.setTop(newGeom.bottom() - minH);
                else newGeom.setBottom(newGeom.top() + minH);
            }

            setGeometry(newGeom);
            return true;
        } else {
            updateCursor(mapToSubWindow(mouseEvent->pos()));
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (m_resizing) {
            m_resizing = false;
            m_activeEdges = None;
            unsetCursor();
            return true;
        }
    } else if (event->type() == QEvent::HoverMove) {
         QHoverEvent* hoverEvent = static_cast<QHoverEvent*>(event);
         updateCursor(mapToSubWindow(hoverEvent->position().toPoint()));
    } else if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (dragEvent->mimeData()->hasFormat("application/x-tstar-link")) {
            dragEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
        if (dropEvent->mimeData()->hasFormat("application/x-tstar-link")) {
            handleDrop(dropEvent);
            return true;
        }
    } else if (event->type() == QEvent::DragLeave) {
        event->accept();
        return true; 
    }
    
    return QMdiSubWindow::eventFilter(obj, event);
}

void CustomMdiSubWindow::setToolWindow(bool isTool) {
    m_isToolWindow = isTool;
    if (m_nameStrip && m_nameStrip->parentWidget()) {
        m_nameStrip->parentWidget()->setVisible(!isTool);
    }
}

void CustomMdiSubWindow::setActiveState(bool active) {
    if (m_titleBar) m_titleBar->setActive(active);
    if (active) {
        m_container->setStyleSheet("#SubWindowContainer { border: 2px solid #00aaff; background: #202020; padding: 2px; }");
    } else {
        m_container->setStyleSheet("#SubWindowContainer { border: 2px solid #444; background: #202020; padding: 2px; }");
    }
}

void CustomMdiSubWindow::setWidget(QWidget *widget) {
    if (!widget) return;
    m_contentLayout->addWidget(widget, 1);
    widget->setParent(m_contentArea);
    widget->show();
    installRecursiveFilter(widget);
    if (ImageViewer* v = qobject_cast<ImageViewer*>(widget)) {
        adjustToImageSize();
        connect(v, &ImageViewer::unlinked, [this](){
            if (m_linkStrip) m_linkStrip->setLinked(false);
        });

        connect(v, &ImageViewer::modifiedChanged, [this](bool mod){
            QString title = windowTitle();
            if (mod) {
                if (!title.endsWith("*")) setSubWindowTitle(title + "*");
            } else {
                if (title.endsWith("*")) {
                    setSubWindowTitle(title.left(title.length() - 1));
                }
            }
        });
        
        connect(v, &ImageViewer::viewChanged, [this](float scale, float, float){
            if (m_titleBar) {
                int percent = static_cast<int>(scale * 100.0f + 0.5f);
                m_titleBar->setZoom(percent);
            }
        });

        if (m_titleBar) {
             int percent = static_cast<int>(v->getScale() * 100.0f + 0.5f);
             m_titleBar->setZoom(percent);
        }
    }
}

ImageViewer* CustomMdiSubWindow::viewer() const {
    return m_contentArea->findChild<ImageViewer*>();
}

void CustomMdiSubWindow::adjustToImageSize() {
    ImageViewer* v = m_contentArea->findChild<ImageViewer*>();
    if (!v) return;
    const ImageBuffer& buf = v->getBuffer();
    if (!buf.isValid()) return;
    
    int sidebar = FixedUI::sidebarWidth;
    int titleH = FixedUI::titleBarHeight;
    int border = FixedUI::borderWidth;
    
    int w = buf.width();
    int h = buf.height();
    int totalW = w + sidebar + border;
    int totalH = h + titleH + border;
    
    if (parentWidget()) {
        QSize area = parentWidget()->size();
        if (totalW > area.width() * 0.9) totalW = area.width() * 0.9;
        if (totalH > area.height() * 0.9) totalH = area.height() * 0.9;
    }
    resize(totalW, totalH);
}

void CustomMdiSubWindow::requestRename() {
    bool ok;
    QString text = QInputDialog::getText(this, tr("Rename View"),
                                         tr("New Name:"), QLineEdit::Normal,
                                         windowTitle(), &ok);
    if (ok && !text.isEmpty()) {
        setSubWindowTitle(text);
        if (ImageViewer* v = m_contentArea->findChild<ImageViewer*>()) {
             v->getBuffer().setName(text); 
        }
    }
}

void CustomMdiSubWindow::setSubWindowTitle(const QString& title) {
    setWindowTitle(title); 
    if (m_titleBar) m_titleBar->setTitle(title);
    if (m_nameStrip) {
        m_nameStrip->setTitle(title);
        int h = m_nameStrip->preferredHeight();
        if (m_linkStrip) m_linkStrip->setFixedHeight(h);
        if (m_adaptStrip) m_adaptStrip->setFixedHeight(h);
    }
    emit layoutChanged();
}

void CustomMdiSubWindow::resizeEvent(QResizeEvent *event) {
    QMdiSubWindow::resizeEvent(event);
    emit layoutChanged();
}

void CustomMdiSubWindow::moveEvent(QMoveEvent *event) {
    QMdiSubWindow::moveEvent(event);
    emit layoutChanged();
}

void CustomMdiSubWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt") ||
        event->mimeData()->hasFormat("application/x-tstar-duplicate")) {
        event->acceptProposedAction();
    } else {
        QMdiSubWindow::dragEnterEvent(event);
    }
}

void CustomMdiSubWindow::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link") || 
        event->mimeData()->hasFormat("application/x-tstar-adapt") ||
        event->mimeData()->hasFormat("application/x-tstar-duplicate")) {
        event->acceptProposedAction();
    } else {
        QMdiSubWindow::dragMoveEvent(event);
    }
}

void CustomMdiSubWindow::dropEvent(QDropEvent *event) {
    handleDrop(event);
}

void CustomMdiSubWindow::handleDrop(QDropEvent* event) {
    if (event->mimeData()->hasFormat("application/x-tstar-link")) {
        QByteArray data = event->mimeData()->data("application/x-tstar-link");
        bool ok;
        quintptr ptrVal = data.toULongLong(&ok, 16);
        if (ok) {
            CustomMdiSubWindow* sourceWin = reinterpret_cast<CustomMdiSubWindow*>(ptrVal);
            if (sourceWin && sourceWin != this) {
                ImageViewer* sourcePtr = sourceWin->findChild<ImageViewer*>();
                ImageViewer* targetPtr = this->findChild<ImageViewer*>(); 
                
                if (sourcePtr && targetPtr) {
                    struct Member { ImageViewer* viewer; CustomMdiSubWindow* win; };
                    QVector<Member> group;
                    group.append({sourcePtr, sourceWin});
                    group.append({targetPtr, this});

                    if (QMdiArea* area = mdiArea()) {
                        for (QMdiSubWindow* sub : area->subWindowList()) {
                            if (sub == sourceWin || sub == this) continue;
                            CustomMdiSubWindow* csw = qobject_cast<CustomMdiSubWindow*>(sub);
                            if (!csw) continue;
                            ImageViewer* v = csw->viewer();
                            if (v && v->isLinked())
                                group.append({v, csw});
                        }
                    }

                    for (int i = 0; i < group.size(); ++i) {
                        for (int j = i + 1; j < group.size(); ++j) {
                            connect(group[i].viewer, &ImageViewer::viewChanged,
                                    group[j].viewer, &ImageViewer::syncView,
                                    Qt::UniqueConnection);
                            connect(group[j].viewer, &ImageViewer::viewChanged,
                                    group[i].viewer, &ImageViewer::syncView,
                                    Qt::UniqueConnection);
                        }
                    }

                    float srcScale = sourcePtr->getScale();
                    float srcH     = sourcePtr->getHBarLoc();
                    float srcV     = sourcePtr->getVBarLoc();
                    for (int i = 1; i < group.size(); ++i)
                        group[i].viewer->syncView(srcScale, srcH, srcV);

                    for (auto& m : group) {
                        m.viewer->setLinked(true);
                        if (m.win->m_linkStrip) m.win->m_linkStrip->setLinked(true);
                    }

                    QPointer<CustomMdiSubWindow> sourceWinSafe = sourceWin;
                    for (int i = 1; i < group.size(); ++i) {
                        QPointer<ImageViewer>        partnerSafe    = group[i].viewer;
                        QPointer<CustomMdiSubWindow> partnerWinSafe = group[i].win;
                        QPointer<ImageViewer>        srcSafe        = sourcePtr;

                        connect(sourcePtr, &QObject::destroyed,
                                [partnerSafe, partnerWinSafe, srcSafe]() {
                            if (!partnerSafe) return;
                            QMdiArea* area = partnerWinSafe ? partnerWinSafe->mdiArea() : nullptr;
                            bool hasOthers = false;
                            if (area) {
                                for (QMdiSubWindow* sub : area->subWindowList()) {
                                    ImageViewer* v = sub->findChild<ImageViewer*>();
                                    if (v && v != partnerSafe && v != srcSafe && v->isLinked()) {
                                        hasOthers = true; break;
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
                                        hasOthers = true; break;
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
                }
            }
        }
        event->accept();
    } else if (event->mimeData()->hasFormat("application/x-tstar-adapt")) {
        bool ok = false;
        quintptr ptr = event->mimeData()->data("application/x-tstar-adapt").toULongLong(&ok, 16);
        if (ok) {
            CustomMdiSubWindow* source = reinterpret_cast<CustomMdiSubWindow*>(ptr);
            if (source && source != this) {
                 if (source->isMaximized()) showMaximized();
                 else {
                     showNormal();
                     resize(source->size());
                 }
                 event->acceptProposedAction();
                 return;
            }
        }
    } else {
         QMdiSubWindow::dropEvent(event);
    }
}

bool CustomMdiSubWindow::canClose() {
    if (property("bypassCloseChecks").toBool()) {
        return true;
    }

    if (m_isToolWindow) {
        return true;
    }
    
    ImageViewer* v = viewer();
    if (v) {
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
                QMessageBox::warning(this, tr("View in use"), tr("This view is currently in use by the '%1' tool. Please close the tool or select different views before closing this image.").arg(toolName));
                return false;
            }
        }
        
        if (v->isModified()) {
            int ret = QMessageBox::question(this, tr("Unsaved Changes"),
                tr("The image '%1' has unsaved changes. Do you want to close it?").arg(windowTitle()),
                QMessageBox::Yes | QMessageBox::No);
            if (ret == QMessageBox::No) {
                return false;
            }
        }
    }
    return true;
}

void CustomMdiSubWindow::animateClose() {
    if (m_isClosing) return;

    if (!canClose()) return;

    m_isClosing = true;

    if (m_anim) {
        m_anim->stop();
        delete m_anim;
        m_anim = nullptr;
    }

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
    
    connect(m_anim, &QPropertyAnimation::finished, this, [this](){
        QMdiSubWindow::close();
    });
    
    m_anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CustomMdiSubWindow::closeEvent(QCloseEvent* event) {
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

void CustomMdiSubWindow::showEvent(QShowEvent* event) {
    QMdiSubWindow::showEvent(event);
    startFadeIn();
}

void CustomMdiSubWindow::startFadeIn() {
    if (m_anim) {
        m_anim->stop();
        delete m_anim;
    }
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
    
    connect(m_anim, &QPropertyAnimation::finished, this, [this](){
        setGraphicsEffect(nullptr);
        m_effect = nullptr;
    });
    
    m_anim->start();
}

void CustomMdiSubWindow::startFadeOut() {
    animateClose();
}