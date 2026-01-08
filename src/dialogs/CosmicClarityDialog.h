#ifndef COSMICCLARITYDIALOG_H
#define COSMICCLARITYDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include "algos/CosmicClarityRunner.h"

#include "DialogBase.h"

class CosmicClarityDialog : public DialogBase {
    Q_OBJECT
public:
    explicit CosmicClarityDialog(QWidget* parent = nullptr);
    CosmicClarityParams getParams() const;

private slots:
    void updateUI();

private:
    QComboBox* m_cmbMode;
    QComboBox* m_cmbGpu;
    
    // Sharpen
    QLabel* m_lblShMode; QComboBox* m_cmbShMode;
    QCheckBox* m_chkShSep;
    QCheckBox* m_chkAutoPsf;
    QLabel* m_lblPsf; QSlider* m_sldPsf;
    QLabel* m_lblStAmt; QSlider* m_sldStAmt;
    QLabel* m_lblNstAmt; QSlider* m_sldNstAmt;
    
    // Denoise
    QLabel* m_lblDnLum; QSlider* m_sldDnLum;
    QLabel* m_lblDnCol; QSlider* m_sldDnCol;
    QLabel* m_lblDnMode; QComboBox* m_cmbDnMode;
    QCheckBox* m_chkDnSep;

    // SuperRes
    // (Optional implementation if needed)
    QLabel* m_lblScale; QComboBox* m_cmbScale;
};

#endif // COSMICCLARITYDIALOG_H
