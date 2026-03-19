#include "RawEditorDialog.h"
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
#include <QDebug>
#include <cmath>

// ═══════════════════════════════════════════════════════════════════════════════
// No-Wheel Scroll Area — prevents mouse wheel from scrolling controls panel
// ═══════════════════════════════════════════════════════════════════════════════

class NoWheelScrollArea : public QScrollArea {
public:
    explicit NoWheelScrollArea(QWidget* parent = nullptr) : QScrollArea(parent) {}
protected:
    void wheelEvent(QWheelEvent* event) override {
        // Ignore wheel events — allow them to propagate up to parent
        event->ignore();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// No-Wheel Slider — prevents mouse wheel from changing slider value
// ═══════════════════════════════════════════════════════════════════════════════

class NoWheelSlider : public QSlider {
public:
    explicit NoWheelSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent) {}
protected:
    void wheelEvent(QWheelEvent* event) override {
        // Completely ignore wheel events on slider
        event->ignore();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Preview Worker
// ═══════════════════════════════════════════════════════════════════════════════

RawEditorPreviewWorker::RawEditorPreviewWorker(QObject* parent) : QThread(parent) {}

void RawEditorPreviewWorker::setSource(const ImageBuffer& source,
                                       int maxDim) {
    // ── RUNS ON THE MAIN THREAD ───────────────────────────────────────────────
    // Make the downsampled copy here, synchronously, so the worker thread never
    // needs to access the live source buffer at all.  This is the same pattern
    // used by WavescaleHDRWorker::setup(), StarAnalysisWorker::setup(), and
    // ContinuumSubtractWorker — the only safe cross-thread ImageBuffer pattern.
    //
    // On the main thread, SwapManager is also on the main thread and cannot run
    // concurrently, so ReadLock below is primarily for correctness when called
    // from a context where another thread could be interacting with the buffer.

    int w, h, ch;
    {
        ImageBuffer::ReadLock srcLock(&source);
        w  = source.width();
        h  = source.height();
        ch = source.channels();
    }
    if (w <= 0 || h <= 0 || ch <= 0) return;

    float sf = 1.0f;
    if (maxDim > 0 && (w > maxDim || h > maxDim))
        sf = static_cast<float>(maxDim) / std::max(w, h);
    const int pw = std::max(1, static_cast<int>(w * sf));
    const int ph = std::max(1, static_cast<int>(h * sf));

    // Copy source into the worker cache once, optionally downsampled.
    ImageBuffer cachedSource(pw, ph, 3);
    {
        ImageBuffer::ReadLock srcLock(&source);
        const auto& srcData = source.data();
        auto& dstData = cachedSource.data();

        for (int py = 0; py < ph; ++py) {
            for (int px = 0; px < pw; ++px) {
                const int sx = std::clamp(static_cast<int>(px / sf), 0, w - 1);
                const int sy = std::clamp(static_cast<int>(py / sf), 0, h - 1);
                const size_t srcIdx = (static_cast<size_t>(sy) * w + sx) * ch;
                const size_t dstIdx = (static_cast<size_t>(py) * pw + px) * 3;
                if (ch >= 3) {
                    dstData[dstIdx    ] = srcData[srcIdx    ];
                    dstData[dstIdx + 1] = srcData[srcIdx + 1];
                    dstData[dstIdx + 2] = srcData[srcIdx + 2];
                } else {
                    dstData[dstIdx    ] = srcData[srcIdx];
                    dstData[dstIdx + 1] = srcData[srcIdx];
                    dstData[dstIdx + 2] = srcData[srcIdx];
                }
            }
        }
    }  // srcLock released — source buffer is NEVER accessed again after this point

    QMutexLocker lock(&m_mutex);
    m_sourceBuffer = std::move(cachedSource);
}

void RawEditorPreviewWorker::requestPreview(const RawEditor::Params& params) {
    QMutexLocker lock(&m_mutex);
    m_params = params;
}

void RawEditorPreviewWorker::run() {
    // ── RUNS ON THE WORKER THREAD ─────────────────────────────────────────────
    // Move the pre-made copy out of the shared slot immediately so the mutex
    // can be released. After this point, localBuf is exclusively owned by
    // this thread.
    ImageBuffer       localBuf;
    RawEditor::Params params;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_sourceBuffer.isValid()) return;
        localBuf = m_sourceBuffer;
        params = m_params;
    }

    // Apply all adjustments on our copy.
    RawEditor::Processor::apply(localBuf, params);

    // Render the result to a QImage.
    const int pw = localBuf.width();
    const int ph = localBuf.height();
    const auto& result = localBuf.data();
    QImage img(pw, ph, QImage::Format_RGB888);
    for (int y = 0; y < ph; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < pw; ++x) {
            const size_t idx = (static_cast<size_t>(y) * pw + x) * 3;
            auto to8Bit = [](float v) -> uchar {
                return static_cast<uchar>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
            };
            line[x * 3    ] = to8Bit(result[idx    ]);
            line[x * 3 + 1] = to8Bit(result[idx + 1]);
            line[x * 3 + 2] = to8Bit(result[idx + 2]);
        }
    }

