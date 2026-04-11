#include "SettingsDialog.h"

#include "network/ModelDownloader.h"
#include "network/AstapDownloader.h"
#include "astrometry/AstapSolver.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------

SettingsDialog::SettingsDialog(QWidget* parent)
    : DialogBase(parent, tr("Settings"), 650, 450)
{
    QVBoxLayout* mainLayout   = new QVBoxLayout(this);
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    QVBoxLayout* leftColumn   = new QVBoxLayout();
    QVBoxLayout* rightColumn  = new QVBoxLayout();

    // General Settings Group
    QGroupBox*   generalGroup = new QGroupBox(tr("General"), this);
    QFormLayout* generalForm  = new QFormLayout(generalGroup);

    m_langCombo = new QComboBox();
    m_langCombo->addItem(tr("System Default"), "System");
    m_langCombo->addItem("English",   "en");
    m_langCombo->addItem("Italiano",  "it");
    m_langCombo->addItem("Espanol",   "es");
    m_langCombo->addItem("Francais",  "fr");
    m_langCombo->addItem("Deutsch",   "de");
    generalForm->addRow(tr("Language:"), m_langCombo);

    m_workspaceProfileCombo = new QComboBox();
    m_workspaceProfileCombo->addItem(tr("sRGB IEC61966-2.1"), "sRGB");
    m_workspaceProfileCombo->addItem(tr("Adobe RGB (1998)"),  "AdobeRGB");
    m_workspaceProfileCombo->addItem(tr("ProPhoto RGB"),      "ProPhotoRGB");
    m_workspaceProfileCombo->addItem(tr("Linear RGB"),        "LinearRGB");
    generalForm->addRow(tr("Workspace Color Profile:"), m_workspaceProfileCombo);

    m_autoConversionCombo = new QComboBox();
    m_autoConversionCombo->addItem(tr("Ask"),    "Ask");
    m_autoConversionCombo->addItem(tr("Never"),  "Never");
    m_autoConversionCombo->addItem(tr("Always"), "Always");
    generalForm->addRow(tr("Auto-Convert Color Profiles:"), m_autoConversionCombo);

    leftColumn->addWidget(generalGroup);

    // Display Settings Group
    QGroupBox*   displayGroup = new QGroupBox(tr("Display"), this);
    QFormLayout* displayForm  = new QFormLayout(displayGroup);

    m_defaultStretchCombo = new QComboBox();
    m_defaultStretchCombo->addItem(tr("Linear"),      "Linear");
    m_defaultStretchCombo->addItem(tr("Auto Stretch"), "AutoStretch");
    m_defaultStretchCombo->addItem(tr("Histogram"),   "Histogram");
    m_defaultStretchCombo->addItem(tr("ArcSinh"),     "ArcSinh");
    m_defaultStretchCombo->addItem(tr("Square Root"), "Sqrt");
    m_defaultStretchCombo->addItem(tr("Logarithmic"), "Log");
    displayForm->addRow(tr("Default Display Stretch:"), m_defaultStretchCombo);

    m_24bitStfCheck = new QCheckBox(tr("24-bit Autostretch (Smoother gradients)"));
    displayForm->addRow("", m_24bitStfCheck);

    m_hideMagnifierCheck = new QCheckBox(tr("Hide Magnifier Viewer"));
    displayForm->addRow("", m_hideMagnifierCheck);

    leftColumn->addWidget(displayGroup);

    // Paths and External Integrations Group
    QGroupBox*   pathsGroup = new QGroupBox(tr("Paths and Integrations"), this);
    QFormLayout* pathsForm  = new QFormLayout(pathsGroup);
    pathsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // GraXpert executable path
    m_graxpertPath = new QLineEdit();
    QPushButton* btnGraX = new QPushButton(tr("Browse..."));
    connect(btnGraX, &QPushButton::clicked, this, &SettingsDialog::pickGraXpertPath);
    QHBoxLayout* rowGraX = new QHBoxLayout();
    rowGraX->addWidget(m_graxpertPath);
    rowGraX->addWidget(btnGraX);
    pathsForm->addRow(tr("GraXpert Executable:"), rowGraX);

    // StarNet++ executable path
    m_starnetPath = new QLineEdit();
    QPushButton* btnSN = new QPushButton(tr("Browse..."));
    connect(btnSN, &QPushButton::clicked, this, &SettingsDialog::pickStarNetPath);
    QHBoxLayout* rowSN = new QHBoxLayout();
    rowSN->addWidget(m_starnetPath);
    rowSN->addWidget(btnSN);
    pathsForm->addRow(tr("StarNet Executable:"), rowSN);

    // ASTAP star database folder path (the CLI binary is bundled with the application)
    m_astapPath = new QLineEdit();
    m_astapPath->setPlaceholderText(tr("Optional manual database folder (D50/D80/etc.)"));
    QPushButton* btnAstap = new QPushButton(tr("Browse..."));
    connect(btnAstap, &QPushButton::clicked, this, &SettingsDialog::pickAstapPath);
    QHBoxLayout* rowAstap = new QHBoxLayout();
    rowAstap->addWidget(m_astapPath);
    rowAstap->addWidget(btnAstap);
    pathsForm->addRow(tr("ASTAP Database Folder:"), rowAstap);
    connect(m_astapPath, &QLineEdit::textChanged, this, &SettingsDialog::refreshAstapStatus);

    // Additional command-line arguments passed to ASTAP
    m_astapExtraArgs = new QLineEdit();
    m_astapExtraArgs->setPlaceholderText("-s 500 -log");
    pathsForm->addRow(tr("ASTAP Extra Args:"), m_astapExtraArgs);

    // Root directory for project files
    m_projectRootPath = new QLineEdit();
    m_projectRootPath->setPlaceholderText(tr("Default: inside each project folder"));
    QPushButton* btnProjRoot = new QPushButton(tr("Browse..."));
    connect(btnProjRoot, &QPushButton::clicked, this, &SettingsDialog::pickProjectRootPath);
    QHBoxLayout* rowProjRoot = new QHBoxLayout();
    rowProjRoot->addWidget(m_projectRootPath);
    rowProjRoot->addWidget(btnProjRoot);
    pathsForm->addRow(tr("Project Files Root:"), rowProjRoot);

    leftColumn->addWidget(pathsGroup);
    leftColumn->addStretch();

    // Updates Group (right column)
    QGroupBox*   updatesGroup  = new QGroupBox(tr("Updates"), this);
    QVBoxLayout* updatesLayout = new QVBoxLayout(updatesGroup);
    m_checkUpdates = new QCheckBox(tr("Check for updates on startup"));
    updatesLayout->addWidget(m_checkUpdates);
    
    m_showWelcomeCheck = new QCheckBox(tr("Show welcome screen"));
    updatesLayout->addWidget(m_showWelcomeCheck);
    rightColumn->addWidget(updatesGroup);

    // Cosmic Clarity ML Models Group
    QGroupBox*   modelsGroup  = new QGroupBox(tr("Cosmic Clarity Models"), this);
    QVBoxLayout* modelsLayout = new QVBoxLayout(modelsGroup);
    m_lblModelsStatus = new QLabel();
    modelsLayout->addWidget(m_lblModelsStatus);
    m_btnDownloadModels = new QPushButton(tr("Download Latest Cosmic Clarity Models"));
    connect(m_btnDownloadModels, &QPushButton::clicked, this, &SettingsDialog::downloadModels);
    modelsLayout->addWidget(m_btnDownloadModels);
    rightColumn->addWidget(modelsGroup);

    // ASTAP Star Database Download Group
    QGroupBox*   astapGroup  = new QGroupBox(tr("ASTAP Star Database"), this);
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

    // Dialog confirmation buttons
    mainLayout->addStretch();
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"));
    QPushButton* okBtn     = new QPushButton(tr("OK"));
    okBtn->setDefault(true);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    mainLayout->addLayout(btnLayout);

    // Load all persisted settings into the UI controls
    m_graxpertPath->setText(m_settings.value("paths/graxpert").toString());
    m_starnetPath->setText(m_settings.value("paths/starnet").toString());

    // Migrate from the legacy "paths/astap" executable key to the new database folder key
    QString astapDbPath = m_settings.value("paths/astap_database").toString();
    if (astapDbPath.isEmpty())
    {
        const QString legacyPath = m_settings.value("paths/astap").toString();
        const QFileInfo legacyInfo(legacyPath);
        if (legacyInfo.exists() && legacyInfo.isDir())
            astapDbPath = legacyInfo.absoluteFilePath();
    }
    m_astapPath->setText(astapDbPath);
    m_astapExtraArgs->setText(m_settings.value("paths/astap_extra").toString());
    m_projectRootPath->setText(m_settings.value("paths/project_root").toString());

    // Restore combo box selections
    auto restoreCombo = [](QComboBox* combo, const QString& key, const QString& defaultVal, QSettings& settings) {
        const QString saved = settings.value(key, defaultVal).toString();
        const int idx = combo->findData(saved);
        if (idx != -1) combo->setCurrentIndex(idx);
    };

    restoreCombo(m_langCombo,               "general/language",             "System",  m_settings);
    restoreCombo(m_workspaceProfileCombo,   "color/workspace_profile",      "sRGB",    m_settings);
    restoreCombo(m_autoConversionCombo,     "color/auto_conversion_mode",   "Ask",     m_settings);
    restoreCombo(m_defaultStretchCombo,     "display/default_stretch",      "Linear",  m_settings);

    m_checkUpdates->setChecked(m_settings.value("general/check_updates", true).toBool());
    m_showWelcomeCheck->setChecked(m_settings.value("general/show_welcome", true).toBool());
    m_24bitStfCheck->setChecked(m_settings.value("display/24bit_stf",    true).toBool());
    m_hideMagnifierCheck->setChecked(m_settings.value("display/hide_magnifier", false).toBool());
}

