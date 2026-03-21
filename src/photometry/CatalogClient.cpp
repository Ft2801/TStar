#include "CatalogClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QDebug>
#include <QTimer>

// VizieR mirror servers (fallback chain for robustness)
static const QStringList VIZIER_MIRRORS = {
    "https://vizier.u-strasbg.fr/viz-bin/votable",     // Primary/Reliable (France)
    "https://vizier.cds.unistra.fr/viz-bin/votable",   // Main CDS
    "https://vizier.nao.ac.jp/viz-bin/votable",        // Mirror (Japan)
    "https://vizier.iucaa.in/viz-bin/votable",         // Mirror (India)
    "http://vizier.cfa.harvard.edu/viz-bin/votable"    // Mirror (USA, might require http)
};

static double gaiaBpRpToBV(double bprp) {
    // Approximation from Gaia BP-RP to Johnson B-V for main-sequence stars.
    return 0.3930 + 0.4750 * bprp - 0.0548 * bprp * bprp;
}

CatalogClient::CatalogClient(QObject* parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
    m_currentMirrorIndex = 0;
}

void CatalogClient::queryAPASS(double ra, double dec, double radiusDeg) {
    m_lastQueryRa = ra;
    m_lastQueryDec = dec;
    m_lastQueryRadius = radiusDeg;
    m_lastQueryType = "APASS";
    m_currentMirrorIndex = 0;  // Reset on fresh query
    sendAPASS();
}

void CatalogClient::sendAPASS() {
    if (m_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for APASS."));
        return;
    }
    
    QString baseUrl = VIZIER_MIRRORS[m_currentMirrorIndex];
    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("-source", "II/336/apass9"); 
    query.addQueryItem("-c", QString("%1 %2").arg(m_lastQueryRa).arg(m_lastQueryDec));
    query.addQueryItem("-c.rm", QString::number(m_lastQueryRadius * 60.0));
    query.addQueryItem("-out", "RAJ2000,DEJ2000,Bmag,Vmag");
    query.addQueryItem("-out.max", "2000");
    url.setQuery(query);
    
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    QNetworkReply* reply = m_manager->get(req);
    
    // Timeout mechanism - 15 seconds per mirror
    QTimer::singleShot(15000, reply, [this, reply]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR APASS timeout on mirror" << m_currentMirrorIndex;
            reply->abort();
        }
    });
    
    // Only use finished signal — errorOccurred also triggers finished
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::queryGaiaDR3(double ra, double dec, double radiusDeg) {
    m_lastQueryRa = ra;
    m_lastQueryDec = dec;
    m_lastQueryRadius = radiusDeg;
    m_lastQueryType = "GAIA";
    m_currentMirrorIndex = 0;  // Reset on fresh query
    sendGaia();
}

void CatalogClient::sendGaia() {
    if (m_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for Gaia DR3."));
        return;
    }
    
    qInfo() << "[CatalogClient] Querying Gaia DR3 on mirror" << m_currentMirrorIndex
               << VIZIER_MIRRORS[m_currentMirrorIndex];
    
    QString baseUrl = VIZIER_MIRRORS[m_currentMirrorIndex];
    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("-source", "I/355/gaiadr3"); 
    query.addQueryItem("-c", QString("%1 %2").arg(m_lastQueryRa).arg(m_lastQueryDec));
    query.addQueryItem("-c.rm", QString::number(m_lastQueryRadius * 60.0)); // arcmin
    query.addQueryItem("-out", "RA_ICRS,DE_ICRS,Gmag,BPmag,RPmag,teff_gspphot");
    query.addQueryItem("-out.max", "3000");
    query.addQueryItem("-sort", "Gmag");
    query.addQueryItem("Gmag", "2..17"); // Explicit range for better filtering
    
    url.setQuery(query);
    
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    QNetworkReply* reply = m_manager->get(req);
    
    // Timeout: 15 seconds per mirror attempt
    QTimer::singleShot(15000, reply, [this, reply]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR Gaia timeout on mirror" << m_currentMirrorIndex;
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
    if (reply->error()) {
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
    m_currentMirrorIndex++;
    
    qWarning() << "[CatalogClient] Retry mirror" << m_currentMirrorIndex
               << "of" << VIZIER_MIRRORS.size();
    
    // Call sendGaia/sendAPASS directly (NOT queryGaiaDR3/queryAPASS which reset the index)
    if (m_lastQueryType == "GAIA") {
        sendGaia();
    } else if (m_lastQueryType == "APASS") {
        sendAPASS();
    }
}
