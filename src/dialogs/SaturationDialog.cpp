#include "SaturationDialog.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include "../ImageViewer.h"
#include <algorithm>
#include <cmath>

SaturationDialog::SaturationDialog(QWidget* parent, ImageViewer* viewer) 
    : QDialog(parent), m_viewer(viewer), m_buffer(nullptr) {
    setWindowTitle(tr("Color Saturation"));
    setMinimumWidth(350);
    setupUI();
    
    if (m_viewer) {
        setTarget(m_viewer);
    }
}

void SaturationDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(10);
    int row = 0;

    auto addSlider = [&](const QString& label, QSlider*& sld, QLabel*& val, int min, int max, int def) {
        grid->addWidget(new QLabel(label), row, 0);
        sld = new QSlider(Qt::Horizontal);
        sld->setRange(min, max);
        sld->setValue(def);
        grid->addWidget(sld, row, 1);
        val = new QLabel(QString::number(def / 100.0, 'f', 2));
        val->setFixedWidth(40);
        grid->addWidget(val, row, 2);
        
        // Update label and trigger preview in real-time
        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::triggerPreview);
        row++;
    };

    addSlider(tr("Amount:"), m_sldAmount, m_valAmount, 0, 500, 150); // 0.0 to 5.0, default 1.5
    addSlider(tr("BG Factor:"), m_sldBgFactor, m_valBgFactor, 0, 500, 100); // 0.0 to 5.0, def 1.0
    
    // Hue Selection
    grid->addWidget(new QLabel(tr("Hue Presets:")), row, 0);
    m_cmbPresets = new QComboBox();
    m_cmbPresets->addItem(tr("All Colors"));
    m_cmbPresets->addItem(tr("Reds"));
    m_cmbPresets->addItem(tr("Yellows"));
    m_cmbPresets->addItem(tr("Greens"));
    m_cmbPresets->addItem(tr("Cyans"));
    m_cmbPresets->addItem(tr("Blues"));
    m_cmbPresets->addItem(tr("Magentas"));
    m_cmbPresets->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    grid->addWidget(m_cmbPresets, row, 1, 1, 2);
    connect(m_cmbPresets, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SaturationDialog::onPresetChanged);
    row++;

    auto addHueSlider = [&](const QString& label, QSlider*& sld, QLabel*& val, int min, int max, int def, bool deg = true) {
        grid->addWidget(new QLabel(label), row, 0);
        sld = new QSlider(Qt::Horizontal);
        sld->setRange(min, max);
        sld->setValue(def);
        grid->addWidget(sld, row, 1);
        val = new QLabel(QString::number(def) + (deg ? "°" : ""));
        val->setFixedWidth(40);
        grid->addWidget(val, row, 2);
        
        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
        connect(sld, &QSlider::valueChanged, this, &SaturationDialog::triggerPreview);
        row++;
    };

    addHueSlider(tr("Hue Center:"), m_sldHueCenter, m_valHueCenter, 0, 360, 0);
    addHueSlider(tr("Hue Width:"), m_sldHueWidth, m_valHueWidth, 0, 360, 360);
    addHueSlider(tr("Hue Smooth:"), m_sldHueSmooth, m_valHueSmooth, 0, 180, 30);

    mainLayout->addLayout(grid);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnApply = new QPushButton(tr("Apply"));
    btnApply->setDefault(true);
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));
    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnApply);
    
    mainLayout->addLayout(btnLayout);

    connect(btnApply, &QPushButton::clicked, this, &SaturationDialog::handleApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void SaturationDialog::onSliderChanged() {
    m_valAmount->setText(QString::number(m_sldAmount->value() / 100.0, 'f', 2));
    m_valBgFactor->setText(QString::number(m_sldBgFactor->value() / 100.0, 'f', 2));
    m_valHueCenter->setText(QString::number(m_sldHueCenter->value()) + "°");
    m_valHueWidth->setText(QString::number(m_sldHueWidth->value()) + "°");
    m_valHueSmooth->setText(QString::number(m_sldHueSmooth->value()) + "°");
}

void SaturationDialog::onPresetChanged(int index) {
    // 0: All, 1: Reds, 2: Yellows, 3: Greens, 4: Cyans, 5: Blues, 6: Magentas
    // Simple hue centers (0, 60, 120, 180, 240, 300)
    disconnect(m_sldHueCenter, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
    disconnect(m_sldHueWidth, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
    
    if (index == 0) {
        m_sldHueCenter->setValue(0);
        m_sldHueWidth->setValue(360);
    } else {
        m_sldHueCenter->setValue((index - 1) * 60);
        m_sldHueWidth->setValue(60);
    }
    
    connect(m_sldHueCenter, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
    connect(m_sldHueWidth, &QSlider::valueChanged, this, &SaturationDialog::onSliderChanged);
    onSliderChanged();
    emit preview(getParams()); // Also preview on preset change
}



// ... setupUI (keep mostly same but remove old constructor dependency if needed) ...

void SaturationDialog::setTarget(ImageViewer* viewer) {
    if (m_viewer == viewer) return;

    // 1. Restore OLD viewer if we have an uncommitted preview
    if (m_viewer && !m_applied) {
        // If we modified the buffer, restore it
        // We know we modified it if we have a valid backup and flags?
        // Simpler: Always restore to m_originalBuffer if valid.
        if (m_originalBuffer.isValid()) {
             m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true); // Preserve view
        }
    }

    m_viewer = viewer;
    m_applied = false;
    m_buffer = nullptr;
    m_originalBuffer = ImageBuffer(); // Clear

    // 2. Setup NEW viewer
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_buffer = &m_viewer->getBuffer();
        m_originalBuffer = *m_buffer; // Deep copy backup
        
        // Trigger preview immediately for new target
        triggerPreview();
    }
}

void SaturationDialog::handleApply() {
    // Commit current state
    if (m_viewer && m_buffer && m_originalBuffer.isValid()) {
        // 1. Restore clean state
        *m_buffer = m_originalBuffer;

        // 2. Push undo
        m_viewer->pushUndo();

        // 3. Re-apply final to buffer (since we restored it)
        m_buffer->applySaturation(getParams());

        // 4. Update display
        m_viewer->refreshDisplay(true);
        
        m_applied = true;
        m_originalBuffer = *m_buffer; 
    }
    emit applyInternal(getParams());
    accept();
}

void SaturationDialog::triggerPreview() {
    if (!m_viewer || !m_buffer || !m_originalBuffer.isValid()) return;
    
    // Reset to clean state from backup
    *m_buffer = m_originalBuffer;
    
    // Apply params
    ImageBuffer::SaturationParams params = getParams();
    m_buffer->applySaturation(params);
    
    // Refresh display
    m_viewer->refreshDisplay(true);
}

ImageBuffer::SaturationParams SaturationDialog::getParams() const {
    ImageBuffer::SaturationParams params;
    params.amount = m_sldAmount->value() / 100.0f;
    params.bgFactor = m_sldBgFactor->value() / 100.0f;
    params.hueCenter = static_cast<float>(m_sldHueCenter->value());
    params.hueWidth = static_cast<float>(m_sldHueWidth->value());
    params.hueSmooth = static_cast<float>(m_sldHueSmooth->value());
    return params;
}

SaturationDialog::State SaturationDialog::getState() const {
    State s;
    s.amount = m_sldAmount->value();
    s.bgFactor = m_sldBgFactor->value();
    s.hueCenter = m_sldHueCenter->value();
    s.hueWidth = m_sldHueWidth->value();
    s.hueSmooth = m_sldHueSmooth->value();
    s.presetIndex = m_cmbPresets->currentIndex();
    return s;
}

void SaturationDialog::setState(const State& s) {
    m_sldAmount->setValue(s.amount);
    m_sldBgFactor->setValue(s.bgFactor);
    m_sldHueCenter->setValue(s.hueCenter);
    m_sldHueWidth->setValue(s.hueWidth);
    m_sldHueSmooth->setValue(s.hueSmooth);
    if (s.presetIndex >= 0 && s.presetIndex < m_cmbPresets->count())
        m_cmbPresets->setCurrentIndex(s.presetIndex);
}

void SaturationDialog::resetState() {
    m_sldAmount->setValue(150);
    m_sldBgFactor->setValue(100);
    m_cmbPresets->setCurrentIndex(0); // All Colors
    m_sldHueSmooth->setValue(30);
}

void SaturationDialog::setBuffer(ImageBuffer* buffer) {
    m_buffer = buffer;
    if (m_buffer) {
        m_originalBuffer = *m_buffer;
        triggerPreview();
    }
}

// Destructor needed to cleanup
SaturationDialog::~SaturationDialog() {
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
}
