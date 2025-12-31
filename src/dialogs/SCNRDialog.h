#ifndef SCNRDIALOG_H
#define SCNRDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPointer>

class SCNRDialog : public QDialog {
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
    
    // New: Handle apply internally or emit with target?
    // Better: emit normally, but MW should use the target we track?
    // Or we handle apply logic here like SaturationDialog?
    // Let's stick to MW handling it for now but Use proper tracking of target in MW?
    // Wait, the instruction is "Apply strict measures to ALL tools". 
    // This implies moving logic INSIDE dialog is better for encapsulation.
    
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
