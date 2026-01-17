#include "CustomMdiSubWindow.h"
#include "ImageViewer.h"
#include "Icons.h"
#include "core/DpiHelper.h"
#include <QStyle>
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


// Helper to create QIcon from SVG string (DPI-aware)
static QIcon iconFromSvg(const QString& svg, QWidget* widget = nullptr) {
    QByteArray ba = svg.toUtf8();
    QSvgRenderer renderer(ba);
    int size = DpiHelper::iconPixmapSize(widget);
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    renderer.render(&painter);
    return QIcon(pix);
}

// --- NameStrip ---
NameStrip::NameStrip(QWidget *parent) : QWidget(parent) {
    setFixedWidth(DpiHelper::sidebarWidth(this));
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
        
        int dragSize = DpiHelper::dragPixmapSize(this);
        QPixmap pix(dragSize, dragSize);
        pix.fill(Qt::cyan); // Cyan for duplicate
        drag->setPixmap(pix);
        drag->exec(Qt::CopyAction);
    }
}

// --- LinkStrip ---
LinkStrip::LinkStrip(QWidget *parent) : QWidget(parent) {
    setFixedWidth(DpiHelper::sidebarWidth(this));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding); // Takes remaining space
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true); // Can drop on link strip too
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
        if (m_linked) {
            emit linkToggled(); // Request unlink
        } else {
            m_dragStartPos = event->pos();
        }
    }
}

