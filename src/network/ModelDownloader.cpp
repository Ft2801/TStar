/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

/**
 * @file ModelDownloader.cpp
 * @brief Implementation of AI model downloader
 *
 * Downloads Cosmic Clarity AI models from Google Drive or GitHub mirror,
 * extracts the ZIP archive, and verifies installation. Handles Google Drive's
 * virus scan interstitial page for large files.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

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
#include <QProcess>
#include <QUrlQuery>
#include <QUrl>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>

/* ============================================================================
 * Constants
 * ============================================================================ */

namespace {

/** Primary Google Drive download URL */
const QString PRIMARY_GDRIVE_URL =
    "https://drive.google.com/file/d/1d0wQr8Oau9UH3IalMW5anC0_oddxBjh3/view?usp=drive_link";

/** Backup Google Drive download URL */
const QString BACKUP_GDRIVE_URL =
    "https://drive.google.com/file/d/1XgqKNd8iBgV3LW8CfzGyS4jigxsxIf86/view?usp=drive_link";

/** GitHub mirror download URL (direct HTTP) */
const QString GITHUB_MIRROR_URL =
    "https://github.com/setiastro/setiastrosuitepro/releases/download/benchmarkFIT/SASPro_Models_AI4.zip";

/** Timeout for initial Google Drive request (15 seconds) */
constexpr int GDRIVE_INITIAL_TIMEOUT_MS = 15000;

/** Timeout for Google Drive confirmation request (30 seconds) */
constexpr int GDRIVE_CONFIRM_TIMEOUT_MS = 30000;

/** Timeout for large file downloads (10 minutes) */
constexpr int DOWNLOAD_TIMEOUT_MS = 600000;

/** Timeout for ZIP extraction (5 minutes) */
constexpr int EXTRACTION_TIMEOUT_MS = 300000;

/** Sentinel file to verify model installation */
const QString SENTINEL_MODEL_FILE = "deep_sharp_stellar_AI4.pth";

/** ZIP file magic bytes */
constexpr unsigned char ZIP_MAGIC_0 = 0x50;  // 'P'
constexpr unsigned char ZIP_MAGIC_1 = 0x4B;  // 'K'
constexpr unsigned char ZIP_MAGIC_2 = 0x03;
constexpr unsigned char ZIP_MAGIC_3 = 0x04;

}  // anonymous namespace

/* ============================================================================
 * ModelDownloaderWorker Implementation
 * ============================================================================ */

ModelDownloaderWorker::ModelDownloaderWorker(QObject* parent)
    : QObject(parent)
{
}

void ModelDownloaderWorker::cancel()
{
    m_cancel = true;
}

/* ----------------------------------------------------------------------------
 * Google Drive Helpers
 * ---------------------------------------------------------------------------- */

QString ModelDownloaderWorker::extractDriveFileId(const QString& urlOrId)
{
    /* Pattern 1: /file/d/FILE_ID/... */
    QRegularExpression filePattern("/file/d/([a-zA-Z0-9_-]+)");
    auto match = filePattern.match(urlOrId);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    /* Pattern 2: ?id=FILE_ID or &id=FILE_ID */
    QRegularExpression idPattern("[?&]id=([a-zA-Z0-9_-]+)");
    match = idPattern.match(urlOrId);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    /* Pattern 3: Raw file ID (10+ alphanumeric characters) */
    QRegularExpression rawPattern("^[0-9A-Za-z_-]{10,}$");
    if (rawPattern.match(urlOrId).hasMatch()) {
        return urlOrId;
    }

    return QString();
}

bool ModelDownloaderWorker::parseGDriveInterstitial(const QByteArray& html,
                                                    QString& action,
                                                    QMap<QString, QString>& params)
{
    QString content = QString::fromUtf8(html);

    /* Extract form action URL */
    QRegularExpression formPattern(
        "<form[^>]+id=\"download-form\"[^>]+action=\"([^\"]+)\"");
    auto match = formPattern.match(content);

    if (!match.hasMatch()) {
        return false;
    }

    action = match.captured(1);

    /* Extract hidden input fields with values */
    QRegularExpression inputPattern(
        "<input[^>]+type=\"hidden\"[^>]+name=\"([^\"]+)\"[^>]*value=\"([^\"]*)\"");
    auto iterator = inputPattern.globalMatch(content);

    while (iterator.hasNext()) {
        auto inputMatch = iterator.next();
        params[inputMatch.captured(1)] = inputMatch.captured(2);
    }

    /* Extract hidden inputs without value attribute */
    QRegularExpression inputNoValuePattern(
        "<input[^>]+type=\"hidden\"[^>]+name=\"([^\"]+)\"(?![^>]*value=)");
    auto iterator2 = inputNoValuePattern.globalMatch(content);

    while (iterator2.hasNext()) {
        auto inputMatch = iterator2.next();
        if (!params.contains(inputMatch.captured(1))) {
            params[inputMatch.captured(1)] = QString();
        }
    }

    return true;
}

