#include "HistogramStretchDialog.h"
#include "ImageViewer.h"
#include "widgets/HistogramWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

static constexpr float AS_DEFAULT_TARGET_BACKGROUND = 0.25f;

namespace {
double histogramZoomScaleFromLevel(int level) {
    const int clamped = std::clamp(level, 1, 100);
    const double t = static_cast<double>(clamped - 1) / 99.0;
    return std::exp(std::log(100.0) * t);
}

QString histogramZoomLabelFromLevel(int level) {
    return QString::number(histogramZoomScaleFromLevel(level), 'f', 2) + "x";
}
}

HistogramStretchDialog::HistogramStretchDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("Histogram Transformation"), 700, 600), m_viewer(nullptr), m_applied(false)
{
    setMinimumWidth(500);
    setMinimumHeight(480);
    setupUI();
    setViewer(viewer);
}


HistogramStretchDialog::~HistogramStretchDialog() {
    // Always clean up preview and restore backup
    if (m_viewer) {
        m_viewer->clearPreviewLUT();
        if (m_backup.isValid()) {
            m_viewer->setBuffer(m_backup, m_viewer->windowTitle(), true);
        }
        m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
        m_viewer->refreshDisplay();
    }
}

void HistogramStretchDialog::reject() {
    // Always clean up preview and restore backup
    if (m_viewer) {
        m_viewer->clearPreviewLUT();
        if (m_backup.isValid()) {
            m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
            m_viewer->setBuffer(m_backup, m_viewer->windowTitle(), true);
        }
    }
    QDialog::reject();
}

void HistogramStretchDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    
    // Clear old viewer
    if (m_viewer) {
        m_viewer->clearPreviewLUT();
    }
    
    m_viewer = v;
    m_applied = false;
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_backup = m_viewer->getBuffer();
        m_originalDisplayMode = m_viewer->getDisplayMode();
        m_originalDisplayLinked = m_viewer->isDisplayLinked();
        if (m_histogram) {
             m_baseHist = m_backup.computeHistogram(65536);
             m_histogram->setData(m_baseHist, m_backup.channels());
             updateClippingStats(m_backup);
        }
        
        if (m_previewCheck->isChecked()) updatePreview();
    }
}

void HistogramStretchDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    
    // --- Histogram Toolbar (Zoom) ---
    QHBoxLayout* histoToolbar = new QHBoxLayout();
    histoToolbar->addWidget(new QLabel(tr("Zoom:")));
    
    m_zoomOutBtn = new QToolButton();
    m_zoomOutBtn->setText("-");
    m_zoomOutBtn->setFixedWidth(24);
    m_zoomOutBtn->setAutoRepeat(true);  // Hold button to keep zooming
    m_zoomOutBtn->setAutoRepeatDelay(300);  // Start repeat after 300ms
    m_zoomOutBtn->setAutoRepeatInterval(50);  // Repeat every 50ms
    histoToolbar->addWidget(m_zoomOutBtn);
    
    m_zoomLabel = new QLabel(histogramZoomLabelFromLevel(m_zoomLevel));
    m_zoomLabel->setMinimumWidth(35);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    histoToolbar->addWidget(m_zoomLabel);
    
    m_zoomInBtn = new QToolButton();
    m_zoomInBtn->setText("+");
    m_zoomInBtn->setFixedWidth(24);
    m_zoomInBtn->setAutoRepeat(true);  // Hold button to keep zooming
    m_zoomInBtn->setAutoRepeatDelay(300);  // Start repeat after 300ms
    m_zoomInBtn->setAutoRepeatInterval(50);  // Repeat every 50ms
    histoToolbar->addWidget(m_zoomInBtn);
    
    m_zoomResetBtn = new QToolButton();
    m_zoomResetBtn->setText("1:1");
    histoToolbar->addWidget(m_zoomResetBtn);
    
    histoToolbar->addStretch();
    mainLayout->addLayout(histoToolbar);

    // --- Histogram Scroll Area ---
    #include <QScrollArea>
    #include <QScrollBar>
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: #1a1a1a; }");

    m_histogram = new HistogramWidget();
    m_histogram->setMinimumHeight(150);
    if (m_baseHist.empty() && m_backup.isValid()) {
         m_baseHist = m_backup.computeHistogram(65536);
    }
    m_histogram->setData(m_baseHist, m_backup.channels());
    
    m_scrollArea->setWidget(m_histogram);
    mainLayout->addWidget(m_scrollArea, 1);

    // Separate Horizontal ScrollBar below the scroll area
    m_histScrollBar = new QScrollBar(Qt::Horizontal);
    m_histScrollBar->setVisible(false);
    m_histScrollBar->setFixedHeight(10);
    m_histScrollBar->setStyleSheet(
        "QScrollBar:horizontal { border: none; background: #2b2b2b; height: 10px; margin: 0px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal:hover { background: #666; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
    );
    mainLayout->addWidget(m_histScrollBar);
    
    // --- Channel Toggles (Curves-style) ---
    QHBoxLayout* channelLayout = new QHBoxLayout();
    
    auto createToggle = [&](const QString& text, const QString& col) {
        QToolButton* btn = new QToolButton();
        btn->setText(text);
        btn->setCheckable(true);
        btn->setChecked(true);
        btn->setFixedSize(30, 30);
        btn->setStyleSheet(QString("QToolButton:checked { background-color: %1; color: black; font-weight: bold; }").arg(col));
        return btn;
    };
    
    m_redBtn = createToggle("R", "#ff0000");
    m_greenBtn = createToggle("G", "#00ff00");
    m_blueBtn = createToggle("B", "#0000ff");
    
    channelLayout->addWidget(m_redBtn);
    channelLayout->addWidget(m_greenBtn);
    channelLayout->addWidget(m_blueBtn);
    channelLayout->addStretch();
    
    // Autostretch button
    m_autoStretchBtn = new QPushButton(tr("Auto Stretch"));
    m_autoStretchBtn->setToolTip(tr("Compute optimal stretch parameters from image statistics"));
    channelLayout->addWidget(m_autoStretchBtn);
    mainLayout->addLayout(channelLayout);
    
    // --- Clipping Stats ---
    QHBoxLayout* clipLayout = new QHBoxLayout();
    m_lowClipLabel = new QLabel("Low: 0.00%");
    m_highClipLabel = new QLabel("High: 0.00%");
    m_lowClipLabel->setStyleSheet("color: #ff8888; margin-right: 10px;");
    m_highClipLabel->setStyleSheet("color: #8888ff;");
    clipLayout->addWidget(m_lowClipLabel);
    clipLayout->addWidget(m_highClipLabel);
    clipLayout->addStretch();
    mainLayout->addLayout(clipLayout);
    
    // --- Parameters with Sliders ---
    QGroupBox* paramsBox = new QGroupBox(tr("Parameters"));
    QVBoxLayout* paramsLayout = new QVBoxLayout(paramsBox);
    
    auto makeSliderRow = [&](const QString& label, QSlider*& slider, QDoubleSpinBox*& spin, 
                             double min, double max, double val, int decimals) {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 10000);
        double t = (val - min) / (max - min);
        slider->setValue(static_cast<int>(t * 10000));
        row->addWidget(slider);
        
        spin = new QDoubleSpinBox();
        spin->setRange(min, max);
        spin->setValue(val);
        spin->setDecimals(decimals);
        spin->setSingleStep(0.001);
        spin->setMinimumWidth(100);
        row->addWidget(spin);
        
        paramsLayout->addLayout(row);
    };
    
    makeSliderRow(tr("Shadows:"), m_shadowsSlider, m_shadowsSpin, 0.0, 1.0, 0.0, 7);
    makeSliderRow(tr("Midtones:"), m_midtonesSlider, m_midtonesSpin, 0.0001, 0.9999, 0.5, 7);
    makeSliderRow(tr("Highlights:"), m_highlightsSlider, m_highlightsSpin, 0.0, 1.0, 1.0, 7);
    
    mainLayout->addWidget(paramsBox);
    
    // --- Preview ---
    m_previewCheck = new QCheckBox(tr("Preview"));
    m_previewCheck->setChecked(true);
    mainLayout->addWidget(m_previewCheck);
    
    // --- Buttons ---
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* resetBtn = new QPushButton(tr("Reset"));
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    applyBtn->setDefault(true);
    buttonLayout->addWidget(resetBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(applyBtn);
    mainLayout->addLayout(buttonLayout);
    
    // --- Connections ---
    // Shadows slider: real-time histogram, clipping stats (no preview to avoid lag)
    connect(m_shadowsSlider, &QSlider::valueChanged, [this](int val){
        double dval = val / 10000.0;
        m_shadowsSpin->blockSignals(true);
        m_shadowsSpin->setValue(dval);
        m_shadowsSpin->blockSignals(false);
        m_shadows = static_cast<float>(dval);
        updateHistogramOnly();  // Real-time histogram
        updateClippingStatsOnly();  // Live clipping stats, no preview
    });
    connect(m_shadowsSlider, &QSlider::sliderReleased, this, &HistogramStretchDialog::onSliderReleased);
    connect(m_shadowsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HistogramStretchDialog::onShadowsChanged);
    
    // Midtones slider
    connect(m_midtonesSlider, &QSlider::valueChanged, [this](int val){
        double dval = 0.0001 + (val / 10000.0) * 0.9998;
        m_midtonesSpin->blockSignals(true);
        m_midtonesSpin->setValue(dval);
        m_midtonesSpin->blockSignals(false);
        m_midtones = static_cast<float>(dval);
        updateHistogramOnly();  // Real-time histogram
        updateClippingStatsOnly();  // Live clipping stats, no preview
    });
    connect(m_midtonesSlider, &QSlider::sliderReleased, this, &HistogramStretchDialog::onSliderReleased);
    connect(m_midtonesSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HistogramStretchDialog::onMidtonesChanged);
    
    // Highlights slider
    connect(m_highlightsSlider, &QSlider::valueChanged, [this](int val){
        double dval = val / 10000.0;
        m_highlightsSpin->blockSignals(true);
        m_highlightsSpin->setValue(dval);
        m_highlightsSpin->blockSignals(false);
        m_highlights = static_cast<float>(dval);
        updateHistogramOnly();  // Real-time histogram
        updateClippingStatsOnly();  // Live clipping stats, no preview
    });
    connect(m_highlightsSlider, &QSlider::sliderReleased, this, &HistogramStretchDialog::onSliderReleased);
    connect(m_highlightsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HistogramStretchDialog::onHighlightsChanged);
    
    connect(m_redBtn, &QToolButton::toggled, this, &HistogramStretchDialog::onChannelToggled);
    connect(m_greenBtn, &QToolButton::toggled, this, &HistogramStretchDialog::onChannelToggled);
    connect(m_blueBtn, &QToolButton::toggled, this, &HistogramStretchDialog::onChannelToggled);
    
    connect(m_previewCheck, &QCheckBox::toggled, this, &HistogramStretchDialog::onPreviewToggled);
    connect(m_autoStretchBtn, &QPushButton::clicked, this, &HistogramStretchDialog::onAutoStretch);
    
    connect(resetBtn, &QPushButton::clicked, this, &HistogramStretchDialog::onReset);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &HistogramStretchDialog::onApply);

    // Zoom connections
    connect(m_zoomInBtn, &QToolButton::clicked, [this](){
        if (m_zoomLevel < 100) { m_zoomLevel++; m_zoomLabel->setText(histogramZoomLabelFromLevel(m_zoomLevel)); onZoomChanged(); }
    });
    connect(m_zoomOutBtn, &QToolButton::clicked, [this](){
        if (m_zoomLevel > 1) { m_zoomLevel--; m_zoomLabel->setText(histogramZoomLabelFromLevel(m_zoomLevel)); onZoomChanged(); }
    });
    connect(m_zoomResetBtn, &QToolButton::clicked, [this](){
        m_zoomLevel = 1; m_zoomLabel->setText(histogramZoomLabelFromLevel(m_zoomLevel)); onZoomChanged();
    });

    // Sync scrollbar
    connect(m_histScrollBar, &QScrollBar::valueChanged, [this](int v){
        m_scrollArea->horizontalScrollBar()->setValue(v);
    });
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::rangeChanged, [this](int min, int max){
        m_histScrollBar->setRange(min, max);
    });
}

