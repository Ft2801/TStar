#include "StarHaloRemovalDialog.h"

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
#include <QSlider>
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

static cv::Mat applyLutTo01Float(const cv::Mat& src01, const cv::Mat& lut) {
    cv::Mat srcU8;
    src01.convertTo(srcU8, CV_8U, 255.0);
    cv::Mat mappedU8;
    cv::LUT(srcU8, lut, mappedU8);
    cv::Mat out;
    mappedU8.convertTo(out, CV_32F, 1.0 / 255.0);
    return out;
}

static QString zoomText(float zoom) {
    return QString::number(zoom, 'f', 2) + "x";
}
} // namespace

StarHaloRemovalDialog::StarHaloRemovalDialog(QWidget* parent)
    : DialogBase(parent, tr("Star Halo Removal")) {
    m_mainWindow = getCallbacks();
    setWindowTitle(tr("Star Halo Removal"));
    resize(900, 640);

    if (m_mainWindow && m_mainWindow->getCurrentViewer() && m_mainWindow->getCurrentViewer()->getBuffer().isValid()) {
        m_sourceImage = m_mainWindow->getCurrentViewer()->getBuffer();
    }

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(180);
    connect(m_previewTimer, &QTimer::timeout, this, &StarHaloRemovalDialog::updatePreview);

    setupUi();
    QTimer::singleShot(250, this, &StarHaloRemovalDialog::updatePreview);
}

StarHaloRemovalDialog::~StarHaloRemovalDialog() {}

bool StarHaloRemovalDialog::eventFilter(QObject* obj, QEvent* ev) {
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

void StarHaloRemovalDialog::setSource(const ImageBuffer& img) {
    m_sourceImage = img;
    m_firstDisplay = true;
    updatePreview();
}

void StarHaloRemovalDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QGroupBox* grp = new QGroupBox(tr("Halo removal parameters"));
    QGridLayout* grid = new QGridLayout(grp);

    grid->addWidget(new QLabel(tr("Reduction:")), 0, 0);
    m_reductionSlider = new QSlider(Qt::Horizontal);
    m_reductionSlider->setRange(0, 3);
    m_reductionSlider->setValue(0);
    m_reductionLabel = new QLabel();
    m_reductionLabel->setMinimumWidth(90);
    updateReductionLabel(0);
    connect(m_reductionSlider, &QSlider::valueChanged, this, [this](int level) {
        updateReductionLabel(level);
        if (m_previewCheck && m_previewCheck->isChecked()) {
            m_previewTimer->start();
        }
    });
    grid->addWidget(m_reductionSlider, 0, 1);
    grid->addWidget(m_reductionLabel, 0, 2);

    m_linearCheck = new QCheckBox(tr("Linear data"));
    m_linearCheck->setChecked(false);
    connect(m_linearCheck, &QCheckBox::toggled, this, [this](bool) {
        if (m_previewCheck && m_previewCheck->isChecked()) {
            m_previewTimer->start();
        }
    });
    grid->addWidget(m_linearCheck, 1, 1, 1, 2);

    grid->addWidget(new QLabel(tr("Apply to:")), 2, 0);
    m_applyTargetCombo = new QComboBox();
    m_applyTargetCombo->addItem(tr("Overwrite active view"));
    m_applyTargetCombo->addItem(tr("Create new view"));
    grid->addWidget(m_applyTargetCombo, 2, 1, 1, 2);

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
    QPushButton* zoomOutBtn = new QPushButton(tr("-"));
    zoomOutBtn->setFixedWidth(28);
    QPushButton* zoomInBtn = new QPushButton(tr("+"));
    zoomInBtn->setFixedWidth(28);
    QPushButton* fitBtn = new QPushButton(tr("Fit"));
    QLabel* zoomStatus = new QLabel(zoomText(1.0f));
    zoomStatus->setMinimumWidth(60);

    connect(zoomOutBtn, &QPushButton::clicked, this, [this, zoomStatus]() {
        zoomOut();
        zoomStatus->setText(zoomText(m_zoom));
    });
    connect(zoomInBtn, &QPushButton::clicked, this, [this, zoomStatus]() {
        zoomIn();
        zoomStatus->setText(zoomText(m_zoom));
    });
    connect(fitBtn, &QPushButton::clicked, this, [this, zoomStatus]() {
        zoomFit();
        zoomStatus->setText(zoomText(m_zoom));
    });

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
        if (on) {
            updatePreview();
        } else if (m_sourceImage.isValid()) {
            QImage qimg = m_sourceImage.getDisplayImage();
            m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
            m_scene->setSceneRect(m_pixmapItem->boundingRect());
        }
    });

    QPushButton* resetBtn = new QPushButton(tr("Reset"));
    QPushButton* closeBtn = new QPushButton(tr("Cancel"));
    QPushButton* applyBtn = new QPushButton(tr("Apply"));

    connect(resetBtn, &QPushButton::clicked, this, &StarHaloRemovalDialog::onReset);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &StarHaloRemovalDialog::onApply);

    btnLayout->addWidget(m_previewCheck);
    btnLayout->addStretch();
    btnLayout->addWidget(resetBtn);
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);
}

