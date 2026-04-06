#include "GraXpertDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLibrary>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// GPU detection helper
// ---------------------------------------------------------------------------

/**
 * @brief Probe for a usable GPU runtime (CUDA or DirectML).
 * @return true if at least one supported GPU library can be loaded.
 */
static bool detectGpuAvailable()
{
    const bool hasCuda     = QLibrary("nvcuda").load();
    const bool hasDirectML = QLibrary("DirectML").load();
    return hasCuda || hasDirectML;
}

// ---------------------------------------------------------------------------
// Shared combo-box stylesheet (dark theme)
// ---------------------------------------------------------------------------

static const char* kComboStyle =
    "QComboBox { color: white; background-color: #2a2a2a; "
    "  border: 1px solid #555; padding: 2px; border-radius: 3px; }"
    "QComboBox:focus { border: 2px solid #4a9eff; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
    "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
    "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
    "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
    "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GraXpertDialog::GraXpertDialog(QWidget* parent)
    : DialogBase(parent, tr("GraXpert"), 350, 250)
{
    setWindowIcon(QIcon(":/images/Logo.png"));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // -- Operation group ----------------------------------------------------
    QGroupBox*   grp       = new QGroupBox(tr("Operation"), this);
    QVBoxLayout* grpLayout = new QVBoxLayout(grp);

    m_rbBackground = new QRadioButton(tr("Background Extraction"));
    m_rbDenoise    = new QRadioButton(tr("Denoise"));
    m_rbBackground->setChecked(true);

    grpLayout->addWidget(m_rbBackground);
    grpLayout->addWidget(m_rbDenoise);
    mainLayout->addWidget(grp);

    // -- Parameter form -----------------------------------------------------
    QFormLayout* form = new QFormLayout();

    m_spinStrength = new QDoubleSpinBox();
    m_spinStrength->setRange(0.0, 1.0);
    m_spinStrength->setSingleStep(0.05);
    m_spinStrength->setValue(0.10);

    m_aiVersionCombo = new QComboBox();
    m_aiVersionCombo->addItem(tr("Latest (auto)"), "Latest (auto)");
    m_aiVersionCombo->addItem("3.0.2", "3.0.2");
    m_aiVersionCombo->addItem("2.0.0", "2.0.0");
    m_aiVersionCombo->setStyleSheet(kComboStyle);

    // Restore persisted GPU preference, falling back to auto-detection.
    QSettings settings;
    const bool gpuDefault = settings.value("graxpert/use_gpu",
                                           detectGpuAvailable()).toBool();
    m_gpuCheck = new QCheckBox(tr("Use GPU Acceleration"));
    m_gpuCheck->setChecked(gpuDefault);

    form->addRow(tr("Smoothing/Strength:"), m_spinStrength);
    form->addRow(tr("AI Model:"),           m_aiVersionCombo);
    mainLayout->addLayout(form);
    mainLayout->addWidget(m_gpuCheck);

    // -- Action buttons -----------------------------------------------------
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    QPushButton* okBtn     = new QPushButton(tr("OK"));
    okBtn->setDefault(true);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);

    mainLayout->addLayout(btnLayout);

    // -- Initial state ------------------------------------------------------
    connect(m_rbBackground, &QRadioButton::toggled, this, &GraXpertDialog::updateUI);
    updateUI();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void GraXpertDialog::updateUI()
{
    // AI model version is only relevant for the denoise operation.
    const bool isBackground = m_rbBackground->isChecked();
    m_aiVersionCombo->setEnabled(!isBackground);
}

// ---------------------------------------------------------------------------
// Parameter collection
// ---------------------------------------------------------------------------

GraXpertParams GraXpertDialog::getParams() const
{
    GraXpertParams p;
    p.isDenoise = m_rbDenoise->isChecked();

    if (p.isDenoise) {
        p.strength  = m_spinStrength->value();
        p.aiVersion = m_aiVersionCombo->currentData().toString();
    } else {
        p.smoothing = m_spinStrength->value();
    }

    p.useGpu = m_gpuCheck->isChecked();

    // Persist the GPU setting for next launch.
    QSettings settings;
    settings.setValue("graxpert/use_gpu", p.useGpu);

    return p;
}