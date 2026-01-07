#include "GraXpertDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QGroupBox>
#include <QSettings>
#include <QIcon>

#include <QLibrary>

// Helper to detect best GPU provider
static bool detectGpuAvailable() {
    // Check for CUDA (NVIDIA) - look for nvcuda.dll
    bool hasCuda = QLibrary("nvcuda").load();
    // Check for DirectML (AMD/Intel) - available on Windows 10+
    bool hasDirectML = QLibrary("DirectML").load();
    return hasCuda || hasDirectML;
}

GraXpertDialog::GraXpertDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("GraXpert"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    resize(350, 250);

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QGroupBox* grp = new QGroupBox(tr("Operation"), this);
    QVBoxLayout* grpLayout = new QVBoxLayout(grp);
    
    m_rbBackground = new QRadioButton(tr("Background Extraction"));
    m_rbDenoise = new QRadioButton(tr("Denoise"));
    m_rbBackground->setChecked(true); // Default
    
    grpLayout->addWidget(m_rbBackground);
    grpLayout->addWidget(m_rbDenoise);
    mainLayout->addWidget(grp);

    QFormLayout* form = new QFormLayout();
    
    m_spinStrength = new QDoubleSpinBox();
    m_spinStrength->setRange(0.0, 1.0);
    m_spinStrength->setSingleStep(0.05);
    m_spinStrength->setValue(0.10); // Smoothing default

    m_aiVersionCombo = new QComboBox();
    m_aiVersionCombo->addItem(tr("Latest (auto)"), "Latest (auto)");
    m_aiVersionCombo->addItem("3.0.2", "3.0.2");
    m_aiVersionCombo->addItem("3.0.1", "3.0.1");
    m_aiVersionCombo->addItem("2.0.0", "2.0.0");
    m_aiVersionCombo->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    
    QSettings s;
    bool gpuDefault = s.value("graxpert/use_gpu", detectGpuAvailable()).toBool();
    m_gpuCheck = new QCheckBox(tr("Use GPU Acceleration"));
    m_gpuCheck->setChecked(gpuDefault);

    form->addRow(tr("Smoothing/Strength:"), m_spinStrength);
    form->addRow(tr("AI Model:"), m_aiVersionCombo);
    mainLayout->addLayout(form);
    
    mainLayout->addWidget(m_gpuCheck);

    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btns);

    connect(m_rbBackground, &QRadioButton::toggled, this, &GraXpertDialog::updateUI);
    updateUI();
}

void GraXpertDialog::updateUI() {
    bool isBg = m_rbBackground->isChecked();
    if (isBg) {
        m_aiVersionCombo->setEnabled(false);
    } else {
        m_aiVersionCombo->setEnabled(true);
    }
}

GraXpertParams GraXpertDialog::getParams() const {
    GraXpertParams p;
    p.isDenoise = m_rbDenoise->isChecked();
    if (p.isDenoise) {
        p.strength = m_spinStrength->value();
        p.aiVersion = m_aiVersionCombo->currentData().toString();
    } else {
        p.smoothing = m_spinStrength->value();
    }
    p.useGpu = m_gpuCheck->isChecked();
    
    // Persist GPU setting
    QSettings s;
    s.setValue("graxpert/use_gpu", p.useGpu);
    
    return p;
}
