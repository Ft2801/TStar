#include "MultiscaleDecompDialog.h"
#include "../ImageViewer.h"
#include "../MainWindowCallbacks.h"
#include <QMessageBox>
#include <QApplication>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

MultiscaleDecompDialog::MultiscaleDecompDialog(QWidget* parent)
    : DialogBase(parent, tr("Multiscale Decomposition"))
{
    m_cfgs.resize(m_layers);
    
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(80);
    connect(m_previewTimer, &QTimer::timeout, this, &MultiscaleDecompDialog::rebuildPreview);

    buildUI();
}

MultiscaleDecompDialog::~MultiscaleDecompDialog() = default;

// ============================================================================
// Public API
// ============================================================================

void MultiscaleDecompDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
    if (!v || !v->getBuffer().isValid()) return;

    const ImageBuffer& buf = v->getBuffer();
    ImageBuffer::ReadLock lock(&buf);

    m_imgW = buf.width();
    m_imgH = buf.height();
    m_origCh = buf.channels();
    m_origMono = (m_origCh == 1);

    // Always work with 3ch internally for display
    size_t n = (size_t)m_imgW * m_imgH;
    const float* src = buf.data().data();

    if (m_origCh == 1) {
        m_imgCh = 3;
        m_image.resize(n * 3);
        for (size_t i = 0; i < n; i++) {
            m_image[i * 3 + 0] = src[i];
            m_image[i * 3 + 1] = src[i];
            m_image[i * 3 + 2] = src[i];
        }
    } else if (m_origCh >= 3) {
        m_imgCh = 3;
        m_image.resize(n * 3);
        for (size_t i = 0; i < n; i++) {
            m_image[i * 3 + 0] = src[i * m_origCh + 0];
            m_image[i * 3 + 1] = src[i * m_origCh + 1];
            m_image[i * 3 + 2] = src[i * m_origCh + 2];
        }
    }

    // Clamp to [0,1]
    for (float& v : m_image) v = std::clamp(v, 0.0f, 1.0f);

    // Initial decomposition
    recomputeDecomp(true);
    rebuildPreview();

    // Fit view
    if (m_pixBase && !m_pixBase->pixmap().isNull()) {
        m_view->resetTransform();
        m_view->fitInView(m_pixBase, Qt::KeepAspectRatio);
    }
}

// ============================================================================
// UI Construction
// ============================================================================

