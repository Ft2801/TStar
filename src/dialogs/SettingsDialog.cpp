#include "SettingsDialog.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include "network/ModelDownloader.h"
#include "network/AstapDownloader.h"
#include "astrometry/AstapSolver.h"

SettingsDialog::SettingsDialog(QWidget* parent) : DialogBase(parent, tr("Settings"), 650, 450) {

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Layout a due colonne
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    QVBoxLayout* leftColumn = new QVBoxLayout();
    QVBoxLayout* rightColumn = new QVBoxLayout();


    // --- General Group ---
    QGroupBox* generalGroup = new QGroupBox(tr("General"), this);
    QFormLayout* generalForm = new QFormLayout(generalGroup);
    
    m_langCombo = new QComboBox();
    m_langCombo->addItem(tr("System Default"), "System");
    m_langCombo->addItem("English", "en");
    m_langCombo->addItem("Italiano", "it");
    m_langCombo->addItem("Español", "es");
    m_langCombo->addItem("Français", "fr");
    m_langCombo->addItem("Deutsch", "de");
    
    generalForm->addRow(tr("Language:"), m_langCombo);
    
    // Workspace Color Profile
    m_workspaceProfileCombo = new QComboBox();
    m_workspaceProfileCombo->addItem(tr("sRGB IEC61966-2.1"), "sRGB");
    m_workspaceProfileCombo->addItem(tr("Adobe RGB (1998)"), "AdobeRGB");
    m_workspaceProfileCombo->addItem(tr("ProPhoto RGB"), "ProPhotoRGB");
    m_workspaceProfileCombo->addItem(tr("Linear RGB"), "LinearRGB");
    generalForm->addRow(tr("Workspace Color Profile:"), m_workspaceProfileCombo);
    
    m_autoConversionCombo = new QComboBox();
    m_autoConversionCombo->addItem(tr("Ask"), "Ask");
    m_autoConversionCombo->addItem(tr("Never"), "Never");
    m_autoConversionCombo->addItem(tr("Always"), "Always");
    generalForm->addRow(tr("Auto-Convert Color Profiles:"), m_autoConversionCombo);

    leftColumn->addWidget(generalGroup);
    
    // --- Display Group ---
    QGroupBox* displayGroup = new QGroupBox(tr("Display"), this);
    QFormLayout* displayForm = new QFormLayout(displayGroup);

    m_defaultStretchCombo = new QComboBox();
    m_defaultStretchCombo->addItem(tr("Linear"), "Linear");
    m_defaultStretchCombo->addItem(tr("Auto Stretch"), "AutoStretch");
    m_defaultStretchCombo->addItem(tr("Histogram"), "Histogram");
    m_defaultStretchCombo->addItem(tr("ArcSinh"), "ArcSinh");
    m_defaultStretchCombo->addItem(tr("Square Root"), "Sqrt");
    m_defaultStretchCombo->addItem(tr("Logarithmic"), "Log");
    displayForm->addRow(tr("Default Display Stretch:"), m_defaultStretchCombo);

    m_24bitStfCheck = new QCheckBox(tr("24-bit Autostretch (Smoother gradients)"));
    displayForm->addRow("", m_24bitStfCheck);

    m_hideMagnifierCheck = new QCheckBox(tr("Hide Magnifier Viewer"));
    displayForm->addRow("", m_hideMagnifierCheck);

    leftColumn->addWidget(displayGroup);

    // --- Paths & Integrations Group ---
    QGroupBox* pathsGroup = new QGroupBox(tr("Paths and Integrations"), this);
    QFormLayout* form = new QFormLayout(pathsGroup);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    // GraXpert
    m_graxpertPath = new QLineEdit();
    QPushButton* btnGraX = new QPushButton(tr("Browse..."));
    connect(btnGraX, &QPushButton::clicked, this, &SettingsDialog::pickGraXpertPath);
    
    QHBoxLayout* rowGraX = new QHBoxLayout();
    rowGraX->addWidget(m_graxpertPath);
    rowGraX->addWidget(btnGraX);
    form->addRow(tr("GraXpert Executable:"), rowGraX);

    // StarNet
    m_starnetPath = new QLineEdit();
    QPushButton* btnSN = new QPushButton(tr("Browse..."));
    connect(btnSN, &QPushButton::clicked, this, &SettingsDialog::pickStarNetPath);

    QHBoxLayout* rowSN = new QHBoxLayout();
    rowSN->addWidget(m_starnetPath);
    rowSN->addWidget(btnSN);
    form->addRow(tr("StarNet Executable:"), rowSN);

    // ASTAP Database (CLI is bundled with the app)
    m_astapPath = new QLineEdit();
    m_astapPath->setPlaceholderText(tr("Optional manual database folder (D50/D80/etc.)"));
    QPushButton* btnAstap = new QPushButton(tr("Browse..."));
    connect(btnAstap, &QPushButton::clicked, this, &SettingsDialog::pickAstapPath);

    QHBoxLayout* rowAstap = new QHBoxLayout();
    rowAstap->addWidget(m_astapPath);
    rowAstap->addWidget(btnAstap);
    form->addRow(tr("ASTAP Database Folder:"), rowAstap);

    connect(m_astapPath, &QLineEdit::textChanged, this, &SettingsDialog::refreshAstapStatus);

    m_astapExtraArgs = new QLineEdit();
    m_astapExtraArgs->setPlaceholderText("-s 500 -log");
    form->addRow(tr("ASTAP Extra Args:"), m_astapExtraArgs);

    // Project root
    m_projectRootPath = new QLineEdit();
    m_projectRootPath->setPlaceholderText(tr("Default: inside each project folder"));
    QPushButton* btnProjRoot = new QPushButton(tr("Browse..."));
    connect(btnProjRoot, &QPushButton::clicked, this, &SettingsDialog::pickProjectRootPath);
    QHBoxLayout* rowProjRoot = new QHBoxLayout();
    rowProjRoot->addWidget(m_projectRootPath);
    rowProjRoot->addWidget(btnProjRoot);
    form->addRow(tr("Project Files Root:"), rowProjRoot);

    leftColumn->addWidget(pathsGroup);
    leftColumn->addStretch();

    // --- Updates Group ---
    QGroupBox* updatesGroup = new QGroupBox(tr("Updates"), this);
    QVBoxLayout* updatesLayout = new QVBoxLayout(updatesGroup);
    m_checkUpdates = new QCheckBox(tr("Check for updates on startup"));
    updatesLayout->addWidget(m_checkUpdates);
    
    rightColumn->addWidget(updatesGroup);

    // --- Cosmic Clarity Models Group ---
    QGroupBox* modelsGroup = new QGroupBox(tr("Cosmic Clarity models"), this);
    QVBoxLayout* modelsLayout = new QVBoxLayout(modelsGroup);

    m_lblModelsStatus = new QLabel();
    modelsLayout->addWidget(m_lblModelsStatus);

    m_btnDownloadModels = new QPushButton(tr("Download latest Cosmic Clarity models"));
    connect(m_btnDownloadModels, &QPushButton::clicked, this, &SettingsDialog::downloadModels);
    modelsLayout->addWidget(m_btnDownloadModels);

    rightColumn->addWidget(modelsGroup);

    // --- ASTAP Database Group ---
    QGroupBox* astapGroup = new QGroupBox(tr("ASTAP Star Database"), this);
    QVBoxLayout* astapLayout = new QVBoxLayout(astapGroup);

    m_lblAstapStatus = new QLabel();
    astapLayout->addWidget(m_lblAstapStatus);

    m_btnDownloadAstap = new QPushButton(tr("Download ASTAP D50 Star Database"));
    connect(m_btnDownloadAstap, &QPushButton::clicked, this, &SettingsDialog::downloadAstapCatalog);
    astapLayout->addWidget(m_btnDownloadAstap);

    rightColumn->addWidget(astapGroup);
    rightColumn->addStretch(); 

    columnsLayout->addLayout(leftColumn);
    columnsLayout->addLayout(rightColumn);
    mainLayout->addLayout(columnsLayout);

    refreshModelsStatus();
    refreshAstapStatus();
    
    // --- Buttons ---
    mainLayout->addStretch();
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    QPushButton* okBtn = new QPushButton(tr("OK"));
    okBtn->setDefault(true);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    mainLayout->addLayout(btnLayout);
    
    // --- Load Settings ---
    m_graxpertPath->setText(m_settings.value("paths/graxpert").toString());
    m_starnetPath->setText(m_settings.value("paths/starnet").toString());
    QString astapDbPath = m_settings.value("paths/astap_database").toString();
    if (astapDbPath.isEmpty()) {
        const QString legacyAstapPath = m_settings.value("paths/astap").toString();
        QFileInfo legacyInfo(legacyAstapPath);
        if (legacyInfo.exists() && legacyInfo.isDir()) {
            astapDbPath = legacyInfo.absoluteFilePath();
        }
    }
    m_astapPath->setText(astapDbPath);
    m_astapExtraArgs->setText(m_settings.value("paths/astap_extra").toString());
    m_projectRootPath->setText(m_settings.value("paths/project_root").toString());
    
    QString savedLang = m_settings.value("general/language", "System").toString();
    int idx = m_langCombo->findData(savedLang);
    if (idx != -1) m_langCombo->setCurrentIndex(idx);
    
    // Load workspace color profile preference
    QString savedProfile = m_settings.value("color/workspace_profile", "sRGB").toString();
    idx = m_workspaceProfileCombo->findData(savedProfile);
    if (idx != -1) m_workspaceProfileCombo->setCurrentIndex(idx);
    
    // Load auto-conversion mode preference
    QString savedMode = m_settings.value("color/auto_conversion_mode", "Ask").toString();
    idx = m_autoConversionCombo->findData(savedMode);
    if (idx != -1) m_autoConversionCombo->setCurrentIndex(idx);

    m_checkUpdates->setChecked(m_settings.value("general/check_updates", true).toBool());

    // Load default stretch mode preference
    QString savedStretch = m_settings.value("display/default_stretch", "Linear").toString();
    idx = m_defaultStretchCombo->findData(savedStretch);
    if (idx != -1) m_defaultStretchCombo->setCurrentIndex(idx);

    // Default to true 
    m_24bitStfCheck->setChecked(m_settings.value("display/24bit_stf", true).toBool());
    m_hideMagnifierCheck->setChecked(m_settings.value("display/hide_magnifier", false).toBool());
}

