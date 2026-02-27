#ifndef STARSTRETCHDIALOG_H
#define STARSTRETCHDIALOG_H

#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/StarStretchRunner.h"

// Forward declarations
class ImageViewer;
class QLabel;
class QSlider;
class QCheckBox;
class QPushButton;

class StarStretchDialog : public DialogBase {
    Q_OBJECT
public:
    explicit StarStretchDialog(QWidget* parent, ImageViewer* viewer);
    ~StarStretchDialog();

    void setViewer(ImageViewer* v);

public slots:
    void onSliderChanged();
    void onApply();
    void updatePreview();
    void reject() override;

private:
    ImageViewer* m_viewer;
    ImageBuffer m_originalBuffer;
    ImageBuffer m_previewBuffer;
    StarStretchRunner m_runner;
    bool m_applied = false;
    
    QLabel* m_lblStretch;
    QSlider* m_sliderStretch;
    
    QLabel* m_lblBoost;
    QSlider* m_sliderBoost;
    
    QCheckBox* m_chkScnr;
    QCheckBox* m_chkPreview;
    
    QPushButton* m_btnApply;
    
    void createUI();
};

#endif // STARSTRETCHDIALOG_H
