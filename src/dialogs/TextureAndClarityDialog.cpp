#include "TextureAndClarityDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QPushButton>
#include <QDebug>
#include "../ImageViewer.h"

TextureAndClarityDialog::TextureAndClarityDialog(QWidget* parent, ImageViewer* viewer)
    : DialogBase(parent, "Texture and Clarity", 450, 0), m_viewer(nullptr), m_buffer(nullptr) {
    
    setupUI();
    
    if (viewer) {
        setViewer(viewer);
    }
    
    m_initializing = false;
}

void TextureAndClarityDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);
    
    // Controls Group
    QGroupBox* group = new QGroupBox(this); // Removed title to save space
    QFormLayout* form = new QFormLayout(group);
    form->setContentsMargins(4, 4, 4, 4);
    form->setSpacing(4);
    
    // Texture
    QHBoxLayout* texLayout = new QHBoxLayout();
    m_textureSlider = new QSlider(Qt::Horizontal, this);
    m_textureSlider->setRange(-100, 100);
    m_textureSlider->setValue(0);
    m_textureValueLabel = new QLabel("0.00", this);
    m_textureValueLabel->setFixedWidth(40);
    texLayout->addWidget(m_textureSlider);
    texLayout->addWidget(m_textureValueLabel);
    form->addRow(tr("Texture:"), texLayout);
    
    // Clarity
    QHBoxLayout* clarLayout = new QHBoxLayout();
    m_claritySlider = new QSlider(Qt::Horizontal, this);
    m_claritySlider->setRange(-100, 100);
    m_claritySlider->setValue(0);
    m_clarityValueLabel = new QLabel("0.00", this);
    m_clarityValueLabel->setFixedWidth(40);
    clarLayout->addWidget(m_claritySlider);
    clarLayout->addWidget(m_clarityValueLabel);
    form->addRow(tr("Clarity:"), clarLayout);
    
    // Radius
    QHBoxLayout* radLayout = new QHBoxLayout();
    m_radiusSlider = new QSlider(Qt::Horizontal, this);
    m_radiusSlider->setRange(1, 50); // 1-50 stops for 0.1-5.0
    m_radiusSlider->setValue(10); // 1.0 (defaulting to 1.0 instead of 5.0 since 5.0 is now max)
    m_radiusValueLabel = new QLabel("1.0", this);
    m_radiusValueLabel->setFixedWidth(40);
    radLayout->addWidget(m_radiusSlider);
    radLayout->addWidget(m_radiusValueLabel);
    form->addRow(tr("Radius:"), radLayout);
    
    // Preview Toggle
    m_previewCheckbox = new QCheckBox(tr("Live Preview"), this);
    m_previewCheckbox->setChecked(true);
    form->addRow("", m_previewCheckbox);
    
    mainLayout->addWidget(group);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(6);
    
    QPushButton* btnReset = new QPushButton(tr("Reset"), this);
    QPushButton* btnCancel = new QPushButton(tr("Cancel"), this);
    QPushButton* btnApply = new QPushButton(tr("Apply"), this);
    btnApply->setDefault(true);
    
    btnLayout->addWidget(btnReset);
    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnApply);
    mainLayout->addLayout(btnLayout);
    
    // Connections
    connect(m_textureSlider, &QSlider::valueChanged, this, &TextureAndClarityDialog::onTextureSliderChanged);
    connect(m_claritySlider, &QSlider::valueChanged, this, &TextureAndClarityDialog::onClaritySliderChanged);
    connect(m_radiusSlider, &QSlider::valueChanged, this, &TextureAndClarityDialog::onRadiusSliderChanged);
    
    connect(m_textureSlider, &QSlider::sliderReleased, this, &TextureAndClarityDialog::triggerPreview);
    connect(m_claritySlider, &QSlider::sliderReleased, this, &TextureAndClarityDialog::triggerPreview);
    connect(m_radiusSlider, &QSlider::sliderReleased, this, &TextureAndClarityDialog::triggerPreview);
    connect(m_previewCheckbox, &QCheckBox::toggled, this, &TextureAndClarityDialog::triggerPreview);
    
    connect(btnReset, &QPushButton::clicked, this, &TextureAndClarityDialog::onReset);
    connect(btnApply, &QPushButton::clicked, this, &TextureAndClarityDialog::onApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    // Internal Signals
    // Using slider signals directly to trigger preview updates via slots
}

TextureAndClarityDialog::~TextureAndClarityDialog() {
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
}

