/**
 * @file ScriptDialog.h
 * @brief Dialog for running TStar scripts
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef SCRIPT_DIALOG_H
#define SCRIPT_DIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QTableWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QComboBox>
#include <memory>

#include "../scripting/ScriptRunner.h"
#include "../scripting/StackingCommands.h"

class MainWindow;

/**
 * @brief Script execution dialog
 * 
 * Provides interface for:
 * - Loading/editing scripts
 * - Managing variables
 * - Executing scripts
 * - Viewing output
 */
class ScriptDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit ScriptDialog(MainWindow* parent = nullptr);
    ~ScriptDialog() override;
    
    /**
     * @brief Load a script file
     */
    bool loadScript(const QString& path);
    
private slots:
    void onLoadScript();
    void onSaveScript();
    void onLoadPredefined();
    
    void onAddVariable();
    void onRemoveVariable();
    
    void onRunScript();
    void onStopScript();
    
    void onCommandStarted(const QString& name, int line);
    void onCommandFinished(const QString& name, bool success);
    void onLogMessage(const QString& message, const QString& color);
    void onProgressChanged(const QString& message, double progress);
    void onFinished(bool success);
    
private:
    void setupUI();
    void populatePredefinedScripts();
    void highlightLine(int lineNumber, bool error);
    
    // UI
    QComboBox* m_predefinedCombo;
    QTextEdit* m_scriptEditor;
    QTableWidget* m_variablesTable;
    QProgressBar* m_progressBar;
    QTextEdit* m_outputLog;
    QPushButton* m_runBtn;
    QPushButton* m_stopBtn;
    
    // Data
    Scripting::ScriptRunner m_runner;
    QString m_currentFile;
    MainWindow* m_mainWindow;
    bool m_isRunning = false;
};

#endif // SCRIPT_DIALOG_H
