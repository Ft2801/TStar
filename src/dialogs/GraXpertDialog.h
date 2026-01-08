#ifndef GRAXPERTDIALOG_H
#define GRAXPERTDIALOG_H

#include <QDialog>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include "algos/GraXpertRunner.h"

#include "DialogBase.h"

class GraXpertDialog : public DialogBase {
    Q_OBJECT
public:
    explicit GraXpertDialog(QWidget* parent = nullptr);
    GraXpertParams getParams() const;

private slots:
    void updateUI();

private:
    QRadioButton* m_rbBackground;
    QRadioButton* m_rbDenoise;
    
    QDoubleSpinBox* m_spinStrength; // reused for smoothing/strength
    QComboBox* m_aiVersionCombo;
    QCheckBox* m_gpuCheck;
};

#endif // GRAXPERTDIALOG_H