void HistogramStretchDialog::onShadowsChanged() {
    m_shadows = static_cast<float>(m_shadowsSpin->value());
    int sval = static_cast<int>(m_shadows * 10000);
    m_shadowsSlider->blockSignals(true);
    m_shadowsSlider->setValue(sval);
    m_shadowsSlider->blockSignals(false);
    updateHistogramOnly();
    if (m_previewCheck->isChecked()) updatePreview();
}

void HistogramStretchDialog::onMidtonesChanged() {
    m_midtones = static_cast<float>(m_midtonesSpin->value());
    double t = (m_midtones - 0.0001) / 0.9998;
    int sval = static_cast<int>(t * 10000);
    m_midtonesSlider->blockSignals(true);
    m_midtonesSlider->setValue(sval);
    m_midtonesSlider->blockSignals(false);
    updateHistogramOnly();
    if (m_previewCheck->isChecked()) updatePreview();
}

void HistogramStretchDialog::onHighlightsChanged() {
    m_highlights = static_cast<float>(m_highlightsSpin->value());
    int sval = static_cast<int>(m_highlights * 10000);
    m_highlightsSlider->blockSignals(true);
    m_highlightsSlider->setValue(sval);
    m_highlightsSlider->blockSignals(false);
    updateHistogramOnly();
    if (m_previewCheck->isChecked()) updatePreview();
}