    emit previewReady(img);
}
// ═══════════════════════════════════════════════════════════════════════════════
// Canvas
// ═══════════════════════════════════════════════════════════════════════════════

RawEditorCanvas::RawEditorCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void RawEditorCanvas::setImage(const QImage& img) {
    m_image = img;
    if (m_fitOnNext) { fitToView(); m_fitOnNext = false; }
    update();
}

void RawEditorCanvas::setOriginalImage(const QImage& img) {
    m_originalImage = img;
}

void RawEditorCanvas::setShowOriginal(bool show) {
    m_showOriginal = show;
    update();
}

void RawEditorCanvas::fitToView() {
    if (m_image.isNull()) return;
    float scaleX = static_cast<float>(width()) / m_image.width();
    float scaleY = static_cast<float>(height()) / m_image.height();
    m_zoom = std::min(scaleX, scaleY) * 0.95f;
    m_panOffset = QPointF(
        (width() - m_image.width() * m_zoom) / 2.0f,
        (height() - m_image.height() * m_zoom) / 2.0f
    );
    emit zoomChanged(m_zoom);
    update();
}

void RawEditorCanvas::zoomIn() { zoomToPoint(1.25f, QPointF(width() / 2.0, height() / 2.0)); }
void RawEditorCanvas::zoomOut() { zoomToPoint(0.8f, QPointF(width() / 2.0, height() / 2.0)); }

void RawEditorCanvas::zoomToPoint(float factor, QPointF viewPos) {
    float oldZoom = m_zoom;
    m_zoom *= factor;
    m_zoom = std::clamp(m_zoom, 0.05f, 20.0f);
    float ratio = m_zoom / oldZoom;
    m_panOffset = viewPos - (viewPos - m_panOffset) * ratio;
    emit zoomChanged(m_zoom);
    update();
}

void RawEditorCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(30, 30, 30));

    const QImage& img = (m_showOriginal && !m_originalImage.isNull()) ? m_originalImage : m_image;
    if (img.isNull()) return;

    QRectF target(m_panOffset.x(), m_panOffset.y(),
                  img.width() * m_zoom, img.height() * m_zoom);
    p.drawImage(target, img);
}

void RawEditorCanvas::wheelEvent(QWheelEvent* e) {
    float factor = (e->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
    zoomToPoint(factor, e->position());
    e->accept();
}

void RawEditorCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastMousePos = e->position();
        setCursor(Qt::ClosedHandCursor);
    }
}

void RawEditorCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging) {
        QPointF delta = e->position() - m_lastMousePos;
        m_panOffset += delta;
        m_lastMousePos = e->position();
        update();
    }
}

void RawEditorCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