/* ----------------------------------------------------------------------------
 * Download Methods
 * ---------------------------------------------------------------------------- */

/**
 * @brief Validate that a file is a valid ZIP archive
 *
 * @param filePath Path to the file to validate
 * @return true if file has ZIP magic bytes, false otherwise
 */
static bool isValidZipFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray header = file.read(4);
    file.close();

    if (header.size() < 4) {
        return false;
    }

    return (static_cast<unsigned char>(header[0]) == ZIP_MAGIC_0 &&
            static_cast<unsigned char>(header[1]) == ZIP_MAGIC_1 &&
            static_cast<unsigned char>(header[2]) == ZIP_MAGIC_2 &&
            static_cast<unsigned char>(header[3]) == ZIP_MAGIC_3);
}

/**
 * @brief Check if downloaded content is an HTML error page
 *
 * @param filePath Path to the file to check
 * @return true if file appears to be HTML, false otherwise
 */
static bool isHtmlErrorPage(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray header = file.read(512);
    file.close();

    return (header.contains("<html") ||
            header.contains("<!doctype") ||
            header.contains("class=\"a-box"));
}

bool ModelDownloaderWorker::downloadGoogleDrive(const QString& fileId,
                                                const QString& destPath)
{
    if (fileId.isEmpty()) {
        return false;
    }

    /*
     * Use a static NAM to maintain session cookies across requests.
     * This prevents Google Drive from re-requesting confirmation.
     */
    static QNetworkAccessManager networkManager;
    static bool initialized = false;

    if (!initialized) {
        networkManager.setCookieJar(new QNetworkCookieJar(&networkManager));
        initialized = true;
    }

    QString downloadUrl = QString("https://drive.google.com/uc?export=download&id=%1").arg(fileId);

    emit progress(tr("Connecting to Google Drive..."));

    /* Initial request */
    QNetworkRequest request{QUrl(downloadUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "TStar-ModelDownloader");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = networkManager.get(request);

    /* Wait for initial response */
    QEventLoop eventLoop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;

    connect(&timeoutTimer, &QTimer::timeout, [&]() {
        timedOut = true;
        eventLoop.quit();
    });
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    timeoutTimer.start(GDRIVE_INITIAL_TIMEOUT_MS);
    eventLoop.exec();
    timeoutTimer.stop();

    if (timedOut || reply->error() != QNetworkReply::NoError) {
        qWarning() << "Google Drive initial request failed or timed out";
        reply->deleteLater();
        return downloadHttp(GITHUB_MIRROR_URL, destPath);
    }

    /* Check if we received an HTML interstitial page */
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader)
                              .toString().toLower();
    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    if (contentType.contains("text/html")) {
        /* Parse the virus scan confirmation form */
        QString formAction;
        QMap<QString, QString> formParams;

        if (!parseGDriveInterstitial(responseData, formAction, formParams)) {
            qWarning() << "Failed to parse Google Drive confirmation form";
            emit progress(tr("Google Drive form parsing failed, trying GitHub mirror..."));
            return downloadHttp(GITHUB_MIRROR_URL, destPath);
        }

        if (formAction.isEmpty()) {
            qWarning() << "Google Drive form action is empty";
            return downloadHttp(GITHUB_MIRROR_URL, destPath);
        }

        emit progress(tr("Google Drive interstitial detected - confirming..."));
        emit progressValue(-1);

        /* Build confirmation request */
        QUrl confirmUrl(formAction);
        QUrlQuery query;
        for (auto it = formParams.begin(); it != formParams.end(); ++it) {
            query.addQueryItem(it.key(), it.value());
        }
        confirmUrl.setQuery(query);

        QNetworkRequest confirmRequest(confirmUrl);
        confirmRequest.setHeader(QNetworkRequest::UserAgentHeader, "TStar-ModelDownloader");
        confirmRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                    QNetworkRequest::NoLessSafeRedirectPolicy);

        reply = networkManager.get(confirmRequest);

        /* Wait for confirmation response */
        QEventLoop confirmLoop;
        QTimer confirmTimer;
        confirmTimer.setSingleShot(true);
        bool confirmTimedOut = false;

        connect(&confirmTimer, &QTimer::timeout, [&]() {
            confirmTimedOut = true;
            confirmLoop.quit();
        });
        connect(reply, &QNetworkReply::finished, &confirmLoop, &QEventLoop::quit);

        confirmTimer.start(GDRIVE_CONFIRM_TIMEOUT_MS);
        confirmLoop.exec();
        confirmTimer.stop();

        if (confirmTimedOut || reply->error() != QNetworkReply::NoError) {
            qWarning() << "Google Drive confirmation failed or timed out";
            emit progress(tr("Google Drive confirmation failed, trying GitHub mirror..."));
            reply->deleteLater();
            return downloadHttp(GITHUB_MIRROR_URL, destPath);
        }
    }

    /* Stream file content to disk */
    QString partialPath = destPath + ".part";
    QFile outputFile(partialPath);

    if (!outputFile.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot create file:" << partialPath;
        reply->deleteLater();
        return false;
    }

    qint64 totalBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    qint64 receivedBytes = 0;

    /* Read and write in chunks */
    QByteArray chunk;
    while (!(chunk = reply->read(1024 * 1024)).isEmpty()) {
        if (m_cancel) {
            outputFile.close();
            QFile::remove(partialPath);
            reply->deleteLater();
            return false;
        }

        outputFile.write(chunk);
        receivedBytes += chunk.size();

        /* Report progress */
        if (totalBytes > 0) {
            int percentage = qBound(0,
                static_cast<int>((double(receivedBytes) / double(totalBytes)) * 100.0),
                100);
            emit progress(tr("Downloading... %1%").arg(percentage));
            emit progressValue(percentage);
        } else {
            double megabytes = receivedBytes / (1024.0 * 1024.0);
            emit progress(tr("Downloading... %1 MB").arg(megabytes, 0, 'f', 1));
            emit progressValue(-1);
        }
    }

    outputFile.close();
    reply->deleteLater();

    /* Validate the downloaded file */
    if (!isValidZipFile(partialPath)) {
        if (isHtmlErrorPage(partialPath)) {
            qWarning() << "Downloaded file is HTML, not ZIP. Falling back to GitHub.";
            QFile::remove(partialPath);
            return downloadHttp(GITHUB_MIRROR_URL, destPath);
        }
    }

    /* Atomic rename from partial to final path */
    QFile::remove(destPath);
    if (!QFile::rename(partialPath, destPath)) {
        QFile::remove(partialPath);
        return false;
    }

    emit progress(tr("Download complete."));
    return true;
}

