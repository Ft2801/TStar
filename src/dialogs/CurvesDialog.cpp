#include "CurvesDialog.h"
#include "../ImageBuffer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QToolButton>
#include <QLineEdit>
#include <cmath>
#include <algorithm>

// --- CurvesGraph ---

CurvesGraph::CurvesGraph(QWidget* parent) : QWidget(parent) {
    setMinimumSize(339, 281); 
    setMouseTracking(true);
    reset();
}

void CurvesGraph::reset() {
    m_points.clear();
    m_points.push_back({0.0, 0.0});
    m_points.push_back({1.0, 1.0});
    m_dragIdx = -1;
    update();
    emit curvesChanged();
}

void CurvesGraph::setPoints(const std::vector<SplinePoint>& pts) {
    m_points = pts;
    sortPoints();
    update();
    emit curvesChanged();
}

void CurvesGraph::sortPoints() {
    std::sort(m_points.begin(), m_points.end());
}

SplineData CurvesGraph::getSpline() const {
    return CubicSpline::fit(m_points);
}

void CurvesGraph::setHistogram(const std::vector<std::vector<int>>& hist) {
    m_hist = hist;
    updateResampledBins();
    update();
}

void CurvesGraph::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResampledBins();
}

void CurvesGraph::updateResampledBins() {
    int w = width();
    if (w <= 0 || m_hist.empty()) return;
    
    int channels = m_hist.size();
    
    // Box blur helper
    auto boxBlur = [](std::vector<float>& data, int radius) {
        if (data.empty() || radius <= 0) return;
        int n = data.size();
        std::vector<float> temp(n);
        for (int i = 0; i < n; ++i) {
            float sum = 0;
            int count = 0;
            for (int j = -radius; j <= radius; ++j) {
                int idx = i + j;
                if (idx >= 0 && idx < n) {
                    sum += data[idx];
                    count++;
                }
            }
            temp[i] = (count > 0) ? sum / count : 0;
        }
        data = std::move(temp);
    };
    
    // Memory Optimization: Only reallocate if size changes
    if (w != m_lastW || m_resampledBins.size() != (size_t)channels) {
        m_resampledBins.assign(channels, std::vector<float>(w, 0.0f));
        m_channelMaxVals.assign(channels, 0.0f);
        m_lastW = w;
    } else {
        // Reuse existing vectors, just zero them out
        for(auto& vec : m_resampledBins) {
            std::fill(vec.begin(), vec.end(), 0.0f);
        }
        std::fill(m_channelMaxVals.begin(), m_channelMaxVals.end(), 0.0f);
    }
    
    for (int c = 0; c < channels; ++c) {
        const auto& bins = m_hist[c];
        int numBins = bins.size();
        if (numBins == 0) continue;
        
        float binsPerPx = (float)numBins / (float)w;
        int binIdx = 0;
        
        for (int px = 0; px < w; ++px) {
            float sum = 0;
            // Accumulate bins that fall into this pixel column
            while (binIdx < numBins && (float)binIdx / binsPerPx <= (float)px + 0.5f) {
                sum += (float)bins[binIdx];
                binIdx++;
            }
            if (m_logScale && sum > 0) sum = std::log(sum); 
            m_resampledBins[c][px] = sum;
        }
        
        // Apply smoothing to eliminate spikes
        boxBlur(m_resampledBins[c], 4); 
        
        // Recalculate max after blur
        float maxVal = 0;
        for (int px = 0; px < w; ++px) {
            if (m_resampledBins[c][px] > maxVal) maxVal = m_resampledBins[c][px];
        }
        m_channelMaxVals[c] = maxVal;
    }
}

void CurvesGraph::setChannelMode(int mode) {
    m_channelMode = mode;
    update();
}

void CurvesGraph::setLogScale(bool enabled) {
    if (m_logScale == enabled) return;
    m_logScale = enabled;
    updateResampledBins();
    update();
}

void CurvesGraph::setGridVisible(bool visible) {
    m_showGrid = visible;
    update();
}

void CurvesGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    int w = width();
    int h = height();
    
    // Background
    p.fillRect(rect(), Qt::black);
    
    // Grid
    if (m_showGrid) {
        p.setPen(QPen(QColor(100, 100, 100), 1, Qt::SolidLine));
        // Quarters
        for (int i=1; i<4; ++i) {
             p.drawLine(i*w/4, 0, i*w/4, h);
             p.drawLine(0, i*h/4, w, i*h/4);
        }
        
        // Eighths (Dashed)
        p.setPen(QPen(QColor(60, 60, 60), 1, Qt::DashLine));
        for (int i=1; i<8; ++i) {
             if (i % 2 == 0) continue; // Skip quarters
             p.drawLine(i*w/8, 0, i*w/8, h);
             p.drawLine(0, i*h/8, w, i*h/8);
        }
    }
    
    // Diagonal Ref (x=y)
    p.setPen(QPen(QColor(80, 80, 80), 1, Qt::SolidLine));
    p.drawLine(0, h, w, 0); 
    
    // Histogram
    if (!m_resampledBins.empty()) {
        QColor colors[3] = { QColor(255, 80, 80), QColor(80, 255, 80), QColor(80, 80, 255) };
        p.setCompositionMode(QPainter::CompositionMode_Screen);

        auto drawCh = [&](int c) {
            if (c >= (int)m_resampledBins.size()) return;
            // Channel toggle check
            if (m_channelMode != -1 && c != m_channelMode) { 
                // But wait, m_channelMode is not used in this class logic for visibility?
                // The dialog handles the 'ch' check for recalculation.
                // However, let's assume if the bin data is 0, it's hidden.
            }
            
            const auto& displayBins = m_resampledBins[c];
            float channelMax = m_channelMaxVals[c];
            if (channelMax <= 0) return;

            QColor color = colors[c % 3];
            color.setAlpha(60);
            
            QPainterPath path;
            path.moveTo(0, h);
            for (int px = 0; px < w; ++px) {
                float val = (px < (int)displayBins.size()) ? displayBins[px] : 0.0f;
                float py = h - (val / channelMax * h);
                path.lineTo(px, py);
            }
            path.lineTo(w, h);
            path.closeSubpath();

            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawPath(path);

            color.setAlpha(200);
            p.setPen(QPen(color, 1.2));
            p.setBrush(Qt::NoBrush);
            
            // Draw line on top
            QPainterPath linePath;
            bool first = true;
            for (int px = 0; px < w; ++px) {
                float val = (px < (int)displayBins.size()) ? displayBins[px] : 0.0f;
                float py = h - (val / channelMax * h);
                if (first) { linePath.moveTo(px, py); first = false; }
                else { linePath.lineTo(px, py); }
            }
            p.drawPath(linePath);
        };
        
        // Draw RGB Histograms
        drawCh(0); // R
        drawCh(1); // G
        drawCh(2); // B
        
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    
    // Spline Curve
    SplineData spline = getSpline();
    if (spline.n >= 2) {
        p.setPen(QPen(QColor(250, 128, 114), 2)); 
        QPolygonF curvePoly;
        int steps = w;
        for (int i=0; i<=steps; ++i) {
            float x = (float)i / steps;
            float val = CubicSpline::interpolate(x, spline);
            float py = h - val * h;
            curvePoly << QPointF(i, py);
        }
        p.drawPolyline(curvePoly);
    }
    
    // Points
    for (size_t i=0; i<m_points.size(); ++i) {
        float px = m_points[i].x * w;
        float py = h - m_points[i].y * h;
        
        p.setPen(QPen(Qt::green, 1));
        if (i == (size_t)m_dragIdx || i == (size_t)m_hoverIdx) {
             p.setBrush(Qt::green);
        } else {
             p.setBrush(Qt::NoBrush);
        }
        p.drawRect(QRectF(px - 3, py - 3, 6, 6)); 
    }
}

void CurvesGraph::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        int w = width();
        int h = height();
        float mx = (float)event->position().x() / w;
        float my = 1.0f - (float)event->position().y() / h;
        
        int idx = -1;
        float dist = 0.05f; 
        for (size_t i=0; i<m_points.size(); ++i) {
            float dx = m_points[i].x - mx;
            float dy = m_points[i].y - my;
            float d = std::sqrt(dx*dx + dy*dy);
            if (d < dist) {
                dist = d;
                idx = (int)i;
            }
        }
        
        if (idx != -1) {
            m_dragIdx = idx;
        } else {
            m_points.push_back({mx, my});
            sortPoints();
            for (size_t i=0; i<m_points.size(); ++i) {
                if (std::abs(m_points[i].x - mx) < 1e-5) {
                    m_dragIdx = (int)i;
                    break;
                }
            }
        }
        update();
        emit curvesChanged(); 
    } else if (event->button() == Qt::RightButton) {
        int w = width();
        int h = height();
        float mx = (float)event->position().x() / w;
        float my = 1.0f - (float)event->position().y() / h;
        
        for (size_t i=0; i<m_points.size(); ++i) {
            if (i == 0 || i == m_points.size()-1) continue;
            
            float dx = m_points[i].x - mx;
            float dy = m_points[i].y - my;
            if (std::sqrt(dx*dx + dy*dy) < 0.05f) {
                m_points.erase(m_points.begin() + i);
                update();
                emit curvesChanged();
                break;
            }
        }
    }
}

