#ifndef NATIVEPLATESOLVER_H
#define NATIVEPLATESOLVER_H

#include <QObject>
#include <QNetworkAccessManager>
#include "../ImageBuffer.h"
#include "../photometry/StarDetector.h"
#include "TriangleMatcher.h"
#include "WcsSolver.h"

// Result structure
struct NativeSolveResult {
    bool success;
    GenericTrans transform;
    // WCS fields would go here or we pass the GenericTrans to a WcsSolver
    double crval1, crval2;
    double crpix1, crpix2;
    double cd[2][2];
    QString errorMsg;
    std::vector<CatalogStar> catalogStars;
};

class NativePlateSolver : public QObject {
    Q_OBJECT
public:
    explicit NativePlateSolver(QObject* parent = nullptr);

    // Main Method: Solve
    // raHint, decHint: Approximate center
    // radius: Search radius in degrees
    // pixelScale: arcsec/pixel (essential for scaling catalog)
    void solve(const ImageBuffer& image, double raHint, double decHint, double radiusDeg, double pixelScale);

signals:
    void logMessage(const QString& msg);
    void finished(const NativeSolveResult& result);

private slots:
    void onCatalogReceived(const std::vector<MatchStar>& catalogStars);
    void onCatalogError(const QString& msg);

private:
    
    QNetworkAccessManager* m_nam;
    
    ImageBuffer m_image;
    double m_raHint, m_decHint;
    double m_radius;
    double m_pixelScale;
    std::vector<CatalogStar> m_catalogStars;
    
    // Internal methods
    void fetchCatalog();
    void processSolving(const std::vector<MatchStar>& catStars);
    std::vector<MatchStar> projectCatalog(const std::vector<MatchStar>& catStars) const;
};

#endif // NATIVEPLATESOLVER_H
