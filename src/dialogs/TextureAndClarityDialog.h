#ifndef TEXTUREANDCLARITYDIALOG_H
#define TEXTUREANDCLARITYDIALOG_H

#include "DialogBase.h"
#include <QSlider>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPointer>
#include <QCheckBox>
#include "../ImageBuffer.h"

// Forward declaration
class ImageViewer;

class TextureAndClarityDialog : public DialogBase {
    Q_OBJECT
public:
    explicit TextureAndClarityDialog(QWidget* parent, ImageViewer* viewer);
    ~TextureAndClarityDialog();
    
    void setViewer(ImageViewer* viewer);
    ImageViewer* viewer() const { return m_viewer; }
    void triggerPreview();
    
    // State management
    struct State {
        int texture;
        int clarity;
        int radius;
        bool preview;
    };
    State getState() const;
    void setState(const State& s);
    void resetState();

signals:
    void applied(const QString& msg);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onTextureSliderChanged(int value);
    void onClaritySliderChanged(int value);
    void onRadiusSliderChanged(int value);
    void onApply();
    void onReset();
    void onPreview(); // Helper for signal connections

private:
    void setupUI();
    void updateLabels();
    ImageBuffer::TextureAndClarityParams getParams() const;

    QPointer<ImageViewer> m_viewer;
    ImageBuffer* m_buffer = nullptr;         // Pointer to live buffer (in viewer)
    ImageBuffer m_originalBuffer;  // Backup for preview/undo
    
    bool m_applied = false;
    bool m_initializing = true;

    // UI Controls
    QSlider* m_textureSlider = nullptr;
    QLabel* m_textureValueLabel = nullptr;
    QSlider* m_radiusSlider = nullptr;
    QLabel* m_radiusValueLabel = nullptr;
    QSlider* m_claritySlider = nullptr;
    QLabel* m_clarityValueLabel = nullptr;
    QCheckBox* m_previewCheckbox = nullptr;
};

#endif // TEXTUREANDCLARITYDIALOG_H
