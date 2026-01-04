#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <QDialog>
#include <QScrollArea>
#include <QTextBrowser>

class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);

private:
    QTextBrowser* m_browser;
    void setupUI();
    QString buildHelpContent();
};

#endif // HELPDIALOG_H