void MultiscaleDecompDialog::buildUI() {
    auto* root = new QHBoxLayout(this);

    auto* splitter = new QSplitter(Qt::Horizontal);
    root->addWidget(splitter);

    // ----- LEFT: Preview -----
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);

    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setAlignment(Qt::AlignCenter);

    m_pixBase = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixBase);

    leftLayout->addWidget(m_view);

    // Zoom buttons
    auto* zoomRow = new QHBoxLayout();
    auto* btnZoomOut = new QPushButton("-");
    auto* btnZoomIn = new QPushButton("+");
    auto* btnFit = new QPushButton(tr("Fit"));
    auto* btn11 = new QPushButton("1:1");

    btnZoomOut->setFixedWidth(30);
    btnZoomIn->setFixedWidth(30);
    btnFit->setFixedWidth(40);
    btn11->setFixedWidth(40);

    connect(btnZoomOut, &QPushButton::clicked, this, [this]() { m_view->scale(0.8, 0.8); });
    connect(btnZoomIn, &QPushButton::clicked, this, [this]() { m_view->scale(1.25, 1.25); });
    connect(btnFit, &QPushButton::clicked, this, [this]() {
        if (m_pixBase && !m_pixBase->pixmap().isNull()) {
            m_view->resetTransform();
            m_view->fitInView(m_pixBase, Qt::KeepAspectRatio);
        }
    });
    connect(btn11, &QPushButton::clicked, this, [this]() { m_view->resetTransform(); });

    zoomRow->addStretch(1);
    zoomRow->addWidget(btnZoomOut);
    zoomRow->addWidget(btnZoomIn);
    zoomRow->addSpacing(10);
    zoomRow->addWidget(btnFit);
    zoomRow->addWidget(btn11);
    zoomRow->addStretch(1);
    leftLayout->addLayout(zoomRow);

    // ----- RIGHT: Controls -----
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);

    // Global settings
    auto* gbGlobal = new QGroupBox(tr("Global"));
    auto* formGlobal = new QFormLayout(gbGlobal);

    m_spinLayers = new QSpinBox();
    m_spinLayers->setRange(1, 10);
    m_spinLayers->setValue(m_layers);

    m_spinSigma = new QDoubleSpinBox();
    m_spinSigma->setRange(0.3, 5.0);
    m_spinSigma->setSingleStep(0.1);
    m_spinSigma->setValue(m_baseSigma);

    m_cbLinkedRGB = new QCheckBox(tr("Linked RGB"));
    m_cbLinkedRGB->setChecked(true);

    m_comboMode = new QComboBox();
    m_comboMode->addItems({QStringLiteral("μ-σ Thresholding"), tr("Linear")});

    m_comboPreview = new QComboBox();

    formGlobal->addRow(tr("Layers:"), m_spinLayers);
    formGlobal->addRow(tr("Base sigma:"), m_spinSigma);
    formGlobal->addRow(m_cbLinkedRGB);
    formGlobal->addRow(tr("Mode:"), m_comboMode);
    formGlobal->addRow(tr("Layer preview:"), m_comboPreview);

    rightLayout->addWidget(gbGlobal);

    // Layers table
    auto* gbLayers = new QGroupBox(tr("Layers"));
    auto* layersLayout = new QVBoxLayout(gbLayers);

    m_table = new QTableWidget(0, 8);
    m_table->setHorizontalHeaderLabels({tr("On"), tr("Layer"), tr("Scale"),
                                         tr("Gain"), tr("Thr (σ)"), tr("Amt"),
                                         tr("NR"), tr("Type")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    layersLayout->addWidget(m_table);
    rightLayout->addWidget(gbLayers, 1);

    // Per-layer editor
    auto* gbEdit = new QGroupBox(tr("Selected Layer"));
    auto* editForm = new QFormLayout(gbEdit);

    m_lblSel = new QLabel(tr("Layer: —"));

    m_spinGain = new QDoubleSpinBox();
    m_spinGain->setRange(0.0, 10.0);
    m_spinGain->setSingleStep(0.05);
    m_spinGain->setValue(1.0);

    m_spinThr = new QDoubleSpinBox();
    m_spinThr->setRange(0.0, 10.0);
    m_spinThr->setSingleStep(0.1);
    m_spinThr->setDecimals(2);

    m_spinAmt = new QDoubleSpinBox();
    m_spinAmt->setRange(0.0, 1.0);
    m_spinAmt->setSingleStep(0.05);

    m_spinDenoise = new QDoubleSpinBox();
    m_spinDenoise->setRange(0.0, 1.0);
    m_spinDenoise->setSingleStep(0.05);
    m_spinDenoise->setValue(0.0);

    m_sliderGain = new QSlider(Qt::Horizontal);
    m_sliderGain->setRange(0, 1000);

    m_sliderThr = new QSlider(Qt::Horizontal);
    m_sliderThr->setRange(0, 1000);

    m_sliderAmt = new QSlider(Qt::Horizontal);
    m_sliderAmt->setRange(0, 100);

    m_sliderDenoise = new QSlider(Qt::Horizontal);
    m_sliderDenoise->setRange(0, 100);

    editForm->addRow(m_lblSel);

    auto* gainRow = new QHBoxLayout();
    gainRow->addWidget(m_sliderGain);
    gainRow->addWidget(m_spinGain);
    editForm->addRow(tr("Gain:"), gainRow);

    auto* thrRow = new QHBoxLayout();
    thrRow->addWidget(m_sliderThr);
    thrRow->addWidget(m_spinThr);
    editForm->addRow(tr("Threshold (σ):"), thrRow);

    auto* amtRow = new QHBoxLayout();
    amtRow->addWidget(m_sliderAmt);
    amtRow->addWidget(m_spinAmt);
    editForm->addRow(tr("Amount:"), amtRow);

    auto* dnRow = new QHBoxLayout();
    dnRow->addWidget(m_sliderDenoise);
    dnRow->addWidget(m_spinDenoise);
    editForm->addRow(tr("Denoise:"), dnRow);

    rightLayout->addWidget(gbEdit);

    // Buttons
    auto* btnRow = new QHBoxLayout();
    m_btnApply = new QPushButton(tr("Apply to Image"));
    m_btnNewDoc = new QPushButton(tr("Send to New Image"));
    m_btnSplit = new QPushButton(tr("Split Layers"));
    m_btnClose = new QPushButton(tr("Close"));

    btnRow->addStretch(1);
    btnRow->addWidget(m_btnApply);
    btnRow->addWidget(m_btnNewDoc);
    btnRow->addWidget(m_btnSplit);
    btnRow->addWidget(m_btnClose);
    rightLayout->addLayout(btnRow);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    // --- Connections ---
    connect(m_spinLayers, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayersChanged);
    connect(m_spinSigma, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onSigmaChanged);
    connect(m_comboMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MultiscaleDecompDialog::onModeChanged);
    connect(m_comboPreview, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MultiscaleDecompDialog::onPreviewComboChanged);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &MultiscaleDecompDialog::onTableSelectionChanged);
    connect(m_table, &QTableWidget::itemChanged,
            this, &MultiscaleDecompDialog::onTableItemChanged);

    connect(m_spinGain, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);
    connect(m_spinThr, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);
    connect(m_spinAmt, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);
    connect(m_spinDenoise, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);

    connect(m_sliderGain, &QSlider::valueChanged,
            this, &MultiscaleDecompDialog::onGainSliderChanged);
    connect(m_sliderThr, &QSlider::valueChanged,
            this, &MultiscaleDecompDialog::onThrSliderChanged);
    connect(m_sliderAmt, &QSlider::valueChanged,
            this, &MultiscaleDecompDialog::onAmtSliderChanged);
    connect(m_sliderDenoise, &QSlider::valueChanged,
            this, &MultiscaleDecompDialog::onDenoiseSliderChanged);

    connect(m_btnApply, &QPushButton::clicked, this, &MultiscaleDecompDialog::onApplyToImage);
    connect(m_btnNewDoc, &QPushButton::clicked, this, &MultiscaleDecompDialog::onSendToNewImage);
    connect(m_btnSplit, &QPushButton::clicked, this, &MultiscaleDecompDialog::onSplitLayers);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::reject);

    // Initial
    refreshPreviewCombo();
    rebuildTable();
}

