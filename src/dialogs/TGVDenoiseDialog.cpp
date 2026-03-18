/*
 * TGVDenoiseDialog.cpp  —  Qt6 dialog for TGV² denoising
 */

#include "TGVDenoiseDialog.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "core/Logger.h"
#include "TGVDenoise.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <cmath>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

TGVDenoiseDialog::TGVDenoiseDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent)
    : QDialog(parent), m_viewer(viewer), m_mainWindow(mw)
{
    setWindowTitle(tr("TGV Denoise"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setMinimumWidth(380);

    // Snapshot original for reset
    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();

    m_watcher = new QFutureWatcher<TGVResult>(this);

    buildUI();
    connectSignals();
}

TGVDenoiseDialog::~TGVDenoiseDialog() {
    if (m_watcher->isRunning()) m_watcher->cancel();
}

void TGVDenoiseDialog::setViewer(ImageViewer* viewer)
{
    m_viewer = viewer;
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer();
    }
}

// ─── Build UI ────────────────────────────────────────────────────────────────

void TGVDenoiseDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(12, 12, 12, 12);

    // ── Strength group ────────────────────────────────────────────────────
    auto* grpStrength = new QGroupBox(tr("Noise Reduction Strength"), this);
    auto* vStrength   = new QVBoxLayout(grpStrength);

    auto* hSlider = new QHBoxLayout;
    auto* lblLow  = new QLabel(tr("Low"),  this);
    auto* lblHigh = new QLabel(tr("High"), this);
    m_strengthSlider = new QSlider(Qt::Horizontal, this);
    m_strengthSlider->setRange(1, 100);
    m_strengthSlider->setValue(30);
    m_strengthSlider->setTickPosition(QSlider::TicksBelow);
    m_strengthSlider->setTickInterval(10);

    m_strengthLabel = new QLabel("30", this);
    m_strengthLabel->setMinimumWidth(28);
    m_strengthLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hSlider->addWidget(lblLow);
    hSlider->addWidget(m_strengthSlider);
    hSlider->addWidget(lblHigh);
    hSlider->addWidget(m_strengthLabel);
    vStrength->addLayout(hSlider);

    // Noise auto-estimation
    auto* hAuto = new QHBoxLayout;
    m_autoLambdaCheck = new QCheckBox(tr("Auto-estimate noise level"), this);
    m_autoLambdaCheck->setChecked(true);
    m_noiseEstLabel = new QLabel(tr("σ̂ = —"), this);
    hAuto->addWidget(m_autoLambdaCheck);
    hAuto->addStretch();
    hAuto->addWidget(m_noiseEstLabel);
    vStrength->addLayout(hAuto);

    root->addWidget(grpStrength);

    // ── Advanced parameters ───────────────────────────────────────────────
    auto* grpAdv   = new QGroupBox(tr("Advanced TGV Parameters"), this);
    grpAdv->setCheckable(true);
    grpAdv->setChecked(false);   // collapsed by default
    auto* frmAdv   = new QFormLayout(grpAdv);
    frmAdv->setSpacing(6);

    m_alpha0Spin = new QDoubleSpinBox(this);
    m_alpha0Spin->setRange(0.01, 10.0); m_alpha0Spin->setSingleStep(0.05);
    m_alpha0Spin->setValue(0.5);
    m_alpha0Spin->setToolTip(tr("α₀ — weight for 2nd-order smoothness (symmetrised gradient of v).\n"
                                "Higher → smoother, fewer staircase artefacts."));
    frmAdv->addRow(tr("α₀  (order 0 weight):"), m_alpha0Spin);

    m_alpha1Spin = new QDoubleSpinBox(this);
    m_alpha1Spin->setRange(0.01, 10.0); m_alpha1Spin->setSingleStep(0.05);
    m_alpha1Spin->setValue(1.0);
    m_alpha1Spin->setToolTip(tr("α₁ — weight for 1st-order gradient term.\n"
                                "Higher → stronger edge preservation."));
    frmAdv->addRow(tr("α₁  (order 1 weight):"), m_alpha1Spin);

    m_lambdaSpin = new QDoubleSpinBox(this);
    m_lambdaSpin->setRange(1.0, 10000.0); m_lambdaSpin->setSingleStep(5.0);
    m_lambdaSpin->setValue(100.0);
    m_lambdaSpin->setToolTip(tr("λ — data fidelity weight.\n"
                                "Higher λ → less smoothing (closer to original)."));
    frmAdv->addRow(tr("λ  (data fidelity):"), m_lambdaSpin);

    m_iterSpin = new QSpinBox(this);
    m_iterSpin->setRange(50, 2000); m_iterSpin->setSingleStep(50);
    m_iterSpin->setValue(500);
    frmAdv->addRow(tr("Max iterations:"), m_iterSpin);

    m_tolSpin = new QDoubleSpinBox(this);
    m_tolSpin->setDecimals(7);
    m_tolSpin->setRange(1e-7, 1e-2); m_tolSpin->setSingleStep(1e-6);
    m_tolSpin->setValue(1e-5);
    frmAdv->addRow(tr("Convergence tol:"), m_tolSpin);

    m_perChannelCheck = new QCheckBox(tr("Process each channel independently"), this);
    m_perChannelCheck->setChecked(false);
    m_perChannelCheck->setToolTip(tr("Off: denoise luminance only (recommended to avoid color artifacts on RGB).\n"
                                     "On: denoise R/G/B separately (can be more aggressive but may create chroma blotches)."));
    frmAdv->addRow(m_perChannelCheck);

    root->addWidget(grpAdv);

    // ── Progress / status ─────────────────────────────────────────────────
    m_progressBar  = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_statusLabel  = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_progressBar);
    root->addWidget(m_statusLabel);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* hBtns = new QHBoxLayout;
    m_resetBtn   = new QPushButton(tr("Reset"),   this);
    m_previewBtn = new QPushButton(tr("Preview"), this);
    m_applyBtn   = new QPushButton(tr("Apply"),   this);
    m_closeBtn   = new QPushButton(tr("Close"),   this);
    m_applyBtn->setDefault(true);

    hBtns->addWidget(m_resetBtn);
    hBtns->addStretch();
    hBtns->addWidget(m_previewBtn);
    hBtns->addWidget(m_applyBtn);
    hBtns->addWidget(m_closeBtn);
    root->addLayout(hBtns);
}

