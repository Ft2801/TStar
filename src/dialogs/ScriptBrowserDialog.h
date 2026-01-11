#ifndef SCRIPT_BROWSER_DIALOG_H
#define SCRIPT_BROWSER_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

class ScriptBrowserDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScriptBrowserDialog(QWidget* parent = nullptr);
    
    QString selectedScript() const;
    
private slots:
    void onScriptSelected(QListWidgetItem* item);
    void onRunScript();
    void onEditScript();
    void refreshScriptList();
    
private:
    void setupUI();
    void loadScripts();
    QString scriptsDir() const;
    
    QListWidget* m_scriptList;
    QTextEdit* m_preview;
    QPushButton* m_runBtn;
    QPushButton* m_editBtn;
    QPushButton* m_refreshBtn;
    QPushButton* m_closeBtn;
    
    QString m_selectedPath;
};

#endif // SCRIPT_BROWSER_DIALOG_H