// ============================================================================
// Decomposition Engine
// ============================================================================

void MultiscaleDecompDialog::recomputeDecomp(bool force) {
    if (m_image.empty()) return;

    int layers = m_spinLayers->value();
    float sigma = (float)m_spinSigma->value();

    if (!force && layers == m_cachedLayers && std::abs(sigma - m_cachedSigma) < 1e-6f
        && !m_cachedDetails.empty()) {
        return;
    }

    m_layers = layers;
    m_baseSigma = sigma;

    ChannelOps::multiscaleDecompose(m_image, m_imgW, m_imgH, m_imgCh,
                                    layers, sigma,
                                    m_cachedDetails, m_cachedResidual);

    m_cachedLayers = layers;
    m_cachedSigma = sigma;

    // Compute noise per layer
    m_layerNoise.resize(layers);
    for (int i = 0; i < layers; i++) {
        m_layerNoise[i] = ChannelOps::robustSigma(m_cachedDetails[i]);
    }

    syncCfgsAndUI();
}

void MultiscaleDecompDialog::syncCfgsAndUI() {
    if ((int)m_cfgs.size() != m_layers) {
        auto old = m_cfgs;
        m_cfgs.resize(m_layers);
        for (int i = 0; i < std::min((int)old.size(), m_layers); i++)
            m_cfgs[i] = old[i];
    }
    rebuildTable();
    refreshPreviewCombo();
}

