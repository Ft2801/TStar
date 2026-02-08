#include "StarAnalysisDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QTableWidget>
#include <QHeaderView>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <numeric>
#include <algorithm>
#include <cmath>

// ------ Widget ------

StarHistogramWidget::StarHistogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 300);
}

void StarHistogramWidget::setData(const std::vector<float>& data, const QString& label, bool logScale) {
    m_data = data;
    m_label = label;
    m_logScale = logScale;
    update();
}

void StarHistogramWidget::setLogScale(bool log) {
    m_logScale = log;
    update();
}

void StarHistogramWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    int w = width();
    int h = height();
    
    // Fill Back
    p.fillRect(rect(), Qt::white);
    
    if (m_data.empty()) {
        p.drawText(rect(), Qt::AlignCenter, tr("No Data"));
        return;
    }
    
    // Binning
    int bins = 50;
    std::vector<int> hist(bins, 0);
    
    // Robust Range (1st - 99th Percentile)
    std::vector<float> sorted = m_data; // Copy to sort
    std::sort(sorted.begin(), sorted.end());
    
    size_t n = sorted.size();
    float minVal = sorted[n * 0.01]; // 1st percentile
    float maxVal = sorted[n * 0.99]; // 99th percentile
    
    // Fallback if range is zero
    if (std::abs(maxVal - minVal) < 1e-6) {
        if (n > 0) {
           minVal = sorted.front();
           maxVal = sorted.back();
        } else {
           minVal = 0; maxVal = 1;
        }
    }
    
    // Still zero? Expand slightly
    if (std::abs(maxVal - minVal) < 1e-6) {
        maxVal = minVal + 1.0f;
        minVal = minVal - 1.0f;
    }
    
    // Linear Histogram
    float range = maxVal - minVal;
    
    std::vector<float> edges(bins + 1);
    
    if (m_logScale && minVal > 1e-5) {
        float logMin = std::log10(minVal);
        float logMax = std::log10(maxVal);
        float logStep = (logMax - logMin) / bins;
        for(int i=0; i<=bins; ++i) edges[i] = std::pow(10.0f, logMin + i * logStep);
    } else {
        float step = range / bins;
        for(int i=0; i<=bins; ++i) edges[i] = minVal + i * step;
    }
    
    for (float v : m_data) {
        // Find bin
        if (v < edges[0]) continue;
        if (v > edges[bins]) continue;
        
        // Linear search for bin (or bisect)
        auto it = std::upper_bound(edges.begin(), edges.end(), v);
        int idx = std::distance(edges.begin(), it) - 1;
        if (idx >= 0 && idx < bins) hist[idx]++;
    }
    
    int maxCount = *std::max_element(hist.begin(), hist.end());
    if (maxCount == 0) maxCount = 1;
    
    // Draw
    p.setBrush(QColor(100, 150, 240));
    p.setPen(Qt::NoPen);
    
    int margin = 30;
    int drawW = w - 2 * margin;
    int drawH = h - 2 * margin;
    
    for (int i = 0; i < bins; ++i) {
        float hRatio = (float)hist[i] / maxCount;
        int barH = (int)(hRatio * drawH);
        
        // Draw equal width bars for visual consistency, labeling X axis accordingly.
        float x1 = margin + (float)i / bins * drawW;
        float x2 = margin + (float)(i+1) / bins * drawW;
        p.drawRect(QRectF(x1, h - margin - barH, x2 - x1, barH));
    }
    
    // Axis
    p.setPen(Qt::black);
    p.drawLine(margin, h - margin, w - margin, h - margin); // X
    p.drawLine(margin, h - margin, margin, margin); // Y
    
    // Labels
    p.drawText(margin, h - 5, QString::number(minVal, 'f', 2));
    p.drawText(w - margin - 30, h - 5, QString::number(maxVal, 'f', 2));
    p.drawText(5, margin + 10, QString::number(maxCount));
    
    // Title
    p.drawText(rect(), Qt::AlignTop | Qt::AlignHCenter, m_label);
}

// ------ Worker ------

StarAnalysisWorker::StarAnalysisWorker(QObject* parent) : QThread(parent) {}

void StarAnalysisWorker::setup(const ImageBuffer& src, float threshold) {
    m_src = src; // Copy for thread safety
    m_threshold = threshold;
}

