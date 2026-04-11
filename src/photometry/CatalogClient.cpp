// =============================================================================
// CatalogClient.cpp
//
// Implementation of the asynchronous VizieR catalog query client.
// Handles APASS DR9, Gaia DR3, and HyperLeda (PGC) queries with
// automatic mirror failover and VOTable XML parsing.
// =============================================================================

#include "CatalogClient.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QDebug>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

// =============================================================================
// VizieR mirror server list (geographically diverse)
// =============================================================================

static const QStringList VIZIER_MIRRORS = {
    "https://vizier.u-strasbg.fr/viz-bin/votable",   // Strasbourg, France (primary)
    "https://vizier.cds.unistra.fr/viz-bin/votable",  // Strasbourg, France (alias)
    "https://vizier.cfa.harvard.edu/viz-bin/votable",  // Cambridge, USA
    "https://vizier.nao.ac.jp/viz-bin/votable",        // Tokyo, Japan
    "https://vizier.iucaa.in/viz-bin/votable",         // Pune, India
    "http://vizier.china-vo.org/viz-bin/votable"        // Beijing, China
};

// Static mirror index shared across all instances for session persistence.
int CatalogClient::s_currentMirrorIndex = 0;

// =============================================================================
// Gaia BP-RP to Johnson B-V color index conversion
//
// Polynomial approximation for main-sequence stars.
// Coefficients derived from cross-matched Gaia DR3 / Landolt samples.
// =============================================================================

static double gaiaBpRpToBV(double bprp)
{
    return 0.3930 + 0.4750 * bprp - 0.0548 * bprp * bprp;
}

// =============================================================================
// Construction
// =============================================================================

CatalogClient::CatalogClient(QObject* parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

// =============================================================================
// Public query entry points
//
// Each method stores query parameters for potential retry, validates the
// mirror index, and delegates to the corresponding internal send method.
// =============================================================================

void CatalogClient::queryAPASS(double ra, double dec, double radiusDeg)
{
    m_lastQueryRa     = ra;
    m_lastQueryDec    = dec;
    m_lastQueryRadius = radiusDeg;
    m_lastQueryType   = "APASS";

    // Preserve the current mirror index to benefit from prior successful connections.
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size())
        s_currentMirrorIndex = 0;

    sendAPASS();
}

void CatalogClient::queryGaiaDR3(double ra, double dec, double radiusDeg)
{
    // Cap the search radius to avoid server timeouts on large-area queries.
    double cappedRadius = std::min(radiusDeg, 3.0);

    m_lastQueryRa     = ra;
    m_lastQueryDec    = dec;
    m_lastQueryRadius = cappedRadius;
    m_lastQueryType   = "GAIA";

    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size())
        s_currentMirrorIndex = 0;

    sendGaia();
}

void CatalogClient::queryHyperLeda(double ra, double dec, double radiusDeg)
{
    double cappedRadius = std::min(radiusDeg, 3.0);

    m_lastQueryRa     = ra;
    m_lastQueryDec    = dec;
    m_lastQueryRadius = cappedRadius;
    m_lastQueryType   = "HYPERLEDA";

    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size())
        s_currentMirrorIndex = 0;

    sendHyperLeda();
}

// =============================================================================
// Internal send methods
//
// Build the VizieR URL, attach timeout and error handling, and dispatch
// the HTTP GET request. These do not reset the mirror index so they can
// be called directly during mirror failover.
// =============================================================================

