#include "CosmicClarityDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QDialogButtonBox>
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

CosmicClarityDialog::CosmicClarityDialog(QWidget* parent) : DialogBase(parent, tr("Cosmic Clarity"), 500, 500) {
    setWindowIcon(QIcon(":/images/Logo.png"));


    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QGroupBox* grp = new QGroupBox(tr("Parameters"), this);
    QGridLayout* grid = new QGridLayout(grp);
    
    // Mode
    grid->addWidget(new QLabel(tr("Mode:")), 0, 0);
    m_cmbMode = new QComboBox();
    m_cmbMode->addItem(tr("Sharpen"), "Sharpen");
    m_cmbMode->addItem(tr("Denoise"), "Denoise");
    m_cmbMode->addItem(tr("Both"), "Both");
    m_cmbMode->addItem(tr("Super Resolution"), "Super Resolution");
    connect(m_cmbMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CosmicClarityDialog::updateUI);
    grid->addWidget(m_cmbMode, 0, 1, 1, 2);

    // GPU
    grid->addWidget(new QLabel(tr("Use GPU:")), 1, 0);
    m_cmbGpu = new QComboBox();
    m_cmbGpu->addItem(tr("Yes"), true);
    m_cmbGpu->addItem(tr("No"), false);
    
    // Auto-detect GPU and set default
    bool gpuAvailable = detectGpuAvailable();
    m_cmbGpu->setCurrentIndex(gpuAvailable ? 0 : 1); // 0 = Yes, 1 = No
    
    grid->addWidget(m_cmbGpu, 1, 1);

    // --- Sharpen ---
    m_lblShMode = new QLabel(tr("Sharpening Mode:"));
    m_cmbShMode = new QComboBox(); 
    m_cmbShMode->addItem(tr("Both"), "Both");
    m_cmbShMode->addItem(tr("Stellar Only"), "Stellar Only");
    m_cmbShMode->addItem(tr("Non-Stellar Only"), "Non-Stellar Only");

    grid->addWidget(m_lblShMode, 2, 0); grid->addWidget(m_cmbShMode, 2, 1);
    
    m_chkShSep = new QCheckBox(tr("Sharpen Channels Separately"));
    grid->addWidget(m_chkShSep, 3, 0);
    m_chkAutoPsf = new QCheckBox(tr("Auto Detect PSF")); m_chkAutoPsf->setChecked(true);
    grid->addWidget(m_chkAutoPsf, 3, 1);

    m_lblPsf = new QLabel(tr("Non-Stellar PSF: 3.0"));
    m_sldPsf = new QSlider(Qt::Horizontal); m_sldPsf->setRange(10, 80); m_sldPsf->setValue(30);
    connect(m_sldPsf, &QSlider::valueChanged, [this](int v){ m_lblPsf->setText(tr("Non-Stellar PSF: %1").arg(v/10.0, 0, 'f', 1)); });
    grid->addWidget(m_lblPsf, 4, 0, 1, 3); grid->addWidget(m_sldPsf, 5, 0, 1, 3);
    
    m_lblStAmt = new QLabel(tr("Stellar Amount: 0.50"));
    m_sldStAmt = new QSlider(Qt::Horizontal); m_sldStAmt->setRange(0, 100); m_sldStAmt->setValue(50);
    connect(m_sldStAmt, &QSlider::valueChanged, [this](int v){ m_lblStAmt->setText(tr("Stellar Amount: %1").arg(v/100.0, 0, 'f', 2)); });
    grid->addWidget(m_lblStAmt, 6, 0, 1, 3); grid->addWidget(m_sldStAmt, 7, 0, 1, 3);

    m_lblNstAmt = new QLabel(tr("Non-Stellar Amount: 0.50"));
    m_sldNstAmt = new QSlider(Qt::Horizontal); m_sldNstAmt->setRange(0, 100); m_sldNstAmt->setValue(50);
    connect(m_sldNstAmt, &QSlider::valueChanged, [this](int v){ m_lblNstAmt->setText(tr("Non-Stellar Amount: %1").arg(v/100.0, 0, 'f', 2)); });
    grid->addWidget(m_lblNstAmt, 8, 0, 1, 3); grid->addWidget(m_sldNstAmt, 9, 0, 1, 3);

    // --- Denoise ---
    m_lblDnLum = new QLabel(tr("Luminance Denoise: 0.50"));
    m_sldDnLum = new QSlider(Qt::Horizontal); m_sldDnLum->setRange(0, 100); m_sldDnLum->setValue(50);
    connect(m_sldDnLum, &QSlider::valueChanged, [this](int v){ m_lblDnLum->setText(tr("Luminance Denoise: %1").arg(v/100.0, 0, 'f', 2)); });
    grid->addWidget(m_lblDnLum, 10, 0, 1, 3); grid->addWidget(m_sldDnLum, 11, 0, 1, 3);

    m_lblDnCol = new QLabel(tr("Color Denoise: 0.50"));
    m_sldDnCol = new QSlider(Qt::Horizontal); m_sldDnCol->setRange(0, 100); m_sldDnCol->setValue(50);
    connect(m_sldDnCol, &QSlider::valueChanged, [this](int v){ m_lblDnCol->setText(tr("Color Denoise: %1").arg(v/100.0, 0, 'f', 2)); });
    grid->addWidget(m_lblDnCol, 12, 0, 1, 3); grid->addWidget(m_sldDnCol, 13, 0, 1, 3);

    m_lblDnMode = new QLabel(tr("Denoise Mode:"));
    m_cmbDnMode = new QComboBox(); 
    m_cmbDnMode->addItem(tr("full"), "full");
    m_cmbDnMode->addItem(tr("luminance"), "luminance");
    grid->addWidget(m_lblDnMode, 14, 0); grid->addWidget(m_cmbDnMode, 14, 1);
    
    m_chkDnSep = new QCheckBox(tr("Process RGB Separately"));
    grid->addWidget(m_chkDnSep, 15, 1);

    // --- SuperRes ---
    m_lblScale = new QLabel(tr("Scale:"));
    m_cmbScale = new QComboBox(); m_cmbScale->addItems({"2x", "3x", "4x"}); 
    grid->addWidget(m_lblScale, 16, 0); grid->addWidget(m_cmbScale, 16, 1);

    mainLayout->addWidget(grp);

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    QLabel* copyright = new QLabel("© 2026 SetiAstro", this);
    copyright->setStyleSheet("color: gray; font-size: 10px;");
    bottomLayout->addWidget(copyright);
    bottomLayout->addStretch();
    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottomLayout->addWidget(btns);
    mainLayout->addLayout(bottomLayout);
    
    updateUI();
}

