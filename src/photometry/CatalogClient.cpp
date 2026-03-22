#include "CatalogClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QDebug>
#include <QTimer>
#include <algorithm>

// VizieR mirror servers (diverse global list)
static const QStringList VIZIER_MIRRORS = {
    "https://vizier.u-strasbg.fr/viz-bin/votable",     // Strasbourg (France) - Primary
    "https://vizier.cds.unistra.fr/viz-bin/votable",    // Strasbourg (Backup Alias)
    "https://vizier.cfa.harvard.edu/viz-bin/votable",  // Harvard (USA)
    "https://vizier.nao.ac.jp/viz-bin/votable",        // Tokyo (Japan)
    "https://vizier.iucaa.in/viz-bin/votable",         // Pune (India)
    "http://vizier.china-vo.org/viz-bin/votable"       // China-VO (China)
};

// Initialize shared mirror index
int CatalogClient::s_currentMirrorIndex = 0;

static double gaiaBpRpToBV(double bprp) {
    // Approximation from Gaia BP-RP to Johnson B-V for main-sequence stars.
    return 0.3930 + 0.4750 * bprp - 0.0548 * bprp * bprp;
}

CatalogClient::CatalogClient(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
}

void CatalogClient::queryAPASS(double ra, double dec, double radiusDeg) {
    m_lastQueryRa = ra; m_lastQueryDec = dec; m_lastQueryRadius = radiusDeg;
    m_lastQueryType = "APASS";
    
    // We don't reset s_currentMirrorIndex here to "persist" success from previous calls
    // But we check bounds just in case
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) s_currentMirrorIndex = 0;
    
    sendAPASS();
}

void CatalogClient::sendAPASS() {
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for APASS."));
        return;
    }
    
    QUrl url(VIZIER_MIRRORS[s_currentMirrorIndex]);
    emit mirrorStatus(tr("Querying APASS on %1...").arg(url.host()));
    QUrlQuery query;
    query.addQueryItem("-source", "II/336/apass9"); 
    query.addQueryItem("-c", QString::number(m_lastQueryRa, 'f', 6) + " " + QString::number(m_lastQueryDec, 'f', 6));
    query.addQueryItem("-c.rm", QString::number(m_lastQueryRadius * 60.0, 'f', 2));
    query.addQueryItem("-out", "RAJ2000,DEJ2000,Bmag,Vmag");
    query.addQueryItem("-out.max", "2000");
    url.setQuery(query);
    
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply* reply = m_manager->get(req);
    
    // Timeout mechanism - 15 seconds per mirror
    QTimer::singleShot(15000, reply, [this, reply]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR APASS timeout on mirror" << s_currentMirrorIndex;
            reply->abort();
        }
    });
    
    // Only use finished signal — errorOccurred also triggers finished
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::queryGaiaDR3(double ra, double dec, double radiusDeg) {
    // CAP search radius to avoid server timeouts on VizieR mirrors (online)
    double cappedRadius = std::min(radiusDeg, 3.0);
    
    m_lastQueryRa = ra; m_lastQueryDec = dec; m_lastQueryRadius = cappedRadius;
    m_lastQueryType = "GAIA";
    
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) s_currentMirrorIndex = 0;
    
    sendGaia();
}

