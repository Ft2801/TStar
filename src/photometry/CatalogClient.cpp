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
    QTimer::singleShot(15000, reply, [reply]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR APASS timeout on mirror" << CatalogClient::s_currentMirrorIndex;
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
    QTimer::singleShot(timeoutMs, reply, [reply]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR Gaia timeout on mirror" << CatalogClient::s_currentMirrorIndex;
            reply->abort();
        }
    });
    
    // Only use finished signal — errorOccurred also triggers finished,
    // connecting both causes double-retry → exponential loop
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::queryHyperLeda(double ra, double dec, double radiusDeg) {
    double cappedRadius = std::min(radiusDeg, 3.0);
    
    m_lastQueryRa = ra; m_lastQueryDec = dec; m_lastQueryRadius = cappedRadius;
    m_lastQueryType = "HYPERLEDA";
    
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) s_currentMirrorIndex = 0;
    
    sendHyperLeda();
}

void CatalogClient::sendHyperLeda() {
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for HyperLeda."));
        return;
    }

    QUrl url(VIZIER_MIRRORS[s_currentMirrorIndex]);
    emit mirrorStatus(tr("Querying HyperLeda (PGC) on %1...").arg(url.host()));
    
    qInfo() << "[CatalogClient] Querying HyperLeda on mirror" << s_currentMirrorIndex
               << VIZIER_MIRRORS[s_currentMirrorIndex];
    
    QUrlQuery query;
    query.addQueryItem("-source", "VII/237/pgc"); 
    query.addQueryItem("-c", QString::number(m_lastQueryRa, 'f', 6) + " " + QString::number(m_lastQueryDec, 'f', 6));
    query.addQueryItem("-c.rm", QString::number(m_lastQueryRadius * 60.0, 'f', 2)); // arcmin
    query.addQueryItem("-out", "PGC,RAJ2000,DEJ2000,logD25,logR25,PA");
    const int outMax = (m_lastQueryRadius > 2.0) ? 1000 : 2000;
    query.addQueryItem("-out.max", QString::number(outMax));
    
    url.setQuery(query);
    
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply* reply = m_manager->get(req);
    
    const int timeoutMs = (m_lastQueryRadius > 2.0) ? 30000 : 20000;
    QTimer::singleShot(timeoutMs, reply, [reply]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR HyperLeda timeout on mirror" << CatalogClient::s_currentMirrorIndex;
            reply->abort();
        }
    });
    
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
    std::vector<PGCGalaxy> galaxies;
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
    
    int idxPGC = -1;
    int idxLogD25 = -1;
    int idxLogR25 = -1;
    int idxPA = -1;
    
    int currentFieldIndex = 0;
    
    while(!xml.atEnd()) {
        xml.readNext();
        
        if (xml.isStartElement()) {
            QString name = xml.name().toString();

            // 0. Reset indices for each table found in VOTable
            if (name == "TABLE") {
                currentFieldIndex = 0;
                idxRA = idxDec = idxB = idxV = idxTeff = idxG = idxBP = idxRP = -1;
                idxPGC = idxLogD25 = idxLogR25 = idxPA = -1;
            }
            
            // 1. Parsing Fields to map indices
            else if (name == "FIELD") {
                // Common aliases - using UCDs for maximum robustness
                QString fieldName = xml.attributes().value("name").toString();
                QString fieldID = xml.attributes().value("ID").toString();
                QString fn = fieldName.toLower();
                QString fi = fieldID.toLower();
                QString fu = xml.attributes().value("ucd").toString().toLower();

                if (fn == "raj2000" || fi == "raj2000" || fn == "ra_icrs" || fu.contains("pos.eq.ra")) idxRA = currentFieldIndex;
                else if (fn == "dej2000" || fi == "dej2000" || fn == "de_icrs" || fu.contains("pos.eq.dec")) idxDec = currentFieldIndex;
                else if ((fn == "bmag" || fi == "bmag") || (fu == "phot.mag;em.opt.b" && !fn.contains("bp") && !fi.contains("bp"))) idxB = currentFieldIndex;
                else if ((fn == "vmag" || fi == "vmag") || (fu == "phot.mag;em.opt.v" && !fn.contains("rp") && !fi.contains("rp"))) idxV = currentFieldIndex;
                else if (fn.contains("teff") || fi.contains("teff") || fu.contains("phys.temperature")) idxTeff = currentFieldIndex;
                else if (fn == "gmag" || fi == "gmag" || fu == "phot.mag;em.opt.g") idxG = currentFieldIndex;
                else if ((fn.contains("bpmag") || fi.contains("bpmag") || fn.contains("bp-mag")) && !fn.startsWith("e_")) idxBP = currentFieldIndex;
                else if ((fn.contains("rpmag") || fi.contains("rpmag") || fn.contains("rp-mag")) && !fn.startsWith("e_")) idxRP = currentFieldIndex;
                // If UCD based matching is needed specifically for Gaia columns when names differ:
                else if (idxBP == -1 && (fu.contains("em.opt.b") || fu.contains("phot.mag.b")) && fu.contains("phot.mag")) idxBP = currentFieldIndex;
                else if (idxRP == -1 && (fu.contains("em.opt.r") || fu.contains("phot.mag.r")) && fu.contains("phot.mag")) idxRP = currentFieldIndex;
                else if (fn == "pgc" || fi == "pgc") idxPGC = currentFieldIndex;
                else if (fn == "logd25" || fi == "logd25") idxLogD25 = currentFieldIndex;
                else if (fn == "logr25" || fi == "logr25") idxLogR25 = currentFieldIndex;
                else if (fn == "pa" || fi == "pa") idxPA = currentFieldIndex;
                
                currentFieldIndex++;
            }
            
            // 2. Parsing Data
            else if (name == "TR") {
                double r=std::numeric_limits<double>::quiet_NaN();
                double d=std::numeric_limits<double>::quiet_NaN();
                double b=std::numeric_limits<double>::quiet_NaN();
                double v=std::numeric_limits<double>::quiet_NaN();
                double teff=0;
                double g_mag=std::numeric_limits<double>::quiet_NaN();
                double bp_mag=std::numeric_limits<double>::quiet_NaN();
                double rp_mag=std::numeric_limits<double>::quiet_NaN();
                
                QString pgcStr;
                double logD=std::numeric_limits<double>::quiet_NaN();
                double logR=std::numeric_limits<double>::quiet_NaN();
                double pa=std::numeric_limits<double>::quiet_NaN();
                
                int colIndex = 0;
                
                // Read TDs for this TR
                while(!(xml.isEndElement() && xml.name().toString() == "TR") && !xml.atEnd()) {
                    if (xml.readNext() == QXmlStreamReader::StartElement && xml.name().toString() == "TD") {
                        QString text = xml.readElementText();
                        
                        if (!text.isEmpty()) {
                            if (colIndex == idxPGC) pgcStr = text;
                            else if (colIndex == idxRA) r = text.toDouble();
                            else if (colIndex == idxDec) d = text.toDouble();
                            else if (colIndex == idxB) b = text.toDouble();
                            else if (colIndex == idxV) v = text.toDouble();
                            else if (colIndex == idxTeff) teff = text.toDouble();
                            else if (colIndex == idxG) g_mag = text.toDouble();
                            else if (colIndex == idxBP) bp_mag = text.toDouble();
                            else if (colIndex == idxRP) rp_mag = text.toDouble();
                            else if (colIndex == idxLogD25) logD = text.toDouble();
                            else if (colIndex == idxLogR25) logR = text.toDouble();
                            else if (colIndex == idxPA) pa = text.toDouble();
                        }
                        
                        colIndex++;
                    }
                }
                
                // Create Object
                if (m_lastQueryType == "HYPERLEDA") {
                    if (idxRA != -1 && idxDec != -1) {
                        if (!std::isfinite(r) || !std::isfinite(d)) {
                            continue;
                        }
                        PGCGalaxy g;
                        g.pgcName = "PGC " + pgcStr.trimmed();
                        g.ra = r;
                        g.dec = d;
                        if (std::isfinite(logD)) {
                            double D = std::pow(10.0, logD) * 0.1;
                            g.majorAxisArcmin = D;
                            if (std::isfinite(logR)) {
                                double R = std::pow(10.0, logR);
                                g.minorAxisArcmin = D / R;
                            } else {
                                g.minorAxisArcmin = D;
                            }
                        }
                        if (std::isfinite(pa)) {
                            g.posAngle = pa;
                        }
                        galaxies.push_back(g);
                    }
                } else {
                    if (idxRA != -1 && idxDec != -1) {
                        if (!std::isfinite(r) || !std::isfinite(d)) {
                            continue;
                        }

                        CatalogStar s;
                        s.ra = r;
                        s.dec = d;
                        s.teff = teff;
                        
                        if (std::isnan(v) && std::isfinite(g_mag)) v = g_mag; 
                        if (std::isnan(b) && std::isfinite(bp_mag) && std::isfinite(rp_mag)) b = bp_mag; 
                        
                        s.magB = b;
                        s.magV = v;
                        s.bp_rp = std::numeric_limits<double>::quiet_NaN();

                        if (std::isfinite(bp_mag) && std::isfinite(rp_mag)) {
                            const double bprp = bp_mag - rp_mag;
                            s.bp_rp = bprp;
                            s.B_V = gaiaBpRpToBV(bprp);
                        } else if (std::isfinite(b) && std::isfinite(v)) {
                            const double bv = b - v;
                            s.B_V = bv;
                            s.bp_rp = (bv - 0.393) / 0.45; 
                        } else if (std::isfinite(bp_mag) && std::isfinite(g_mag)) {
                            s.B_V = bp_mag - g_mag;
                            s.bp_rp = s.B_V / 0.8; 
                        } else {
                            s.B_V = 0.65;
                        }
                        
                        if ((std::isfinite(s.magV) || s.teff > 0) && std::isfinite(s.ra) && std::isfinite(s.dec)) {
                            stars.push_back(s);
                        }
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
    
    if (m_lastQueryType == "HYPERLEDA") {
        if (galaxies.empty()) {
            qWarning() << "No galaxies found in region - catalog query succeeded but returned 0 galaxies";
        }
        emit galaxiesReady(galaxies);
    } else {
        if (stars.empty()) {
            qWarning() << "No stars found in region - catalog query succeeded but returned 0 stars";
        }
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
    else if (m_lastQueryType == "HYPERLEDA") sendHyperLeda();
}
