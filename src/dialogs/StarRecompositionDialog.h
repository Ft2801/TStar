#ifndef STARRECOMPOSITIONDIALOG_H
#define STARRECOMPOSITIONDIALOG_H

#include "DialogBase.h"
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include "../ImageBuffer.h"
#include "../algos/StarRecompositionRunner.h"

class MainWindowCallbacks;
class ImageViewer;
class StarRecompositionDialog : public DialogBase {
    Q_OBJECT
public:
    explicit StarRecompositionDialog(QWidget* parent = nullptr);

    void setViewer(ImageViewer* v);
    
    // Check if the given viewer is currently selected in either combobox
    bool isUsingViewer(ImageViewer* v) const;

private slots:
    void onRefreshViews();
    void onUpdatePreview();
    void onApply();

private:
    void createUI();
    void populateCombos();
    
    StarRecompositionRunner m_runner;
    
    QComboBox* m_cmbStarless;
    QComboBox* m_cmbStars;

    // Advanced Stretch parameters
    QComboBox* m_cmbStretchMode;
    QComboBox* m_cmbColorMode;
    QComboBox* m_cmbClipMode;
    
    QSlider* m_sliderD;
    class QDoubleSpinBox* m_spinD;

    QSlider* m_sliderB;
    class QDoubleSpinBox* m_spinB;

    QSlider* m_sliderSP;
    class QDoubleSpinBox* m_spinSP;
    
    // Preview Logic
    ImageViewer* m_previewViewer;
    QPushButton* m_btnFit;
    
    // Preview Buffer (scaled for performance)
    ImageBuffer m_previewBufferSll;
    ImageBuffer m_previewBufferStr;
    float m_previewScale = 1.0f;
    
    bool m_initializing = true;
};

#endif // STARRECOMPOSITIONDIALOG_H
