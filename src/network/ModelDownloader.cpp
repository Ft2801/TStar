#include "ModelDownloader.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkCookieJar>
#include <QEventLoop>
#include <QTimer>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QDebug>
#include <QCoreApplication>
#include <QProcess>
#include <QUrlQuery>
#include <QUrl>
#include <QDateTime>
#include <QOverload>

// Third-party ZIP support via Qt (minizip is not available, so we use a
// simple approach: call the bundled Python to unzip, or do a manual unzip
// using QProcess + built-in tools).
// For robustness we implement a minimal ZIP reader using Qt's built-in
// support or delegate to platform tools.

// Download URLs
static const QString PRIMARY_GDRIVE =
    "https://drive.google.com/file/d/1d0wQr8Oau9UH3IalMW5anC0_oddxBjh3/view?usp=drive_link";
static const QString BACKUP_GDRIVE =
    "https://drive.google.com/file/d/1XgqKNd8iBgV3LW8CfzGyS4jigxsxIf86/view?usp=drive_link";
static const QString GITHUB_MIRROR =
    "https://github.com/setiastro/setiastrosuitepro/releases/download/benchmarkFIT/SASPro_Models_AI4.zip";

// ============================================================================
// ModelDownloaderWorker
// ============================================================================

ModelDownloaderWorker::ModelDownloaderWorker(QObject* parent)
    : QObject(parent) {}

void ModelDownloaderWorker::cancel() {
    m_cancel = true;
}

QString ModelDownloaderWorker::extractDriveFileId(const QString& urlOrId) {
    // Try /file/d/ID pattern
    QRegularExpression re("/file/d/([a-zA-Z0-9_-]+)");
    auto m = re.match(urlOrId);
    if (m.hasMatch()) return m.captured(1);

    // Try ?id=ID pattern
    QRegularExpression re2("[?&]id=([a-zA-Z0-9_-]+)");
    m = re2.match(urlOrId);
    if (m.hasMatch()) return m.captured(1);

    // Raw ID
    QRegularExpression re3("^[0-9A-Za-z_-]{10,}$");
    if (re3.match(urlOrId).hasMatch()) return urlOrId;

    return {};
}

bool ModelDownloaderWorker::parseGDriveInterstitial(const QByteArray& html,
                                                     QString& action,
                                                     QMap<QString, QString>& params) {
    QString h = QString::fromUtf8(html);

    // Find form action
    QRegularExpression reForm("<form[^>]+id=\"download-form\"[^>]+action=\"([^\"]+)\"");
    auto m = reForm.match(h);
    if (!m.hasMatch()) return false;
    action = m.captured(1);

    // Find hidden inputs
    QRegularExpression reInput("<input[^>]+type=\"hidden\"[^>]+name=\"([^\"]+)\"[^>]*value=\"([^\"]*)\"");
    auto it = reInput.globalMatch(h);
    while (it.hasNext()) {
        auto match = it.next();
        params[match.captured(1)] = match.captured(2);
    }

    // Also find hidden inputs without value attribute
    QRegularExpression reInput2("<input[^>]+type=\"hidden\"[^>]+name=\"([^\"]+)\"(?![^>]*value=)");
    auto it2 = reInput2.globalMatch(h);
    while (it2.hasNext()) {
        auto match = it2.next();
        if (!params.contains(match.captured(1)))
            params[match.captured(1)] = "";
    }

    return true;
}