void SettingsDialog::pickStarNetPath() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select StarNet++ Executable"), "", tr("Executables (*.exe);;All Files (*)"));
    if (!path.isEmpty()) {
        m_starnetPath->setText(path);
        m_settings.setValue("paths/starnet", path);
        m_settings.sync();
    }
}

void SettingsDialog::pickAstapPath() {
    QString startDir = m_astapPath->text().trimmed();
    if (startDir.isEmpty()) {
        startDir = QDir::homePath();
    }

    QString path = QFileDialog::getExistingDirectory(this, tr("Select ASTAP Database Folder"), startDir);
    if (!path.isEmpty()) {
        m_astapPath->setText(path);
        m_settings.setValue("paths/astap_database", path);
        m_settings.sync();
    }
}

void SettingsDialog::pickGraXpertPath() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select GraXpert Executable"), "", tr("Executables (*.exe);;All Files (*)"));
    if (!path.isEmpty()) {
        m_graxpertPath->setText(path);
        m_settings.setValue("paths/graxpert", path);
        m_settings.sync();
    }
}

void SettingsDialog::downloadModels() {
    m_btnDownloadModels->setEnabled(false);
    m_lblModelsStatus->setText(tr("Preparing…"));

    auto* downloader = new ModelDownloader(this);

    auto* progressDlg = new QProgressDialog(tr("Downloading Cosmic Clarity models..."), tr("Cancel"), 0, 100, this);
    progressDlg->setWindowModality(Qt::WindowModal);
    progressDlg->setMinimumDuration(0);
    progressDlg->setAutoClose(false);
    progressDlg->setAutoReset(false);
    progressDlg->show();

    connect(progressDlg, &QProgressDialog::canceled, downloader, &ModelDownloader::cancel);

    connect(downloader, &ModelDownloader::progress, this, [progressDlg](const QString& msg) {
        progressDlg->setLabelText(msg);
    });

    connect(downloader, &ModelDownloader::progressValue, this, [progressDlg](int val) {
        if (val < 0) {
            progressDlg->setRange(0, 0); // Indeterminate
        } else {
            progressDlg->setRange(0, 100);
            progressDlg->setValue(val);
        }
    });

    connect(downloader, &ModelDownloader::finished, this, [this, downloader, progressDlg](bool ok, const QString& msg) {
        progressDlg->close();
        progressDlg->deleteLater();

        if (ok) {
            QMessageBox::information(this, tr("Cosmic Clarity models"), 
                tr("Models downloaded and installed successfully."));
        } else {
            QMessageBox::warning(this, tr("Cosmic Clarity models"), 
                tr("Failed to download models: %1").arg(msg));
        }

        refreshModelsStatus();
        m_btnDownloadModels->setEnabled(true);
        downloader->deleteLater();
    });

    downloader->startDownload();
}