void LinkStrip::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if (m_linked) return; // No dragging if already linked (click to unlink)
    
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) return;
    
    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData();
    // Link mode always true for LinkStrip
    
    QWidget* widget = this;
    while(widget && !widget->inherits("CustomMdiSubWindow")) {
        widget = widget->parentWidget();
    }
    CustomMdiSubWindow* sourceWin = qobject_cast<CustomMdiSubWindow*>(widget);
    if (sourceWin) {
        QByteArray ptrData = QByteArray::number((quintptr)sourceWin, 16);
        mimeData->setData("application/x-tstar-link", ptrData);
        drag->setMimeData(mimeData);
        int dragSize = DpiHelper::dragPixmapSize(this);
        QPixmap pix(dragSize, dragSize);
        pix.fill(Qt::green);
        drag->setPixmap(pix);
        drag->exec(Qt::LinkAction);
    }
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
    setFixedWidth(DpiHelper::sidebarWidth(this));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
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
        
        int dragSize = DpiHelper::dragPixmapSize(this);
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
    setFixedHeight(DpiHelper::titleBarHeight(this));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_active = false;
    
    QHBoxLayout* layout = new QHBoxLayout(this);
    int margin = DpiHelper::scale(5, this);
    layout->setContentsMargins(margin, 0, margin, DpiHelper::scale(4, this));
    layout->setSpacing(DpiHelper::scale(4, this));
    
    m_titleLabel = new QLabel("", this);
    m_titleLabel->setStyleSheet("color: #ddd; font-weight: bold; font-family: 'Segoe UI'; font-size: 11px; background: transparent;");
    
    // Zoom Label (Left of title)
    m_zoomLabel = new QLabel("", this);
    m_zoomLabel->setStyleSheet("QLabel { color: #aaa; font-family: 'Segoe UI'; font-size: 10px; background-color: transparent; margin-right: 5px; border: none; }");
    m_zoomLabel->setVisible(false); // Hide initially
    
    int btnSize = DpiHelper::buttonSize(this);
    int icnSize = DpiHelper::iconSize(this);
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

    // Balance layout for centering
    int buttonsWidth = 3 * btnSize + 2 * DpiHelper::scale(4, this); 
    
    m_leftSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_rightSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);

    // Add fixed spacing to match buttons width on the right
    layout->addSpacing(buttonsWidth); 
    layout->addSpacerItem(m_leftSpacer);
    layout->addWidget(m_zoomLabel); 
    layout->addWidget(m_titleLabel);
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
    m_titleLabel->setText(title);
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
                // Content area shown below/resized by setGeometry
            }

            showNormal();
            m_isMaximized = false;
            m_titleBar->setMaximized(false);
            
            // Restore valid normal geometry if available
            if (!m_validNormalGeometry.isNull()) {
                qDebug() << "  -> Restoring valid normal geometry:" << m_validNormalGeometry;
                setGeometry(m_validNormalGeometry);
            } else {
                qDebug() << "  -> WARNING: No valid normal geometry, using fallback";
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
                qDebug() << "  -> Saved Normal Geometry:" << m_validNormalGeometry;
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
                
                 // Robust Unshade-to-Maximize:
                 // DO NOT call showNormal() here as it might reset geometry to Shaded size
                 
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
                         m_contentArea->resize(width(), height() - m_titleBar->height()); // Explicit resize help
                     }
                     
                     if (ImageViewer* v = viewer()) {
                         v->fitToWindow();
                         // Double tap fit to window to handle layout latency
                         QTimer::singleShot(10, v, &ImageViewer::fitToWindow);
                     }
                 });
                 return; // Async completion
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
    leftStrip->setFixedWidth(DpiHelper::sidebarWidth(this));
    QVBoxLayout* leftLayout = new QVBoxLayout(leftStrip);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    
    m_nameStrip = new NameStrip(leftStrip);
    connect(m_nameStrip, &NameStrip::renameRequested, this, &CustomMdiSubWindow::requestRename);
    
    m_linkStrip = new LinkStrip(leftStrip);
    connect(m_linkStrip, &LinkStrip::linkToggled, [this](){
        // Unlink implementation - must disconnect signal/slots and update both viewers
        ImageViewer* v = m_contentArea->findChild<ImageViewer*>();
        if (v && v->isLinked()) {
            // Find all other linked viewers in the MDI area and disconnect them
            if (QMdiArea* area = mdiArea()) {
                for (QMdiSubWindow* sub : area->subWindowList()) {
                    if (sub == this) continue;
                    
                    CustomMdiSubWindow* otherWin = qobject_cast<CustomMdiSubWindow*>(sub);
                    if (!otherWin) continue;
                    
                    ImageViewer* otherViewer = otherWin->findChild<ImageViewer*>();
                    if (otherViewer && otherViewer->isLinked()) {
                        // Disconnect bidirectional view synchronization
                        disconnect(v, &ImageViewer::viewChanged, otherViewer, &ImageViewer::syncView);
                        disconnect(otherViewer, &ImageViewer::viewChanged, v, &ImageViewer::syncView);
                        
                        // Check if this viewer has any other links remaining
                        bool otherHasMoreLinks = false;
                        for (QMdiSubWindow* checkSub : area->subWindowList()) {
                            if (checkSub == sub || checkSub == this) continue;
                            ImageViewer* checkViewer = checkSub->findChild<ImageViewer*>();
                            if (checkViewer && checkViewer->isLinked()) {
                                // Check if otherViewer is still connected to checkViewer
                                // by testing if they share viewChanged connections
                                otherHasMoreLinks = true;
                                break;
                            }
                        }
                        
                        if (!otherHasMoreLinks) {
                            otherViewer->setLinked(false);
                            if (otherWin->m_linkStrip) {
                                otherWin->m_linkStrip->setLinked(false);
                            }
                        }
                    }
                }
            }
            
            v->setLinked(false);
        }
        m_linkStrip->setLinked(false);
    });

    m_adaptStrip = new AdaptStrip(leftStrip);

    leftLayout->addWidget(m_nameStrip);
    leftLayout->addWidget(m_linkStrip);
    leftLayout->addWidget(m_adaptStrip);
    
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
    
    // Special handling for QAbstractScrollArea (like QGraphicsView)
    // The viewport is a separate widget that needs the filter too
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

        // Enforce minimum size (DPI-aware)
        int minW = DpiHelper::minWindowWidth(this);
        int minH = DpiHelper::minWindowHeight(this);
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
    int margin = DpiHelper::resizeMargin(this);
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
}

void CustomMdiSubWindow::showNormal() {
    m_isMaximized = false;
    m_titleBar->setMaximized(false);
    QMdiSubWindow::showNormal();
}

