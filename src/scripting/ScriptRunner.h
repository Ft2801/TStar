
#ifndef SCRIPT_RUNNER_H
#define SCRIPT_RUNNER_H

#include "ScriptTypes.h"
#include "ScriptParser.h"
#include <QObject>
#include <QMap>
#include <atomic>

namespace Scripting {

class ScriptRunner : public QObject {
    Q_OBJECT
    
public:
    explicit ScriptRunner(QObject* parent = nullptr);
    ~ScriptRunner() override;
    
    //=========================================================================
    // COMMAND REGISTRATION
    //=========================================================================
    
    /**
     * @brief Register a command
     */
    void registerCommand(const CommandDef& def);
    
    /**
     * @brief Register multiple commands
     */
    void registerCommands(const QVector<CommandDef>& defs);
    
    /**
     * @brief Check if command is registered
     */
    bool hasCommand(const QString& name) const;
    
    /**
     * @brief Get command definition
     */
    const CommandDef* getCommand(const QString& name) const;
    
    /**
     * @brief Get all registered commands
     */
    QStringList registeredCommands() const;
    
    //=========================================================================
    // SCRIPT EXECUTION
    //=========================================================================
    
    /**
     * @brief Execute a script file
     * @param path Path to script file
     * @return Execution result
     */
    ScriptResult executeFile(const QString& path);
    
    /**
     * @brief Execute a script string
     * @param content Script content
     * @return Execution result
     */
    ScriptResult executeString(const QString& content);
    
    /**
     * @brief Execute pre-parsed commands
     * @param commands Commands to execute
     * @return Execution result
     */
    ScriptResult executeCommands(const QVector<ScriptCommand>& commands);
    
    /**
     * @brief Execute a single command
     * @param cmd Command to execute
     * @return true on success
     */
    bool executeCommand(const ScriptCommand& cmd);
    
    //=========================================================================
    // VARIABLES
    //=========================================================================
    
    /**
     * @brief Set a variable
     */
    void setVariable(const QString& name, const QString& value);
    
    /**
     * @brief Get a variable
     */
    QString variable(const QString& name) const;
    
    /**
     * @brief Get all variables
     */
    const QMap<QString, QString>& variables() const { return m_variables; }
    
    //=========================================================================
    // WORKING DIRECTORY
    //=========================================================================
    
    /**
     * @brief Set working directory for the script
     */
    void setWorkingDirectory(const QString& path);
    
    /**
     * @brief Get current working directory
     */
    QString workingDirectory() const { return m_workingDir; }
    
    //=========================================================================
    // CANCELLATION
    //=========================================================================
    
    void requestCancel();  // Declaration only, implementation in .cpp
    bool isCancelled() const { return m_cancelled.load(); }
    void resetCancel();    // Declaration only, implementation in .cpp
    
    //=========================================================================
    // ERROR HANDLING
    //=========================================================================
    
    /**
     * @brief Set error information
     */
    void setError(const QString& message, int lineNumber);
    
    /**
     * @brief Get last error message
     */
    QString lastError() const { return m_lastError; }
    
    /**
     * @brief Get last error line number
     */
    int lastErrorLine() const { return m_lastErrorLine; }
    
    /**
     * @brief Direct logging function for critical paths (bypasses signal overhead)
     * This is used in tight loops to avoid Qt event processing overhead
     */
    void logMessageDirect(const QString& message, const QString& color);

signals:
    /**
     * @brief Emitted before executing a command
     */
    void commandStarted(const QString& name, int lineNumber);
    
    /**
     * @brief Emitted after executing a command
     */
    void commandFinished(const QString& name, bool success);
    
    /**
     * @brief Progress update
     */
    void progressChanged(const QString& message, double progress);
    
    /**
     * @brief Log message
     */
    void logMessage(const QString& message, const QString& color);
    
    /**
     * @brief Script execution finished
     */
    void finished(bool success);
    
    /**
     * @brief Signal for cancellation requested
     */
    void cancelRequested();


private:
    /**
     * @brief Validate command arguments
     */
    bool validateCommand(const ScriptCommand& cmd);
    
    QMap<QString, CommandDef> m_commands;
    QMap<QString, QString> m_variables;
    QString m_workingDir;
    QString m_lastError;
    int m_lastErrorLine = 0;
    std::atomic<bool> m_cancelled{false};
};

} // namespace Scripting

#endif // SCRIPT_RUNNER_H
