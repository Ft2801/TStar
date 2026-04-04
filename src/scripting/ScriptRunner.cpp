/**
 * @file ScriptRunner.cpp
 * @brief Implementation of the script execution engine.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "ScriptRunner.h"
#include "StackingCommands.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QRegularExpression>

#include "../core/ThreadState.h"

#include <cstdlib>
#include <functional>

namespace Scripting {

// ============================================================================
// Platform-specific memory query
// ============================================================================

#ifdef _WIN32
#include <windows.h>

/**
 * @brief Query the amount of physical memory currently available (in MB).
 * @return Available physical memory in megabytes, or 0 on failure.
 */
static long long getAvailableMemory()
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    return static_cast<long long>(statex.ullAvailPhys / (1024 * 1024));
}

#else

static long long getAvailableMemory()
{
    return 0; // Not implemented for this platform.
}

#endif // _WIN32

// ============================================================================
// Constructor / Destructor
// ============================================================================

ScriptRunner::ScriptRunner(QObject* parent)
    : QObject(parent)
{
    m_workingDir = QDir::currentPath();
}

ScriptRunner::~ScriptRunner() = default;

// ============================================================================
// Command registration
// ============================================================================

void ScriptRunner::registerCommand(const CommandDef& def)
{
    m_commands[def.name.toLower()] = def;
}

void ScriptRunner::registerCommands(const QVector<CommandDef>& defs)
{
    for (const auto& def : defs)
        registerCommand(def);
}

bool ScriptRunner::hasCommand(const QString& name) const
{
    return m_commands.contains(name.toLower());
}

const CommandDef* ScriptRunner::getCommand(const QString& name) const
{
    auto it = m_commands.find(name.toLower());
    return (it != m_commands.end()) ? &it.value() : nullptr;
}

QStringList ScriptRunner::registeredCommands() const
{
    return m_commands.keys();
}

// ============================================================================
// Script execution -- file entry point
// ============================================================================

ScriptResult ScriptRunner::executeFile(const QString& path)
{
    m_cancelled = false;
    Threading::setThreadRun(true);

    QFileInfo fi(path);
    if (!fi.exists()) {
        setError(tr("Script file not found: %1").arg(path), 0);
        return ScriptResult::FileError;
    }

    // Scripts execute relative to the user-defined working directory
    // rather than the script file's location.
    if (m_workingDir.isEmpty()) {
        const QString scriptDir = fi.absoluteDir().absolutePath();
        if (!scriptDir.isEmpty() && QDir(scriptDir).exists()) {
            setWorkingDirectory(scriptDir);
        } else {
            setWorkingDirectory(QDir::currentPath());
        }
    }

    // Parse the file.
    ScriptParser parser;
    for (auto it = m_variables.begin(); it != m_variables.end(); ++it)
        parser.setVariable(it.key(), it.value());

    if (!parser.parseFile(path)) {
        QStringList errors = parser.errors();
        setError(errors.isEmpty() ? tr("Parse error") : errors.first(), 0);
        return ScriptResult::SyntaxError;
    }

    return executeCommands(parser.commands());
}

// ============================================================================
// Script execution -- string entry point
// ============================================================================

ScriptResult ScriptRunner::executeString(const QString& content)
{
    qDebug() << "ScriptRunner::executeString called. Content length:"
             << content.length();

    m_cancelled = false;
    Threading::setThreadRun(true);

    ScriptParser parser;
    for (auto it = m_variables.begin(); it != m_variables.end(); ++it)
        parser.setVariable(it.key(), it.value());

    if (!parser.parseString(content)) {
        QStringList errors = parser.errors();
        qDebug() << "ScriptRunner: Parse failed!"
                 << (errors.isEmpty() ? "Unknown error" : errors.first());
        setError(errors.isEmpty() ? tr("Parse error") : errors.first(), 0);
        return ScriptResult::SyntaxError;
    }

    auto commands = parser.commands();
    qDebug() << "ScriptRunner: Successfully parsed"
             << commands.size() << "commands";
    return executeCommands(commands);
}