void CustomMdiSubWindow::toggleShade() {
    // Guard against rapid successive calls (e.g., from double-click triggering twice)
    static QElapsedTimer lastCallTimer;
    if (lastCallTimer.isValid() && lastCallTimer.elapsed() < 200) {
        qDebug() << "toggleShade: BLOCKED (too rapid, elapsed=" << lastCallTimer.elapsed() << "ms)";
        return;
    }
    lastCallTimer.start();
    
    m_shaded = !m_shaded;
    m_titleBar->setShaded(m_shaded);
    
    qDebug() << "toggleShade: m_shaded now =" << m_shaded << ", m_isMaximized =" << m_isMaximized << ", m_wasMaximized =" << m_wasMaximized;
    
    QPoint center = geometry().center();
    
    if (m_shaded) {
        // Save valid normal geometry if we are shading from a normal state
        if (!m_isMaximized) {
            m_validNormalGeometry = geometry();
        }

        m_wasMaximized = m_isMaximized; // Use our manual flag, not isMaximized()
        m_originalHeight = height();
        m_originalWidth = width(); 
        
        qDebug() << "  SHADING: saving m_wasMaximized =" << m_wasMaximized << ", originalSize =" << m_originalWidth << "x" << m_originalHeight;
        
        m_contentArea->hide();
        int newH = m_titleBar->height() + DpiHelper::borderWidth(this);
        
        // Shrink width to title + buttons for ALL windows (DPI-aware)
        QFontMetrics fm(m_titleBar->font());
        int textW = fm.horizontalAdvance(windowTitle());
        int btnSize = DpiHelper::buttonSize(this);
        int buttonsW = 3 * btnSize + DpiHelper::scale(20, this); 
        int minW = DpiHelper::minShadedWidth(this);
        int totalW = std::max(minW, textW + buttonsW + DpiHelper::scale(90, this)); 
        
        // Center Collapse Implementation (Horizontal Center, Vertical Top)
        int newX = center.x() - totalW / 2;
        int newY = geometry().top(); // Preserve Top
        
        setGeometry(newX, newY, totalW, newH);
        
    } else {
        // Restore
        setMinimumWidth(0);
        setMaximumWidth(16777215);
        setMaximumHeight(16777215); 
        setMinimumHeight(0);
        
        m_titleBar->show();
        m_contentArea->show();
        
        qDebug() << "  UNSHADING: m_wasMaximized =" << m_wasMaximized;
        bool restoreMax = m_wasMaximized;
        
        // Reset internal state to Normal first to avoid stuck flags
        if (isMaximized()) QMdiSubWindow::showNormal();

        if (restoreMax) {
            qDebug() << "  -> Restoring geometry (FULL) before deferred showMaximized()";
            
            // Robustness: Use MDI viewport size to ensure we expand fully, 
            // ignoring potentially corrupted m_originalWidth/Height
            if (QMdiArea* area = mdiArea()) {
                if (QWidget* vp = area->viewport()) {
                    setGeometry(vp->rect());
                } else {
                    setGeometry(area->rect());
                }
            } else {
                 // Fallback
                 setGeometry(0, 0, 1920, 1080); 
            }
            
            qDebug() << "  -> Deferring showMaximized()";
            QTimer::singleShot(50, this, [this](){
                qDebug() << "  -> Calling showMaximized() (deferred)";
                showMaximized();
                if (m_contentArea) {
                    m_contentArea->show(); // Ensure visible
                    m_contentArea->resize(width(), height() - m_titleBar->height());
                }
                
                // Double check size
                if (width() < 500) {
                    qDebug() << "  -> WARNING: Window still small after max (" << width() << "x" << height() << "), forcing resize";
                    if (QMdiArea* area = mdiArea()) setGeometry(area->viewport()->rect());
                }
                
                if (ImageViewer* v = viewer()) {
                    v->fitToWindow();
                    QTimer::singleShot(10, v, &ImageViewer::fitToWindow);
                }
            });
        } else {
            // Restore (Horizontal Center, Vertical Top)
            int newX = center.x() - m_originalWidth / 2;
            int newY = geometry().top(); // Preserve Top
            
            qDebug() << "  -> Restoring to" << m_originalWidth << "x" << m_originalHeight;
            setGeometry(newX, newY, m_originalWidth, m_originalHeight);
        }
    }
}

