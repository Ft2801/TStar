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

void HistogramWidget::setData(const std::vector<std::vector<int>>& bins, int channels) {
    m_bins = bins;
    m_channels = channels;
    m_cachedWidth = -1;  // Invalidate cache when data changes
    update();
}

void HistogramWidget::setGhostData(const std::vector<std::vector<int>>& bins, int channels) {
    m_ghostBins = bins;
    m_ghostChannels = channels;
    m_cachedWidth = -1;  // Invalidate cache when data changes
    update();
}

void HistogramWidget::setLogScale(bool enabled) {
    if (m_logScale == enabled) return;
    m_logScale = enabled;
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
    m_channels = 0;
    m_ghostChannels = 0;
    m_lut.clear();
    update();
}

// Helper function to compute aggregated histogram values for display width
static void computeDisplayHistogram(const std::vector<std::vector<int>>& srcBins,
                                     int srcChannels,
                                     int displayWidth,
                                     std::vector<std::vector<float>>& dst,
                                     double& maxVal,
                                     bool logScale) {
    dst.clear();
    maxVal = 0.0;
    
    if (srcBins.empty() || srcChannels <= 0 || displayWidth <= 0) {
        return;
    }
    
    int numBins = srcBins[0].size();
    if (numBins == 0) return;
    
    dst.assign(srcChannels, std::vector<float>(displayWidth, 0.0f));
    
    // bins_per_px = how many source bins correspond to 1 pixel width
    float binsPerPx = (float)numBins / (float)displayWidth;
    
    for (int c = 0; c < srcChannels && c < (int)srcBins.size(); ++c) {
        int binIdx = 0;
        for (int px = 0; px < displayWidth; ++px) {
            double sum = 0.0;
            // Accumulate all source bins that contribute to this pixel
            while (binIdx < numBins && (float)binIdx / binsPerPx <= (float)px + 0.5f) {
                sum += (double)srcBins[c][binIdx];
                binIdx++;
            }
            // Apply log scale if requested
            if (logScale && sum > 0.0) {
                sum = std::log(sum);
            }
            dst[c][px] = (float)sum;
            if (dst[c][px] > maxVal) {
                maxVal = dst[c][px];
            }
        }
    }
}

void HistogramWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    int w = width();
    int h = height();
    
    if (w <= 0 || h <= 0) return;
    
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
            if (std::abs(std::fmod(x, 0.25f)) > 0.01f) {
                int px = static_cast<int>(x * w);
                painter.drawLine(px, 0, px, h);
            }
        }
    }
    
    // Recompute display histograms only if width changed
    if (w != m_cachedWidth) {
        m_cachedWidth = w;
        // Compute ghost histogram
        computeDisplayHistogram(m_ghostBins, m_ghostChannels, w, m_cachedDisplayGhostBins, m_cachedGhostMaxVal, m_logScale);
        // Compute main histogram
        computeDisplayHistogram(m_bins, m_channels, w, m_cachedDisplayBins, m_cachedMaxVal, m_logScale);
    }
    
    // Draw cached ghost histogram
    if (!m_cachedDisplayGhostBins.empty() && m_cachedGhostMaxVal > 0.0) {
        QColor ghostColor(100, 100, 100, 120);
        painter.setPen(QPen(ghostColor, 1, Qt::DotLine));
        
        for (int c = 0; c < (int)m_cachedDisplayGhostBins.size(); ++c) {
            QPainterPath path;
            bool first = true;
            for (int px = 0; px < w; ++px) {
                double normH = m_cachedDisplayGhostBins[c][px] / m_cachedGhostMaxVal;
                float py = h - (normH * h);
                if (first) {
                    path.moveTo(px, py);
                    first = false;
                } else {
                    path.lineTo(px, py);
                }
            }
            painter.drawPath(path);
        }
    }
    
    // Draw cached main histogram
    if (!m_cachedDisplayBins.empty() && m_cachedMaxVal > 0.0) {
        QColor colors[3] = { QColor(255, 80, 80), QColor(80, 255, 80), QColor(80, 80, 255) };
        if (m_channels == 1) colors[0] = Qt::white;
        
        for (int c = 0; c < (int)m_cachedDisplayBins.size(); ++c) {
            QPainterPath path;
            path.moveTo(0, h);
            
            bool first = true;
            for (int px = 0; px < w; ++px) {
                double normH = m_cachedDisplayBins[c][px] / m_cachedMaxVal;
                float py = h - (normH * h);
                path.lineTo(px, py);
            }
            path.lineTo(w, h);
            path.closeSubpath();
            
            QColor col = colors[c % 3];
            col.setAlpha(60);
            painter.setBrush(col);
            painter.setPen(Qt::NoPen);
            painter.drawPath(path);
            
            // Draw outline
            QPainterPath outline;
            outline.moveTo(0, h);
            first = true;
            for (int px = 0; px < w; ++px) {
                double normH = m_cachedDisplayBins[c][px] / m_cachedMaxVal;
                float py = h - (normH * h);
                if (first) {
                    outline.moveTo(px, py);
                    first = false;
                } else {
                    outline.lineTo(px, py);
                }
            }
            
            col.setAlpha(200);
            painter.setPen(QPen(col, 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(outline);
        }
    }
    
    // Draw Transform Curve
    if (m_showCurve) {
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
                if (first) {
                    path.moveTo(i, py);
                    first = false;
                } else {
                    path.lineTo(i, py);
                }
            }
            painter.drawPath(path);
        }
    }
}

