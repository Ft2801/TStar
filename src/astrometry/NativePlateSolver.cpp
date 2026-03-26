#include "NativePlateSolver.h"
#include "../photometry/CatalogClient.h"
#include <cmath>
#include <QtConcurrent/QtConcurrent>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)
static const double RAD2ARCSEC = 206264.80624709636;

// 1E-2 arcsec convergence tolerance
static const double CONV_TOLERANCE = 1E-2;
// Maximum convergence iterations
static const int    MAX_CONVERGENCE_TRIALS = 5;
// TRANS_SANITY_CHECK absolute tolerance
static const double TRANS_SANITY_CHECK = 0.1;

NativePlateSolver::NativePlateSolver(QObject* parent) 
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

void NativePlateSolver::solve(const ImageBuffer& image, double raHint, double decHint, double radiusDeg, double pixelScale) {
    m_image = image;
    m_raHint = raHint;
    m_decHint = decHint;
    m_radius = radiusDeg;
    m_pixelScale = pixelScale;

    emit logMessage(tr("Starting Native Solver. Center: %1, %2 Radius: %3 deg")
                    .arg(raHint).arg(decHint).arg(radiusDeg));
    
    fetchCatalog();
}

double NativePlateSolver::computeMagLimit(double ra, double dec, double fovDegrees, int nStars) {
    double l0 = 122.9320 * DEG2RAD;
    double a0 = 192.8595 * DEG2RAD;
    double d0 =  27.1284 * DEG2RAD;
    double ra_rad = ra * DEG2RAD;
    double dec_rad = dec * DEG2RAD;

    double ml = (l0 - std::atan2(std::cos(dec_rad) * std::sin(ra_rad - a0),
                 std::sin(dec_rad) * std::cos(d0) - std::cos(dec_rad) * std::sin(d0) * std::cos(ra_rad - a0))) * RAD2DEG;
    double mb = std::asin(std::sin(dec_rad) * std::sin(d0) + std::cos(dec_rad) * std::cos(d0) * std::cos(ra_rad - a0)) * RAD2DEG;
    if (ml > 180.0) ml -= 360.0;

    double S = 2.0 * (1.0 - std::cos(0.5 * fovDegrees * DEG2RAD)) * 180.0 * 180.0 / M_PI;
    double m0 = 11.68 + 2.66 * std::sin(std::abs(mb) * DEG2RAD);
    double a = 2.36 + (std::abs(ml) - 90.0) * 0.0073 * (std::abs(ml) < 90.0 ? 1.0 : 0.0);
    double b = 0.88 - (std::abs(ml) - 90.0) * 0.0065 * (std::abs(ml) < 90.0 ? 1.0 : 0.0);
    double s = a + b * std::sin(std::abs(mb) * DEG2RAD);
    double limit = m0 + s * (std::log10((double)nStars / S) - 2.0);
    return std::max(limit, 7.0);
}

void NativePlateSolver::fetchCatalog() {
    emit logMessage(tr("Querying Catalog (VizieR)..."));
    
    CatalogClient* client = new CatalogClient(this);
    
    connect(client, &CatalogClient::catalogReady, this, [this, client](const std::vector<CatalogStar>& stars) {
        m_catalogStars = stars;
        std::vector<MatchStar> ms;
        ms.reserve(stars.size());
        for(const auto& s : stars) {
            MatchStar m;
            m.id    = (int)ms.size();  // ← set sequential catalog index (critical for updateStarPositions)
            m.index = (int)ms.size();
            m.x     = s.ra;   // RA in degrees
            m.y     = s.dec;  // Dec in degrees
            m.mag   = s.magV;
            m.match_id = -1;
            ms.push_back(m);
        }
        this->onCatalogReceived(ms);
        client->deleteLater();
    });
    
    connect(client, &CatalogClient::mirrorStatus, this, [this](const QString& msg) {
        emit logMessage(msg);
    });

    connect(client, &CatalogClient::errorOccurred, this, [this, client](const QString& err) {
        emit logMessage(tr("Catalog Error: %1").arg(err));
        onCatalogError(err);
        client->deleteLater();
    });

    // Use fixed 1.0 deg radius for Gaia DR3 (consistent with PCC).
    // This balances catalog completeness with VizieR server reliability.
    // For wider fields, ASTAP is recommended as a fallback.
    const double gaia_radius = 1.0;
    if (m_radius > 3.0) {
        emit logMessage(tr("Search radius %1 deg > 3.0 deg: using Gaia 1.0 deg for online query. Recommend ASTAP for wide fields.").arg(m_radius));
    }

    client->queryGaiaDR3(m_raHint, m_decHint, gaia_radius);
}