// ----------------------------------------------------------------------------
// Public Slots - Path Pickers
// ----------------------------------------------------------------------------

void SettingsDialog::pickStarNetPath()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select StarNet++ Executable"), "",
        tr("Executables (*.exe);;All Files (*)"));

    if (!path.isEmpty())
    {
        m_starnetPath->setText(path);
        m_settings.setValue("paths/starnet", path);
        m_settings.sync();
    }
}

void SettingsDialog::pickAstapPath()
{
    QString startDir = m_astapPath->text().trimmed();
    if (startDir.isEmpty())
        startDir = QDir::homePath();

    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Select ASTAP Database Folder"), startDir);

    if (!path.isEmpty())
    {
        m_astapPath->setText(path);
        m_settings.setValue("paths/astap_database", path);
        m_settings.sync();
    }
}

void SettingsDialog::pickGraXpertPath()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select GraXpert Executable"), "",
        tr("Executables (*.exe);;All Files (*)"));

    if (!path.isEmpty())
    {
        m_graxpertPath->setText(path);
        m_settings.setValue("paths/graxpert", path);
        m_settings.sync();
    }
}

void SettingsDialog::pickProjectRootPath()
{
    QString startDir = m_projectRootPath->text().trimmed();
    if (startDir.isEmpty())
        startDir = QDir::homePath();

    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Select Project Files Root Directory"), startDir);

    if (!path.isEmpty())
    {
        m_projectRootPath->setText(path);
        m_settings.setValue("paths/project_root", path);
        m_settings.sync();
    }
}