void StarAnalysisWorker::run() {
    if (!m_src.isValid()) {
        emit failed(tr("No Image"));
        return;
    }
    
    // Extract Stars.
    // extractStars works on vector<float>, so convert to mono luminance if input is RGB.
    std::vector<float> mono;
    if (m_src.channels() == 1) {
        mono = m_src.data();
    } else {
        int w = m_src.width();
        int h = m_src.height();
        mono.resize(w * h);
        const auto& data = m_src.data();
        for(int i=0; i<w*h; ++i) {
            float r = data[i*3];
            float g = data[i*3+1];
            float b = data[i*3+2];
            mono[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        }
    }
    
    auto stars = ImageBuffer::extractStars(mono, m_src.width(), m_src.height(), m_threshold);
    emit finished(stars);
}


// ------ Dialog ------
 
StarAnalysisDialog::StarAnalysisDialog(QWidget* parent, ImageViewer* viewer)
    : DialogBase(parent, "Star Analysis", 600, 500), m_viewer(viewer)
{
    
    m_worker = new StarAnalysisWorker(this);
    connect(m_worker, &StarAnalysisWorker::finished, this, &StarAnalysisDialog::onWorkerFinished);
    
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(500);
    connect(m_debounceTimer, &QTimer::timeout, this, &StarAnalysisDialog::onRunClicked);
    
    createUI();
    
    // Initial run
    QTimer::singleShot(100, this, &StarAnalysisDialog::onRunClicked);

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

StarAnalysisDialog::~StarAnalysisDialog() {
    if (m_worker->isRunning()) {
        m_worker->terminate();
        m_worker->wait();
    }
}

void StarAnalysisDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    m_viewer = v;
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_statusLabel->setText(tr("Target: %1. Processing...").arg(m_viewer->windowTitle()));
        onRunClicked();
    } else {
        m_statusLabel->setText(tr("No Image"));
        m_stars.clear();
        updateStats(); // Clear
        updateHistogram(); // Clear
    }
}

void StarAnalysisDialog::createUI() {
    QVBoxLayout* main = new QVBoxLayout(this);
    
    // Graphs
    m_histWidget = new StarHistogramWidget(this);
    main->addWidget(m_histWidget);
    
    // Stats Table
    m_statsTable = new QTableWidget(this);
    m_statsTable->setRowCount(4);
    m_statsTable->setColumnCount(6);
    m_statsTable->setHorizontalHeaderLabels({tr("Flux"), tr("HFR"), tr("Eccentricity"), tr("a"), tr("b"), tr("theta")});
    m_statsTable->setVerticalHeaderLabels({tr("Min"), tr("Max"), tr("Median"), tr("StdDev")});
    m_statsTable->setFixedHeight(150);
    main->addWidget(m_statsTable);
    
    // Controls
    QHBoxLayout* controls = new QHBoxLayout();
    
    controls->addWidget(new QLabel(tr("Threshold (Sigma):")));
    m_threshSlider = new QSlider(Qt::Horizontal);
    m_threshSlider->setRange(1, 20);
    m_threshSlider->setValue(5);
    m_threshLabel = new QLabel(tr("5"));
    
    connect(m_threshSlider, &QSlider::valueChanged, [=](int v){ 
        m_threshLabel->setText(QString::number(v)); 
        onThresholdChanged();
    });
    
    controls->addWidget(m_threshSlider);
    controls->addWidget(m_threshLabel);
    
    m_modeBtn = new QPushButton(tr("Show Flux Distribution"));
    connect(m_modeBtn, &QPushButton::clicked, this, &StarAnalysisDialog::toggleMode);
    controls->addWidget(m_modeBtn);
    
    m_logCheck = new QCheckBox(tr("Log Scale"));
    connect(m_logCheck, &QCheckBox::toggled, this, &StarAnalysisDialog::toggleLog);
    controls->addWidget(m_logCheck);
    
    main->addLayout(controls);
    
    m_statusLabel = new QLabel(tr("Ready"));
    main->addWidget(m_statusLabel);
    
    // Buttons
    QHBoxLayout* btns = new QHBoxLayout();
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btns->addStretch();
    btns->addWidget(closeBtn);
    main->addLayout(btns);
}

void StarAnalysisDialog::onThresholdChanged() {
    m_debounceTimer->start();
}