bool NativePlateSolver::checkTransSanity(const GenericTrans& trans) {
    double var1 = std::abs(std::abs(trans.x10) - std::abs(trans.y01));
    double var2 = std::abs(std::abs(trans.y10) - std::abs(trans.x01));
    
    if (var1 > TRANS_SANITY_CHECK || var2 > TRANS_SANITY_CHECK) {
        return false;
    }
    return true;
}

bool NativePlateSolver::checkTransScale(const GenericTrans& trans, double scaleMin, double scaleMax) {
    double resolution = std::sqrt(trans.x10 * trans.x10 + trans.y10 * trans.y10);
    if (scaleMin > 0 && scaleMax > 0) {
        // 1/scaleMin = scale*a (upper bound), 1/scaleMax = scale*b (lower bound)
        return resolution <= 1.0 / scaleMin && resolution >= 1.0 / scaleMax;
    }
    return true;
}

void NativePlateSolver::applyMatch(double ra, double dec,
                                    double xval, double yval,
                                    const GenericTrans& trans,
                                    double& raOut, double& decOut)
{
    double dec_rad = dec * DEG2RAD;

    // Apply the transform: pixel → tangent plane (arcsec)
    double delta_ra  = trans.x00 + trans.x10 * xval + trans.x01 * yval;
    double delta_dec = trans.y00 + trans.y10 * xval + trans.y01 * yval;

    // Convert arcsec → radians
    delta_ra  = (delta_ra  / 3600.0) * DEG2RAD;
    delta_dec = (delta_dec / 3600.0) * DEG2RAD;

    // De-project from tangent plane to celestial
    double z = std::cos(dec_rad) - delta_dec * std::sin(dec_rad);
    double zz = std::atan2(delta_ra, z) * RAD2DEG;
    double alpha = zz + ra;
    double delta = std::asin(
        (std::sin(dec_rad) + delta_dec * std::cos(dec_rad)) /
        std::sqrt(1.0 + delta_ra * delta_ra + delta_dec * delta_dec)
    ) * RAD2DEG;

    // Normalize RA
    if (alpha < 0) alpha += 360.0;
    if (alpha >= 360.0) alpha -= 360.0;

    // Avoid exactly 90° (wcslib convention issue)
    if (delta == 90.0) delta -= 1.0e-8;

    raOut = alpha;
    decOut = delta;
}

// ==========================================================================
// Gnomonic projection of catalog stars to tangent plane (in arcsec)
// ==========================================================================
std::vector<MatchStar> NativePlateSolver::projectCatalog(const std::vector<MatchStar>& catStars,
                                                          double ra0, double dec0) const {
    double a0 = ra0 * DEG2RAD;
    double d0 = dec0 * DEG2RAD;
    
    std::vector<MatchStar> projected;
    projected.reserve(catStars.size());
    
    double sin_d0 = std::sin(d0);
    double cos_d0 = std::cos(d0);
    
    for(const auto& s : catStars) {
        double a = s.x * DEG2RAD;
        double d = s.y * DEG2RAD;
        
        double H = std::sin(d) * sin_d0 + std::cos(d) * cos_d0 * std::cos(a - a0);
        
        if (H < 0.01) continue; // Skip stars behind the tangent point
        
        double xi_rad = (std::cos(d) * std::sin(a - a0)) / H;
        double eta_rad = (std::sin(d) * cos_d0 - std::cos(d) * sin_d0 * std::cos(a - a0)) / H;
        
        MatchStar p = s;
        p.x = xi_rad * RAD2ARCSEC;   // tangent plane X in arcsec
        p.y = eta_rad * RAD2ARCSEC;  // tangent plane Y in arcsec
        
        projected.push_back(p);
    }
    
    return projected;
}

// ==========================================================================
// updateStarPositions
// After reprojecting catalog at a new center, update the matched catalog 
// star positions to the new projection, preserving the matched pair linkage
// ==========================================================================
void NativePlateSolver::updateStarPositions(std::vector<MatchStar>& matchedCatStars,
                                             int numMatched,
                                             const std::vector<MatchStar>& newProjectedCat)
{
    // For each matched catalog star, find its counterpart in newProjectedCat by ID
    for (int i = 0; i < numMatched; i++) {
        for (const auto& np : newProjectedCat) {
            if (np.id == matchedCatStars[i].id) {
                matchedCatStars[i].x = np.x;
                matchedCatStars[i].y = np.y;
                break;
            }
        }
    }
}