bool CustomMdiSubWindow::event(QEvent *event) {
    return QMdiSubWindow::event(event);
}

bool CustomMdiSubWindow::eventFilter(QObject *obj, QEvent *event) {
    QWidget* sender = qobject_cast<QWidget*>(obj);
    
    // Handle new children being added dynamically
    if (event->type() == QEvent::ChildAdded) {
        QChildEvent* ce = static_cast<QChildEvent*>(event);
        if (QWidget* cw = qobject_cast<QWidget*>(ce->child())) {
            installRecursiveFilter(cw);
        }
    }

    if (!sender || (!m_container->isAncestorOf(sender) && sender != m_container)) 
        return QMdiSubWindow::eventFilter(obj, event);

    // Avoid resizing when interacting with standard buttons
    if (qobject_cast<QPushButton*>(sender)) return false;

    // Use global->local mapping for accurate coordinate translation
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

            int minW = DpiHelper::minWindowWidth(this);
            int minH = DpiHelper::minWindowHeight(this);
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
        // Suppress "drag leave received before drag enter" debug warning from QGraphicsView
        // caused by MDI window stealing drag events or hierarchy issues.
        event->accept();
        return true; 
    }
    
    return QMdiSubWindow::eventFilter(obj, event);
}

void CustomMdiSubWindow::setToolWindow(bool isTool) {
    m_isToolWindow = isTool;
    // Hide leftStrip if tool
    if (m_nameStrip && m_nameStrip->parentWidget()) {
        m_nameStrip->parentWidget()->setVisible(!isTool);
    }
    
    // Adjust title bar spacers based on window type
    if (m_titleBar) {
        // Center titles for consistency as requested
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
        // Sync LinkStrip when viewer unlinks
        connect(v, &ImageViewer::unlinked, [this](){
            if (m_linkStrip) m_linkStrip->setLinked(false);
        });

        // Sync Modified Asterisk in Title
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
        
        // Sync Zoom Label
        connect(v, &ImageViewer::viewChanged, [this](float scale, float, float){
            if (m_titleBar) {
                int percent = static_cast<int>(scale * 100.0f + 0.5f);
                m_titleBar->setZoom(percent);
            }
        });

        // Initial update
        if (m_titleBar) {
             int percent = static_cast<int>(v->getScale() * 100.0f + 0.5f);
             m_titleBar->setZoom(percent);
        }
        
        // Also sync if viewer becomes linked...
    }
}

ImageViewer* CustomMdiSubWindow::viewer() const {
    return m_contentArea->findChild<ImageViewer*>();
}

void CustomMdiSubWindow::adjustToImageSize() {
    ImageViewer* v = m_contentArea->findChild<ImageViewer*>();
    if (!v) return;
    QImage img = v->getBuffer().getDisplayImage(ImageBuffer::Display_Linear);
    if (img.isNull()) return;
    
    int sidebar = DpiHelper::sidebarWidth(this);
    int titleH = DpiHelper::titleBarHeight(this);
    int border = DpiHelper::borderWidth(this);
    
    int w = img.width();
    int h = img.height();
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
            // Also rename the buffer?
             v->getBuffer().setName(text); 
        }
    }
}

void CustomMdiSubWindow::setSubWindowTitle(const QString& title) {
    setWindowTitle(title); // Call base QWidget method
    if (m_titleBar) m_titleBar->setTitle(title);
    if (m_nameStrip) {
        m_nameStrip->setTitle(title);
        int h = m_nameStrip->preferredHeight();
        if (m_linkStrip) m_linkStrip->setFixedHeight(h);
        if (m_adaptStrip) m_adaptStrip->setFixedHeight(h);
    }
}