// ─── Connect signals ──────────────────────────────────────────────────────────

void TGVDenoiseDialog::connectSignals()
{
    connect(m_strengthSlider, &QSlider::valueChanged, this, [this](int v) {
        m_strengthLabel->setText(QString::number(v));
        // Map strength (1..100) → lambda (2000..5)
        double lam = 2000.0 * std::pow(5.0 / 2000.0, (v - 1) / 99.0);
        m_lambdaSpin->blockSignals(true);
        m_lambdaSpin->setValue(lam);
        m_lambdaSpin->blockSignals(false);
        onParameterChanged();
    });

    connect(m_alpha0Spin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TGVDenoiseDialog::onParameterChanged);
    connect(m_alpha1Spin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TGVDenoiseDialog::onParameterChanged);
    connect(m_lambdaSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TGVDenoiseDialog::onParameterChanged);
    connect(m_autoLambdaCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_lambdaSpin->setEnabled(!on);
        onParameterChanged();
    });

    connect(m_previewBtn, &QPushButton::clicked, this, &TGVDenoiseDialog::onPreview);
    connect(m_applyBtn,   &QPushButton::clicked, this, &TGVDenoiseDialog::onApply);
    connect(m_resetBtn,   &QPushButton::clicked, this, &TGVDenoiseDialog::onReset);
    connect(m_closeBtn,   &QPushButton::clicked, this, &QDialog::close);

    connect(m_watcher, &QFutureWatcher<TGVResult>::finished,
            this, [this]() {
                if (m_previewActive) onPreviewFinished();
                else                 onApplyFinished();
            });
}