// ==========================================================================
// matchCatalog
//
// This is the CORRECT convergence architecture:
//   1. Project catalog at (ra0, dec0)
//   2. Run TriangleMatcher::solve() ONCE → initial TRANS + matched pairs
//   3. Check TRANS sanity
//   4. Convergence loop (up to MAX_CONVERGENCE_TRIALS):
//      a. Compute new (ra0, dec0) via applyMatch at image center (0,0)
//      b. Reproject catalog at new center
//      c. Update matched star positions from new projection
//      d. Recalculate TRANS from updated matched pairs (iterTrans RECALC_YES)
//      e. Check convergence (offset < CONV_TOLERANCE arcsec)
// ==========================================================================
int NativePlateSolver::matchCatalog(const std::vector<MatchStar>& imgStars, int nbImgStars,
                                     const std::vector<MatchStar>& catStarsRaw,
                                     double scale, double ra0, double dec0,
                                     GenericTrans& transOut, double& raOut, double& decOut)
{
    (void)nbImgStars; // Unused but kept for API consistency

    double minScale = -1.0, maxScale = -1.0;
    if (scale > 0) {
        double rangePct = 30.0;
        double a_factor = 1.0 + (rangePct / 100.0);
        double b_factor = 1.0 - (rangePct / 100.0);
        // scaleMin = 1/(scale*a), scaleMax = 1/(scale*b)
        // vote matrix ratio check: imageTriSide/catTriSide ≈ 1/scale
        // so: 1/(scale*a) < ratio < 1/(scale*b)
        minScale = 1.0 / (scale * a_factor);
        maxScale = 1.0 / (scale * b_factor);
    }

    // === Step 1: Project catalog at initial center ===
    std::vector<MatchStar> projCat = projectCatalog(catStarsRaw, ra0, dec0);
    emit logMessage(tr("Projected %1 catalog stars at RA=%2 Dec=%3")
                    .arg(projCat.size()).arg(ra0, 0, 'f', 4).arg(dec0, 0, 'f', 4));

    if ((int)projCat.size() < 10) return -1;

    // === Step 2: Triangle matching (new_star_match) — ONE TIME ONLY ===
    //   - Top m_maxStars stars from each list → triangles + voting → initial TRANS
    //   - ALL imgStars + ALL projCat stars → full matching at AT_MATCH_MAXDIST then AT_MATCH_RADIUS
    //
    // Fallback strategy:
    //   Attempt 1: 60 stars, ±30% scale filter, default triangle radius (normal case)
    //   Attempt 2: 60 stars, no scale filter (handles uncertain pixel scale)
    //   Attempt 3: 150 stars, no scale filter (sparse/unusual fields)
    //   Attempt 4: 60 stars, no scale filter, wider triangle radius 0.003
    //              (handles lens distortion that shifts triangle shapes > 0.002 in (ba,ca) space)
    //   Attempt 5: 60 stars, no scale filter, wider radius, PARITY FLIP on image Y
    //              (handles images where the Y-axis orientation is unexpectedly reversed)
    TriangleMatcher matcher;

    static const double TRIANGLE_RADIUS_WIDE = 0.003; // 50 % larger than the default 0.002

    std::vector<MatchStar> matchedImgStars, matchedCatStars;
    bool parityFlipped = false; // set true if attempt 5 (image-Y flip) was used

    auto logMatcherDiag = [&](int attempt) {
        emit logMessage(tr("    [Attempt %1 diag] maxVote=%2 validPairs=%3 nMatched=%4 stage=%5")
                        .arg(attempt)
                        .arg(matcher.lastMaxVote())
                        .arg(matcher.lastValidPairs())
                        .arg(matcher.lastNmatched())
                        .arg(matcher.lastFailStage()));
    };

    // Attempt 1: standard (60 stars, ±30% scale filter, default radius 0.002)
    matcher.setMaxStars(60);
    matcher.setTriangleRadius(0.002);
    bool matchOk = matcher.solve(imgStars, projCat, transOut, matchedImgStars, matchedCatStars,
                                 minScale, maxScale);

    if (!matchOk && minScale > 0) {
        // Attempt 2: disable scale filter (handles cases where pixel scale estimate is off)
        emit logMessage(tr("  Attempt 1 failed, retrying without scale filter..."));
        logMatcherDiag(1);
        matchOk = matcher.solve(imgStars, projCat, transOut, matchedImgStars, matchedCatStars, -1.0, -1.0);
    }

    if (!matchOk) {
        // Attempt 3: more voting stars + no scale filter
        emit logMessage(tr("  Attempt 2 failed, retrying with 150 stars..."));
        logMatcherDiag(2);
        matcher.setMaxStars(150);
        matcher.setTriangleRadius(0.002);
        matchOk = matcher.solve(imgStars, projCat, transOut, matchedImgStars, matchedCatStars, -1.0, -1.0);
    }

    if (!matchOk) {
        // Attempt 4: standard count but WIDER triangle radius (handles lens distortion).
        // Images with field distortion > ~0.4 % cause triangle shapes to differ by
        // more than the 0.002 default radius.  0.003 accommodates up to ~0.6 % distortion.
        emit logMessage(tr("  Attempt 3 failed, retrying with wider triangle radius 0.003..."));
        logMatcherDiag(3);
        matcher.setMaxStars(60);
        matcher.setTriangleRadius(TRIANGLE_RADIUS_WIDE);
        matchOk = matcher.solve(imgStars, projCat, transOut, matchedImgStars, matchedCatStars, -1.0, -1.0);
    }

    if (!matchOk) {
        // Attempt 5: PARITY FLIP — negate image Y axis.
        // The vote step is shape-invariant (triangle shapes are reflection-symmetric),
        // so votes accumulate correctly with either handedness.  Flipping image Y makes
        // the solver try the opposite orientation, which is needed when the telescope
        // optical train produces a mirror-reversed image (odd number of reflections).
        //
        // After success we un-flip x01/y01 of the final TRANS so the WCS coefficients
        // are expressed in the original (un-flipped) image pixel coordinate system.
        emit logMessage(tr("  Attempt 4 failed, retrying with image-Y parity flip..."));
        logMatcherDiag(4);
        std::vector<MatchStar> flippedImgStars = imgStars;
        for (auto& s : flippedImgStars) s.y = -s.y;
        matcher.setMaxStars(60);
        matcher.setTriangleRadius(TRIANGLE_RADIUS_WIDE);
        matchOk = matcher.solve(flippedImgStars, projCat, transOut, matchedImgStars, matchedCatStars, -1.0, -1.0);
        if (matchOk) {
            parityFlipped = true;
            emit logMessage(tr("  Parity-flip succeeded (image-Y inverted)."));
        } else {
            logMatcherDiag(5);
        }
    }

    if (!matchOk) {
        emit logMessage(tr("Triangle matching failed."));
        return -1;
    }

    emit logMessage(tr("Initial match: %1 pairs, offset=(%2, %3) arcsec")
                    .arg(matchedImgStars.size()).arg(transOut.x00, 0, 'f', 2).arg(transOut.y00, 0, 'f', 2));

    // === Step 3: TRANS sanity and scale checks (port of check_affine_TRANS_sanity/scale) ===
    if (!checkTransSanity(transOut)) {
        emit logMessage(tr("Transform sanity check failed (|cos|≠|sin| by >%.1f arcsec/px).").arg(TRANS_SANITY_CHECK));
        return -1;
    }
    if (!checkTransScale(transOut, minScale, maxScale)) {
        emit logMessage(tr("Transform scale check failed."));
        return -1;
    }

    int numMatched = (int)matchedImgStars.size();

    // === Step 4: Convergence loop ===
    //
    //   while (offset > CONV_TOLERANCE && trial < max_trials):
    //     1. Compute new center: apply_match(ra0,dec0, 0,0, trans) -> (newRA, newDec)
    //     2. Re-project catalog at new center
    //     3. update_stars_positions(&star_list_B, numMatched, newCstars)
    //        <- update B positions of the ORIGINAL matched pairs using new projection
    //     4. atRecalcTrans(numMatched, star_list_A, numMatched, star_list_B, ...) -> new TRANS
    //        <- recalc TRANS from SAME matched pairs (with updated B positions)
    //
    // Key: We do NOT re-apply trans to all stars and re-match each iteration.
    // We keep the ORIGINAL matched pairs and just update catalog side positions.
    //
    double conv = std::sqrt(transOut.x00 * transOut.x00 + transOut.y00 * transOut.y00);
    emit logMessage(tr("  Initial: offset=%1 arcsec, nr=%2")
                    .arg(conv, 0, 'f', 3).arg(transOut.nr));

    for (int trial = 0; trial < MAX_CONVERGENCE_TRIALS && conv > CONV_TOLERANCE; trial++) {

        // 4a: Compute new projection center via apply_match at image center (0, 0)
        double newRA, newDec;
        applyMatch(ra0, dec0, 0.0, 0.0, transOut, newRA, newDec);
        ra0 = newRA;
        dec0 = newDec;

        // 4b: Re-project catalog at new center
        std::vector<MatchStar> newProjCat = projectCatalog(catStarsRaw, ra0, dec0);
        if ((int)newProjCat.size() < AT_MATCH_STARTN_LINEAR) {
            emit logMessage(tr("  Not enough stars after reprojection (%1).").arg(newProjCat.size()));
            break;
        }

        // 4c: Update positions of matched catalog stars from new projection (by ID)
        // Port of: update_stars_positions(&star_list_B, numMatched, cstars)
        // Uses each star's .id (original catalog index) to find its new projected coords.
        updateStarPositions(matchedCatStars, numMatched, newProjCat);

        // 4d: Recalculate TRANS from the SAME matched pairs (RECALC_YES = use all from start)
        // Port of: atRecalcTrans(numMatched, star_list_A, numMatched, star_list_B, ...)
        std::vector<int> idxA(numMatched), idxB(numMatched);
        for (int i = 0; i < numMatched; i++) { idxA[i] = i; idxB[i] = i; }

        GenericTrans newTrans;
        newTrans.order = 1;
        if (!matcher.iterTrans(numMatched, matchedImgStars, matchedCatStars,
                               idxA, idxB, true /* RECALC_YES */, newTrans)) {
            emit logMessage(tr("  Recalculation failed at trial %1.").arg(trial + 1));
            break;
        }

        // Cull arrays to prevent accumulation of rejected outliers that will induce NaN mappings downstream
        std::vector<MatchStar> culledImg(newTrans.nr);
        std::vector<MatchStar> culledCat(newTrans.nr);
        for (int i = 0; i < newTrans.nr; i++) {
            culledImg[i] = matchedImgStars[idxA[i]];
            culledCat[i] = matchedCatStars[idxB[i]];
        }
        matchedImgStars = culledImg;
        matchedCatStars = culledCat;

        transOut = newTrans;
        // Update numMatched to survivors after sigma clipping
        numMatched = transOut.nr;
        conv = std::sqrt(transOut.x00 * transOut.x00 + transOut.y00 * transOut.y00);

        emit logMessage(tr("  Trial %1: RA=%2 Dec=%3, offset=%4 arcsec, nr=%5")
                        .arg(trial + 1).arg(ra0, 0, 'f', 6).arg(dec0, 0, 'f', 6)
                        .arg(conv, 0, 'f', 3).arg(transOut.nr));
    }

    if (conv <= CONV_TOLERANCE)
        emit logMessage(tr("  Converged: offset=%1 arcsec").arg(conv, 0, 'f', 4));
    else
        emit logMessage(tr("  No full convergence (offset=%1 arcsec), using best solution").arg(conv, 0, 'f', 4));

    // If parity-flip was used (image Y was negated), convert TRANS back to original
    // image coordinates by negating the x01 and y01 terms.
    // Derivation: y_flip = -y_orig, so if TRANS was fit in flip-space:
    //   x' = x00 + x10*x + x01*y_flip  =  x00 + x10*x + x01*(-y_orig)
    // In original space this becomes: x01_orig = -x01_flip (and same for y01).
    if (parityFlipped) {
        transOut.x01 = -transOut.x01;
        transOut.y01 = -transOut.y01;
        emit logMessage(tr("  Parity un-flip applied to TRANS (x01, y01 negated)."));
    }

    raOut  = ra0;
    decOut = dec0;
    return transOut.nr;
}