// ----------------------------------------------------------------------------
// Public Slots - Downloads
// ----------------------------------------------------------------------------

void SettingsDialog::downloadModels()
{
    m_btnDownloadModels->setEnabled(false);
    m_lblModelsStatus->setText(tr("Preparing..."));

    auto* downloader   = new ModelDownloader(this);
    auto* progressDlg  = new QProgressDialog(
        tr("Downloading Cosmic Clarity models..."), tr("Cancel"), 0, 100, this);
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
        if (val < 0)
            progressDlg->setRange(0, 0); // Indeterminate mode
        else
        {
            progressDlg->setRange(0, 100);
            progressDlg->setValue(val);
        }
    });

    connect(downloader, &ModelDownloader::finished, this,
            [this, downloader, progressDlg](bool ok, const QString& msg)
    {
        progressDlg->close();
        progressDlg->deleteLater();

        if (ok)
            QMessageBox::information(this, tr("Cosmic Clarity Models"),
                                     tr("Models downloaded and installed successfully."));
        else
            QMessageBox::warning(this, tr("Cosmic Clarity Models"),
                                 tr("Failed to download models: %1").arg(msg));

        refreshModelsStatus();
        m_btnDownloadModels->setEnabled(true);
        downloader->deleteLater();
    });

    downloader->startDownload();
}

