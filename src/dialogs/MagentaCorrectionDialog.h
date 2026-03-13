#ifndef MAGENTACORRECTIONDIALOG_H
#define MAGENTACORRECTIONDIALOG_H

#include "DialogBase.h"
#include <QCheckBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPointer>

class MagentaCorrectionDialog : public DialogBase {
    Q_OBJECT
public:
    explicit MagentaCorrectionDialog(QWidget* parent = nullptr);
    ~MagentaCorrectionDialog();

    float getAmount() const;
    float getThreshold() const;
    bool isWithStarMask() const;

    void setViewer(class ImageViewer* v);

signals:
    void apply();

private:
    QPointer<class ImageViewer> m_viewer;

    QSlider* m_amountSlider;
    QDoubleSpinBox* m_amountSpin;
    QSlider* m_threshSlider;
    QDoubleSpinBox* m_threshSpin;
    QCheckBox* m_starMaskCheck;
};

#endif // MAGENTACORRECTIONDIALOG_H
