#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSettings>
#include <QLabel>
#include <QFileDialog>
#include <QComboBox>
#include <QPushButton>

#include "DialogBase.h"

class SettingsDialog : public DialogBase {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

public slots:
    void pickGraXpertPath();
    void pickStarNetPath();
    void pickAstapPath();
    void downloadModels();
    void downloadAstapCatalog();
    void saveSettings();
    void pickProjectRootPath();

signals:
    void settingsChanged();

private:
    void refreshModelsStatus();
    void refreshAstapStatus();

    QSettings m_settings;
    QLineEdit* m_graxpertPath;
    QLineEdit* m_starnetPath;
    QLineEdit* m_astapPath;
    QComboBox* m_langCombo;
    QComboBox* m_workspaceProfileCombo;
    QComboBox* m_autoConversionCombo;
    QComboBox* m_defaultStretchCombo;
    class QCheckBox* m_checkUpdates;
    class QCheckBox* m_24bitStfCheck;
    class QCheckBox* m_hideMagnifierCheck;
    QLineEdit* m_astapExtraArgs;
    QPushButton* m_btnDownloadModels;
    QLabel* m_lblModelsStatus;
    QPushButton* m_btnDownloadAstap;
    QLabel* m_lblAstapStatus;
    QLineEdit* m_projectRootPath;
};

#endif // SETTINGSDIALOG_H