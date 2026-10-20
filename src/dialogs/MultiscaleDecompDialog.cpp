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
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

#include "MultiscaleDecompDialog.h"
#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QMessageBox>
#include <QApplication>
#include <QWheelEvent>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

// =============================================================================
// Constructor / Destructor
// =============================================================================

MultiscaleDecompDialog::MultiscaleDecompDialog(QWidget* parent)
    : DialogBase(parent, tr("Multiscale Decomposition"), 1150, 650)
{
    m_mainWindow = dynamic_cast<MainWindowCallbacks*>(parent);
    m_cfgs.resize(m_layers);

    // Debounce preview rebuilds to avoid excessive recomputation during interaction
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(80);
    connect(m_previewTimer, &QTimer::timeout,
            this, &MultiscaleDecompDialog::rebuildPreview);

    buildUI();
}

MultiscaleDecompDialog::~MultiscaleDecompDialog() = default;

// =============================================================================
// Event Filter: Ctrl+Wheel zoom on the preview viewport
// =============================================================================

bool MultiscaleDecompDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (m_view && obj == m_view->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent* we     = static_cast<QWheelEvent*>(event);
        const double factor = (we->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
        m_view->scale(factor, factor);
        return true;
    }
    return DialogBase::eventFilter(obj, event);
}

// =============================================================================
// Public API
// =============================================================================

void MultiscaleDecompDialog::setViewer(ImageViewer* v)
{
    m_viewer = v;
    if (!v || !v->getBuffer().isValid()) return;

    const ImageBuffer& buf = v->getBuffer();
    ImageBuffer::ReadLock lock(&buf);

    m_imgW   = buf.width();
    m_imgH   = buf.height();
    m_origCh = buf.channels();
    m_origMono = (m_origCh == 1);

    const size_t n   = static_cast<size_t>(m_imgW) * m_imgH;
    const float* src = buf.data().data();

    // Internally always work with 3-channel data for consistent display
    m_imgCh = 3;
    m_image.resize(n * 3);

    if (m_origCh == 1) {
        for (size_t i = 0; i < n; ++i) {
            m_image[i * 3 + 0] = src[i];
            m_image[i * 3 + 1] = src[i];
            m_image[i * 3 + 2] = src[i];
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            m_image[i * 3 + 0] = src[i * m_origCh + 0];
            m_image[i * 3 + 1] = src[i * m_origCh + 1];
            m_image[i * 3 + 2] = src[i * m_origCh + 2];
        }
    }

    for (float& v : m_image) v = std::clamp(v, 0.0f, 1.0f);

    recomputeDecomp(true);
    rebuildPreview();

    // Defer fit-to-view until the widget has a valid geometry after show
    QTimer::singleShot(0, this, [this]() {
        if (m_pixBase && !m_pixBase->pixmap().isNull()) {
            m_view->resetTransform();
            m_view->fitInView(m_pixBase, Qt::KeepAspectRatio);
        }
    });
}

// =============================================================================
// UI Construction
// =============================================================================

