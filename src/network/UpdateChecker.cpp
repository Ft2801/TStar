#include "UpdateChecker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

void UpdateChecker::checkForUpdates(const QString& currentVersion) {
    m_currentVersion = currentVersion;
    
    QNetworkRequest request(QUrl("https://api.github.com/repos/Ft2801/TStar/releases/latest"));
    request.setHeader(QNetworkRequest::UserAgentHeader, "TStar-Updater");
    
    m_nam->get(request);
    
    connect(m_nam, &QNetworkAccessManager::finished, this, &UpdateChecker::onReplyFinished, Qt::UniqueConnection);
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    QUrl url = reply->request().url();

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(tr("Network error: %1").arg(reply->errorString()));
        return;
    }

    // 1. Handle GitHub Release API Response
    if (url.toString().contains("api.github.com")) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isObject()) {
            emit errorOccurred(tr("Invalid JSON response from GitHub."));
            return;
        }

        QJsonObject obj = doc.object();
        QString tagName = obj["tag_name"].toString();
        
        // Extract version from tag (e.g., "v1.2.3" -> "1.2.3")
        QString remoteVersion = tagName;
        if (remoteVersion.startsWith("v") || remoteVersion.startsWith("V")) {
            remoteVersion = remoteVersion.mid(1);
        }

        if (isNewer(m_currentVersion, remoteVersion)) {
            m_pendingVersion = remoteVersion;
            
            // Find asset URL (looking for .exe)
            m_pendingDownloadUrl.clear();
            QJsonArray assets = obj["assets"].toArray();
            for (const auto& assetVal : assets) {
                QJsonObject asset = assetVal.toObject();
                QString name = asset["name"].toString();
                if (name.endsWith(".exe", Qt::CaseInsensitive)) {
                    m_pendingDownloadUrl = asset["browser_download_url"].toString();
                    break; 
                }
            }
            
            // If no exe found, fallback to html_url of the release page
            if (m_pendingDownloadUrl.isEmpty()) {
                m_pendingDownloadUrl = obj["html_url"].toString();
            }

            // [NEW] Instead of using obj["body"], fetch changelog.txt from repo
            fetchChangelog();
        } else {
            emit noUpdateAvailable();
        }
    } 
    // 2. Handle Changelog Fetching Response
    else if (url.toString().contains("changelog.txt")) {
        QString changelog = QString::fromUtf8(reply->readAll());
        if (changelog.isEmpty()) {
            changelog = tr("No detailed changelog available.");
        }
        emit updateAvailable(m_pendingVersion, changelog, m_pendingDownloadUrl);
    }
}

void UpdateChecker::fetchChangelog() {
    // We fetch from master branch, or we could try to fetch from the specific tag if known. 
    // Fetching from master is usually fine for latest.
    QNetworkRequest request(QUrl("https://raw.githubusercontent.com/Ft2801/TStar/master/changelog.txt"));
    request.setHeader(QNetworkRequest::UserAgentHeader, "TStar-Updater");
    m_nam->get(request);
}

bool UpdateChecker::isNewer(const QString& current, const QString& remote) {
    QStringList cParts = current.split('.');
    QStringList rParts = remote.split('.');
    
    int count = std::min(cParts.size(), rParts.size());
    
    for (int i = 0; i < count; ++i) {
        int c = cParts[i].toInt();
        int r = rParts[i].toInt();
        if (r > c) return true;
        if (r < c) return false;
    }
    
    // If we're here, common parts are equal.
    // If remote has more parts (e.g. 1.0 vs 1.0.1), it's newer
    return rParts.size() > cParts.size();
}
