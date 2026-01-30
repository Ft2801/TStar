/**
 * @file RegistrationDialog.h
 * @brief Dialog for image registration/alignment
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef REGISTRATION_DIALOG_H
#define REGISTRATION_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <memory>

#include "../stacking/Registration.h"
#include "../stacking/StackingSequence.h"

class MainWindow;

/**
 * @brief Image registration dialog
 */
class RegistrationDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit RegistrationDialog(MainWindow* parent = nullptr);
    ~RegistrationDialog() override;
    
    void setSequence(std::unique_ptr<Stacking::ImageSequence> sequence);
    Stacking::ImageSequence* sequence() { return m_sequence.get(); }
    
private slots:
    void onLoadSequence();
    void onSetReference();
    void onAutoFindReference();
    
    void onStartRegistration();
    void onCancel();
    
    void onProgressChanged(const QString& message, double progress);
    void onLogMessage(const QString& message, const QString& color);
    void onImageRegistered(int index, bool success);
    void onFinished(int successCount);
    
private:
    void setupUI();
    void updateTable();
    void updateReferenceLabel();
    Stacking::RegistrationParams gatherParams() const;
    
    // UI
    QGroupBox* m_sequenceGroup;
    QTableWidget* m_imageTable;
    QPushButton* m_loadBtn;
    QPushButton* m_setRefBtn;
    QPushButton* m_autoRefBtn;
    QLabel* m_referenceLbl;
    
    QGroupBox* m_paramsGroup;
    QLineEdit* m_outputDir; // New output directory field
    QDoubleSpinBox* m_detectionSigma;
    QSpinBox* m_minStars;
    QSpinBox* m_maxStars;
    QDoubleSpinBox* m_matchTolerance;
    QCheckBox* m_allowRotation;
    QCheckBox* m_highPrecision;
    
    QGroupBox* m_progressGroup;
    QProgressBar* m_progressBar;
    QTextEdit* m_logText;
    QPushButton* m_startBtn;
    QPushButton* m_cancelBtn;
    
    QLabel* m_summaryLabel;
    
    // Data
    std::unique_ptr<Stacking::ImageSequence> m_sequence;
    std::unique_ptr<Stacking::RegistrationWorker> m_worker;
    MainWindow* m_mainWindow;
    bool m_isRunning = false;
};

#endif // REGISTRATION_DIALOG_H