void HistogramStretchDialog::onSliderValueChanged() {
    updateHistogramOnly();
}

void HistogramStretchDialog::onSliderReleased() {
    if (m_previewCheck->isChecked()) updatePreview();
}

void HistogramStretchDialog::onChannelToggled() {
    m_doRed = m_redBtn->isChecked();
    m_doGreen = m_greenBtn->isChecked();
    m_doBlue = m_blueBtn->isChecked();
    updateHistogramOnly();
    if (m_previewCheck->isChecked()) updatePreview();
}

void HistogramStretchDialog::onPreviewToggled(bool checked) {
    if (checked) {
        updatePreview();
    } else {
        if (m_viewer) {
            m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
            m_viewer->setBuffer(m_backup, m_viewer->windowTitle(), true);
        }
        m_lowClipLabel->setText(tr("Low: 0.00%"));
        m_highClipLabel->setText(tr("High: 0.00%"));
    }
}

void HistogramStretchDialog::onAutoStretch() {
    if (!m_backup.isValid()) return;
    
    computeAutostretch(m_backup, m_shadows, m_midtones, m_highlights);
    
    // Update UI
    m_shadowsSpin->blockSignals(true);
    m_midtonesSpin->blockSignals(true);
    m_highlightsSpin->blockSignals(true);
    m_shadowsSlider->blockSignals(true);
    m_midtonesSlider->blockSignals(true);
    m_highlightsSlider->blockSignals(true);
    
    m_shadowsSpin->setValue(m_shadows);
    m_midtonesSpin->setValue(m_midtones);
    m_highlightsSpin->setValue(m_highlights);
    m_shadowsSlider->setValue(static_cast<int>(m_shadows * 10000));
    m_midtonesSlider->setValue(static_cast<int>((m_midtones - 0.0001) / 0.9998 * 10000));
    m_highlightsSlider->setValue(static_cast<int>(m_highlights * 10000));
    
    m_shadowsSpin->blockSignals(false);
    m_midtonesSpin->blockSignals(false);
    m_highlightsSpin->blockSignals(false);
    m_shadowsSlider->blockSignals(false);
    m_midtonesSlider->blockSignals(false);
    m_highlightsSlider->blockSignals(false);
    
    updateHistogramOnly();
    if (m_previewCheck->isChecked()) updatePreview();
}