bool ModelDownloaderWorker::downloadHttp(const QString& url, const QString& destPath)
{
    QNetworkAccessManager networkManager;

    emit progress(tr("Connecting to GitHub mirror..."));

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "TStar-ModelDownloader");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = networkManager.get(request);

    QString partialPath = destPath + ".part";
    QFile outputFile(partialPath);
    bool fileOpened = false;

    qint64 totalBytes = 0;
    qint64 receivedBytes = 0;

    /* Open file when metadata arrives */
    connect(reply, &QNetworkReply::metaDataChanged, [&]() {
        if (!fileOpened && outputFile.open(QIODevice::WriteOnly)) {
            fileOpened = true;
            totalBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            if (totalBytes > 0) {
                emit progress(tr("Downloading... 0%"));
            }
        }
    });

    /* Stream data to file */
    connect(reply, &QNetworkReply::readyRead, [&]() {
        if (!fileOpened) {
            return;
        }

        QByteArray chunk = reply->read(1024 * 1024);
        outputFile.write(chunk);
        receivedBytes += chunk.size();

        if (totalBytes > 0) {
            int percentage = qBound(0,
                static_cast<int>((double(receivedBytes) / double(totalBytes)) * 100.0),
                100);
            emit progress(tr("Downloading... %1%").arg(percentage));
            emit progressValue(percentage);
        } else {
            double megabytes = receivedBytes / (1024.0 * 1024.0);
            emit progress(tr("Downloading... %1 MB").arg(megabytes, 0, 'f', 1));
            emit progressValue(-1);
        }
    });

    /* Wait for completion */
    QEventLoop eventLoop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;

    connect(&timeoutTimer, &QTimer::timeout, [&]() {
        timedOut = true;
        eventLoop.quit();
    });
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    timeoutTimer.start(DOWNLOAD_TIMEOUT_MS);
    eventLoop.exec();
    timeoutTimer.stop();

    if (fileOpened) {
        outputFile.close();
    }

    if (timedOut || reply->error() != QNetworkReply::NoError) {
        emit progress(tr("GitHub mirror download failed or timed out."));
        qWarning() << "GitHub download failed:" << reply->errorString();
        QFile::remove(partialPath);
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();

    /* Validate ZIP file */
    if (!isValidZipFile(partialPath)) {
        qWarning() << "GitHub file is not a valid ZIP";
        QFile::remove(partialPath);
        return false;
    }

    /* Atomic rename */
    QFile::remove(destPath);
    if (!QFile::rename(partialPath, destPath)) {
        QFile::remove(partialPath);
        return false;
    }

    emit progress(tr("Download complete."));
    return true;
}