void MultiscaleDecompDialog::buildUI()
{
    auto* root     = new QHBoxLayout(this);
    auto* splitter = new QSplitter(Qt::Horizontal);
    root->addWidget(splitter);

    // -------------------------------------------------------------------------
    // Left panel: preview with zoom controls
    // -------------------------------------------------------------------------
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);

    m_scene = new QGraphicsScene(this);
    m_view  = new QGraphicsView(m_scene);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->viewport()->installEventFilter(this);

    m_pixBase = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixBase);
    leftLayout->addWidget(m_view);

    auto* zoomRow   = new QHBoxLayout();
    auto* btnZoomOut = new QPushButton("-"); btnZoomOut->setFixedWidth(30);
    auto* btnZoomIn  = new QPushButton("+"); btnZoomIn->setFixedWidth(30);
    auto* btnFit     = new QPushButton(tr("Fit")); btnFit->setFixedWidth(40);
    auto* btn11      = new QPushButton("1:1");     btn11->setFixedWidth(40);

    connect(btnZoomOut, &QPushButton::clicked, this, [this]() { m_view->scale(0.8,  0.8);  });
    connect(btnZoomIn,  &QPushButton::clicked, this, [this]() { m_view->scale(1.25, 1.25); });
    connect(btnFit,     &QPushButton::clicked, this, [this]() {
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

    // -------------------------------------------------------------------------
    // Right panel: global settings, layer table, per-layer editor, actions
    // -------------------------------------------------------------------------
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);

    // Global settings group
    auto* gbGlobal  = new QGroupBox(tr("Global"));
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

    m_comboMode    = new QComboBox();
    m_comboPreview = new QComboBox();
    m_comboMode->addItems({ QStringLiteral("Soft Thresholding"), tr("Linear") });

    formGlobal->addRow(tr("Layers:"),        m_spinLayers);
    formGlobal->addRow(tr("Base sigma:"),    m_spinSigma);
    formGlobal->addRow(m_cbLinkedRGB);
    formGlobal->addRow(tr("Mode:"),          m_comboMode);
    formGlobal->addRow(tr("Layer preview:"), m_comboPreview);
    rightLayout->addWidget(gbGlobal);

    // Layer table
    auto* gbLayers    = new QGroupBox(tr("Layers"));
    auto* layersLayout = new QVBoxLayout(gbLayers);

    m_table = new QTableWidget(0, 8);
    m_table->setHorizontalHeaderLabels({
        tr("On"), tr("Layer"), tr("Scale"),
        tr("Gain"), tr("Threshold"), tr("Amount"),
        tr("NR"), tr("Type")
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layersLayout->addWidget(m_table);
    rightLayout->addWidget(gbLayers, 1);

    // Per-layer editor
    auto* gbEdit  = new QGroupBox(tr("Selected Layer"));
    auto* editForm = new QFormLayout(gbEdit);

    m_lblSel = new QLabel(tr("Layer: ---"));

    m_spinGain = new QDoubleSpinBox();
    m_spinGain->setRange(0.0, 10.0); m_spinGain->setSingleStep(0.05); m_spinGain->setValue(1.0);

    m_spinThr = new QDoubleSpinBox();
    m_spinThr->setRange(0.0, 10.0); m_spinThr->setSingleStep(0.1); m_spinThr->setDecimals(2);

    m_spinAmt = new QDoubleSpinBox();
    m_spinAmt->setRange(0.0, 1.0); m_spinAmt->setSingleStep(0.05);

    m_spinDenoise = new QDoubleSpinBox();
    m_spinDenoise->setRange(0.0, 1.0); m_spinDenoise->setSingleStep(0.05); m_spinDenoise->setValue(0.0);

    m_sliderGain   = new QSlider(Qt::Horizontal); m_sliderGain->setRange(0, 1000);
    m_sliderThr    = new QSlider(Qt::Horizontal); m_sliderThr->setRange(0, 1000);
    m_sliderAmt    = new QSlider(Qt::Horizontal); m_sliderAmt->setRange(0, 100);
    m_sliderDenoise = new QSlider(Qt::Horizontal); m_sliderDenoise->setRange(0, 100);

    editForm->addRow(m_lblSel);

    auto makeRow = [&](QSlider* s, QDoubleSpinBox* sp) -> QHBoxLayout* {
        auto* row = new QHBoxLayout();
        row->addWidget(s); row->addWidget(sp);
        return row;
    };

    editForm->addRow(tr("Gain:"),      makeRow(m_sliderGain,    m_spinGain));
    editForm->addRow(tr("Threshold:"), makeRow(m_sliderThr,     m_spinThr));
    editForm->addRow(tr("Amount:"),    makeRow(m_sliderAmt,     m_spinAmt));
    editForm->addRow(tr("Denoise:"),   makeRow(m_sliderDenoise, m_spinDenoise));
    rightLayout->addWidget(gbEdit);

    // Action buttons
    auto* btnRow = new QHBoxLayout();
    m_btnApply  = new QPushButton(tr("Apply to Image"));
    m_btnNewDoc = new QPushButton(tr("Send to New Image"));
    m_btnClose  = new QPushButton(tr("Close"));
    btnRow->addStretch(1);
    btnRow->addWidget(m_btnClose);
    btnRow->addWidget(m_btnNewDoc);
    btnRow->addWidget(m_btnApply);
    rightLayout->addLayout(btnRow);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    // -------------------------------------------------------------------------
    // Signal connections
    // -------------------------------------------------------------------------
    connect(m_spinLayers, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayersChanged);
    connect(m_spinSigma,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onSigmaChanged);
    connect(m_comboMode,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MultiscaleDecompDialog::onModeChanged);
    connect(m_comboPreview, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MultiscaleDecompDialog::onPreviewComboChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &MultiscaleDecompDialog::onTableSelectionChanged);
    connect(m_table, &QTableWidget::itemChanged,
            this, &MultiscaleDecompDialog::onTableItemChanged);

    connect(m_spinGain,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);
    connect(m_spinThr,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);
    connect(m_spinAmt,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);
    connect(m_spinDenoise, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MultiscaleDecompDialog::onLayerEditorChanged);

    connect(m_sliderGain,    &QSlider::valueChanged, this, &MultiscaleDecompDialog::onGainSliderChanged);
    connect(m_sliderThr,     &QSlider::valueChanged, this, &MultiscaleDecompDialog::onThrSliderChanged);
    connect(m_sliderAmt,     &QSlider::valueChanged, this, &MultiscaleDecompDialog::onAmtSliderChanged);
    connect(m_sliderDenoise, &QSlider::valueChanged, this, &MultiscaleDecompDialog::onDenoiseSliderChanged);

    connect(m_btnApply,  &QPushButton::clicked, this, &MultiscaleDecompDialog::onApplyToImage);
    connect(m_btnNewDoc, &QPushButton::clicked, this, &MultiscaleDecompDialog::onSendToNewImage);
    connect(m_btnClose,  &QPushButton::clicked, this, &QDialog::reject);

    refreshPreviewCombo();
    rebuildTable();
}

// =============================================================================
// Decomposition Engine
// =============================================================================

// Run the multiscale decomposition if parameters have changed (or force == true).
// Caches the result and computes per-layer noise estimates for thresholding.
void MultiscaleDecompDialog::recomputeDecomp(bool force)
{
    if (m_image.empty()) return;

    const int   layers = m_spinLayers->value();
    const float sigma  = static_cast<float>(m_spinSigma->value());

    if (!force &&
        layers == m_cachedLayers &&
        std::abs(sigma - m_cachedSigma) < 1e-6f &&
        !m_cachedDetails.empty())
    {
        return;
    }

    m_layers    = layers;
    m_baseSigma = sigma;

    ChannelOps::multiscaleDecompose(m_image, m_imgW, m_imgH, m_imgCh,
                                    layers, sigma,
                                    m_cachedDetails, m_cachedResidual);

    m_cachedLayers = layers;
    m_cachedSigma  = sigma;

    m_layerNoise.resize(layers);
    for (int i = 0; i < layers; ++i)
        m_layerNoise[i] = ChannelOps::robustSigma(m_cachedDetails[i]);

    syncCfgsAndUI();
}

// Resize the per-layer config vector to match the current layer count,
// preserving existing values, then refresh the table and preview combo.
void MultiscaleDecompDialog::syncCfgsAndUI()
{
    if (static_cast<int>(m_cfgs.size()) != m_layers) {
        auto old = m_cfgs;
        m_cfgs.resize(m_layers);
        const int keep = std::min(static_cast<int>(old.size()), m_layers);
        for (int i = 0; i < keep; ++i) m_cfgs[i] = old[i];
    }
    rebuildTable();
    refreshPreviewCombo();
}

// Apply per-layer operations to a copy of the cached detail layers.
void MultiscaleDecompDialog::buildTunedLayers(std::vector<std::vector<float>>& tuned,
                                               std::vector<float>& residual)
{
    recomputeDecomp(false);
    if (m_cachedDetails.empty()) return;

    const int mode = (m_comboMode->currentIndex() == 0) ? 0 : 1;

    tuned.resize(m_cachedDetails.size());
    for (int i = 0; i < static_cast<int>(m_cachedDetails.size()); ++i) {
        tuned[i] = m_cachedDetails[i];
        ChannelOps::applyLayerOps(tuned[i], m_cfgs[i], m_layerNoise[i], i, mode);
    }

    residual = m_cachedResidual;
}

// =============================================================================
// Preview
// =============================================================================

void MultiscaleDecompDialog::schedulePreview()
{
    m_previewTimer->start(80);
}

void MultiscaleDecompDialog::rebuildPreview()
{
    if (m_image.empty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::vector<std::vector<float>> tuned;
    std::vector<float>              residual;
    buildTunedLayers(tuned, residual);

    if (tuned.empty()) {
        QApplication::restoreOverrideCursor();
        return;
    }

    const QString      selData = m_comboPreview->currentData().toString();
    const size_t       total   = static_cast<size_t>(m_imgW) * m_imgH * m_imgCh;
    std::vector<float> display;

    if (selData == "residual") {
        display = residual;
        for (float& v : display) v = std::clamp(v, 0.0f, 1.0f);

    } else if (selData == "final" || selData.isEmpty()) {
        std::vector<float> res = m_residualEnabled
            ? residual
            : std::vector<float>(total, 0.0f);

        display = ChannelOps::multiscaleReconstruct(tuned, res, static_cast<int>(total));

        if (!m_residualEnabled) {
            // Visualise detail-only layers as mid-grey offset
            for (float& v : display) v = std::clamp(0.5f + v * 4.0f, 0.0f, 1.0f);
        } else {
            for (float& v : display) v = std::clamp(v, 0.0f, 1.0f);
        }
    } else {
        // Individual detail layer selected
        bool ok;
        const int idx = selData.toInt(&ok);
        if (ok && idx >= 0 && idx < static_cast<int>(tuned.size())) {
            display = tuned[idx];
            for (float& v : display) v = std::clamp(0.5f + v * 4.0f, 0.0f, 1.0f);
        }
    }

    if (!display.empty()) {
        m_pixBase->setPixmap(floatToPixmap(display, m_imgW, m_imgH, m_imgCh));
        m_scene->setSceneRect(0, 0, m_imgW, m_imgH);
    }

    QApplication::restoreOverrideCursor();
}

// Convert a float32 image buffer to a QPixmap suitable for display.
QPixmap MultiscaleDecompDialog::floatToPixmap(const std::vector<float>& img,
                                               int w, int h, int ch)
{
    QImage qimg(w, h, QImage::Format_RGB888);

    for (int y = 0; y < h; ++y) {
        uchar* scanline = qimg.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * ch;
            if (ch >= 3) {
                scanline[x * 3 + 0] = static_cast<uchar>(std::clamp(static_cast<int>(img[idx + 0] * 255.0f), 0, 255));
                scanline[x * 3 + 1] = static_cast<uchar>(std::clamp(static_cast<int>(img[idx + 1] * 255.0f), 0, 255));
                scanline[x * 3 + 2] = static_cast<uchar>(std::clamp(static_cast<int>(img[idx + 2] * 255.0f), 0, 255));
            } else {
                const uchar v = static_cast<uchar>(std::clamp(static_cast<int>(img[idx] * 255.0f), 0, 255));
                scanline[x * 3 + 0] = v;
                scanline[x * 3 + 1] = v;
                scanline[x * 3 + 2] = v;
            }
        }
    }

    return QPixmap::fromImage(qimg);
}

// =============================================================================
// Table Management
// =============================================================================

// Rebuild the layer table from the current configuration.
// The last row always represents the residual layer.
void MultiscaleDecompDialog::rebuildTable()
{
    m_table->blockSignals(true);
    m_table->setRowCount(m_layers + 1);

    for (int i = 0; i < m_layers; ++i) {
        const auto& cfg = m_cfgs[i];

        auto* itemOn = new QTableWidgetItem("");
        itemOn->setFlags(itemOn->flags() | Qt::ItemIsUserCheckable);
        itemOn->setCheckState(cfg.enabled ? Qt::Checked : Qt::Unchecked);

        m_table->setItem(i, 0, itemOn);
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(i + 1)));
        m_table->setItem(i, 2, new QTableWidgetItem(
            QString::number(m_baseSigma * std::pow(2.0f, static_cast<float>(i)), 'f', 2)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(cfg.biasGain, 'f', 2)));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(cfg.thr,      'f', 2)));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(cfg.amount,   'f', 2)));
        m_table->setItem(i, 6, new QTableWidgetItem(QString::number(cfg.denoise,  'f', 2)));
        m_table->setItem(i, 7, new QTableWidgetItem("D"));
    }

    // Residual row
    const int r    = m_layers;
    auto* itemOnR  = new QTableWidgetItem("");
    itemOnR->setFlags(itemOnR->flags() | Qt::ItemIsUserCheckable);
    itemOnR->setCheckState(m_residualEnabled ? Qt::Checked : Qt::Unchecked);

    m_table->setItem(r, 0, itemOnR);
    m_table->setItem(r, 1, new QTableWidgetItem("R"));
    m_table->setItem(r, 2, new QTableWidgetItem("---"));
    m_table->setItem(r, 3, new QTableWidgetItem("1.00"));
    m_table->setItem(r, 4, new QTableWidgetItem("0.00"));
    m_table->setItem(r, 5, new QTableWidgetItem("0.00"));
    m_table->setItem(r, 6, new QTableWidgetItem("0.00"));
    m_table->setItem(r, 7, new QTableWidgetItem("R"));

    m_table->blockSignals(false);

    if (m_layers > 0 && m_table->selectedItems().isEmpty()) {
        m_table->selectRow(0);
        loadLayerIntoEditor(0);
    }
}

