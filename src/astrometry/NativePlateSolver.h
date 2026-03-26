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
    bool success = false;
    GenericTrans transform;
    double crval1 = 0, crval2 = 0;
    double crpix1 = 0, crpix2 = 0;
    double cd[2][2] = {{0,0},{0,0}};
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
    void processSolving(const std::vector<MatchStar>& catStars,
                        const ImageBuffer&             image,
                        const std::vector<CatalogStar>& catalogStars,
                        double raHint, double decHint,
                        double pixelScale);

    // Core solver 
    //
    //   1. Project catalog at initial (ra0, dec0)
    //   2. new_star_match: triangle voting → iter_trans(RECALC_NO) → applyMatch+matchLists
    //                      → atRecalcTrans x2 → return matched pairs (star_list_A/B)
    //   3. Sanity + scale checks on initial TRANS
    //   4. Convergence loop (up to MAX_CONVERGENCE_TRIALS):
    //      a. apply_match(ra0, dec0, 0, 0) → new (ra0, dec0)
    //      b. Re-project catalog at new center
    //      c. update_stars_positions: update B positions of ORIGINAL matched pairs
    //      d. atRecalcTrans: recalc TRANS from SAME matched pairs (RECALC_YES)
    //      e. Check offset < CONV_TOLERANCE → stop
    //
    // NOTE: The convergence loop does NOT re-apply trans to all stars and re-match.
    // It preserves the original matched pairs and only updates catalog side positions.
    int matchCatalog(const std::vector<MatchStar>& imgStars, int nbImgStars,
                     const std::vector<MatchStar>& catStarsRaw,
                     double scale, double ra0, double dec0,
                     GenericTrans& transOut, double& raOut, double& decOut);

    // Gnomonic projection of catalog stars to tangent plane (in arcsec)
    std::vector<MatchStar> projectCatalog(const std::vector<MatchStar>& catStars,
                                          double ra0, double dec0) const;

    // Given (ra0, dec0) projection center and (xval, yval) in the centered image frame,
    // applies the TRANS to get tangent plane offset, then de-projects to get (raOut, decOut)
    static void applyMatch(double ra0, double dec0,
                           double xval, double yval,
                           const GenericTrans& trans,
                           double& raOut, double& decOut);

    // Check affine TRANS sanity
    static bool checkTransSanity(const GenericTrans& trans);

    // Check affine TRANS scale
    static bool checkTransScale(const GenericTrans& trans, double scaleMin, double scaleMax);

    // Compute magnitude limit from sky position and FOV
    static double computeMagLimit(double ra, double dec, double fovDegrees, int nStars);

    // Update star positions from a new catalog projection, preserving match linkage
    static void updateStarPositions(std::vector<MatchStar>& matchedCatStars,
                                    int numMatched,
                                    const std::vector<MatchStar>& newProjectedCat);
};

#endif // NATIVEPLATESOLVER_H
