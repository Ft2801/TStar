#include "NativePlateSolver.h"
#include "../photometry/CatalogClient.h"
#include <cmath>
#include <limits>
#include <QtConcurrent/QtConcurrent>
#include <QPointer>
#include <unordered_map>

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
    if (!image.isValid()) {
        NativeSolveResult res;
        res.errorMsg = tr("Invalid image buffer.");
        emit finished(res);
        return;
    }
    m_stop = false;

    if (!std::isfinite(raHint) || !std::isfinite(decHint) || !std::isfinite(radiusDeg) || radiusDeg <= 0.0) {
        NativeSolveResult res;
        res.errorMsg = tr("Invalid solve parameters (RA/Dec/Radius).");
        emit finished(res);
        return;
    }

    if (!std::isfinite(pixelScale) || pixelScale <= 0.0) {
        emit logMessage(tr("Pixel scale is invalid or missing; proceeding without strict scale constraints."));
        pixelScale = -1.0;
    }

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
        m_catalogStars.clear();
        m_catalogStars.reserve(stars.size());

        for (const auto& s : stars) {
            if (!std::isfinite(s.ra) || !std::isfinite(s.dec)) {
                continue;
            }
            m_catalogStars.push_back(s);
        }

        std::vector<MatchStar> ms;
        ms.reserve(m_catalogStars.size());
        for (const auto& s : m_catalogStars) {
            MatchStar m;
            m.id    = (int)ms.size();
            m.index = (int)ms.size();
            m.x     = s.ra;   // RA in degrees
            m.y     = s.dec;  // Dec in degrees
            m.mag   = std::isfinite(s.magV) ? s.magV : 99.0;
            m.match_id = -1;
            ms.push_back(m);
        }

        if (ms.size() < 10) {
            this->onCatalogError(tr("Catalog query returned too few valid stars (%1).").arg(ms.size()));
            client->deleteLater();
            return;
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
    if (numMatched <= 0 || newProjectedCat.empty()) return;

    // Build a map for O(1) lookup of new positions by star ID
    std::unordered_map<int, const MatchStar*> idMap;
    idMap.reserve(newProjectedCat.size());
    for (const auto& np : newProjectedCat) {
        idMap[np.id] = &np;
    }

    // For each matched catalog star, update its coordinates using the map
    const int safeCount = std::min(numMatched, static_cast<int>(matchedCatStars.size()));
    for (int i = 0; i < safeCount; i++) {
        auto it = idMap.find(matchedCatStars[i].id);
        if (it != idMap.end()) {
            matchedCatStars[i].x = it->second->x;
            matchedCatStars[i].y = it->second->y;
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
    QPointer<NativePlateSolver> safeThis(this);
    (void)nbImgStars; // Unused but kept for API consistency

    double minScale = -1.0, maxScale = -1.0;
    if (scale > 0) {
        double rangePct = 30.0;
        double a_factor = 1.0 + (rangePct / 100.0);
        double b_factor = 1.0 - (rangePct / 100.0);
        minScale = 1.0 / (scale * a_factor);
        maxScale = 1.0 / (scale * b_factor);
    }

    // === Step 1: Project catalog at initial center ===
    std::vector<MatchStar> projCat = projectCatalog(catStarsRaw, ra0, dec0);
    if (safeThis) {
        emit logMessage(tr("Projected %1 catalog stars at RA=%2 Dec=%3")
                        .arg(projCat.size()).arg(ra0, 0, 'f', 4).arg(dec0, 0, 'f', 4));
    }

    if ((int)projCat.size() < 10) return -1;

    // === Step 2: Triangle matching (new_star_match) — ONE TIME ONLY ===
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
    bool parityFlipped = false;

    auto logMatcherDiag = [&](int attempt) {
        if (safeThis) {
            emit logMessage(tr("    [Attempt %1 diag] maxVote=%2 validPairs=%3 nMatched=%4 stage=%5")
                            .arg(attempt)
                            .arg(matcher.lastMaxVote())
                            .arg(matcher.lastValidPairs())
                            .arg(matcher.lastNmatched())
                            .arg(matcher.lastFailStage()));
        }
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
        emit logMessage(tr("  Attempt 3 failed, retrying with wider triangle radius 0.003..."));
        logMatcherDiag(3);
        matcher.setMaxStars(60);
        matcher.setTriangleRadius(TRIANGLE_RADIUS_WIDE);
        matchOk = matcher.solve(imgStars, projCat, transOut, matchedImgStars, matchedCatStars, -1.0, -1.0);
    }

    if (!matchOk) {
        // Attempt 5: PARITY FLIP — negate image Y axis.
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

    if (safeThis) {
        emit logMessage(tr("Initial match: %1 pairs, offset=(%2, %3) arcsec")
                        .arg(matchedImgStars.size()).arg(transOut.x00, 0, 'f', 2).arg(transOut.y00, 0, 'f', 2));
    }

    // === Step 3: TRANS sanity and scale checks ===
    if (!checkTransSanity(transOut)) {
        if (safeThis) {
            emit logMessage(tr("Transform sanity check failed (|cos|≠|sin| by >%.1f arcsec/px).").arg(TRANS_SANITY_CHECK));
        }
        return -1;
    }
    if (!checkTransScale(transOut, minScale, maxScale)) {
        if (safeThis) emit logMessage(tr("Transform scale check failed."));
        return -1;
    }

    int numMatched = std::min((int)matchedImgStars.size(), (int)matchedCatStars.size());
    if (numMatched < AT_MATCH_STARTN_LINEAR) {
        if (safeThis) {
            emit logMessage(tr("Insufficient matched pairs after initial solve (%1).").arg(numMatched));
        }
        return -1;
    }

    // === Step 4: Convergence loop ===
    double conv = std::sqrt(transOut.x00 * transOut.x00 + transOut.y00 * transOut.y00);
    if (safeThis) {
        emit logMessage(tr("  Initial: offset=%1 arcsec, nr=%2")
                        .arg(conv, 0, 'f', 3).arg(transOut.nr));
    }

    for (int trial = 0; trial < MAX_CONVERGENCE_TRIALS && conv > CONV_TOLERANCE; trial++) {
        if (m_stop) return -1;

        // 4a: Compute new projection center via apply_match at image center (0, 0)
        double newRA, newDec;
        applyMatch(ra0, dec0, 0.0, 0.0, transOut, newRA, newDec);
        ra0 = newRA;
        dec0 = newDec;

        // 4b: Re-project catalog at new center
        std::vector<MatchStar> newProjCat = projectCatalog(catStarsRaw, ra0, dec0);
        if ((int)newProjCat.size() < AT_MATCH_STARTN_LINEAR) {
            if (safeThis) emit logMessage(tr("  Not enough stars after reprojection (%1).").arg(newProjCat.size()));
            break;
        }

        // 4c: Update positions of matched catalog stars from new projection (by ID)
        updateStarPositions(matchedCatStars, numMatched, newProjCat);

        // 4d: Recalculate TRANS from the SAME matched pairs (RECALC_YES)
        if ((int)matchedImgStars.size() < numMatched || (int)matchedCatStars.size() < numMatched) {
            if (safeThis) emit logMessage(tr("Matched pair vectors became inconsistent (A=%1, B=%2, need=%3).")
                                          .arg(matchedImgStars.size()).arg(matchedCatStars.size()).arg(numMatched));
            break;
        }

        std::vector<int> idxA(numMatched), idxB(numMatched);
        for (int i = 0; i < numMatched; i++) { idxA[i] = i; idxB[i] = i; }

        GenericTrans newTrans;
        newTrans.order = 1;
        if (!matcher.iterTrans(numMatched, matchedImgStars, matchedCatStars,
                               idxA, idxB, true /* RECALC_YES */, newTrans)) {
            if (safeThis) emit logMessage(tr("  Recalculation failed at trial %1.").arg(trial + 1));
            break;
        }

        // Cull arrays to prevent accumulation of rejected outliers
        const int requested = std::max(0, std::min(newTrans.nr,
                              std::min((int)idxA.size(), (int)idxB.size())));
        std::vector<MatchStar> culledImg;
        std::vector<MatchStar> culledCat;
        culledImg.reserve(requested);
        culledCat.reserve(requested);

        for (int i = 0; i < requested; i++) {
            const int aIdx = idxA[i];
            const int bIdx = idxB[i];
            if (aIdx < 0 || bIdx < 0 || aIdx >= (int)matchedImgStars.size() || bIdx >= (int)matchedCatStars.size()) {
                continue;
            }
            culledImg.push_back(matchedImgStars[aIdx]);
            culledCat.push_back(matchedCatStars[bIdx]);
        }

        if ((int)culledImg.size() < AT_MATCH_REQUIRE_LINEAR || (int)culledCat.size() < AT_MATCH_REQUIRE_LINEAR) {
            if (safeThis) emit logMessage(tr("Too few valid pairs after culling (%1).").arg(culledImg.size()));
            break;
        }

        matchedImgStars = culledImg;
        matchedCatStars = culledCat;

        transOut = newTrans;
        // Update numMatched to survivors after sigma clipping
        numMatched = std::min((int)transOut.nr, std::min((int)matchedImgStars.size(), (int)matchedCatStars.size()));

        if (numMatched <= 0) {
            if (safeThis) emit logMessage(tr("Recalculation resulted in flat match set."));
            break;
        }
        conv = std::sqrt(transOut.x00 * transOut.x00 + transOut.y00 * transOut.y00);

        if (safeThis) {
            emit logMessage(tr("  Trial %1: RA=%2 Dec=%3, offset=%4 arcsec, nr=%5")
                            .arg(trial + 1).arg(ra0, 0, 'f', 6).arg(dec0, 0, 'f', 6)
                            .arg(conv, 0, 'f', 3).arg(transOut.nr));
        }
    }

    if (safeThis) {
        if (conv <= CONV_TOLERANCE)
            emit logMessage(tr("  Converged: offset=%1 arcsec").arg(conv, 0, 'f', 4));
        else
            emit logMessage(tr("  No full convergence (offset=%1 arcsec), using best solution").arg(conv, 0, 'f', 4));
    }

    // If parity-flip was used (image Y was negated), convert TRANS back to original
    // image coordinates by negating the x01 and y01 terms.
    if (parityFlipped) {
        transOut.x01 = -transOut.x01;
        transOut.y01 = -transOut.y01;
        if (safeThis) emit logMessage(tr("  Parity un-flip applied to TRANS (x01, y01 negated)."));
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
    if (!image.isValid() || image.width() <= 0 || image.height() <= 0 || image.channels() <= 0) {
        NativeSolveResult res;
        res.errorMsg = tr("Invalid image snapshot for solving.");
        QPointer<NativePlateSolver> safeThis(this);
        if (safeThis) emit finished(res);
        return;
    }

    const std::vector<float>& pixels = image.data();
    const size_t expectedSize = static_cast<size_t>(image.width()) *
                                static_cast<size_t>(image.height()) *
                                static_cast<size_t>(image.channels());
    if (pixels.size() < expectedSize || !pixels.data()) {
        NativeSolveResult res;
        res.errorMsg = tr("Image data buffer is inconsistent.");
        QPointer<NativePlateSolver> safeThis(this);
        if (safeThis) emit finished(res);
        return;
    }

    if (catStars.size() < 10) {
        NativeSolveResult res;
        res.errorMsg = tr("Not enough catalog stars found (%1).").arg(catStars.size());
        QPointer<NativePlateSolver> safeThis(this);
        if (safeThis) emit finished(res);
        return;
    }

    QPointer<NativePlateSolver> safeThis(this);
    if (safeThis) emit logMessage(tr("Detecting Image Stars..."));
    StarDetector detector;
    if (m_stop) return;
    detector.setMaxStars(500);
    std::vector<DetectedStar> detected = detector.detect(image);
    if (m_stop) return;
    if (safeThis) emit logMessage(tr("Detected %1 stars in image.").arg(detected.size()));

    if (detected.size() < 8) {
        NativeSolveResult res;
        res.errorMsg = tr("Insufficient image stars detected for reliable solve (%1). "
                          "Run ASTAP solver externally after stacking.").arg(detected.size());
        if (safeThis) emit finished(res);
        return;
    }

    // Center and flip Y — image stars in centered coordinates
    double imgCenterX = image.width()  * 0.5;
    double imgCenterY = image.height() * 0.5;

    std::vector<MatchStar> imgMatchStars;
    imgMatchStars.reserve(detected.size());
    for(const auto& s : detected) {
        MatchStar ms;
        ms.id    = (int)imgMatchStars.size();
        ms.index = (int)imgMatchStars.size();
        ms.x = s.x - imgCenterX;
        ms.y = s.y - imgCenterY;
        ms.mag = -2.5 * std::log10(std::max(s.flux, 1.0)); // Instrumental magnitude
        ms.match_id = -1;
        imgMatchStars.push_back(ms);
    }

    // Log catalog magnitude range to diagnose bad catalog queries
    if (!catStars.empty()) {
        double magMin = std::numeric_limits<double>::infinity();
        double magMax = -std::numeric_limits<double>::infinity();
        int nBadMag = 0;
        int nGoodMag = 0;
        for (const auto& s : catStars) {
            if (!std::isfinite(s.mag) || s.mag <= 0.0) {
                nBadMag++;
                continue;
            }
            magMin = std::min(magMin, s.mag);
            magMax = std::max(magMax, s.mag);
            nGoodMag++;
        }
        if (safeThis) {
            if (nGoodMag > 0) {
                emit logMessage(tr("Catalog mag range: %1 - %2 (%3 stars, %4 with bad mag)")
                                .arg(magMin, 0, 'f', 1).arg(magMax, 0, 'f', 1)
                                .arg((int)catStars.size()).arg(nBadMag));
            } else {
                emit logMessage(tr("Catalog magnitudes unavailable (%1 stars, all invalid mags)")
                                .arg((int)catStars.size()));
            }
        }
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
        if (safeThis) emit logMessage(tr("Match Success! Computing WCS..."));

        // WCS: After convergence, CRVAL = converged center, CD = trans coeffs
        if (WcsSolver::computeWcs(bestTrans, finalRA, finalDec,
                                  image.width(), image.height(),
                                  res.crpix1, res.crpix2,
                                  res.crval1, res.crval2, res.cd)) {
            if (safeThis) {
                emit logMessage(tr("WCS computed: CRPIX=(%1, %2) CRVAL=(%3, %4)")
                               .arg(res.crpix1).arg(res.crpix2)
                               .arg(res.crval1).arg(res.crval2));

                double solvedScale = std::sqrt(res.cd[0][0]*res.cd[0][0] + res.cd[1][0]*res.cd[1][0]) * 3600.0;
                emit logMessage(tr("Solved pixel scale: %1 arcsec/px").arg(solvedScale, 0, 'f', 3));
            }
        } else {
             res.errorMsg = tr("WCS Computation failed (Singular Matrix)");
             res.success = false;
        }
    } else {
        res.errorMsg = tr("Matching failed. No valid solution found.");
    }

    res.catalogStars = catalogStars;
    if (safeThis) emit finished(res);
}

void NativePlateSolver::onCatalogReceived(const std::vector<MatchStar>& catalogStars) {
    emit logMessage(tr("Catalog received. Found %1 stars.").arg(catalogStars.size()));

    // Capture member data by value so processing uses a stable snapshot.
    ImageBuffer              imageSnapshot   = m_image;
    double                   pixelScale      = m_pixelScale;
    double                   raHint          = m_raHint;
    double                   decHint         = m_decHint;
    std::vector<CatalogStar> rawCatalogStars = m_catalogStars;

    QPointer<NativePlateSolver> safeThis(this);

    if (safeThis) {
        emit logMessage(tr("Starting native solve pipeline in background..."));
        (void)QtConcurrent::run([this, safeThis, catalogStars, imageSnapshot,
                                  rawCatalogStars, raHint, decHint, pixelScale]() {
            if (!safeThis) return;
            try {
                this->processSolving(catalogStars, imageSnapshot,
                                     rawCatalogStars, raHint, decHint, pixelScale);
            } catch (const std::exception& e) {
                NativeSolveResult res;
                res.errorMsg = QString("Native solver exception: %1").arg(e.what());
                if (safeThis) emit this->finished(res);
            } catch (...) {
                NativeSolveResult res;
                res.errorMsg = "Native solver: unknown exception.";
                if (safeThis) emit this->finished(res);
            }
        });
    }
}

void NativePlateSolver::onCatalogError(const QString& msg) {
    NativeSolveResult res;
    res.errorMsg = msg;
    emit finished(res);
}