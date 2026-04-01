#include "BlinkComparatorDialog.h"
#include "../MainWindow.h"
#include "../ImageViewer.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../ImageBuffer.h"
#include <QMdiArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QIcon>
#include <QCheckBox>
#include <QTimer>
#include <QCoreApplication>

BlinkCanvas::BlinkCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setStyleSheet("background-color: #1a1a1a;");
}

void BlinkCanvas::setImage(const QImage& img) {
    m_image = img;
    updateBounds();
    update();
}

void BlinkCanvas::zoomIn() {
    m_zoom *= 1.2f;
    updateBounds();
    update();
}

void BlinkCanvas::zoomOut() {
    m_zoom /= 1.2f;
    if (m_zoom < 0.05f) m_zoom = 0.05f;
    updateBounds();
    update();
}

void BlinkCanvas::fitToView() {
    if (m_image.isNull() || width() <= 0 || height() <= 0) return;
    float zw = (float)width() / m_image.width();
    float zh = (float)height() / m_image.height();
    m_zoom = std::min(zw, zh);
    m_panX = 0;
    m_panY = 0;
    updateBounds();
    update();
}

void BlinkCanvas::updateBounds() {
    if (m_image.isNull()) return;
    float scaledW = m_image.width() * m_zoom;
    float scaledH = m_image.height() * m_zoom;
    
    // Limits
    float diffX = (scaledW > width()) ? (scaledW - width()) / 2.0f : 0;
    float diffY = (scaledH > height()) ? (scaledH - height()) / 2.0f : 0;
    
    if (diffX == 0) m_panX = 0;
    else {
        if (m_panX > diffX) m_panX = diffX;
        if (m_panX < -diffX) m_panX = -diffX;
    }
    
    if (diffY == 0) m_panY = 0;
    else {
        if (m_panY > diffY) m_panY = diffY;
        if (m_panY < -diffY) m_panY = -diffY;
    }
}

void BlinkCanvas::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), QColor(26, 26, 26)); // Dark background
    
    if (m_image.isNull()) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, tr("No Image"));
        return;
    }
    
    float sw = m_image.width() * m_zoom;
    float sh = m_image.height() * m_zoom;
    float cx = width() / 2.0f + m_panX;
    float cy = height() / 2.0f + m_panY;
    
    QRectF targetRect(cx - sw / 2.0f, cy - sh / 2.0f, sw, sh);
    
    // Enable smooth transform for zoom out, but fast for 1:1 or zoom in if necessary
    if (m_zoom < 1.0f) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
    }
    p.drawImage(targetRect, m_image);
}

void BlinkCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastMousePos = event->pos();
    }
}

void BlinkCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_panX += delta.x();
        m_panY += delta.y();
        m_lastMousePos = event->pos();
        updateBounds();
        update();
    }
}

void BlinkCanvas::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    m_dragging = false;
}

void BlinkCanvas::wheelEvent(QWheelEvent* event) {
    float factor = (event->angleDelta().y() > 0) ? 1.1f : (1.0f / 1.1f);
    m_zoom *= factor;
    if (m_zoom < 0.05f) m_zoom = 0.05f;
    if (m_zoom > 100.0f) m_zoom = 100.0f;
    updateBounds();
    update();
}

void BlinkCanvas::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);
    updateBounds();
}

// -------------------------------------------------------------
// BlinkComparatorDialog
// -------------------------------------------------------------

BlinkComparatorDialog::BlinkComparatorDialog(MainWindow* mainWindow, QWidget* parent)
    : DialogBase(parent), m_mainWindow(mainWindow) 
{
    setWindowTitle(tr("Blink Comparator"));
    resize(900, 600);
    
    setupUI();
    
    // Timer setup
    m_blinkTimer.setInterval(500); // Default 500ms
    connect(&m_blinkTimer, &QTimer::timeout, this, &BlinkComparatorDialog::onBlinkTimeout);

    updateViewLists();
}

BlinkComparatorDialog::~BlinkComparatorDialog() {
}