// ============================================================================
// Script execution -- command loop
// ============================================================================

ScriptResult ScriptRunner::executeCommands(
    const QVector<ScriptCommand>& commands)
{
    const int total    = commands.size();
    int       executed = 0;

    // Snapshot available memory at the start so we can report deltas.
    long long startMem = getAvailableMemory();
    long long lastMem  = startMem;

    QElapsedTimer scriptTimer;
    scriptTimer.start();

    for (const ScriptCommand& cmd : commands) {
        // -- Cancellation check ----------------------------------------------
        if (m_cancelled) {
            logMessageDirect(tr("Script cancelled"), "salmon");
            emit finished(false);
            return ScriptResult::Cancelled;
        }

        // -- Per-command timing ----------------------------------------------
        QElapsedTimer commandTimer;
        commandTimer.start();

        emit progressChanged(tr("Executing: %1").arg(cmd.name),
                             static_cast<double>(executed) / total);

        // -- Execute the command ---------------------------------------------
        if (!executeCommand(cmd)) {
            emit finished(false);
            return ScriptResult::CommandError;
        }

        executed++;

        // -- Memory delta reporting ------------------------------------------
        const int elapsedMs = commandTimer.elapsed();
        long long currentMem = getAvailableMemory();

        if (startMem > 0) {
            long long memDiff = lastMem - currentMem;
            if (memDiff != 0) {
                logMessageDirect(
                    tr("End of command %1, memory difference: %2 MB")
                        .arg(cmd.name).arg(memDiff),
                    "neutral");
            }
        }
        lastMem = currentMem;

        // -- Timing report (skip trivially fast commands) --------------------
        double elapsedSec = elapsedMs / 1000.0;
        if (elapsedSec > 0.01) {
            logMessageDirect(
                tr("Execution time: %1 s").arg(elapsedSec, 0, 'f', 2),
                "neutral");
        }
    }

    emit progressChanged(tr("Script complete"), 1.0);

    // Report total wall-clock time for the entire script.
    double totalSec = scriptTimer.elapsed() / 1000.0;
    logMessageDirect(
        tr("Total script execution time: %1 s").arg(totalSec, 0, 'f', 2),
        "green");

    emit finished(true);
    return ScriptResult::OK;
}

// ============================================================================
// Single command execution
// ============================================================================

bool ScriptRunner::executeCommand(const ScriptCommand& cmd)
{
    emit commandStarted(cmd.name, cmd.lineNumber);

    // Look up the handler.
    const CommandDef* def = getCommand(cmd.name);
    if (!def) {
        setError(tr("Unknown command: %1").arg(cmd.name), cmd.lineNumber);
        emit commandFinished(cmd.name, false);
        return false;
    }

    // Perform variable substitution on arguments and option values.
    ScriptCommand processedCmd = cmd;

    for (int i = 0; i < processedCmd.args.size(); ++i)
        processedCmd.args[i] = substituteVariables(processedCmd.args[i]);

    for (auto it = processedCmd.options.begin();
         it != processedCmd.options.end(); ++it)
        it.value() = substituteVariables(it.value());

    // Validate argument count.
    if (!validateCommand(processedCmd)) {
        emit commandFinished(processedCmd.name, false);
        return false;
    }

    // Invoke the handler inside a try/catch to guard against exceptions.
    bool success = false;
    try {
        success = def->handler(processedCmd);

        // Allow the event loop to process pending events so the UI remains
        // responsive and the OS can reclaim freed memory pages.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    } catch (const std::exception& e) {
        setError(tr("Exception in %1: %2")
                     .arg(processedCmd.name, e.what()),
                 processedCmd.lineNumber);
        success = false;
    }

    if (!success && m_lastError.isEmpty())
        setError(tr("Command failed: %1").arg(processedCmd.name),
                 processedCmd.lineNumber);

    emit commandFinished(processedCmd.name, success);
    return success;
}

// ============================================================================
// Variable substitution (runtime)
// ============================================================================

