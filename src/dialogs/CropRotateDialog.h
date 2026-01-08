#ifndef CROPROTATEDIALOG_H
#define CROPROTATEDIALOG_H

#include "DialogBase.h"
#include <QDoubleSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QPointer>

class CropRotateDialog : public DialogBase {
    Q_OBJECT
public:
    explicit CropRotateDialog(QWidget* parent = nullptr);
    ~CropRotateDialog(); // Clean up crop mode
    
    void setViewer(class ImageViewer* v);

private slots:
    void onApply();
    void onRotationChanged();
    void onRatioChanged();

private:
    QDoubleSpinBox* m_angleSpin;
    QSlider* m_angleSlider;
    QComboBox* m_aspectCombo;
    
    QPointer<class ImageViewer> m_viewer;
};

#endif // CROPROTATEDIALOG_H
