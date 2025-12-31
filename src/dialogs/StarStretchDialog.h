#ifndef STARSTRETCHDIALOG_H
#define STARSTRETCHDIALOG_H

#include <QDialog>
#include "../ImageBuffer.h"
#include "../algos/StarStretchRunner.h"

// Forward declarations
class MainWindow;
class ImageViewer;
class QLabel;
class QSlider;
class QCheckBox;
class QPushButton;

class StarStretchDialog : public QDialog {
    Q_OBJECT
public:
    explicit StarStretchDialog(MainWindow* parent, ImageViewer* viewer);

    void setViewer(ImageViewer* v);

public slots:
    void onSliderChanged();
    void onApply();
    void updatePreview();
    void reject() override;

private:
    MainWindow* m_mainWin;
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
    
    QPushButton* m_btnApply;
    
    void createUI();
};

#endif // STARSTRETCHDIALOG_H