/* ----------------------------------------------------------------------------
 * Extraction Methods
 * ---------------------------------------------------------------------------- */

bool ModelDownloaderWorker::extractZip(const QString& zipPath, const QString& destDir)
{
    emit progress(tr("Extracting models..."));
    emit progressValue(-1);

    /* Create temporary directory for extraction */
    QString tempDir = destDir + "_extract_temp";
    QDir().mkpath(tempDir);

    QProcess extractProcess;
    bool success = false;

#ifdef Q_OS_WIN
    /*
     * Windows extraction strategy:
     * 1. Try 'tar' (available on Windows 10+, more reliable)
     * 2. Fall back to PowerShell Expand-Archive
     */
    extractProcess.setProgram("tar");
    extractProcess.setArguments({"-xf", zipPath, "-C", tempDir});
    extractProcess.start();

    if (!extractProcess.waitForFinished(EXTRACTION_TIMEOUT_MS)) {
        emit progress(tr("Extraction timed out."));
        QDir(tempDir).removeRecursively();
        return false;
    }

    if (extractProcess.exitCode() == 0) {
        success = true;
    } else {
        /* Fallback to PowerShell */
        qWarning() << "tar failed, falling back to PowerShell Expand-Archive";

        QString psCommand = QString(
            "$ProgressPreference='SilentlyContinue'; "
            "Expand-Archive -Path \"%1\" -DestinationPath \"%2\" -Force"
        ).arg(zipPath, tempDir);

        extractProcess.start("powershell",
            {"-NoProfile", "-NoExit", "-Command", psCommand});

        if (!extractProcess.waitForFinished(EXTRACTION_TIMEOUT_MS)) {
            emit progress(tr("Extraction timed out."));
            QDir(tempDir).removeRecursively();
            return false;
        }

        success = (extractProcess.exitCode() == 0);
    }

#elif defined(Q_OS_MAC)
    /*
     * macOS extraction strategy:
     * 1. Try 'bsdtar' (system default, natively supports ZIP)
     * 2. Fall back to 'unzip'
     */
    extractProcess.setProgram("tar");
    extractProcess.setArguments({"-xf", zipPath, "-C", tempDir});
    extractProcess.start();

    if (!extractProcess.waitForFinished(EXTRACTION_TIMEOUT_MS)) {
        emit progress(tr("Extraction timed out."));
        QDir(tempDir).removeRecursively();
        return false;
    }

    if (extractProcess.exitCode() == 0) {
        success = true;
    } else {
        /* Fallback to unzip */
        qWarning() << "tar failed on macOS, falling back to unzip";

        extractProcess.start("unzip", {"-o", zipPath, "-d", tempDir});

        if (!extractProcess.waitForFinished(EXTRACTION_TIMEOUT_MS)) {
            emit progress(tr("Extraction timed out."));
            QDir(tempDir).removeRecursively();
            return false;
        }

        success = (extractProcess.exitCode() == 0);
    }

#else
    /* Linux: Use 'unzip' as the standard tool */
    extractProcess.setProgram("unzip");
    extractProcess.setArguments({"-o", zipPath, "-d", tempDir});
    extractProcess.start();

    if (!extractProcess.waitForFinished(EXTRACTION_TIMEOUT_MS)) {
        emit progress(tr("Extraction timed out."));
        QDir(tempDir).removeRecursively();
        return false;
    }

    success = (extractProcess.exitCode() == 0);
#endif

    if (!success) {
        QString errorMsg = QString::fromUtf8(extractProcess.readAllStandardError()).trimmed();
        if (errorMsg.isEmpty()) {
            errorMsg = "Unknown extraction error";
        }

        emit progress(tr("Extraction failed: %1").arg(errorMsg));
        qWarning() << "Extraction error:" << errorMsg;
        QDir(tempDir).removeRecursively();
        return false;
    }

    /* Move extracted files to destination */
    emit progress(tr("Finalizing installation..."));

    QDir temp(tempDir);
    QStringList entries = temp.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& entry : entries) {
        QString srcPath = temp.filePath(entry);
        QString dstPath = QDir(destDir).filePath(entry);

        /* Remove existing file/directory at destination */
        QFile::remove(dstPath);
        QDir(dstPath).removeRecursively();

        if (!QFile::rename(srcPath, dstPath)) {
            qWarning() << "Failed to move" << srcPath << "to" << dstPath;
        }
    }

    QDir(tempDir).removeRecursively();

    /*
     * Handle ZIPs with a single top-level folder.
     * Move contents up if the folder contains model files.
     */
    QDir dest(destDir);
    QStringList topEntries = dest.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (topEntries.size() == 1) {
        QDir subDir(dest.filePath(topEntries.first()));

        /* Check if this directory contains model files */
        bool hasModels = false;
        for (const QString& file : subDir.entryList(QDir::Files)) {
            if (file.endsWith(".pth") || file.endsWith(".onnx")) {
                hasModels = true;
                break;
            }
        }

        if (hasModels) {
            /* Move all contents from subdirectory to destination */
            for (const QString& entry : subDir.entryList(
                     QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {

                QString srcPath = subDir.filePath(entry);
                QString dstPath = dest.filePath(entry);

                QFile::remove(dstPath);

                if (!QFile::rename(srcPath, dstPath)) {
                    qWarning() << "Failed to move" << srcPath << "to" << dstPath;
                }
            }

            subDir.removeRecursively();
        }
    }

    emit progress(tr("Extraction complete."));
    emit progressValue(100);

    return true;
}

/* ----------------------------------------------------------------------------
 * Main Download Workflow
 * ---------------------------------------------------------------------------- */

void ModelDownloaderWorker::run()
{
    m_cancel = false;
    emit progressValue(-1);

    QString modelsDir = ModelDownloader::cosmicClarityRoot();
    QDir().mkpath(modelsDir);

    QString tempZipPath = QDir::tempPath() + "/tstar_models_latest.zip";

    bool downloadSuccess = false;
    QString usedSource;

    /* Attempt 1: Primary Google Drive */
    QString primaryFileId = extractDriveFileId(PRIMARY_GDRIVE_URL);

    if (!primaryFileId.isEmpty() && !m_cancel) {
        emit progress(tr("Downloading from primary (Google Drive)..."));
        downloadSuccess = downloadGoogleDrive(primaryFileId, tempZipPath);

        if (downloadSuccess) {
            usedSource = "google_drive_primary";
        } else {
            QFile::remove(tempZipPath);
        }
    }

    /* Attempt 2: Backup Google Drive */
    if (!downloadSuccess && !m_cancel) {
        QString backupFileId = extractDriveFileId(BACKUP_GDRIVE_URL);

        if (!backupFileId.isEmpty()) {
            emit progress(tr("Primary failed. Trying backup (Google Drive)..."));
            downloadSuccess = downloadGoogleDrive(backupFileId, tempZipPath);

            if (downloadSuccess) {
                usedSource = "google_drive_backup";
            } else {
                QFile::remove(tempZipPath);
            }
        }
    }

    /* Attempt 3: GitHub HTTP mirror */
    if (!downloadSuccess && !m_cancel) {
        emit progress(tr("Google Drive failed. Trying GitHub mirror..."));
        downloadSuccess = downloadHttp(GITHUB_MIRROR_URL, tempZipPath);

        if (downloadSuccess) {
            usedSource = "github_http";
        } else {
            QFile::remove(tempZipPath);
        }
    }

    /* Handle cancellation */
    if (m_cancel) {
        QFile::remove(tempZipPath);
        emit finished(false, tr("Download cancelled."));
        return;
    }

    /* Handle download failure */
    if (!downloadSuccess) {
        QFile::remove(tempZipPath);
        emit finished(false,
            tr("All download sources failed. Please check your internet connection."));
        return;
    }

    /* Clear existing models before extraction */
    emit progress(tr("Installing models..."));

    QDir modelsDirectory(modelsDir);
    for (const QString& entry : modelsDirectory.entryList(
             QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {

        QString path = modelsDirectory.filePath(entry);
        QFileInfo fileInfo(path);

        if (fileInfo.isDir()) {
            QDir(path).removeRecursively();
        } else {
            QFile::remove(path);
        }
    }

    /* Extract the downloaded ZIP */
    if (!extractZip(tempZipPath, modelsDir)) {
        QFile::remove(tempZipPath);
        emit finished(false, tr("Failed to extract models ZIP."));
        return;
    }

    /* Write installation manifest */
    QJsonObject manifest;
    manifest["source"] = usedSource;
    manifest["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile manifestFile(modelsDir + "/manifest.json");
    if (manifestFile.open(QIODevice::WriteOnly)) {
        manifestFile.write(QJsonDocument(manifest).toJson());
        manifestFile.close();
    }

    /* Clean up temporary file */
    QFile::remove(tempZipPath);
    emit progressValue(100);

    /* Verify installation */
    if (ModelDownloader::modelsInstalled()) {
        emit finished(true, tr("Models downloaded and installed successfully."));
    } else {
        /* Provide detailed error information */
        QDir dest(ModelDownloader::cosmicClarityRoot());
        QStringList files = dest.entryList(QDir::Files);
        QString fileList = files.isEmpty() ? "No files found" : files.join(", ");

        qWarning() << "Model verification failed. Expected" << SENTINEL_MODEL_FILE
                   << "but found:" << fileList;

        emit finished(false,
            tr("Models extraction completed but verification failed - "
               "expected file '%1' not found. Files extracted: %2")
               .arg(SENTINEL_MODEL_FILE, fileList));
    }
}

/* ============================================================================
 * ModelDownloader Implementation
 * ============================================================================ */

ModelDownloader::ModelDownloader(QObject* parent)
    : QObject(parent)
{
}

ModelDownloader::~ModelDownloader()
{
    /* Ensure worker thread is properly terminated */
    if (m_thread) {
        m_thread->quit();

        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

/* ----------------------------------------------------------------------------
 * Static Utility Methods
 * ---------------------------------------------------------------------------- */

QString ModelDownloader::modelsRoot()
{
    QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);

    QString path = dataDir + "/TStar/models";
    QDir().mkpath(path);

    return path;
}

QString ModelDownloader::cosmicClarityRoot()
{
    QString path = modelsRoot() + "/CosmicClarity";
    QDir().mkpath(path);

    return path;
}

bool ModelDownloader::modelsInstalled()
{
    QString sentinelPath = cosmicClarityRoot() + "/" + SENTINEL_MODEL_FILE;
    return QFile::exists(sentinelPath);
}

/* ----------------------------------------------------------------------------
 * Download Control
 * ---------------------------------------------------------------------------- */

void ModelDownloader::startDownload()
{
    /* Prevent multiple concurrent downloads */
    if (m_thread) {
        return;
    }

    /* Create worker and thread */
    m_thread = new QThread(this);
    m_worker = new ModelDownloaderWorker();
    m_worker->moveToThread(m_thread);

    /* Connect thread lifecycle */
    connect(m_thread, &QThread::started,
            m_worker, &ModelDownloaderWorker::run);

    /* Forward worker signals */
    connect(m_worker, &ModelDownloaderWorker::progress,
            this, &ModelDownloader::progress);

    connect(m_worker, &ModelDownloaderWorker::progressValue,
            this, &ModelDownloader::progressValue);

    connect(m_worker, &ModelDownloaderWorker::finished,
            this, [this](bool ok, const QString& msg) {
                emit finished(ok, msg);
                m_thread->quit();
            });

    /* Clean up when thread finishes */
    connect(m_thread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    connect(m_thread, &QThread::finished,
            m_thread, &QObject::deleteLater);

    connect(m_thread, &QThread::finished, this, [this]() {
        m_thread = nullptr;
        m_worker = nullptr;
    });

    /* Start the download */
    m_thread->start();
}

void ModelDownloader::cancel()
{
    if (m_worker) {
        m_worker->cancel();
    }
}