void CatalogClient::sendAPASS()
{
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for APASS."));
        return;
    }

    QUrl url(VIZIER_MIRRORS[s_currentMirrorIndex]);
    emit mirrorStatus(tr("Querying APASS on %1...").arg(url.host()));

    QUrlQuery query;
    query.addQueryItem("-source", "II/336/apass9");
    query.addQueryItem("-c",
        QString::number(m_lastQueryRa, 'f', 6) + " " +
        QString::number(m_lastQueryDec, 'f', 6));
    query.addQueryItem("-c.rm",
        QString::number(m_lastQueryRadius * 60.0, 'f', 2));
    query.addQueryItem("-out", "RAJ2000,DEJ2000,Bmag,Vmag");
    query.addQueryItem("-out.max", "2000");
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply* reply = m_manager->get(req);

    // Per-mirror timeout: 30 seconds for APASS queries.
    const int apassMirrorIdx = s_currentMirrorIndex;
    QTimer::singleShot(30000, reply, [reply, apassMirrorIdx]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR APASS timeout on mirror"
                       << apassMirrorIdx;
            reply->abort();
        }
    });

    // Connect only the finished signal; the errorOccurred signal also triggers
    // finished, so connecting both would cause double-retry loops.
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::sendGaia()
{
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for Gaia DR3."));
        return;
    }

    QUrl url(VIZIER_MIRRORS[s_currentMirrorIndex]);
    emit mirrorStatus(tr("Querying Gaia DR3 on %1...").arg(url.host()));

    qInfo() << "[CatalogClient] Querying Gaia DR3 on mirror"
            << s_currentMirrorIndex
            << VIZIER_MIRRORS[s_currentMirrorIndex];

    QUrlQuery query;
    query.addQueryItem("-source", "I/355/gaiadr3");
    query.addQueryItem("-c",
        QString::number(m_lastQueryRa, 'f', 6) + " " +
        QString::number(m_lastQueryDec, 'f', 6));
    query.addQueryItem("-c.rm",
        QString::number(m_lastQueryRadius * 60.0, 'f', 2));
    query.addQueryItem("-out",
        "RA_ICRS,DE_ICRS,Gmag,BPmag,RPmag,teff_gspphot");

    // Adaptive output limit: ensure dense star mapping even for wide fields (e.g. 3.0 deg radius)
    // 3000 ensures we go deep enough into the magnitudes so that the 500 brightest image stars
    // have a matching equivalent in the catalog to form triangles even on a 15 sq deg FOV.
    const int outMax = 3000;
    query.addQueryItem("-out.max", QString::number(outMax));
    query.addQueryItem("-sort", "Gmag");
    query.addQueryItem("Gmag", "<20");
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply* reply = m_manager->get(req);

    // Adaptive timeout: wider fields require more VizieR processing time.
    // Increased to 45s/60s for heavy Gaia DR3 queries.
    const int gaiaTimeoutMs = (m_lastQueryRadius > 2.0) ? 60000 : 45000;
    const int gaiaMirrorIdx = s_currentMirrorIndex;
    QTimer::singleShot(gaiaTimeoutMs, reply, [reply, gaiaMirrorIdx]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR Gaia timeout on mirror"
                       << gaiaMirrorIdx;
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::sendHyperLeda()
{
    if (s_currentMirrorIndex >= VIZIER_MIRRORS.size()) {
        emit errorOccurred(tr("All VizieR mirrors failed for HyperLeda."));
        return;
    }

    QUrl url(VIZIER_MIRRORS[s_currentMirrorIndex]);
    emit mirrorStatus(tr("Querying HyperLeda (PGC) on %1...").arg(url.host()));

    qInfo() << "[CatalogClient] Querying HyperLeda on mirror"
            << s_currentMirrorIndex
            << VIZIER_MIRRORS[s_currentMirrorIndex];

    QUrlQuery query;
    query.addQueryItem("-source", "VII/237/pgc");
    query.addQueryItem("-c",
        QString::number(m_lastQueryRa, 'f', 6) + " " +
        QString::number(m_lastQueryDec, 'f', 6));
    query.addQueryItem("-c.rm",
        QString::number(m_lastQueryRadius * 60.0, 'f', 2));
    query.addQueryItem("-out", "PGC,RAJ2000,DEJ2000,logD25,logR25,PA");

    const int outMax = (m_lastQueryRadius > 2.0) ? 1000 : 2000;
    query.addQueryItem("-out.max", QString::number(outMax));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TStar-CatalogClient");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply* reply = m_manager->get(req);

    // Increased timeout for HyperLeda queries.
    const int hledaTimeoutMs = (m_lastQueryRadius > 2.0) ? 60000 : 45000;
    const int hledaMirrorIdx = s_currentMirrorIndex;
    QTimer::singleShot(hledaTimeoutMs, reply, [reply, hledaMirrorIdx]() {
        if (reply->isRunning()) {
            qWarning() << "VizieR HyperLeda timeout on mirror"
                       << hledaMirrorIdx;
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

// =============================================================================
// Network reply handler and VOTable XML parser
//
// Validates the HTTP response, detects error pages, then parses the VOTable
// XML to extract catalog records. Field-to-column mapping uses both field
// names and UCDs for robustness across different VizieR mirror responses.
// =============================================================================

void CatalogClient::onReply(QNetworkReply* reply)
{
    reply->deleteLater();

    // --- Check for network-level errors ---

    if (reply->error() != QNetworkReply::NoError) {
        QString errStr  = reply->errorString();
        int httpCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        qWarning() << "[CatalogClient] Mirror" << s_currentMirrorIndex
                   << "failed:" << errStr << "(HTTP" << httpCode << ")";
        emit mirrorStatus(tr("Mirror %1 failed (%2, HTTP %3). Retrying...")
                          .arg(s_currentMirrorIndex)
                          .arg(errStr)
                          .arg(httpCode));
        retryWithNextMirror();
        return;
    }

    // --- Read response body ---

    QByteArray data = reply->readAll();
    qDebug() << "[CatalogClient] Response sample (200 bytes):" << data.left(200);

    // Detect HTML error pages (VizieR returns HTML rather than XML on errors).
    QString dataStr = QString::fromUtf8(data);
    if (dataStr.toLower().contains("<html")  ||
        dataStr.toLower().contains("<!doctype") ||
        dataStr.contains("Error 500")        ||
        dataStr.contains("Bad Request"))
    {
        qWarning() << "VizieR returned HTML error page, retrying next mirror...";
        retryWithNextMirror();
        return;
    }

    // --- Parse VOTable XML ---

    std::vector<CatalogStar> stars;
    std::vector<PGCGalaxy>   galaxies;
    QXmlStreamReader xml(data);

    // Column index mapping (initialized to -1 = not found)
    int idxRA   = -1, idxDec  = -1;
    int idxB    = -1, idxV    = -1;
    int idxTeff = -1, idxG    = -1;
    int idxBP   = -1, idxRP   = -1;

    int idxPGC    = -1, idxLogD25 = -1;
    int idxLogR25 = -1, idxPA     = -1;

    int currentFieldIndex = 0;

    while (!xml.atEnd()) {
        xml.readNext();

        if (!xml.isStartElement()) continue;

        QString name = xml.name().toString();

        // --- Reset column indices for each TABLE element in the VOTable ---

        if (name == "TABLE") {
            currentFieldIndex = 0;
            idxRA = idxDec = idxB = idxV = idxTeff = idxG = idxBP = idxRP = -1;
            idxPGC = idxLogD25 = idxLogR25 = idxPA = -1;
        }

        // --- Map FIELD elements to column indices ---

        else if (name == "FIELD") {
            QString fieldName = xml.attributes().value("name").toString();
            QString fieldID   = xml.attributes().value("ID").toString();
            QString fn = fieldName.toLower();
            QString fi = fieldID.toLower();
            QString fu = xml.attributes().value("ucd").toString().toLower();

            // Right ascension
            if (fn == "raj2000" || fi == "raj2000" ||
                fn == "ra_icrs" || fu.contains("pos.eq.ra"))
                idxRA = currentFieldIndex;

            // Declination
            else if (fn == "dej2000" || fi == "dej2000" ||
                     fn == "de_icrs" || fu.contains("pos.eq.dec"))
                idxDec = currentFieldIndex;

            // Johnson B magnitude (exclude Gaia BP)
            else if ((fn == "bmag" || fi == "bmag") ||
                     (fu == "phot.mag;em.opt.b" &&
                      !fn.contains("bp") && !fi.contains("bp")))
                idxB = currentFieldIndex;

            // Johnson V magnitude (exclude Gaia RP)
            else if ((fn == "vmag" || fi == "vmag") ||
                     (fu == "phot.mag;em.opt.v" &&
                      !fn.contains("rp") && !fi.contains("rp")))
                idxV = currentFieldIndex;

            // Effective temperature
            else if (fn.contains("teff") || fi.contains("teff") ||
                     fu.contains("phys.temperature"))
                idxTeff = currentFieldIndex;

            // Gaia G magnitude
            else if (fn == "gmag" || fi == "gmag" ||
                     fu == "phot.mag;em.opt.g")
                idxG = currentFieldIndex;

            // Gaia BP magnitude
            else if ((fn.contains("bpmag") || fi.contains("bpmag") ||
                      fn.contains("bp-mag")) && !fn.startsWith("e_"))
                idxBP = currentFieldIndex;

            // Gaia RP magnitude
            else if ((fn.contains("rpmag") || fi.contains("rpmag") ||
                      fn.contains("rp-mag")) && !fn.startsWith("e_"))
                idxRP = currentFieldIndex;

            // UCD-based fallback matching for Gaia photometric columns
            else if (idxBP == -1 &&
                     (fu.contains("em.opt.b") || fu.contains("phot.mag.b")) &&
                     fu.contains("phot.mag"))
                idxBP = currentFieldIndex;
            else if (idxRP == -1 &&
                     (fu.contains("em.opt.r") || fu.contains("phot.mag.r")) &&
                     fu.contains("phot.mag"))
                idxRP = currentFieldIndex;

            // HyperLeda fields
            else if (fn == "pgc"    || fi == "pgc")    idxPGC    = currentFieldIndex;
            else if (fn == "logd25" || fi == "logd25") idxLogD25 = currentFieldIndex;
            else if (fn == "logr25" || fi == "logr25") idxLogR25 = currentFieldIndex;
            else if (fn == "pa"     || fi == "pa")     idxPA     = currentFieldIndex;

            ++currentFieldIndex;
        }

        // --- Parse table row data ---

        else if (name == "TR") {
            // Initialize per-row values to NaN or zero
            double r      = std::numeric_limits<double>::quiet_NaN();
            double d      = std::numeric_limits<double>::quiet_NaN();
            double b      = std::numeric_limits<double>::quiet_NaN();
            double v      = std::numeric_limits<double>::quiet_NaN();
            double teff   = 0.0;
            double g_mag  = std::numeric_limits<double>::quiet_NaN();
            double bp_mag = std::numeric_limits<double>::quiet_NaN();
            double rp_mag = std::numeric_limits<double>::quiet_NaN();

            QString pgcStr;
            double logD = std::numeric_limits<double>::quiet_NaN();
            double logR = std::numeric_limits<double>::quiet_NaN();
            double pa   = std::numeric_limits<double>::quiet_NaN();

            int colIndex = 0;

            // Read all TD elements within this TR
            while (!(xml.isEndElement() && xml.name().toString() == "TR")
                   && !xml.atEnd())
            {
                if (xml.readNext() == QXmlStreamReader::StartElement &&
                    xml.name().toString() == "TD")
                {
                    QString text = xml.readElementText();

                    if (!text.isEmpty()) {
                        if      (colIndex == idxPGC)    pgcStr = text;
                        else if (colIndex == idxRA)     r      = text.toDouble();
                        else if (colIndex == idxDec)    d      = text.toDouble();
                        else if (colIndex == idxB)      b      = text.toDouble();
                        else if (colIndex == idxV)      v      = text.toDouble();
                        else if (colIndex == idxTeff)   teff   = text.toDouble();
                        else if (colIndex == idxG)      g_mag  = text.toDouble();
                        else if (colIndex == idxBP)     bp_mag = text.toDouble();
                        else if (colIndex == idxRP)     rp_mag = text.toDouble();
                        else if (colIndex == idxLogD25) logD   = text.toDouble();
                        else if (colIndex == idxLogR25) logR   = text.toDouble();
                        else if (colIndex == idxPA)     pa     = text.toDouble();
                    }

                    ++colIndex;
                }
            }

            // --- Construct the appropriate output record ---

            if (m_lastQueryType == "HYPERLEDA") {
                // HyperLeda galaxy record
                if (idxRA == -1 || idxDec == -1) continue;
                if (!std::isfinite(r) || !std::isfinite(d)) continue;

                PGCGalaxy g;
                g.pgcName = "PGC " + pgcStr.trimmed();
                g.ra      = r;
                g.dec     = d;

                if (std::isfinite(logD)) {
                    double D = std::pow(10.0, logD) * 0.1;
                    g.majorAxisArcmin = D;
                    g.minorAxisArcmin = std::isfinite(logR)
                        ? D / std::pow(10.0, logR)
                        : D;
                }
                if (std::isfinite(pa)) {
                    g.posAngle = pa;
                }

                galaxies.push_back(g);

            } else {
                // Star record (APASS or Gaia DR3)
                if (idxRA == -1 || idxDec == -1) continue;
                if (!std::isfinite(r) || !std::isfinite(d)) continue;

                CatalogStar s;
                s.ra   = r;
                s.dec  = d;
                s.teff = teff;

                // Fallback magnitude assignment: use Gaia G if Johnson V is absent
                if (std::isnan(v) && std::isfinite(g_mag))
                    v = g_mag;
                if (std::isnan(b) && std::isfinite(bp_mag) && std::isfinite(rp_mag))
                    b = bp_mag;

                s.magB  = b;
                s.magV  = v;
                s.bp_rp = std::numeric_limits<double>::quiet_NaN();

                // Derive B-V color index from available photometry
                if (std::isfinite(bp_mag) && std::isfinite(rp_mag)) {
                    const double bprp = bp_mag - rp_mag;
                    s.bp_rp = bprp;
                    s.B_V   = gaiaBpRpToBV(bprp);
                } else if (std::isfinite(b) && std::isfinite(v)) {
                    const double bv = b - v;
                    s.B_V   = bv;
                    s.bp_rp = (bv - 0.393) / 0.45;
                } else if (std::isfinite(bp_mag) && std::isfinite(g_mag)) {
                    s.B_V   = bp_mag - g_mag;
                    s.bp_rp = s.B_V / 0.8;
                } else {
                    s.B_V = 0.65; // Default solar-type color
                }

                // Accept the star if it has at least a magnitude or temperature
                if ((std::isfinite(s.magV) || s.teff > 0.0) &&
                    std::isfinite(s.ra) && std::isfinite(s.dec))
                {
                    stars.push_back(s);
                }
            }
        }
    }

    // --- Handle XML parse errors ---

    if (xml.hasError()) {
        qWarning() << "XML Parse Error:" << xml.errorString()
                   << "- trying next mirror";
        retryWithNextMirror();
        return;
    }

    // --- Emit results ---

    if (m_lastQueryType == "HYPERLEDA") {
        if (galaxies.empty()) {
            qWarning() << "No galaxies found in region"
                       << "- catalog query succeeded but returned 0 galaxies";
        }
        emit galaxiesReady(galaxies);
    } else {
        if (stars.empty()) {
            qWarning() << "No stars found in region"
                       << "- catalog query succeeded but returned 0 stars";
        }
        emit catalogReady(stars);
    }
}

// =============================================================================
// Mirror failover logic
//
// Advances to the next VizieR mirror. For Gaia DR3, if all mirrors have
// been exhausted at the current radius and the radius exceeds 1 degree,
// the radius is reduced by 40% and the cycle restarts from mirror 0.
// =============================================================================

void CatalogClient::retryWithNextMirror()
{
    ++s_currentMirrorIndex;

    // Gaia-specific radius reduction: retry with a smaller cone if all mirrors failed.
    if (m_lastQueryType == "GAIA" &&
        s_currentMirrorIndex >= VIZIER_MIRRORS.size() &&
        m_lastQueryRadius > 1.0)
    {
        const double oldRadius = m_lastQueryRadius;
        m_lastQueryRadius = std::max(1.0, m_lastQueryRadius * 0.6);
        s_currentMirrorIndex = 0;

        emit mirrorStatus(
            tr("All Gaia mirrors failed at %1 deg; retrying at %2 deg...")
            .arg(oldRadius, 0, 'f', 2)
            .arg(m_lastQueryRadius, 0, 'f', 2));
    }

    // Dispatch the retry to the appropriate send method.
    if      (m_lastQueryType == "GAIA")      sendGaia();
    else if (m_lastQueryType == "APASS")     sendAPASS();
    else if (m_lastQueryType == "HYPERLEDA") sendHyperLeda();
}