void SettingsDialog::downloadAstapCatalog() {
    m_btnDownloadAstap->setEnabled(false);

    auto* downloader = new AstapDownloader(this);

    auto* progressDlg = new QProgressDialog(tr("Downloading ASTAP D50 Catalog..."), tr("Cancel"), 0, 100, this);
    progressDlg->setWindowModality(Qt::WindowModal);
    progressDlg->setMinimumDuration(0);
    progressDlg->setAutoClose(false);
    progressDlg->setAutoReset(false);
    progressDlg->show();

    connect(progressDlg, &QProgressDialog::canceled, downloader, &AstapDownloader::cancel);

    connect(downloader, &AstapDownloader::progress, this, [progressDlg](const QString& msg) {
        progressDlg->setLabelText(msg);
    });

    connect(downloader, &AstapDownloader::progressValue, this, [progressDlg](int val) {
        if (val < 0) {
            progressDlg->setRange(0, 0); // Indeterminate
        } else {
            progressDlg->setRange(0, 100);
            progressDlg->setValue(val);
        }
    });

    connect(downloader, &AstapDownloader::finished, this, [this, downloader, progressDlg](bool ok, const QString& msg) {
        progressDlg->close();
        progressDlg->deleteLater();

        if (ok) {
            QMessageBox::information(this, tr("ASTAP Database Download"), msg);
        } else {
            QMessageBox::warning(this, tr("ASTAP Database Download"), msg);
        }

        refreshAstapStatus();
        m_btnDownloadAstap->setEnabled(true);
        downloader->deleteLater();
    });

    downloader->startDownload();
}