void HistogramStretchDialog::onReset() {
    m_shadows = 0.0f;
    m_midtones = 0.5f;
    m_highlights = 1.0f;
    
    m_shadowsSpin->setValue(0.0);
    m_midtonesSpin->setValue(0.5);
    m_highlightsSpin->setValue(1.0);
    m_shadowsSlider->setValue(0);
    m_midtonesSlider->setValue(5000);
    m_highlightsSlider->setValue(10000);
    
    m_redBtn->setChecked(true);
    m_greenBtn->setChecked(true);
    m_blueBtn->setChecked(true);
    
    if (m_viewer) {
        m_viewer->setDisplayState(m_originalDisplayMode, m_originalDisplayLinked);
        m_viewer->setBuffer(m_backup, m_viewer->windowTitle(), true);
    }
    if (m_histogram && m_backup.isValid()) {
        auto bins = m_backup.computeHistogram(65536);
        m_histogram->setData(bins, m_backup.channels());
    }
    m_lowClipLabel->setText(tr("Low: 0.00%"));
    m_highClipLabel->setText(tr("High: 0.00%"));
}

void HistogramStretchDialog::onApply() {
    if (m_viewer) {
        // Clear preview LUT immediately to prevent double-application
        m_viewer->clearPreviewLUT();

        // 1. Restore to backup (clean state)
        m_viewer->setBuffer(m_backup, m_viewer->windowTitle(), true);

        // 2. Push Undo de backup
        m_viewer->pushUndo();

        // 3. Apply MTF
        ImageBuffer buf = m_backup;
        applyMTF(buf, m_shadows, m_midtones, m_highlights, m_doRed, m_doGreen, m_doBlue);
        // Switch to Linear BEFORE setBuffer so refreshDisplay shows the stretched
        // data as-is, without a second auto-stretch pass on top of the MTF result.
        m_viewer->setDisplayState(ImageBuffer::Display_Linear, m_originalDisplayLinked);
        m_viewer->setBuffer(buf, m_viewer->windowTitle(), true);
        
        m_applied = true;
        
        // --- Persistence: Keep dialog open for continued adjustments ---
        // Update backup to the newly applied result so subsequent cancels/closes revert to THIS state
        // Also update the saved display mode: after apply the image is stretched, so Linear is now correct.
        m_originalDisplayMode = ImageBuffer::Display_Linear;
        m_backup = buf;
        // We do NOT set m_applied = true because we want destructor/reject to ALWAYS restore m_backup
        // (which now holds the applied state). This handles cases where user applies, then changes sliders, then cancels.
        // m_applied = true; 
        
        // Reset sliders to neutral position
        m_shadows = 0.0f;
        m_midtones = 0.5f;
        m_highlights = 1.0f;
        
        m_shadowsSpin->blockSignals(true);
        m_midtonesSpin->blockSignals(true);
        m_highlightsSpin->blockSignals(true);
        m_shadowsSlider->blockSignals(true);
        m_midtonesSlider->blockSignals(true);
        m_highlightsSlider->blockSignals(true);
        
        m_shadowsSpin->setValue(0.0);
        m_midtonesSpin->setValue(0.5);
        m_highlightsSpin->setValue(1.0);
        m_shadowsSlider->setValue(0);
        m_midtonesSlider->setValue(5000);
        m_highlightsSlider->setValue(10000);
        
        m_shadowsSpin->blockSignals(false);
        m_midtonesSpin->blockSignals(false);
        m_highlightsSpin->blockSignals(false);
        m_shadowsSlider->blockSignals(false);
        m_midtonesSlider->blockSignals(false);
        m_highlightsSlider->blockSignals(false);
        
        // Recompute histogram for the new baseline
        m_baseHist = m_backup.computeHistogram(65536);
        if (m_histogram) {
            m_histogram->setData(m_baseHist, m_backup.channels());
        }
        updateClippingStats(m_backup);
        
        emit applied();
    }
    // Don't call accept() - keep dialog open for continued adjustments
}


