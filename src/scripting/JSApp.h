// =============================================================================
// JSApp.h
//
// Wraps the TStar application context (MainWindow) for JavaScript scripting.
// Provides access to open image windows, file operations, and app metadata.
// =============================================================================

#ifndef JSAPP_H
#define JSAPP_H

#include <QObject>
#include <QPointer>
#include <QVariant>

class MainWindow;

namespace Scripting {

class JSRuntime;

class JSApp : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version  READ version  CONSTANT)
    Q_PROPERTY(QString appName  READ appName  CONSTANT)

public:
    explicit JSApp(MainWindow* mainWindow, JSRuntime* runtime,
                   QObject* parent = nullptr);
    ~JSApp() override = default;

    // -- Properties -----------------------------------------------------------

    QString version() const;
    QString appName() const;

    // -- Window access --------------------------------------------------------

    /**
     * @brief Get the currently active image window.
     * @return JSView* for the active window, or null if none is open.
     */
    Q_INVOKABLE QObject* activeWindow();

    /**
     * @brief Get a list of all open image windows.
     * @return Array of JSView* objects.
     */
    Q_INVOKABLE QVariantList windows();

    /**
     * @brief Open an image file and return its view.
     * @param filePath  Path to the image file.
     * @return JSView* for the newly opened window, or null on failure.
     */
    Q_INVOKABLE QObject* open(const QString& filePath);

    // -- Utility --------------------------------------------------------------

    /**
     * @brief Sleep for the given number of milliseconds.
     *
     * Useful for scripts that need to wait for external processes
     * or pace their execution.
     */
    Q_INVOKABLE void sleep(int milliseconds);

    /** @brief Trigger a global Undo operation in the application. */
    Q_INVOKABLE void undo();

    /** @brief Trigger a global Redo operation in the application. */
    Q_INVOKABLE void redo();

    /**
     * @brief Write a log message to the TStar application log.
     * @param message The message to log.
     * @param type    0=Info, 1=Success, 2=Warning, 3=Error (default: 0).
     */
    Q_INVOKABLE void log(const QString& message, int type = 0);

private:
    QPointer<MainWindow> m_mainWindow;
    JSRuntime*           m_runtime;
};

} // namespace Scripting

#endif // JSAPP_H