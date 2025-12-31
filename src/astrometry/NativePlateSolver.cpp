#include "NativePlateSolver.h"
#include "../photometry/CatalogClient.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

NativePlateSolver::NativePlateSolver(QObject* parent) 
    : QObject(parent), m_simbad(nullptr), m_nam(new QNetworkAccessManager(this))
{
}

void NativePlateSolver::solve(const ImageBuffer& image, double raHint, double decHint, double radiusDeg, double pixelScale) {
    m_image = image; // Copy image for thread safety
    
    m_raHint = raHint;
    m_decHint = decHint;
    m_radius = radiusDeg;
    m_pixelScale = pixelScale;

    emit logMessage(tr("Starting Native Solver. Center: %1, %2 Radius: %3 deg").arg(raHint).arg(decHint).arg(radiusDeg));
    
    fetchCatalog();
}

void NativePlateSolver::fetchCatalog() {
    emit logMessage(tr("Querying Catalog (VizieR)..."));
    
    // Check if CatalogClient is suitable. It queries VizieR.
    // We can instantiate it here.
    CatalogClient* client = new CatalogClient(this); // Parented to this so it dies with solver
    
    // Connect signals
    connect(client, &CatalogClient::catalogReady, this, [this, client](const std::vector<CatalogStar>& stars) {
        m_catalogStars = stars;
        std::vector<MatchStar> ms;
        for(const auto& s : stars) {
            MatchStar m;
            m.id = 0;
            m.index = 0;
            m.x = s.ra;
            m.y = s.dec;
            m.mag = s.magV;
            m.match_id = -1;
            ms.push_back(m);
        }
        this->onCatalogReceived(ms);
        client->deleteLater();
    });
    
    connect(client, &CatalogClient::errorOccurred, this, [this, client](const QString& err) {
        emit logMessage(tr("Catalog Error: %1").arg(err));
        onCatalogError(err);
        client->deleteLater();
    });

    client->queryGaiaDR3(m_raHint, m_decHint, m_radius);
}

void NativePlateSolver::processSolving(const std::vector<MatchStar>& catStars) {
    if (catStars.size() < 10) {
        NativeSolveResult res;
        res.success = false;
        res.errorMsg = tr("Not enough catalog stars found.");
        emit finished(res);
        return;
    }

    emit logMessage(tr("Detecting Image Stars..."));
    StarDetector detector;
    detector.setMaxStars(500); // We only need bright ones for solving
    std::vector<DetectedStar> detected = detector.detect(m_image); // Default channel
    emit logMessage(tr("Detected %1 stars in image.").arg(detected.size()));

    if (detected.size() < 5) {
        NativeSolveResult res;
        res.success = false;
        res.errorMsg = tr("Not enough image stars detected.");
        emit finished(res);
        return;
    }

    // Convert Image Stars to MatchStar
    std::vector<MatchStar> imgMatchStars;
    for(const auto& s : detected) {
        MatchStar ms;
        ms.x = s.x;
        ms.y = s.y;
        ms.mag = -2.5 * std::log10(s.flux); // Instrumental Mag
        imgMatchStars.push_back(ms);
    }

    // Project Catalog Stars
    // Convert RA/DEC to Standard Coordinates (Xi, Eta) centered on raHint, decHint
    emit logMessage(tr("Projecting Catalog Stars..."));
    std::vector<MatchStar> projCatStars = projectCatalog(catStars);

    // Filter project stars only if field margin checks are required

    emit logMessage(tr("Matching Triangles..."));
    TriangleMatcher matcher;
    matcher.setMaxStars(30); // Use top 30 brightest stars
    GenericTrans trans;
    
    bool solved = matcher.solve(imgMatchStars, projCatStars, trans);
    
    NativeSolveResult res;
    res.success = solved;
    res.transform = trans;
    
    if (solved) {
        emit logMessage(tr("Match Success!"));
        
        // Calculate WCS
        // Use transform coefficients to derive CRPIX and CD matrix.
        // Transform maps (x_img, y_img) -> (Xi, Eta) relative to reference point.
        
        if (WcsSolver::computeWcs(trans, m_raHint, m_decHint, res.crpix1, res.crpix2, res.cd)) {
             res.crval1 = m_raHint;
             res.crval2 = m_decHint;
        } else {
             res.errorMsg = tr("WCS Computation failed (Singular Matrix)");
             res.success = false;
        }
    } else {
        res.errorMsg = tr("Matching failed. Pattern not found.");
    }

    res.catalogStars = m_catalogStars;
    emit finished(res);
}


std::vector<MatchStar> NativePlateSolver::projectCatalog(const std::vector<MatchStar>& catStars) const {
    // Project RA/Dec to Standard Coords (Xi, Eta) in Degrees
    // Gnomonic Projection (TAN)
    
    double a0 = m_raHint * DEG2RAD;
    double d0 = m_decHint * DEG2RAD;
    
    std::vector<MatchStar> projected;
    projected.reserve(catStars.size());
    
    double sin_d0 = std::sin(d0);
    double cos_d0 = std::cos(d0);
    
    for(const auto& s : catStars) {
        double a = s.x * DEG2RAD;
        double d = s.y * DEG2RAD;
        
        double H = std::sin(d) * sin_d0 + std::cos(d) * cos_d0 * std::cos(a - a0);
        
        double xi_rad = (std::cos(d) * std::sin(a - a0)) / H;
        double eta_rad = (std::sin(d) * cos_d0 - std::cos(d) * sin_d0 * std::cos(a - a0)) / H;
        
        MatchStar p = s;
        p.x = xi_rad * RAD2DEG;
        p.y = eta_rad * RAD2DEG;
        // p.mag unchanged
        
        projected.push_back(p);
    }
    
    return projected;
}

void NativePlateSolver::onCatalogReceived(const std::vector<MatchStar>& catalogStars) {
    emit logMessage(tr("Catalog received. Found %1 stars.").arg(catalogStars.size()));
    processSolving(catalogStars);
}

void NativePlateSolver::onCatalogError(const QString& msg) {
    NativeSolveResult res;
    res.success = false;
    res.errorMsg = msg;
    emit finished(res);
}