void SettingsDialog::downloadAstapCatalog()
{
    m_btnDownloadAstap->setEnabled(false);

    auto* downloader  = new AstapDownloader(this);
    auto* progressDlg = new QProgressDialog(
        tr("Downloading ASTAP D50 Catalog..."), tr("Cancel"), 0, 100, this);
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
        if (val < 0)
            progressDlg->setRange(0, 0);
        else
        {
            progressDlg->setRange(0, 100);
            progressDlg->setValue(val);
        }
    });

    connect(downloader, &AstapDownloader::finished, this,
            [this, downloader, progressDlg](bool ok, const QString& msg)
    {
        progressDlg->close();
        progressDlg->deleteLater();

        if (ok)
            QMessageBox::information(this, tr("ASTAP Database Download"), msg);
        else
            QMessageBox::warning(this, tr("ASTAP Database Download"), msg);

        refreshAstapStatus();
        m_btnDownloadAstap->setEnabled(true);
        downloader->deleteLater();
    });

    downloader->startDownload();
}

// ----------------------------------------------------------------------------
// Public Slots - Save
// ----------------------------------------------------------------------------

void SettingsDialog::saveSettings()
{
    m_settings.setValue("paths/graxpert",      m_graxpertPath->text());
    m_settings.setValue("paths/starnet",       m_starnetPath->text());
    m_settings.setValue("paths/astap_database", m_astapPath->text());
    m_settings.setValue("paths/astap_extra",   m_astapExtraArgs->text());
    m_settings.setValue("paths/project_root",  m_projectRootPath->text());

    // Remove the legacy ASTAP executable key; the bundled CLI is used instead
    m_settings.remove("paths/astap");

    const QString oldLang = m_settings.value("general/language", "System").toString();
    const QString newLang = m_langCombo->currentData().toString();
    m_settings.setValue("general/language",           newLang);
    m_settings.setValue("general/check_updates",      m_checkUpdates->isChecked());
    m_settings.setValue("general/show_welcome",       m_showWelcomeCheck->isChecked());
    m_settings.setValue("display/24bit_stf",          m_24bitStfCheck->isChecked());
    m_settings.setValue("display/hide_magnifier",     m_hideMagnifierCheck->isChecked());
    m_settings.setValue("display/default_stretch",    m_defaultStretchCombo->currentData().toString());
    m_settings.setValue("color/workspace_profile",    m_workspaceProfileCombo->currentData().toString());
    m_settings.setValue("color/auto_conversion_mode", m_autoConversionCombo->currentData().toString());

    m_settings.sync(); // Flush all changes to disk immediately

    if (oldLang != newLang)
    {
        QMessageBox::information(this, tr("Restart Required"),
            tr("Please restart the application for the language changes to take effect."));
    }

    emit settingsChanged();
    accept();
}

// ----------------------------------------------------------------------------
// Private Methods - Status Refresh
// ----------------------------------------------------------------------------

void SettingsDialog::refreshModelsStatus()
{
    if (ModelDownloader::modelsInstalled())
        m_lblModelsStatus->setText(tr("Cosmic Clarity models: installed"));
    else
        m_lblModelsStatus->setText(tr("Cosmic Clarity models: not installed"));
}

void SettingsDialog::refreshAstapStatus()
{
    const QString currentUiPath = m_astapPath ? m_astapPath->text().trimmed() : "";
    const QString dbPath = AstapSolver::getAstapDatabasePath(currentUiPath);

    if (!dbPath.isEmpty())
        m_lblAstapStatus->setText(tr("ASTAP Star Database: installed"));
    else
        m_lblAstapStatus->setText(tr("ASTAP Star Database: not installed"));
}