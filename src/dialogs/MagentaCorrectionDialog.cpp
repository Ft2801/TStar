#include "MagentaCorrectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QCheckBox>
#include "../ImageViewer.h"

MagentaCorrectionDialog::MagentaCorrectionDialog(QWidget* parent)
    : DialogBase(parent, tr("Magenta Correction"), 380, 180) {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowIcon(QIcon(":/images/Logo.png"));

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Amount
    // 1.0 = No change, 0.0 = Full modulation
    QHBoxLayout* amountLayout = new QHBoxLayout();
    amountLayout->addWidget(new QLabel(tr("Amount (Mod B):")));

    m_amountSlider = new QSlider(Qt::Horizontal);
    m_amountSlider->setRange(0, 100);
    m_amountSlider->setValue(50); // Default to some effect

    m_amountSpin = new QDoubleSpinBox();
    m_amountSpin->setRange(0.0, 1.0);
    m_amountSpin->setSingleStep(0.05);
    m_amountSpin->setValue(0.5);

    amountLayout->addWidget(m_amountSlider);
    amountLayout->addWidget(m_amountSpin);
    layout->addLayout(amountLayout);

    // Threshold (Luminance)
    QHBoxLayout* threshLayout = new QHBoxLayout();
    threshLayout->addWidget(new QLabel(tr("Luma Threshold:")));

    m_threshSlider = new QSlider(Qt::Horizontal);
    m_threshSlider->setRange(0, 100);
    m_threshSlider->setValue(10); // Default low threshold

    m_threshSpin = new QDoubleSpinBox();
    m_threshSpin->setRange(0.0, 1.0);
    m_threshSpin->setSingleStep(0.01);
    m_threshSpin->setValue(0.1);

    threshLayout->addWidget(m_threshSlider);
    threshLayout->addWidget(m_threshSpin);
    layout->addLayout(threshLayout);

    // Star Mask
    m_starMaskCheck = new QCheckBox(tr("Restrict to Stars (Star Mask)"));
    m_starMaskCheck->setStyleSheet("color: white;");
    layout->addWidget(m_starMaskCheck);

    // Sync slider <-> spinbox for Amount
    connect(m_amountSlider, &QSlider::valueChanged, [this](int val) {
        m_amountSpin->setValue(val / 100.0);
    });
    connect(m_amountSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double val) {
        m_amountSlider->setValue(static_cast<int>(val * 100));
    });

    // Sync slider <-> spinbox for Threshold
    connect(m_threshSlider, &QSlider::valueChanged, [this](int val) {
        m_threshSpin->setValue(val / 100.0);
    });
    connect(m_threshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double val) {
        m_threshSlider->setValue(static_cast<int>(val * 100));
    });

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    layout->addLayout(btnLayout);

    connect(applyBtn, &QPushButton::clicked, this, &MagentaCorrectionDialog::apply);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

MagentaCorrectionDialog::~MagentaCorrectionDialog() {}

void MagentaCorrectionDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

float MagentaCorrectionDialog::getAmount() const {
    return static_cast<float>(m_amountSpin->value());
}

float MagentaCorrectionDialog::getThreshold() const {
    return static_cast<float>(m_threshSpin->value());
}

bool MagentaCorrectionDialog::isWithStarMask() const {
    return m_starMaskCheck->isChecked();
}
