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

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Preferences"));
    resize(800, 400);

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
    
    // Cosmic Clarity
    m_cosmicClarityPath = new QLineEdit();
    QPushButton* btnCC = new QPushButton(tr("Browse..."));
    connect(btnCC, &QPushButton::clicked, this, &SettingsDialog::pickCosmicClarityPath);
    
    QHBoxLayout* rowCC = new QHBoxLayout();
    rowCC->addWidget(m_cosmicClarityPath);
    rowCC->addWidget(btnCC);
    form->addRow(tr("Cosmic Clarity Folder:"), rowCC);

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
    
    // --- Buttons ---
    mainLayout->addStretch();
    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::saveSettings);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btns);
    
    // --- Load Settings ---
    m_graxpertPath->setText(m_settings.value("paths/graxpert").toString());
    m_cosmicClarityPath->setText(m_settings.value("paths/cosmic_clarity").toString());
    m_starnetPath->setText(m_settings.value("paths/starnet").toString());

    QString savedLang = m_settings.value("general/language", "System").toString();
    int idx = m_langCombo->findData(savedLang);
    if (idx != -1) m_langCombo->setCurrentIndex(idx);

    m_checkUpdates->setChecked(m_settings.value("general/check_updates", true).toBool());
}

void SettingsDialog::pickStarNetPath() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select StarNet++ Executable"), "", tr("Executables (*.exe);;All Files (*)"));
    if (!path.isEmpty()) m_starnetPath->setText(path);
}

void SettingsDialog::pickGraXpertPath() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select GraXpert Executable"), "", tr("Executables (*.exe);;All Files (*)"));
    if (!path.isEmpty()) m_graxpertPath->setText(path);
}

void SettingsDialog::pickCosmicClarityPath() {
    QString path = QFileDialog::getExistingDirectory(this, tr("Select Cosmic Clarity Folder"));
    if (!path.isEmpty()) m_cosmicClarityPath->setText(path);
}

void SettingsDialog::saveSettings() {
    m_settings.setValue("paths/graxpert", m_graxpertPath->text());
    m_settings.setValue("paths/cosmic_clarity", m_cosmicClarityPath->text());
    m_settings.setValue("paths/starnet", m_starnetPath->text());
    
    QString oldLang = m_settings.value("general/language", "System").toString();
    QString newLang = m_langCombo->currentData().toString();
    m_settings.setValue("general/language", newLang);
    m_settings.setValue("general/check_updates", m_checkUpdates->isChecked());
    
    if (oldLang != newLang) {
        QMessageBox::information(this, tr("Restart Required"), 
            tr("Please restart the application for the language changes to take effect."));
    }
    
    emit settingsChanged();
    accept();
}

