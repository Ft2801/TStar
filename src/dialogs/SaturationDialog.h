#ifndef SATURATIONDIALOG_H
#define SATURATIONDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include "ImageBuffer.h"

class ImageViewer; // Forward declaration

class SaturationDialog : public QDialog {
    Q_OBJECT
public:
    explicit SaturationDialog(QWidget* parent, class ImageViewer* viewer);
    ~SaturationDialog();
    ImageBuffer::SaturationParams getParams() const;
    void setBuffer(ImageBuffer* buffer);
    void setTarget(ImageViewer* viewer); // New: for retargeting
    void triggerPreview();

    struct State {
        int amount, bgFactor, hueCenter, hueWidth, hueSmooth;
        int presetIndex;
    };
    State getState() const;
    void setState(const State& s);
    void resetState();

signals:
    void applyInternal(const ImageBuffer::SaturationParams& params);
    void preview(const ImageBuffer::SaturationParams& params);

private slots:
    void onSliderChanged();
    void onPresetChanged(int index);
    void handleApply();

private:
    ImageBuffer* m_buffer; // Pointer to LIVE buffer
    ImageBuffer m_originalBuffer; // Backup
    ImageViewer* m_viewer; // Tracking viewer
    bool m_applied = false;
    
    QSlider* m_sldAmount;
    QSlider* m_sldBgFactor;
    QSlider* m_sldHueCenter;
    QSlider* m_sldHueWidth;
    QSlider* m_sldHueSmooth;
    
    QLabel* m_valAmount;
    QLabel* m_valBgFactor;
    QLabel* m_valHueCenter;
    QLabel* m_valHueWidth;
    QLabel* m_valHueSmooth;
    
    QComboBox* m_cmbPresets;

    void setupUI();
};

#endif // SATURATIONDIALOG_H