void MultiscaleDecompDialog::refreshPreviewCombo()
{
    m_comboPreview->blockSignals(true);
    m_comboPreview->clear();
    m_comboPreview->addItem(tr("Final"),       "final");
    m_comboPreview->addItem(tr("R (Residual)"), "residual");
    for (int i = 0; i < m_layers; ++i)
        m_comboPreview->addItem(tr("Detail Layer %1").arg(i + 1), QString::number(i));
    m_comboPreview->blockSignals(false);
}

// Populate the per-layer editor with values from the selected layer config.
// Disables all editor controls when the residual row is selected.
void MultiscaleDecompDialog::loadLayerIntoEditor(int idx)
{
    m_selectedLayer = idx;

    const bool isResidual = (idx == m_layers);

    m_spinGain->setEnabled(!isResidual);
    m_spinThr->setEnabled(!isResidual);
    m_spinAmt->setEnabled(!isResidual);
    m_spinDenoise->setEnabled(!isResidual);
    m_sliderGain->setEnabled(!isResidual);
    m_sliderThr->setEnabled(!isResidual);
    m_sliderAmt->setEnabled(!isResidual);
    m_sliderDenoise->setEnabled(!isResidual);

    if (isResidual) {
        m_lblSel->setText(tr("Layer: R (Residual)"));
        return;
    }

    m_lblSel->setText(tr("Layer: %1 / %2").arg(idx + 1).arg(m_layers));

    const auto& cfg = m_cfgs[idx];

    // Block all editor signals during sync to prevent recursive updates
    for (QObject* w : QObjectList{ m_spinGain, m_spinThr, m_spinAmt, m_spinDenoise,
                                   m_sliderGain, m_sliderThr, m_sliderAmt, m_sliderDenoise })
        w->blockSignals(true);

    m_spinGain->setValue(cfg.biasGain);
    m_spinThr->setValue(cfg.thr);
    m_spinAmt->setValue(cfg.amount);
    m_spinDenoise->setValue(cfg.denoise);

    m_sliderGain->setValue(static_cast<int>(cfg.biasGain * 100.0));
    m_sliderThr->setValue(static_cast<int>(cfg.thr       * 100.0));
    m_sliderAmt->setValue(static_cast<int>(cfg.amount    * 100.0));
    m_sliderDenoise->setValue(static_cast<int>(cfg.denoise * 100.0));

    for (QObject* w : QObjectList{ m_spinGain, m_spinThr, m_spinAmt, m_spinDenoise,
                                   m_sliderGain, m_sliderThr, m_sliderAmt, m_sliderDenoise })
        w->blockSignals(false);

    updateParamWidgetsForMode();
}