void MultiscaleDecompDialog::buildTunedLayers(
    std::vector<std::vector<float>>& tuned,
    std::vector<float>& residual) {

    recomputeDecomp(false);
    if (m_cachedDetails.empty()) return;

    int mode = (m_comboMode->currentIndex() == 0) ? 0 : 1;

    tuned.resize(m_cachedDetails.size());
    for (int i = 0; i < (int)m_cachedDetails.size(); i++) {
        tuned[i] = m_cachedDetails[i]; // copy
        ChannelOps::applyLayerOps(tuned[i], m_cfgs[i],
                                   m_layerNoise[i], i, mode);
    }
    residual = m_cachedResidual;
}

// ============================================================================
// Preview
// ============================================================================

void MultiscaleDecompDialog::schedulePreview() {
    m_previewTimer->start(80);
}

void MultiscaleDecompDialog::rebuildPreview() {
    if (m_image.empty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::vector<std::vector<float>> tuned;
    std::vector<float> residual;
    buildTunedLayers(tuned, residual);

    if (tuned.empty()) {
        QApplication::restoreOverrideCursor();
        return;
    }

    // Determine what to show
    QString selData = m_comboPreview->currentData().toString();

    std::vector<float> display;
    size_t total = (size_t)m_imgW * m_imgH * m_imgCh;

    if (selData == "residual") {
        display = residual;
        for (float& v : display) v = std::clamp(v, 0.0f, 1.0f);
    } else if (selData == "final" || selData.isEmpty()) {
        std::vector<float> res = m_residualEnabled ? residual : std::vector<float>(total, 0.0f);
        display = ChannelOps::multiscaleReconstruct(tuned, res, (int)total);

        if (!m_residualEnabled) {
            // Detail-only: mid-gray visualization
            for (float& v : display) v = std::clamp(0.5f + v * 4.0f, 0.0f, 1.0f);
        } else {
            for (float& v : display) v = std::clamp(v, 0.0f, 1.0f);
        }
    } else {
        // Individual layer
        bool ok;
        int layerIdx = selData.toInt(&ok);
        if (ok && layerIdx >= 0 && layerIdx < (int)tuned.size()) {
            display = tuned[layerIdx];
            // Visualize: 0.5 + detail*4
            for (float& v : display) v = std::clamp(0.5f + v * 4.0f, 0.0f, 1.0f);
        }
    }

    if (!display.empty()) {
        QPixmap pm = floatToPixmap(display, m_imgW, m_imgH, m_imgCh);
        m_pixBase->setPixmap(pm);
        m_scene->setSceneRect(0, 0, m_imgW, m_imgH);
    }

    QApplication::restoreOverrideCursor();
}

QPixmap MultiscaleDecompDialog::floatToPixmap(const std::vector<float>& img,
                                               int w, int h, int ch) {
    QImage qimg(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; y++) {
        uchar* scanline = qimg.scanLine(y);
        for (int x = 0; x < w; x++) {
            size_t idx = ((size_t)y * w + x) * ch;
            if (ch >= 3) {
                scanline[x * 3 + 0] = (uchar)std::clamp((int)(img[idx + 0] * 255.0f), 0, 255);
                scanline[x * 3 + 1] = (uchar)std::clamp((int)(img[idx + 1] * 255.0f), 0, 255);
                scanline[x * 3 + 2] = (uchar)std::clamp((int)(img[idx + 2] * 255.0f), 0, 255);
            } else {
                uchar v = (uchar)std::clamp((int)(img[idx] * 255.0f), 0, 255);
                scanline[x * 3 + 0] = v;
                scanline[x * 3 + 1] = v;
                scanline[x * 3 + 2] = v;
            }
        }
    }
    return QPixmap::fromImage(qimg);
}

// ============================================================================
// Table Management
// ============================================================================

void MultiscaleDecompDialog::rebuildTable() {
    m_table->blockSignals(true);

    m_table->setRowCount(m_layers + 1); // +1 for residual

    for (int i = 0; i < m_layers; i++) {
        auto& cfg = m_cfgs[i];

        auto* itemOn = new QTableWidgetItem("");
        itemOn->setFlags(itemOn->flags() | Qt::ItemIsUserCheckable);
        itemOn->setCheckState(cfg.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, 0, itemOn);

        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(i + 1)));
        m_table->setItem(i, 2, new QTableWidgetItem(
            QString::number(m_baseSigma * std::pow(2.0f, (float)i), 'f', 2)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(cfg.biasGain, 'f', 2)));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(cfg.thr, 'f', 2)));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(cfg.amount, 'f', 2)));
        m_table->setItem(i, 6, new QTableWidgetItem(QString::number(cfg.denoise, 'f', 2)));
        m_table->setItem(i, 7, new QTableWidgetItem("D"));
    }

    // Residual row
    int r = m_layers;
    auto* itemOn = new QTableWidgetItem("");
    itemOn->setFlags(itemOn->flags() | Qt::ItemIsUserCheckable);
    itemOn->setCheckState(m_residualEnabled ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(r, 0, itemOn);
    m_table->setItem(r, 1, new QTableWidgetItem("R"));
    m_table->setItem(r, 2, new QTableWidgetItem(QStringLiteral("—")));
    m_table->setItem(r, 3, new QTableWidgetItem("1.00"));
    m_table->setItem(r, 4, new QTableWidgetItem("0.00"));
    m_table->setItem(r, 5, new QTableWidgetItem("0.00"));
    m_table->setItem(r, 6, new QTableWidgetItem("0.00"));
    m_table->setItem(r, 7, new QTableWidgetItem("R"));

    m_table->blockSignals(false);

    if (m_layers > 0 && !m_table->selectedItems().size()) {
        m_table->selectRow(0);
        loadLayerIntoEditor(0);
    }
}

