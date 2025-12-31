#ifndef SIMBADSEARCHER_H
#define SIMBADSEARCHER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class SimbadSearcher : public QObject {
    Q_OBJECT
public:
    explicit SimbadSearcher(QObject* parent = nullptr);
    void search(const QString& objectName);

signals:
    void objectFound(const QString& name, double ra, double dec);
    void errorOccurred(const QString& errorMsg);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_manager;
};

#endif // SIMBADSEARCHER_H