// ─── Collect parameters ───────────────────────────────────────────────────────

TGVParams TGVDenoiseDialog::collectParams() const
{
    TGVParams p;
    p.alpha0      = m_alpha0Spin->value();
    p.alpha1      = m_alpha1Spin->value();
    p.lambda      = m_lambdaSpin->value();
    p.maxIter     = m_iterSpin->value();
    p.tol         = m_tolSpin->value();
    p.perChannel  = m_perChannelCheck->isChecked();
    return p;
}

// ─── Auto noise estimation (MAD on wavelet HH sub-band approximation) ─────────

static double estimateNoiseSigma(const ImageBuffer& buf) {
    const int w  = buf.width();
    const int h  = buf.height();
    const int ch = buf.channels();
    const std::vector<float>& data = buf.data();
    // Simple median-of-absolute-differences on horizontal differences
    std::vector<float> diffs;
    diffs.reserve(static_cast<size_t>(w * h / 2));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w-1; x++)
            diffs.push_back(std::fabs(
                data[(static_cast<size_t>(y) * w + (x + 1)) * ch] -
                data[(static_cast<size_t>(y) * w + x) * ch]));
    if (diffs.empty()) return 0.0;
    std::nth_element(diffs.begin(), diffs.begin() + diffs.size()/2, diffs.end());
    return static_cast<double>(diffs[diffs.size()/2]) / 0.6745;
}

// ─── Slot implementations ─────────────────────────────────────────────────────

void TGVDenoiseDialog::onParameterChanged() {
    m_dirty = true;

    // Auto-estimate noise and update lambda
    if (m_autoLambdaCheck->isChecked() && m_viewer && m_viewer->getBuffer().isValid()) {
        double sigma = estimateNoiseSigma(m_viewer->getBuffer());
        if (sigma > 1e-8) {
            // λ ∝ 1/σ²
            double lam = std::clamp(1.0 / (sigma * sigma * 10.0), 1.0, 5000.0);
            m_lambdaSpin->blockSignals(true);
            m_lambdaSpin->setValue(lam);
            m_lambdaSpin->blockSignals(false);
            m_noiseEstLabel->setText(QString("σ̂ = %1").arg(sigma, 0, 'f', 5));
        }
    }
}

void TGVDenoiseDialog::onPreview()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    if (m_watcher->isRunning()) return;

    m_previewActive = true;
    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Computing preview…"));

    // Work on a copy
    auto buf   = std::make_shared<ImageBuffer>(m_viewer->getBuffer());
    auto params = collectParams();
    params.maxIter = std::min(params.maxIter, 150); // fast preview
    params.progressCallback = [this](int pct, const QString& msg) {
        QMetaObject::invokeMethod(this, [this, pct, msg]() {
            m_progressBar->setValue(pct);
            if (!msg.isEmpty()) m_statusLabel->setText(msg);
        }, Qt::QueuedConnection);
    };

    QFuture<TGVResult> fut = QtConcurrent::run([buf, params]() {
        return TGVDenoise::denoiseBuffer(*buf, params);
    });
    // Store shared_ptr so we can retrieve it in finished slot
    m_watcher->setProperty("previewBuf", QVariant::fromValue(static_cast<void*>(buf.get())));
    // We keep buf alive via lambda capture; store pointer on heap
    auto* raw = new std::shared_ptr<ImageBuffer>(buf);
    m_watcher->setProperty("bufPtr", QVariant::fromValue(static_cast<void*>(raw)));
    m_watcher->setFuture(fut);
}

