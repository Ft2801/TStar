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

static constexpr float AS_DEFAULT_SHADOWS_CLIPPING = -2.80f;
static constexpr float AS_DEFAULT_TARGET_BACKGROUND = 0.25f;
static constexpr float MAD_NORM = 1.4826f;

HistogramStretchDialog::HistogramStretchDialog(ImageViewer* viewer, QWidget* parent)
    : QDialog(parent), m_viewer(nullptr), m_applied(false)
{
    setWindowTitle(tr("Histogram Transformation"));
    setMinimumWidth(500);
    setMinimumHeight(480);
    
    setupUI();
    setViewer(viewer);
}

HistogramStretchDialog::~HistogramStretchDialog() {
    if (!m_applied && m_viewer) {
        m_viewer->clearPreviewLUT();
    }
}

void HistogramStretchDialog::reject() {
    // Clear preview and stay with original state
    if (!m_applied && m_viewer) {
        m_viewer->clearPreviewLUT();
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
    
    // --- Histogram Display ---
    m_histogram = new HistogramWidget();
    m_histogram->setMinimumHeight(180);
    if (m_backup.isValid()) {
        m_baseHist = m_backup.computeHistogram(65536);
        m_histogram->setData(m_baseHist, m_backup.channels());
    }
    mainLayout->addWidget(m_histogram);
    
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
    // Shadows slider: real-time histogram, preview on release
    connect(m_shadowsSlider, &QSlider::valueChanged, [this](int val){
        double dval = val / 10000.0;
        m_shadowsSpin->blockSignals(true);
        m_shadowsSpin->setValue(dval);
        m_shadowsSpin->blockSignals(false);
        m_shadows = static_cast<float>(dval);
        updateHistogramOnly();  // Real-time histogram
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
        if (m_viewer) m_viewer->setBuffer(m_backup);
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
    
    if (m_viewer) m_viewer->setBuffer(m_backup);
    if (m_histogram && m_backup.isValid()) {
        auto bins = m_backup.computeHistogram(65536);
        m_histogram->setData(bins, m_backup.channels());
    }
    m_lowClipLabel->setText(tr("Low: 0.00%"));
    m_highClipLabel->setText(tr("High: 0.00%"));
}

void HistogramStretchDialog::onApply() {
    if (m_viewer) {
        // 1. Restore to backup (clean state)
        m_viewer->setBuffer(m_backup);

        // 2. Push Undo de backup
        m_viewer->pushUndo();

        // 3. Apply MTF
        ImageBuffer buf = m_backup;
        applyMTF(buf, m_shadows, m_midtones, m_highlights, m_doRed, m_doGreen, m_doBlue);
        m_viewer->setBuffer(buf);
        m_applied = true;
    }
    accept();
}

void HistogramStretchDialog::updatePreview() {
    if (!m_viewer || !m_previewCheck->isChecked()) return;
    
    // Create Preview LUT (65536 levels)
    std::vector<std::vector<float>> luts(3, std::vector<float>(65536));
    for (int i = 0; i < 65536; ++i) {
        float x = (float)i / 65535.0f;
        float y = MTF(x, m_midtones, m_shadows, m_highlights);
        luts[0][i] = m_doRed ? y : x;
        luts[1][i] = m_doGreen ? y : x;
        luts[2][i] = m_doBlue ? y : x;
    }
    m_viewer->setPreviewLUT(luts);
    
    // Clipping stats are updated in updateHistogramOnly now (much faster)
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
    
    int channels = buffer.channels();
    int w = buffer.width();
    int h = buffer.height();
    size_t n = static_cast<size_t>(w) * h;
    
    const std::vector<float>& data = buffer.data();
    
    float c0_sum = 0.0f;
    float m_sum = 0.0f;
    
    for (int c = 0; c < channels; ++c) {
        // Extract channel data
        std::vector<float> channelData(n);
        if (channels == 3) {
            for (size_t i = 0; i < n; ++i) {
                channelData[i] = data[i * 3 + c];
            }
        } else {
            channelData.assign(data.begin(), data.end());
        }
        
        // Compute median
        std::vector<float> sorted = channelData;
        std::nth_element(sorted.begin(), sorted.begin() + n/2, sorted.end());
        float median = sorted[n/2];
        
        // Compute MAD (Median Absolute Deviation)
        std::vector<float> deviations(n);
        for (size_t i = 0; i < n; ++i) {
            deviations[i] = std::abs(channelData[i] - median);
        }
        std::nth_element(deviations.begin(), deviations.begin() + n/2, deviations.end());
        float mad = deviations[n/2] * MAD_NORM;
        
        // Guard against zero MAD
        if (mad == 0.0f) mad = 0.001f;
        
        // Compute shadow clipping
        float c0 = median + AS_DEFAULT_SHADOWS_CLIPPING * mad;
        if (c0 < 0.0f) c0 = 0.0f;
        
        c0_sum += c0;
        m_sum += median;
    }
    
    // Average across channels
    c0_sum /= channels;
    m_sum /= channels;
    
    // Compute final parameters
    shadows = c0_sum;
    float m2 = m_sum - c0_sum;
    midtones = MTF(m2, AS_DEFAULT_TARGET_BACKGROUND, 0.0f, 1.0f);
    highlights = 1.0f;
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
