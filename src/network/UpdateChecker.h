#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    void checkForUpdates(const QString& currentVersion);

signals:
    void updateAvailable(const QString& newVersion, const QString& changelog, const QString& downloadUrl);
    void noUpdateAvailable();
    void errorOccurred(const QString& error);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_nam;
    QString m_currentVersion;
    
    // State for pending update notify
    QString m_pendingVersion;
    QString m_pendingDownloadUrl;

    bool isNewer(const QString& current, const QString& remote);
    void fetchChangelog();
};

#endif // UPDATECHECKER_H
