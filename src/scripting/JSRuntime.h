// =============================================================================
// JSRuntime.h
//
// JavaScript runtime environment for TStar using Qt's QJSEngine.
// Provides a PixInsight-style scripting interface where users can create
// Process objects, set parameters via properties, and execute them on images.
//
// The runtime registers all C++ API objects (App, Console, Process factories)
// into the global JS scope so that scripts can be written as pure JS without
// import statements.
// =============================================================================

#ifndef JSRUNTIME_H
#define JSRUNTIME_H

#include <QObject>
#include <QJSEngine>
#include <QPointer>
#include <QThread>

class MainWindow;

namespace Scripting {

// Forward declarations
class JSConsole;
class JSApp;
class JSProcessFactory;

// =============================================================================
// JSRuntime
// =============================================================================

class JSRuntime : public QObject {
    Q_OBJECT

public:
    explicit JSRuntime(MainWindow* mainWindow, QObject* parent = nullptr);
    ~JSRuntime() override;

    /**
     * @brief Evaluate a JavaScript source string synchronously.
     * @param script   The JavaScript source code.
     * @param fileName Optional file name for error reporting.
     * @return Execution result as a string, or error description on failure.
     */
    QString evaluate(const QString& script, const QString& fileName = QString());

    /**
     * @brief Load and evaluate a JavaScript file.
     * @param filePath Absolute path to the .js file.
     * @return Execution result as a string, or error description on failure.
     */
    QString evaluateFile(const QString& filePath);

    /**
     * @brief Execute a script asynchronously on a worker thread.
     *
     * Results are reported via the standardOutput, standardError,
     * and scriptFinished signals. The script runs in a new QJSEngine
     * instance to ensure thread safety.
     *
     * @param script   The JavaScript source code.
     * @param fileName Optional file name for error reporting.
     */
    void evaluateAsync(const QString& script, const QString& fileName = QString());

    /**
     * @brief Cancel an asynchronous script execution.
     *
     * Sets the interrupt flag on the engine. The script will stop
     * at the next JS statement boundary.
     */
    void cancelExecution();

    /** @brief Returns true if an async script is currently running. */
    bool isRunning() const { return m_running; }

    /** @brief Access the underlying QJSEngine (for advanced use). */
    QJSEngine* engine() { return &m_engine; }

    /** @brief Access the MainWindow context. */
    MainWindow* mainWindow() const;

    /**
     * @brief Build a structured API reference for display in the UI.
     *
     * Returns a list of objects, each containing name, description,
     * properties, methods, and example code.
     */
    static QVariantList buildApiReference();

signals:
    void standardOutput(const QString& message);
    void standardError(const QString& message);
    void scriptStarted();
    void scriptFinished(bool success);

private:
    /**
     * @brief Register all C++ APIs into the QJSEngine global object.
     *
     * Called once during construction. Creates Console, App, and
     * factory-based constructor shims (Curves, Saturation, etc.).
     */
    void registerAPIs();

    QJSEngine               m_engine;
    QPointer<MainWindow>    m_mainWindow;
    JSConsole*              m_console   = nullptr;
    JSApp*                  m_app       = nullptr;
    JSProcessFactory*       m_factory   = nullptr;
    bool                    m_running   = false;
    QPointer<QThread>       m_currentWorkerThread;
    class ScriptWorker*     m_activeWorker = nullptr;
};

/**
 * @brief Background worker for asynchronous script execution.
 * Owns its own QJSEngine to ensure thread safety.
 */
class ScriptWorker : public QObject {
    Q_OBJECT
public:
    ScriptWorker(MainWindow* mainWindow, const QString& script, const QString& fileName);
    ~ScriptWorker() override;

public slots:
    void run();
    void requestCancellation();

signals:
    void standardOutput(const QString& message);
    void standardError(const QString& message);
    void finished(bool success);

private:
    MainWindow*             m_mainWindow;
    QString                 m_script;
    QString                 m_fileName;
    QPointer<JSRuntime>     m_runtime;
    bool                    m_isCancelled = false;
};

// =============================================================================
// JSConsole
//
// Global 'Console' object exposed to JavaScript. Bridges console.log(),
// console.warn(), and console.error() to the runtime's signal-based logger.
// =============================================================================

class JSConsole : public QObject {
    Q_OBJECT

public:
    explicit JSConsole(JSRuntime* runtime, QObject* parent = nullptr);

    Q_INVOKABLE void log(const QString& msg);
    Q_INVOKABLE void warn(const QString& msg);
    Q_INVOKABLE void error(const QString& msg);
    Q_INVOKABLE void info(const QString& msg);

private:
    JSRuntime* m_runtime;
};

} // namespace Scripting

#endif // JSRUNTIME_H