void StarAnalysisDialog::onRunClicked() {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        m_statusLabel->setText(tr("No Image or Image Closed"));
        return;
    }
    
    m_statusLabel->setText(tr("Processing..."));
    float t = (float)m_threshSlider->value();
    m_worker->setup(m_viewer->getBuffer(), t);
    m_worker->start();
}

// Stats Helper
static float get_median(std::vector<float>& v) {
    if (v.empty()) return 0;
    size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + n, v.end());
    return v[n];
}

static float get_stddev(const std::vector<float>& v, float mean) {
    if (v.empty()) return 0;
    double sumSq = 0;
    for (float x : v) sumSq += (x - mean) * (x - mean);
    return std::sqrt(sumSq / v.size());
}

void StarAnalysisDialog::onWorkerFinished(std::vector<ImageBuffer::DetectedStar> stars) {
    m_stars = stars;
    QString msg = tr("Star Analysis: Found %1 stars").arg(stars.size());
    m_statusLabel->setText(msg);
    if (!stars.empty()) {
        updateStats();
        // Get median HFR for log
        std::vector<float> hfrs;
        for(const auto& s : stars) hfrs.push_back(s.hfr);
        float medHFR = get_median(hfrs);
        msg += tr(" (Median HFR: %1 px)").arg(medHFR, 0, 'f', 2);
    }
    emit statusMsg(msg);
    updateHistogram();
}

void StarAnalysisDialog::toggleLog(bool checked) {
    m_logScale = checked;
    m_histWidget->setLogScale(checked);
    updateHistogram();
}

void StarAnalysisDialog::toggleMode() {
    if (m_mode == Mode_HFR) {
        m_mode = Mode_Flux;
        m_modeBtn->setText(tr("Show HFR Distribution"));
    } else {
        m_mode = Mode_HFR;
        m_modeBtn->setText(tr("Show Flux Distribution"));
    }
    updateHistogram();
}

void StarAnalysisDialog::updateHistogram() {
    if (m_stars.empty()) {
        m_histWidget->setData({}, tr("No Data"), m_logScale);
        return;
    }
    
    std::vector<float> data;
    data.reserve(m_stars.size());
    QString label;
    
    for (const auto& s : m_stars) {
        if (m_mode == Mode_HFR) data.push_back(s.hfr);
        else data.push_back(s.flux);
    }
    
    label = (m_mode == Mode_HFR) ? tr("Half Flux Radius Distribution") : tr("Flux Distribution");
    m_histWidget->setData(data, label, m_logScale);
}



void StarAnalysisDialog::updateStats() {
    if (m_stars.empty()) return;
    
    // Collect columns
    std::vector<float> flux, hfr, ecc, a, b, theta;
    int n = m_stars.size();
    flux.reserve(n); hfr.reserve(n); ecc.reserve(n); a.reserve(n); b.reserve(n); theta.reserve(n);
    
    for (const auto& s : m_stars) {
        flux.push_back(s.flux);
        hfr.push_back(s.hfr);
        float e = 0.0f;
        if (s.a > 1e-5f) e = std::sqrt(std::max(0.0f, 1.0f - (s.b / s.a)*(s.b / s.a)));
        ecc.push_back(e);
        a.push_back(s.a);
        b.push_back(s.b);
        theta.push_back(s.theta);
    }
    
    auto computeAndSet = [&](int col, std::vector<float>& vec) {
        if (vec.empty()) return;
        float mn = *std::min_element(vec.begin(), vec.end());
        float mx = *std::max_element(vec.begin(), vec.end());
        float med = get_median(vec);
        double sum = std::accumulate(vec.begin(), vec.end(), 0.0);
        float mean = sum / vec.size();
        float std = get_stddev(vec, mean);
        
        m_statsTable->setItem(0, col, new QTableWidgetItem(QString::number(mn, 'f', 3)));
        m_statsTable->setItem(1, col, new QTableWidgetItem(QString::number(mx, 'f', 3)));
        m_statsTable->setItem(2, col, new QTableWidgetItem(QString::number(med, 'f', 3)));
        m_statsTable->setItem(3, col, new QTableWidgetItem(QString::number(std, 'f', 3)));
    };
    
    computeAndSet(0, flux);
    computeAndSet(1, hfr);
    computeAndSet(2, ecc);
    computeAndSet(3, a);
    computeAndSet(4, b);
    computeAndSet(5, theta);
}
