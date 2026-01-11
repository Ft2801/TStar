#include "TextureAndClarityDialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QCloseEvent>

#include "../ImageViewer.h"

TextureAndClarityDialog::TextureAndClarityDialog(QWidget* parent) 
    : QDialog(parent) {
    setWindowTitle("Texture and Clarity");
    setMinimumSize(500, 300);
    setMinimumWidth(400);
    setupUI();
    setupConnections();
}

void TextureAndClarityDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // ==================================
    // Texture Group
    // ==================================
    QGroupBox* textureGroup = new QGroupBox(tr("Texture"), this);
    QFormLayout* textureLayout = new QFormLayout(textureGroup);
    
    // Texture slider (-100 to +100)
    QHBoxLayout* textureSliderLayout = new QHBoxLayout();
    m_textureSlider = new QSlider(Qt::Horizontal, this);
    m_textureSlider->setRange(-100, 100);
    m_textureSlider->setValue(0);
    m_textureSlider->setTickPosition(QSlider::TicksBelow);
    m_textureSlider->setTickInterval(10);
    
    m_textureValueLabel = new QLabel("0", this);
    m_textureValueLabel->setMinimumWidth(40);
    m_textureValueLabel->setAlignment(Qt::AlignCenter);
    
    textureSliderLayout->addWidget(m_textureSlider);
    textureSliderLayout->addWidget(m_textureValueLabel);
    textureLayout->addRow(tr("Amount:"), textureSliderLayout);
    
    // Radius control
    m_radiusSpin = new QDoubleSpinBox(this);
    m_radiusSpin->setRange(0.5, 50.0);
    m_radiusSpin->setSingleStep(0.5);
    m_radiusSpin->setValue(5.0);
    m_radiusSpin->setToolTip(tr("Radius for texture detection (in pixels)"));
    textureLayout->addRow(tr("Radius:"), m_radiusSpin);
    
    mainLayout->addWidget(textureGroup);
    
    // ==================================
    // Clarity Group
    // ==================================
    QGroupBox* clarityGroup = new QGroupBox(tr("Clarity"), this);
    QFormLayout* clarityLayout = new QFormLayout(clarityGroup);
    
    // Clarity slider (-100 to +100)
    QHBoxLayout* claritySliderLayout = new QHBoxLayout();
    m_claritySlider = new QSlider(Qt::Horizontal, this);
    m_claritySlider->setRange(-100, 100);
    m_claritySlider->setValue(0);
    m_claritySlider->setTickPosition(QSlider::TicksBelow);
    m_claritySlider->setTickInterval(10);
    
    m_clarityValueLabel = new QLabel("0", this);
    m_clarityValueLabel->setMinimumWidth(40);
    m_clarityValueLabel->setAlignment(Qt::AlignCenter);
    
    claritySliderLayout->addWidget(m_claritySlider);
    claritySliderLayout->addWidget(m_clarityValueLabel);
    clarityLayout->addRow(tr("Amount:"), claritySliderLayout);
    
    mainLayout->addWidget(clarityGroup);
    
    mainLayout->addStretch();
    
    // ==================================
    // Buttons
    // ==================================
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* previewBtn = new QPushButton(tr("Preview"), this);
    previewBtn->setFixedWidth(100);
    
    QPushButton* applyBtn = new QPushButton(tr("Apply"), this);
    applyBtn->setDefault(true);
    applyBtn->setFixedWidth(100);
    applyBtn->setStyleSheet("QPushButton { background-color: #3a7d44; color: white; font-weight: bold; }");
    
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setFixedWidth(100);
    
    btnLayout->addStretch();
    btnLayout->addWidget(previewBtn);
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(cancelBtn);
    
    connect(previewBtn, &QPushButton::clicked, this, &TextureAndClarityDialog::onPreview);
    connect(applyBtn, &QPushButton::clicked, this, &TextureAndClarityDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    mainLayout->addLayout(btnLayout);
}

void TextureAndClarityDialog::setupConnections() {
    connect(m_textureSlider, &QSlider::valueChanged, this, &TextureAndClarityDialog::onTextureSliderChanged);
    connect(m_claritySlider, &QSlider::valueChanged, this, &TextureAndClarityDialog::onClaritySliderChanged);
    connect(m_radiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TextureAndClarityDialog::onRadiusChanged);
}

void TextureAndClarityDialog::updateLabels() {
    m_textureValueLabel->setText(QString::number(m_textureSlider->value()));
    m_clarityValueLabel->setText(QString::number(m_claritySlider->value()));
}

void TextureAndClarityDialog::onTextureSliderChanged(int /*value*/) {
    updateLabels();
    onPreview();
}

void TextureAndClarityDialog::onClaritySliderChanged(int /*value*/) {
    updateLabels();
    onPreview();
}

void TextureAndClarityDialog::onRadiusChanged(double /*value*/) {
    onPreview();
}

TextureAndClarityDialog::~TextureAndClarityDialog() {
    if (!m_applied && m_viewer) {
        m_viewer->clearPreviewLUT();
    }
}

void TextureAndClarityDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    qDebug() << "Texture and Clarity tool opened.";
}

void TextureAndClarityDialog::reject() {
    // Always restore when dialog is closed without applying
    if (m_viewer) {
        m_viewer->clearPreviewLUT();
        // Restore original buffer
        if (m_originalBuffer.isValid() && !m_applied) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
    }
    QDialog::reject();
}

void TextureAndClarityDialog::closeEvent(QCloseEvent* event) {
    // Ensure reject() is called when the window is closed via the X button
    if (!m_applied && m_viewer) {
        m_viewer->clearPreviewLUT();
        if (m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
    }
    QDialog::closeEvent(event);
}

void TextureAndClarityDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    
    if (m_viewer && !m_applied) {
        if (m_originalBuffer.isValid()) {
             m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
        m_viewer->clearPreviewLUT();
    }
    
    m_viewer = v;
    m_applied = false;
    m_originalBuffer = ImageBuffer();
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer();
    }
}

void TextureAndClarityDialog::triggerPreview() {
    onPreview();
}

void TextureAndClarityDialog::onPreview() {
    if (!m_viewer || !m_originalBuffer.isValid()) return;
    
    ImageBuffer::TextureAndClarityParams p = getParams();
    
    auto lut = m_originalBuffer.computeTextureAndClarityLUT(p);
    m_viewer->setPreviewLUT(lut);
}

void TextureAndClarityDialog::onApply() {
    if (m_viewer && m_originalBuffer.isValid()) {
        m_viewer->pushUndo();
        m_viewer->clearPreviewLUT();
        
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), false);
        
        ImageBuffer::TextureAndClarityParams p = getParams();
        m_viewer->getBuffer().performTextureAndClarity(p);
        m_viewer->refreshDisplay();
        
        m_applied = true;
        
        QString msg = tr("Texture and Clarity applied (T=%1, C=%2)")
                        .arg(p.texture)
                        .arg(p.clarity);
        emit applied(msg);
    }
    accept();
}

ImageBuffer::TextureAndClarityParams TextureAndClarityDialog::getParams() const {
    ImageBuffer::TextureAndClarityParams p;
    p.texture = static_cast<float>(m_textureSlider->value()) / 100.0f;
    p.clarity = static_cast<float>(m_claritySlider->value()) / 100.0f;
    p.radius = static_cast<float>(m_radiusSpin->value());
    return p;
}
