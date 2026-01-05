#ifndef SELECTIVECOLORDIALOG_H
#define SELECTIVECOLORDIALOG_H

#include <QDialog>
#include <QImage>
#include "../ImageBuffer.h"

class QSlider;
class QLabel;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QPushButton;
class MainWindow;

class SelectiveColorDialog : public QDialog {
    Q_OBJECT
public:
    explicit SelectiveColorDialog(MainWindow* parent);
    ~SelectiveColorDialog();
    
    void setSource(const ImageBuffer& img);

private slots:
    void onPresetChanged(int index);
    void updateMask();
    void updatePreview();
    void onApply();
    void onReset();

private:
    void setupUi();
    ImageBuffer applyAdjustments(const ImageBuffer& src, const std::vector<float>& mask);
    std::vector<float> computeHueMask(const ImageBuffer& src);
    
    MainWindow* m_mainWindow;
    ImageBuffer m_sourceImage;
    ImageBuffer m_previewImage;
    std::vector<float> m_mask;
    
    // Hue Presets: Red, Orange, Yellow, Green, Cyan, Blue, Magenta
    struct HuePreset {
        QString name;
        float hueStart; // 0-360
        float hueEnd;   // 0-360
    };
    std::vector<HuePreset> m_presets;
    
    // UI Elements
    QComboBox* m_presetCombo;
    QDoubleSpinBox* m_hueStartSpin;
    QDoubleSpinBox* m_hueEndSpin;
    QDoubleSpinBox* m_smoothnessSpin;
    QDoubleSpinBox* m_minChromaSpin;
    QDoubleSpinBox* m_intensitySpin;
    QCheckBox* m_invertCheck;
    QCheckBox* m_showMaskCheck;
    
    // CMY Sliders
    QSlider* m_cyanSlider;
    QSlider* m_magentaSlider;
    QSlider* m_yellowSlider;
    
    // RGB Sliders
    QSlider* m_redSlider;
    QSlider* m_greenSlider;
    QSlider* m_blueSlider;
    
    // LSC Sliders
    QSlider* m_luminanceSlider;
    QSlider* m_saturationSlider;
    QSlider* m_contrastSlider;
    
    // Preview
    QGraphicsView* m_view;
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    
    bool m_updatingPreset;
};

#endif // SELECTIVECOLORDIALOG_H
