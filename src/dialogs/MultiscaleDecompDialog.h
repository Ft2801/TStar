#ifndef MULTISCALE_DECOMP_DIALOG_H
#define MULTISCALE_DECOMP_DIALOG_H

#include <QVBoxLayout>
#include "../MainWindowCallbacks.h"
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QScrollBar>
#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"

class ImageViewer;

// ============================================================================
// MultiscaleDecompDialog — Multiscale Decomposition Tool
// ============================================================================
class MultiscaleDecompDialog : public DialogBase {
    Q_OBJECT
public:
    explicit MultiscaleDecompDialog(QWidget* parent = nullptr);
    ~MultiscaleDecompDialog();

    void setViewer(ImageViewer* v);

private slots:
    void onLayersChanged(int val);
    void onSigmaChanged(double val);
    void onModeChanged(int idx);
    void onPreviewComboChanged(int idx);
    void onTableSelectionChanged();
    void onTableItemChanged(QTableWidgetItem* item);
    void onLayerEditorChanged();
    void onGainSliderChanged(int v);
    void onThrSliderChanged(int v);
    void onAmtSliderChanged(int v);
    void onDenoiseSliderChanged(int v);
    void onApplyToImage();
    void onSendToNewImage();
    void rebuildPreview();

private:
    void buildUI();
    void recomputeDecomp(bool force = false);
    void syncCfgsAndUI();
    void rebuildTable();
    void refreshPreviewCombo();
    void loadLayerIntoEditor(int idx);
    void updateParamWidgetsForMode();
    void schedulePreview();

    // Convert float image to QPixmap
    QPixmap floatToPixmap(const std::vector<float>& img, int w, int h, int ch);

    // Build tuned layers from current settings
    void buildTunedLayers(std::vector<std::vector<float>>& tuned,
                          std::vector<float>& residual);

    ImageViewer* m_viewer = nullptr;

    // Original image data (float32 [0,1], always 3ch for display)
    std::vector<float> m_image;
    int m_imgW = 0, m_imgH = 0, m_imgCh = 3;
    bool m_origMono = false;
    int m_origCh = 0;

    // Decomposition cache
    std::vector<std::vector<float>> m_cachedDetails;
    std::vector<float> m_cachedResidual;
    std::vector<float> m_layerNoise;
    float m_cachedSigma = -1.0f;
    int m_cachedLayers = -1;

    // Per-layer configs
    int m_layers = 4;
    float m_baseSigma = 1.0f;
    std::vector<ChannelOps::LayerCfg> m_cfgs;
    bool m_residualEnabled = true;

    // Selected layer in table
    int m_selectedLayer = -1;

    // Preview debounce
    QTimer* m_previewTimer = nullptr;

    // --- UI Widgets ---
    // Left: preview
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    QGraphicsPixmapItem* m_pixBase = nullptr;

    // Right: controls
    QSpinBox* m_spinLayers = nullptr;
    QDoubleSpinBox* m_spinSigma = nullptr;
    QCheckBox* m_cbLinkedRGB = nullptr;
    QComboBox* m_comboMode = nullptr;
    QComboBox* m_comboPreview = nullptr;

    QTableWidget* m_table = nullptr;

    // Per-layer editor
    QLabel* m_lblSel = nullptr;
    QDoubleSpinBox* m_spinGain = nullptr;
    QDoubleSpinBox* m_spinThr = nullptr;
    QDoubleSpinBox* m_spinAmt = nullptr;
    QDoubleSpinBox* m_spinDenoise = nullptr;
    QSlider* m_sliderGain = nullptr;
    QSlider* m_sliderThr = nullptr;
    QSlider* m_sliderAmt = nullptr;
    QSlider* m_sliderDenoise = nullptr;

    MainWindowCallbacks* m_mainWindow = nullptr;

    QPushButton* m_btnApply = nullptr;
    QPushButton* m_btnNewDoc = nullptr;
    QPushButton* m_btnClose = nullptr;
};

#endif // MULTISCALE_DECOMP_DIALOG_H