void MultiscaleDecompDialog::refreshPreviewCombo() {
    m_comboPreview->blockSignals(true);
    m_comboPreview->clear();
    m_comboPreview->addItem(tr("Final"), "final");
    m_comboPreview->addItem(tr("R (Residual)"), "residual");
    for (int i = 0; i < m_layers; i++) {
        m_comboPreview->addItem(tr("Detail Layer %1").arg(i + 1), QString::number(i));
    }
    m_comboPreview->blockSignals(false);
}

void MultiscaleDecompDialog::loadLayerIntoEditor(int idx) {
    m_selectedLayer = idx;

    if (idx == m_layers) {
        // Residual
        m_lblSel->setText(tr("Layer: R (Residual)"));
        m_spinGain->setEnabled(false);
        m_spinThr->setEnabled(false);
        m_spinAmt->setEnabled(false);
        m_spinDenoise->setEnabled(false);
        m_sliderGain->setEnabled(false);
        m_sliderThr->setEnabled(false);
        m_sliderAmt->setEnabled(false);
        m_sliderDenoise->setEnabled(false);
        return;
    }

    m_spinGain->setEnabled(true);
    m_spinThr->setEnabled(true);
    m_spinAmt->setEnabled(true);
    m_spinDenoise->setEnabled(true);
    m_sliderGain->setEnabled(true);
    m_sliderThr->setEnabled(true);
    m_sliderAmt->setEnabled(true);
    m_sliderDenoise->setEnabled(true);

    auto& cfg = m_cfgs[idx];
    m_lblSel->setText(tr("Layer: %1 / %2").arg(idx + 1).arg(m_layers));

    // Block signals during sync
    m_spinGain->blockSignals(true);
    m_spinThr->blockSignals(true);
    m_spinAmt->blockSignals(true);
    m_spinDenoise->blockSignals(true);
    m_sliderGain->blockSignals(true);
    m_sliderThr->blockSignals(true);
    m_sliderAmt->blockSignals(true);
    m_sliderDenoise->blockSignals(true);

    m_spinGain->setValue(cfg.biasGain);
    m_spinThr->setValue(cfg.thr);
    m_spinAmt->setValue(cfg.amount);
    m_spinDenoise->setValue(cfg.denoise);

    m_sliderGain->setValue((int)(cfg.biasGain * 100.0));
    m_sliderThr->setValue((int)(cfg.thr * 100.0));
    m_sliderAmt->setValue((int)(cfg.amount * 100.0));
    m_sliderDenoise->setValue((int)(cfg.denoise * 100.0));

    m_spinGain->blockSignals(false);
    m_spinThr->blockSignals(false);
    m_spinAmt->blockSignals(false);
    m_spinDenoise->blockSignals(false);
    m_sliderGain->blockSignals(false);
    m_sliderThr->blockSignals(false);
    m_sliderAmt->blockSignals(false);
    m_sliderDenoise->blockSignals(false);

    updateParamWidgetsForMode();
}

