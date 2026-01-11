#ifndef TEXTUREANDCLARITYDIALOG_H
#define TEXTUREANDCLARITYDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include "ImageBuffer.h"
#include <QPointer>

class TextureAndClarityDialog : public QDialog {
    Q_OBJECT
public:
    explicit TextureAndClarityDialog(QWidget* parent = nullptr);
    ~TextureAndClarityDialog();
    void setViewer(class ImageViewer* v);
    class ImageViewer* viewer() const { return m_viewer; }
    void triggerPreview();

signals:
    void applied(const QString& msg);

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onApply();
    void onPreview();
    void onTextureSliderChanged(int value);
    void onClaritySliderChanged(int value);
    void onRadiusChanged(double value);
    
    ImageBuffer::TextureAndClarityParams getParams() const;

private:
    void setupUI();
    void setupConnections();
    void updateLabels();

    QPointer<class ImageViewer> m_viewer = nullptr;
    ImageBuffer m_originalBuffer;
    bool m_applied = false;

    // Texture Control
    QSlider* m_textureSlider;
    QLabel* m_textureValueLabel;
    QDoubleSpinBox* m_radiusSpin;
    
    // Clarity Control
    QSlider* m_claritySlider;
    QLabel* m_clarityValueLabel;
};

#endif // TEXTUREANDCLARITYDIALOG_H
