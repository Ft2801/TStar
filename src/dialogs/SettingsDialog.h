#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "DialogBase.h"

#include <QLineEdit>
#include <QSettings>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>

/**
 * @brief Application settings dialog.
 *
 * Provides configuration for:
 *   - Interface language and display preferences.
 *   - Workspace color profile and auto-conversion behavior.
 *   - External tool paths (GraXpert, StarNet, ASTAP database).
 *   - Project file root directory.
 *   - Download controls for Cosmic Clarity ML models and the ASTAP star catalog.
 */
class SettingsDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

public slots:
    void pickGraXpertPath();
    void pickStarNetPath();
    void pickAstapPath();
    void pickProjectRootPath();
    void downloadModels();
    void downloadAstapCatalog();
    void saveSettings();

signals:
    /**
     * @brief Emitted after settings are saved, allowing the main window
     *        to apply any changes that affect the running application.
     */
    void settingsChanged();

private:
    /**
     * @brief Refreshes the Cosmic Clarity model installation status label.
     */
    void refreshModelsStatus();

    /**
     * @brief Refreshes the ASTAP star database installation status label.
     */
    void refreshAstapStatus();

    QSettings m_settings;

    // Path configuration fields
    QLineEdit* m_graxpertPath;
    QLineEdit* m_starnetPath;
    QLineEdit* m_astapPath;
    QLineEdit* m_astapExtraArgs;
    QLineEdit* m_projectRootPath;

    // General and color settings
    QComboBox* m_langCombo;
    QComboBox* m_workspaceProfileCombo;
    QComboBox* m_autoConversionCombo;
    QComboBox* m_defaultStretchCombo;

    // Display options
    QCheckBox* m_checkUpdates;
    QCheckBox* m_showWelcomeCheck;
    QCheckBox* m_24bitStfCheck;
    QCheckBox* m_hideMagnifierCheck;

    // Cosmic Clarity models section
    QPushButton* m_btnDownloadModels;
    QLabel*      m_lblModelsStatus;

    // ASTAP database section
    QPushButton* m_btnDownloadAstap;
    QLabel*      m_lblAstapStatus;
};

#endif // SETTINGSDIALOG_H