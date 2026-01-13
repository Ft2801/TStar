
#include "ScriptRunner.h"
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThreadPool>
#include <QElapsedTimer>
#include "../core/ThreadState.h"
#include <cstdlib>
#include <functional>

namespace Scripting {

//=========================
// MEMORY TRACKING
//=========================

#ifdef _WIN32
#include <windows.h>
static long long getAvailableMemory() {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    return statex.ullAvailPhys / (1024 * 1024); // Return in MB
}
#else
static long long getAvailableMemory() {
    // Fallback for non-Windows
    return 0;
}
#endif

//=============================================================================
// CONSTRUCTOR / DESTRUCTOR
//=============================================================================

ScriptRunner::ScriptRunner(QObject* parent)
    : QObject(parent)
{
    m_workingDir = QDir::currentPath();
}

ScriptRunner::~ScriptRunner() = default;

//=============================================================================
// COMMAND REGISTRATION
//=============================================================================

void ScriptRunner::registerCommand(const CommandDef& def) {
    m_commands[def.name.toLower()] = def;
}

void ScriptRunner::registerCommands(const QVector<CommandDef>& defs) {
    for (const auto& def : defs) {
        registerCommand(def);
    }
}

bool ScriptRunner::hasCommand(const QString& name) const {
    return m_commands.contains(name.toLower());
}

const CommandDef* ScriptRunner::getCommand(const QString& name) const {
    auto it = m_commands.find(name.toLower());
    if (it != m_commands.end()) {
        return &it.value();
    }
    return nullptr;
}

QStringList ScriptRunner::registeredCommands() const {
    return m_commands.keys();
}

//=============================================================================
// SCRIPT EXECUTION
//=============================================================================

ScriptResult ScriptRunner::executeFile(const QString& path) {
    m_cancelled = false;
    // Ensure thread state is reset so we can run
    Threading::setThreadRun(true);
    
    QFileInfo fi(path);
    if (!fi.exists()) {
        setError(tr("Script file not found: %1").arg(path), 0);
        return ScriptResult::FileError;
    }
    
    // Do NOT change working directory to script location.
    // Scripts should run in the user-defined Home Directory (QDir::currentPath()).
    // However, if the runner hasn't been initialized with a WD, sync it.
    if (m_workingDir.isEmpty()) {
        setWorkingDirectory(QDir::currentPath());
    }
    
    ScriptParser parser;
    
    // Set current variables
    for (auto it = m_variables.begin(); it != m_variables.end(); ++it) {
        parser.setVariable(it.key(), it.value());
    }
    
    if (!parser.parseFile(path)) {
        QStringList errors = parser.errors();
        setError(errors.isEmpty() ? tr("Parse error") : errors.first(), 0);
        return ScriptResult::SyntaxError;
    }
    
    return executeCommands(parser.commands());
}

ScriptResult ScriptRunner::executeString(const QString& content) {
    qDebug() << "ScriptRunner::executeString called. Content length:" << content.length();
    m_cancelled = false;
    // Ensure thread state is reset so we can run
    Threading::setThreadRun(true);
    
    ScriptParser parser;
    
    // Set current variables
    for (auto it = m_variables.begin(); it != m_variables.end(); ++it) {
        parser.setVariable(it.key(), it.value());
    }
    
    if (!parser.parseString(content)) {
        QStringList errors = parser.errors();
        qDebug() << "ScriptRunner: Parse failed!" << (errors.isEmpty() ? "Unknown error" : errors.first());
        setError(errors.isEmpty() ? tr("Parse error") : errors.first(), 0);
        return ScriptResult::SyntaxError;
    }
    
    auto commands = parser.commands();
    qDebug() << "ScriptRunner: Successfully parsed" << commands.size() << "commands";
    return executeCommands(commands);
}

ScriptResult ScriptRunner::executeCommands(const QVector<ScriptCommand>& commands) {
    int total = commands.size();
    int executed = 0;
    
    // Memory tracking
    long long startMem = getAvailableMemory();
    long long lastMem = startMem;
    
    QElapsedTimer scriptTimer;
    scriptTimer.start();
    
    for (const ScriptCommand& cmd : commands) {
        if (m_cancelled) {
            logMessageDirect(tr("Script cancelled"), "salmon");
            emit finished(false);
            return ScriptResult::Cancelled;
        }
        
        // Command timing
        QElapsedTimer commandTimer;
        commandTimer.start();
        
        // Progress update
        emit progressChanged(tr("Executing: %1").arg(cmd.name),
                            static_cast<double>(executed) / total);
        
        if (!executeCommand(cmd)) {
            emit finished(false);
            return ScriptResult::CommandError;
        }
        
        executed++;
        int elapsedMs = commandTimer.elapsed();
        
        long long currentMem = getAvailableMemory();
        if (startMem > 0) {
            long long memDiff = lastMem - currentMem;
            if (memDiff != 0) {
                logMessageDirect(tr("End of command %1, memory difference: %2 MB")
                    .arg(cmd.name).arg(memDiff), "neutral");
            }
        }
        lastMem = currentMem;
        
        double elapsedSec = elapsedMs / 1000.0;
        if (elapsedSec > 0.01) {  // Only log if > 10ms
            logMessageDirect(tr("Execution time: %1 s").arg(elapsedSec, 0, 'f', 2), "neutral");
        }
    }
    
    emit progressChanged(tr("Script complete"), 1.0);
    
    // Log total script execution time
    int totalMsec = scriptTimer.elapsed();
    double totalSec = totalMsec / 1000.0;
    logMessageDirect(tr("Total script execution time: %1 s").arg(totalSec, 0, 'f', 2), "green");
    
    emit finished(true);
    return ScriptResult::OK;
}

bool ScriptRunner::executeCommand(const ScriptCommand& cmd) {
    emit commandStarted(cmd.name, cmd.lineNumber);
    
    // Find command handler
    const CommandDef* def = getCommand(cmd.name);
    if (!def) {
        setError(tr("Unknown command: %1").arg(cmd.name), cmd.lineNumber);
        emit commandFinished(cmd.name, false);
        return false;
    }
    
    // Validate arguments
    if (!validateCommand(cmd)) {
        emit commandFinished(cmd.name, false);
        return false;
    }
    
    // Execute handler
    bool success = false;
    try {
        success = def->handler(cmd);
        
        // Process any pending events to keep UI responsive
        // and allow OS/heap to optimize memory layout
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    } catch (const std::exception& e) {
        setError(tr("Exception in %1: %2").arg(cmd.name, e.what()), cmd.lineNumber);
        success = false;
    }
    
    if (!success && m_lastError.isEmpty()) {
        setError(tr("Command failed: %1").arg(cmd.name), cmd.lineNumber);
    }
    
    emit commandFinished(cmd.name, success);
    return success;
}

//=============================================================================
// VARIABLES
//=============================================================================

void ScriptRunner::setVariable(const QString& name, const QString& value) {
    m_variables[name] = value;
}

QString ScriptRunner::variable(const QString& name) const {
    return m_variables.value(name);
}

//=============================================================================
// WORKING DIRECTORY
//=============================================================================

void ScriptRunner::setWorkingDirectory(const QString& path) {
    m_workingDir = path;
    setVariable("cwd", path);
    setVariable("WORKING_DIR", path);  // For script compatibility
}

//=============================================================================
// VALIDATION
//=============================================================================

bool ScriptRunner::validateCommand(const ScriptCommand& cmd) {
    const CommandDef* def = getCommand(cmd.name);
    if (!def) {
        return false;
    }
    
    int argc = cmd.args.size();
    
    if (argc < def->minArgs) {
        setError(tr("Too few arguments for %1: expected at least %2, got %3")
                .arg(cmd.name).arg(def->minArgs).arg(argc),
                cmd.lineNumber);
        return false;
    }
    
    if (def->maxArgs >= 0 && argc > def->maxArgs) {
        setError(tr("Too many arguments for %1: expected at most %2, got %3")
                .arg(cmd.name).arg(def->maxArgs).arg(argc),
                cmd.lineNumber);
        return false;
    }
    
    return true;
}

//=============================================================================
// LOGGING (Direct + Signal-based for compatibility)
//=============================================================================

void ScriptRunner::logMessageDirect(const QString& message, const QString& color) {
    // Direct logging without signal overhead - used in hot command loop
    // Still emit signal for UI, but the message is already logged
    emit logMessage(message, color);
}

//=============================================================================
// ERROR HANDLING
//=============================================================================

void ScriptRunner::setError(const QString& message, int lineNumber) {
    m_lastError = message;
    m_lastErrorLine = lineNumber;
    
    QString logMsg = lineNumber > 0 
        ? tr("Line %1: %2").arg(lineNumber).arg(message)
        : message;
    emit logMessage(logMsg, "red");
}

void ScriptRunner::requestCancel() {
    qDebug() << "ScriptRunner::requestCancel called";
    m_cancelled = true;
    emit cancelRequested();
    // Also set global thread state to signal all operations to stop
    Threading::setThreadRun(false);
}

void ScriptRunner::resetCancel() {
    m_cancelled = false;
    // Reset global thread state to allow new operations
    Threading::setThreadRun(true);
}

} // namespace Scripting