void RawEditorCanvas::resizeEvent(QResizeEvent*) {
    if (!m_image.isNull()) fitToView();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Dialog
// ═══════════════════════════════════════════════════════════════════════════════

RawEditorDialog::RawEditorDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("RawEditor (Light and Color)"), 1200, 750)
    , m_viewer(viewer)
{
    setModal(true);
    m_worker = new RawEditorPreviewWorker(this);
    connect(m_worker, &RawEditorPreviewWorker::previewReady, this, &RawEditorDialog::onPreviewReady);

    // Cache the source once with downsampled preview buffer (max 2048px for instant feedback).
    // During slider drag: no updates. On release: instant preview render on downsampled buffer.
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_worker->setSource(m_viewer->getBuffer(), 2048); // 2048px max dimension for snappy preview
    }

    buildUI();
    pushHistory(); // Initial state
    requestPreview();

    // Generate original preview for compare
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        const auto& buf = m_viewer->getBuffer();
        int w = buf.width(), h = buf.height(), ch = buf.channels();
        float sf = 1.0f;
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
                        return static_cast<uchar>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
                    };
                    float r = srcData[idx];
                    float g = (ch >= 3) ? srcData[idx + 1] : r;
                    float b = (ch >= 3) ? srcData[idx + 2] : r;
                    line[px * 3 + 0] = to8Bit(r);
                    line[px * 3 + 1] = to8Bit(g);
                    line[px * 3 + 2] = to8Bit(b);
                }
            }
        }
        m_originalPreview = origImg;
        m_canvas->setOriginalImage(origImg);
    }
}

RawEditorDialog::~RawEditorDialog() {
    if (m_worker->isRunning()) {
        m_worker->wait(3000);
    }
}

void RawEditorDialog::buildUI() {
    auto* mainLayout = new QHBoxLayout();
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Canvas (left side, stretchy)
    m_canvas = new RawEditorCanvas(this);
    auto* canvasLayout = new QVBoxLayout();
    canvasLayout->addWidget(buildToolbar());
    canvasLayout->addWidget(m_canvas, 1);
    canvasLayout->setSpacing(2);

    auto* canvasContainer = new QWidget();
    canvasContainer->setLayout(canvasLayout);

    // Control panel (right side, scroll)
    auto* controlPanel = buildControlPanel();
    auto* scrollArea = new NoWheelScrollArea();
    scrollArea->setWidget(controlPanel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(370);
    scrollArea->setMaximumWidth(450);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(canvasContainer);
    splitter->addWidget(scrollArea);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);

    // Bottom bar: Apply/Cancel
    auto* bottomBar = new QHBoxLayout();
    bottomBar->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancel"));
    auto* applyBtn = new QPushButton(tr("Apply"));
    applyBtn->setDefault(true);
    applyBtn->setStyleSheet("QPushButton { background-color: #2d7d46; color: white; padding: 6px 24px; font-weight: bold; border-radius: 4px; }");
    cancelBtn->setStyleSheet("QPushButton { padding: 6px 16px; }");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &RawEditorDialog::onApply);
    bottomBar->addWidget(cancelBtn);
    bottomBar->addWidget(applyBtn);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->addLayout(mainLayout, 1);
    outerLayout->addLayout(bottomBar);
    outerLayout->setContentsMargins(6, 6, 6, 6);
}

