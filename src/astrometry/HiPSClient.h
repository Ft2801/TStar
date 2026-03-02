#ifndef HIPSCLIENT_H
#define HIPSCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryFile>
#include "../ImageBuffer.h"

class HiPSClient : public QObject {
    Q_OBJECT
public:
    explicit HiPSClient(QObject* parent = nullptr);

    // Common HiPS catalogs
    static const QString SUR_PANSTARRS_DR1_COLOR;
    static const QString SUR_DSS2_RED;
    static const QString SUR_UNWISE_COLOR;

    /**
     * @brief Fetches a FITS image cutout from CDS Aladin hips2fits
     * @param hips          The HiPS survey ID (e.g. "CDS/P/DSS2/red")
     * @param ra            Right Ascension of the center in degrees
     * @param dec           Declination of the center in degrees
     * @param fov           Field of View of the X axis (width) in degrees
     * @param width         Width of the requested image in pixels
     * @param height        Height of the requested image in pixels
     * @param rotationAngle Position angle of image Y-axis from North, East of North (degrees).
     *                      Pass 0 for standard North-up orientation.
     *                      Computed from target image WCS CD matrix via WCSUtils::positionAngle().
     */
    void fetchFITS(const QString& hips, double ra, double dec, double fov,
                   int width, int height, double rotationAngle = 0.0);
    void clearCache();
    static qint64 getCacheSize();
    static void setMaxCacheSize(qint64 bytes);

signals:
    void imageReady(const ImageBuffer& buffer);
    void errorOccurred(const QString& errorMsg);
    void downloadProgress(qint64 received, qint64 total);

private slots:
    void onReplyFinished();

private:
    QString getCacheFilePath(const QString& hips, double ra, double dec, double fov,
                             int width, int height, double rotationAngle);
    void cleanupCache();

    QNetworkAccessManager* m_manager;
    QNetworkReply* m_reply = nullptr;
    QString m_cacheDir;
    static qint64 s_maxCacheSize;
};

#endif // HIPSCLIENT_H