bool ModelDownloaderWorker::downloadGoogleDrive(const QString& fileId, const QString& destPath) {
    if (fileId.isEmpty()) return false;

    // Usa una singola NAM statica per mantenere sessione e cookie tra le richieste
    // Questo evita che Google Drive chieda di nuovo la conferma
    static QNetworkAccessManager nam;
    static bool initialized = false;
    if (!initialized) {
        nam.setCookieJar(new QNetworkCookieJar(&nam));
        initialized = true;
    }
    
    QString url = QString("https://drive.google.com/uc?export=download&id=%1").arg(fileId);

    emit progress(tr("Connecting to Google Drive…"));

    // First request with timeout
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-ModelDownloader");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    QTimer timeout1;
    timeout1.setSingleShot(true);
    bool timedOut = false;
    connect(&timeout1, &QTimer::timeout, [&](){ timedOut = true; loop.quit(); });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout1.start(15000); // 15 sec timeout
    loop.exec();
    timeout1.stop();

    if (timedOut || reply->error() != QNetworkReply::NoError) {
        qWarning() << "Google Drive first request failed or timed out";
        reply->deleteLater();
        return downloadHttp(GITHUB_MIRROR, destPath);
    }

    // Check if we got HTML (virus scan interstitial)
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (contentType.contains("text/html")) {
        QString action;
        QMap<QString, QString> params;
        if (!parseGDriveInterstitial(data, action, params)) {
            qWarning() << "Failed to parse Google Drive confirmation form";
            emit progress(tr("Google Drive form parsing failed, trying GitHub mirror…"));
            return downloadHttp(GITHUB_MIRROR, destPath);
        }

        if (action.isEmpty()) {
            qWarning() << "Google Drive form action is empty";
            return downloadHttp(GITHUB_MIRROR, destPath);
        }

        emit progress(tr("Google Drive interstitial detected — confirming…"));
        emit progressValue(-1);

        // Build and send confirmation request with timeout
        QUrl confirmUrl(action);
        QUrlQuery query;
        for (auto it = params.begin(); it != params.end(); ++it)
            query.addQueryItem(it.key(), it.value());
        confirmUrl.setQuery(query);

        QNetworkRequest req2(confirmUrl);
        req2.setHeader(QNetworkRequest::UserAgentHeader, "TStar-ModelDownloader");
        req2.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        reply = nam.get(req2);
        QEventLoop loop2;
        QTimer timeout2;
        timeout2.setSingleShot(true);
        bool timedOut2 = false;
        connect(&timeout2, &QTimer::timeout, [&](){ timedOut2 = true; loop2.quit(); });
        connect(reply, &QNetworkReply::finished, &loop2, &QEventLoop::quit);
        timeout2.start(30000); // 30 sec timeout
        loop2.exec();
        timeout2.stop();

        if (timedOut2 || reply->error() != QNetworkReply::NoError) {
            qWarning() << "Google Drive confirmation failed or timed out";
            emit progress(tr("Google Drive confirmation failed, trying GitHub mirror…"));
            reply->deleteLater();
            return downloadHttp(GITHUB_MIRROR, destPath);
        }
    }

    // Stream main file to temporary location
    QString partPath = destPath + ".part";
    QFile file(partPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot create file:" << partPath;
        reply->deleteLater();
        return false;
    }

    qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    qint64 done = 0;
    
    // Read data in chunks as it becomes available
    QByteArray chunk;
    while (!(chunk = reply->read(1024 * 1024)).isEmpty()) {
        if (m_cancel) {
            file.close();
            QFile::remove(partPath);
            reply->deleteLater();
            return false;
        }

        file.write(chunk);
        done += chunk.size();
        
        if (total > 0) {
            int pct = qBound(0, static_cast<int>((double(done) / double(total)) * 100.0), 100);
            emit progress(tr("Downloading… %1%").arg(pct));
            emit progressValue(pct);
        } else {
            emit progress(tr("Downloading… %1 MB").arg(done / (1024.0 * 1024.0), 0, 'f', 1));
            emit progressValue(-1);
        }
    }

    file.close();
    reply->deleteLater();

    // Validate file is ZIP (check magic bytes)
    QFile checkFile(partPath);
    if (checkFile.open(QIODevice::ReadOnly)) {
        QByteArray head = checkFile.read(512);
        checkFile.close();
        
        // Check for ZIP magic bytes: PK\x03\x04
        if (head.size() >= 4 && (unsigned char)head[0] == 0x50 && (unsigned char)head[1] == 0x4B && 
            (unsigned char)head[2] == 0x03 && (unsigned char)head[3] == 0x04) {
            // Valid ZIP file
        } else if (head.contains("<html") || head.contains("<!doctype") || head.contains("class=\"a-box")) {
            // HTML error page from Google Drive
            qWarning() << "Downloaded file is HTML, not ZIP. Falling back to GitHub.";
            QFile::remove(partPath);
            return downloadHttp(GITHUB_MIRROR, destPath);
        }
    }

    // Atomic rename
    QFile::remove(destPath);
    if (!QFile::rename(partPath, destPath)) {
        QFile::remove(partPath);
        return false;
    }

    emit progress(tr("Download complete."));
    return true;
}

