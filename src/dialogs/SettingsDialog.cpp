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
#include "network/ModelDownloader.h"

SettingsDialog::SettingsDialog(QWidget* parent) : DialogBase(parent, tr("Settings"), 500, 400) {

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
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
    
    m_checkUpdates = new QCheckBox(tr("Check for updates on startup"));
    generalForm->addRow("", m_checkUpdates);

    mainLayout->addWidget(generalGroup);
    
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

    
    // --- Layout Assembly ---
    mainLayout->addWidget(pathsGroup);

    // --- Cosmic Clarity Models Group ---
    QGroupBox* modelsGroup = new QGroupBox(tr("Cosmic Clarity models"), this);
    QVBoxLayout* modelsLayout = new QVBoxLayout(modelsGroup);

    m_lblModelsStatus = new QLabel();
    modelsLayout->addWidget(m_lblModelsStatus);

    m_btnDownloadModels = new QPushButton(tr("Download latest Cosmic Clarity models"));
    connect(m_btnDownloadModels, &QPushButton::clicked, this, &SettingsDialog::downloadModels);
    modelsLayout->addWidget(m_btnDownloadModels);

    mainLayout->addWidget(modelsGroup);

    refreshModelsStatus();
    
    // --- Buttons ---
    mainLayout->addStretch();
    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::saveSettings);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btns);
    
    // --- Load Settings ---
    m_graxpertPath->setText(m_settings.value("paths/graxpert").toString());
    m_starnetPath->setText(m_settings.value("paths/starnet").toString());
    
    QString savedLang = m_settings.value("general/language", "System").toString();
    int idx = m_langCombo->findData(savedLang);
    if (idx != -1) m_langCombo->setCurrentIndex(idx);

    m_checkUpdates->setChecked(m_settings.value("general/check_updates", true).toBool());

    // --- Display Group ---
    QGroupBox* displayGroup = new QGroupBox(tr("Display"), this);
    QFormLayout* displayForm = new QFormLayout(displayGroup);

    m_24bitStfCheck = new QCheckBox(tr("24-bit Autostretch (Smoother gradients)"));
    // Default to true as requested
    m_24bitStfCheck->setChecked(m_settings.value("display/24bit_stf", true).toBool());
    
    displayForm->addRow("", m_24bitStfCheck);

    // Insert Display group after General group (index 1)
    mainLayout->insertWidget(1, displayGroup);

}

void SettingsDialog::pickStarNetPath() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select StarNet++ Executable"), "", tr("Executables (*.exe);;All Files (*)"));
    if (!path.isEmpty()) {
        m_starnetPath->setText(path);
        m_settings.setValue("paths/starnet", path);
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

void SettingsDialog::refreshModelsStatus() {
    if (ModelDownloader::modelsInstalled()) {
        m_lblModelsStatus->setText(tr("Cosmic Clarity models: installed") + " ✅");
    } else {
        m_lblModelsStatus->setText(tr("Cosmic Clarity models: not installed") + " ❌");
    }
}

void SettingsDialog::saveSettings() {
    m_settings.setValue("paths/graxpert", m_graxpertPath->text());
    m_settings.setValue("paths/starnet", m_starnetPath->text());
    //m_settings.setValue("paths/cosmic_clarity_engine", m_cosmicClarityEnginePath->text());

    QString oldLang = m_settings.value("general/language", "System").toString();
    QString newLang = m_langCombo->currentData().toString();
    m_settings.setValue("general/language", newLang);
    m_settings.setValue("general/check_updates", m_checkUpdates->isChecked());
    m_settings.setValue("display/24bit_stf", m_24bitStfCheck->isChecked());
    m_settings.sync(); // Ensure all settings are written to disk
    
    if (oldLang != newLang) {
        QMessageBox::information(this, tr("Restart Required"), 
            tr("Please restart the application for the language changes to take effect."));
    }
    
    emit settingsChanged();
    accept();
}
