#ifndef SCRIPT_BROWSER_DIALOG_H
#define SCRIPT_BROWSER_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>

/**
 * @brief Dialog for browsing and selecting TStar script files.
 *
 * Scans the application's scripts directory for .tss and .txt files,
 * displays them in a list, and shows a read-only preview of the selected
 * script's content. The user can open the script in an external editor
 * or execute it directly from this dialog.
 */
class ScriptBrowserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptBrowserDialog(QWidget* parent = nullptr);

    /**
     * @brief Returns the file path of the script selected by the user.
     */
    QString selectedScript() const;

private slots:
    void onScriptSelected(QListWidgetItem* item);
    void onRunScript();
    void onEditScript();
    void refreshScriptList();

private:
    void    setupUI();
    void    loadScripts();

    /**
     * @brief Resolves the path to the application's scripts directory,
     *        searching several platform-specific and development locations.
     */
    QStringList scriptDirs() const;

    QListWidget* m_scriptList;
    QTextEdit*   m_preview;
    QPushButton* m_runBtn;
    QPushButton* m_editBtn;
    QPushButton* m_refreshBtn;
    QPushButton* m_closeBtn;

    QString m_selectedPath;
};

#endif // SCRIPT_BROWSER_DIALOG_H