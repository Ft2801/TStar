#include "HistogramWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

HistogramWidget::HistogramWidget(QWidget *parent) : QWidget(parent) {
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumHeight(150);
}

#include <QResizeEvent>

void HistogramWidget::setData(const std::vector<std::vector<int>>& bins, int channels) {
    m_bins = bins;
    m_channels = channels;
    updateResampledBins();
    update();
}

void HistogramWidget::setGhostData(const std::vector<std::vector<int>>& bins, int channels) {
    m_ghostBins = bins;
    m_ghostChannels = channels;
    updateResampledBins();
    update();
}

void HistogramWidget::setLogScale(bool enabled) {
    if (m_logScale == enabled) return;
    m_logScale = enabled;
    updateResampledBins();
    update();
}

void HistogramWidget::setZoom(float h, float v) {
    m_zoomH = h;
    m_zoomV = v;
    update();
}

void HistogramWidget::setShowGrid(bool show) {
    m_showGrid = show;
    update();
}

void HistogramWidget::setShowCurve(bool show) {
    m_showCurve = show;
    update();
}

void HistogramWidget::setTransformCurve(const std::vector<float>& lut) {
    m_lut = lut;
    update();
}

void HistogramWidget::clear() {
    m_bins.clear();
    m_ghostBins.clear();
    m_resampledBins.clear();
    m_resampledGhostBins.clear();
    m_channels = 0;
    m_ghostChannels = 0;
    m_lut.clear();
    update();
}

void HistogramWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResampledBins();
}

void HistogramWidget::updateResampledBins() {
    int w = width();
    int h = height();
    if (w <= 0 || h <= 0) return;
    m_lastW = w;

    auto resample = [&](const std::vector<std::vector<int>>& src, int channels, 
                        std::vector<std::vector<float>>& dst, double& maxValOut) {
        dst.clear();
        maxValOut = 0;
        if (src.empty() || channels <= 0) return;
        
        int numBins = src[0].size();
        dst.assign(channels, std::vector<float>(w, 0.0f));
        float binsPerPx = (float)numBins / (float)w;

        for (int c = 0; c < channels && c < (int)src.size(); ++c) {
            int binIdx = 0;
            for (int px = 0; px < w; ++px) {
                double sum = 0;
                while (binIdx < numBins && ((float)binIdx / binsPerPx) <= ((float)px + 0.5f)) {
                    sum += (double)src[c][binIdx];
                    binIdx++;
                }
                if (m_logScale && sum > 0) sum = std::log(sum); 
                dst[c][px] = (float)sum;
                if (dst[c][px] > maxValOut) maxValOut = dst[c][px];
            }
        }
    };

    resample(m_bins, m_channels, m_resampledBins, m_maxVal);
    resample(m_ghostBins, m_ghostChannels, m_resampledGhostBins, m_ghostMaxVal);
}

void HistogramWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    int w = width();
    int h = height();
    
    // Check if we need to resample due to size change (fallback)
    if (w != m_lastW) updateResampledBins();

    // Dark background
    painter.fillRect(rect(), QColor(20, 20, 20));
    
    // Draw Grid
    if (m_showGrid) {
        painter.setPen(QPen(QColor(60, 60, 60), 1));
        for (float x = 0.25f; x < 1.0f; x += 0.25f) {
            int px = static_cast<int>(x * w);
            painter.drawLine(px, 0, px, h);
        }
        for (float y = 0.25f; y < 1.0f; y += 0.25f) {
            int py = static_cast<int>(y * h);
            painter.drawLine(0, py, w, py);
        }
        painter.setPen(QPen(QColor(40, 40, 40), 1, Qt::DotLine));
        for (float x = 0.125f; x < 1.0f; x += 0.125f) {
            if (std::abs(fmod(x, 0.25f)) > 0.01f) {
                int px = static_cast<int>(x * w);
                painter.drawLine(px, 0, px, h);
            }
        }
    }
    
    // Draw Ghost Histogram
    if (!m_resampledGhostBins.empty() && m_ghostMaxVal > 0) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QColor ghostColor(100, 100, 100, 120);
        for (int c = 0; c < (int)m_resampledGhostBins.size(); ++c) {
            QPainterPath ghostPath;
            bool first = true;
            for (int px = 0; px < w; ++px) {
                double normH = m_resampledGhostBins[c][px] / m_ghostMaxVal;
                float py = h - (normH * h);
                if (first) { ghostPath.moveTo(px, py); first = false; }
                else { ghostPath.lineTo(px, py); }
            }
            painter.setPen(QPen(ghostColor, 1, Qt::DotLine));
            painter.drawPath(ghostPath);
        }
    }

    // Draw Histogram
    if (!m_resampledBins.empty() && m_maxVal > 0) {
        QColor colors[3] = { QColor(255, 80, 80), QColor(80, 255, 80), QColor(80, 80, 255) };
        if (m_channels == 1) colors[0] = Qt::white;
        painter.setCompositionMode(QPainter::CompositionMode_Screen);
        
        for (int c = 0; c < (int)m_resampledBins.size(); ++c) {
            QPainterPath path;
            QPainterPath linePath;
            path.moveTo(0, h);
            
            bool first = true;
            for (int px = 0; px < w; ++px) {
                double normH = m_resampledBins[c][px] / m_maxVal;
                float py = h - (normH * h);
                path.lineTo(px, py);
                if (first) { linePath.moveTo(px, py); first = false; }
                else { linePath.lineTo(px, py); }
            }
            path.lineTo(w, h);
            path.closeSubpath();
            
            QColor col = colors[c % 3];
            col.setAlpha(60);
            painter.setBrush(col);
            painter.setPen(Qt::NoPen);
            painter.drawPath(path);
            
            col.setAlpha(200);
            painter.setPen(QPen(col, 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(linePath);
        }
    }
    
    // Draw Transform Curve
    if (m_showCurve) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setPen(QPen(QColor(100, 100, 100), 1));
        painter.drawLine(0, h, w, 0);
        if (!m_lut.empty()) {
            painter.setPen(QPen(QColor(255, 150, 100), 2));
            QPainterPath path;
            bool first = true;
            int lutSize = m_lut.size();
            for (int i = 0; i < w; ++i) {
                float t = (float)i / (float)(w - 1);
                int lutIdx = std::clamp(static_cast<int>(t * (lutSize - 1)), 0, lutSize - 1);
                float py = h - std::clamp(m_lut[lutIdx], 0.0f, 1.0f) * h;
                if (first) { path.moveTo(i, py); first = false; }
                else { path.lineTo(i, py); }
            }
            painter.drawPath(path);
        }
    }
}

