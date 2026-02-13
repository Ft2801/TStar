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
    // void pickCosmicClarityEnginePath(); // Removed
    void downloadModels();
    void saveSettings();

signals:
    void settingsChanged();

private:
    void refreshModelsStatus();

    QSettings m_settings;
    QLineEdit* m_graxpertPath;
    QLineEdit* m_starnetPath;
    // QLineEdit* m_cosmicClarityEnginePath; // Removed
    QComboBox* m_langCombo;
    class QCheckBox* m_checkUpdates;
    class QCheckBox* m_24bitStfCheck;
    QPushButton* m_btnDownloadModels;
    QLabel* m_lblModelsStatus;
};

#endif // SETTINGSDIALOG_H
