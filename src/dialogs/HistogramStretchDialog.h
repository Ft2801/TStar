#ifndef HISTOGRAMSTRETCHDIALOG_H
#define HISTOGRAMSTRETCHDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QToolButton>
#include "ImageBuffer.h"
#include "ImageViewer.h"
#include "widgets/HistogramWidget.h"

#include <QPointer>

class HistogramStretchDialog : public QDialog {
    Q_OBJECT

public:
    explicit HistogramStretchDialog(ImageViewer* viewer, QWidget* parent = nullptr);
    ~HistogramStretchDialog();
    
    void setViewer(ImageViewer* v);

protected:
    void reject() override;  // Handle close/cancel

private slots:
    void onShadowsChanged();
    void onMidtonesChanged();
    void onHighlightsChanged();
    void onSliderValueChanged();  // Real-time histogram update
    void onSliderReleased();      // Image preview update
    void onChannelToggled();
    void onPreviewToggled(bool checked);
    void onAutoStretch();
    void onReset();
    void onApply();

private:
    void setupUI();
    void updatePreview();
    void updateHistogramOnly();    // Update histogram without image preview
    void updateClippingStats(const ImageBuffer& buffer);
    void applyMTF(ImageBuffer& buffer, float shadows, float midtones, float highlights,
                  bool doRed, bool doGreen, bool doBlue);
    void computeAutostretch(const ImageBuffer& buffer, float& shadows, float& midtones, float& highlights);
    float MTF(float x, float m, float lo, float hi);
    
    // Statistics helpers
    float computeMedian(const float* data, size_t n);
    float computeMAD(const float* data, size_t n, float median);
    
    QPointer<ImageViewer> m_viewer;
    ImageBuffer m_backup;
    bool m_applied = false;
    
    // UI elements
    HistogramWidget* m_histogram;
    QSlider* m_shadowsSlider;
    QSlider* m_midtonesSlider;
    QSlider* m_highlightsSlider;
    QDoubleSpinBox* m_shadowsSpin;
    QDoubleSpinBox* m_midtonesSpin;
    QDoubleSpinBox* m_highlightsSpin;
    QToolButton* m_redBtn;
    QToolButton* m_greenBtn;
    QToolButton* m_blueBtn;
    QCheckBox* m_previewCheck;
    QPushButton* m_autoStretchBtn;
    QLabel* m_lowClipLabel;
    QLabel* m_highClipLabel;
    
    // Parameters
    float m_shadows = 0.0f;
    float m_midtones = 0.5f;
    float m_highlights = 1.0f;
    bool m_doRed = true;
    bool m_doGreen = true;
    bool m_doBlue = true;
    
    // Caching for performance
    std::vector<std::vector<int>> m_baseHist;
};

#endif // HISTOGRAMSTRETCHDIALOG_H