void TextureAndClarityDialog::setViewer(ImageViewer* viewer) {
    if (m_viewer == viewer) return;
    
    // Restore old viewer if needed
    if (m_viewer && !m_applied && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
    
    m_viewer = viewer;
    m_applied = false;
    m_buffer = nullptr;
    m_originalBuffer = ImageBuffer();
    
    if (m_viewer) {
        // Safety: Handle viewer destruction
        connect(m_viewer, &QObject::destroyed, this, [this](){
            m_viewer = nullptr;
            m_buffer = nullptr;
            m_originalBuffer = ImageBuffer();
        });

        if (m_viewer && m_viewer->getBuffer().isValid()) {
            m_originalBuffer = m_viewer->getBuffer();
            m_buffer = &m_viewer->getBuffer();
            triggerPreview();
        }
    }
}

void TextureAndClarityDialog::triggerPreview() {
    if (!m_viewer || !m_originalBuffer.isValid() || m_initializing) return;
    
    // Always restore clean state first
    *m_buffer = m_originalBuffer;

    if (m_previewCheckbox && m_previewCheckbox->isChecked()) {
        ImageBuffer::TextureAndClarityParams params = getParams();
        // Apply params to live buffer
        m_buffer->performTextureAndClarity(params);
    }
    
    // Refresh viewer display
    m_viewer->refreshDisplay(true);
}

void TextureAndClarityDialog::onApply() {
    if (m_viewer && m_originalBuffer.isValid()) {
        // 1. Restore clean state
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), false); // Restore original to viewer's buffer
        m_buffer = &m_viewer->getBuffer(); // Ensure m_buffer points to the viewer's current buffer

        // 2. Push undo
        m_viewer->pushUndo();

        // 3. Re-apply final to buffer
        ImageBuffer::TextureAndClarityParams params = getParams();
        m_buffer->performTextureAndClarity(params);

        // 4. Update display
        m_viewer->refreshDisplay(true);
        
        m_applied = true;
        m_originalBuffer = *m_buffer; 
        
        QString msg = tr("Texture: %1, Clarity: %2").arg(params.texture).arg(params.clarity);
        emit applied(msg);
    }
    accept();
}

ImageBuffer::TextureAndClarityParams TextureAndClarityDialog::getParams() const {
    ImageBuffer::TextureAndClarityParams p;
    p.texture = static_cast<float>(m_textureSlider->value()) / 100.0f;
    p.clarity = static_cast<float>(m_claritySlider->value()) / 100.0f;
    // Slider 1-50 maps to 0.1-5.0 radius
    p.radius = static_cast<float>(m_radiusSlider->value()) / 10.0f;
    return p;
}

void TextureAndClarityDialog::onTextureSliderChanged(int /*value*/) {
    updateLabels();
}

void TextureAndClarityDialog::onClaritySliderChanged(int /*value*/) {
    updateLabels();
}

void TextureAndClarityDialog::onRadiusSliderChanged(int /*value*/) {
    updateLabels();
}

void TextureAndClarityDialog::updateLabels() {
    if (m_textureSlider) m_textureValueLabel->setText(QString::number(m_textureSlider->value() / 100.0, 'f', 2));
    if (m_claritySlider) m_clarityValueLabel->setText(QString::number(m_claritySlider->value() / 100.0, 'f', 2));
    if (m_radiusSlider) m_radiusValueLabel->setText(QString::number(m_radiusSlider->value() / 10.0, 'f', 1));
}

void TextureAndClarityDialog::showEvent(QShowEvent* event) {
    DialogBase::showEvent(event);
    updateLabels();
}

TextureAndClarityDialog::State TextureAndClarityDialog::getState() const {
    State s;
    s.texture = m_textureSlider->value();
    s.clarity = m_claritySlider->value();
    s.radius = m_radiusSlider->value();
    s.preview = m_previewCheckbox->isChecked();
    return s;
}

void TextureAndClarityDialog::setState(const State& s) {
    m_textureSlider->setValue(s.texture);
    m_claritySlider->setValue(s.clarity);
    m_radiusSlider->setValue(s.radius);
    m_previewCheckbox->setChecked(s.preview);
}

void TextureAndClarityDialog::resetState() {
    m_textureSlider->setValue(0);
    m_claritySlider->setValue(0);
    m_radiusSlider->setValue(10); // 1.0
    m_previewCheckbox->setChecked(true);
}
void TextureAndClarityDialog::onReset() {
    m_initializing = true;
    resetState();
    updateLabels();
    m_initializing = false;
    triggerPreview();
}

void TextureAndClarityDialog::onPreview() {
    triggerPreview();
}