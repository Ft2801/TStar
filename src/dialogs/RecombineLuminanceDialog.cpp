#include "RecombineLuminanceDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QSlider>
#include <QMessageBox>
#include <QMdiSubWindow>
#include "RecombineLuminanceDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../algos/ChannelOps.h"
#include "../ImageViewer.h"

RecombineLuminanceDialog::RecombineLuminanceDialog(QWidget* parent) : DialogBase(parent, tr("Recombine Luminance")) {
    m_mainWindow = getCallbacks();
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Source Image
    QHBoxLayout* srcLayout = new QHBoxLayout();
    srcLayout->addWidget(new QLabel(tr("Luminance Source:")));
    m_sourceCombo = new QComboBox();
    srcLayout->addWidget(m_sourceCombo);
    mainLayout->addLayout(srcLayout);
    
    // Method Selection
    QHBoxLayout* methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:")));
    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(tr("Rec. 709 (Standard)"), (int)ChannelOps::LumaMethod::REC709);
    m_methodCombo->addItem(tr("Rec. 601"), (int)ChannelOps::LumaMethod::REC601);
    m_methodCombo->addItem(tr("Rec. 2020"), (int)ChannelOps::LumaMethod::REC2020);
    m_methodCombo->addItem(tr("Custom / Sensor"), (int)ChannelOps::LumaMethod::CUSTOM);
    m_methodCombo->addItem(tr("Average"), (int)ChannelOps::LumaMethod::AVERAGE);
    methodLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(methodLayout);
    
    // Parameters
    QGroupBox* paramGroup = new QGroupBox(tr("Parameters"));
    QVBoxLayout* pLayout = new QVBoxLayout(paramGroup);
    
    // Blend
    QHBoxLayout* bleedLayout = new QHBoxLayout();
    bleedLayout->addWidget(new QLabel(tr("Blend:")));
    m_blendSlider = new QSlider(Qt::Horizontal);
    m_blendSlider->setRange(0, 100);
    m_blendSlider->setValue(100);
    m_blendLabel = new QLabel("100%");
    bleedLayout->addWidget(m_blendSlider);
    bleedLayout->addWidget(m_blendLabel);
    pLayout->addLayout(bleedLayout);
    
    // Soft Knee
    QHBoxLayout* kneeLayout = new QHBoxLayout();
    kneeLayout->addWidget(new QLabel(tr("Highlight Protection (Soft Knee):")));
    m_softKneeSpin = new QDoubleSpinBox();
    m_softKneeSpin->setRange(0.0, 1.0);
    m_softKneeSpin->setSingleStep(0.05);
    m_softKneeSpin->setValue(0.0);
    kneeLayout->addWidget(m_softKneeSpin);
    pLayout->addLayout(kneeLayout);
    
    // Custom Weights
    m_customGroup = new QGroupBox(tr("Custom RGB Weights"));
    m_customGroup->setCheckable(false);
    QHBoxLayout* wLayout = new QHBoxLayout(m_customGroup);
    m_weightR = new QDoubleSpinBox(); m_weightR->setRange(0, 5); m_weightR->setSingleStep(0.01); m_weightR->setValue(0.33); wLayout->addWidget(new QLabel("R:")); wLayout->addWidget(m_weightR);
    m_weightG = new QDoubleSpinBox(); m_weightG->setRange(0, 5); m_weightG->setSingleStep(0.01); m_weightG->setValue(0.33); wLayout->addWidget(new QLabel("G:")); wLayout->addWidget(m_weightG);
    m_weightB = new QDoubleSpinBox(); m_weightB->setRange(0, 5); m_weightB->setSingleStep(0.01); m_weightB->setValue(0.33); wLayout->addWidget(new QLabel("B:")); wLayout->addWidget(m_weightB);
    pLayout->addWidget(m_customGroup);
    
    mainLayout->addWidget(paramGroup);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
    
    connect(m_blendSlider, &QSlider::valueChanged, this, &RecombineLuminanceDialog::updateBlendLabel);
    connect(applyBtn, &QPushButton::clicked, this, &RecombineLuminanceDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &RecombineLuminanceDialog::reject);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RecombineLuminanceDialog::onMethodChanged);
    
    onMethodChanged(0);
    refreshSourceList();

}

void RecombineLuminanceDialog::refreshSourceList() {
    m_sourceCombo->clear();
    if (!m_mainWindow) return;
    
    // Enum windows
    ImageViewer* current = m_mainWindow ? m_mainWindow->getCurrentViewer() : nullptr;
    if (!current) return;
    
    auto list = current->window()->findChildren<CustomMdiSubWindow*>();
    
    for (auto* win : list) {
        ImageViewer* v = win->viewer();
        if (v && v != current) { // Don't allow self as source (usually)
             m_sourceCombo->addItem(win->windowTitle(), QVariant::fromValue((void*)v));
        }
    }
}

void RecombineLuminanceDialog::updateBlendLabel(int val) {
    m_blendLabel->setText(QString("%1%").arg(val));
}

void RecombineLuminanceDialog::onMethodChanged(int index) {
    int method = m_methodCombo->itemData(index).toInt();
    bool isCustom = (method == (int)ChannelOps::LumaMethod::CUSTOM);
    m_customGroup->setEnabled(isCustom);
}

void RecombineLuminanceDialog::onApply() {
    ImageViewer* target = m_mainWindow ? m_mainWindow->getCurrentViewer() : nullptr;
    if (!target) return;
    
    // Resolve Source
    int idx = m_sourceCombo->currentIndex();
    if (idx < 0) {
        QMessageBox::warning(this, tr("No Source"), tr("Please select a source luminance image."));
        return;
    }
    ImageViewer* srcViewer = (ImageViewer*)m_sourceCombo->itemData(idx).value<void*>();
    if (!srcViewer) return;
    
    if (m_mainWindow) {
        m_mainWindow->logMessage(tr("Recombining..."), 0); 
        m_mainWindow->startLongProcess();
    }
    
    ChannelOps::LumaMethod method = (ChannelOps::LumaMethod)m_methodCombo->currentData().toInt();
    float blend = m_blendSlider->value() / 100.0f;
    float softKnee = (float)m_softKneeSpin->value();
    std::vector<float> weights = { (float)m_weightR->value(), (float)m_weightG->value(), (float)m_weightB->value() };
    
    bool ok = ChannelOps::recombineLuminance(target->getBuffer(), srcViewer->getBuffer(), method, blend, softKnee, weights);
    
    if (ok) {
        target->refresh();
        accept();
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Recombination failed. Typically specific to size mismatch or invalid source image."));
        // Don't undo here, the buffer might be partially touched or untouched? 
        // ChannelOps implementation checks validity first. If it returns false, it likely didn't touch data.
    }
}
