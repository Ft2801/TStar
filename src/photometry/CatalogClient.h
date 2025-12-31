#ifndef CATALOGCLIENT_H
#define CATALOGCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <vector>

struct CatalogStar {
    double ra;
    double dec;
    double magB;
    double magV;
    double B_V; // Color index
    double teff; // Effective Temperature (Gaia DR3)
};

class CatalogClient : public QObject {
    Q_OBJECT
public:
    explicit CatalogClient(QObject* parent = nullptr);
    void queryAPASS(double ra, double dec, double radiusDeg);
    void queryGaiaDR3(double ra, double dec, double radiusDeg);

signals:
    void catalogReady(const std::vector<CatalogStar>& stars);
    void errorOccurred(const QString& msg);

private slots:
    void onReply(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_manager;
};

#endif // CATALOGCLIENT_H
