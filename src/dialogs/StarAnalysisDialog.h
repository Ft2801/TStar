#ifndef STARANALYSISDIALOG_H
#define STARANALYSISDIALOG_H

#include <QDialog>
#include <QThread>
#include <vector>
#include "../ImageBuffer.h"

class QTableWidget;
class QLabel;
class QSlider;
class QPushButton;
class QCheckBox;

// Histogram Widget
class StarHistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit StarHistogramWidget(QWidget* parent = nullptr);
    void setData(const std::vector<float>& data, const QString& label, bool logScale);
    void setLogScale(bool log);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> m_data;
    QString m_label;
    bool m_logScale = false;
};

// Worker
class StarAnalysisWorker : public QThread {
    Q_OBJECT
public:
    StarAnalysisWorker(QObject* parent = nullptr);
    void setup(const ImageBuffer& src, float threshold);
    void run() override;

signals:
    void finished(std::vector<ImageBuffer::DetectedStar> stars);
    void failed(QString msg);

private:
    ImageBuffer m_src;
    float m_threshold;
};


#include <QPointer>
#include "../ImageViewer.h"

// ... (Classes)

// Main Dialog
#include "DialogBase.h"

class StarAnalysisDialog : public DialogBase {
    Q_OBJECT
public:
    explicit StarAnalysisDialog(QWidget* parent = nullptr, ImageViewer* viewer = nullptr);
    ~StarAnalysisDialog();
    void setViewer(ImageViewer* v);

signals:
    void statusMsg(const QString& msg);

private slots:
    void onRunClicked(); // Threshold change triggers this (debounced)
    void onThresholdChanged();
    void onWorkerFinished(std::vector<ImageBuffer::DetectedStar> stars);
    void toggleLog(bool checked);
    void toggleMode(); // HFR <-> Flux

private:
    void createUI();
    void updateStats();
    void updateHistogram();

    QPointer<ImageViewer> m_viewer;
    std::vector<ImageBuffer::DetectedStar> m_stars;
    
    StarAnalysisWorker* m_worker;
    
    // UI
    StarHistogramWidget* m_histWidget;
    QTableWidget* m_statsTable;
    
    QSlider* m_threshSlider;
    QLabel* m_threshLabel;
    
    QPushButton* m_modeBtn;
    QCheckBox* m_logCheck;
    QLabel* m_statusLabel;
    
    QTimer* m_debounceTimer;
    
    enum Mode { Mode_HFR, Mode_Flux };
    Mode m_mode = Mode_HFR;
    bool m_logScale = false;
};

#endif // STARANALYSISDIALOG_H