void CosmicClarityDialog::updateUI() {
    int idx = m_cmbMode->currentIndex(); // 0 Sharpen, 1 Denoise, 2 Both, 3 SuperRes
    bool showSh = (idx == 0 || idx == 2);
    bool showDn = (idx == 1 || idx == 2);
    bool showSr = (idx == 3);
    
    m_lblShMode->setVisible(showSh); m_cmbShMode->setVisible(showSh);
    m_chkShSep->setVisible(showSh); m_chkAutoPsf->setVisible(showSh);
    m_lblPsf->setVisible(showSh); m_sldPsf->setVisible(showSh);
    m_lblStAmt->setVisible(showSh); m_sldStAmt->setVisible(showSh);
    m_lblNstAmt->setVisible(showSh); m_sldNstAmt->setVisible(showSh);
    
    m_lblDnLum->setVisible(showDn); m_sldDnLum->setVisible(showDn);
    m_lblDnCol->setVisible(showDn); m_sldDnCol->setVisible(showDn);
    m_lblDnMode->setVisible(showDn); m_cmbDnMode->setVisible(showDn);
    m_chkDnSep->setVisible(showDn);

    m_lblScale->setVisible(showSr); m_cmbScale->setVisible(showSr);
    m_cmbGpu->setVisible(!showSr);
}

CosmicClarityParams CosmicClarityDialog::getParams() const {
    CosmicClarityParams p;
    int idx = m_cmbMode->currentIndex();
    if (idx == 0) p.mode = CosmicClarityParams::Mode_Sharpen;
    else if (idx == 1) p.mode = CosmicClarityParams::Mode_Denoise;
    else if (idx == 2) p.mode = CosmicClarityParams::Mode_Both;
    else p.mode = CosmicClarityParams::Mode_SuperRes;
    
    p.useGpu = m_cmbGpu->currentData().toBool();
    
    p.sharpenMode = m_cmbShMode->currentData().toString();
    p.separateChannelsSharpen = m_chkShSep->isChecked();
    p.autoPSF = m_chkAutoPsf->isChecked();
    p.nonStellarPSF = m_sldPsf->value() / 10.0f;
    p.stellarAmount = m_sldStAmt->value() / 100.0f;
    p.nonStellarAmount = m_sldNstAmt->value() / 100.0f;
    
    p.denoiseLum = m_sldDnLum->value() / 100.0f;
    p.denoiseColor = m_sldDnCol->value() / 100.0f;
    p.denoiseMode = m_cmbDnMode->currentData().toString();
    p.separateChannelsDenoise = m_chkDnSep->isChecked();
    
    p.scaleFactor = m_cmbScale->currentText();
    return p;
}
