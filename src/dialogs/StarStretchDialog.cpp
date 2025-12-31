#include "StarStretchDialog.h"
#include "../MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QIcon>

StarStretchDialog::StarStretchDialog(MainWindow* parent, ImageViewer* viewer)
    : QDialog(parent), m_mainWin(parent), m_viewer(viewer)
{
    setWindowTitle(tr("Star Stretch"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    if (m_viewer) {
        m_originalBuffer = m_viewer->getBuffer();
    }
    createUI();
    if (m_viewer) {
        m_originalBuffer = m_viewer->getBuffer();
    }
    createUI();
}

void StarStretchDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    
    // Restore old viewer if needed
    if (m_viewer && !m_applied) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
    
    m_viewer = v;
    m_applied = false;
    
    if (m_viewer) {
        m_originalBuffer = m_viewer->getBuffer();
        updatePreview();
    }
}

void StarStretchDialog::createUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Stretch Slider
    m_lblStretch = new QLabel(tr("Stretch Amount: 5.00"));
    m_sliderStretch = new QSlider(Qt::Horizontal);
    m_sliderStretch->setRange(0, 800);
    m_sliderStretch->setValue(500);
    connect(m_sliderStretch, &QSlider::valueChanged, this, [this](int val){
        m_lblStretch->setText(tr("Stretch Amount: %1").arg(val / 100.0, 0, 'f', 2));
        onSliderChanged();
    });
    
    mainLayout->addWidget(m_lblStretch);
    mainLayout->addWidget(m_sliderStretch);
    
    // Color Boost Slider
    m_lblBoost = new QLabel(tr("Color Boost: 1.00"));
    m_sliderBoost = new QSlider(Qt::Horizontal);
    m_sliderBoost->setRange(0, 200);
    m_sliderBoost->setValue(100);
    connect(m_sliderBoost, &QSlider::valueChanged, this, [this](int val){
        m_lblBoost->setText(tr("Color Boost: %1").arg(val / 100.0, 0, 'f', 2));
        onSliderChanged();
    });
    
    mainLayout->addWidget(m_lblBoost);
    mainLayout->addWidget(m_sliderBoost);
    
    // SCNR
    m_chkScnr = new QCheckBox(tr("Remove Green via SCNR (Optional)"));
    connect(m_chkScnr, &QCheckBox::toggled, this, &StarStretchDialog::onSliderChanged);
    mainLayout->addWidget(m_chkScnr);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnApply = new QPushButton(tr("Apply"));
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));
    
    connect(m_btnApply, &QPushButton::clicked, this, &StarStretchDialog::onApply);
    connect(btnCancel, &QPushButton::clicked, this, &StarStretchDialog::reject);
    
    btnLayout->addWidget(m_btnApply);
    btnLayout->addWidget(btnCancel);
    
    mainLayout->addLayout(btnLayout);
}

void StarStretchDialog::onSliderChanged() {
    updatePreview();
}

void StarStretchDialog::updatePreview() {
    if (!m_viewer) return;

    StarStretchParams params;
    params.stretchAmount = m_sliderStretch->value() / 100.0f;
    params.colorBoost = m_sliderBoost->value() / 100.0f;
    params.scnr = m_chkScnr->isChecked();
    
    if (m_runner.run(m_originalBuffer, m_previewBuffer, params)) {
        // preserveView = true to avoid zooming out on every slider move
        m_viewer->setBuffer(m_previewBuffer, m_viewer->windowTitle(), true); 
    }
}

void StarStretchDialog::onApply() {
    if (!m_viewer) {
        reject();
        return;
    }

    // 1. Restore clean state
    m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

    // 2. Push undo
    m_viewer->pushUndo();

    // 3. Apply final (updatePreview will run the runner and set the buffer)
    updatePreview();
    
    m_applied = true;
    m_mainWin->log(tr("Star Stretch applied."));
    accept();
}

void StarStretchDialog::reject() {
    if (!m_applied && m_viewer) {
        // Restore original image if we were previewing
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
    QDialog::reject();
}