bool ModelDownloaderWorker::downloadHttp(const QString& url, const QString& destPath) {
    QNetworkAccessManager nam;

    emit progress(tr("Connecting to GitHub mirror…"));

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-ModelDownloader");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam.get(req);
    
    // Connect to start reading data as soon as first bytes arrive
    QString partPath = destPath + ".part";
    QFile file(partPath);
    bool fileOpened = false;
    
    qint64 total = 0;
    qint64 done = 0;
    
    connect(reply, &QNetworkReply::metaDataChanged, [&]() {
        if (!fileOpened && file.open(QIODevice::WriteOnly)) {
            fileOpened = true;
            total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            if (total > 0) {
                emit progress(tr("Downloading… 0%"));
            }
        }
    });
    
    connect(reply, &QNetworkReply::readyRead, [&]() {
        if (!fileOpened) return;
        QByteArray chunk = reply->read(1024 * 1024);
        file.write(chunk);
        done += chunk.size();
        
        if (total > 0) {
            int pct = qBound(0, static_cast<int>((double(done) / double(total)) * 100.0), 100);
            emit progress(tr("Downloading… %1%").arg(pct));
            emit progressValue(pct);
        } else {
            emit progress(tr("Downloading… %1 MB").arg(done / (1024.0 * 1024.0), 0, 'f', 1));
            emit progressValue(-1);
        }
    });

    // Wait with timeout
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    connect(&timeout, &QTimer::timeout, [&](){ timedOut = true; loop.quit(); });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(600000); // 10 min timeout for GitHub
    loop.exec();
    timeout.stop();
    
    if (fileOpened) file.close();

    if (timedOut || reply->error() != QNetworkReply::NoError) {
        emit progress(tr("GitHub mirror download failed or timed out."));
        qWarning() << "GitHub download failed:" << reply->errorString();
        QFile::remove(partPath);
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();

    // Validate ZIP
    QFile checkFile(partPath);
    if (checkFile.open(QIODevice::ReadOnly)) {
        QByteArray head = checkFile.read(512);
        checkFile.close();
        
        if (head.size() >= 4 && (unsigned char)head[0] == 0x50 && (unsigned char)head[1] == 0x4B && 
            (unsigned char)head[2] == 0x03 && (unsigned char)head[3] == 0x04) {
            // Valid ZIP
        } else {
            qWarning() << "GitHub file is not a valid ZIP";
            QFile::remove(partPath);
            return false;
        }
    }

    QFile::remove(destPath);
    if (!QFile::rename(partPath, destPath)) {
        QFile::remove(partPath);
        return false;
    }

    emit progress(tr("Download complete."));
    return true;
}

bool ModelDownloaderWorker::extractZip(const QString& zipPath, const QString& destDir) {
    emit progress(tr("Extracting models…"));
    emit progressValue(-1);

    // Create temp dir for extraction
    QString tempDir = destDir + "_extract_temp";
    QDir().mkpath(tempDir);
    
    // Use platform tools to extract ZIP
    QProcess proc;
    bool success = false;

#ifdef Q_OS_WIN
    // Try tar first (Windows 10+) - more reliable than PowerShell for large zips
    proc.setProgram("tar");
    proc.setArguments({"-xf", zipPath, "-C", tempDir});
    
    proc.start();
    if (!proc.waitForFinished(300000)) { // 5 min timeout
        emit progress(tr("Extraction timed out."));
        QDir(tempDir).removeRecursively();
        return false;
    }

    if (proc.exitCode() == 0) {
        success = true;
    } else {
        // Fallback to PowerShell Expand-Archive
        qWarning() << "tar failed, falling back to PowerShell Expand-Archive";
        proc.start("powershell", {"-NoProfile", "-NoExit", "-Command",
            QString("$ProgressPreference='SilentlyContinue'; Expand-Archive -Path \"%1\" -DestinationPath \"%2\" -Force").arg(zipPath, tempDir)});
        
        if (!proc.waitForFinished(300000)) {
            emit progress(tr("Extraction timed out."));
            QDir(tempDir).removeRecursively();
            return false;
        }
        
        if (proc.exitCode() == 0) {
            success = true;
        }
    }
#elif defined(Q_OS_MAC)
    // macOS uses bsdtar by default, which natively supports ZIP and is very robust
    proc.setProgram("tar");
    proc.setArguments({"-xf", zipPath, "-C", tempDir});

    proc.start();
    if (!proc.waitForFinished(300000)) { // 5 min timeout
        emit progress(tr("Extraction timed out."));
        QDir(tempDir).removeRecursively();
        return false;
    }

    if (proc.exitCode() == 0) {
        success = true;
    } else {
        // Fallback to unzip
        qWarning() << "tar failed on macOS, falling back to unzip";
        proc.start("unzip", {"-o", zipPath, "-d", tempDir});
         if (!proc.waitForFinished(300000)) {
            emit progress(tr("Extraction timed out."));
            QDir(tempDir).removeRecursively();
            return false;
        }
        if (proc.exitCode() == 0) success = true;
    }
#else
    // Linux/Other: unzip is the standard tool
    proc.setProgram("unzip");
    proc.setArguments({"-o", zipPath, "-d", tempDir});
    
    proc.start();
    if (!proc.waitForFinished(300000)) { // 5 min timeout
        emit progress(tr("Extraction timed out."));
        // ... cleanup handled below
    }

    if (proc.exitCode() == 0) {
        success = true;
    }
#endif

    if (!success) {
        QString errMsg = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (errMsg.isEmpty()) {
            errMsg = "Unknown extraction error";
        }
        emit progress(tr("Extraction failed: %1").arg(errMsg));
        qWarning() << "Extraction error:" << errMsg;
        QDir(tempDir).removeRecursively();
        return false;
    }
    
    // Move extracted files to destDir
    emit progress(tr("Finalizing installation…"));
    QDir temp(tempDir);
    for (const auto& entry : temp.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString src = temp.filePath(entry);
        QString dst = QDir(destDir).filePath(entry);
        QFile::remove(dst);
        QDir(dst).removeRecursively();
        if (!QFile::rename(src, dst)) {
            qWarning() << "Failed to move" << src << "to" << dst;
        }
    }
    QDir(tempDir).removeRecursively();

    // Some ZIPs contain a top-level folder; normalize by moving contents up
    QDir dest(destDir);
    QStringList entries = dest.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.size() == 1) {
        QDir subDir(dest.filePath(entries.first()));
        // Check if this single dir contains model files
        bool hasModels = false;
        for (const auto& f : subDir.entryList(QDir::Files)) {
            if (f.endsWith(".pth") || f.endsWith(".onnx")) {
                hasModels = true;
                break;
            }
        }
        if (hasModels) {
            // Move all contents from subDir to destDir
            for (const auto& entry : subDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                QString src = subDir.filePath(entry);
                QString dst = dest.filePath(entry);
                QFile::remove(dst);
                if (!QFile::rename(src, dst)) {
                    qWarning() << "Failed to move" << src << "to" << dst;
                }
            }
            subDir.removeRecursively();
        }
    }

    emit progress(tr("Extraction complete."));
    emit progressValue(100);
    return true;
}

