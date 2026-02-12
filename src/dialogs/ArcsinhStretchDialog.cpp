#include "ArcsinhStretchDialog.h"
#include "ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

ArcsinhStretchDialog::ArcsinhStretchDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("Arcsinh Stretch"), 500, 250), m_viewer(nullptr), m_applied(false)
{
    setMinimumWidth(400);
    setupUI();
    setViewer(viewer);
}

void ArcsinhStretchDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    
    // 1. Restore OLD viewer if preview active/uncommitted
    if (m_viewer && !m_applied && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }

    m_viewer = v;
    m_applied = false;
    m_originalBuffer = ImageBuffer();
    
    // 2. Setup NEW viewer
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer();
        if (m_previewCheck->isChecked()) updatePreview();
    }
}

ArcsinhStretchDialog::~ArcsinhStretchDialog() {
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
}

void ArcsinhStretchDialog::reject() {
    // Restore original image when cancelled/closed
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
    QDialog::reject();
}

void ArcsinhStretchDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    
    // --- Stretch Factor ---
    QHBoxLayout* stretchLayout = new QHBoxLayout();
    QLabel* stretchLabel = new QLabel(tr("Stretch factor:"));
    m_stretchSpin = new QDoubleSpinBox();
    m_stretchSpin->setRange(0, 1000);
    m_stretchSpin->setDecimals(1);
    m_stretchSpin->setSingleStep(1.0);
    m_stretchSpin->setValue(0);
    stretchLayout->addWidget(stretchLabel);
    stretchLayout->addStretch();
    stretchLayout->addWidget(m_stretchSpin);
    mainLayout->addLayout(stretchLayout);
    
    m_stretchSlider = new QSlider(Qt::Horizontal);
    m_stretchSlider->setRange(0, 10000);  // 0-1000 with 0.1 precision
    m_stretchSlider->setValue(0);
    m_stretchSlider->setToolTip(tr("The stretch factor adjusts the non-linearity."));
    mainLayout->addWidget(m_stretchSlider);
    
    // --- Black Point ---
    QHBoxLayout* bpLayout = new QHBoxLayout();
    QLabel* bpLabel = new QLabel(tr("Black Point:"));
    m_blackPointSpin = new QDoubleSpinBox();
    m_blackPointSpin->setRange(0, 0.20);
    m_blackPointSpin->setDecimals(5);
    m_blackPointSpin->setSingleStep(0.001);
    m_blackPointSpin->setValue(0);
    bpLayout->addWidget(bpLabel);
    bpLayout->addStretch();
    bpLayout->addWidget(m_blackPointSpin);
    mainLayout->addLayout(bpLayout);
    
    m_blackPointSlider = new QSlider(Qt::Horizontal);
    m_blackPointSlider->setRange(0, 20000);  // 0-0.2 with 0.00001 precision
    m_blackPointSlider->setValue(0);
    m_blackPointSlider->setToolTip(tr("Constant value subtracted from the image."));
    mainLayout->addWidget(m_blackPointSlider);
    
    // --- Clipping Stats ---
    QHBoxLayout* clipLayout = new QHBoxLayout();
    m_lowClipLabel = new QLabel(tr("Low: 0.00%"));
    m_highClipLabel = new QLabel(tr("High: 0.00%"));
    m_lowClipLabel->setStyleSheet("color: #ff8888; margin-right: 10px;");
    m_highClipLabel->setStyleSheet("color: #8888ff;");
    clipLayout->addWidget(m_lowClipLabel);
    clipLayout->addWidget(m_highClipLabel);
    clipLayout->addStretch();
    mainLayout->addLayout(clipLayout);
    
    // --- Human Luminance ---
    m_humanLuminanceCheck = new QCheckBox(tr("Human-weighted luminance"));
    m_humanLuminanceCheck->setChecked(true);
    m_humanLuminanceCheck->setToolTip(tr("For colour images, use human eye luminous efficiency weights to compute the luminance."));
    mainLayout->addWidget(m_humanLuminanceCheck);
    
    // --- Preview ---
    m_previewCheck = new QCheckBox(tr("Preview"));
    m_previewCheck->setChecked(true);
    mainLayout->addWidget(m_previewCheck);
    
    // --- Buttons ---
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* resetBtn = new QPushButton(tr("Reset"));
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    applyBtn->setDefault(true);
    buttonLayout->addWidget(resetBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(applyBtn);
    mainLayout->addLayout(buttonLayout);
    
    // --- Connections ---
    // Slider drag: update spin only, no preview
    connect(m_stretchSlider, &QSlider::valueChanged, [this](int val){
        double dval = val / 10.0;
        m_stretchSpin->blockSignals(true);
        m_stretchSpin->setValue(dval);
        m_stretchSpin->blockSignals(false);
        m_stretch = static_cast<float>(dval);
    });
    // Slider release: trigger preview
    connect(m_stretchSlider, &QSlider::sliderReleased, this, &ArcsinhStretchDialog::updatePreview);
    
    // Spin change: sync slider and preview immediately
    connect(m_stretchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double val){
        m_stretchSlider->blockSignals(true);
        m_stretchSlider->setValue(static_cast<int>(val * 10));
        m_stretchSlider->blockSignals(false);
        m_stretch = static_cast<float>(val);
        if (m_previewCheck->isChecked()) updatePreview();
    });
    
    // Black point slider drag: update spin only
    connect(m_blackPointSlider, &QSlider::valueChanged, [this](int val){
        double dval = val / 100000.0;
        m_blackPointSpin->blockSignals(true);
        m_blackPointSpin->setValue(dval);
        m_blackPointSpin->blockSignals(false);
        m_blackPoint = static_cast<float>(dval);
    });
    // Black point slider release: trigger preview
    connect(m_blackPointSlider, &QSlider::sliderReleased, this, &ArcsinhStretchDialog::updatePreview);
    
    // Black point spin change
    connect(m_blackPointSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ArcsinhStretchDialog::onBlackPointChanged);
    
    connect(m_humanLuminanceCheck, &QCheckBox::toggled, this, &ArcsinhStretchDialog::onHumanLuminanceToggled);
    connect(m_previewCheck, &QCheckBox::toggled, this, &ArcsinhStretchDialog::onPreviewToggled);
    
    connect(resetBtn, &QPushButton::clicked, this, &ArcsinhStretchDialog::onReset);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &ArcsinhStretchDialog::onApply);
}