void TGVDenoiseDialog::onPreviewFinished()
{
    m_progressBar->setVisible(false);
    setControlsEnabled(true);

    auto* raw = static_cast<std::shared_ptr<ImageBuffer>*>(
        m_watcher->property("bufPtr").value<void*>());
    if (!raw) return;

    TGVResult res = m_watcher->result();
    if (res.success && m_viewer) {
        m_viewer->setBuffer(**raw, m_viewer->windowTitle(), true);
        m_statusLabel->setText(
            tr("Preview — %1 iterations, gap=%2")
                .arg(res.iterations)
                .arg(res.finalGap, 0, 'e', 2));
    } else {
        m_statusLabel->setText(tr("Preview failed: ") + res.errorMsg);
    }
    delete raw;
    m_previewActive = false;
}

void TGVDenoiseDialog::onApply()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    if (m_watcher->isRunning()) return;

    m_previewActive = false;
    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Applying TGV Denoise…"));

    // Work on the actual buffer (deep-copy for thread safety)
    auto buf    = std::make_shared<ImageBuffer>(m_viewer->getBuffer());
    auto params = collectParams();
    params.progressCallback = [this](int pct, const QString& msg) {
        QMetaObject::invokeMethod(this, [this, pct, msg]() {
            m_progressBar->setValue(pct);
            if (!msg.isEmpty()) m_statusLabel->setText(msg);
        }, Qt::QueuedConnection);
    };

    auto* raw = new std::shared_ptr<ImageBuffer>(buf);
    m_watcher->setProperty("bufPtr", QVariant::fromValue(static_cast<void*>(raw)));

    QFuture<TGVResult> fut = QtConcurrent::run([buf, params]() {
        return TGVDenoise::denoiseBuffer(*buf, params);
    });
    m_watcher->setFuture(fut);
}

void TGVDenoiseDialog::onApplyFinished()
{
    m_progressBar->setVisible(false);
    setControlsEnabled(true);

    auto* raw = static_cast<std::shared_ptr<ImageBuffer>*>(
        m_watcher->property("bufPtr").value<void*>());
    if (!raw) return;

    TGVResult res = m_watcher->result();
    if (res.success && m_viewer) {
        m_viewer->pushUndo();                 // save undo snapshot
        m_viewer->setBuffer(**raw, m_viewer->windowTitle(), true);
        m_viewer->refreshDisplay(true);
        m_statusLabel->setText(
            tr("Done — %1 iterations, gap=%2")
                .arg(res.iterations)
                .arg(res.finalGap, 0, 'e', 2));

        if (m_mainWindow)
            Logger::info(tr("TGV Denoise applied: α₀=%1, α₁=%2, λ=%3, %4 iter")
                .arg(m_alpha0Spin->value()).arg(m_alpha1Spin->value())
                .arg(m_lambdaSpin->value()).arg(res.iterations));

        m_dirty = false;
    } else {
        m_statusLabel->setText(tr("Failed: ") + res.errorMsg);
        QMessageBox::warning(this, tr("TGV Denoise"),
                             tr("Processing failed:\n") + res.errorMsg);
    }
    delete raw;
}

void TGVDenoiseDialog::onReset()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    m_viewer->refreshDisplay(true);
    m_statusLabel->setText(tr("Reset to original."));
    m_dirty = false;
}

void TGVDenoiseDialog::setControlsEnabled(bool en)
{
    m_strengthSlider->setEnabled(en);
    m_autoLambdaCheck->setEnabled(en);
    m_alpha0Spin->setEnabled(en);
    m_alpha1Spin->setEnabled(en);
    m_lambdaSpin->setEnabled(en && !m_autoLambdaCheck->isChecked());
    m_iterSpin->setEnabled(en);
    m_tolSpin->setEnabled(en);
    m_perChannelCheck->setEnabled(en);
    m_previewBtn->setEnabled(en);
    m_applyBtn->setEnabled(en);
    m_resetBtn->setEnabled(en);
}

void TGVDenoiseDialog::closeEvent(QCloseEvent* event)
{
    if (m_watcher->isRunning()) {
        m_watcher->cancel();
        m_watcher->waitForFinished();
    }
    QDialog::closeEvent(event);
}
