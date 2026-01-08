#ifndef GHSDIALOG_H
#define GHSDIALOG_H

#include "DialogBase.h"
#include <vector>
#include "ImageBuffer.h"

class QDoubleSpinBox;
class QSlider;
class QComboBox;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QToolButton;
class HistogramWidget;
class QScrollArea;
class MainWindowCallbacks;
class ImageViewer; // Forward declaration at global scope

#include <QPointer>
class GHSDialog : public DialogBase {
    Q_OBJECT
public:
    explicit GHSDialog(QWidget *parent = nullptr);
    ~GHSDialog();
    
    // Override reject (Close/Esc) to restore original image
    void reject() override;
    
    void setHistogramData(const std::vector<std::vector<int>>& bins, int channels);
    void setSymmetryPoint(double sp);
    void setClippingStats(float lowPct, float highPct);
    
    // Context Switching
    void setTarget(ImageViewer* viewer); // Updates active viewer and original buffer
    void setInteractionEnabled(bool enabled);    

    struct State {
        double d, b, sp, lp, hp, bp;
        int mode, colorMode, clipMode;
        bool channels[3];
        bool logScale;
        int sliderD, sliderB, sliderSP, sliderLP, sliderHP, sliderBP;
        // Zoom handled separately? No, zoom is purely UI.
    };
    State getState() const;
    void setState(const State& s);
    void resetState(); // Helper to reset to defaults

    ImageBuffer::GHSParams getParams() const;

signals:
    void apply(const ImageBuffer::GHSParams& params);
    void previewLUT(const std::vector<std::vector<float>>& luts);
    void previewRequested(const ImageBuffer::GHSParams& params);
    void resetRequested();
    void undoRequested();
    void redoRequested();
    void applied(const QString& msg);

public slots:
    void onReset(); // Public so MainWindow can call after apply
    void resetIntensity(); // Partial reset for iterative workflow

private slots:
    void onApply();
    void onValueChange();
    void onPreviewTrigger(); // For expensive full preview
    void onZoomChanged();
    void onChannelToggled();

private:
    void setupUI();
    void connectSignals();
    void updateHistogram();
    
    HistogramWidget* m_histWidget;
    
    // Mode Selection
    QComboBox* m_modeCombo;
    QComboBox* m_colorCombo;
    QComboBox* m_clipModeCombo;
    
    // Parameters (Standard defaults)
    // D: Stretch Factor - Range 0-10, Default 0, Step 0.01
    QDoubleSpinBox* m_dSpin;
    QSlider* m_dSlider;
    
    // B: Local Intensity - Range -5 to 15, Default 0, Step 0.01
    QDoubleSpinBox* m_bSpin;
    QSlider* m_bSlider;
    
    // SP: Symmetry Point - Range 0-1, Default 0, Step 0.001
    QDoubleSpinBox* m_spSpin;
    QSlider* m_spSlider;
    
    // LP: Low Protection - Range 0-1, Default 0, Step 0.001  
    QDoubleSpinBox* m_lpSpin;
    QSlider* m_lpSlider;
    
    // HP: High Protection - Range 0-1, Default 1, Step 0.001
    QDoubleSpinBox* m_hpSpin;
    QSlider* m_hpSlider;
    
    // BP: Black Point - Range 0-1, Default 0, Step 0.001
    QDoubleSpinBox* m_bpSpin;
    QSlider* m_bpSlider;
    
    // Histogram Controls
    QToolButton* m_zoomInBtn;
    QToolButton* m_zoomOutBtn;
    QToolButton* m_zoomResetBtn;
    class QLabel* m_zoomLabel;
    QLabel* m_lowClipLabel;
    QLabel* m_highClipLabel;
    QCheckBox* m_logScaleCheck;
    QToolButton* m_redBtn;
    QToolButton* m_greenBtn;
    QToolButton* m_blueBtn;
    QToolButton* m_gridBtn;
    QToolButton* m_curveBtn;
    QCheckBox* m_previewCheck;
    QScrollArea* m_scrollArea;
    
    // State
    std::vector<std::vector<int>> m_origBins;
    int m_channels = 3;
    int m_zoomLevel = 1; // Histogram zoom level
    bool m_showRed = true, m_showGreen = true, m_showBlue = true;
    bool m_showGrid = true, m_showCurve = true;
    
    // Throttling for live preview
    class QTimer* m_previewTimer;
    bool m_previewPending;
    
    // Active Context
    ImageBuffer m_originalBuffer; // Setup on target switch or apply
    QPointer<ImageViewer> m_activeViewer;
    bool m_selfUpdating = false; // Guard against recursive buffer updates
    bool m_interactionEnabled = false;
    bool m_applied = false;
};

#endif // GHSDIALOG_H
