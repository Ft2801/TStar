#ifndef NARROWBAND_NORMALIZATION_DIALOG_H
#define NARROWBAND_NORMALIZATION_DIALOG_H

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QTimer>
#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"

class ImageViewer;
class MainWindowCallbacks;

// ============================================================================
// Narrowband Normalization Dialog
// ============================================================================
class NarrowbandNormalizationDialog : public DialogBase {
    Q_OBJECT
public:
    explicit NarrowbandNormalizationDialog(QWidget* parent = nullptr);
    ~NarrowbandNormalizationDialog();

    void setViewer(ImageViewer* v);
    void refreshImageList();

private slots:
    void onLoadChannel(const QString& which);
    void onImportMappedView(const QString& scenario);
    void onClear();
    void onPreview();
    void onApply();
    void onPushNew();
    void onScenarioChanged();
    void onModeChanged();
    void schedulePreview();

private:
    void buildUI();
    void initWidgets();
    void connectSignals();
    void refreshVisibility();

    // Slider + SpinBox row helper
    struct SliderSpinRow {
        QWidget*        widget;
        QDoubleSpinBox* spin;
        QSlider*        slider;
    };
    SliderSpinRow createSliderSpinRow(double lo, double hi, double step,
                                      double val, int decimals);

    // Gather UI params
    ChannelOps::NBNParams gatherParams() const;

    // Image loading
    void loadFromViewer(const QString& which);
    void loadFromFile(const QString& which);
    void setStatusLabel(const QString& which, const QString& text);

    // Computed result
    void computeAndDisplay(bool fit = false);
    QPixmap floatToPixmap(const std::vector<float>& img, int w, int h, int ch);

    // Viewer
    ImageViewer* m_viewer = nullptr;
    MainWindowCallbacks* m_mainWindow = nullptr;

    // Raw channels (mono float32 [0,1], w*h)
    std::vector<float> m_ha, m_oiii, m_sii;
    int m_chW = 0, m_chH = 0;

    // Result
    std::vector<float> m_result; // RGB interleaved

    // Debounce timer
    QTimer* m_debounce = nullptr;

    // --- Import buttons ---
    QPushButton* m_btnImpSHO = nullptr;
    QPushButton* m_btnImpHSO = nullptr;
    QPushButton* m_btnImpHOS = nullptr;
    QPushButton* m_btnImpHOO = nullptr;

    // --- Channel load buttons + labels ---
    QPushButton* m_btnHa = nullptr;
    QPushButton* m_btnOIII = nullptr;
    QPushButton* m_btnSII = nullptr;
    QPushButton* m_btnOSC1 = nullptr;
    QPushButton* m_btnOSC2 = nullptr;

    QLabel* m_lblHa = nullptr;
    QLabel* m_lblOIII = nullptr;
    QLabel* m_lblSII = nullptr;
    QLabel* m_lblOSC1 = nullptr;
    QLabel* m_lblOSC2 = nullptr;

    // --- Normalization controls ---
    QComboBox* m_cmbScenario = nullptr;
    QComboBox* m_cmbMode = nullptr;
    QComboBox* m_cmbLightness = nullptr;

    SliderSpinRow m_rowBlackpoint;
    SliderSpinRow m_rowHLrecover;
    SliderSpinRow m_rowHLreduct;
    SliderSpinRow m_rowBrightness;
    SliderSpinRow m_rowHaBlend;
    SliderSpinRow m_rowOIIIboost;
    SliderSpinRow m_rowSIIboost;
    SliderSpinRow m_rowOIIIboost2;

    QComboBox* m_cmbBlendmode = nullptr;
    QCheckBox* m_chkSCNR = nullptr;

    // Labels (for show/hide)
    QLabel* m_lblBlackpoint = nullptr;
    QLabel* m_lblHLrecover = nullptr;
    QLabel* m_lblHLreduct = nullptr;
    QLabel* m_lblBrightness = nullptr;
    QLabel* m_lblBlendmode = nullptr;
    QLabel* m_lblHaBlend = nullptr;
    QLabel* m_lblOIIIboostLabel = nullptr;
    QLabel* m_lblSIIboostLabel = nullptr;
    QLabel* m_lblOIIIboost2Label = nullptr;

    QFormLayout* m_normForm = nullptr;

    // --- Action buttons ---
    QPushButton* m_btnClear = nullptr;
    QPushButton* m_btnPreview = nullptr;
    QPushButton* m_btnApply = nullptr;
    QPushButton* m_btnPush = nullptr;

    // --- Preview ---
    QGraphicsView* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_pixBase = nullptr;

    QLabel* m_status = nullptr;
};

#endif // NARROWBAND_NORMALIZATION_DIALOG_H