void HistogramStretchDialog::updatePreview() {
    if (!m_viewer || !m_previewCheck->isChecked()) return;

    // Apply MTF directly to a float copy of the backup buffer and show it in Linear
    // mode. This matches exactly what onApply() does, so preview == apply result.
    // Using a 65536-entry LUT was previously used here, but that approach only has
    // ~65 effective levels for linear astronomical images (values in [0, 0.001]),
    // causing visible banding/posterization. Direct float computation avoids this.
    m_viewer->clearPreviewLUT();
    ImageBuffer temp = m_backup;
    applyMTF(temp, m_shadows, m_midtones, m_highlights, m_doRed, m_doGreen, m_doBlue);
    m_viewer->setDisplayState(ImageBuffer::Display_Linear, m_originalDisplayLinked);
    m_viewer->setBuffer(temp, m_viewer->windowTitle(), true);
}

void HistogramStretchDialog::updateHistogramOnly() {
    if (!m_histogram || !m_backup.isValid() || m_baseHist.empty()) return;
    
    // Use cached base histogram (Never scan image pixels here!)
    int channels = m_backup.channels();
    const auto& origBins = m_baseHist;
    int numBins = origBins[0].size();
    
    // Build MTF LUT
    std::vector<float> lut(numBins);
    for (int i = 0; i < numBins; ++i) {
        float x = static_cast<float>(i) / (numBins - 1);
        lut[i] = MTF(x, m_midtones, m_shadows, m_highlights);
    }
    
    // Transform histogram bins
    std::vector<std::vector<int>> transformedBins(channels, std::vector<int>(numBins, 0));
    bool doChannel[3] = { m_doRed, m_doGreen, m_doBlue };
    
    for (int c = 0; c < channels && c < 3; ++c) {
        if (!doChannel[c]) {
            // Keep original histogram for disabled channels
            transformedBins[c] = origBins[c];
            continue;
        }
        
        for (int i = 0; i < numBins; ++i) {
            int count = origBins[c][i];
            if (count == 0) continue;
            
            float out = lut[i];
            int outBin = static_cast<int>(out * (numBins - 1) + 0.5f);
            if (outBin < 0) outBin = 0;
            if (outBin > numBins - 1) outBin = numBins - 1;
            
            transformedBins[c][outBin] += count;
        }
    }
    
    m_histogram->setData(transformedBins, channels);
    
    // Compute clipping stats from transformed bins (approximate but fast)
    long lowClip = 0;
    long highClip = 0;
    long totalPixels = 0;
    
    for (int c = 0; c < channels && c < 3; ++c) {
        if (!doChannel[c]) continue;
        
        // Sum total pixels based on original bins (reliable count)
        for (int count : origBins[c]) totalPixels += count;
        
        // Sum clipped pixels from transformed bins
        // In high-res histogram, bin 0 is low clip, last bin is high clip
        lowClip += transformedBins[c][0];
        highClip += transformedBins[c][numBins - 1];
    }
    
    // Normalize total if we summed multiple channels
    // Actually totalPixels above is sum of all channel pixels
    // We want percentage relative to total pixels considered
    
    if (totalPixels > 0) {
        float lowPct = (100.0f * lowClip) / totalPixels;
        float highPct = (100.0f * highClip) / totalPixels;
        m_lowClipLabel->setText(tr("Low: %1%").arg(lowPct, 0, 'f', 4));
        m_highClipLabel->setText(tr("High: %1%").arg(highPct, 0, 'f', 4));
    }
}

void HistogramStretchDialog::updateClippingStats(const ImageBuffer& buffer) {
    long lowClip = 0, highClip = 0;
    buffer.computeClippingStats(lowClip, highClip);
    long total = static_cast<long>(buffer.width()) * buffer.height() * buffer.channels();
    
    if (total > 0) {
        float lowPct = (100.0f * lowClip) / total;
        float highPct = (100.0f * highClip) / total;
        m_lowClipLabel->setText(tr("Low: %1%").arg(lowPct, 0, 'f', 4));
        m_highClipLabel->setText(tr("High: %1%").arg(highPct, 0, 'f', 4));
    }
}

void HistogramStretchDialog::updateClippingStatsOnly() {
    // Calculate clipping stats WITHOUT updating preview (to avoid lag during drag)
    if (!m_backup.isValid()) return;
    
    // Create temporary buffer to compute clipping
    ImageBuffer temp = m_backup;
    applyMTF(temp, m_shadows, m_midtones, m_highlights, m_doRed, m_doGreen, m_doBlue);
    
    // Update labels only
    updateClippingStats(temp);
}

