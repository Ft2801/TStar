#include "TemperatureTintDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include "../ImageViewer.h"
#include <algorithm>
#include <cmath>

TemperatureTintDialog::TemperatureTintDialog(QWidget* parent, ImageViewer* viewer)
    : DialogBase(parent, tr("Temperature / Tint"), 420, 180), m_viewer(nullptr), m_buffer(nullptr)
{
    setupUI();
    if (viewer) {
        setViewer(viewer);
    }
}

TemperatureTintDialog::~TemperatureTintDialog() {
    if (!m_applied && m_viewer && m_originalBuffer.isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    }
}

void TemperatureTintDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(10);

    auto addSlider = [&](int row, const QString& label, QSlider*& sld, QLabel*& val, int min, int max, int def) {
        // Min label
        QLabel* minLbl = new QLabel(QString::number(min));
        minLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        minLbl->setFixedWidth(30);
        grid->addWidget(new QLabel(label), row, 0);
        grid->addWidget(minLbl, row, 1);
        sld = new QSlider(Qt::Horizontal);
        sld->setRange(min, max);
        sld->setValue(def);
        grid->addWidget(sld, row, 2);
        QLabel* maxLbl = new QLabel(QString::number(max));
        maxLbl->setFixedWidth(30);
        grid->addWidget(maxLbl, row, 3);
        val = new QLabel(QString::number(def));
        val->setFixedWidth(40);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(val, row, 4);
        connect(sld, &QSlider::valueChanged, this, &TemperatureTintDialog::onSliderChanged);
        connect(sld, &QSlider::valueChanged, this, &TemperatureTintDialog::triggerPreview);
    };

    addSlider(0, tr("Temperature:"), m_sldTemperature, m_valTemperature, -100, 100, 0);
    addSlider(1, tr("Tint:"),        m_sldTint,        m_valTint,        -100, 100, 0);

    mainLayout->addLayout(grid);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_chkPreview = new QCheckBox(tr("Preview"));
    m_chkPreview->setChecked(true);
    connect(m_chkPreview, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            triggerPreview();
        } else if (m_viewer && m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
    });

    QPushButton* btnReset  = new QPushButton(tr("Reset"));
    QPushButton* btnApply  = new QPushButton(tr("Apply"));
    btnApply->setDefault(true);
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));

    btnLayout->addWidget(m_chkPreview);
    btnLayout->addStretch();
    btnLayout->addWidget(btnReset);
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnApply);
    mainLayout->addLayout(btnLayout);

    connect(btnReset,  &QPushButton::clicked, this, &TemperatureTintDialog::resetState);
    connect(btnApply,  &QPushButton::clicked, this, &TemperatureTintDialog::handleApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void TemperatureTintDialog::onSliderChanged() {
    m_valTemperature->setText(QString::number(m_sldTemperature->value()));
    m_valTint->setText(QString::number(m_sldTint->value()));
}

void TemperatureTintDialog::computeGain(float& r, float& g, float& b) const {
    // Temperature: positive = warm (more red, less blue)
    //              negative = cool (less red, more blue)
    float T  = m_sldTemperature->value() / 100.0f; // -1.0 to +1.0
    // Tint: positive = magenta (less green, slightly more R+B)
    //       negative = green   (more green, slightly less R+B)
    float Ti = m_sldTint->value() / 100.0f;        // -1.0 to +1.0

    r = 1.0f + T * 0.5f;           // 0.5 to 1.5
    b = 1.0f - T * 0.5f;           // 0.5 to 1.5
    g = 1.0f - Ti * 0.5f;          // 0.5 to 1.5  (positive = magenta = lower G)

    // Small R/B boost for positive tint (magenta direction)
    r *= 1.0f + Ti * 0.15f;
    b *= 1.0f + Ti * 0.15f;

    // Clamp to sensible range
    r = std::max(0.01f, r);
    g = std::max(0.01f, g);
    b = std::max(0.01f, b);
}

void TemperatureTintDialog::triggerPreview() {
    if (!m_viewer || !m_buffer || !m_originalBuffer.isValid()) return;
    if (m_chkPreview && !m_chkPreview->isChecked()) return;

    // Reset to clean state
    *m_buffer = m_originalBuffer;

    float r, g, b;
    computeGain(r, g, b);
    m_buffer->applyWhiteBalance(r, g, b);
    m_viewer->refreshDisplay(true);
}

void TemperatureTintDialog::handleApply() {
    if (m_viewer && m_buffer && m_originalBuffer.isValid()) {
        // Restore clean state
        *m_buffer = m_originalBuffer;

        // Push undo
        m_viewer->pushUndo();

        // Apply final values
        float r, g, b;
        computeGain(r, g, b);
        m_buffer->applyWhiteBalance(r, g, b);

        m_viewer->refreshDisplay(true);
        m_applied = true;
    }
    emit applyInternal();
    accept();
}

void TemperatureTintDialog::setViewer(ImageViewer* viewer) {
    if (m_viewer == viewer) return;

    // Restore old viewer if we have an uncommitted preview
    if (m_viewer) {
        disconnect(m_viewer, &QObject::destroyed, this, nullptr);
        if (!m_applied && m_originalBuffer.isValid()) {
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        }
    }

    m_viewer = viewer;
    m_applied = false;
    m_buffer = nullptr;
    m_originalBuffer = ImageBuffer();

    if (m_viewer) {
        connect(m_viewer, &QObject::destroyed, this, [this]() {
            m_viewer = nullptr;
            m_buffer = nullptr;
            m_originalBuffer = ImageBuffer();
        });

        if (m_viewer->getBuffer().isValid()) {
            m_buffer = &m_viewer->getBuffer();
            m_originalBuffer = *m_buffer;
            triggerPreview();
        }
    }
}

TemperatureTintDialog::State TemperatureTintDialog::getState() const {
    return { m_sldTemperature->value(), m_sldTint->value() };
}

void TemperatureTintDialog::setState(const State& s) {
    m_sldTemperature->setValue(s.temperature);
    m_sldTint->setValue(s.tint);
}

void TemperatureTintDialog::resetState() {
    m_sldTemperature->setValue(0);
    m_sldTint->setValue(0);
}
