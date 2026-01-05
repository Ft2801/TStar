#ifndef EXTRACTLUMINANCEDIALOG_H
#define EXTRACTLUMINANCEDIALOG_H

#include <QDialog>
#include <vector>
#include "../ImageBuffer.h"

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QCheckBox;
class MainWindow;

class ExtractLuminanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExtractLuminanceDialog(QWidget* parent = nullptr);
    
    struct Params {
        int methodIndex; // Maps to LumaMethod
        std::vector<float> customWeights;
        std::vector<float> customNoiseSigma;
        bool autoEstimateNoise;
    };
    
    Params getParams() const;

private slots:
    void onMethodChanged(int index);
    void onApply();

private:
    QComboBox* m_methodCombo;
    
    // Custom Weights
    QGroupBox* m_customGroup;
    QDoubleSpinBox* m_weightR;
    QDoubleSpinBox* m_weightG;
    QDoubleSpinBox* m_weightB;
    
    // SNR Settings
    QGroupBox* m_snrGroup;
    QCheckBox* m_autoNoiseCheck;
    QDoubleSpinBox* m_sigmaR;
    QDoubleSpinBox* m_sigmaG;
    QDoubleSpinBox* m_sigmaB;
    
    MainWindow* m_mainWindow;
};

#endif // EXTRACTLUMINANCEDIALOG_H