float HistogramStretchDialog::MTF(float x, float m, float lo, float hi) {
    if (x <= lo) return 0.0f;
    if (x >= hi) return 1.0f;
    
    float xp = (x - lo) / (hi - lo);
    return ((m - 1.0f) * xp) / (((2.0f * m - 1.0f) * xp) - m);
}

void HistogramStretchDialog::applyMTF(ImageBuffer& buffer, float shadows, float midtones, float highlights,
                                       bool doRed, bool doGreen, bool doBlue) {
    if (!buffer.isValid()) return;
    
    // Mask Support: Copy original if mask is present
    bool hasMask = buffer.hasMask();
    ImageBuffer original;
    if (hasMask) {
        original = buffer; // Copy data
    }

    int w = buffer.width();
    int h = buffer.height();
    int channels = buffer.channels();
    size_t n = static_cast<size_t>(w) * h;
    
    std::vector<float>& data = buffer.data();
    
    if (channels == 3) {
        bool doChannel[3] = { doRed, doGreen, doBlue };
        
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (size_t i = 0; i < n; ++i) {
            size_t idx = i * 3;
            for (int c = 0; c < 3; ++c) {
                if (doChannel[c]) {
                    data[idx + c] = MTF(data[idx + c], midtones, shadows, highlights);
                }
            }
        }
    } else if (channels == 1) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (size_t i = 0; i < n; ++i) {
            data[i] = MTF(data[i], midtones, shadows, highlights);
        }
    }

    // Blend back if masked
    if (hasMask) {
        buffer.blendResult(original);
    }
}

void HistogramStretchDialog::computeAutostretch(const ImageBuffer& buffer,
                                                  float& shadows, float& midtones, float& highlights) {
    if (!buffer.isValid()) return;
    // Delegate entirely to ImageBuffer::computeAutoStretchParams, which uses the exact same
    // internal computeStats() code as getDisplayImage(Display_AutoStretch). This is the only
    // way to guarantee that Auto Stretch in the dialog produces bit-identical results to the display.
    float targetMedian = (m_viewer) ? m_viewer->getAutoStretchMedian() : AS_DEFAULT_TARGET_BACKGROUND;
    auto p = buffer.computeAutoStretchParams(true, targetMedian);
    shadows    = p.shadows;
    midtones   = p.midtones;
    highlights = p.highlights;
}

float HistogramStretchDialog::computeMedian(const float* data, size_t n) {
    std::vector<float> sorted(data, data + n);
    std::nth_element(sorted.begin(), sorted.begin() + n/2, sorted.end());
    return sorted[n/2];
}

float HistogramStretchDialog::computeMAD(const float* data, size_t n, float median) {
    std::vector<float> deviations(n);
    for (size_t i = 0; i < n; ++i) {
        deviations[i] = std::abs(data[i] - median);
    }
    std::nth_element(deviations.begin(), deviations.begin() + n/2, deviations.end());
    return deviations[n/2];
}

void HistogramStretchDialog::onZoomChanged() {
    int baseWidth = m_scrollArea->viewport()->width();
    if (baseWidth < 100) baseWidth = 400;
    const double zoomScale = histogramZoomScaleFromLevel(m_zoomLevel);
    const int newWidth = static_cast<int>(baseWidth * zoomScale);

    if (m_zoomLevel == 1 || zoomScale <= 1.001) {
        m_scrollArea->setWidgetResizable(true);
        m_histogram->setMinimumWidth(0);
        m_histogram->setMaximumWidth(16777215);
        m_histScrollBar->setVisible(false);
    } else {
        m_scrollArea->setWidgetResizable(true); // Keep resizable to allow dynamic height adjustment!
        m_histogram->setMinimumWidth(newWidth);
        m_histogram->setMaximumWidth(newWidth); // Lock width explicitly
        m_histScrollBar->setVisible(true);
        m_scrollArea->horizontalScrollBar()->setValue(0);
    }
    updateHistogramOnly();
}
