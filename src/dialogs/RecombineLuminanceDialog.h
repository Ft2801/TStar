#ifndef RECOMBINELUMINANCEDIALOG_H
#define RECOMBINELUMINANCEDIALOG_H

#include "DialogBase.h"
#include <vector>

class MainWindowCallbacks;
class ImageViewer;
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
        int sourceWindowId;
        int colorSpaceIndex; // Maps to ChannelOps::ColorSpaceMode
        float blend;
    };

    // Helper to refresh source image list
    void refreshSourceList();

private slots:
    void onApply();
    void updateBlendLabel(int val);

private:
    MainWindowCallbacks* m_mainWindow;
    
    QComboBox* m_sourceCombo;
    QComboBox* m_colorSpaceCombo;
    
    QSlider* m_blendSlider;
    QLabel* m_blendLabel;
};

#endif // RECOMBINELUMINANCEDIALOG_H