QWidget* RawEditorDialog::buildToolbar() {
    auto* toolbar = new QWidget();
    auto* layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto makeBtn = [](const QString& text) {
        auto* btn = new QPushButton(text);
        btn->setFixedHeight(28);
        btn->setStyleSheet("QPushButton { padding: 2px 8px; }");
        return btn;
    };

    auto* zoomInBtn = makeBtn("+");
    auto* zoomOutBtn = makeBtn("−");
    auto* fitBtn = makeBtn(tr("Fit"));
    auto* compareBtn = makeBtn(tr("Compare (Show Original)"));
    auto* resetBtn = makeBtn(tr("Reset to Defaults"));
    m_undoBtn = makeBtn(tr("Undo"));
    m_redoBtn = makeBtn(tr("Redo"));

    connect(zoomInBtn, &QPushButton::clicked, m_canvas, &RawEditorCanvas::zoomIn);
    connect(zoomOutBtn, &QPushButton::clicked, m_canvas, &RawEditorCanvas::zoomOut);
    connect(fitBtn, &QPushButton::clicked, m_canvas, &RawEditorCanvas::fitToView);
    connect(compareBtn, &QPushButton::pressed, this, &RawEditorDialog::comparePressed);
    connect(compareBtn, &QPushButton::released, this, &RawEditorDialog::compareReleased);
    connect(resetBtn, &QPushButton::clicked, this, &RawEditorDialog::onReset);
    connect(m_undoBtn, &QPushButton::clicked, this, &RawEditorDialog::onUndo);
    connect(m_redoBtn, &QPushButton::clicked, this, &RawEditorDialog::onRedo);

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

RawEditorControl RawEditorDialog::createControl(const QString& name, float* param,
                                                 float minVal, float maxVal, float defVal,
                                                 int steps, QWidget* parent) {
    RawEditorControl ctrl;
    ctrl.paramPtr = param;
    ctrl.minVal = minVal;
    ctrl.maxVal = maxVal;
    ctrl.defaultVal = defVal;
    ctrl.sliderSteps = steps;

    auto* row = new QHBoxLayout();
    ctrl.label = new QLabel(name, parent);
    ctrl.label->setMinimumWidth(90);
    ctrl.label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    ctrl.slider = new NoWheelSlider(Qt::Horizontal, parent);
    ctrl.slider->setRange(0, steps);
    ctrl.slider->setFocusPolicy(Qt::NoFocus);
    int sliderVal = static_cast<int>(((*param - minVal) / (maxVal - minVal)) * steps);
    ctrl.slider->setValue(sliderVal);

    ctrl.spinBox = new QDoubleSpinBox(parent);
    ctrl.spinBox->setRange(minVal, maxVal);
    ctrl.spinBox->setSingleStep((maxVal - minVal) / steps);
    ctrl.spinBox->setDecimals(3);
    ctrl.spinBox->setValue(*param);
    ctrl.spinBox->setFixedWidth(72);

    (void)row;  // Layout management done by caller
    return ctrl;
}

void RawEditorDialog::connectControl(RawEditorControl& ctrl) {
    // IMPORTANT: never capture `ctrl` by reference in these lambdas.
    // `m_controls` is a QVector; subsequent append() operations can reallocate
    // and invalidate references to existing elements, causing use-after-free
    // crashes when sliders are moved.
    float* paramPtr = ctrl.paramPtr;
    const float minVal = ctrl.minVal;
    const float maxVal = ctrl.maxVal;
    const int sliderSteps = ctrl.sliderSteps;
    QSlider* slider = ctrl.slider;
    QDoubleSpinBox* spinBox = ctrl.spinBox;

    // During slider drag: ONLY update spinBox value, NO preview rendering.
    // This preserves UI responsiveness and prevents buffer processing during interaction.
    connect(slider, &QSlider::valueChanged, this, [this, paramPtr, minVal, maxVal, sliderSteps, spinBox](int val) {
        if (m_blockSignals) return;
        float f = minVal + (maxVal - minVal) * val / sliderSteps;
        *paramPtr = f;
        m_blockSignals = true;
        spinBox->setValue(f);
        m_blockSignals = false;
        // ← NO onParamChanged() call here: defer until mouse release
    });

    // On slider release: trigger immediate preview with latest params on downsampled buffer.
    // Combined with maxDim=2048px cache, this gives instant visual feedback.
    connect(slider, &QSlider::sliderReleased, this, [this](void) {
        // Update param if needed (value already set by valueChanged lambda above)
        onParamChanged();
    });

    // SpinBox: also trigger on release (user typically uses for fine-grained adjustments)
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, paramPtr, minVal, maxVal, sliderSteps, slider](double val) {
        if (m_blockSignals) return;
        *paramPtr = static_cast<float>(val);
        m_blockSignals = true;
        int sv = static_cast<int>((static_cast<float>(val) - minVal) /
                                   (maxVal - minVal) * sliderSteps);
        slider->setValue(sv);
        m_blockSignals = false;
        // Trigger preview immediately for spinbox changes (smaller deltas, interactive feel)
        onParamChanged();
    });
}

