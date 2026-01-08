#include "CatalogClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QDebug>

CatalogClient::CatalogClient(QObject* parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
}

void CatalogClient::queryAPASS(double ra, double dec, double radiusDeg) {
    // VizieR Cone Search for APASS (II/336)
    // VizieR Cone Search (II/336) uses arcminutes for radius (-c.rm)
    
    QString baseUrl = "http://vizier.u-strasbg.fr/viz-bin/votable";
    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("-source", "II/336/apass9"); 
    query.addQueryItem("-c", QString("%1 %2").arg(ra).arg(dec));
    query.addQueryItem("-c.rm", QString::number(radiusDeg * 60.0));
    query.addQueryItem("-out", "RAJ2000,DEJ2000,Bmag,Vmag");
    query.addQueryItem("-out.max", "2000");
    url.setQuery(query);
    
    QNetworkRequest req(url);
    QNetworkReply* reply = m_manager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::queryGaiaDR3(double ra, double dec, double radiusDeg) {
    // VizieR Cone Search for Gaia DR3 (I/355/gaiadr3)
    QString baseUrl = "http://vizier.u-strasbg.fr/viz-bin/votable";
    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("-source", "I/355/gaiadr3"); 
    query.addQueryItem("-c", QString("%1 %2").arg(ra).arg(dec));
    query.addQueryItem("-c.rm", QString::number(radiusDeg * 60.0)); // arcmin
    // Request basic astrometry + photometry + parameters
    // Note: 'Teff' is the standardized column name in VizieR for Effective Temperature or teff_gspphot
    query.addQueryItem("-out", "RA_ICRS,DE_ICRS,Gmag,BPmag,RPmag,Teff");
    query.addQueryItem("-out.max", "2000"); // Limit to brighter stars usually
    query.addQueryItem("-out.add", "_r"); // sort by distance
    // Filter by Gmag to avoid downloading faint noise (limit to mag 16 for PCC)
    query.addQueryItem("Gmag", "<16");
    
    url.setQuery(query);
    
    QNetworkRequest req(url);
    QNetworkReply* reply = m_manager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReply(reply);
    });
}

void CatalogClient::onReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error()) {
        emit errorOccurred(reply->errorString());
        return;
    }
    
    QByteArray data = reply->readAll();
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
        emit errorOccurred(tr("XML Parse Error: %1").arg(xml.errorString()));
        return;
    }
    
    if (stars.empty()) {
        if (idxRA == -1) emit errorOccurred(tr("Parser failed to find RA column."));
        else emit errorOccurred(tr("No stars found in region."));
    } else {
        emit catalogReady(stars);
    }
}
