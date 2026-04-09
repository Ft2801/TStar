#ifndef NATIVEPLATESOLVER_H
#define NATIVEPLATESOLVER_H

// ============================================================================
// NativePlateSolver
//
// Built-in astrometric plate solver that operates entirely within the
// application, requiring only an online catalog query (Gaia DR3 via VizieR).
//
// Algorithm overview:
//   1. Query Gaia DR3 catalog for reference stars around the hint position.
//   2. Detect stars in the input image.
//   3. Project catalog stars onto a tangent plane centred at the hint.
//   4. Match image and catalog stars via geometric triangle voting.
//   5. Iteratively converge on the true field centre by re-projecting
//      the catalog and refitting the affine transform.
//   6. Compute FITS-standard WCS parameters (CRPIX, CRVAL, CD matrix).
// ============================================================================

#include <QObject>
#include <QNetworkAccessManager>

#include "../ImageBuffer.h"
#include "../photometry/StarDetector.h"
#include "TriangleMatcher.h"
#include "WcsSolver.h"

// ============================================================================
// Result structure
// ============================================================================

struct NativeSolveResult {
    bool         success   = false;
    GenericTrans transform;

    double crval1 = 0, crval2 = 0;
    double crpix1 = 0, crpix2 = 0;
    double cd[2][2] = {{0, 0}, {0, 0}};

    QString errorMsg;

    std::vector<CatalogStar> catalogStars;
};

// ============================================================================
// NativePlateSolver class
// ============================================================================

class NativePlateSolver : public QObject {
    Q_OBJECT

public:
    explicit NativePlateSolver(QObject* parent = nullptr);

    /**
     * Initiates an asynchronous plate solve.
     *
     * @param image      The image to solve (must be valid).
     * @param raHint     Approximate RA of field centre (degrees).
     * @param decHint    Approximate Dec of field centre (degrees).
     * @param radiusDeg  Search radius (degrees).
     * @param pixelScale Pixel scale (arcsec/pixel). Essential for scaling
     *                   the catalog to match the image. Pass -1 if unknown.
     */
    void solve(const ImageBuffer& image,
               double raHint, double decHint,
               double radiusDeg, double pixelScale);

    /** Requests cancellation of the current solve. Thread-safe. */
    void cancelSolve() { m_stop = true; }

signals:
    void logMessage(const QString& msg);
    void finished(const NativeSolveResult& result);

private slots:
    void onCatalogReceived(const std::vector<MatchStar>& catalogStars);
    void onCatalogError(const QString& msg);

private:
    // -- Member state -----------------------------------------------------
    QNetworkAccessManager*   m_nam;
    ImageBuffer              m_image;
    double                   m_raHint, m_decHint;
    double                   m_radius;
    double                   m_pixelScale;
    std::vector<CatalogStar> m_catalogStars;
    std::atomic<bool>        m_stop{false};

    // -- Internal pipeline methods ----------------------------------------

    /** Queries the Gaia DR3 catalog via VizieR. */
    void fetchCatalog();

    /**
     * Main solving pipeline: star detection, triangle matching,
     * convergence loop, and WCS computation. Runs on a background thread.
     */
    void processSolving(const std::vector<MatchStar>& catStars,
                        const std::vector<float>& pixels,
                        int w, int h, int ch,
                        const std::vector<CatalogStar>& catalogStars,
                        double raHint, double decHint,
                        double pixelScale);

    // -- Core matching and convergence ------------------------------------

    /**
     * Core solver: projects catalog, performs triangle matching, and runs
     * the iterative convergence loop to refine the field centre.
     *
     * Convergence architecture:
     *   1. Project catalog at (ra0, dec0).
     *   2. Run TriangleMatcher::solve() ONCE for initial TRANS + matched pairs.
     *   3. Validate TRANS sanity and scale.
     *   4. Convergence loop (up to MAX_CONVERGENCE_TRIALS):
     *      a. Compute new (ra0, dec0) via applyMatch at image centre (0,0).
     *      b. Reproject catalog at the new centre.
     *      c. Update matched catalog star positions from the new projection.
     *      d. Recalculate TRANS from updated matched pairs (RECALC_YES).
     *      e. Check convergence (offset < CONV_TOLERANCE arcsec).
     *
     * NOTE: The convergence loop preserves the original matched pairs and
     * only updates catalog-side positions. It does NOT re-match from scratch.
     */
    int matchCatalog(const std::vector<MatchStar>& imgStars, int nbImgStars,
                     const std::vector<MatchStar>& catStarsRaw,
                     double scale, double ra0, double dec0,
                     GenericTrans& transOut,
                     double& raOut, double& decOut);

    /** Gnomonic projection of catalog stars to tangent plane (arcsec). */
    std::vector<MatchStar> projectCatalog(
        const std::vector<MatchStar>& catStars,
        double ra0, double dec0) const;

    /**
     * Applies the affine TRANS to a point in centred image coordinates
     * and de-projects the result from the tangent plane to celestial
     * coordinates.
     */
    static void applyMatch(double ra0, double dec0,
                           double xval, double yval,
                           const GenericTrans& trans,
                           double& raOut, double& decOut);

    /** Validates that the affine TRANS has consistent scale terms. */
    static bool checkTransSanity(const GenericTrans& trans);

    /** Validates that the TRANS scale falls within the expected range. */
    static bool checkTransScale(const GenericTrans& trans,
                                double scaleMin, double scaleMax);

    /** Estimates a limiting magnitude from sky position and FOV. */
    static double computeMagLimit(double ra, double dec,
                                  double fovDegrees, int nStars);

    /**
     * Updates the positions of previously matched catalog stars using a
     * new gnomonic projection, preserving the match-pair linkage.
     */
    static void updateStarPositions(
        std::vector<MatchStar>& matchedCatStars,
        int numMatched,
        const std::vector<MatchStar>& newProjectedCat);

public:
    void solveWithCatalog(const ImageBuffer& image,
                      double raHint, double decHint,
                      double pixelScale,
                      const std::vector<MatchStar>& preloadedCatalog,
                      const std::vector<CatalogStar>& rawCatalogStars);
};

#endif // NATIVEPLATESOLVER_H