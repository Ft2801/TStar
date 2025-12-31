#include "SimbadSearcher.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <QDebug>

SimbadSearcher::SimbadSearcher(QObject* parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
}

void SimbadSearcher::search(const QString& objectName) {
    // Use Sesame Name Resolver (XML)
    QString urlStr = QString("http://cdsweb.u-strasbg.fr/cgi-bin/nph-sesame/-ox?%1").arg(objectName);
    QNetworkRequest request{QUrl(urlStr)};
    
    QNetworkReply* reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

void SimbadSearcher::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(tr("Network Error: %1").arg(reply->errorString()));
        return;
    }
    
    QByteArray data = reply->readAll();
    QXmlStreamReader xml(data);
    
    QString resolvedName;
    double ra = 0;
    double dec = 0;
    bool found = false;
    
    // Parse Sesame XML
    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("Target")) {
                // Keep looking
            } else if (xml.name() == QLatin1String("name")) { // Resolver name
                 // We might want the main identifier, usually 'oname' inside <Resolver>
            } else if (xml.name() == QLatin1String("jradeg")) {
                ra = xml.readElementText().toDouble();
            } else if (xml.name() == QLatin1String("jdedeg")) {
                dec = xml.readElementText().toDouble();
                found = true; // Assume if we got DEC we got RA
            }
        }
    }
    
    if (xml.hasError()) {
        emit errorOccurred(tr("XML Parse Error"));
        return;
    }
    
    if (found) {
        emit objectFound("", ra, dec); // Name passed back empty or original?
    } else {
        emit errorOccurred(tr("Object not found in Simbad/Sesame."));
    }
}
