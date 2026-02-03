#ifndef SCNRDIALOG_H
#define SCNRDIALOG_H

#include "DialogBase.h"
#include <QPointer>
#include "../ImageViewer.h"
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>

class SCNRDialog : public DialogBase {
    Q_OBJECT
public:
    enum ProtectionMethod {
        AverageNeutral,
        MaximumNeutral,
        MinimumNeutral
    };

    explicit SCNRDialog(QWidget* parent = nullptr);
    ~SCNRDialog();

    float getAmount() const;
    ProtectionMethod getMethod() const;
    
    void setViewer(class ImageViewer* v);
    
    // Standardize handling: Dialog prepares/emits params, main window or controller applies.
    // MW connects to apply() signal.
    
signals:
    void apply(); // To maintain compatibility if needed, but we might change usage.

private:
    // Robust members
    QPointer<class ImageViewer> m_viewer;
    
    QComboBox* m_methodCombo;
    QSlider* m_amountSlider;
    QDoubleSpinBox* m_amountSpin;
};

#endif // SCNRDIALOG_H