void CurvesGraph::mouseMoveEvent(QMouseEvent* event) {
    int w = width();
    int h = height();
    float mx = (float)event->position().x() / w;
    float my = 1.0f - (float)event->position().y() / h;
    
    if (m_dragIdx != -1) {
        mx = std::max(0.0f, std::min(1.0f, mx));
        my = std::max(0.0f, std::min(1.0f, my));
        
        if (m_dragIdx > 0) {
            float minX = (float)m_points[m_dragIdx - 1].x + 0.0001f;
            if (mx < minX) mx = minX;
        } else {
             mx = 0.0f; 
        }
        
        if (m_dragIdx < (int)m_points.size() - 1) {
            float maxX = (float)m_points[m_dragIdx + 1].x - 0.0001f;
            if (mx > maxX) mx = maxX;
        } else {
             mx = 1.0f; 
        }
        
        m_points[m_dragIdx].x = mx;
        m_points[m_dragIdx].y = my;
        update();
        emit curvesChanged();
    } else {
        m_hoverIdx = -1;
        float dist = 0.05f;
        for (size_t i=0; i<m_points.size(); ++i) {
            float dx = m_points[i].x - mx;
            float dy = m_points[i].y - my;
            if (std::sqrt(dx*dx + dy*dy) < dist) {
                m_hoverIdx = (int)i;
                break;
            }
        }
        update();
    }
}

void CurvesGraph::mouseReleaseEvent(QMouseEvent* event) {
    m_dragIdx = -1;
    update();
}

// --- CurvesDialog ---

