#include "SCNRDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QPointer>
#include "../ImageViewer.h"

SCNRDialog::SCNRDialog(QWidget* parent) : DialogBase(parent, "SCNR (Remove Green Noise)", 350, 200) {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowIcon(QIcon(":/images/Logo.png"));

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Method
    QHBoxLayout* methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Protection Method:")));
    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(tr("Average Neutral"), AverageNeutral);
    m_methodCombo->addItem(tr("Maximum Neutral"), MaximumNeutral);
    m_methodCombo->addItem(tr("Minimum Neutral"), MinimumNeutral);
    m_methodCombo->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    methodLayout->addWidget(m_methodCombo);
    layout->addLayout(methodLayout);

    // Amount
    QHBoxLayout* amountLayout = new QHBoxLayout();
    amountLayout->addWidget(new QLabel(tr("Amount:")));
    
    m_amountSlider = new QSlider(Qt::Horizontal);
    m_amountSlider->setRange(0, 100);
    m_amountSlider->setValue(100); // Default 1.0
    
    m_amountSpin = new QDoubleSpinBox();
    m_amountSpin->setRange(0.0, 1.0);
    m_amountSpin->setSingleStep(0.1);
    m_amountSpin->setValue(1.0);
    
    amountLayout->addWidget(m_amountSlider);
    amountLayout->addWidget(m_amountSpin);
    layout->addLayout(amountLayout);

    // Sync
    connect(m_amountSlider, &QSlider::valueChanged, [this](int val){
        m_amountSpin->setValue(val / 100.0);
    });
    connect(m_amountSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double val){
        m_amountSlider->setValue(static_cast<int>(val * 100));
    });

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(applyBtn, &QPushButton::clicked, this, &SCNRDialog::apply);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

}

SCNRDialog::~SCNRDialog() {}

void SCNRDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

float SCNRDialog::getAmount() const {
    return static_cast<float>(m_amountSpin->value());
}

SCNRDialog::ProtectionMethod SCNRDialog::getMethod() const {
    return static_cast<ProtectionMethod>(m_methodCombo->currentData().toInt());
}
