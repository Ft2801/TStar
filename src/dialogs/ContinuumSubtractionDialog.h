#ifndef CONTINUUM_SUBTRACTION_DIALOG_H
#define CONTINUUM_SUBTRACTION_DIALOG_H

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include "DialogBase.h"

class ImageViewer;
class MainWindowCallbacks;

class ContinuumSubtractionDialog : public DialogBase {
    Q_OBJECT
public:
    explicit ContinuumSubtractionDialog(QWidget* parent = nullptr);

    void setViewer(ImageViewer* v);
    void refreshImageList();

private slots:
    void onApply();
    void onQFactorChanged(double val);

private:
    MainWindowCallbacks* m_mainWindow;
    ImageViewer* m_viewer = nullptr;
    
    QComboBox* m_narrowbandCombo;
    QComboBox* m_continuumCombo;
    QDoubleSpinBox* m_qFactorSpin;
    QSlider* m_qFactorSlider;
    QCheckBox* m_outputLinearCheck;
    QLabel* m_statusLabel;
    QProgressBar* m_progress;
    QPushButton* m_applyBtn;
};

#endif // CONTINUUM_SUBTRACTION_DIALOG_H
