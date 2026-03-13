#ifndef MAGENTACORRECTIONDIALOG_H
#define MAGENTACORRECTIONDIALOG_H

#include "DialogBase.h"
#include <QPointer>
#include "../ImageViewer.h"
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>

class MagentaCorrectionDialog : public DialogBase {
    Q_OBJECT
public:
    enum ProtectionMethod {
        GreenChannel,    // Use G as neutral reference (average neutral)
        MaximumNeutral,  // Conservative
        MinimumNeutral   // Aggressive
    };

    explicit MagentaCorrectionDialog(QWidget* parent = nullptr);
    ~MagentaCorrectionDialog();

    float getAmount() const;
    ProtectionMethod getMethod() const;

    void setViewer(class ImageViewer* v);

signals:
    void apply();

private:
    QPointer<class ImageViewer> m_viewer;

    QComboBox* m_methodCombo;
    QSlider* m_amountSlider;
    QDoubleSpinBox* m_amountSpin;
};

#endif // MAGENTACORRECTIONDIALOG_H
