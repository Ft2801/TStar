/**
 * @file SimplePlotWidget.cpp
 * @brief Implementation of SimplePlotWidget
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "SimplePlotWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QtMath>
#include <algorithm>
#include <limits>

SimplePlotWidget::SimplePlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
    setMouseTracking(true); // For hover effects
}

void SimplePlotWidget::setData(const QVector<double>& x, const QVector<double>& y) {
    m_data.clear();
    if (x.size() != y.size()) return;
    
    m_data.reserve(x.size());
    for (int i = 0; i < x.size(); ++i) {
        m_data.append({x[i], y[i], false});
    }
    
    updateRange();
    update();
}

void SimplePlotWidget::setTitle(const QString& title) {
    m_title = title;
    update();
}

void SimplePlotWidget::setAxisLabels(const QString& xLabel, const QString& yLabel) {
    m_xLabel = xLabel;
    m_yLabel = yLabel;
    update();
}

void SimplePlotWidget::setSelection(const QVector<int>& selectedIndices) {
    for (auto& p : m_data) p.selected = false;
    for (int idx : selectedIndices) {
        if (idx >= 0 && idx < m_data.size()) {
            m_data[idx].selected = true;
        }
    }
    update();
}

void SimplePlotWidget::updateRange() {
    if (m_data.empty()) {
        m_minX = m_maxX = m_minY = m_maxY = 0;
        return;
    }
    
    m_minX = std::numeric_limits<double>::max();
    m_maxX = std::numeric_limits<double>::lowest();
    m_minY = std::numeric_limits<double>::max();
    m_maxY = std::numeric_limits<double>::lowest();
    
    for (const auto& p : m_data) {
        if (p.x < m_minX) m_minX = p.x;
        if (p.x > m_maxX) m_maxX = p.x;
        if (p.y < m_minY) m_minY = p.y;
        if (p.y > m_maxY) m_maxY = p.y;
    }
    
    // Add padding
    double rangeX = (m_maxX - m_minX);
    double rangeY = (m_maxY - m_minY);
    
    if (rangeX <= 1e-6) rangeX = 1.0;
    if (rangeY <= 1e-6) rangeY = 1.0;
    
    m_minX -= rangeX * 0.05;
    m_maxX += rangeX * 0.05;
    m_minY -= rangeY * 0.1;
    m_maxY += rangeY * 0.1;
}

QPointF SimplePlotWidget::dataToScreen(const QPointF& point) const {
    double effW = width() - m_marginLeft - m_marginRight;
    double effH = height() - m_marginTop - m_marginBottom;
    
    double dx = (point.x() - m_minX) / (m_maxX - m_minX);
    double dy = (point.y() - m_minY) / (m_maxY - m_minY);
    
    return QPointF(m_marginLeft + dx * effW,
                   height() - m_marginBottom - dy * effH);
}

void SimplePlotWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    // Background
    p.fillRect(rect(), QColor(40, 40, 40));
    
    // Plot area
    QRect plotRect(m_marginLeft, m_marginTop, 
                   width() - m_marginLeft - m_marginRight,
                   height() - m_marginTop - m_marginBottom);
    p.fillRect(plotRect, QColor(30, 30, 30));
    p.setPen(QColor(60, 60, 60));
    p.drawRect(plotRect);
    
    if (m_data.empty()) return;
    
    // Grid and Axes
    p.setPen(QColor(60, 60, 60));
    p.drawLine(plotRect.left(), plotRect.bottom(), plotRect.right(), plotRect.bottom());
    p.drawLine(plotRect.left(), plotRect.top(), plotRect.left(), plotRect.bottom());
    
    // Draw Data Line
    if (m_data.size() > 1) {
        QPainterPath path;
        path.moveTo(dataToScreen(QPointF(m_data[0].x, m_data[0].y)));
        
        for (int i = 1; i < m_data.size(); ++i) {
            path.lineTo(dataToScreen(QPointF(m_data[i].x, m_data[i].y)));
        }
        
        p.setPen(QPen(m_lineColor, 2));
        p.drawPath(path);
    }
    
    // Draw Points
    if (m_showPoints) {
        for (int i = 0; i < m_data.size(); ++i) {
            QPointF pos = dataToScreen(QPointF(m_data[i].x, m_data[i].y));
            
            QColor c = m_data[i].selected ? m_selectColor : m_pointColor;
            int radius = (m_data[i].selected || i == m_highlightIndex) ? 4 : 2;
            
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawEllipse(pos, radius, radius);
        }
    }
    
    // Title and Labels
    p.setPen(Qt::white);
    p.drawText(rect().adjusted(0, 5, 0, 0), Qt::AlignTop | Qt::AlignHCenter, m_title);
    
    // Y Axis Text (Min/Max)
    p.drawText(QRect(0, m_marginTop, m_marginLeft - 5, 20), 
               Qt::AlignRight | Qt::AlignVCenter, QString::number(m_maxY, 'f', 2));
    p.drawText(QRect(0, height() - m_marginBottom - 20, m_marginLeft - 5, 20), 
               Qt::AlignRight | Qt::AlignVCenter, QString::number(m_minY, 'f', 2));

    // X Axis Text
    p.drawText(QRect(m_marginLeft, height() - m_marginBottom, 40, 20), 
               Qt::AlignLeft | Qt::AlignTop, QString::number(m_minX, 'f', 0));
    p.drawText(QRect(width() - m_marginRight - 40, height() - m_marginBottom, 40, 20), 
               Qt::AlignRight | Qt::AlignTop, QString::number(m_maxX, 'f', 0));
}

void SimplePlotWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_data.empty()) return;
    
    // Find closest point
    double minDist = 100.0; // Max hover distance
    int closest = -1;
    
    for (int i = 0; i < m_data.size(); ++i) {
        QPointF pos = dataToScreen(QPointF(m_data[i].x, m_data[i].y));
        double dist = std::hypot(pos.x() - event->pos().x(), pos.y() - event->pos().y());
        if (dist < minDist) {
            minDist = dist;
            closest = i;
        }
    }
    
    if (closest != m_highlightIndex) {
        m_highlightIndex = closest;
        update();
        if (closest != -1) {
            setToolTip(QString("Frame %1: %2").arg(closest + 1).arg(m_data[closest].y));
        } else {
            setToolTip("");
        }
    }
}

void SimplePlotWidget::mousePressEvent(QMouseEvent* event) {
    if (m_highlightIndex != -1) {
        emit pointSelected(m_highlightIndex);
    }
    QWidget::mousePressEvent(event);
}