CurvesDialog::CurvesDialog(ImageViewer* viewer, QWidget* parent) 
    : QDialog(parent), m_viewer(viewer)
{
    setWindowTitle(tr("Curves Transformation"));
    setMinimumWidth(500);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Toolbar (Log, Grid, Toggles)
    QHBoxLayout* topLayout = new QHBoxLayout();
    
    m_logBtn = new QToolButton();
    m_logBtn->setText(tr("Log"));
    m_logBtn->setCheckable(true);
    m_logBtn->setChecked(false); // Default Linear
    connect(m_logBtn, &QToolButton::toggled, this, &CurvesDialog::onLogToggled);
    topLayout->addWidget(m_logBtn);
    
    m_gridBtn = new QToolButton();
    m_gridBtn->setText(tr("Grid"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setChecked(true);
    connect(m_gridBtn, &QToolButton::toggled, this, &CurvesDialog::onGridToggled);
    topLayout->addWidget(m_gridBtn);
    
    topLayout->addSpacing(20);
    
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
    
    connect(m_redBtn, &QToolButton::toggled, this, &CurvesDialog::onChannelToggled);
    connect(m_greenBtn, &QToolButton::toggled, this, &CurvesDialog::onChannelToggled);
    connect(m_blueBtn, &QToolButton::toggled, this, &CurvesDialog::onChannelToggled);
    
    topLayout->addWidget(m_redBtn);
    topLayout->addWidget(m_greenBtn);
    topLayout->addWidget(m_blueBtn);
    
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);
    
    // Graph area
    m_graph = new CurvesGraph(this);
    if (m_viewer && m_viewer->getBuffer().isValid()) {
         m_origHist = m_viewer->getBuffer().computeHistogram(65536);
         m_graph->setHistogram(m_origHist);
    }
    m_graph->installEventFilter(this);
    connect(m_graph, &CurvesGraph::curvesChanged, this, [this](){
        onCurvesChanged(false); 
    });
    
    mainLayout->addWidget(m_graph, 1);
    
    // Point Stats area
    QHBoxLayout* statsLayout = new QHBoxLayout();
    m_statsLabel = new QLabel(tr("Point: x=0.000, y=0.000"));
    statsLayout->addWidget(m_statsLabel);
    // connect mouse move on graph to update stats? 
    // For now we'll just update it in onCurvesChanged
    mainLayout->addLayout(statsLayout);

    // Bottom Controls
    QHBoxLayout* botLayout = new QHBoxLayout();
    
    QPushButton* resetBtn = new QPushButton(tr("Reset"));
    connect(resetBtn, &QPushButton::clicked, this, &CurvesDialog::onReset);
    botLayout->addWidget(resetBtn);
    
    m_previewCheck = new QCheckBox(tr("Preview"));
    m_previewCheck->setChecked(true);
    connect(m_previewCheck, &QCheckBox::toggled, this, &CurvesDialog::onPreviewToggled);
    botLayout->addWidget(m_previewCheck);
    
    botLayout->addStretch();
    
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    applyBtn->setDefault(true);
    connect(applyBtn, &QPushButton::clicked, this, &CurvesDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &CurvesDialog::reject);
    
    botLayout->addWidget(applyBtn);
    botLayout->addWidget(cancelBtn);
    
    mainLayout->addLayout(botLayout);
}

CurvesDialog::~CurvesDialog() {}

// Helper to downsample histogram
static std::vector<std::vector<int>> downsampleHist(const std::vector<std::vector<int>>& src, int targetBins) {
    if (src.empty()) return {};
    int srcBins = src[0].size();
    if (srcBins <= targetBins) return src;
    
    std::vector<std::vector<int>> dst(src.size(), std::vector<int>(targetBins, 0));
    for(size_t c=0; c<src.size(); ++c) {
        for(int i=0; i<srcBins; ++i) {
             int tIdx = (i * (targetBins - 1)) / (srcBins - 1);
             dst[c][tIdx] += src[c][i];
        }
    }
    return dst;
}

void CurvesDialog::setInputHistogram(const std::vector<std::vector<int>>& hist) {
    m_origHist = hist;
    m_uiHist = downsampleHist(m_origHist, 1024);
    onCurvesChanged(false); 
}

void CurvesDialog::setViewer(ImageViewer* viewer) {
    if (m_viewer == viewer) return;
    
    // Clear preview on old viewer if needed
    if (m_viewer) {
        m_viewer->clearPreviewLUT();
    }

    m_viewer = viewer;
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
         m_origHist = m_viewer->getBuffer().computeHistogram(65536);
         m_uiHist = downsampleHist(m_origHist, 1024);
         m_graph->setHistogram(m_uiHist); // Initial show
    } else {
         m_origHist.clear();
         m_uiHist.clear();
         m_graph->setHistogram({});
    }
    
    // Force re-calculation/re-preview for the new buffer
    // But we must NOT allow `onCurvesChanged` to emit preview if the buffer is invalid.
    onCurvesChanged(true); 
}

bool CurvesDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_graph && event->type() == QEvent::MouseButtonRelease) {
        onCurvesChanged(true);
    }
    return QDialog::eventFilter(obj, event);
}

void CurvesDialog::onLogToggled(bool checked) {
    m_graph->setLogScale(checked);
}

void CurvesDialog::onGridToggled(bool checked) {
    m_graph->setGridVisible(checked);
}

void CurvesDialog::onChannelToggled() {
    onCurvesChanged(true); 
}

void CurvesDialog::onReset() {
    m_graph->reset();
    onCurvesChanged(true);
}

CurvesDialog::State CurvesDialog::getState() const {
    State s;
    s.points = m_graph->getPoints();
    // Read directly from graph private members? No, need accessors or use UI toggles.
    // Graph doesn't expose log/grid getters easily but we have toggles.
    s.logScale = m_logBtn->isChecked();
    s.showGrid = m_gridBtn->isChecked();
    s.ch[0] = m_redBtn->isChecked();
    s.ch[1] = m_greenBtn->isChecked();
    s.ch[2] = m_blueBtn->isChecked();
    s.preview = (m_previewCheck ? m_previewCheck->isChecked() : true);
    return s;
}