void BlinkComparatorDialog::setupUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    // --- Left Panel (Controls) ---
    QWidget* leftPanel = new QWidget(this);
    leftPanel->setFixedWidth(260);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    
    // Views Selection
    leftLayout->addWidget(new QLabel(tr("View 1:")));
    m_view1Combo = new QComboBox(this);
    leftLayout->addWidget(m_view1Combo);
    
    leftLayout->addWidget(new QLabel(tr("View 2:")));
    m_view2Combo = new QComboBox(this);
    leftLayout->addWidget(m_view2Combo);
    
    connect(m_view1Combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BlinkComparatorDialog::onViewsSelectionChanged);
    connect(m_view2Combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BlinkComparatorDialog::onViewsSelectionChanged);

    m_refreshBtn = new QPushButton(tr("Refresh Views"), this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &BlinkComparatorDialog::updateViewLists);
    leftLayout->addWidget(m_refreshBtn);
    
    leftLayout->addSpacing(20);
    
    // Blinking Controls
    leftLayout->addWidget(new QLabel(tr("Blink Rate (ms):")));
    m_rateSpinBox = new QSpinBox(this);
    m_rateSpinBox->setRange(50, 5000);
    m_rateSpinBox->setSingleStep(50);
    m_rateSpinBox->setValue(500);
    connect(m_rateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &BlinkComparatorDialog::onRateChanged);
    leftLayout->addWidget(m_rateSpinBox);
    
    m_playPauseBtn = new QPushButton(tr("Play"), this);
    m_playPauseBtn->setCheckable(true);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &BlinkComparatorDialog::onPlayPauseClicked);
    leftLayout->addWidget(m_playPauseBtn);
    
    m_switchBtn = new QPushButton(tr("Manual blink"), this);
    connect(m_switchBtn, &QPushButton::clicked, this, &BlinkComparatorDialog::onSwitchClicked);
    leftLayout->addWidget(m_switchBtn);
    
    leftLayout->addSpacing(20);
    
    // AutoStretch Checkbox (styled as a tool button to match other tools)
    m_autoStretchBtn = new QToolButton(this);
    m_autoStretchBtn->setText(tr("AutoStretch"));
    m_autoStretchBtn->setCheckable(true);
    connect(m_autoStretchBtn, &QToolButton::toggled, this, &BlinkComparatorDialog::onAutoStretchToggled);
    leftLayout->addWidget(m_autoStretchBtn);
    
    leftLayout->addStretch();
    
    // Zoom Controls
    QHBoxLayout* zoomLayout = new QHBoxLayout();
    m_btnZoomOut = new QToolButton(this); m_btnZoomOut->setText("-");
    m_btnZoomIn = new QToolButton(this); m_btnZoomIn->setText("+");
    m_btnFit = new QToolButton(this); m_btnFit->setText(tr("Fit"));
    
    zoomLayout->addWidget(m_btnZoomOut);
    zoomLayout->addWidget(m_btnFit);
    zoomLayout->addWidget(m_btnZoomIn);
    
    leftLayout->addLayout(zoomLayout);
    
    mainLayout->addWidget(leftPanel);
    
    // --- Right Panel (Filename + Canvas) ---
    QWidget* rightPanel = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    
    m_filenameLabel = new QLabel(tr("No Image"), this);
    m_filenameLabel->setAlignment(Qt::AlignCenter);
    m_filenameLabel->setStyleSheet("font-weight: bold; font-size: 14px; background-color: #222; color: #eee; padding: 5px; border-bottom: 1px solid #444;");
    rightLayout->addWidget(m_filenameLabel);
    
    m_canvas = new BlinkCanvas(this);
    rightLayout->addWidget(m_canvas, 1);
    
    mainLayout->addWidget(rightPanel, 1);
    
    connect(m_btnZoomIn, &QToolButton::clicked, m_canvas, &BlinkCanvas::zoomIn);
    connect(m_btnZoomOut, &QToolButton::clicked, m_canvas, &BlinkCanvas::zoomOut);
    connect(m_btnFit, &QToolButton::clicked, m_canvas, &BlinkCanvas::fitToView);
}

void BlinkComparatorDialog::updateViewLists() {
    m_view1Combo->blockSignals(true);
    m_view2Combo->blockSignals(true);
    
    QString current1 = m_view1Combo->currentText();
    QString current2 = m_view2Combo->currentText();
    
    m_view1Combo->clear();
    m_view2Combo->clear();
    
    // Find QMdiArea
    QMdiArea* mdi = m_mainWindow->findChild<QMdiArea*>();
    if (mdi) {
        for (auto* win : mdi->subWindowList()) {
            CustomMdiSubWindow* csw = qobject_cast<CustomMdiSubWindow*>(win);
            if (csw && !csw->isToolWindow() && csw->viewer() && csw->viewer()->getBuffer().isValid()) {
                QString title = csw->windowTitle();
                if (title.endsWith("*")) title.chop(1); // Remove modified star
                m_view1Combo->addItem(title, QVariant::fromValue((void*)csw->viewer()));
                m_view2Combo->addItem(title, QVariant::fromValue((void*)csw->viewer()));
            }
        }
    }
    
    // Restore selection if possible
    int idx1 = m_view1Combo->findText(current1);
    int idx2 = m_view2Combo->findText(current2);
    if (idx1 >= 0) m_view1Combo->setCurrentIndex(idx1);
    if (idx2 >= 0) m_view2Combo->setCurrentIndex(idx2);
    
    m_view1Combo->blockSignals(false);
    m_view2Combo->blockSignals(false);
    
    onViewsSelectionChanged();
}

