#ifndef CONTINUUM_SUBTRACTION_DIALOG_H
#define CONTINUUM_SUBTRACTION_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QProgressBar>

class ImageViewer;
class MainWindow;

class ContinuumSubtractionDialog : public QDialog {
    Q_OBJECT
public:
    explicit ContinuumSubtractionDialog(MainWindow* parent);

    void setViewer(ImageViewer* v);
    void refreshImageList();

private slots:
    void onApply();
    void onQFactorChanged(double val);

private:
    MainWindow* m_mainWindow;
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
