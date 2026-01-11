/**
 * @file SimplePlotWidget.h
 * @brief Lightweight plotting widget for analysis graphs
 * 
 * Provides a simple line graph visualization for sequence metrics
 * (FWHM, Roundness, etc.) using QPainter.
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef SIMPLE_PLOT_WIDGET_H
#define SIMPLE_PLOT_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QString>
#include <QColor>
#include <QPointF>

class SimplePlotWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit SimplePlotWidget(QWidget* parent = nullptr);
    
    struct DataPoint {
        double x;
        double y;
        bool selected;
    };
    
    void setData(const QVector<double>& x, const QVector<double>& y);
    void setTitle(const QString& title);
    void setAxisLabels(const QString& xLabel, const QString& yLabel);
    void setSelection(const QVector<int>& selectedIndices);
    
    // Configuration
    void setColor(const QColor& color) { m_lineColor = color; update(); }
    void setShowPoints(bool show) { m_showPoints = show; update(); }
    
signals:
    void pointSelected(int index);
    void selectionChanged(const QVector<int>& indices);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    
private:
    QPointF dataToScreen(const QPointF& point) const;
    QPointF screenToData(const QPoint& point) const;
    void updateRange();
    
    QVector<DataPoint> m_data;
    QString m_title;
    QString m_xLabel;
    QString m_yLabel;
    
    double m_minX = 0, m_maxX = 0;
    double m_minY = 0, m_maxY = 0;
    
    // Layout geometry
    int m_marginLeft = 50;
    int m_marginRight = 20;
    int m_marginTop = 30;
    int m_marginBottom = 40;
    
    QColor m_lineColor = QColor(0, 120, 215);
    QColor m_pointColor = QColor(255, 255, 255);
    QColor m_selectColor = QColor(255, 0, 0);
    
    bool m_showPoints = true;
    int m_highlightIndex = -1;
};

#endif // SIMPLE_PLOT_WIDGET_H