void MultiscaleDecompDialog::updateParamWidgetsForMode() {
    bool linear = (m_comboMode->currentText() == tr("Linear"));
    // Only threshold/amount/denoise are disabled in linear mode
    m_spinThr->setEnabled(!linear);
    m_sliderThr->setEnabled(!linear);
    m_spinAmt->setEnabled(!linear);
    m_sliderAmt->setEnabled(!linear);
    m_spinDenoise->setEnabled(!linear);
    m_sliderDenoise->setEnabled(!linear);
}

// ============================================================================
// Slots
// ============================================================================

void MultiscaleDecompDialog::onLayersChanged(int val) {
    m_layers = val;
    recomputeDecomp(true);
    schedulePreview();
}

void MultiscaleDecompDialog::onSigmaChanged(double val) {
    m_baseSigma = (float)val;
    recomputeDecomp(true);
    schedulePreview();
}

void MultiscaleDecompDialog::onModeChanged(int) {
    updateParamWidgetsForMode();
    schedulePreview();
}

void MultiscaleDecompDialog::onPreviewComboChanged(int) {
    schedulePreview();
}

void MultiscaleDecompDialog::onTableSelectionChanged() {
    auto items = m_table->selectedItems();
    if (items.isEmpty()) return;
    int row = items.first()->row();
    loadLayerIntoEditor(row);
}

void MultiscaleDecompDialog::onTableItemChanged(QTableWidgetItem* item) {
    int r = item->row();
    int c = item->column();

    // Residual row
    if (r == m_layers) {
        if (c == 0) {
            m_residualEnabled = (item->checkState() == Qt::Checked);
            schedulePreview();
        }
        return;
    }

    if (r < 0 || r >= (int)m_cfgs.size()) return;
    auto& cfg = m_cfgs[r];

    if (c == 0) {
        cfg.enabled = (item->checkState() == Qt::Checked);
        schedulePreview();
        return;
    }

    bool ok;
    float val = item->text().toFloat(&ok);
    if (!ok) return;

    switch (c) {
        case 3: cfg.biasGain = val; break;
        case 4: cfg.thr = val; break;
        case 5: cfg.amount = val; break;
        case 6: cfg.denoise = val; break;
        default: return;
    }

    if (m_selectedLayer == r)
        loadLayerIntoEditor(r);
    schedulePreview();
}

