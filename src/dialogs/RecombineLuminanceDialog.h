#ifndef RECOMBINELUMINANCEDIALOG_H
#define RECOMBINELUMINANCEDIALOG_H

#include "DialogBase.h"
#include <vector>

class MainWindowCallbacks;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QCheckBox;
class QSlider;
class QLabel;

class RecombineLuminanceDialog : public DialogBase {
    Q_OBJECT
public:
    explicit RecombineLuminanceDialog(QWidget* parent = nullptr);
    
    struct Params {
        int sourceWindowId; // Index in the combo or ptr ID? Better use index.
        int methodIndex;
        float blend;
        float softKnee;
        std::vector<float> customWeights;
        bool autoEstimateNoise; // For SNR
        std::vector<float> customNoiseSigma; // For SNR
    };

    // Helper to refresh source image list
    void refreshSourceList();

private slots:
    void onMethodChanged(int index);
    void onApply();
    void updateBlendLabel(int val);

private:
    MainWindowCallbacks* m_mainWindow;
    
    QComboBox* m_sourceCombo;
    QComboBox* m_methodCombo;
    
    QSlider* m_blendSlider;
    QLabel* m_blendLabel;
    
    QDoubleSpinBox* m_softKneeSpin;
    
    // Custom Weights
    QGroupBox* m_customGroup;
    QDoubleSpinBox* m_weightR;
    QDoubleSpinBox* m_weightG;
    QDoubleSpinBox* m_weightB;
    
    // SNR Settings
    QCheckBox* m_autoNoiseCheck;
};

#endif // RECOMBINELUMINANCEDIALOG_H
