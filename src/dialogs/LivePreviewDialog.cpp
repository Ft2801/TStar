#include "LivePreviewDialog.h"
#include "../ImageBuffer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QWheelEvent>
#include <QTimer>
#include <QImage>
#include <algorithm>

LivePreviewDialog::LivePreviewDialog(int width, int height, QWidget* parent)
    : DialogBase(parent, tr("Live Mask Preview"), 0, 0), m_targetWidth(width), m_targetHeight(height) {
    setWindowFlags(windowFlags() | Qt::Tool); // Make it a tool window

    // Calculate scaled size - limit to reasonable subwindow size (max 800x600)
    int maxW = 800;
    int maxH = 600;

    float scaleW = (width > maxW) ? (float)maxW / width : 1.0f;
    float scaleH = (height > maxH) ? (float)maxH / height : 1.0f;
    float scale = std::min(scaleW, scaleH);

    m_targetWidth  = static_cast<int>(width  * scale);
    m_targetHeight = static_cast<int>(height * scale);

    // Graphics scene / view
    m_scene   = new QGraphicsScene(this);
    m_pixItem = m_scene->addPixmap(QPixmap());
    m_pixItem->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);

    m_view = new QGraphicsView(m_scene, this);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform);
    m_view->viewport()->installEventFilter(this);

    // Zoom buttons
    QPushButton* btnZoomIn  = new QPushButton("+",   this);
    QPushButton* btnZoomOut = new QPushButton("–",   this);
    QPushButton* btnFit     = new QPushButton(tr("Fit"), this);
    btnZoomIn ->setFixedWidth(32);
    btnZoomOut->setFixedWidth(32);

    connect(btnZoomIn,  &QPushButton::clicked, this, &LivePreviewDialog::onZoomIn);
    connect(btnZoomOut, &QPushButton::clicked, this, &LivePreviewDialog::onZoomOut);
    connect(btnFit,     &QPushButton::clicked, this, &LivePreviewDialog::onFit);

    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->addWidget(btnZoomIn);
    btnRow->addWidget(btnZoomOut);
    btnRow->addWidget(btnFit);
    btnRow->addStretch();
    btnRow->setContentsMargins(2, 2, 2, 2);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_view);
    layout->addLayout(btnRow);
    layout->setContentsMargins(0, 0, 0, 4);

    resize(m_targetWidth + 20, m_targetHeight + 36);

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

bool LivePreviewDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_view->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent* we = static_cast<QWheelEvent*>(event);
        float factor    = (we->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
        float newZoom   = std::clamp(m_zoom * factor, 0.05f, 16.0f);
        float actual    = newZoom / m_zoom;
        m_zoom          = newZoom;
        m_view->scale(actual, actual);
        return true;
    }
    return DialogBase::eventFilter(obj, event);
}

void LivePreviewDialog::onZoomIn()  { setZoom(m_zoom * 1.25f); }
void LivePreviewDialog::onZoomOut() { setZoom(m_zoom / 1.25f); }

void LivePreviewDialog::setZoom(float z) {
    m_zoom = std::clamp(z, 0.05f, 16.0f);
    m_view->resetTransform();
    m_view->scale(m_zoom, m_zoom);
}

void LivePreviewDialog::onFit() {
    if (m_pixItem->pixmap().isNull()) return;
    m_view->fitInView(m_pixItem, Qt::KeepAspectRatio);
    m_zoom = static_cast<float>(m_view->transform().m11());
}

void LivePreviewDialog::updateMask(const std::vector<float>& maskData, int width, int height, 
                                 ImageBuffer::DisplayMode mode, bool inverted, bool falseColor) {
    if (maskData.empty() || width == 0 || height == 0) {
        // Do not clear — keep last valid state to prevent black flickering.
        return;
    }

    // Use ImageBuffer for advanced rendering
    ImageBuffer buf;
    buf.setData(width, height, 1, maskData);

    // Generate at native resolution
    QImage img = buf.getDisplayImage(mode, true, nullptr, width, height, false, inverted, falseColor);

    m_pixItem->setPixmap(QPixmap::fromImage(img));
    m_scene->setSceneRect(m_pixItem->boundingRect());

    if (m_firstUpdate) {
        // Defer fit-to-view so it runs after the dialog is actually shown and
        // the view has a valid geometry (fitInView is a no-op on hidden widgets).
        QTimer::singleShot(0, this, &LivePreviewDialog::onFit);
        m_firstUpdate = false;
    }
}