void CustomMdiSubWindow::resizeEvent(QResizeEvent *event) {
    // If shaded, we don't update m_originalWidth/Height
    QMdiSubWindow::resizeEvent(event);
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
                    // Connect bidirectional view synchronization
                    // These will auto-disconnect when either receiver is destroyed
                    connect(sourcePtr, &ImageViewer::viewChanged, targetPtr, &ImageViewer::syncView, Qt::UniqueConnection);
                    connect(targetPtr, &ImageViewer::viewChanged, sourcePtr, &ImageViewer::syncView, Qt::UniqueConnection);
                    
                    targetPtr->syncView(sourcePtr->getScale(), sourcePtr->getHBarLoc(), sourcePtr->getVBarLoc());
                    sourcePtr->setLinked(true);
                    targetPtr->setLinked(true);
                    
                    // Update LinkStrip Status
                    if (sourceWin->m_linkStrip) sourceWin->m_linkStrip->setLinked(true);
                    if (m_linkStrip) m_linkStrip->setLinked(true);
                    
                    // When one viewer is destroyed, update the other's linked state
                    // Only set linked=false if there are no other linked viewers remaining
                    QPointer<ImageViewer> targetSafe = targetPtr;
                    QPointer<CustomMdiSubWindow> targetWin = this;
                    QPointer<ImageViewer> sourceSafe = sourcePtr;
                    
                    connect(sourcePtr, &QObject::destroyed, [targetSafe, targetWin, sourceSafe](){
                        if (targetSafe) {
                            // Check if target still has other linked viewers
                            bool hasOtherLinks = false;
                            if (targetWin) {
                                QMdiArea* mdiArea = targetWin->mdiArea();
                                if (mdiArea) {
                                    for (QMdiSubWindow* sub : mdiArea->subWindowList()) {
                                        ImageViewer* otherViewer = sub->findChild<ImageViewer*>();
                                        if (otherViewer && otherViewer != targetSafe && otherViewer != sourceSafe && otherViewer->isLinked()) {
                                            hasOtherLinks = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            
                            if (!hasOtherLinks) {
                                targetSafe->setLinked(false);
                                if (targetWin && targetWin->m_linkStrip) {
                                    targetWin->m_linkStrip->setLinked(false);
                                }
                            }
                        }
                    });
                    
                    QPointer<CustomMdiSubWindow> sourceWinSafe = sourceWin;
                    connect(targetPtr, &QObject::destroyed, [sourceSafe, sourceWinSafe, targetSafe](){
                        if (sourceSafe) {
                            // Check if source still has other linked viewers
                            bool hasOtherLinks = false;
                            if (sourceWinSafe) {
                                QMdiArea* mdiArea = sourceWinSafe->mdiArea();
                                if (mdiArea) {
                                    for (QMdiSubWindow* sub : mdiArea->subWindowList()) {
                                        ImageViewer* otherViewer = sub->findChild<ImageViewer*>();
                                        if (otherViewer && otherViewer != sourceSafe && otherViewer != targetSafe && otherViewer->isLinked()) {
                                            hasOtherLinks = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            
                            if (!hasOtherLinks) {
                                sourceSafe->setLinked(false);
                                if (sourceWinSafe && sourceWinSafe->m_linkStrip) {
                                    sourceWinSafe->m_linkStrip->setLinked(false);
                                }
                            }
                        }
                    });
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
                 // Adapt this window's size to source window's size
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
    // Skip unsaved changes check for tool windows - their internal viewers are just for preview
    if (m_isToolWindow) {
        return true;
    }
    
    // Check for unsaved changes if this window contains an ImageViewer
    ImageViewer* v = viewer();
    if (v && v->isModified()) {
        int ret = QMessageBox::question(this, tr("Unsaved Changes"),
            tr("The image '%1' has unsaved changes. Do you want to close it?").arg(windowTitle()),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::No) {
            return false;
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

    // Ensure opacity effect exists
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
    // Ensure effect exists
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
    
    // Remove effect after animation to avoid GPU overhead
    connect(m_anim, &QPropertyAnimation::finished, this, [this](){
        setGraphicsEffect(nullptr);
        m_effect = nullptr;
    });
    
    m_anim->start();
}

void CustomMdiSubWindow::startFadeOut() {
    animateClose();
}
