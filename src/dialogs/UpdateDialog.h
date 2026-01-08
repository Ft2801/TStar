#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include <QDialog>
#include <QDialog>
#include <QtNetwork>
#include <QFile>
#include <QFile>

class QTextBrowser;
class QProgressBar;
class QLabel;
class QPushButton;

#include "DialogBase.h"

class UpdateDialog : public DialogBase {
    Q_OBJECT
public:
    UpdateDialog(QWidget* parent, const QString& version, const QString& changelog, const QString& downloadUrl);
    ~UpdateDialog();

private slots:
    void startDownload();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onReadyRead();

private:
    QString m_downloadUrl;
    QString m_destinationPath;
    
    QTextBrowser* m_changelogView;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QPushButton* m_updateBtn;
    QPushButton* m_cancelBtn;
    
    QNetworkAccessManager* m_nam;
    QNetworkReply* m_reply;
    QFile* m_file;
    
    void launchInstaller();
};

#endif // UPDATEDIALOG_H
