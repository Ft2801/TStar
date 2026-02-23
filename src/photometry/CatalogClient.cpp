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
    "http://vizier.u-strasbg.fr/viz-bin/votable",      // Primary (France)
    "http://vizier.cds.unistra.fr/viz-bin/votable",    // Mirror (France, alt. domain)
    "http://vizier.iucaa.in/viz-bin/votable",          // Mirror (India)
    "http://vizier.nao.ac.jp/viz-bin/votable",         // Mirror (Japan)
};

CatalogClient::CatalogClient(QObject* parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
    m_currentMirrorIndex = 0;
}

void CatalogClient::queryAPASS(double ra, double dec, double radiusDeg) {
    m_lastQueryRa = ra;
    m_lastQueryDec = dec;
    m_lastQueryRadius = radiusDeg;
    m_lastQueryType = "APASS";
    if (m_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        m_currentMirrorIndex = 0;  // Reset to primary mirror on fresh query
    }
    
    // VizieR Cone Search for APASS (II/336)
    // VizieR Cone Search (II/336) uses arcminutes for radius (-c.rm)
    
    QString baseUrl = VIZIER_MIRRORS[m_currentMirrorIndex];  // Use current mirror
    QUrl url(baseUrl + "/viz-bin/votable");
    QUrlQuery query;
    query.addQueryItem("-source", "II/336/apass9"); 
    query.addQueryItem("-c", QString("%1 %2").arg(ra).arg(dec));
    query.addQueryItem("-c.rm", QString::number(radiusDeg * 60.0));
    query.addQueryItem("-out", "RAJ2000,DEJ2000,Bmag,Vmag");
    query.addQueryItem("-out.max", "2000");
    url.setQuery(query);
    
    QNetworkRequest req(url);
    QNetworkReply* reply = m_manager->get(req);
    
    // Timeout mechanism - 30 seconds per attempt
    QTimer::singleShot(30000, reply, [this, reply]() {
        if (reply->isRunning()) {
            reply->abort();
            retryWithNextMirror();
        }
    });
    
    // Error handlers - network failures trigger retry
    connect(reply, &QNetworkReply::errorOccurred,
            this, &CatalogClient::retryWithNextMirror);
    
    // Success handler
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::queryGaiaDR3(double ra, double dec, double radiusDeg) {
    // VizieR Cone Search for Gaia DR3 (I/355/gaiadr3)
    // Save query parameters for retry on next mirror
    m_lastQueryRa = ra;
    m_lastQueryDec = dec;
    m_lastQueryRadius = radiusDeg;
    m_lastQueryType = "GAIA";
    m_currentMirrorIndex = 0;
    
    QString baseUrl = VIZIER_MIRRORS[0];
    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("-source", "I/355/gaiadr3"); 
    query.addQueryItem("-c", QString("%1 %2").arg(ra).arg(dec));
    query.addQueryItem("-c.rm", QString::number(radiusDeg * 60.0)); // arcmin
    // Request basic astrometry + photometry + parameters
    // Note: 'Teff' is the standardized column name in VizieR for Effective Temperature or teff_gspphot
    query.addQueryItem("-out", "RA_ICRS,DE_ICRS,Gmag,BPmag,RPmag,Teff");
    // 3000 stars: better coverage for both plate solving and PCC,
    // especially in sparse fields (galactic poles) where mag 16 is insufficient.
    query.addQueryItem("-out.max", "3000");
    query.addQueryItem("-out.add", "_r"); // sort by distance from center
    // Gmag < 17: extra magnitude depth vs <16 for sparse high-galactic-latitude fields.
    query.addQueryItem("Gmag", "<17");
    
    url.setQuery(query);
    
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    QNetworkReply* reply = m_manager->get(req);
    
    // Timeout: VizieR queries should respond within 30 seconds
    QTimer::singleShot(30000, reply, [this, reply]() {
        if (reply->isRunning()) {
            reply->abort();
            retryWithNextMirror();
        }
    });
    
    connect(reply, &QNetworkReply::errorOccurred,
            this, &CatalogClient::retryWithNextMirror);
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
                if (fieldName == "RAJ2000" || fieldID == "RAJ2000" || fieldName == "RA_ICRS") idxRA = currentFieldIndex;
                else if (fieldName == "DEJ2000" || fieldID == "DEJ2000" || fieldName == "DE_ICRS") idxDec = currentFieldIndex;
                else if (fieldName == "Bmag" || fieldID == "Bmag") idxB = currentFieldIndex;
                else if (fieldName == "Vmag" || fieldID == "Vmag") idxV = currentFieldIndex;
                else if (fieldName == "Teff" || fieldID == "Teff" || fieldName == "teff_gspphot") idxTeff = currentFieldIndex;
                else if (fieldName == "Gmag" || fieldID == "Gmag") idxG = currentFieldIndex;
                else if (fieldName == "BPmag" || fieldID == "BPmag") idxBP = currentFieldIndex;
                else if (fieldName == "RPmag" || fieldID == "RPmag") idxRP = currentFieldIndex;
                
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
                    s.B_V = b - v; // May be inaccurate if using BP/G proxy, but PCC uses Teff if available.
                    
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
    
    if (m_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(
            tr("All VizieR mirrors failed. Network connectivity issue?\n"
               "Tried: %1").arg(VIZIER_MIRRORS.join(", ")));
        return;
    }
    
    qWarning() << "VizieR mirror retry:" << m_currentMirrorIndex 
               << "switching to" << VIZIER_MIRRORS[m_currentMirrorIndex];
    
    if (m_lastQueryType == "GAIA") {
        queryGaiaDR3(m_lastQueryRa, m_lastQueryDec, m_lastQueryRadius);
    } else if (m_lastQueryType == "APASS") {
        queryAPASS(m_lastQueryRa, m_lastQueryDec, m_lastQueryRadius);
    }
}
