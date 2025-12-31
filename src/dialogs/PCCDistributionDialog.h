#ifndef PCCDISTRIBUTIONDIALOG_H
#define PCCDISTRIBUTIONDIALOG_H

#include <QDialog>
#include <vector>
#include "photometry/PCCCalibrator.h"

class PCCDistributionDialog : public QDialog {
    Q_OBJECT
public:
    explicit PCCDistributionDialog(const PCCResult& result, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PCCResult m_result;
    
    void drawScatterPlot(QPainter& p, const QRect& rect, 
                         const std::vector<double>& xData, 
                         const std::vector<double>& yData,
                         double slope, double intercept,
                         const QString& title, const QColor& color);
};

#endif // PCCDISTRIBUTIONDIALOG_H