void MultiscaleDecompDialog::onLayerEditorChanged() {
    if (m_selectedLayer < 0 || m_selectedLayer >= (int)m_cfgs.size()) return;

    auto& cfg = m_cfgs[m_selectedLayer];
    cfg.biasGain = (float)m_spinGain->value();
    cfg.thr = (float)m_spinThr->value();
    cfg.amount = (float)m_spinAmt->value();
    cfg.denoise = (float)m_spinDenoise->value();

    // Sync table
    m_table->blockSignals(true);
    if (m_table->item(m_selectedLayer, 3))
        m_table->item(m_selectedLayer, 3)->setText(QString::number(cfg.biasGain, 'f', 2));
    if (m_table->item(m_selectedLayer, 4))
        m_table->item(m_selectedLayer, 4)->setText(QString::number(cfg.thr, 'f', 2));
    if (m_table->item(m_selectedLayer, 5))
        m_table->item(m_selectedLayer, 5)->setText(QString::number(cfg.amount, 'f', 2));
    if (m_table->item(m_selectedLayer, 6))
        m_table->item(m_selectedLayer, 6)->setText(QString::number(cfg.denoise, 'f', 2));
    m_table->blockSignals(false);

    schedulePreview();
}

void MultiscaleDecompDialog::onGainSliderChanged(int v) {
    float val = v / 100.0f;
    m_spinGain->blockSignals(true);
    m_spinGain->setValue(val);
    m_spinGain->blockSignals(false);
    onLayerEditorChanged();
}

void MultiscaleDecompDialog::onThrSliderChanged(int v) {
    float val = v / 100.0f;
    m_spinThr->blockSignals(true);
    m_spinThr->setValue(val);
    m_spinThr->blockSignals(false);
    onLayerEditorChanged();
}

void MultiscaleDecompDialog::onAmtSliderChanged(int v) {
    float val = v / 100.0f;
    m_spinAmt->blockSignals(true);
    m_spinAmt->setValue(val);
    m_spinAmt->blockSignals(false);
    onLayerEditorChanged();
}

void MultiscaleDecompDialog::onDenoiseSliderChanged(int v) {
    float val = v / 100.0f;
    m_spinDenoise->blockSignals(true);
    m_spinDenoise->setValue(val);
    m_spinDenoise->blockSignals(false);
    onLayerEditorChanged();
}

// ============================================================================
// Actions
// ============================================================================

