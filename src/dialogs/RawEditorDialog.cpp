/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is inspired by or incorporates code/design from RapidRAW
 * (hosted at https://github.com/CyberTimon/RapidRAW), which is
 * licensed under AGPL-3.0.
 * Inspired by the work of Timon Käch.
 * Modified by Ft2801 in 2026.
 * -------------------------------------------------------------------
 */

/**
 * @file RawEditorDialog.cpp
 * @brief Non-destructive light and color editor implementation.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "RawEditorDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QMessageBox>
#include <QApplication>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QMutexLocker>
#include <cmath>
#include <algorithm>

// ============================================================================
// No-Wheel Scroll Area -- prevents scroll events from reaching child widgets
// ============================================================================

class NoWheelScrollArea : public QScrollArea {
public:
    explicit NoWheelScrollArea(QWidget* parent = nullptr)
        : QScrollArea(parent) {}
protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

// ============================================================================
// No-Wheel Slider -- prevents accidental value changes via mouse wheel
// ============================================================================

class NoWheelSlider : public QSlider {
public:
    explicit NoWheelSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent) {}
protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

// ============================================================================
// Preview Worker Implementation
// ============================================================================

RawEditorPreviewWorker::RawEditorPreviewWorker(QObject* parent)
    : QThread(parent)
{
}

void RawEditorPreviewWorker::setSource(const ImageBuffer& source, int maxDim)
{
    // This method runs on the main thread. It creates a downsampled copy
    // of the source buffer so the worker thread never accesses the live buffer.
    int w, h, ch;
    {
        ImageBuffer::ReadLock srcLock(&source);
        w  = source.width();
        h  = source.height();
        ch = source.channels();
    }

    if (w <= 0 || h <= 0 || ch <= 0)
        return;

    // Compute downscale factor
    float sf = 1.0f;
    if (maxDim > 0 && (w > maxDim || h > maxDim))
        sf = static_cast<float>(maxDim) / std::max(w, h);

    const int pw = std::max(1, static_cast<int>(w * sf));
    const int ph = std::max(1, static_cast<int>(h * sf));

    // Build the cached downsampled buffer
    ImageBuffer cachedSource(pw, ph, 3);
    {
        ImageBuffer::ReadLock srcLock(&source);
        const auto& srcData = source.data();
        auto&       dstData = cachedSource.data();

        for (int py = 0; py < ph; ++py) {
            for (int px = 0; px < pw; ++px) {
                const int sx = std::clamp(static_cast<int>(px / sf), 0, w - 1);
                const int sy = std::clamp(static_cast<int>(py / sf), 0, h - 1);
                const size_t srcIdx = (static_cast<size_t>(sy) * w + sx) * ch;
                const size_t dstIdx = (static_cast<size_t>(py) * pw + px) * 3;

                if (ch >= 3) {
                    dstData[dstIdx]     = srcData[srcIdx];
                    dstData[dstIdx + 1] = srcData[srcIdx + 1];
                    dstData[dstIdx + 2] = srcData[srcIdx + 2];
                } else {
                    // Mono: replicate to all three channels
                    dstData[dstIdx]     = srcData[srcIdx];
                    dstData[dstIdx + 1] = srcData[srcIdx];
                    dstData[dstIdx + 2] = srcData[srcIdx];
                }
            }
        }
    }  // ReadLock released -- source buffer is never accessed again

    QMutexLocker lock(&m_mutex);
    m_sourceBuffer = std::move(cachedSource);
}

void RawEditorPreviewWorker::requestPreview(const RawEditor::Params& params)
{
    QMutexLocker lock(&m_mutex);
    m_params = params;
}

void RawEditorPreviewWorker::run()
{
    // Move the cached buffer out of the shared slot for exclusive use
    ImageBuffer       localBuf;
    RawEditor::Params params;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_sourceBuffer.isValid())
            return;
        localBuf = m_sourceBuffer;
        params   = m_params;
    }

    // Apply all adjustments on the local copy
    RawEditor::Processor::apply(localBuf, params);

    // Render the result to a QImage
    const int   pw     = localBuf.width();
    const int   ph     = localBuf.height();
    const auto& result = localBuf.data();

    QImage img(pw, ph, QImage::Format_RGB888);
    for (int y = 0; y < ph; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < pw; ++x) {
            const size_t idx = (static_cast<size_t>(y) * pw + x) * 3;
            auto to8Bit = [](float v) -> uchar {
                return static_cast<uchar>(
                    std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
            };
            line[x * 3]     = to8Bit(result[idx]);
            line[x * 3 + 1] = to8Bit(result[idx + 1]);
            line[x * 3 + 2] = to8Bit(result[idx + 2]);
        }
    }

    emit previewReady(img);
}

// ============================================================================
// Canvas Implementation
// ============================================================================

RawEditorCanvas::RawEditorCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void RawEditorCanvas::setImage(const QImage& img)
{
    m_image = img;
    if (m_fitOnNext) {
        m_showOriginal = false;
        fitToView();
        m_fitOnNext = false;
    }
    update();
}

void RawEditorCanvas::setOriginalImage(const QImage& img)
{
    m_originalImage = img;
}

void RawEditorCanvas::setShowOriginal(bool show)
{
    if (m_showOriginal == show)
        return;
    m_showOriginal = show;
    update();
}

void RawEditorCanvas::fitToView()
{
    const QImage& img = (m_showOriginal && !m_originalImage.isNull())
                            ? m_originalImage : m_image;
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return;

    // Fit image to canvas with a small margin, capped at 1:1
    float maxWidth  = static_cast<float>(width()  - 20);
    float maxHeight = static_cast<float>(height() - 20);

    float zoomX = maxWidth  / img.width();
    float zoomY = maxHeight / img.height();
    m_zoom = std::min(zoomX, zoomY);
    m_zoom = std::clamp(m_zoom, 0.05f, 1.0f);

    // Center the image in the canvas
    float drawWidth  = img.width()  * m_zoom;
    float drawHeight = img.height() * m_zoom;
    m_panOffset = QPointF(
        (width()  - drawWidth)  / 2.0f,
        (height() - drawHeight) / 2.0f);

    emit zoomChanged(m_zoom);
    update();
}

void RawEditorCanvas::zoomIn()
{
    zoomToPoint(1.25f, QPointF(width() / 2.0, height() / 2.0));
}

void RawEditorCanvas::zoomOut()
{
    zoomToPoint(0.8f, QPointF(width() / 2.0, height() / 2.0));
}

void RawEditorCanvas::zoomToPoint(float factor, QPointF viewPos)
{
    float oldZoom = m_zoom;
    m_zoom *= factor;
    m_zoom = std::clamp(m_zoom, 0.05f, 20.0f);

    float ratio  = m_zoom / oldZoom;
    m_panOffset  = viewPos - (viewPos - m_panOffset) * ratio;

    emit zoomChanged(m_zoom);
    update();
}

void RawEditorCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(30, 30, 30));

    const QImage& img = (m_showOriginal && !m_originalImage.isNull())
                            ? m_originalImage : m_image;
    if (img.isNull())
        return;

    QRectF target(m_panOffset.x(), m_panOffset.y(),
                  img.width() * m_zoom, img.height() * m_zoom);
    p.drawImage(target, img);
}

void RawEditorCanvas::wheelEvent(QWheelEvent* e)
{
    float factor = (e->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
    zoomToPoint(factor, e->position());
    e->accept();
}

void RawEditorCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging     = true;
        m_lastMousePos = e->position();
        setCursor(Qt::ClosedHandCursor);
    }
}

void RawEditorCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging) {
        QPointF delta = e->position() - m_lastMousePos;
        m_panOffset  += delta;
        m_lastMousePos = e->position();
        update();
    }
}

void RawEditorCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

void RawEditorCanvas::resizeEvent(QResizeEvent*)
{
    if (!m_image.isNull())
        fitToView();
}

// ============================================================================
// Dialog Construction
// ============================================================================

RawEditorDialog::RawEditorDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("RawEditor (Light and Color)"), 1200, 750)
    , m_viewer(viewer)
{
    setModal(true);

    // Initialize the background preview worker
    m_worker = new RawEditorPreviewWorker(this);
    connect(m_worker, &RawEditorPreviewWorker::previewReady,
            this,     &RawEditorDialog::onPreviewReady);

    // Cache the source once with a downsampled preview buffer
    if (m_viewer && m_viewer->getBuffer().isValid())
        m_worker->setSource(m_viewer->getBuffer(), 2048);

    buildUI();
    pushHistory();
    requestPreview();

    // Generate the original preview for comparison (same scale as worker)
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        const auto& buf = m_viewer->getBuffer();
        int w  = buf.width();
        int h  = buf.height();
        int ch = buf.channels();

        float sf = 1.0f;
        constexpr int maxDim = 2048;
        if (maxDim > 0 && (w > maxDim || h > maxDim))
            sf = static_cast<float>(maxDim) / std::max(w, h);

        int pw = std::max(1, static_cast<int>(w * sf));
        int ph = std::max(1, static_cast<int>(h * sf));

        QImage origImg(pw, ph, QImage::Format_RGB888);
        {
            ImageBuffer::ReadLock readLock(&buf);
            const auto& srcData = buf.data();
            for (int py = 0; py < ph; ++py) {
                uchar* line = origImg.scanLine(py);
                for (int px = 0; px < pw; ++px) {
                    int sx = std::clamp(static_cast<int>(px / sf), 0, w - 1);
                    int sy = std::clamp(static_cast<int>(py / sf), 0, h - 1);
                    size_t idx = (static_cast<size_t>(sy) * w + sx) * ch;

                    auto to8Bit = [](float v) -> uchar {
                        return static_cast<uchar>(
                            std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
                    };
                    float r = srcData[idx];
                    float g = (ch >= 3) ? srcData[idx + 1] : r;
                    float b = (ch >= 3) ? srcData[idx + 2] : r;

                    line[px * 3]     = to8Bit(r);
                    line[px * 3 + 1] = to8Bit(g);
                    line[px * 3 + 2] = to8Bit(b);
                }
            }
        }
        m_originalPreview = origImg;
        m_canvas->setOriginalImage(origImg);
    }
}

RawEditorDialog::~RawEditorDialog()
{
    if (m_worker->isRunning())
        m_worker->wait(3000);
}

// ============================================================================
// UI Construction
// ============================================================================

void RawEditorDialog::buildUI()
{
    auto* mainLayout = new QHBoxLayout();
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // -- Canvas (left, stretchy) --
    m_canvas = new RawEditorCanvas(this);
    auto* canvasLayout = new QVBoxLayout();
    canvasLayout->addWidget(buildToolbar());
    canvasLayout->addWidget(m_canvas, 1);
    canvasLayout->setSpacing(2);

    auto* canvasContainer = new QWidget();
    canvasContainer->setLayout(canvasLayout);

    // -- Control panel (right, scrollable) --
    auto* controlPanel = buildControlPanel();
    auto* scrollArea   = new NoWheelScrollArea();
    scrollArea->setWidget(controlPanel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(370);
    scrollArea->setMaximumWidth(450);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Splitter between canvas and controls
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(canvasContainer);
    splitter->addWidget(scrollArea);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);

    // -- Bottom bar: Cancel / Apply --
    auto* bottomBar = new QHBoxLayout();
    bottomBar->addStretch();

    auto* cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setStyleSheet("QPushButton { padding: 6px 16px; }");

    auto* applyBtn = new QPushButton(tr("Apply"));
    applyBtn->setDefault(true);
    applyBtn->setStyleSheet(
        "QPushButton { background-color: #2d7d46; color: white; "
        "padding: 6px 24px; font-weight: bold; border-radius: 4px; }");

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn,  &QPushButton::clicked, this, &RawEditorDialog::onApply);

    bottomBar->addWidget(cancelBtn);
    bottomBar->addWidget(applyBtn);

    // -- Outer layout --
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->addLayout(mainLayout, 1);
    outerLayout->addLayout(bottomBar);
    outerLayout->setContentsMargins(6, 6, 6, 6);
}

QWidget* RawEditorDialog::buildToolbar()
{
    auto* toolbar = new QWidget();
    auto* layout  = new QHBoxLayout(toolbar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto makeBtn = [](const QString& text) {
        auto* btn = new QPushButton(text);
        btn->setFixedHeight(28);
        btn->setStyleSheet("QPushButton { padding: 2px 8px; }");
        return btn;
    };

    auto* zoomInBtn  = makeBtn("+");
    auto* zoomOutBtn = makeBtn("-");
    auto* fitBtn     = makeBtn(tr("Fit"));
    auto* compareBtn = makeBtn(tr("Compare (Show Original)"));
    auto* resetBtn   = makeBtn(tr("Reset to Defaults"));
    m_undoBtn        = makeBtn(tr("Undo"));
    m_redoBtn        = makeBtn(tr("Redo"));

    connect(zoomInBtn,  &QPushButton::clicked,  m_canvas, &RawEditorCanvas::zoomIn);
    connect(zoomOutBtn, &QPushButton::clicked,  m_canvas, &RawEditorCanvas::zoomOut);
    connect(fitBtn,     &QPushButton::clicked,  m_canvas, &RawEditorCanvas::fitToView);
    connect(compareBtn, &QPushButton::pressed,  this,     &RawEditorDialog::comparePressed);
    connect(compareBtn, &QPushButton::released, this,     &RawEditorDialog::compareReleased);
    connect(resetBtn,   &QPushButton::clicked,  this,     &RawEditorDialog::onReset);
    connect(m_undoBtn,  &QPushButton::clicked,  this,     &RawEditorDialog::onUndo);
    connect(m_redoBtn,  &QPushButton::clicked,  this,     &RawEditorDialog::onRedo);

    layout->addWidget(zoomInBtn);
    layout->addWidget(zoomOutBtn);
    layout->addWidget(fitBtn);
    layout->addStretch();
    layout->addWidget(compareBtn);
    layout->addWidget(m_undoBtn);
    layout->addWidget(m_redoBtn);
    layout->addWidget(resetBtn);

    return toolbar;
}

// ============================================================================
// Control creation and wiring
// ============================================================================

RawEditorControl RawEditorDialog::createControl(
    const QString& name, float* param,
    float minVal, float maxVal, float defVal,
    int steps, QWidget* parent)
{
    RawEditorControl ctrl;
    ctrl.paramPtr    = param;
    ctrl.minVal      = minVal;
    ctrl.maxVal      = maxVal;
    ctrl.defaultVal  = defVal;
    ctrl.sliderSteps = steps;

    ctrl.label = new QLabel(name, parent);
    ctrl.label->setMinimumWidth(90);
    ctrl.label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    ctrl.slider = new NoWheelSlider(Qt::Horizontal, parent);
    ctrl.slider->setRange(0, steps);
    ctrl.slider->setFocusPolicy(Qt::NoFocus);
    int sliderVal = static_cast<int>(
        ((*param - minVal) / (maxVal - minVal)) * steps);
    ctrl.slider->setValue(sliderVal);

    ctrl.spinBox = new QDoubleSpinBox(parent);
    ctrl.spinBox->setRange(minVal, maxVal);
    ctrl.spinBox->setSingleStep((maxVal - minVal) / steps);
    ctrl.spinBox->setDecimals(3);
    ctrl.spinBox->setValue(*param);
    ctrl.spinBox->setFixedWidth(72);

    return ctrl;
}

void RawEditorDialog::connectControl(RawEditorControl& ctrl)
{
    // IMPORTANT: Capture by value, not reference, because m_controls is a
    // QVector that may reallocate when new controls are appended, which
    // would invalidate any captured references.
    float*          paramPtr    = ctrl.paramPtr;
    const float     minVal      = ctrl.minVal;
    const float     maxVal      = ctrl.maxVal;
    const int       sliderSteps = ctrl.sliderSteps;
    QSlider*        slider      = ctrl.slider;
    QDoubleSpinBox* spinBox     = ctrl.spinBox;

    // Slider drag: update parameter and spinbox only (no preview yet)
    connect(slider, &QSlider::valueChanged, this,
            [this, paramPtr, minVal, maxVal, sliderSteps, spinBox](int val) {
        if (m_blockSignals) return;
        float f = minVal + (maxVal - minVal) * val / sliderSteps;
        *paramPtr = f;
        m_blockSignals = true;
        spinBox->setValue(f);
        m_blockSignals = false;
        // Preview deferred until slider release for responsiveness
    });

    // Slider release: trigger preview with latest parameters
    connect(slider, &QSlider::sliderReleased, this, [this]() {
        onParamChanged();
    });

    // SpinBox change: immediate preview (fine-grained adjustments)
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, paramPtr, minVal, maxVal, sliderSteps, slider](double val) {
        if (m_blockSignals) return;
        *paramPtr = static_cast<float>(val);
        m_blockSignals = true;
        int sv = static_cast<int>(
            (static_cast<float>(val) - minVal) / (maxVal - minVal) * sliderSteps);
        slider->setValue(sv);
        m_blockSignals = false;
        onParamChanged();
    });
}

// ============================================================================
// Control panel sections
// ============================================================================

QWidget* RawEditorDialog::buildSection(
    const QString& title,
    const std::vector<std::tuple<QString, float*, float, float, float, int>>& controls)
{
    auto* group = new QGroupBox(title);
    group->setStyleSheet(
        "QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 16px; }");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(4);
    layout->setContentsMargins(4, 4, 4, 4);

    for (auto& [name, ptr, minV, maxV, defV, steps] : controls) {
        auto ctrl = createControl(name, ptr, minV, maxV, defV, steps, group);
        auto* row = new QHBoxLayout();
        row->addWidget(ctrl.label);
        row->addWidget(ctrl.slider, 1);
        row->addWidget(ctrl.spinBox);
        layout->addLayout(row);
        m_controls.append(ctrl);
        connectControl(m_controls.last());
    }

    return group;
}

QWidget* RawEditorDialog::buildHSLSection()
{
    auto* group = new QGroupBox(tr("HSL"));
    group->setStyleSheet(
        "QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 16px; }");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    static const QString hslNames[] = {
        tr("Red"), tr("Orange"), tr("Yellow"), tr("Green"),
        tr("Aqua"), tr("Blue"), tr("Purple"), tr("Magenta")
    };

    for (int i = 0; i < 8; ++i) {
        auto* subLabel = new QLabel(hslNames[i], group);
        subLabel->setStyleSheet(
            "font-weight: bold; color: #cccccc; margin-top: 4px;");
        layout->addWidget(subLabel);

        auto addCtrl = [&](const QString& name, float* ptr,
                           float minV, float maxV, float defV) {
            auto ctrl = createControl(name, ptr, minV, maxV, defV, 200, group);
            auto* row = new QHBoxLayout();
            row->addWidget(ctrl.label);
            row->addWidget(ctrl.slider, 1);
            row->addWidget(ctrl.spinBox);
            layout->addLayout(row);
            m_controls.append(ctrl);
            connectControl(m_controls.last());
        };

        addCtrl(tr("Hue"),        &m_params.hsl[i].hue,        -30.0f, 30.0f, 0.0f);
        addCtrl(tr("Saturation"), &m_params.hsl[i].saturation, -1.0f,  1.0f,  0.0f);
        addCtrl(tr("Luminance"),  &m_params.hsl[i].luminance,  -1.0f,  1.0f,  0.0f);
    }

    return group;
}

QWidget* RawEditorDialog::buildColorGradingSection()
{
    auto* group = new QGroupBox(tr("Color Grading"));
    group->setStyleSheet(
        "QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 16px; }");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    // Tonal zones: shadows, midtones, highlights
    struct Zone {
        QString name;
        RawEditor::ColorGradeSettings* settings;
    };
    Zone zones[] = {
        { tr("Shadows"),    &m_params.colorGradingShadows    },
        { tr("Midtones"),   &m_params.colorGradingMidtones   },
        { tr("Highlights"), &m_params.colorGradingHighlights }
    };

    for (auto& zone : zones) {
        auto* subLabel = new QLabel(zone.name, group);
        subLabel->setStyleSheet(
            "font-weight: bold; color: #cccccc; margin-top: 4px;");
        layout->addWidget(subLabel);

        auto addCtrl = [&](const QString& name, float* ptr,
                           float minV, float maxV, float defV) {
            auto ctrl = createControl(name, ptr, minV, maxV, defV, 200, group);
            auto* row = new QHBoxLayout();
            row->addWidget(ctrl.label);
            row->addWidget(ctrl.slider, 1);
            row->addWidget(ctrl.spinBox);
            layout->addLayout(row);
            m_controls.append(ctrl);
            connectControl(m_controls.last());
        };

        addCtrl(tr("Hue"),        &zone.settings->hue,        0.0f, 360.0f, 0.0f);
        addCtrl(tr("Saturation"), &zone.settings->saturation, 0.0f, 1.0f,   0.0f);
        addCtrl(tr("Luminance"),  &zone.settings->luminance, -1.0f, 1.0f,   0.0f);
    }

    // Global blending and balance controls
    auto addCtrl = [&](const QString& name, float* ptr,
                       float minV, float maxV, float defV) {
        auto ctrl = createControl(name, ptr, minV, maxV, defV, 200, group);
        auto* row = new QHBoxLayout();
        row->addWidget(ctrl.label);
        row->addWidget(ctrl.slider, 1);
        row->addWidget(ctrl.spinBox);
        layout->addLayout(row);
        m_controls.append(ctrl);
        connectControl(m_controls.last());
    };

    addCtrl(tr("Blending"), &m_params.colorGradingBlending,  0.0f, 1.0f, 1.0f);
    addCtrl(tr("Balance"),  &m_params.colorGradingBalance,  -1.0f, 1.0f, 0.0f);

    return group;
}

QWidget* RawEditorDialog::buildControlPanel()
{
    auto* panel  = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    // Basic adjustments
    layout->addWidget(buildSection(tr("Basic"), {
        { tr("Exposure"),   &m_params.exposure,   -5.0f,    5.0f,    0.0f, 1000 },
        { tr("Brightness"), &m_params.brightness, -6.25f,   6.25f,   0.0f, 500  },
        { tr("Contrast"),   &m_params.contrast,   -1.0f,    1.0f,    0.0f, 200  },
        { tr("Highlights"), &m_params.highlights, -0.8333f, 0.8333f, 0.0f, 200  },
        { tr("Shadows"),    &m_params.shadows,    -0.8333f, 0.8333f, 0.0f, 200  },
        { tr("Whites"),     &m_params.whites,     -3.3333f, 3.3333f, 0.0f, 300  },
        { tr("Blacks"),     &m_params.blacks,     -1.4286f, 1.4286f, 0.0f, 240  },
    }));

    // Color adjustments
    layout->addWidget(buildSection(tr("Color"), {
        { tr("Temperature"), &m_params.temperature, -1.0f, 1.0f, 0.0f, 200 },
        { tr("Tint"),        &m_params.tint,        -1.0f, 1.0f, 0.0f, 200 },
        { tr("Saturation"),  &m_params.saturation,  -1.0f, 1.0f, 0.0f, 200 },
        { tr("Vibrance"),    &m_params.vibrance,    -1.0f, 1.0f, 0.0f, 200 },
    }));

    // Per-channel color calibration
    layout->addWidget(buildSection(tr("Color Calibration"), {
        { tr("Red Hue"),      &m_params.redHue,      -1.0f, 1.0f, 0.0f, 200 },
        { tr("Green Hue"),    &m_params.greenHue,    -1.0f, 1.0f, 0.0f, 200 },
        { tr("Blue Hue"),     &m_params.blueHue,     -1.0f, 1.0f, 0.0f, 200 },
        { tr("Red Sat"),      &m_params.redSat,      -1.0f, 1.0f, 0.0f, 200 },
        { tr("Green Sat"),    &m_params.greenSat,    -1.0f, 1.0f, 0.0f, 200 },
        { tr("Blue Sat"),     &m_params.blueSat,     -1.0f, 1.0f, 0.0f, 200 },
        { tr("Shadows Tint"), &m_params.shadowsTint, -1.0f, 1.0f, 0.0f, 200 },
    }));

    // Detail / sharpening
    layout->addWidget(buildSection(tr("Detail"), {
        { tr("Sharpness"), &m_params.sharpness, 0.0f, 1.0f, 0.0f, 200 },
        { tr("Clarity"),   &m_params.clarity,  -1.0f, 1.0f, 0.0f, 200 },
        { tr("Structure"), &m_params.structure, -1.0f, 1.0f, 0.0f, 200 },
        { tr("Dehaze"),    &m_params.dehaze,   -1.0f, 1.0f, 0.0f, 200 },
    }));

    // HSL per-color adjustments
    layout->addWidget(buildHSLSection());

    // Color grading (shadows/midtones/highlights)
    layout->addWidget(buildColorGradingSection());

    // Chromatic aberration correction
    layout->addWidget(buildSection(tr("Chromatic Aberration"), {
        { tr("Red-Cyan"),    &m_params.caRedCyan,    -0.01f, 0.01f, 0.0f, 200 },
        { tr("Blue-Yellow"), &m_params.caBlueYellow, -0.01f, 0.01f, 0.0f, 200 },
    }));

    // Vignette
    layout->addWidget(buildSection(tr("Vignette"), {
        { tr("Amount"),    &m_params.vignetteAmount,    -1.0f, 1.0f, 0.0f, 200 },
        { tr("Midpoint"),  &m_params.vignetteMidpoint,   0.0f, 1.0f, 0.5f, 200 },
        { tr("Roundness"), &m_params.vignetteRoundness,  0.0f, 1.0f, 0.0f, 200 },
        { tr("Feather"),   &m_params.vignetteFeather,    0.0f, 1.0f, 0.5f, 200 },
    }));

    // Film grain
    layout->addWidget(buildSection(tr("Grain"), {
        { tr("Amount"),    &m_params.grainAmount,    0.0f, 1.0f, 0.0f, 200 },
        { tr("Size"),      &m_params.grainSize,      0.1f, 5.0f, 1.0f, 200 },
        { tr("Roughness"), &m_params.grainRoughness, 0.0f, 1.0f, 0.5f, 200 },
    }));

    layout->addStretch();
    return panel;
}

// ============================================================================
// Parameter change and preview pipeline
// ============================================================================

void RawEditorDialog::onParamChanged()
{
    requestPreview();
}

void RawEditorDialog::requestPreview()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid())
        return;

    if (m_worker->isRunning()) {
        // Queue a single re-render with the latest parameters.
        // When the current render completes, onPreviewReady() will
        // detect the flag and trigger another requestPreview().
        m_previewQueued = true;
        return;
    }

    m_worker->requestPreview(m_params);
    m_worker->start();
}

void RawEditorDialog::onPreviewReady(const QImage& img)
{
    m_canvas->setImage(img);

    // Save to undo history if this was a normal parameter change
    if (!m_inUndoRedo) {
        if (m_historyIndex < 0 || m_history[m_historyIndex] != m_params)
            pushHistory();
    }
    m_inUndoRedo = false;

    // Process any queued re-render request
    if (m_previewQueued) {
        m_previewQueued = false;
        requestPreview();
    }
}

// ============================================================================
// Apply -- full-resolution processing
// ============================================================================

void RawEditorDialog::onApply()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        reject();
        return;
    }

    // Wait for any in-progress preview to finish before writing
    if (m_worker->isRunning())
        m_worker->wait(5000);

    QApplication::setOverrideCursor(Qt::WaitCursor);

    m_viewer->pushUndo(tr("Raw Editor"));

    // Apply at full resolution on the live buffer
    RawEditor::Processor::apply(m_viewer->getBuffer(), m_params);
    m_viewer->setBuffer(m_viewer->getBuffer(), m_viewer->windowTitle(), true);

    if (auto mw = getCallbacks())
        mw->logMessage(tr("Raw Editor applied."), 1);

    QApplication::restoreOverrideCursor();
    accept();
}

// ============================================================================
// Reset to defaults
// ============================================================================

void RawEditorDialog::onReset()
{
    m_params = RawEditor::Params();
    updateControlsFromParams();
    pushHistory();
    onParamChanged();
}

// ============================================================================
// Control synchronization
// ============================================================================

void RawEditorDialog::updateControlsFromParams()
{
    m_blockSignals = true;
    for (auto& ctrl : m_controls) {
        ctrl.spinBox->setValue(*ctrl.paramPtr);
        int sv = static_cast<int>(
            (*ctrl.paramPtr - ctrl.minVal) /
            (ctrl.maxVal - ctrl.minVal) * ctrl.sliderSteps);
        ctrl.slider->setValue(sv);
    }
    m_blockSignals = false;
}

// ============================================================================
// Undo / Redo
// ============================================================================

void RawEditorDialog::pushHistory()
{
    // Discard any forward redo states
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();

    m_history.append(m_params);
    m_historyIndex = m_history.size() - 1;

    if (m_undoBtn) m_undoBtn->setEnabled(m_historyIndex > 0);
    if (m_redoBtn) m_redoBtn->setEnabled(false);
}

void RawEditorDialog::onUndo()
{
    if (m_historyIndex <= 0)
        return;

    m_inUndoRedo = true;

    // Ensure the current state is saved before stepping back
    if (m_historyIndex == m_history.size() - 1) {
        if (m_history.size() > m_historyIndex)
            m_history[m_historyIndex] = m_params;
    }

    m_historyIndex--;
    m_params = m_history[m_historyIndex];
    updateControlsFromParams();
    onParamChanged();

    if (auto mw = getCallbacks())
        mw->logMessage(tr("Undo: Raw Editor parameters."), 1);

    if (m_undoBtn) m_undoBtn->setEnabled(m_historyIndex > 0);
    if (m_redoBtn) m_redoBtn->setEnabled(m_historyIndex < m_history.size() - 1);
}

void RawEditorDialog::onRedo()
{
    if (m_historyIndex >= m_history.size() - 1)
        return;

    m_inUndoRedo = true;

    m_historyIndex++;
    m_params = m_history[m_historyIndex];
    updateControlsFromParams();
    onParamChanged();

    if (auto mw = getCallbacks())
        mw->logMessage(tr("Redo: Raw Editor parameters."), 1);

    if (m_undoBtn) m_undoBtn->setEnabled(m_historyIndex > 0);
    if (m_redoBtn) m_redoBtn->setEnabled(m_historyIndex < m_history.size() - 1);
}

// ============================================================================
// Compare (original vs. edited)
// ============================================================================

void RawEditorDialog::comparePressed()
{
    m_canvas->setShowOriginal(true);
}

void RawEditorDialog::compareReleased()
{
    m_canvas->setShowOriginal(false);
}

// ============================================================================
// Keyboard shortcuts
// ============================================================================

void RawEditorDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Z &&
        event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        onRedo();
        event->accept();
    } else if (event->key() == Qt::Key_Z &&
               event->modifiers() == Qt::ControlModifier) {
        onUndo();
        event->accept();
    } else {
        DialogBase::keyPressEvent(event);
    }
}