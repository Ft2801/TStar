#ifndef STRETCHDIALOG_H
#define STRETCHDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include "ImageBuffer.h"
#include <QPointer>

class StretchDialog : public QDialog {
    Q_OBJECT
public:
    explicit StretchDialog(QWidget* parent = nullptr);
    ~StretchDialog(); // Cleanup
    void setViewer(class ImageViewer* v);
    class ImageViewer* viewer() const { return m_viewer; }
    void triggerPreview();

protected:
    void reject() override;

private slots:
    void onApply();
    void onPreview();
    
    ImageBuffer::StretchParams getParams() const;

private:
    QPointer<class ImageViewer> m_viewer = nullptr;
    ImageBuffer m_originalBuffer;
    bool m_applied = false;

    QDoubleSpinBox* m_targetSpin;
    QCheckBox* m_linkedCheck;
    QCheckBox* m_normalizeCheck;
    QCheckBox* m_curvesCheck;
    QDoubleSpinBox* m_boostSpin;
};

#endif // STRETCHDIALOG_H