void ArcsinhStretchDialog::onStretchChanged(int value) {
    double dval = value / 10.0;
    m_stretchSpin->blockSignals(true);
    m_stretchSpin->setValue(dval);
    m_stretchSpin->blockSignals(false);
    m_stretch = static_cast<float>(dval);
}

void ArcsinhStretchDialog::onBlackPointChanged(double value) {
    m_blackPointSlider->blockSignals(true);
    m_blackPointSlider->setValue(static_cast<int>(value * 100000));
    m_blackPointSlider->blockSignals(false);
    m_blackPoint = static_cast<float>(value);
    if (m_previewCheck->isChecked()) updatePreview();
}

void ArcsinhStretchDialog::onHumanLuminanceToggled(bool checked) {
    m_humanLuminance = checked;
    if (m_previewCheck->isChecked()) updatePreview();
}

void ArcsinhStretchDialog::onPreviewToggled(bool checked) {
    if (checked) {
        updatePreview();
    } else {
        // Restore original
        // Restore original
        if (m_viewer && m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
        m_lowClipLabel->setText(tr("Low: 0.00%"));
        m_highClipLabel->setText(tr("High: 0.00%"));
    }
}

void ArcsinhStretchDialog::onReset() {
    m_stretchSlider->setValue(0);
    m_stretchSpin->setValue(0);
    m_blackPointSlider->setValue(0);
    m_blackPointSpin->setValue(0);
    m_humanLuminanceCheck->setChecked(true);
    
    m_stretch = 0.0f;
    m_blackPoint = 0.0f;
    m_humanLuminance = true;
    
    if (m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
    m_lowClipLabel->setText(tr("Low: 0.00%"));
    m_highClipLabel->setText(tr("High: 0.00%"));
}

void ArcsinhStretchDialog::onApply() {
    if (m_viewer && m_originalBuffer.isValid()) {
        // 1. Restore the viewer to the CLEAN state (if preview was active)
        // This is safe even if preview was off. 
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);

        // 2. Save the CLEAN state to undo stack
        m_viewer->pushUndo();

        // 3. Apply to actual buffer
        ImageBuffer buf = m_originalBuffer;
        buf.applyArcSinh(m_stretch, m_blackPoint, m_humanLuminance);
        
        // 4. Set the final buffer
        m_viewer->setBuffer(buf, m_viewer->windowTitle(), true);
        m_applied = true;
    }
    accept();
}

void ArcsinhStretchDialog::updatePreview() {
    if (!m_viewer || !m_previewCheck->isChecked() || !m_originalBuffer.isValid()) return;
    
    ImageBuffer buf = m_originalBuffer;
    buf.applyArcSinh(m_stretch, m_blackPoint, m_humanLuminance);
    m_viewer->setBuffer(buf, m_viewer->windowTitle(), true);
    
    // Update clipping stats
    updateClippingStats(buf);
}

void ArcsinhStretchDialog::updateClippingStats(const ImageBuffer& buffer) {
    long lowClip = 0, highClip = 0;
    buffer.computeClippingStats(lowClip, highClip);
    long total = static_cast<long>(buffer.width()) * buffer.height() * buffer.channels();
    
    if (total > 0) {
        float lowPct = (100.0f * lowClip) / total;
        float highPct = (100.0f * highClip) / total;
        m_lowClipLabel->setText(tr("Low: %1%").arg(lowPct, 0, 'f', 4));
        m_highClipLabel->setText(tr("High: %1%").arg(highPct, 0, 'f', 4));
    }
}
