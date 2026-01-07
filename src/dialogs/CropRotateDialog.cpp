#include "CropRotateDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QMessageBox>
#include "../ImageViewer.h"

CropRotateDialog::CropRotateDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Rotate & Crop Tool"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Rotation Controls
    QHBoxLayout* spinLayout = new QHBoxLayout();
    spinLayout->addWidget(new QLabel(tr("Rotation (deg):")));
    
    m_angleSpin = new QDoubleSpinBox(this);
    m_angleSpin->setRange(-180.0, 180.0);
    m_angleSpin->setSingleStep(0.1);
    
    spinLayout->addWidget(m_angleSpin);
    mainLayout->addLayout(spinLayout);
    
    m_angleSlider = new QSlider(Qt::Horizontal, this);
    m_angleSlider->setRange(-1800, 1800); // 10x for 0.1 precision
    mainLayout->addWidget(m_angleSlider);
    
    // Aspect Ratio
    QHBoxLayout* arLayout = new QHBoxLayout();
    arLayout->addWidget(new QLabel(tr("Aspect Ratio:")));
    m_aspectCombo = new QComboBox(this);
    m_aspectCombo->addItem(tr("Free"), -1.0f);
    m_aspectCombo->addItem(tr("1:1 (Square)"), 1.0f);
    m_aspectCombo->addItem("3:2", 1.5f);
    m_aspectCombo->addItem("2:3", 0.6666f);
    m_aspectCombo->addItem("4:3", 1.3333f);
    m_aspectCombo->addItem("3:4", 0.75f);
    m_aspectCombo->addItem("16:9", 1.7777f);
    m_aspectCombo->addItem("9:16", 0.5625f);
    arLayout->addWidget(m_aspectCombo);
    mainLayout->addLayout(arLayout);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
    
    // Connections
    connect(m_angleSlider, &QSlider::valueChanged, this, [this](int val){
        m_angleSpin->blockSignals(true);
        m_angleSpin->setValue(val / 10.0);
        m_angleSpin->blockSignals(false);
        onRotationChanged();
    });
    
    connect(m_angleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val){
        m_angleSlider->blockSignals(true);
        m_angleSlider->setValue(static_cast<int>(val * 10));
        m_angleSlider->blockSignals(false);
        onRotationChanged();
    });
    
    connect(applyBtn, &QPushButton::clicked, this, &CropRotateDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &CropRotateDialog::reject); 
    
    connect(m_aspectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CropRotateDialog::onRatioChanged);
    
    resize(300, 200);

    // Ensure dialog is on screen (fix for macOS off-screen issue)
    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

CropRotateDialog::~CropRotateDialog() {
    if (m_viewer) {
        m_viewer->setCropMode(false);
    }
}

void CropRotateDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    
    // Disable crop mode on old viewer
    if (m_viewer) {
        m_viewer->setCropMode(false);
        // We might want to clear crop angle/rect too? 
        // m_viewer->setCropAngle(0); 
        // No, keep state? Usually exiting tool means finishing or cancelling.
    }
    
    m_viewer = v;
    
    if (m_viewer) {
        m_viewer->setCropMode(true);
        // Apply current dialog state to new viewer
        m_viewer->setAspectRatio(m_aspectCombo->currentData().toFloat());
        m_viewer->setCropAngle(static_cast<float>(m_angleSpin->value()));
    }
}

void CropRotateDialog::onRotationChanged() {
    if (m_viewer) {
        m_viewer->setCropAngle(static_cast<float>(m_angleSpin->value()));
    }
}

void CropRotateDialog::onRatioChanged() {
    if (m_viewer) {
        m_viewer->setAspectRatio(m_aspectCombo->currentData().toFloat());
    }
}

void CropRotateDialog::onApply() {
    if (!m_viewer) return;
    
    float cx, cy, w, h, angle;
    m_viewer->getCropState(cx, cy, w, h, angle);
    
    if (w <= 0 || h <= 0) {
        QMessageBox::warning(this, tr("Warning"), tr("Please draw a crop rectangle first."));
        return;
    }
    
    m_viewer->pushUndo();
    m_viewer->getBuffer().cropRotated(cx, cy, w, h, angle);
    m_viewer->refreshDisplay(false); // Resets display mapping if needed
    m_viewer->fitToWindow();
    
    // Reset angle after apply? Usually yes for crop.
    m_angleSpin->setValue(0);
    // m_viewer->setCropMode(false); // Keep mode on? Usually tools stay open.
    // If we want continuous cropping, keep it on.
    // Reset crop rect?
    m_viewer->setCropMode(false); // Reset internal state
    m_viewer->setCropMode(true);  // Re-enable for next op
}
