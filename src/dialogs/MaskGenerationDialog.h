#ifndef MASKGENERATIONDIALOG_H
#define MASKGENERATIONDIALOG_H

#include <QDialog>
#include <QImage>
#include <vector>
#include "core/MaskLayer.h"
#include "../ImageBuffer.h"

class QComboBox;
class QSlider;
class QLabel;
class QGroupBox;
class QCheckBox;
class QPushButton;
class MaskCanvas;
class LivePreviewDialog;
class QVBoxLayout;

#include "DialogBase.h"

class MaskGenerationDialog : public DialogBase {
    Q_OBJECT
public:
    explicit MaskGenerationDialog(const ImageBuffer& image, QWidget* parent = nullptr);
    MaskLayer getGeneratedMask(int requestedW = 0, int requestedH = 0) const;

private slots:
    void onTypeChanged(const QString& type);
    void updateLivePreview();
    void generatePreview();
    void clearShapes();
    void setMode(const QString& mode);
    
private:
    void setupUI();
    std::vector<float> generateBaseMask(); // From canvas
    std::vector<float> generateRangeMask(const std::vector<float>& base);
    
    // UI Elements
    MaskCanvas* m_canvas;
    
    QComboBox* m_typeCombo;
    QSlider* m_blurSlider;
    QLabel* m_blurLabel;
    
    // Range Selection
    QGroupBox* m_rangeGroup;
    QSlider* m_lowerSl;
    QSlider* m_upperSl;
    QSlider* m_fuzzSl;
    QSlider* m_smoothSl;
    
    QLabel* m_lowerLbl;
    QLabel* m_upperLbl;
    QLabel* m_fuzzLbl;
    QLabel* m_smoothLbl;
    
    QCheckBox* m_linkCb;
    QCheckBox* m_screenCb;
    QCheckBox* m_lightCb; // Use Lightness
    QCheckBox* m_invertCb;
    
    // Preview Visualization
    QComboBox* m_previewStretchCombo;
    QComboBox* m_previewSizeCombo; // 1024, 2048, Full
    int m_maxPreviewDim = 1024;
    
    QPushButton* m_freeBtn;
    QPushButton* m_ellipseBtn;
    QPushButton* m_selectBtn;
    QPushButton* m_moveBtn;
    
    // Data
    const ImageBuffer& m_sourceImage;
    MaskLayer m_resultLayer;
    LivePreviewDialog* m_livePreview;
    
    // Helpers
    std::vector<float> getComponentLightness() const;
    std::vector<float> getComponentChrominance() const;
    std::vector<float> getComponentHue() const;
    std::vector<float> getStarMask() const;
    std::vector<float> getStarMask(int w, int h) const;
    std::vector<float> getChrominance(int w, int h) const;
    std::vector<float> getColorMask(const QString& color) const;
    std::vector<float> getColorMask(const QString& color, int w, int h) const;
    
    // Downsampled data for fast preview
    std::vector<float> m_smallLuma;
    int m_smallW = 0;
    int m_smallH = 0;
    
    // Helper to get lightness (full or small)
    std::vector<float> getLightness(int w, int h) const;
};

#endif // MASKGENERATIONDIALOG_H