void ModelDownloaderWorker::run() {
    m_cancel = false;
    emit progressValue(-1);

    QString modelsDir = ModelDownloader::cosmicClarityRoot();
    QDir().mkpath(modelsDir);

    QString tmpZip = QDir::tempPath() + "/tstar_models_latest.zip";

    bool ok = false;
    QString usedSource;

    // 1) Try primary Google Drive
    QString fid1 = extractDriveFileId(PRIMARY_GDRIVE);
    if (!fid1.isEmpty() && !m_cancel) {
        emit progress(tr("Downloading from primary (Google Drive)…"));
        ok = downloadGoogleDrive(fid1, tmpZip);
        if (ok) usedSource = "google_drive_primary";
        else QFile::remove(tmpZip); // Clean up failed download
    }

    // 2) Try backup Google Drive
    if (!ok && !m_cancel) {
        QString fid2 = extractDriveFileId(BACKUP_GDRIVE);
        if (!fid2.isEmpty()) {
            emit progress(tr("Primary failed. Trying backup (Google Drive)…"));
            ok = downloadGoogleDrive(fid2, tmpZip);
            if (ok) usedSource = "google_drive_backup";
            else QFile::remove(tmpZip); // Clean up failed download
        }
    }

    // 3) Try GitHub HTTP mirror
    if (!ok && !m_cancel) {
        emit progress(tr("Google Drive failed. Trying GitHub mirror…"));
        ok = downloadHttp(GITHUB_MIRROR, tmpZip);
        if (ok) usedSource = "github_http";
        else QFile::remove(tmpZip); // Clean up failed download
    }

    if (m_cancel) {
        QFile::remove(tmpZip);
        emit finished(false, tr("Download cancelled."));
        return;
    }

    if (!ok) {
        QFile::remove(tmpZip);
        emit finished(false, tr("All download sources failed. Please check your internet connection."));
        return;
    }

    // 4) Clear existing models
    emit progress(tr("Installing models…"));
    QDir md(modelsDir);
    for (const auto& entry : md.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString path = md.filePath(entry);
        QFileInfo fi(path);
        if (fi.isDir()) {
            QDir(path).removeRecursively();
        } else {
            QFile::remove(path);
        }
    }

    // 5) Extract ZIP
    if (!extractZip(tmpZip, modelsDir)) {
        QFile::remove(tmpZip);
        emit finished(false, tr("Failed to extract models ZIP."));
        return;
    }

    // 6) Write manifest
    QJsonObject manifest;
    manifest["source"] = usedSource;
    manifest["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile manifestFile(modelsDir + "/manifest.json");
    if (manifestFile.open(QIODevice::WriteOnly)) {
        manifestFile.write(QJsonDocument(manifest).toJson());
        manifestFile.close();
    }

    // 7) Cleanup
    QFile::remove(tmpZip);
    emit progressValue(100);

    // 8) Verify
    if (ModelDownloader::modelsInstalled()) {
        emit finished(true, tr("Models downloaded and installed successfully."));
    } else {
        // Detailed error message showing what was actually extracted
        QDir dest(ModelDownloader::cosmicClarityRoot());
        QStringList files = dest.entryList(QDir::Files);
        QString fileList = files.isEmpty() ? "No files found" : files.join(", ");
        
        qWarning() << "Model verification failed. Expected deep_sharp_stellar_AI4.pth but found:" << fileList;
        emit finished(false, tr("Models extraction completed but verification failed - expected file 'deep_sharp_stellar_AI4.pth' not found. Files extracted: %1").arg(fileList));
    }
}

// ============================================================================
// ModelDownloader (main-thread wrapper)
// ============================================================================

ModelDownloader::ModelDownloader(QObject* parent) : QObject(parent) {}

ModelDownloader::~ModelDownloader() {
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

QString ModelDownloader::modelsRoot() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString path = dataDir + "/TStar/models";
    QDir().mkpath(path);
    return path;
}

QString ModelDownloader::cosmicClarityRoot() {
    QString path = modelsRoot() + "/CosmicClarity";
    QDir().mkpath(path);
    return path;
}

bool ModelDownloader::modelsInstalled() {
    QString sentinel = cosmicClarityRoot() + "/deep_sharp_stellar_AI4.pth";
    return QFile::exists(sentinel);
}

void ModelDownloader::startDownload() {
    if (m_thread) return; // already running

    m_thread = new QThread(this);
    m_worker = new ModelDownloaderWorker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &ModelDownloaderWorker::run);
    connect(m_worker, &ModelDownloaderWorker::progress, this, &ModelDownloader::progress);
    connect(m_worker, &ModelDownloaderWorker::finished, this, [this](bool ok, const QString& msg) {
        emit finished(ok, msg);
        m_thread->quit();
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    connect(m_worker, &ModelDownloaderWorker::progressValue, this, &ModelDownloader::progressValue);
    connect(m_thread, &QThread::finished, this, [this]() {
        m_thread = nullptr;
        m_worker = nullptr;
    });

    m_thread->start();
}

void ModelDownloader::cancel() {
    if (m_worker) {
        m_worker->cancel();
    }
}
