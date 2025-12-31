#include "StretchDialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QIcon>

#include "../ImageViewer.h"

StretchDialog::StretchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("True Stretch"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    
    QFormLayout* layout = new QFormLayout(this);
    
    m_targetSpin = new QDoubleSpinBox(this);
    m_targetSpin->setRange(0.01, 0.99);
    m_targetSpin->setSingleStep(0.01);
    m_targetSpin->setValue(0.25);
    
    m_linkedCheck = new QCheckBox(tr("Linked (RGB)"), this);
    m_linkedCheck->setChecked(true); 
    
    m_normalizeCheck = new QCheckBox(tr("Normalize Output"), this);
    m_normalizeCheck->setChecked(false);
    
    m_curvesCheck = new QCheckBox(tr("Apply S-Curves"), this);
    
    m_boostSpin = new QDoubleSpinBox(this);
    m_boostSpin->setRange(0.0, 1.0);
    m_boostSpin->setSingleStep(0.1);
    m_boostSpin->setValue(0.0);
    m_boostSpin->setEnabled(false);
    
    connect(m_curvesCheck, &QCheckBox::toggled, m_boostSpin, &QWidget::setEnabled);
    
    layout->addRow(tr("Target Median:"), m_targetSpin);
    layout->addRow("", m_linkedCheck);
    layout->addRow("", m_normalizeCheck);
    layout->addRow("", m_curvesCheck);
    layout->addRow(tr("Curves Boost:"), m_boostSpin);
    
    // Buttons: Preview + Apply + Cancel
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* previewBtn = new QPushButton(tr("Preview"), this);
    QPushButton* applyBtn = new QPushButton(tr("Apply"), this);
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    
    btnLayout->addWidget(previewBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(cancelBtn);
    
    connect(previewBtn, &QPushButton::clicked, this, &StretchDialog::onPreview);
    connect(applyBtn, &QPushButton::clicked, this, &StretchDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    layout->addRow(btnLayout);
}

StretchDialog::~StretchDialog() {
    if (!m_applied && m_viewer) {
        m_viewer->clearPreviewLUT();
    }
}

void StretchDialog::reject() {
    if (!m_applied && m_viewer) {
        m_viewer->clearPreviewLUT();
    }
    QDialog::reject();
}

void StretchDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    
    // Restore old
    if (m_viewer && !m_applied) {
        if (m_originalBuffer.isValid()) {
             // Revert strictly
             m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
        m_viewer->clearPreviewLUT(); // Clear any LUT based preview
    }
    
    m_viewer = v;
    m_applied = false;
    m_originalBuffer = ImageBuffer();
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer(); // Backup
        // No auto-preview on context switch
    }
}

void StretchDialog::onPreview() {
    if (!m_viewer || !m_originalBuffer.isValid()) return;
    
    // Use LUT preview for speed if supported
    ImageBuffer::StretchParams p = getParams();
    auto lut = m_originalBuffer.computeTrueStretchLUT(p);
    m_viewer->setPreviewLUT(lut);
}

void StretchDialog::onApply() {
    if (m_viewer && m_originalBuffer.isValid()) {
        m_viewer->pushUndo();
        // LUT preview is visual only, so we must apply changes to the buffer now
        m_viewer->clearPreviewLUT(); 
        
        // Restore original first to be clean (though LUT didn't change data)
        // Then apply
        ImageBuffer::StretchParams p = getParams();
        m_viewer->getBuffer().performTrueStretch(p);
        m_viewer->refreshDisplay();
        
        m_applied = true;
    }
    accept();
}

ImageBuffer::StretchParams StretchDialog::getParams() const {
    ImageBuffer::StretchParams p;
    p.targetMedian = (float)m_targetSpin->value();
    p.linked = m_linkedCheck->isChecked();
    p.normalize = m_normalizeCheck->isChecked();
    p.applyCurves = m_curvesCheck->isChecked();
    p.curvesBoost = (float)m_boostSpin->value();
    return p;
}