void SettingsDialog::refreshModelsStatus() {
    if (ModelDownloader::modelsInstalled()) {
        m_lblModelsStatus->setText(tr("Cosmic Clarity models: installed"));
    } else {
        m_lblModelsStatus->setText(tr("Cosmic Clarity models: not installed"));
    }
}

void SettingsDialog::saveSettings() {
    m_settings.setValue("paths/graxpert", m_graxpertPath->text());
    m_settings.setValue("paths/starnet", m_starnetPath->text());
    m_settings.setValue("paths/astap_database", m_astapPath->text());
    // Legacy executable setting is no longer exposed in UI; prefer bundled CLI.
    m_settings.remove("paths/astap");
    m_settings.setValue("paths/astap_extra", m_astapExtraArgs->text());
    m_settings.setValue("paths/project_root", m_projectRootPath->text());

    QString oldLang = m_settings.value("general/language", "System").toString();
    QString newLang = m_langCombo->currentData().toString();
    m_settings.setValue("general/language", newLang);
    m_settings.setValue("general/check_updates", m_checkUpdates->isChecked());
    m_settings.setValue("display/24bit_stf", m_24bitStfCheck->isChecked());
    m_settings.setValue("display/hide_magnifier", m_hideMagnifierCheck->isChecked());
    
    // Save default stretch mode
    QString newStretch = m_defaultStretchCombo->currentData().toString();
    m_settings.setValue("display/default_stretch", newStretch);

    // Save workspace color profile preference
    QString newProfile = m_workspaceProfileCombo->currentData().toString();
    m_settings.setValue("color/workspace_profile", newProfile);
    
    // Save auto-conversion mode preference
    QString newMode = m_autoConversionCombo->currentData().toString();
    m_settings.setValue("color/auto_conversion_mode", newMode);
    
    m_settings.sync(); // Ensure all settings are written to disk
    
    if (oldLang != newLang) {
        QMessageBox::information(this, tr("Restart Required"), 
            tr("Please restart the application for the language changes to take effect."));
    }
    
    emit settingsChanged();
    accept();
}

void SettingsDialog::refreshAstapStatus() {
    QString currentUiPath = m_astapPath ? m_astapPath->text().trimmed() : "";
    QString dbPath = AstapSolver::getAstapDatabasePath(currentUiPath);

    if (!dbPath.isEmpty()) {
        m_lblAstapStatus->setText(tr("ASTAP Star Database: installed"));
    } else {
        m_lblAstapStatus->setText(tr("ASTAP Star Database: not installed"));
    }
}

void SettingsDialog::pickProjectRootPath() {
    QString startDir = m_projectRootPath->text().trimmed();
    if (startDir.isEmpty()) startDir = QDir::homePath();
    QString path = QFileDialog::getExistingDirectory(
        this, tr("Select Project Files Root Directory"), startDir);
    if (!path.isEmpty()) {
        m_projectRootPath->setText(path);
        m_settings.setValue("paths/project_root", path);
        m_settings.sync();
    }
}