void MultiscaleDecompDialog::onApplyToImage() {
    if (!m_viewer || m_image.empty()) {
        QMessageBox::warning(this, tr("Multiscale Decomposition"),
                             tr("No image loaded."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::vector<std::vector<float>> tuned;
    std::vector<float> residual;
    buildTunedLayers(tuned, residual);

    size_t total = (size_t)m_imgW * m_imgH * m_imgCh;
    std::vector<float> res = m_residualEnabled ? residual : std::vector<float>(total, 0.0f);
    std::vector<float> outRaw = ChannelOps::multiscaleReconstruct(tuned, res, (int)total);

    std::vector<float> out;
    if (!m_residualEnabled) {
        out.resize(total);
        for (size_t i = 0; i < total; i++)
            out[i] = std::clamp(0.5f + outRaw[i] * 4.0f, 0.0f, 1.0f);
    } else {
        out.resize(total);
        for (size_t i = 0; i < total; i++)
            out[i] = std::clamp(outRaw[i], 0.0f, 1.0f);
    }

    // Convert back to original channel count
    size_t n = (size_t)m_imgW * m_imgH;
    ImageBuffer result;
    if (m_origMono) {
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; i++) mono[i] = out[i * 3]; // R channel
        result.setData(m_imgW, m_imgH, 1, mono);
    } else {
        result.setData(m_imgW, m_imgH, 3, out);
    }
    result.setMetadata(m_viewer->getBuffer().metadata());

    // Apply to viewer
    auto* cb = dynamic_cast<MainWindowCallbacks*>(parentWidget());
    if (cb) {
        cb->startLongProcess();
        m_viewer->setBuffer(result);
        cb->endLongProcess();
    }

    QApplication::restoreOverrideCursor();
    accept();
}

void MultiscaleDecompDialog::onSendToNewImage() {
    if (m_image.empty()) {
        QMessageBox::warning(this, tr("Multiscale Decomposition"),
                             tr("No image loaded."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::vector<std::vector<float>> tuned;
    std::vector<float> residual;
    buildTunedLayers(tuned, residual);

    size_t total = (size_t)m_imgW * m_imgH * m_imgCh;
    std::vector<float> res = m_residualEnabled ? residual : std::vector<float>(total, 0.0f);
    std::vector<float> outRaw = ChannelOps::multiscaleReconstruct(tuned, res, (int)total);

    std::vector<float> out(total);
    if (!m_residualEnabled) {
        for (size_t i = 0; i < total; i++)
            out[i] = std::clamp(0.5f + outRaw[i] * 4.0f, 0.0f, 1.0f);
    } else {
        for (size_t i = 0; i < total; i++)
            out[i] = std::clamp(outRaw[i], 0.0f, 1.0f);
    }

    size_t n = (size_t)m_imgW * m_imgH;
    ImageBuffer result;
    if (m_origMono) {
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; i++) mono[i] = out[i * 3];
        result.setData(m_imgW, m_imgH, 1, mono);
    } else {
        result.setData(m_imgW, m_imgH, 3, out);
    }

    if (m_viewer)
        result.setMetadata(m_viewer->getBuffer().metadata());

    auto* cb = dynamic_cast<MainWindowCallbacks*>(parentWidget());
    if (cb) {
        cb->createResultWindow(result, tr("Multiscale Result"));
    }

    QApplication::restoreOverrideCursor();
}

void MultiscaleDecompDialog::onSplitLayers() {
    if (m_image.empty()) {
        QMessageBox::warning(this, tr("Multiscale Decomposition"),
                             tr("No image loaded."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::vector<std::vector<float>> tuned;
    std::vector<float> residual;
    buildTunedLayers(tuned, residual);

    auto* cb = dynamic_cast<MainWindowCallbacks*>(parentWidget());
    if (!cb) {
        QApplication::restoreOverrideCursor();
        return;
    }

    size_t n = (size_t)m_imgW * m_imgH;

    // Detail layers
    for (int i = 0; i < (int)tuned.size(); i++) {
        auto& d = tuned[i];
        size_t total = d.size();
        std::vector<float> vis(total);
        for (size_t j = 0; j < total; j++)
            vis[j] = std::clamp(0.5f + d[j] * 4.0f, 0.0f, 1.0f);

        ImageBuffer buf;
        if (m_origMono) {
            std::vector<float> mono(n);
            for (size_t j = 0; j < n; j++) mono[j] = vis[j * 3];
            buf.setData(m_imgW, m_imgH, 1, mono);
        } else {
            buf.setData(m_imgW, m_imgH, 3, vis);
        }
        cb->createResultWindow(buf, tr("Multiscale Detail %1").arg(i + 1));
    }

    // Residual
    {
        std::vector<float> res(residual.size());
        for (size_t j = 0; j < residual.size(); j++)
            res[j] = std::clamp(residual[j], 0.0f, 1.0f);

        ImageBuffer buf;
        if (m_origMono) {
            std::vector<float> mono(n);
            for (size_t j = 0; j < n; j++) mono[j] = res[j * 3];
            buf.setData(m_imgW, m_imgH, 1, mono);
        } else {
            buf.setData(m_imgW, m_imgH, 3, res);
        }
        cb->createResultWindow(buf, tr("Multiscale Residual"));
    }

    QApplication::restoreOverrideCursor();
}