void CatalogClient::sendGaia() {
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for Gaia DR3."));
        return;
    }

    QUrl url(VIZIER_MIRRORS[s_currentMirrorIndex]);
    emit mirrorStatus(tr("Querying Gaia DR3 on %1...").arg(url.host()));
    
    qInfo() << "[CatalogClient] Querying Gaia DR3 on mirror" << s_currentMirrorIndex
               << VIZIER_MIRRORS[s_currentMirrorIndex];
    
    QUrlQuery query;
    query.addQueryItem("-source", "I/355/gaiadr3"); 
    query.addQueryItem("-c", QString::number(m_lastQueryRa, 'f', 6) + " " + QString::number(m_lastQueryDec, 'f', 6));
    query.addQueryItem("-c.rm", QString::number(m_lastQueryRadius * 60.0, 'f', 2)); // arcmin
    query.addQueryItem("-out", "RA_ICRS,DE_ICRS,Gmag,BPmag,RPmag,teff_gspphot");
    const int outMax = (m_lastQueryRadius > 2.0) ? 1800 : 3000;
    query.addQueryItem("-out.max", QString::number(outMax));
    query.addQueryItem("-sort", "Gmag");
    query.addQueryItem("Gmag", "<20"); // Allow fainter stars
    
    url.setQuery(query);
    
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply* reply = m_manager->get(req);
    
    // Adaptive timeout: wider fields need more time on VizieR.
    const int timeoutMs = (m_lastQueryRadius > 2.0) ? 30000 : 20000;
    QTimer::singleShot(timeoutMs, reply, [this, reply]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR Gaia timeout on mirror" << s_currentMirrorIndex;
            reply->abort();
        }
    });
    
    // Only use finished signal — errorOccurred also triggers finished,
    // connecting both causes double-retry → exponential loop
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::onReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        qWarning() << "[CatalogClient] Mirror" << s_currentMirrorIndex << "failed:" << errStr << "(HTTP" << httpCode << ")";
        emit mirrorStatus(tr("Mirror %1 failed (%2, HTTP %3). Retrying...")
                          .arg(s_currentMirrorIndex)
                          .arg(errStr)
                          .arg(httpCode));
        
        retryWithNextMirror();
        return;
    }
    
    QByteArray data = reply->readAll();
    qDebug() << "[CatalogClient] Response sample (200 bytes):" << data.left(200);
    
    // Detect HTML error pages (VizieR returns HTML for errors, not XML)
    QString dataStr = QString::fromUtf8(data);
    if (dataStr.toLower().contains("<html") || 
        dataStr.toLower().contains("<!doctype") ||
        dataStr.contains("Error 500") ||
        dataStr.contains("Bad Request")) {
        qWarning() << "VizieR returned HTML error page, retrying next mirror...";
        retryWithNextMirror();
        return;
    }
    
    std::vector<CatalogStar> stars;
    QXmlStreamReader xml(data);
    
    // Column Mapping
    int idxRA = -1;
    int idxDec = -1;
    int idxB = -1;
    int idxV = -1;
    int idxTeff = -1;
    int idxG = -1;
    int idxBP = -1;
    int idxRP = -1;
    
    int currentFieldIndex = 0;
    
    while(!xml.atEnd()) {
        xml.readNext();
        
        if (xml.isStartElement()) {
            QString name = xml.name().toString();
            
            // 1. Parsing Fields to map indices
            if (name == "FIELD") {
                QString fieldName = xml.attributes().value("name").toString();
                QString fieldID = xml.attributes().value("ID").toString();
                
                // Common aliases
                if (fieldName == "RAJ2000" || fieldID == "RAJ2000" || fieldName == "RA_ICRS" || fieldName.toLower() == "ra") idxRA = currentFieldIndex;
                else if (fieldName == "DEJ2000" || fieldID == "DEJ2000" || fieldName == "DE_ICRS" || fieldName.toLower() == "dec") idxDec = currentFieldIndex;
                else if (fieldName == "Bmag" || fieldID == "Bmag" || fieldName.toLower() == "b") idxB = currentFieldIndex;
                else if (fieldName == "Vmag" || fieldID == "Vmag" || fieldName.toLower() == "v") idxV = currentFieldIndex;
                else if (fieldName == "Teff" || fieldID == "Teff" || fieldName == "teff_gspphot" || fieldName.toLower().contains("teff")) idxTeff = currentFieldIndex;
                else if (fieldName == "Gmag" || fieldID == "Gmag" || fieldName.toLower() == "g") idxG = currentFieldIndex;
                else if (fieldName == "BPmag" || fieldID == "BPmag" || fieldName.toLower() == "bpmag" || fieldName.toLower() == "bp") idxBP = currentFieldIndex;
                else if (fieldName == "RPmag" || fieldID == "RPmag" || fieldName.toLower() == "rpmag" || fieldName.toLower() == "rp") idxRP = currentFieldIndex;
                
                currentFieldIndex++;
            }
            
            // 2. Parsing Data
            else if (name == "TR") {
                double r=0, d=0, b=0, v=0, teff=0;
                double g_mag=0, bp_mag=0;
                [[maybe_unused]] double rp_mag=0;
                
                int colIndex = 0;
                
                // Read TDs for this TR
                while(!(xml.isEndElement() && xml.name().toString() == "TR") && !xml.atEnd()) {
                    if (xml.readNext() == QXmlStreamReader::StartElement && xml.name().toString() == "TD") {
                        QString text = xml.readElementText();
                        
                        if (!text.isEmpty()) {
                            double val = text.toDouble();
                            // Parse based on mapped index
                            if (colIndex == idxRA) r = val;
                            else if (colIndex == idxDec) d = val;
                            else if (colIndex == idxB) b = val;
                            else if (colIndex == idxV) v = val;
                            else if (colIndex == idxTeff) teff = val;
                            else if (colIndex == idxG) g_mag = val;
                            else if (colIndex == idxBP) bp_mag = val;
                            else if (colIndex == idxRP) rp_mag = val;
                        }
                        
                        colIndex++;
                    }
                }
                
                // Create Star Object
                if (idxRA != -1 && idxDec != -1) {
                    CatalogStar s;
                    s.ra = r;
                    s.dec = d;
                    s.teff = teff;
                    
                    // Logic for Mag/Color
                    // Support Gaia DR3 if APASS missing
                    if (idxV == -1 && idxG != -1) v = g_mag; // Use G as proxy for V magnitude for solving brightness checks
                    if (idxB == -1 && idxBP != -1 && idxRP != -1) {
                         // Populate with available BP/RP for debugging if B is missing
                         b = bp_mag; 
                    }
                    
                    s.magB = b;
                    s.magV = v;

                    if (idxB != -1 && idxV != -1 && b > 0.0 && v > 0.0) {
                        s.B_V = b - v;
                        s.bp_rp = 0.0;
                    } else if (idxBP != -1 && idxRP != -1 && bp_mag > 0.0 && rp_mag > 0.0) {
                        const double bprp = bp_mag - rp_mag;
                        s.B_V = gaiaBpRpToBV(bprp);
                        s.bp_rp = bprp;
                    } else if (idxBP != -1 && idxG != -1 && bp_mag > 0.0 && g_mag > 0.0) {
                        s.B_V = bp_mag - g_mag;
                        s.bp_rp = 0.0; // Approximation
                    } else {
                        s.B_V = 0.65; // G2V fallback
                        s.bp_rp = 0.0;
                    }
                    
                    if ((s.magV > 0 || s.teff > 0)) { // Valid star
                        stars.push_back(s);
                    }
                }
            }
        }
    }
    
    if (xml.hasError()) {
        qWarning() << "XML Parse Error:" << xml.errorString() << "- trying next mirror";
        retryWithNextMirror();
        return;
    }
    
    if (stars.empty()) {
        // No stars found - still a valid response, emit empty set
        // Don't retry on empty results (mirror is working, just no data in region)
        qWarning() << "No stars found in region - catalog query succeeded but returned 0 stars";
        emit catalogReady(stars);
    } else {
        emit catalogReady(stars);
    }
}

void CatalogClient::retryWithNextMirror() {
    s_currentMirrorIndex++;

    // For Gaia DR3, if all mirrors failed at a large radius, retry once more
    // from the first mirror with a smaller query radius before giving up.
    if (m_lastQueryType == "GAIA" &&
        s_currentMirrorIndex >= VIZIER_MIRRORS.size() &&
        m_lastQueryRadius > 1.0) {
        const double oldRadius = m_lastQueryRadius;
        m_lastQueryRadius = std::max(1.0, m_lastQueryRadius * 0.6);
        s_currentMirrorIndex = 0;
        emit mirrorStatus(tr("All Gaia mirrors failed at %1 deg; retrying at %2 deg...")
                          .arg(oldRadius, 0, 'f', 2)
                          .arg(m_lastQueryRadius, 0, 'f', 2));
    }

    if (m_lastQueryType == "GAIA") sendGaia();
    else if (m_lastQueryType == "APASS") sendAPASS();
}
