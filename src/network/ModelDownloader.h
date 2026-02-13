#ifndef MODELDOWNLOADER_H
#define MODELDOWNLOADER_H

#include <QObject>
#include <QString>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryFile>
#include <atomic>

// ---------------------------------------------------------------------------
// ModelDownloader – downloads Cosmic Clarity models ZIP from Google Drive
// (primary + backup) or a GitHub HTTP mirror, extracts into the app-local
// models directory, and writes a manifest.json.
// ---------------------------------------------------------------------------

class ModelDownloaderWorker : public QObject {
    Q_OBJECT
public:
    explicit ModelDownloaderWorker(QObject* parent = nullptr);

public slots:
    void run();
    void cancel();

signals:
    void progress(const QString& message);
    void progressValue(int value);
    void finished(bool ok, const QString& message);

private:
    bool downloadGoogleDrive(const QString& fileId, const QString& destPath);
    bool downloadHttp(const QString& url, const QString& destPath);
    bool extractZip(const QString& zipPath, const QString& destDir);
    bool parseGDriveInterstitial(const QByteArray& html, QString& action, QMap<QString, QString>& params);
    QString extractDriveFileId(const QString& urlOrId);

    std::atomic<bool> m_cancel{false};
};

class ModelDownloader : public QObject {
    Q_OBJECT
public:
    explicit ModelDownloader(QObject* parent = nullptr);
    ~ModelDownloader();

    void startDownload();

    // Static helpers
    static QString modelsRoot();
    static QString cosmicClarityRoot();
    static bool modelsInstalled();

signals:
    void progress(const QString& message);
    void progressValue(int value);
    void finished(bool ok, const QString& message);

public slots:
    void cancel();

private:
    QThread* m_thread = nullptr;
    ModelDownloaderWorker* m_worker = nullptr;
};

#endif // MODELDOWNLOADER_H
