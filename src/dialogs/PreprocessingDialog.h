/**
 * @file PreprocessingDialog.h
 * @brief Dialog for image preprocessing and calibration
 * 
 * Provides UI for configuring and executing image calibration
 * with bias, dark, and flat frames.
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef PREPROCESSING_DIALOG_H
#define PREPROCESSING_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QListWidget>
#include <memory>

#include "../preprocessing/PreprocessingTypes.h"
#include "../preprocessing/Preprocessing.h"

class MainWindow;

/**
 * @brief Image preprocessing/calibration dialog
 * 
 * Provides interface for:
 * - Selecting master calibration frames
 * - Configuring calibration options
 * - Selecting files to calibrate
 * - Executing batch preprocessing
 */
class PreprocessingDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit PreprocessingDialog(MainWindow* parent = nullptr);
    ~PreprocessingDialog() override;
    
private slots:
    // File selection
    void onSelectBias();
    void onSelectDark();
    void onSelectFlat();
    void onSelectDarkFlat();
    void onCreateMasterBias();
    void onCreateMasterDark();
    void onCreateMasterFlat();
    
    void onAddLights();
    void onRemoveLights();
    void onClearLights();
    
    // Actions
    void onStartCalibration();
    void onCancel();
    
    // Progress
    void onProgressChanged(const QString& message, double progress);
    void onLogMessage(const QString& message, const QString& color);
    void onImageProcessed(const QString& path, bool success);
    void onFinished(bool success);
    
private:
    void setupUI();
    void setupMastersGroup();
    void setupLightsGroup();
    void setupOptionsGroup();
    void setupProgressGroup();
    
    Preprocessing::PreprocessParams gatherParams() const;
    
    // UI - Masters
    QGroupBox* m_mastersGroup;
    QLineEdit* m_biasPath;
    QLineEdit* m_darkPath;
    QLineEdit* m_flatPath;
    QLineEdit* m_darkFlatPath;
    QPushButton* m_selectBiasBtn;
    QPushButton* m_selectDarkBtn;
    QPushButton* m_selectFlatBtn;
    QPushButton* m_selectDarkFlatBtn;
    QPushButton* m_createBiasBtn;
    QPushButton* m_createDarkBtn;
    QPushButton* m_createFlatBtn;
    
    // UI - Lights
    QGroupBox* m_lightsGroup;
    QListWidget* m_lightsList;
    QPushButton* m_addLightsBtn;
    QPushButton* m_removeLightsBtn;
    QPushButton* m_clearLightsBtn;
    
    // UI - Options
    QGroupBox* m_optionsGroup;
    QCheckBox* m_useBiasCheck;
    QCheckBox* m_useDarkCheck;
    QCheckBox* m_useFlatCheck;
    QCheckBox* m_darkOptimCheck;
    QCheckBox* m_fixBandingCheck;
    QCheckBox* m_fixBadLinesCheck;
    QCheckBox* m_fixXTransCheck;
    
    QCheckBox* m_debayerCheck;
    QComboBox* m_bayerPatternCombo;
    QComboBox* m_debayerAlgoCombo;
    QCheckBox* m_cfaEqualizeCheck;
    
    QCheckBox* m_cosmeticCheck;
    QComboBox* m_cosmeticModeCombo;
    QDoubleSpinBox* m_hotSigmaSpin;
    QDoubleSpinBox* m_coldSigmaSpin;
    
    QDoubleSpinBox* m_biasLevelSpin;
    QDoubleSpinBox* m_pedestalSpin;
    
    QLineEdit* m_outputPrefix;
    QLineEdit* m_outputDir;
    
    // UI - Progress
    QGroupBox* m_progressGroup;
    QProgressBar* m_progressBar;
    QTextEdit* m_logText;
    QPushButton* m_startBtn;
    QPushButton* m_cancelBtn;
    
    // Data
    QStringList m_lightFiles;
    std::unique_ptr<Preprocessing::PreprocessingWorker> m_worker;
    MainWindow* m_mainWindow;
    bool m_isRunning = false;
};

#endif // PREPROCESSING_DIALOG_H