QWidget* RawEditorDialog::buildSection(const QString& title,
    const std::vector<std::tuple<QString, float*, float, float, float, int>>& controls) {

    auto* group = new QGroupBox(title);
    group->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 16px; }");
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

QWidget* RawEditorDialog::buildHSLSection() {
    auto* group = new QGroupBox(tr("HSL"));
    group->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 16px; }");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    static const QString hslNames[] = {
        tr("Red"), tr("Orange"), tr("Yellow"), tr("Green"),
        tr("Aqua"), tr("Blue"), tr("Purple"), tr("Magenta")
    };

    for (int i = 0; i < 8; ++i) {
        auto* subLabel = new QLabel(hslNames[i], group);
        subLabel->setStyleSheet("font-weight: bold; color: #cccccc; margin-top: 4px;");
        layout->addWidget(subLabel);

        auto addCtrl = [&](const QString& name, float* ptr, float minV, float maxV, float defV) {
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
        addCtrl(tr("Saturation"), &m_params.hsl[i].saturation, -1.0f, 1.0f, 0.0f);
        addCtrl(tr("Luminance"),  &m_params.hsl[i].luminance,  -1.0f, 1.0f, 0.0f);
    }

    return group;
}

QWidget* RawEditorDialog::buildColorGradingSection() {
    auto* group = new QGroupBox(tr("Color Grading"));
    group->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 16px; }");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    struct Zone {
        QString name;
        RawEditor::ColorGradeSettings* settings;
    };
    Zone zones[] = {
        { tr("Shadows"),    &m_params.colorGradingShadows },
        { tr("Midtones"),   &m_params.colorGradingMidtones },
        { tr("Highlights"), &m_params.colorGradingHighlights }
    };

    for (auto& zone : zones) {
        auto* subLabel = new QLabel(zone.name, group);
        subLabel->setStyleSheet("font-weight: bold; color: #cccccc; margin-top: 4px;");
        layout->addWidget(subLabel);

        auto addCtrl = [&](const QString& name, float* ptr, float minV, float maxV, float defV) {
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
        addCtrl(tr("Saturation"), &zone.settings->saturation, 0.0f, 1.0f, 0.0f);
        addCtrl(tr("Luminance"),  &zone.settings->luminance,  -1.0f, 1.0f, 0.0f);
    }

    // Blending and Balance
    auto addCtrl = [&](const QString& name, float* ptr, float minV, float maxV, float defV) {
        auto ctrl = createControl(name, ptr, minV, maxV, defV, 200, group);
        auto* row = new QHBoxLayout();
        row->addWidget(ctrl.label);
        row->addWidget(ctrl.slider, 1);
        row->addWidget(ctrl.spinBox);
        layout->addLayout(row);
        m_controls.append(ctrl);
        connectControl(m_controls.last());
    };

    addCtrl(tr("Blending"), &m_params.colorGradingBlending, 0.0f, 1.0f, 1.0f);
    addCtrl(tr("Balance"),  &m_params.colorGradingBalance,  -1.0f, 1.0f, 0.0f);

    return group;
}

