/**
 * @file PreprocessingDialog.cpp
 * @brief Implementation of preprocessing dialog
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "PreprocessingDialog.h"
#include "../MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QApplication>
#include <QSettings>

//=============================================================================
// CONSTRUCTOR / DESTRUCTOR
//=============================================================================

PreprocessingDialog::PreprocessingDialog(MainWindow* parent)
    : QDialog(parent)
    , m_mainWindow(parent)
{
    setWindowTitle(tr("Image Calibration"));
    setMinimumSize(700, 600);
    resize(800, 700);
    
    setupUI();
    
    // Center on parent
    if (parent) {
        move(parent->geometry().center() - rect().center());
    }
}

PreprocessingDialog::~PreprocessingDialog() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
        m_worker->wait();
    }
}

//=============================================================================
// UI SETUP
//=============================================================================

void PreprocessingDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    
    // Masters
    setupMastersGroup();
    mainLayout->addWidget(m_mastersGroup);
    
    // Options
    setupOptionsGroup();
    mainLayout->addWidget(m_optionsGroup);
    
    // Lights and Progress side by side
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    setupLightsGroup();
    setupProgressGroup();
    bottomLayout->addWidget(m_lightsGroup, 1);
    bottomLayout->addWidget(m_progressGroup, 1);
    mainLayout->addLayout(bottomLayout);
}

void PreprocessingDialog::setupMastersGroup() {
    m_mastersGroup = new QGroupBox(tr("Master Calibration Frames"), this);
    QHBoxLayout* mainLayout = new QHBoxLayout(m_mastersGroup);
    
    // Bias
    QVBoxLayout* biasCol = new QVBoxLayout();
    biasCol->addWidget(new QLabel(tr("Bias:"), this));
    m_biasPath = new QLineEdit(this);
    m_biasPath->setPlaceholderText(tr("Select or create..."));
    biasCol->addWidget(m_biasPath);
    QHBoxLayout* biasBtn = new QHBoxLayout();
    m_selectBiasBtn = new QPushButton(tr("..."), this);
    m_selectBiasBtn->setMaximumWidth(30);
    connect(m_selectBiasBtn, &QPushButton::clicked, this, &PreprocessingDialog::onSelectBias);
    biasBtn->addWidget(m_selectBiasBtn);
    m_createBiasBtn = new QPushButton(tr("Create"), this);
    connect(m_createBiasBtn, &QPushButton::clicked, this, &PreprocessingDialog::onCreateMasterBias);
    biasBtn->addWidget(m_createBiasBtn);
    biasCol->addLayout(biasBtn);
    mainLayout->addLayout(biasCol);
    
    // Dark
    QVBoxLayout* darkCol = new QVBoxLayout();
    darkCol->addWidget(new QLabel(tr("Dark:"), this));
    m_darkPath = new QLineEdit(this);
    m_darkPath->setPlaceholderText(tr("Select or create..."));
    darkCol->addWidget(m_darkPath);
    QHBoxLayout* darkBtn = new QHBoxLayout();
    m_selectDarkBtn = new QPushButton(tr("..."), this);
    m_selectDarkBtn->setMaximumWidth(30);
    connect(m_selectDarkBtn, &QPushButton::clicked, this, &PreprocessingDialog::onSelectDark);
    darkBtn->addWidget(m_selectDarkBtn);
    m_createDarkBtn = new QPushButton(tr("Create"), this);
    connect(m_createDarkBtn, &QPushButton::clicked, this, &PreprocessingDialog::onCreateMasterDark);
    darkBtn->addWidget(m_createDarkBtn);
    darkCol->addLayout(darkBtn);
    mainLayout->addLayout(darkCol);
    
    // Flat
    QVBoxLayout* flatCol = new QVBoxLayout();
    flatCol->addWidget(new QLabel(tr("Flat:"), this));
    m_flatPath = new QLineEdit(this);
    m_flatPath->setPlaceholderText(tr("Select or create..."));
    flatCol->addWidget(m_flatPath);
    QHBoxLayout* flatBtn = new QHBoxLayout();
    m_selectFlatBtn = new QPushButton(tr("..."), this);
    m_selectFlatBtn->setMaximumWidth(30);
    connect(m_selectFlatBtn, &QPushButton::clicked, this, &PreprocessingDialog::onSelectFlat);
    flatBtn->addWidget(m_selectFlatBtn);
    m_createFlatBtn = new QPushButton(tr("Create"), this);
    connect(m_createFlatBtn, &QPushButton::clicked, this, &PreprocessingDialog::onCreateMasterFlat);
    flatBtn->addWidget(m_createFlatBtn);
    flatCol->addLayout(flatBtn);
    mainLayout->addLayout(flatCol);
    
    // Dark Flat (optional)
    QVBoxLayout* darkFlatCol = new QVBoxLayout();
    darkFlatCol->addWidget(new QLabel(tr("Dark for Flat:"), this));
    m_darkFlatPath = new QLineEdit(this);
    m_darkFlatPath->setPlaceholderText(tr("(Optional)"));
    darkFlatCol->addWidget(m_darkFlatPath);
    QHBoxLayout* darkFlatBtn = new QHBoxLayout();
    m_selectDarkFlatBtn = new QPushButton(tr("..."), this);
    m_selectDarkFlatBtn->setMaximumWidth(30);
    connect(m_selectDarkFlatBtn, &QPushButton::clicked, this, &PreprocessingDialog::onSelectDarkFlat);
    darkFlatBtn->addWidget(m_selectDarkFlatBtn);
    darkFlatCol->addLayout(darkFlatBtn);
    mainLayout->addLayout(darkFlatCol);
}

void PreprocessingDialog::setupOptionsGroup() {
    m_optionsGroup = new QGroupBox(tr("Calibration Options"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_optionsGroup);
    
    // Row 1: Enable/disable checks
    QHBoxLayout* row1 = new QHBoxLayout();
    m_useBiasCheck = new QCheckBox(tr("Use Bias calibration"), this);
    m_useBiasCheck->setChecked(true);
    row1->addWidget(m_useBiasCheck);
    
    m_useDarkCheck = new QCheckBox(tr("Use Dark calibration"), this);
    m_useDarkCheck->setChecked(true);
    row1->addWidget(m_useDarkCheck);
    
    m_useFlatCheck = new QCheckBox(tr("Use Flat calibration"), this);
    m_useFlatCheck->setChecked(true);
    row1->addWidget(m_useFlatCheck);
    
    m_darkOptimCheck = new QCheckBox(tr("Optimize dark scaling"), this);
    row1->addWidget(m_darkOptimCheck);
    layout->addLayout(row1);
    
    // Row 2: Fixes
    QHBoxLayout* fixRow = new QHBoxLayout();
    m_fixBandingCheck = new QCheckBox(tr("Fix banding"), this);
    m_fixBadLinesCheck = new QCheckBox(tr("Fix bad lines"), this);
    m_fixXTransCheck = new QCheckBox(tr("Fix X-Trans"), this);
    fixRow->addWidget(m_fixBandingCheck);
    fixRow->addWidget(m_fixBadLinesCheck);
    fixRow->addWidget(m_fixXTransCheck);
    layout->addLayout(fixRow);
    
    // Row 3: Advanced math
    QHBoxLayout* advRow = new QHBoxLayout();
    advRow->addWidget(new QLabel(tr("Bias value:"), this));
    m_biasLevelSpin = new QDoubleSpinBox(this);
    m_biasLevelSpin->setRange(0, 65535);
    m_biasLevelSpin->setValue(0);
    m_biasLevelSpin->setSpecialValueText(tr("Auto"));
    m_biasLevelSpin->setMaximumWidth(80);
    advRow->addWidget(m_biasLevelSpin);
    
    advRow->addWidget(new QLabel(tr("Pedestal:"), this));
    m_pedestalSpin = new QDoubleSpinBox(this);
    m_pedestalSpin->setRange(0, 10000);
    m_pedestalSpin->setValue(0);
    m_pedestalSpin->setMaximumWidth(80);
    advRow->addWidget(m_pedestalSpin);
    
    advRow->addWidget(new QLabel(tr("Prefix:"), this));
    m_outputPrefix = new QLineEdit(this);
    m_outputPrefix->setText("pp_");
    m_outputPrefix->setMaximumWidth(60);
    advRow->addWidget(m_outputPrefix);
    advRow->addStretch();
    layout->addLayout(advRow);

    // Row 4: Debayer
    QHBoxLayout* debayerRow = new QHBoxLayout();
    m_debayerCheck = new QCheckBox(tr("Debayer:"), this);
    debayerRow->addWidget(m_debayerCheck);
    
    debayerRow->addWidget(new QLabel(tr("Pattern:"), this));
    m_bayerPatternCombo = new QComboBox(this);
    m_bayerPatternCombo->addItem("RGGB");
    m_bayerPatternCombo->addItem("BGGR");
    m_bayerPatternCombo->addItem("GBRG");
    m_bayerPatternCombo->addItem("GRBG");
    m_bayerPatternCombo->setMaximumWidth(80);
    debayerRow->addWidget(m_bayerPatternCombo);
    
    debayerRow->addWidget(new QLabel(tr("Algorithm:"), this));
    m_debayerAlgoCombo = new QComboBox(this);
    m_debayerAlgoCombo->addItem(tr("Bilinear"));
    m_debayerAlgoCombo->addItem(tr("VNG"));
    m_debayerAlgoCombo->addItem(tr("Super Pixel"));
    m_debayerAlgoCombo->setMaximumWidth(100);
    debayerRow->addWidget(m_debayerAlgoCombo);
    
    m_cfaEqualizeCheck = new QCheckBox(tr("Equalize CFA"), this);
    debayerRow->addWidget(m_cfaEqualizeCheck);
    debayerRow->addStretch();
    layout->addLayout(debayerRow);
    
    // Row 5: Cosmetic
    QHBoxLayout* cosmRow = new QHBoxLayout();
    m_cosmeticCheck = new QCheckBox(tr("Cosmetic correction:"), this);
    cosmRow->addWidget(m_cosmeticCheck);
    
    m_cosmeticModeCombo = new QComboBox(this);
    m_cosmeticModeCombo->addItem(tr("Sigma-clipping"), 0);
    m_cosmeticModeCombo->addItem(tr("From Master Dark"), 1);
    m_cosmeticModeCombo->setMaximumWidth(120);
    cosmRow->addWidget(m_cosmeticModeCombo);

    cosmRow->addWidget(new QLabel(tr("Hot σ:"), this));
    m_hotSigmaSpin = new QDoubleSpinBox(this);
    m_hotSigmaSpin->setRange(1.0, 20.0);
    m_hotSigmaSpin->setValue(3.0);
    m_hotSigmaSpin->setMaximumWidth(70);
    cosmRow->addWidget(m_hotSigmaSpin);

    cosmRow->addWidget(new QLabel(tr("Cold σ:"), this));
    m_coldSigmaSpin = new QDoubleSpinBox(this);
    m_coldSigmaSpin->setRange(1.0, 20.0);
    m_coldSigmaSpin->setValue(3.0);
    m_coldSigmaSpin->setMaximumWidth(70);
    cosmRow->addWidget(m_coldSigmaSpin);
    
    cosmRow->addStretch();
    layout->addLayout(cosmRow);

    
    // Connect enable states
    connect(m_debayerCheck, &QCheckBox::toggled, m_bayerPatternCombo, &QWidget::setEnabled);
    connect(m_debayerCheck, &QCheckBox::toggled, m_debayerAlgoCombo, &QWidget::setEnabled);
    connect(m_debayerCheck, &QCheckBox::toggled, m_cfaEqualizeCheck, &QWidget::setEnabled);
    
    auto updateCosm = [this]() {
        bool en = m_cosmeticCheck->isChecked();
        m_cosmeticModeCombo->setEnabled(en);
        m_hotSigmaSpin->setEnabled(en);
        m_coldSigmaSpin->setEnabled(en);
    };
    connect(m_cosmeticCheck, &QCheckBox::toggled, this, updateCosm);
    
    m_bayerPatternCombo->setEnabled(false);
    m_debayerAlgoCombo->setEnabled(false);
    m_cfaEqualizeCheck->setEnabled(false);
    updateCosm();
}

void PreprocessingDialog::setupLightsGroup() {
    m_lightsGroup = new QGroupBox(tr("Light Frames to Calibrate"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_lightsGroup);
    
    // Toolbar
    QHBoxLayout* toolbar = new QHBoxLayout();
    m_addLightsBtn = new QPushButton(tr("Add Files..."), this);
    connect(m_addLightsBtn, &QPushButton::clicked, this, &PreprocessingDialog::onAddLights);
    toolbar->addWidget(m_addLightsBtn);
    
    m_removeLightsBtn = new QPushButton(tr("Remove"), this);
    connect(m_removeLightsBtn, &QPushButton::clicked, this, &PreprocessingDialog::onRemoveLights);
    toolbar->addWidget(m_removeLightsBtn);
    
    m_clearLightsBtn = new QPushButton(tr("Clear All"), this);
    connect(m_clearLightsBtn, &QPushButton::clicked, this, &PreprocessingDialog::onClearLights);
    toolbar->addWidget(m_clearLightsBtn);
    
    toolbar->addStretch();
    layout->addLayout(toolbar);
    
    // List
    m_lightsList = new QListWidget(this);
    m_lightsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_lightsList->setAlternatingRowColors(true);
    layout->addWidget(m_lightsList);
    
    // Output dir
    QHBoxLayout* dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(tr("Output directory:"), this));
    m_outputDir = new QLineEdit(this);
    m_outputDir->setPlaceholderText(tr("(Same as input)"));
    dirRow->addWidget(m_outputDir, 1);
    QPushButton* browseBtn = new QPushButton(tr("..."), this);
    browseBtn->setMaximumWidth(30);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QSettings settings("TStar", "TStar");
        QString initialDir = settings.value("Preprocessing/OutputFolder", QDir::currentPath()).toString();
        
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), initialDir);
        if (!dir.isEmpty()) {
            m_outputDir->setText(dir);
            settings.setValue("Preprocessing/OutputFolder", dir);
        }
    });
    dirRow->addWidget(browseBtn);
    layout->addLayout(dirRow);
}

void PreprocessingDialog::setupProgressGroup() {
    m_progressGroup = new QGroupBox(tr("Progress"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_progressGroup);
    
    m_progressBar = new QProgressBar(this);
    layout->addWidget(m_progressBar);
    
    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    layout->addWidget(m_logText, 1);  // Expand to fill available space
    
    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    
    m_startBtn = new QPushButton(tr("Start Calibration"), this);
    connect(m_startBtn, &QPushButton::clicked, this, &PreprocessingDialog::onStartCalibration);
    buttonRow->addWidget(m_startBtn);
    
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &PreprocessingDialog::onCancel);
    buttonRow->addWidget(m_cancelBtn);
    
    layout->addLayout(buttonRow);
}

//=============================================================================
// FILE SELECTION
//=============================================================================

void PreprocessingDialog::onSelectBias() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();
    
    QString file = QFileDialog::getOpenFileName(this,
        tr("Select Master Bias"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_biasPath->setText(file);
        settings.setValue("Preprocessing/InputFolder", QFileInfo(file).absolutePath());
    }
}

void PreprocessingDialog::onSelectDark() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();
    
    QString file = QFileDialog::getOpenFileName(this,
        tr("Select Master Dark"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_darkPath->setText(file);
        settings.setValue("Preprocessing/InputFolder", QFileInfo(file).absolutePath());
    }
}

void PreprocessingDialog::onSelectFlat() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();

    QString file = QFileDialog::getOpenFileName(this,
        tr("Select Master Flat"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_flatPath->setText(file);
        settings.setValue("Preprocessing/InputFolder", QFileInfo(file).absolutePath());
    }
}

void PreprocessingDialog::onSelectDarkFlat() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();

    QString file = QFileDialog::getOpenFileName(this,
        tr("Select Dark for Flat"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    if (!file.isEmpty()) {
        m_darkFlatPath->setText(file);
        settings.setValue("Preprocessing/InputFolder", QFileInfo(file).absolutePath());
    }
}

void PreprocessingDialog::onCreateMasterBias() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();

    QStringList files = QFileDialog::getOpenFileNames(this,
        tr("Select Bias Frames"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    
    if (files.isEmpty()) return;
    
    // Update Input Folder
    QString inputDir = QFileInfo(files.first()).absolutePath();
    settings.setValue("Preprocessing/InputFolder", inputDir);
    
    QString output = QFileDialog::getSaveFileName(this,
        tr("Save Master Bias"),
        initialDir + "/master_bias.fit",
        tr("FITS Files (*.fit)"));
    
    if (output.isEmpty()) return;
    
    // Update Input Folder (often we save master where inputs are)
    settings.setValue("Preprocessing/InputFolder", QFileInfo(output).absolutePath());
    
    m_logText->append(tr("Creating master bias from %1 frames...").arg(files.size()));
    
    Preprocessing::MasterFrames masters;
    bool success = masters.createMasterBias(files, output,
        Stacking::Method::Mean,
        Stacking::Rejection::Winsorized,
        3.0f, 3.0f,
        [this]([[maybe_unused]] const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            QApplication::processEvents();
        });
    
    if (success) {
        m_biasPath->setText(output);
        m_logText->append(tr("Master bias created: %1").arg(output));
    } else {
        m_logText->append(tr("<span style='color:red'>Failed to create master bias</span>"));
    }
    
    m_progressBar->setValue(0);
}

void PreprocessingDialog::onCreateMasterDark() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();

    QStringList files = QFileDialog::getOpenFileNames(this,
        tr("Select Dark Frames"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    
    if (files.isEmpty()) return;
    
    settings.setValue("Preprocessing/InputFolder", QFileInfo(files.first()).absolutePath());
    
    QString output = QFileDialog::getSaveFileName(this,
        tr("Save Master Dark"),
        initialDir + "/master_dark.fit",
        tr("FITS Files (*.fit)"));
    
    if (output.isEmpty()) return;
    settings.setValue("Preprocessing/InputFolder", QFileInfo(output).absolutePath());
    
    m_logText->append(tr("Creating master dark from %1 frames...").arg(files.size()));
    
    Preprocessing::MasterFrames masters;
    bool success = masters.createMasterDark(files, output,
        m_biasPath->text(),
        Stacking::Method::Mean,
        Stacking::Rejection::Winsorized,
        3.0f, 3.0f,
        [this]([[maybe_unused]] const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            QApplication::processEvents();
        });
    
    if (success) {
        m_darkPath->setText(output);
        m_logText->append(tr("Master dark created: %1").arg(output));
    } else {
        m_logText->append(tr("<span style='color:red'>Failed to create master dark</span>"));
    }
    
    m_progressBar->setValue(0);
}

void PreprocessingDialog::onCreateMasterFlat() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();

    QStringList files = QFileDialog::getOpenFileNames(this,
        tr("Select Flat Frames"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    
    if (files.isEmpty()) return;
    
    settings.setValue("Preprocessing/InputFolder", QFileInfo(files.first()).absolutePath());
    
    QString output = QFileDialog::getSaveFileName(this,
        tr("Save Master Flat"),
        initialDir + "/master_flat.fit",
        tr("FITS Files (*.fit)"));
    
    if (output.isEmpty()) return;
    settings.setValue("Preprocessing/InputFolder", QFileInfo(output).absolutePath());
    
    m_logText->append(tr("Creating master flat from %1 frames...").arg(files.size()));
    
    Preprocessing::MasterFrames masters;
    bool success = masters.createMasterFlat(files, output,
        m_biasPath->text(),
        m_darkFlatPath->text().isEmpty() ? m_darkPath->text() : m_darkFlatPath->text(),
        Stacking::Method::Mean,
        Stacking::Rejection::Winsorized,
        3.0f, 3.0f,
        [this]([[maybe_unused]] const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            QApplication::processEvents();
        });
    
    if (success) {
        m_flatPath->setText(output);
        m_logText->append(tr("Master flat created: %1").arg(output));
    } else {
        m_logText->append(tr("<span style='color:red'>Failed to create master flat</span>"));
    }
    
    m_progressBar->setValue(0);
}

void PreprocessingDialog::onAddLights() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Preprocessing/InputFolder", QDir::currentPath()).toString();

    QStringList files = QFileDialog::getOpenFileNames(this,
        tr("Select Light Frames"),
        initialDir,
        tr("FITS Files (*.fit *.fits);;All Files (*)"));
    
    if (!files.isEmpty()) {
        settings.setValue("Preprocessing/InputFolder", QFileInfo(files.first()).absolutePath());
    }
    
    for (const QString& file : files) {
        if (!m_lightFiles.contains(file)) {
            m_lightFiles.append(file);
            m_lightsList->addItem(QFileInfo(file).fileName());
        }
    }
}

void PreprocessingDialog::onRemoveLights() {
    QList<QListWidgetItem*> selected = m_lightsList->selectedItems();
    for (auto* item : selected) {
        int row = m_lightsList->row(item);
        if (row >= 0 && row < m_lightFiles.size()) {
            m_lightFiles.removeAt(row);
        }
        delete item;
    }
}

void PreprocessingDialog::onClearLights() {
    m_lightFiles.clear();
    m_lightsList->clear();
}

//=============================================================================
// CALIBRATION
//=============================================================================

Preprocessing::PreprocessParams PreprocessingDialog::gatherParams() const {
    Preprocessing::PreprocessParams params;
    
    params.masterBias = m_biasPath->text();
    params.masterDark = m_darkPath->text();
    params.masterFlat = m_flatPath->text();
    params.masterDarkFlat = m_darkFlatPath->text();
    
    params.useBias = m_useBiasCheck->isChecked() && !params.masterBias.isEmpty();
    params.useDark = m_useDarkCheck->isChecked() && !params.masterDark.isEmpty();
    params.useFlat = m_useFlatCheck->isChecked() && !params.masterFlat.isEmpty();
    
    params.darkOptim.enabled = m_darkOptimCheck->isChecked();
    
    params.debayer = m_debayerCheck->isChecked();
    int patternIdx = m_bayerPatternCombo->currentIndex();
    switch (patternIdx) {
        case 0: params.bayerPattern = Preprocessing::BayerPattern::RGGB; break;
        case 1: params.bayerPattern = Preprocessing::BayerPattern::BGGR; break;
        case 2: params.bayerPattern = Preprocessing::BayerPattern::GBRG; break;
        case 3: params.bayerPattern = Preprocessing::BayerPattern::GRBG; break;
    }
    
    int algoIdx = m_debayerAlgoCombo->currentIndex();
    switch (algoIdx) {
        case 0: params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::Bilinear; break;
        case 1: params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::VNG; break;
        case 2: params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::SuperPixel; break;
    }
    
    params.cfaEqualize.enabled = m_cfaEqualizeCheck->isChecked();
    
    if (m_cosmeticCheck->isChecked()) {
        int mode = m_cosmeticModeCombo->currentIndex();
        params.cosmetic.type = (mode == 0) ? Preprocessing::CosmeticType::Sigma : Preprocessing::CosmeticType::FromMaster;
        params.cosmetic.hotSigma = static_cast<float>(m_hotSigmaSpin->value());
        params.cosmetic.coldSigma = static_cast<float>(m_coldSigmaSpin->value());
    } else {
        params.cosmetic.type = Preprocessing::CosmeticType::None;
    }
    
    params.outputPrefix = m_outputPrefix->text();
    params.outputDir = m_outputDir->text();
    
    params.fixBanding = m_fixBandingCheck->isChecked();
    params.fixBadLines = m_fixBadLinesCheck->isChecked();
    params.fixXTrans = m_fixXTransCheck->isChecked();
    
    params.biasLevel = (m_biasLevelSpin->value() > 0) ? m_biasLevelSpin->value() : 1e30;
    params.pedestal = m_pedestalSpin->value();
    params.equalizeFlat = m_cfaEqualizeCheck->isChecked();
    
    return params;
}

void PreprocessingDialog::onStartCalibration() {
    if (m_lightFiles.isEmpty()) {
        QMessageBox::warning(this, tr("No Files"),
            tr("Please add light frames to calibrate."));
        return;
    }
    
    m_isRunning = true;
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_logText->clear();
    
    Preprocessing::PreprocessParams params = gatherParams();
    
    QString outputDir = params.outputDir.isEmpty()
        ? QFileInfo(m_lightFiles.first()).absolutePath()
        : params.outputDir;
    
    m_worker = std::make_unique<Preprocessing::PreprocessingWorker>(
        params, m_lightFiles, outputDir);
    
    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::progressChanged,
            this, &PreprocessingDialog::onProgressChanged);
    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::logMessage,
            this, &PreprocessingDialog::onLogMessage);
    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::imageProcessed,
            this, &PreprocessingDialog::onImageProcessed);
    connect(m_worker.get(), &Preprocessing::PreprocessingWorker::finished,
            this, &PreprocessingDialog::onFinished);
    
    m_logText->append(tr("Starting calibration of %1 images...").arg(m_lightFiles.size()));
    m_worker->start();
}

void PreprocessingDialog::onCancel() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
    }
}

void PreprocessingDialog::onProgressChanged([[maybe_unused]] const QString& message, double progress) {
    if (progress >= 0) {
        m_progressBar->setValue(static_cast<int>(progress * 100));
    }
}

void PreprocessingDialog::onLogMessage(const QString& message, const QString& color) {
    QString finalColor = color;
    if (finalColor.toLower() == "neutral") finalColor = "";

    if (finalColor.isEmpty()) {
        m_logText->append(message);
    } else {
        m_logText->append(QString("<span style='color:%1'>%2</span>").arg(finalColor, message));
    }
}

void PreprocessingDialog::onImageProcessed(const QString& path, bool success) {
    QString filename = QFileInfo(path).fileName();
    if (success) {
        m_logText->append(tr("✓ %1").arg(filename));
    } else {
        m_logText->append(tr("<span style='color:red'>✗ %1</span>").arg(filename));
    }
}

void PreprocessingDialog::onFinished(bool success) {
    m_isRunning = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    
    if (success) {
        m_logText->append(tr("<span style='color:green'>Calibration complete!</span>"));
    } else {
        m_logText->append(tr("<span style='color:salmon'>Calibration finished with errors</span>"));
    }
    
    m_worker.reset();
}
