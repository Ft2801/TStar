#ifndef IMAGEBLENDINGDIALOG_H
#define IMAGEBLENDINGDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ImageBlendingRunner.h"
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>

class ImageViewer;

class ImageBlendingDialog : public DialogBase {
    Q_OBJECT
public:
    explicit ImageBlendingDialog(QWidget* parent = nullptr);
    void setViewer(ImageViewer* v);

protected slots:
    void onApply();
    void updatePreview();
    void populateCombos();
    void onTopImageChanged();
    void showEvent(QShowEvent* event) override;

private:
    void createUI();
    
    QComboBox* m_cmbBase;
    QComboBox* m_cmbTop;
    QComboBox* m_cmbMode;
    QComboBox* m_cmbTargetChannel;
    QLabel* m_lblTargetChannel;
    
    QSlider* m_sldOpacity;
    QSlider* m_sldLow;
    QSlider* m_sldHigh;
    QSlider* m_sldFeather;
    
    QLabel* m_lblOpacity;
    QLabel* m_lblLow;
    QLabel* m_lblHigh;
    QLabel* m_lblFeather;
    
    QCheckBox* m_chkShowPreview;
    QCheckBox* m_chkHighRes;
    
    ImageViewer* m_previewViewer;
    ImageBlendingRunner m_runner;
    
    bool m_initializing = true;
    bool m_firstPreview = true;
    int m_lastPreviewWidth = 0;
    int m_lastPreviewHeight = 0;
};

#endif // IMAGEBLENDINGDIALOG_H