QWidget* RawEditorDialog::buildControlPanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);

    // Basic section
    layout->addWidget(buildSection(tr("Basic"), {
        { tr("Exposure"),   &m_params.exposure,   -5.0f, 5.0f, 0.0f, 1000 },
        { tr("Brightness"), &m_params.brightness, -6.25f, 6.25f, 0.0f, 500 },
        { tr("Contrast"),   &m_params.contrast,   -1.0f, 1.0f, 0.0f, 200 },
        { tr("Highlights"), &m_params.highlights, -0.8333f, 0.8333f, 0.0f, 200 },
        { tr("Shadows"),    &m_params.shadows,    -0.8333f, 0.8333f, 0.0f, 200 },
        { tr("Whites"),     &m_params.whites,     -3.3333f, 3.3333f, 0.0f, 300 },
        { tr("Blacks"),     &m_params.blacks,     -1.4286f, 1.4286f, 0.0f, 240 },
    }));

    // Color section
    layout->addWidget(buildSection(tr("Color"), {
        { tr("Temperature"), &m_params.temperature, -1.0f, 1.0f, 0.0f, 200 },
        { tr("Tint"),        &m_params.tint,        -1.0f, 1.0f, 0.0f, 200 },
        { tr("Saturation"),  &m_params.saturation,  -1.0f, 1.0f, 0.0f, 200 },
        { tr("Vibrance"),    &m_params.vibrance,    -1.0f, 1.0f, 0.0f, 200 },
    }));

    // Color Calibration (per-channel hue and saturation)
    layout->addWidget(buildSection(tr("Color Calibration"), {
        { tr("Red Hue"),     &m_params.redHue,   -1.0f, 1.0f, 0.0f, 200 },
        { tr("Green Hue"),   &m_params.greenHue, -1.0f, 1.0f, 0.0f, 200 },
        { tr("Blue Hue"),    &m_params.blueHue,  -1.0f, 1.0f, 0.0f, 200 },
        { tr("Red Sat"),     &m_params.redSat,   -1.0f, 1.0f, 0.0f, 200 },
        { tr("Green Sat"),   &m_params.greenSat, -1.0f, 1.0f, 0.0f, 200 },
        { tr("Blue Sat"),    &m_params.blueSat,  -1.0f, 1.0f, 0.0f, 200 },
        { tr("Shadows Tint"), &m_params.shadowsTint, -1.0f, 1.0f, 0.0f, 200 },
    }));

    // Detail section
    layout->addWidget(buildSection(tr("Detail"), {
        { tr("Sharpness"), &m_params.sharpness, 0.0f, 1.0f, 0.0f, 200 },
        { tr("Clarity"),   &m_params.clarity,   -1.0f, 1.0f, 0.0f, 200 },
        { tr("Structure"), &m_params.structure, -1.0f, 1.0f, 0.0f, 200 },
        { tr("Dehaze"),    &m_params.dehaze,    -1.0f, 1.0f, 0.0f, 200 },
    }));

    // HSL
    layout->addWidget(buildHSLSection());

    // Color Grading
    layout->addWidget(buildColorGradingSection());

    // Chromatic Aberration
    layout->addWidget(buildSection(tr("Chromatic Aberration"), {
        { tr("Red-Cyan"),     &m_params.caRedCyan,    -0.01f, 0.01f, 0.0f, 200 },
        { tr("Blue-Yellow"),  &m_params.caBlueYellow, -0.01f, 0.01f, 0.0f, 200 },
    }));

    // Vignette
    layout->addWidget(buildSection(tr("Vignette"), {
        { tr("Amount"),    &m_params.vignetteAmount,    -1.0f, 1.0f, 0.0f, 200 },
        { tr("Midpoint"),  &m_params.vignetteMidpoint,  0.0f, 1.0f, 0.5f, 200 },
        { tr("Roundness"), &m_params.vignetteRoundness, 0.0f, 1.0f, 0.0f, 200 },
        { tr("Feather"),   &m_params.vignetteFeather,   0.0f, 1.0f, 0.5f, 200 },
    }));

    // Grain
    layout->addWidget(buildSection(tr("Grain"), {
        { tr("Amount"),    &m_params.grainAmount,    0.0f, 1.0f, 0.0f, 200 },
        { tr("Size"),      &m_params.grainSize,      0.1f, 5.0f, 1.0f, 200 },
        { tr("Roughness"), &m_params.grainRoughness, 0.0f, 1.0f, 0.5f, 200 },
    }));

    layout->addStretch();
    return panel;
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void RawEditorDialog::onParamChanged() {
    // Direct request: no debounce, no delay. Instant preview on 2048px downsampled buffer.
    // Worker will render and emit previewReady() as soon as possible.
    requestPreview();
}

