#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSettings>
#include <QLabel>
#include <QFileDialog>
#include <QComboBox>
#include "MainWindow.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

public slots:
    void pickGraXpertPath();
    void pickCosmicClarityPath();
    void pickStarNetPath();
    void saveSettings();

signals:
    void settingsChanged();

private:
    QSettings m_settings;
    QLineEdit* m_graxpertPath;
    QLineEdit* m_cosmicClarityPath;
    QLineEdit* m_starnetPath;
    QComboBox* m_langCombo;
    class QCheckBox* m_checkUpdates;
    class QCheckBox* m_24bitStfCheck;
};

#endif // SETTINGSDIALOG_H