// In linear mode, threshold, amount, and denoise have no effect; disable them.
void MultiscaleDecompDialog::updateParamWidgetsForMode()
{
    if (m_selectedLayer < 0 || m_selectedLayer >= m_layers) return;

    const bool linear = (m_comboMode->currentText() == tr("Linear"));
    m_spinThr->setEnabled(!linear);
    m_sliderThr->setEnabled(!linear);
    m_spinAmt->setEnabled(!linear);
    m_sliderAmt->setEnabled(!linear);
    m_spinDenoise->setEnabled(!linear);
    m_sliderDenoise->setEnabled(!linear);
}

// =============================================================================
// Slots
// =============================================================================

void MultiscaleDecompDialog::onLayersChanged(int val)
{
    m_layers = val;
    recomputeDecomp(true);
    schedulePreview();
}

void MultiscaleDecompDialog::onSigmaChanged(double val)
{
    m_baseSigma = static_cast<float>(val);
    recomputeDecomp(true);
    schedulePreview();
}

void MultiscaleDecompDialog::onModeChanged(int)
{
    updateParamWidgetsForMode();
    schedulePreview();
}

void MultiscaleDecompDialog::onPreviewComboChanged(int)
{
    schedulePreview();
}

void MultiscaleDecompDialog::onTableSelectionChanged()
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty()) return;
    loadLayerIntoEditor(items.first()->row());
}

