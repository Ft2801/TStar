#include "HiPSClient.h"
#include "../io/FitsLoader.h"
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <cmath>

const QString HiPSClient::SUR_PANSTARRS_DR1_COLOR = "CDS/P/PanSTARRS/DR1/color-z-zg-g";
const QString HiPSClient::SUR_DSS2_RED            = "CDS/P/DSS2/red";
const QString HiPSClient::SUR_UNWISE_COLOR        = "CDS/P/unWISE/color-W2-W1W2-W1";

qint64 HiPSClient::s_maxCacheSize = 2LL * 1024 * 1024 * 1024; // 2 GB

HiPSClient::HiPSClient(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/hips_cache";
    QDir().mkpath(m_cacheDir);
}

void HiPSClient::fetchFITS(const QString& hips, double ra, double dec,
                            double fov, int width, int height)
{
    QString cachePath = getCacheFilePath(hips, ra, dec, fov, width, height);

    // Try cache first
    if (QFile::exists(cachePath)) {
        ImageBuffer refImg;
        QString err;
        if (FitsLoader::load(cachePath, refImg, &err)) {
            // Touch access time for LRU ordering
            QFile f(cachePath);
            if (f.open(QIODevice::ReadWrite)) {
                f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
                f.close();
            }
            emit imageReady(refImg);
            return;
        }
        // Cache file is corrupt — remove and re-download
        QFile::remove(cachePath);
    }

    // Abort any in-flight request
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    // Build CDS Aladin hips2fits URL
    QUrl url("https://alasky.cds.unistra.fr/hips-image-services/hips2fits");
    QUrlQuery query;
    query.addQueryItem("hips",       hips);
    query.addQueryItem("width",      QString::number(width));
    query.addQueryItem("height",     QString::number(height));
    query.addQueryItem("fov",        QString::number(fov, 'f', 6));
    query.addQueryItem("projection", "TAN");
    query.addQueryItem("coordsys",   "icrs");
    query.addQueryItem("ra",         QString::number(ra, 'f', 6));
    query.addQueryItem("dec",        QString::number(dec, 'f', 6));
    query.addQueryItem("format",     "fits");
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-HiPSClient/1.0");
    req.setTransferTimeout(60000); // 60s timeout
    req.setAttribute(QNetworkRequest::Attribute(QNetworkRequest::User + 1), cachePath);

    m_reply = m_manager->get(req);
    connect(m_reply, &QNetworkReply::finished,         this, &HiPSClient::onReplyFinished);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &HiPSClient::downloadProgress);
}

void HiPSClient::onReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply == m_reply) m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(tr("Network error: %1").arg(reply->errorString()));
        return;
    }

    QByteArray data = reply->readAll();
    if (data.isEmpty()) {
        emit errorOccurred(tr("Received empty response from HiPS server."));
        return;
    }

    // Detect HTML error pages or server-side error strings
    if (data.startsWith("Error") || data.startsWith("<!DOCTYPE") || data.startsWith("<html")) {
        emit errorOccurred(tr("HiPS service error: %1").arg(QString::fromUtf8(data).left(300)));
        return;
    }

    // Save to cache
    QString cachePath = reply->request()
                            .attribute(QNetworkRequest::Attribute(QNetworkRequest::User + 1))
                            .toString();
    if (!cachePath.isEmpty()) {
        QFile file(cachePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        }
    }

    // Load the FITS directly from the cache file we just wrote
    // (avoids creating a redundant temp file)
    if (!cachePath.isEmpty() && QFile::exists(cachePath)) {
        ImageBuffer refImg;
        QString err;
        if (FitsLoader::load(cachePath, refImg, &err)) {
            cleanupCache();
            emit imageReady(refImg);
        } else {
            QFile::remove(cachePath);
            emit errorOccurred(tr("Failed to parse downloaded FITS: %1").arg(err));
        }
    } else {
        emit errorOccurred(tr("Failed to save reference image to cache."));
    }
}

QString HiPSClient::getCacheFilePath(const QString& hips, double ra, double dec,
                                      double fov, int width, int height)
{
    // Round coordinates to a precision proportional to the FoV.
    // This allows nearby fields (within ~10% of the FoV) to share cached tiles.
    double roundingStep = std::max(0.001, fov * 0.1);
    double roundedRA  = std::round(ra  / roundingStep) * roundingStep;
    double roundedDec = std::round(dec / roundingStep) * roundingStep;
    double roundedFoV = std::round(fov * 100.0) / 100.0; // round to 0.01 deg

    QString key = QString("%1_%2_%3_%4_%5_%6")
                      .arg(hips)
                      .arg(roundedRA,  0, 'f', 6)
                      .arg(roundedDec, 0, 'f', 6)
                      .arg(roundedFoV, 0, 'f', 6)
                      .arg(width)
                      .arg(height);

    QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex();
    return m_cacheDir + "/" + hash + ".fits";
}

void HiPSClient::cleanupCache() {
    QDir dir(m_cacheDir);
    QFileInfoList list = dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);

    qint64 totalSize = 0;
    for (const QFileInfo& fi : list) totalSize += fi.size();

    // LRU eviction: remove oldest files until under limit
    if (totalSize > s_maxCacheSize) {
        for (const QFileInfo& fi : list) {
            if (totalSize <= s_maxCacheSize) break;
            qint64 sz = fi.size();
            if (QFile::remove(fi.absoluteFilePath()))
                totalSize -= sz;
        }
    }
}

void HiPSClient::clearCache() {
    QDir dir(m_cacheDir);
    dir.removeRecursively();
    dir.mkpath(".");
}

qint64 HiPSClient::getCacheSize() {
    // Use the same cache dir calculation as the constructor
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/hips_cache";
    QDir dir(cacheDir);
    qint64 total = 0;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files))
        total += fi.size();
    return total;
}

void HiPSClient::setMaxCacheSize(qint64 bytes) {
    s_maxCacheSize = bytes;
}
