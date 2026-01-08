#ifndef ARCSINHSTRETCHDIALOG_H
#define ARCSINHSTRETCHDIALOG_H

#include "DialogBase.h"
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include "ImageBuffer.h"
#include "ImageViewer.h"

#include <QPointer>

class ArcsinhStretchDialog : public DialogBase {
    Q_OBJECT

public:
    explicit ArcsinhStretchDialog(ImageViewer* viewer, QWidget* parent = nullptr);
    ~ArcsinhStretchDialog();

    void setViewer(ImageViewer* v);

protected:
    void reject() override;  // Handle close/cancel

private slots:
    void onStretchChanged(int value);
    void onBlackPointChanged(double value);
    void onHumanLuminanceToggled(bool checked);
    void onPreviewToggled(bool checked);
    void onReset();
    void onApply();

private:
    void setupUI();
    void updatePreview();
    void updateClippingStats(const ImageBuffer& buffer);
    QPointer<ImageViewer> m_viewer;
    ImageBuffer m_originalBuffer;  // Original image for preview
    bool m_applied = false;
    
    // UI elements
    QSlider* m_stretchSlider;
    QDoubleSpinBox* m_stretchSpin;
    QSlider* m_blackPointSlider;
    QDoubleSpinBox* m_blackPointSpin;
    QCheckBox* m_humanLuminanceCheck;
    QCheckBox* m_previewCheck;
    QLabel* m_lowClipLabel;
    QLabel* m_highClipLabel;
    
    // Parameters
    float m_stretch = 0.0f;
    float m_blackPoint = 0.0f;
    bool m_humanLuminance = true;
};

#endif // ARCSINHSTRETCHDIALOG_H