void BlinkComparatorDialog::showEvent(QShowEvent* event) {
    DialogBase::showEvent(event);
    updateViewLists();
    
    // If not playing, force display of View 1 initially
    if (!m_isPlaying) {
        m_showingView1 = true;
        refreshCurrentImage();
    }

    if (m_needsInitialFit) {
        m_canvas->fitToView();
        if (!m_img1.isNull() || !m_img2.isNull()) {
            m_needsInitialFit = false;
        }
    }
}

void BlinkComparatorDialog::closeEvent(QCloseEvent* event) {
    m_blinkTimer.stop();
    m_isPlaying = false;
    DialogBase::closeEvent(event);
}

void BlinkComparatorDialog::onPlayPauseClicked() {
    m_isPlaying = m_playPauseBtn->isChecked();
    if (m_isPlaying) {
        m_playPauseBtn->setText(tr("Pause"));
        m_blinkTimer.start(m_rateSpinBox->value());
    } else {
        m_playPauseBtn->setText(tr("Play"));
        m_blinkTimer.stop();
        m_showingView1 = true;
        refreshCurrentImage();
    }
}

void BlinkComparatorDialog::onBlinkTimeout() {
    m_showingView1 = !m_showingView1;
    refreshCurrentImage();
}

void BlinkComparatorDialog::onRateChanged(int rateMs) {
    if (m_isPlaying) {
        m_blinkTimer.setInterval(rateMs);
    }
}

void BlinkComparatorDialog::onSwitchClicked() {
    if (m_isPlaying) {
        m_playPauseBtn->setChecked(false);
        onPlayPauseClicked();
    }
    m_showingView1 = !m_showingView1;
    refreshCurrentImage();
}

void BlinkComparatorDialog::onAutoStretchToggled(bool checked) {
    m_useAutoStretch = checked;
    // Invalidate caches to force re-render
    m_lastView1 = nullptr;
    m_lastView2 = nullptr;
    onViewsSelectionChanged();
}

QImage BlinkComparatorDialog::renderViewImage(ImageViewer* viewer) {
    if (!viewer) return QImage();
    const ImageBuffer& buf = viewer->getBuffer();
    if (!buf.isValid()) return QImage();

    ImageBuffer::DisplayMode mode = m_useAutoStretch ? ImageBuffer::Display_AutoStretch : ImageBuffer::Display_Linear;
    // Assume typical settings: linked, targetMedian 0.25f, no invert, no false color
    return buf.getDisplayImage(mode, true, nullptr, 0, 0, false, false, false, 0.25f, ImageBuffer::ChannelRGB);
}

void BlinkComparatorDialog::onViewsSelectionChanged() {
    ImageViewer* v1 = nullptr;
    if (m_view1Combo->currentIndex() >= 0) {
        v1 = static_cast<ImageViewer*>(m_view1Combo->currentData().value<void*>());
    }
    ImageViewer* v2 = nullptr;
    if (m_view2Combo->currentIndex() >= 0) {
        v2 = static_cast<ImageViewer*>(m_view2Combo->currentData().value<void*>());
    }

    // Refresh m_img1
    if (v1 != m_lastView1) {
        m_img1 = renderViewImage(v1);
        m_lastView1 = v1;
    }
    // Refresh m_img2
    if (v2 != m_lastView2) {
        m_img2 = renderViewImage(v2);
        m_lastView2 = v2;
    }

    refreshCurrentImage();
}

void BlinkComparatorDialog::refreshCurrentImage() {
    if (m_showingView1) {
        m_canvas->setImage(m_img1);
        m_filenameLabel->setText(m_view1Combo->currentText());
    } else {
        m_canvas->setImage(m_img2);
        m_filenameLabel->setText(m_view2Combo->currentText());
    }
}
