#include "RecombineLuminanceDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QMessageBox>
#include <QMdiSubWindow>
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
    
    // Color Space Selection (HSL, HSV, CIE L*a*b*)
    QHBoxLayout* csLayout = new QHBoxLayout();
    csLayout->addWidget(new QLabel(tr("Color Space:")));
    m_colorSpaceCombo = new QComboBox();
    m_colorSpaceCombo->addItem(tr("HSL (Hue-Saturation-Lightness)"), (int)ChannelOps::ColorSpaceMode::HSL);
    m_colorSpaceCombo->addItem(tr("HSV (Hue-Saturation-Value)"), (int)ChannelOps::ColorSpaceMode::HSV);
    m_colorSpaceCombo->addItem(tr("CIE L*a*b*"), (int)ChannelOps::ColorSpaceMode::CIELAB);
    csLayout->addWidget(m_colorSpaceCombo);
    mainLayout->addLayout(csLayout);
    
    // Blend
    QHBoxLayout* blendLayout = new QHBoxLayout();
    blendLayout->addWidget(new QLabel(tr("Blend:")));
    m_blendSlider = new QSlider(Qt::Horizontal);
    m_blendSlider->setRange(0, 100);
    m_blendSlider->setValue(100);
    m_blendLabel = new QLabel("100%");
    blendLayout->addWidget(m_blendSlider);
    blendLayout->addWidget(m_blendLabel);
    mainLayout->addLayout(blendLayout);
    
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
        if (v && v != current) {
             m_sourceCombo->addItem(win->windowTitle(), QVariant::fromValue((void*)v));
        }
    }
}

void RecombineLuminanceDialog::updateBlendLabel(int val) {
    m_blendLabel->setText(QString("%1%").arg(val));
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
    
    // Validate source is mono
    if (srcViewer->getBuffer().channels() != 1) {
        QMessageBox::warning(this, tr("Invalid Source"), tr("The source image must be a single-channel (mono) luminance image."));
        return;
    }
    
    // Validate target is RGB
    if (target->getBuffer().channels() < 3) {
        QMessageBox::warning(this, tr("Invalid Target"), tr("The target image must be a color (RGB) image."));
        return;
    }
    
    // Save original buffer for mask blending before pushing undo
    ImageBuffer origBuf = target->getBuffer();

    if (m_mainWindow) {
        m_mainWindow->logMessage(tr("Recombining luminance..."), 0); 
        m_mainWindow->startLongProcess();
    }
    
    // Push undo state BEFORE modifying the buffer
    target->pushUndo();
    
    ChannelOps::ColorSpaceMode csMode = (ChannelOps::ColorSpaceMode)m_colorSpaceCombo->currentData().toInt();
    float blend = m_blendSlider->value() / 100.0f;
    
    bool ok = ChannelOps::recombineLuminance(target->getBuffer(), srcViewer->getBuffer(), csMode, blend);

    // Respect mask: blend processed result back with original
    if (ok && origBuf.hasMask()) {
        target->getBuffer().blendResult(origBuf);
    }
    
    if (m_mainWindow) {
        m_mainWindow->endLongProcess();
    }
    
    if (ok) {
        target->refresh();
        if (m_mainWindow) {
            m_mainWindow->logMessage(tr("Luminance recombination completed."), 0);
        }
        accept();
    } else {
        // Undo the change since operation failed
        target->undo();
        QMessageBox::critical(this, tr("Error"), tr("Recombination failed. Check that image dimensions match and the source is a valid luminance image."));
    }
}
