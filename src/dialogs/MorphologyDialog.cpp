#include "MorphologyDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <opencv2/opencv.hpp>
#include <algorithm>

namespace {
static cv::Mat imageBufferToMatFloat(const ImageBuffer& src) {
    const int w = src.width();
    const int h = src.height();
    const int c = src.channels();
    const float* in = src.data().data();

    if (c == 1) {
        cv::Mat out(h, w, CV_32FC1);
        float* dst = out.ptr<float>(0);
        const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < count; ++i) {
            dst[i] = std::clamp(in[i], 0.0f, 1.0f);
        }
        return out;
    }

    cv::Mat out(h, w, CV_32FC3);
    for (int y = 0; y < h; ++y) {
        cv::Vec3f* row = out.ptr<cv::Vec3f>(y);
        for (int x = 0; x < w; ++x) {
            const int i = (y * w + x) * 3;
            row[x][0] = std::clamp(in[i + 0], 0.0f, 1.0f);
            row[x][1] = std::clamp(in[i + 1], 0.0f, 1.0f);
            row[x][2] = std::clamp(in[i + 2], 0.0f, 1.0f);
        }
    }
    return out;
}

static void matFloatToImageBuffer(const cv::Mat& src, ImageBuffer& dst) {
    const int w = dst.width();
    const int h = dst.height();
    const int c = dst.channels();
    float* out = dst.data().data();

    if (c == 1) {
        for (int y = 0; y < h; ++y) {
            const float* row = src.ptr<float>(y);
            for (int x = 0; x < w; ++x) {
                out[y * w + x] = std::clamp(row[x], 0.0f, 1.0f);
            }
        }
        return;
    }

    for (int y = 0; y < h; ++y) {
        const cv::Vec3f* row = src.ptr<cv::Vec3f>(y);
        for (int x = 0; x < w; ++x) {
            const int i = (y * w + x) * 3;
            out[i + 0] = std::clamp(row[x][0], 0.0f, 1.0f);
            out[i + 1] = std::clamp(row[x][1], 0.0f, 1.0f);
            out[i + 2] = std::clamp(row[x][2], 0.0f, 1.0f);
        }
    }
}

static QString zoomText(float zoom) {
    return QString::number(zoom, 'f', 2) + "x";
}
} // namespace

MorphologyDialog::MorphologyDialog(QWidget* parent)
    : DialogBase(parent, tr("Morphological Operations")) {
    m_mainWindow = getCallbacks();
    setWindowTitle(tr("Morphological Operations"));
    resize(900, 640);

    if (m_mainWindow && m_mainWindow->getCurrentViewer() && m_mainWindow->getCurrentViewer()->getBuffer().isValid()) {
        m_sourceImage = m_mainWindow->getCurrentViewer()->getBuffer();
    }

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(200);
    connect(m_previewTimer, &QTimer::timeout, this, &MorphologyDialog::updatePreview);

    setupUi();
    QTimer::singleShot(250, this, &MorphologyDialog::updatePreview);
}

MorphologyDialog::~MorphologyDialog() {}

bool MorphologyDialog::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_view->viewport() && ev->type() == QEvent::Wheel) {
        QWheelEvent* we = static_cast<QWheelEvent*>(ev);
        if (we->modifiers().testFlag(Qt::ControlModifier)) {
            const float factor = (we->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
            const float newZoom = std::clamp(m_zoom * factor, 0.10f, 10.0f);
            const float actual = newZoom / m_zoom;
            m_zoom = newZoom;
            m_view->scale(actual, actual);
            return true;
        }
    }
    return DialogBase::eventFilter(obj, ev);
}

void MorphologyDialog::setSource(const ImageBuffer& img) {
    m_sourceImage = img;
    m_firstDisplay = true;
    updatePreview();
}

void MorphologyDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QGroupBox* grp = new QGroupBox(tr("Morphological Parameters"));
    QGridLayout* grid = new QGridLayout(grp);

    grid->addWidget(new QLabel(tr("Operation:")), 0, 0);
    m_cbOp = new QComboBox();
    m_cbOp->addItems({tr("Erosion"), tr("Dilation"), tr("Opening"), tr("Closing")});
    connect(m_cbOp, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_previewCheck && m_previewCheck->isChecked()) m_previewTimer->start();
    });
    grid->addWidget(m_cbOp, 0, 1, 1, 2);

    // Helper for slider rows
    auto addSliderRow = [&](int row, const QString& label, QSlider*& slider, QSpinBox*& spin, int min, int max, int def, int step = 1) {
        grid->addWidget(new QLabel(label), row, 0);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setSingleStep(step);
        slider->setValue(def);
        
        spin = new QSpinBox();
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setValue(def);
        spin->setFixedWidth(60);

        grid->addWidget(slider, row, 1);
        grid->addWidget(spin, row, 2);

        connect(slider, &QSlider::valueChanged, this, [spin, this](int v){ 
            spin->blockSignals(true); spin->setValue(v); spin->blockSignals(false); 
            if (m_previewCheck && m_previewCheck->isChecked()) m_previewTimer->start();
        });
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [slider, this](int v){
            slider->blockSignals(true); slider->setValue(v); slider->blockSignals(false); 
            if (m_previewCheck && m_previewCheck->isChecked()) m_previewTimer->start();
        });
    };

    addSliderRow(1, tr("Kernel size:"), m_sliderKernel, m_spinKernel, 1, 31, 3, 2);
    // Enforce odd values for kernel
    connect(m_spinKernel, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int k) {
        if (k % 2 == 0) {
            m_spinKernel->setValue(k + 1);
            m_sliderKernel->setValue(k + 1);
        }
    });

    addSliderRow(2, tr("Iterations:"), m_sliderIter, m_spinIter, 1, 10, 1);

    grid->addWidget(new QLabel(tr("Apply to:")), 3, 0);
    m_applyTargetCombo = new QComboBox();
    m_applyTargetCombo->addItem(tr("Overwrite active view"));
    m_applyTargetCombo->addItem(tr("Create new view"));
    grid->addWidget(m_applyTargetCombo, 3, 1, 1, 2);

    mainLayout->addWidget(grp);

    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setBackgroundBrush(Qt::black);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->viewport()->installEventFilter(this);
    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
    mainLayout->addWidget(m_view, 1);

    QHBoxLayout* zoomRow = new QHBoxLayout();
    zoomRow->addWidget(new QLabel(tr("Zoom: Ctrl+Wheel")));
    QPushButton* zoomOutBtn = new QPushButton(tr("-")); zoomOutBtn->setFixedWidth(28);
    QPushButton* zoomInBtn = new QPushButton(tr("+")); zoomInBtn->setFixedWidth(28);
    QPushButton* fitBtn = new QPushButton(tr("Fit"));
    QLabel* zoomStatus = new QLabel(zoomText(1.0f)); zoomStatus->setMinimumWidth(60);

    connect(zoomOutBtn, &QPushButton::clicked, this, [this, zoomStatus]() { zoomOut(); zoomStatus->setText(zoomText(m_zoom)); });
    connect(zoomInBtn, &QPushButton::clicked, this, [this, zoomStatus]() { zoomIn(); zoomStatus->setText(zoomText(m_zoom)); });
    connect(fitBtn, &QPushButton::clicked, this, [this, zoomStatus]() { zoomFit(); zoomStatus->setText(zoomText(m_zoom)); });

    zoomRow->addWidget(zoomOutBtn);
    zoomRow->addWidget(zoomInBtn);
    zoomRow->addWidget(fitBtn);
    zoomRow->addWidget(zoomStatus);
    zoomRow->addStretch();
    mainLayout->addLayout(zoomRow);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_previewCheck = new QCheckBox(tr("Preview"));
    m_previewCheck->setChecked(true);
    connect(m_previewCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on) updatePreview();
        else if (m_sourceImage.isValid()) {
            QImage qimg = m_sourceImage.getDisplayImage();
            m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
            m_scene->setSceneRect(m_pixmapItem->boundingRect());
        }
    });

    QPushButton* resetBtn = new QPushButton(tr("Reset"));
    QPushButton* closeBtn = new QPushButton(tr("Cancel"));
    QPushButton* applyBtn = new QPushButton(tr("Apply"));

    connect(resetBtn, &QPushButton::clicked, this, &MorphologyDialog::onReset);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &MorphologyDialog::onApply);

    btnLayout->addWidget(m_previewCheck);
    btnLayout->addStretch();
    btnLayout->addWidget(resetBtn);
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);
}

