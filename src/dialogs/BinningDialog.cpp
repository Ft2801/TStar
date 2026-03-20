#include "BinningDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include "../ImageViewer.h"
#include "MainWindowCallbacks.h"

BinningDialog::BinningDialog(QWidget* parent) : DialogBase(parent, tr("Image Binning"), 200, 80) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QHBoxLayout* comboLayout = new QHBoxLayout();
    comboLayout->addWidget(new QLabel(tr("Binning Factor:")));
    
    m_binCombo = new QComboBox(this);
    m_binCombo->addItem(tr("1x1 (None)"), 1);
    m_binCombo->addItem(tr("2x2"), 2);
    m_binCombo->addItem(tr("3x3"), 3);
    m_binCombo->setCurrentIndex(1); // Default to 2x2
    
    comboLayout->addWidget(m_binCombo);
    mainLayout->addLayout(comboLayout);
    
    mainLayout->addStretch();
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);
    
    connect(applyBtn, &QPushButton::clicked, this, &BinningDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &BinningDialog::reject);
}

void BinningDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

void BinningDialog::onApply() {
    // Resolve the current active viewer at time of apply so the tool follows view selection
    MainWindowCallbacks* cb = getCallbacks();
    ImageViewer* v = cb ? cb->getCurrentViewer() : m_viewer.data();
    if (!v) { QMessageBox::warning(this, tr("No Image"), tr("Select image.")); return; }

    int factor = m_binCombo->currentData().toInt();
    if (factor <= 1) return;

    v->pushUndo();
    v->getBuffer().bin(factor);
    v->refreshDisplay(false);
    v->fitToWindow();

    // Close dialog after apply (one-time operation)
    accept();
}