void StarHaloRemovalDialog::updateReductionLabel(int level) {
    static const char* kLevels[] = {"Extra Low", "Low", "Medium", "High"};
    const int idx = std::clamp(level, 0, 3);
    m_reductionLabel->setText(tr(kLevels[idx]));
}

void StarHaloRemovalDialog::applyHaloRemoval(const ImageBuffer& src, int reductionLevel, bool isLinear, ImageBuffer& dst) const {
    if (!src.isValid()) {
        dst = ImageBuffer();
        return;
    }

    const int clampedLevel = std::clamp(reductionLevel, 0, 3);
    cv::Mat work = imageBufferToMatFloat(src);

    if (isLinear) {
        cv::Mat tmp;
        cv::pow(work, 1.0 / 5.0, tmp);
        work = tmp;
    }

    cv::Mat light;
    if (work.channels() == 1) {
        light = work / 255.0;
    } else {
        cv::cvtColor(work, light, cv::COLOR_RGB2GRAY);
        light /= 255.0;
    }

    cv::Mat blurred;
    cv::GaussianBlur(light, blurred, cv::Size(0, 0), 2.0);

    cv::Mat unsharp;
    cv::addWeighted(light, 1.66, blurred, -0.66, 0.0, unsharp);

    cv::Mat inv = 1.0 - unsharp;
    cv::Mat dup;
    cv::GaussianBlur(unsharp, dup, cv::Size(0, 0), 2.0);

    const float scale = static_cast<float>(clampedLevel) * 0.33f;
    cv::Mat enhancedMask = inv - dup * scale;

    cv::Mat mask;
    if (work.channels() == 3) {
        std::vector<cv::Mat> channels(3, enhancedMask);
        cv::merge(channels, mask);
    } else {
        mask = enhancedMask;
    }

    cv::Mat masked;
    cv::multiply(work, mask, masked);

    static const float kGammas[] = {1.2f, 1.5f, 1.8f, 2.2f};
    const float gamma = kGammas[clampedLevel];
    cv::Mat x(1, 256, CV_32F);
    for (int i = 0; i < 256; ++i) {
        x.at<float>(0, i) = static_cast<float>(i) / 255.0f;
    }
    cv::Mat y;
    cv::pow(x, gamma, y);
    cv::Mat lut;
    y.convertTo(lut, CV_8U, 255.0);

    cv::Mat out = applyLutTo01Float(masked, lut);

    dst = src;
    matFloatToImageBuffer(out, dst);
}

void StarHaloRemovalDialog::updatePreview() {
    if (!m_sourceImage.isValid()) return;
    if (m_previewCheck && !m_previewCheck->isChecked()) return;

    applyHaloRemoval(m_sourceImage, m_reductionSlider->value(), m_linearCheck->isChecked(), m_previewImage);

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

void StarHaloRemovalDialog::onApply() {
    if (!m_mainWindow) return;
    ImageViewer* viewer = m_mainWindow->getCurrentViewer();
    if (!viewer || !viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    const ImageBuffer source = viewer->getBuffer();
    ImageBuffer output;
    applyHaloRemoval(source, m_reductionSlider->value(), m_linearCheck->isChecked(), output);

    if (source.hasMask()) {
        MaskLayer ml = *source.getMask();
        output.setMask(ml);
        output.blendResult(source);
    }

    const bool createNew = (m_applyTargetCombo->currentIndex() == 1);
    if (createNew) {
        m_mainWindow->createResultWindow(output, tr("%1 [Star Halo Removal]").arg(viewer->windowTitle()));
    } else {
        viewer->pushUndo();
        viewer->setBuffer(output, viewer->windowTitle(), true);
        viewer->refreshDisplay();
    }

    m_mainWindow->logMessage(tr("Star Halo Removal applied."), 1, true);
    accept();
}

void StarHaloRemovalDialog::onReset() {
    m_reductionSlider->blockSignals(true);
    m_linearCheck->blockSignals(true);

    m_reductionSlider->setValue(0);
    m_linearCheck->setChecked(false);
    m_applyTargetCombo->setCurrentIndex(0);
    m_previewCheck->setChecked(true);
    updateReductionLabel(0);

    m_reductionSlider->blockSignals(false);
    m_linearCheck->blockSignals(false);

    m_firstDisplay = true;
    updatePreview();
}

void StarHaloRemovalDialog::zoomIn() {
    const float newZoom = std::clamp(m_zoom * 1.15f, 0.10f, 10.0f);
    const float actual = newZoom / m_zoom;
    m_zoom = newZoom;
    m_view->scale(actual, actual);
}

void StarHaloRemovalDialog::zoomOut() {
    const float newZoom = std::clamp(m_zoom / 1.15f, 0.10f, 10.0f);
    const float actual = newZoom / m_zoom;
    m_zoom = newZoom;
    m_view->scale(actual, actual);
}

void StarHaloRemovalDialog::zoomFit() {
    if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) return;
    m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    m_zoom = std::clamp(static_cast<float>(m_view->transform().m11()), 0.10f, 10.0f);
}