void MultiscaleDecompDialog::onTableItemChanged(QTableWidgetItem* item)
{
    const int r = item->row();
    const int c = item->column();

    // Handle the residual row toggle separately
    if (r == m_layers) {
        if (c == 0) {
            m_residualEnabled = (item->checkState() == Qt::Checked);
            schedulePreview();
        }
        return;
    }

    if (r < 0 || r >= static_cast<int>(m_cfgs.size())) return;

    auto& cfg = m_cfgs[r];

    if (c == 0) {
        cfg.enabled = (item->checkState() == Qt::Checked);
        schedulePreview();
        return;
    }

    bool ok;
    const float val = item->text().toFloat(&ok);
    if (!ok) return;

    switch (c) {
        case 3: cfg.biasGain = val; break;
        case 4: cfg.thr      = val; break;
        case 5: cfg.amount   = val; break;
        case 6: cfg.denoise  = val; break;
        default: return;
    }

    if (m_selectedLayer == r) loadLayerIntoEditor(r);
    schedulePreview();
}

// Propagate editor spin-box values to the config and sync the table.
void MultiscaleDecompDialog::onLayerEditorChanged()
{
    if (m_selectedLayer < 0 || m_selectedLayer >= static_cast<int>(m_cfgs.size())) return;

    auto& cfg    = m_cfgs[m_selectedLayer];
    cfg.biasGain = static_cast<float>(m_spinGain->value());
    cfg.thr      = static_cast<float>(m_spinThr->value());
    cfg.amount   = static_cast<float>(m_spinAmt->value());
    cfg.denoise  = static_cast<float>(m_spinDenoise->value());

    m_table->blockSignals(true);
    auto setCell = [&](int col, float v) {
        if (m_table->item(m_selectedLayer, col))
            m_table->item(m_selectedLayer, col)->setText(QString::number(v, 'f', 2));
    };
    setCell(3, cfg.biasGain);
    setCell(4, cfg.thr);
    setCell(5, cfg.amount);
    setCell(6, cfg.denoise);
    m_table->blockSignals(false);

    schedulePreview();
}

