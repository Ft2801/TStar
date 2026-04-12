// =============================================================================
// ScriptConsoleWidget.h
//
// Full-featured JavaScript scripting console for the right sidebar.
// Contains a code editor with syntax highlighting, output log, toolbar,
// API reference panel, and template selector.
// =============================================================================

#ifndef SCRIPTCONSOLEWIDGET_H
#define SCRIPTCONSOLEWIDGET_H

#include <QWidget>
#include <QPointer>

class QPlainTextEdit;
class QTextEdit;
class QPushButton;
class QComboBox;
class QSplitter;
class QTabWidget;
class QTreeWidget;
class QLabel;
class QProgressBar;

namespace Scripting {
class JSRuntime;
}

class ScriptConsoleWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScriptConsoleWidget(QWidget* parent = nullptr);
    ~ScriptConsoleWidget() override = default;

    /**
     * @brief Set the JSRuntime instance to use for script execution.
     *
     * Must be called before the user can run scripts. Connects
     * the runtime's output/error signals to the console log.
     */
    void setRuntime(Scripting::JSRuntime* runtime);

private slots:
    void onRunScript();
    void onStopScript();
    void onClearOutput();
    void onLoadScript();
    void onSaveScript();
    void onTemplateSelected(int index);
    void onScriptSelected(int index);

    void onStandardOutput(const QString& message);
    void onStandardError(const QString& message);
    void onScriptStarted();
    void onScriptFinished(bool success);

private:
    void setupUI();
    void setupToolbar(QWidget* parent, class QVBoxLayout* layout);
    void setupEditor(QSplitter* splitter);
    void setupOutputLog(QSplitter* splitter);
    void setupApiReference(QTabWidget* tabs);
    void setupTemplates();
    void setupScriptsMenu();

    /**
     * @brief Populate the API reference tree with entries from
     *        JSRuntime::buildApiReference().
     */
    void populateApiReference();

    /**
     * @brief Append a colored message to the output log.
     */
    void appendLog(const QString& message, const QString& color = "#cccccc");

    // -- UI elements ----------------------------------------------------------

    // Toolbar
    QPushButton*    m_runBtn      = nullptr;
    QPushButton*    m_stopBtn     = nullptr;
    QPushButton*    m_clearBtn    = nullptr;
    QPushButton*    m_loadBtn     = nullptr;
    QPushButton*    m_saveBtn     = nullptr;
    QPushButton*    m_refreshScriptsBtn = nullptr;
    QComboBox*      m_scriptsCombo  = nullptr;
    QComboBox*      m_templateCombo = nullptr;

    // Editor
    QPlainTextEdit* m_editor      = nullptr;

    // Output
    QTextEdit*      m_outputLog   = nullptr;

    // API Reference
    QTreeWidget*    m_apiTree     = nullptr;

    // Status
    QLabel*         m_statusLabel = nullptr;

    // -- Data -----------------------------------------------------------------
    QPointer<Scripting::JSRuntime> m_runtime;
    QString m_currentFile;
};

#endif // SCRIPTCONSOLEWIDGET_H