void MorphologyDialog::applyMorphology(const ImageBuffer& src, int opIndex, int kernelSize, int iterations, ImageBuffer& dst) const {
    if (!src.isValid()) { dst = ImageBuffer(); return; }

    cv::Mat mat = imageBufferToMatFloat(src);
    if (kernelSize % 2 == 0) kernelSize += 1;
    cv::Mat element = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernelSize, kernelSize));
    
    cv::Mat out;
    // 0: Erosion, 1: Dilation, 2: Opening, 3: Closing
    if (opIndex == 0) cv::erode(mat, out, element, cv::Point(-1,-1), iterations);
    else if (opIndex == 1) cv::dilate(mat, out, element, cv::Point(-1,-1), iterations);
    else if (opIndex == 2) cv::morphologyEx(mat, out, cv::MORPH_OPEN, element, cv::Point(-1,-1), iterations);
    else if (opIndex == 3) cv::morphologyEx(mat, out, cv::MORPH_CLOSE, element, cv::Point(-1,-1), iterations);
    else out = mat.clone();
    
    dst = src;
    matFloatToImageBuffer(out, dst);
}

void MorphologyDialog::updatePreview() {
    if (!m_sourceImage.isValid()) return;
    if (m_previewCheck && !m_previewCheck->isChecked()) return;

    applyMorphology(m_sourceImage, m_cbOp->currentIndex(), m_spinKernel->value(), m_spinIter->value(), m_previewImage);

    if (m_sourceImage.hasMask()) {
        MaskLayer ml = *m_sourceImage.getMask();
        m_previewImage.setMask(ml);
        m_previewImage.blendResult(m_sourceImage);
    }

    QImage qimg = m_previewImage.getDisplayImage();
    m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    m_scene->setSceneRect(m_pixmapItem->boundingRect());

    if (m_firstDisplay) {
        m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
        m_zoom = std::clamp(static_cast<float>(m_view->transform().m11()), 0.10f, 10.0f);
        m_firstDisplay = false;
    }
}

void MorphologyDialog::onApply() {
    if (!m_mainWindow) return;
    ImageViewer* viewer = m_mainWindow->getCurrentViewer();
    if (!viewer || !viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    const ImageBuffer source = viewer->getBuffer();
    ImageBuffer output;
    applyMorphology(source, m_cbOp->currentIndex(), m_spinKernel->value(), m_spinIter->value(), output);

    if (source.hasMask()) {
        MaskLayer ml = *source.getMask();
        output.setMask(ml);
        output.blendResult(source);
    }

    const bool createNew = (m_applyTargetCombo->currentIndex() == 1);
    if (createNew) {
        m_mainWindow->createResultWindow(output, tr("%1 [Morphology]").arg(viewer->windowTitle()));
    } else {
        viewer->pushUndo();
        viewer->setBuffer(output, viewer->windowTitle(), true);
        viewer->refreshDisplay();
    }

    m_mainWindow->logMessage(tr("Morphological operations applied."), 1, true);
    accept();
}

void MorphologyDialog::onReset() {
    m_cbOp->blockSignals(true); 
    if (m_sliderKernel) m_sliderKernel->blockSignals(true);
    if (m_spinKernel) m_spinKernel->blockSignals(true);
    if (m_sliderIter) m_sliderIter->blockSignals(true);
    if (m_spinIter) m_spinIter->blockSignals(true);

    m_cbOp->setCurrentIndex(0); 
    if (m_spinKernel) m_spinKernel->setValue(3); 
    if (m_sliderKernel) m_sliderKernel->setValue(3);
    if (m_spinIter) m_spinIter->setValue(1);
    if (m_sliderIter) m_sliderIter->setValue(1);
    
    m_applyTargetCombo->setCurrentIndex(0); 
    m_previewCheck->setChecked(true);

    m_cbOp->blockSignals(false); 
    if (m_sliderKernel) m_sliderKernel->blockSignals(false);
    if (m_spinKernel) m_spinKernel->blockSignals(false);
    if (m_sliderIter) m_sliderIter->blockSignals(false);
    if (m_spinIter) m_spinIter->blockSignals(false);

    m_firstDisplay = true;
    updatePreview();
}

void MorphologyDialog::zoomIn() {
    const float newZoom = std::clamp(m_zoom * 1.15f, 0.10f, 10.0f);
    const float actual = newZoom / m_zoom;
    m_zoom = newZoom; m_view->scale(actual, actual);
}

void MorphologyDialog::zoomOut() {
    const float newZoom = std::clamp(m_zoom / 1.15f, 0.10f, 10.0f);
    const float actual = newZoom / m_zoom;
    m_zoom = newZoom; m_view->scale(actual, actual);
}

void MorphologyDialog::zoomFit() {
    if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) return;
    m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    m_zoom = std::clamp(static_cast<float>(m_view->transform().m11()), 0.10f, 10.0f);
}