// Each slider-changed slot converts the integer slider value to float,
// silently updates the corresponding spin-box, then triggers an editor change.
void MultiscaleDecompDialog::onGainSliderChanged(int v)
{
    m_spinGain->blockSignals(true);
    m_spinGain->setValue(v / 100.0f);
    m_spinGain->blockSignals(false);
    onLayerEditorChanged();
}

void MultiscaleDecompDialog::onThrSliderChanged(int v)
{
    m_spinThr->blockSignals(true);
    m_spinThr->setValue(v / 100.0f);
    m_spinThr->blockSignals(false);
    onLayerEditorChanged();
}

void MultiscaleDecompDialog::onAmtSliderChanged(int v)
{
    m_spinAmt->blockSignals(true);
    m_spinAmt->setValue(v / 100.0f);
    m_spinAmt->blockSignals(false);
    onLayerEditorChanged();
}

void MultiscaleDecompDialog::onDenoiseSliderChanged(int v)
{
    m_spinDenoise->blockSignals(true);
    m_spinDenoise->setValue(v / 100.0f);
    m_spinDenoise->blockSignals(false);
    onLayerEditorChanged();
}

// =============================================================================
// Actions
// =============================================================================

// Build the reconstructed image and overwrite the active viewer buffer.
void MultiscaleDecompDialog::onApplyToImage()
{
    if (!m_viewer || m_image.empty()) {
        QMessageBox::warning(this, tr("Multiscale Decomposition"), tr("No image loaded."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    const ImageBuffer origBuf = m_viewer->getBuffer();

    std::vector<std::vector<float>> tuned;
    std::vector<float>              residual;
    buildTunedLayers(tuned, residual);

    if (tuned.empty() || residual.empty()) {
        QApplication::restoreOverrideCursor();
        return;
    }

    const size_t total = static_cast<size_t>(m_imgW) * m_imgH * m_imgCh;
    std::vector<float> res = m_residualEnabled ? residual : std::vector<float>(total, 0.0f);
    std::vector<float> outRaw = ChannelOps::multiscaleReconstruct(tuned, res, static_cast<int>(total));

    std::vector<float> out(total);
    if (!m_residualEnabled) {
        for (size_t i = 0; i < total; ++i)
            out[i] = std::clamp(0.5f + outRaw[i] * 4.0f, 0.0f, 1.0f);
    } else {
        for (size_t i = 0; i < total; ++i)
            out[i] = std::clamp(outRaw[i], 0.0f, 1.0f);
    }

    // Convert back to the original channel count
    const size_t n = static_cast<size_t>(m_imgW) * m_imgH;
    ImageBuffer result;
    if (m_origMono) {
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; ++i) mono[i] = out[i * 3];
        result.setData(m_imgW, m_imgH, 1, mono);
    } else {
        result.setData(m_imgW, m_imgH, 3, out);
    }

    result.setMetadata(m_viewer->getBuffer().metadata());

    if (origBuf.hasMask()) {
        result.setMask(*origBuf.getMask());
        result.blendResult(origBuf);
    }

    if (m_mainWindow) {
        m_mainWindow->startLongProcess();
        m_viewer->pushUndo(tr("Multiscale Decomposition"));
        m_viewer->setBuffer(result);
        m_mainWindow->endLongProcess();
        m_mainWindow->logMessage(tr("Multiscale Decomposition applied."), 1);
    }

    QApplication::restoreOverrideCursor();
    accept();
}

// Build the reconstructed image and push it as a new viewer window.
void MultiscaleDecompDialog::onSendToNewImage()
{
    if (m_image.empty()) {
        QMessageBox::warning(this, tr("Multiscale Decomposition"), tr("No image loaded."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::vector<std::vector<float>> tuned;
    std::vector<float>              residual;
    buildTunedLayers(tuned, residual);

    if (tuned.empty() || residual.empty()) {
        QApplication::restoreOverrideCursor();
        return;
    }

    const size_t total = static_cast<size_t>(m_imgW) * m_imgH * m_imgCh;
    std::vector<float> res = m_residualEnabled ? residual : std::vector<float>(total, 0.0f);
    std::vector<float> outRaw = ChannelOps::multiscaleReconstruct(tuned, res, static_cast<int>(total));

    std::vector<float> out(total);
    if (!m_residualEnabled) {
        for (size_t i = 0; i < total; ++i)
            out[i] = std::clamp(0.5f + outRaw[i] * 4.0f, 0.0f, 1.0f);
    } else {
        for (size_t i = 0; i < total; ++i)
            out[i] = std::clamp(outRaw[i], 0.0f, 1.0f);
    }

    const size_t n = static_cast<size_t>(m_imgW) * m_imgH;
    ImageBuffer result;
    if (m_origMono) {
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; ++i) mono[i] = out[i * 3];
        result.setData(m_imgW, m_imgH, 1, mono);
    } else {
        result.setData(m_imgW, m_imgH, 3, out);
    }

    if (m_viewer) result.setMetadata(m_viewer->getBuffer().metadata());

    if (m_mainWindow) {
        m_mainWindow->createResultWindow(result, tr("Multiscale Result"));
        m_mainWindow->logMessage(tr("Multiscale Decomposition result created."), 1);
    }

    QApplication::restoreOverrideCursor();
}