void CurvesDialog::setState(const State& s) {
    bool wasBlocked = signalsBlocked();
    blockSignals(true);
    
    // Graph
    m_graph->setPoints(s.points);
    
    // UI
    m_logBtn->blockSignals(true);
    m_logBtn->setChecked(s.logScale);
    m_logBtn->blockSignals(false);
    
    m_gridBtn->blockSignals(true);
    m_gridBtn->setChecked(s.showGrid);
    m_gridBtn->blockSignals(false);
    
    m_redBtn->blockSignals(true); m_redBtn->setChecked(s.ch[0]); m_redBtn->blockSignals(false);
    m_greenBtn->blockSignals(true); m_greenBtn->setChecked(s.ch[1]); m_greenBtn->blockSignals(false);
    m_blueBtn->blockSignals(true); m_blueBtn->setChecked(s.ch[2]); m_blueBtn->blockSignals(false);
    
    if(m_previewCheck) {
        m_previewCheck->blockSignals(true);
        m_previewCheck->setChecked(s.preview);
        m_previewCheck->blockSignals(false);
    }
    
    // Apply low-level state to graph without triggering update yet
    m_graph->setLogScale(s.logScale);
    m_graph->setGridVisible(s.showGrid);

    blockSignals(wasBlocked);
}

void CurvesDialog::resetState() {
    onReset();
}

void CurvesDialog::onApply() {
    if (m_viewer) m_viewer->clearPreviewLUT();
    m_applied = true;
    SplineData spline = m_graph->getSpline();
    bool ch[3] = {m_redBtn->isChecked(), m_greenBtn->isChecked(), m_blueBtn->isChecked()};
    emit applyRequested(spline, ch);
    accept();
}

void CurvesDialog::onPreviewToggled(bool checked) {
    onCurvesChanged(true);
}

void CurvesDialog::onCurvesChanged(bool isFinal) {
    SplineData spline = m_graph->getSpline();
    bool ch[3] = {m_redBtn->isChecked(), m_greenBtn->isChecked(), m_blueBtn->isChecked()};
    
    // Update stats label for last points or selected?
    // We'll just show the last modified point for now
    // (Actually graph should provide it)
    
    // 1. Real-time Histogram Update (Optimized)
    // 1. Real-time Histogram Update (High Quality)
    if (!m_origHist.empty()) {
        const int HIST_SIZE = 65536;
        const int UI_BINS = 1024;
        
        // 1. Build Transform LUT (65536 -> 65536)
        std::vector<int> transformLUT(HIST_SIZE);
        #pragma omp parallel for
        for (int i = 0; i < HIST_SIZE; ++i) {
            float x = (float)i / (HIST_SIZE - 1);
            float val = CubicSpline::interpolate(x, spline);
            int outBin = std::clamp((int)(val * (HIST_SIZE - 1) + 0.5f), 0, HIST_SIZE - 1);
            transformLUT[i] = outBin;
        }

        // 2. Transform High-Res Histogram
        // We use a temp high-res histogram to accumulate transposed values
        int channels = std::min((int)m_origHist.size(), 3);
        std::vector<std::vector<int>> highResTransformed(channels, std::vector<int>(HIST_SIZE, 0));

        for(int c=0; c<channels; ++c) {
            if (!ch[c]) {
                // If channel is unchecked, we just copy original (untouched)
                highResTransformed[c] = m_origHist[c]; 
                continue;
            }
            
            const auto& srcBins = m_origHist[c];
            auto& dstBins = highResTransformed[c];
            
            // Scatter accumulation
            // (Note: Transforming a histogram means moving counts from bin I to transformLUT[I])
            for(int i=0; i<HIST_SIZE; ++i) {
                int count = (i < (int)srcBins.size()) ? srcBins[i] : 0;
                if (count > 0) {
                    dstBins[transformLUT[i]] += count;
                }
            }
        }
        
        // 3. Downsample for UI (1024 bins)
        // This "binning" step acts as an antialias filter, smoothing the "comb" gaps
        m_uiHist = downsampleHist(highResTransformed, UI_BINS);
        m_graph->setHistogram(m_uiHist);
    }
    
    // 2. Image Preview Update 
    if (isFinal) {
        if (!m_previewCheck->isChecked()) {
            std::vector<std::vector<float>> empty;
            emit previewRequested(empty);
            return;
        }
        
        std::vector<std::vector<float>> lut(3, std::vector<float>(65536));
        
        #pragma omp parallel for
        for (int i=0; i<65536; ++i) {
            float x = (float)i / 65535.0f;
            float y = CubicSpline::interpolate(x, spline);
            
            for (int c=0; c<3; ++c) {
                if (ch[c]) {
                    lut[c][i] = y;
                } else {
                    lut[c][i] = x;
                }
            }
        }
        emit previewRequested(lut);
    }
}