QString ScriptRunner::substituteVariables(const QString& text) const
{
    QString result = text;

    // Lambda that resolves a single variable name, checking script variables
    // first, then dynamic image metadata.
    auto resolve = [&](const QString& name) -> QString {
        // 1. Script-level variables.
        if (m_variables.contains(name))
            return m_variables[name];

        // 2. Dynamic variables from the currently loaded image.
        const ImageBuffer* img = StackingCommands::getCurrentImage();
        if (img && img->isValid()) {
            if (name == "FILTER")   return img->getHeaderValue("FILTER");
            if (name == "EXPTIME")  return img->getHeaderValue("EXPTIME");
            if (name == "DATE")     return img->getHeaderValue("DATE-OBS");
            if (name == "OBJECT")   return img->getHeaderValue("OBJECT");
            if (name == "INSTRUME") return img->getHeaderValue("INSTRUME");
            if (name == "LIVETIME") return img->getHeaderValue("EXPTIME");
        }

        return "";
    };

    // Substitute ${variable} syntax.
    {
        QRegularExpression bracePattern("\\$\\{([^}]+)\\}");
        QRegularExpressionMatchIterator it = bracePattern.globalMatch(result);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString val = resolve(match.captured(1));
            if (!val.isEmpty())
                result.replace(match.captured(0), val);
        }
    }

    // Substitute $variable syntax (simple identifier).
    {
        QRegularExpression simplePattern("\\$([A-Za-z_][A-Za-z0-9_]*)");
        QRegularExpressionMatchIterator it = simplePattern.globalMatch(result);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString val = resolve(match.captured(1));
            if (!val.isEmpty())
                result.replace(match.captured(0), val);
        }
    }

    return result;
}

// ============================================================================
// Variables
// ============================================================================

void ScriptRunner::setVariable(const QString& name, const QString& value)
{
    m_variables[name] = value;
}

QString ScriptRunner::variable(const QString& name) const
{
    return m_variables.value(name);
}

// ============================================================================
// Working directory
// ============================================================================

void ScriptRunner::setWorkingDirectory(const QString& path)
{
    m_workingDir = path;
    setVariable("cwd", path);
    setVariable("WORKING_DIR", path);
}

// ============================================================================
// Command validation
// ============================================================================

bool ScriptRunner::validateCommand(const ScriptCommand& cmd)
{
    const CommandDef* def = getCommand(cmd.name);
    if (!def)
        return false;

    const int argc = cmd.args.size();

    if (argc < def->minArgs) {
        setError(
            tr("Too few arguments for %1: expected at least %2, got %3")
                .arg(cmd.name).arg(def->minArgs).arg(argc),
            cmd.lineNumber);
        return false;
    }

    if (def->maxArgs >= 0 && argc > def->maxArgs) {
        setError(
            tr("Too many arguments for %1: expected at most %2, got %3")
                .arg(cmd.name).arg(def->maxArgs).arg(argc),
            cmd.lineNumber);
        return false;
    }

    return true;
}

// ============================================================================
// Logging
// ============================================================================

void ScriptRunner::logMessageDirect(const QString& message,
                                    const QString& color)
{
    emit logMessage(message, color);
}

// ============================================================================
// Error handling
// ============================================================================

void ScriptRunner::setError(const QString& message, int lineNumber)
{
    m_lastError     = message;
    m_lastErrorLine = lineNumber;

    QString logMsg = (lineNumber > 0)
        ? tr("Line %1: %2").arg(lineNumber).arg(message)
        : message;
    emit logMessage(logMsg, "red");
}

// ============================================================================
// Cancellation
// ============================================================================

void ScriptRunner::requestCancel()
{
    qDebug() << "ScriptRunner::requestCancel called";
    m_cancelled = true;
    emit cancelRequested();

    // Signal all background operations to stop immediately.
    Threading::setThreadRun(false);
}

void ScriptRunner::resetCancel()
{
    m_cancelled = false;
    Threading::setThreadRun(true);
}

} // namespace Scripting