// ==========================================================================
// processSolving — Main pipeline after catalog received
// ==========================================================================
void NativePlateSolver::processSolving(const std::vector<MatchStar>& catStars,
                                        const ImageBuffer&             image,
                                        const std::vector<CatalogStar>& catalogStars,
                                        double raHint, double decHint,
                                        double pixelScale)
{
    if (catStars.size() < 10) {
        NativeSolveResult res;
        res.errorMsg = tr("Not enough catalog stars found (%1).").arg(catStars.size());
        emit finished(res);
        return;
    }

    emit logMessage(tr("Detecting Image Stars..."));
    StarDetector detector;
    detector.setMaxStars(500);
    std::vector<DetectedStar> detected = detector.detect(image);
    emit logMessage(tr("Detected %1 stars in image.").arg(detected.size()));

    if (detected.size() < 5) {
        NativeSolveResult res;
        res.errorMsg = tr("Not enough image stars detected (%1).").arg(detected.size());
        emit finished(res);
        return;
    }

    // Center and flip Y — image stars in centered coordinates
    double imgCenterX = image.width()  * 0.5;
    double imgCenterY = image.height() * 0.5;

    std::vector<MatchStar> imgMatchStars;
    for(const auto& s : detected) {
        MatchStar ms;
        ms.x = s.x - imgCenterX;               // center X
        ms.y = s.y - imgCenterY;               // center Y (match FITS raw memory orientation logic)
        ms.mag = -2.5 * std::log10(std::max(s.flux, 1.0)); // Instrumental magnitude
        imgMatchStars.push_back(ms);
    }

    // Log catalog magnitude range to diagnose bad catalog queries
    if (!catStars.empty()) {
        double magMin = catStars[0].mag, magMax = catStars[0].mag;
        int nBadMag = 0;
        for (const auto& s : catStars) {
            if (s.mag <= 0 || std::isnan(s.mag)) { nBadMag++; continue; }
            magMin = std::min(magMin, s.mag);
            magMax = std::max(magMax, s.mag);
        }
        emit logMessage(tr("Catalog mag range: %1 – %2 (%3 stars, %4 with bad mag)")
                        .arg(magMin, 0, 'f', 1).arg(magMax, 0, 'f', 1)
                        .arg((int)catStars.size()).arg(nBadMag));
    }

    // Run matchCatalog
    GenericTrans bestTrans;
    double finalRA, finalDec;

    int result = matchCatalog(imgMatchStars, (int)imgMatchStars.size(),
                              catStars, pixelScale,
                              raHint, decHint,
                              bestTrans, finalRA, finalDec);

    NativeSolveResult res;
    res.success = (result > 0);
    res.transform = bestTrans;
    
    if (res.success) {
        emit logMessage(tr("Match Success! Computing WCS..."));

        // WCS: After convergence, CRVAL = converged center, CD = trans coeffs
        if (WcsSolver::computeWcs(bestTrans, finalRA, finalDec,
                                  image.width(), image.height(),
                                  res.crpix1, res.crpix2,
                                  res.crval1, res.crval2, res.cd)) {
            emit logMessage(tr("WCS computed: CRPIX=(%1, %2) CRVAL=(%3, %4)")
                           .arg(res.crpix1).arg(res.crpix2)
                           .arg(res.crval1).arg(res.crval2));
            
            double solvedScale = std::sqrt(res.cd[0][0]*res.cd[0][0] + res.cd[1][0]*res.cd[1][0]) * 3600.0;
            emit logMessage(tr("Solved pixel scale: %1 arcsec/px").arg(solvedScale, 0, 'f', 3));
        } else {
             res.errorMsg = tr("WCS Computation failed (Singular Matrix)");
             res.success = false;
        }
    } else {
        res.errorMsg = tr("Matching failed. No valid solution found.");
    }

    res.catalogStars = catalogStars;
    emit finished(res);
}

void NativePlateSolver::onCatalogReceived(const std::vector<MatchStar>& catalogStars) {
    emit logMessage(tr("Catalog received. Found %1 stars.").arg(catalogStars.size()));

    // Capture member data by value so the background thread does not access
    // member variables through `this` — the QObject may be destroyed (or its
    // parent dialog hidden/closed) while the thread is still running.
    ImageBuffer              imageSnapshot   = m_image;
    double                   pixelScale      = m_pixelScale;
    double                   raHint          = m_raHint;
    double                   decHint         = m_decHint;
    std::vector<CatalogStar> rawCatalogStars = m_catalogStars;

    (void)QtConcurrent::run([this,
                              catalogStars,
                              imageSnapshot   = std::move(imageSnapshot),
                              rawCatalogStars = std::move(rawCatalogStars),
                              pixelScale,
                              raHint,
                              decHint]() mutable {
        processSolving(catalogStars, imageSnapshot, rawCatalogStars, raHint, decHint, pixelScale);
    });
}

void NativePlateSolver::onCatalogError(const QString& msg) {
    NativeSolveResult res;
    res.errorMsg = msg;
    emit finished(res);
}