void RawEditorDialog::requestPreview() {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    if (m_worker->isRunning()) {
        // Keep only one queued rerender; latest m_params will be used.
        // When current render finishes, onPreviewReady() will see m_previewQueued=true
        // and automatically call requestPreview() again with latest params.
        m_previewQueued = true;
        return;
    }
    m_worker->requestPreview(m_params);
    m_worker->start();
}

void RawEditorDialog::onPreviewReady(const QImage& img) {
    m_canvas->setImage(img);
    if (m_previewQueued) {
        m_previewQueued = false;
        requestPreview();
    }
}

void RawEditorDialog::onApply() {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        reject();
        return;
    }

    // Stop any in-progress preview before writing to the source buffer.
    // Without this wait, apply()'s WriteLock would block until the worker's
    // ReadLock is released, hanging the GUI thread.
    if (m_worker->isRunning()) {
        m_worker->wait(5000);
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Push undo in the viewer
    m_viewer->pushUndo();

    // Apply at full resolution
    RawEditor::Processor::apply(m_viewer->getBuffer(), m_params);
    m_viewer->setBuffer(m_viewer->getBuffer(), m_viewer->windowTitle(), true);

    QApplication::restoreOverrideCursor();
    accept();
}

void RawEditorDialog::onReset() {
    m_params = RawEditor::Params();
    updateControlsFromParams();
    pushHistory();
    onParamChanged();
}

void RawEditorDialog::updateControlsFromParams() {
    m_blockSignals = true;
    for (auto& ctrl : m_controls) {
        ctrl.spinBox->setValue(*ctrl.paramPtr);
        int sv = static_cast<int>((*ctrl.paramPtr - ctrl.minVal) /
                                   (ctrl.maxVal - ctrl.minVal) * ctrl.sliderSteps);
        ctrl.slider->setValue(sv);
    }
    m_blockSignals = false;
}

void RawEditorDialog::pushHistory() {
    // Remove any redo states
    while (m_history.size() > m_historyIndex + 1) {
        m_history.removeLast();
    }
    m_history.append(m_params);
    m_historyIndex = m_history.size() - 1;
    if (m_undoBtn) m_undoBtn->setEnabled(m_historyIndex > 0);
    if (m_redoBtn) m_redoBtn->setEnabled(false);
}

void RawEditorDialog::onUndo() {
    if (m_historyIndex <= 0) return;
    // Save current state if at the end
    if (m_historyIndex == m_history.size() - 1) {
        // Ensure current params are saved
        if (m_history.size() > m_historyIndex) {
            m_history[m_historyIndex] = m_params;
        }
    }
    m_historyIndex--;
    m_params = m_history[m_historyIndex];
    updateControlsFromParams();
    onParamChanged();
    if (m_undoBtn) m_undoBtn->setEnabled(m_historyIndex > 0);
    if (m_redoBtn) m_redoBtn->setEnabled(m_historyIndex < m_history.size() - 1);
}

void RawEditorDialog::onRedo() {
    if (m_historyIndex >= m_history.size() - 1) return;
    m_historyIndex++;
    m_params = m_history[m_historyIndex];
    updateControlsFromParams();
    onParamChanged();
    if (m_undoBtn) m_undoBtn->setEnabled(m_historyIndex > 0);
    if (m_redoBtn) m_redoBtn->setEnabled(m_historyIndex < m_history.size() - 1);
}

void RawEditorDialog::comparePressed() {
    m_canvas->setShowOriginal(true);
}

void RawEditorDialog::compareReleased() {
    m_canvas->setShowOriginal(false);
}

void RawEditorDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Z && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        onRedo();
        event->accept();
    } else if (event->key() == Qt::Key_Z && event->modifiers() == Qt::ControlModifier) {
        onUndo();
        event->accept();
    } else {
        DialogBase::keyPressEvent(event);
    }
}
