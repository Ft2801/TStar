#include "ExtractLuminanceDialog.h"
#include "DialogBase.h"
#include "../MainWindowCallbacks.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"
#include "../widgets/CustomMdiSubWindow.h"
#include <QComboBox>
#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QMessageBox>

ExtractLuminanceDialog::ExtractLuminanceDialog(QWidget* parent) : DialogBase(parent, tr("Extract Luminance")) {
    // m_mainWindow = getCallbacks(); // Removed direct member storage, access via getCallbacks() when needed
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Method Selection
    QHBoxLayout* methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:")));
    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(tr("Rec. 709 (Standard)"), (int)ChannelOps::LumaMethod::REC709);
    m_methodCombo->addItem(tr("Rec. 601"), (int)ChannelOps::LumaMethod::REC601);
    m_methodCombo->addItem(tr("Rec. 2020"), (int)ChannelOps::LumaMethod::REC2020);
    m_methodCombo->addItem(tr("Average (Equal)"), (int)ChannelOps::LumaMethod::AVERAGE);
    m_methodCombo->addItem(tr("Max"), (int)ChannelOps::LumaMethod::MAX);
    m_methodCombo->addItem(tr("Median"), (int)ChannelOps::LumaMethod::MEDIAN);
    m_methodCombo->addItem(tr("SNR (Noise Weighted)"), (int)ChannelOps::LumaMethod::SNR);
    m_methodCombo->addItem(tr("Custom / Sensor"), (int)ChannelOps::LumaMethod::CUSTOM);
    methodLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(methodLayout);
    
    // Custom Weights Group
    m_customGroup = new QGroupBox(tr("Custom RGB Weights"));
    QHBoxLayout* customLayout = new QHBoxLayout(m_customGroup);
    
    auto addWeight = [&](const QString& label, QDoubleSpinBox*& spin) { // Pass by reference to helper
        customLayout->addWidget(new QLabel(label));
        spin = new QDoubleSpinBox();
        spin->setRange(0.0, 10.0);
        spin->setSingleStep(0.01);
        spin->setDecimals(4);
        spin->setValue(0.3333);
        customLayout->addWidget(spin);
    };
    
    addWeight("R:", m_weightR);
    addWeight("G:", m_weightG);
    addWeight("B:", m_weightB);
    
    mainLayout->addWidget(m_customGroup);
    
    // SNR Settings Group
    m_snrGroup = new QGroupBox(tr("SNR Settings"));
    QVBoxLayout* snrLayout = new QVBoxLayout(m_snrGroup);
    m_autoNoiseCheck = new QCheckBox(tr("Auto Estimate Noise"));
    m_autoNoiseCheck->setChecked(true);
    snrLayout->addWidget(m_autoNoiseCheck);
    
    QHBoxLayout* sigmaLayout = new QHBoxLayout();
    auto addSigma = [&](const QString& label, QDoubleSpinBox*& spin) {
        sigmaLayout->addWidget(new QLabel(label));
        spin = new QDoubleSpinBox();
        spin->setRange(0.0, 1.0); // Normalized sigma usually small
        spin->setSingleStep(0.0001);
        spin->setDecimals(6);
        spin->setValue(0.01); 
        sigmaLayout->addWidget(spin);
    };
    addSigma("σR:", m_sigmaR);
    addSigma("σG:", m_sigmaG);
    addSigma("σB:", m_sigmaB);
    snrLayout->addLayout(sigmaLayout);
    
    mainLayout->addWidget(m_snrGroup);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton(tr("Extract"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
    
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExtractLuminanceDialog::onMethodChanged);
    connect(applyBtn, &QPushButton::clicked, this, &ExtractLuminanceDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &ExtractLuminanceDialog::reject);
    connect(m_autoNoiseCheck, &QCheckBox::toggled, [this](bool checked) {
        m_sigmaR->setEnabled(!checked);
        m_sigmaG->setEnabled(!checked);
        m_sigmaB->setEnabled(!checked);
    });
    
    // Initial State Trigger
    onMethodChanged(0);

}

void ExtractLuminanceDialog::onMethodChanged(int index) {
    int method = m_methodCombo->itemData(index).toInt();
    bool isCustom = (method == (int)ChannelOps::LumaMethod::CUSTOM);
    bool isSNR = (method == (int)ChannelOps::LumaMethod::SNR);
    
    m_customGroup->setEnabled(isCustom);
    m_snrGroup->setEnabled(isSNR);

    if (!isCustom) {
        // UI updates for standard methods handled by disabling custom group.
    }
}

ExtractLuminanceDialog::Params ExtractLuminanceDialog::getParams() const {
    Params p;
    p.methodIndex = m_methodCombo->currentData().toInt();
    p.customWeights = { (float)m_weightR->value(), (float)m_weightG->value(), (float)m_weightB->value() };
    p.autoEstimateNoise = m_autoNoiseCheck->isChecked();
    p.customNoiseSigma = { (float)m_sigmaR->value(), (float)m_sigmaG->value(), (float)m_sigmaB->value() };
    return p;
}

void ExtractLuminanceDialog::onApply() {
    MainWindowCallbacks* mainWindow = getCallbacks(); // Access callbacks directly
    if (!mainWindow) return;
    
    // Find MDI area and populate with open images
    ImageViewer* v_cur = mainWindow->getCurrentViewer(); // Use local mainWindow pointer
    if (!v_cur) return;
    // The following loop was incorrect and caused early return, it's removed.
    // auto* sublist = v_cur->window()->findChildren<CustomMdiSubWindow*>();
    // for (CustomMdiSubWindow* csw : sublist) {
    //     return;
    // }
    
    Params p = getParams();
    ChannelOps::LumaMethod method = (ChannelOps::LumaMethod)p.methodIndex;
    
    std::vector<float> noiseSigma;
    if (method == ChannelOps::LumaMethod::SNR && !p.autoEstimateNoise) {
        noiseSigma = p.customNoiseSigma;
    }
    
    ImageBuffer res = ChannelOps::computeLuminance(v_cur->getBuffer(), method, p.customWeights, noiseSigma);
    if (!res.isValid()) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to compute luminance."));
        return;
    }
    
    QString title = v_cur->getBuffer().name() + "_L";
    if (mainWindow) mainWindow->createResultWindow(res, title); // Use local mainWindow pointer
    accept();
}
