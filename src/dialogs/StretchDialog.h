#ifndef STRETCHDIALOG_H
#define STRETCHDIALOG_H

#include "DialogBase.h"
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include "ImageBuffer.h"
#include <QPointer>

class StretchDialog : public DialogBase {
    Q_OBJECT
public:
    explicit StretchDialog(QWidget* parent = nullptr);
    ~StretchDialog();
    void setViewer(class ImageViewer* v);
    class ImageViewer* viewer() const { return m_viewer; }
    void triggerPreview();

signals:
    void applied(const QString& msg);

protected:
    void reject() override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onApply();
    void onPreview();
    void onHdrToggled(bool enabled);
    void onHighRangeToggled(bool enabled);
    void onLumaOnlyToggled(bool enabled);
    
    ImageBuffer::StretchParams getParams() const;

private:
    void setupUI();
    void setupConnections();

    QPointer<class ImageViewer> m_viewer = nullptr;
    ImageBuffer m_originalBuffer;
    bool m_applied = false;

    // Basic Controls
    QDoubleSpinBox* m_targetSpin;
    QCheckBox* m_linkedCheck;
    QCheckBox* m_normalizeCheck;
    
    // Black Point Control
    QDoubleSpinBox* m_blackpointSigmaSpin;
    QCheckBox* m_noBlackClipCheck;
    
    // Curves
    QGroupBox* m_curvesGroup;
    QCheckBox* m_curvesCheck;
    QDoubleSpinBox* m_boostSpin;
    
    // HDR Compression
    QGroupBox* m_hdrGroup;
    QDoubleSpinBox* m_hdrAmountSpin;
    QDoubleSpinBox* m_hdrKneeSpin;
    
    // Luminance-Only Mode
    QGroupBox* m_lumaOnlyGroup;
    QComboBox* m_lumaModeCombo;
    
    // High-Range Mode
    QGroupBox* m_highRangeGroup;
    QDoubleSpinBox* m_hrPedestalSpin;
    QDoubleSpinBox* m_hrSoftCeilSpin;
    QDoubleSpinBox* m_hrHardCeilSpin;
    QDoubleSpinBox* m_hrSoftclipSpin;
};

#endif // STRETCHDIALOG_H
