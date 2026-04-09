/**
 * @file WCSUtils.cpp
 *
 * Utility functions for World Coordinate System (WCS) transformations.
 *
 * Provides conversions between pixel coordinates and celestial coordinates
 * (RA/Dec) using the FITS TAN (gnomonic) projection, CD matrix algebra,
 * and SIP distortion polynomials.
 *
 * References:
 *   - FITS WCS Paper I   (Greisen & Calabretta 2002, A&A 395, 1061)
 *   - FITS WCS Paper II  (Calabretta & Greisen 2002, A&A 395, 1077)
 *   - SIP Convention     (Shupe et al. 2005, ASPC 347, 491)
 */

#include "WCSUtils.h"
#include <cmath>
#include <QDebug>

// ---------------------------------------------------------------------------
// Angular conversion constants
// ---------------------------------------------------------------------------
static const double DEG_TO_RAD = M_PI / 180.0;
static const double RAD_TO_DEG = 180.0 / M_PI;

// ===========================================================================
// CD / CDELT / PC matrix conversions
// ===========================================================================

/**
 * Convert CDELT + PC matrix representation to a CD matrix.
 *
 * The FITS standard allows the coordinate transformation to be expressed as
 *   CD_i_j = CDELT_i * PC_i_j
 * This helper performs that multiplication for a 2x2 case.
 */
void WCSUtils::cdeltPcToCD(double cdelt1, double cdelt2,
                           double pc1_1, double pc1_2,
                           double pc2_1, double pc2_2,
                           double& cd1_1, double& cd1_2,
                           double& cd2_1, double& cd2_2)
{
    cd1_1 = pc1_1 * cdelt1;
    cd1_2 = pc1_2 * cdelt1;
    cd2_1 = pc2_1 * cdelt2;
    cd2_2 = pc2_2 * cdelt2;
}

/**
 * Decompose a CD matrix into CDELT values and a scalar CROTA2 rotation angle.
 *
 * The per-axis pixel scales are recovered as the column norms of the CD
 * matrix.  The sign of CDELT1 is determined by the handedness of the
 * coordinate system (det(CD)).  The rotation angle is averaged from both
 * matrix columns for robustness against minor skew.
 */
void WCSUtils::cdToCdeltCrota(double cd1_1, double cd1_2,
                              double cd2_1, double cd2_2,
                              double& cdelt1, double& cdelt2,
                              double& crota2)
{
    // Compute per-axis scales (RA scale is conventionally negative)
    cdelt1 = -std::sqrt(cd1_1 * cd1_1 + cd2_1 * cd2_1);
    cdelt2 =  std::sqrt(cd1_2 * cd1_2 + cd2_2 * cd2_2);

    // Correct sign based on coordinate-system handedness
    double det = cd1_1 * cd2_2 - cd1_2 * cd2_1;
    if (det > 0) {
        cdelt1 = -cdelt1;
    }

    // Average rotation angle from both matrix columns
    double rota1 = std::atan2(cd2_1, cd1_1) * RAD_TO_DEG;
    double rota2 = std::atan2(-cd1_2, cd2_2) * RAD_TO_DEG;
    crota2 = (rota1 + rota2) / 2.0;

    // Normalize to [0, 360)
    while (crota2 < 0.0)    crota2 += 360.0;
    while (crota2 >= 360.0) crota2 -= 360.0;
}

// ===========================================================================
// WCS validity and derived quantities
// ===========================================================================

/**
 * Determine whether the metadata contains a usable WCS solution.
 *
 * A valid WCS requires:
 *   1. A non-degenerate CD matrix (non-zero determinant).
 *   2. At least one non-zero CRPIX component.
 *
 * Note: RA = 0 and Dec = 0 are valid astronomical coordinates (the vernal
 * equinox on the celestial equator), so zero values are explicitly allowed.
 */
bool WCSUtils::hasValidWCS(const Metadata& meta)
{
    // A non-degenerate CD matrix is required (determinant must be non-zero).
    // RA = 0 and Dec = 0 are valid sky coordinates and must not be rejected.
    // CRPIX at (0,0) is also valid (e.g. edge-referenced or 0-indexed solutions).
    double det = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    return std::abs(det) > 1e-20;
}

/**
 * Compute the average pixel scale in arcseconds per pixel.
 *
 * The scale along each axis is the norm of the corresponding CD matrix
 * column, converted from degrees to arcseconds.
 */
double WCSUtils::pixelScale(const Metadata& meta)
{
    if (!hasValidWCS(meta)) return 0.0;

    double scale1 = std::sqrt(meta.cd1_1 * meta.cd1_1 + meta.cd2_1 * meta.cd2_1);
    double scale2 = std::sqrt(meta.cd1_2 * meta.cd1_2 + meta.cd2_2 * meta.cd2_2);
    double avgScale = (scale1 + scale2) / 2.0;

    return avgScale * 3600.0;
}

/**
 * Compute the image rotation angle (degrees) from the CD matrix.
 *
 * This returns the rotation of the pixel X-axis relative to celestial North,
 * normalized to [0, 360).
 */
double WCSUtils::imageRotation(const Metadata& meta)
{
    if (!hasValidWCS(meta)) return 0.0;

    double crota = std::atan2(meta.cd2_1, meta.cd1_1) * RAD_TO_DEG;

    while (crota < 0.0)    crota += 360.0;
    while (crota >= 360.0) crota -= 360.0;

    return crota;
}

/**
 * Compute the position angle (East of North) in degrees.
 *
 * The internal CD matrix has its Y-axis terms (CD1_2, CD2_2) negated to
 * accommodate the top-down memory layout produced by AstapSolver.  This
 * method restores the standard FITS signs before computing the angle so
 * that the result matches external conventions (e.g. hips2fits).
 *
 * The returned angle is normalized to (-180, 180].
 */
double WCSUtils::positionAngle(const Metadata& meta)
{
    if (!hasValidWCS(meta)) return 0.0;

    // Restore standard FITS Y-axis signs
    double std_cd1_2 = -meta.cd1_2;
    double std_cd2_2 = -meta.cd2_2;

    double pa = std::atan2(std_cd1_2, std_cd2_2) * RAD_TO_DEG;

    // Normalize to (-180, 180]
    while (pa >  180.0) pa -= 360.0;
    while (pa <= -180.0) pa += 360.0;

    return pa;
}

/**
 * Determine whether the image parity is flipped (mirrored).
 *
 * Standard astronomical orientation (East-left) normally yields det(CD) < 0
 * in FITS.  Because the internal CD matrix has negated Y-axis terms, the
 * sign convention is inverted: det > 0 is normal, det < 0 is mirrored.
 */
bool WCSUtils::isParityFlipped(const Metadata& meta)
{
    if (!hasValidWCS(meta)) return false;

    double det = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    return det < 0.0;
}

// ===========================================================================
// TAN (gnomonic) projection and deprojection
// ===========================================================================

/**
 * Forward gnomonic (TAN) projection: celestial (RA, Dec) to tangent plane
 * (xi, eta) in degrees.
 *
 * @param ra, dec   Input celestial coordinates (degrees).
 * @param ra0, dec0 Tangent-point (projection centre) coordinates (degrees).
 * @param xi, eta   Output tangent-plane offsets (degrees).
 * @return false if the point is more than ~90 degrees from the tangent point.
 */
bool WCSUtils::tanProject(double ra, double dec, double ra0, double dec0,
                          double& xi, double& eta)
{
    double ra_rad   = ra   * DEG_TO_RAD;
    double dec_rad  = dec  * DEG_TO_RAD;
    double ra0_rad  = ra0  * DEG_TO_RAD;
    double dec0_rad = dec0 * DEG_TO_RAD;

    double cosD  = std::cos(dec_rad);
    double sinD  = std::sin(dec_rad);
    double cosD0 = std::cos(dec0_rad);
    double sinD0 = std::sin(dec0_rad);
    double cosDR = std::cos(ra_rad - ra0_rad);
    double sinDR = std::sin(ra_rad - ra0_rad);

    double denom = sinD * sinD0 + cosD * cosD0 * cosDR;

    if (denom <= 1e-10) {
        xi  = 0.0;
        eta = 0.0;
        return false;
    }

    xi  = (cosD * sinDR) / denom * RAD_TO_DEG;
    eta = (sinD * cosD0 - cosD * sinD0 * cosDR) / denom * RAD_TO_DEG;

    return true;
}

/**
 * Inverse gnomonic (TAN) deprojection: tangent plane (xi, eta) in degrees
 * to celestial (RA, Dec) in degrees.
 *
 * @param xi, eta   Input tangent-plane offsets (degrees).
 * @param ra0, dec0 Tangent-point (projection centre) coordinates (degrees).
 * @param ra, dec   Output celestial coordinates (degrees, RA in [0, 360)).
 */
void WCSUtils::tanDeproject(double xi, double eta, double ra0, double dec0,
                            double& ra, double& dec)
{
    double xi_rad   = xi   * DEG_TO_RAD;
    double eta_rad  = eta  * DEG_TO_RAD;
    double ra0_rad  = ra0  * DEG_TO_RAD;
    double dec0_rad = dec0 * DEG_TO_RAD;

    double cosD0 = std::cos(dec0_rad);
    double sinD0 = std::sin(dec0_rad);

    double rho  = std::sqrt(xi_rad * xi_rad + eta_rad * eta_rad);
    double c    = std::atan(rho);
    double cosC = std::cos(c);
    double sinC = std::sin(c);

    // Compute declination
    double dec_rad;
    if (rho < 1e-10) {
        dec_rad = dec0_rad;
    } else {
        dec_rad = std::asin(cosC * sinD0 + (eta_rad * sinC * cosD0) / rho);
    }

    // Compute right ascension
    double ra_rad;
    if (std::abs(cosD0) < 1e-10) {
        // Special case: tangent point at a celestial pole
        ra_rad = ra0_rad + std::atan2(xi_rad, -eta_rad * sinD0 / std::abs(sinD0));
    } else {
        ra_rad = ra0_rad + std::atan2(xi_rad * sinC,
                                      rho * cosD0 * cosC - eta_rad * sinD0 * sinC);
    }

    ra  = ra_rad  * RAD_TO_DEG;
    dec = dec_rad * RAD_TO_DEG;

    // Normalize RA to [0, 360)
    while (ra < 0.0)    ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
}

// ===========================================================================
// SIP distortion polynomials
// ===========================================================================

/**
 * Look up a single SIP coefficient from the metadata map.
 *
 * @param prefix  One of "A_", "B_", "AP_", "BP_".
 * @param i, j    Polynomial powers.
 * @return Coefficient value, or 0.0 if absent.
 */
double WCSUtils::getSIPCoeff(const Metadata& meta,
                             const QString& prefix, int i, int j)
{
    QString key = QString("%1%2_%3").arg(prefix).arg(i).arg(j);
    return meta.sipCoeffs.value(key, 0.0);
}

/**
 * Evaluate the forward SIP distortion correction (A and B polynomials).
 *
 * Given relative pixel offsets (u, v), compute the additive distortion
 * (du, dv) so that the corrected position is (u + du, v + dv).
 */
void WCSUtils::applySIP(const Metadata& meta, double u, double v,
                        double& du, double& dv)
{
    du = 0.0;
    dv = 0.0;

    int orderA = meta.sipOrderA;
    int orderB = meta.sipOrderB;
    if (orderA <= 0 && orderB <= 0) return;

    // Evaluate A polynomial: du = sum_ij( A_i_j * u^i * v^j )
    for (int i = 0; i <= orderA; ++i) {
        for (int j = 0; j <= orderA - i; ++j) {
            if (i == 0 && j == 0) continue;
            double coeff = getSIPCoeff(meta, "A_", i, j);
            double ui = (i == 0) ? 1.0 : std::pow(u, i);
            double vj = (j == 0) ? 1.0 : std::pow(v, j);
            du += coeff * ui * vj;
        }
    }

    // Evaluate B polynomial: dv = sum_ij( B_i_j * u^i * v^j )
    for (int i = 0; i <= orderB; ++i) {
        for (int j = 0; j <= orderB - i; ++j) {
            if (i == 0 && j == 0) continue;
            double coeff = getSIPCoeff(meta, "B_", i, j);
            double ui = (i == 0) ? 1.0 : std::pow(u, i);
            double vj = (j == 0) ? 1.0 : std::pow(v, j);
            dv += coeff * ui * vj;
        }
    }
}

/**
 * Evaluate the inverse SIP distortion correction (AP / BP polynomials),
 * or solve iteratively if inverse coefficients are not available.
 *
 * Given distorted intermediate coordinates (u, v), recover the undistorted
 * pixel offsets (u0, v0).
 */
void WCSUtils::applySIPInverse(const Metadata& meta, double u, double v,
                               double& u0, double& v0)
{
    // --- Path 1: use stored inverse coefficients (AP, BP) if present ---------
    if (meta.sipOrderAP > 0 || meta.sipOrderBP > 0) {
        double du = 0.0;
        double dv = 0.0;

        for (int i = 0; i <= meta.sipOrderAP; ++i) {
            for (int j = 0; j <= meta.sipOrderAP - i; ++j) {
                if (i == 0 && j == 0) continue;
                double coeff = getSIPCoeff(meta, "AP_", i, j);
                double ui = (i == 0) ? 1.0 : std::pow(u, i);
                double vj = (j == 0) ? 1.0 : std::pow(v, j);
                du += coeff * ui * vj;
            }
        }

        for (int i = 0; i <= meta.sipOrderBP; ++i) {
            for (int j = 0; j <= meta.sipOrderBP - i; ++j) {
                if (i == 0 && j == 0) continue;
                double coeff = getSIPCoeff(meta, "BP_", i, j);
                double ui = (i == 0) ? 1.0 : std::pow(u, i);
                double vj = (j == 0) ? 1.0 : std::pow(v, j);
                dv += coeff * ui * vj;
            }
        }

        u0 = u + du;
        v0 = v + dv;
        return;
    }

    // --- Path 2: fixed-point iteration to invert forward SIP -----------------
    u0 = u;
    v0 = v;

    for (int iter = 0; iter < 10; ++iter) {
        double du, dv;
        applySIP(meta, u0, v0, du, dv);

        double u_pred = u0 + du;
        double v_pred = v0 + dv;

        u0 = u - du;
        v0 = v - dv;

        if (std::abs(u_pred - u) < 1e-10 && std::abs(v_pred - v) < 1e-10) {
            break;
        }
    }
}

// ===========================================================================
// High-level pixel <-> world coordinate transformations
// ===========================================================================

/**
 * Convert 0-indexed pixel coordinates to celestial (RA, Dec) coordinates.
 *
 * The transformation chain is:
 *   pixel -> relative offset (u, v) -> SIP correction -> CD matrix
 *   -> intermediate world coords (xi, eta) -> TAN deprojection -> (RA, Dec)
 *
 * FITS CRPIX is 1-indexed; the +1 offset accounts for the 0-indexed pixel
 * convention used internally.
 */
bool WCSUtils::pixelToWorld(const Metadata& meta, double px, double py,
                            double& ra, double& dec)
{
    if (!hasValidWCS(meta)) {
        ra  = 0.0;
        dec = 0.0;
        return false;
    }

    // Relative pixel coordinates (0-indexed to CRPIX, with FITS 0.5-pixel center offset)
    double u = px - meta.crpix1 + 0.5;
    double v = py - meta.crpix2 + 0.5;

    // Apply forward SIP distortion if present
    if (meta.sipOrderA > 0 || meta.sipOrderB > 0) {
        double du, dv;
        applySIP(meta, u, v, du, dv);
        u += du;
        v += dv;
    }

    // Apply CD matrix to obtain intermediate world coordinates (degrees)
    double xi  = meta.cd1_1 * u + meta.cd1_2 * v;
    double eta = meta.cd2_1 * u + meta.cd2_2 * v;

    // Deproject from tangent plane to celestial sphere
    tanDeproject(xi, eta, meta.ra, meta.dec, ra, dec);

    return true;
}

/**
 * Convert celestial (RA, Dec) coordinates to 0-indexed pixel coordinates.
 *
 * The transformation chain is:
 *   (RA, Dec) -> TAN projection -> intermediate world coords (xi, eta)
 *   -> inverse CD matrix -> inverse SIP -> relative offset (u, v) -> pixel
 */
bool WCSUtils::worldToPixel(const Metadata& meta, double ra, double dec,
                            double& px, double& py)
{
    if (!hasValidWCS(meta)) {
        px = 0.0;
        py = 0.0;
        return false;
    }

    // Project celestial coordinates onto the tangent plane
    double xi, eta;
    if (!tanProject(ra, dec, meta.ra, meta.dec, xi, eta)) {
        px = 0.0;
        py = 0.0;
        return false;
    }

    // Invert the CD matrix: [u, v] = CD^-1 * [xi, eta]
    double det = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    if (std::abs(det) < 1e-20) {
        px = 0.0;
        py = 0.0;
        return false;
    }

    double u = ( meta.cd2_2 * xi - meta.cd1_2 * eta) / det;
    double v = (-meta.cd2_1 * xi + meta.cd1_1 * eta) / det;

    // Apply inverse SIP distortion if any SIP data is present
    if (meta.sipOrderA > 0 || meta.sipOrderB > 0 ||
        meta.sipOrderAP > 0 || meta.sipOrderBP > 0)
    {
        double u0, v0;
        applySIPInverse(meta, u, v, u0, v0);
        u = u0;
        v = v0;
    }

    // Convert relative offset back to 0-indexed pixel coordinates
    px = u + meta.crpix1 - 0.5;
    py = v + meta.crpix2 - 0.5;

    return true;
}

// ===========================================================================
// Field geometry helpers
// ===========================================================================

/**
 * Compute the celestial coordinates of the image centre.
 */
bool WCSUtils::getFieldCenter(const Metadata& meta, int width, int height,
                              double& ra, double& dec)
{
    return pixelToWorld(meta, width / 2.0, height / 2.0, ra, dec);
}

/**
 * Estimate the field of view in degrees along each image axis.
 *
 * This is an approximate calculation based on the average pixel scale and
 * does not account for distortion or projection curvature across the field.
 */
bool WCSUtils::getFieldOfView(const Metadata& meta, int width, int height,
                              double& fovX, double& fovY)
{
    if (!hasValidWCS(meta)) {
        fovX = 0.0;
        fovY = 0.0;
        return false;
    }

    double scale = pixelScale(meta);       // arcsec / pixel
    fovX = width  * scale / 3600.0;        // degrees
    fovY = height * scale / 3600.0;        // degrees

    return true;
}

// ===========================================================================
// Astrometric registration helpers
// ===========================================================================

/**
 * Compute a 3x3 homography that maps source pixel coords to reference
 * pixel coords, by projecting N sample points through both WCS solutions.
 *
 *   1. For each sample pixel (x,y) in the source image, call
 *      pixelToWorld to get (RA, Dec) via the source WCS.
 *   2. Call worldToPixel to map that (RA, Dec) into the reference
 *      image's pixel coordinates.
 *   3. Solve for the 3x3 homography from the point correspondences
 *      using the Direct Linear Transform (DLT).
 */
bool WCSUtils::computeHomographyFromWCS(
    const Metadata& srcMeta, int srcW, int srcH,
    const Metadata& refMeta, int refW, int refH,
    double H[3][3])
{
    Q_UNUSED(refW);
    Q_UNUSED(refH);

    if (!hasValidWCS(srcMeta) || !hasValidWCS(refMeta))
        return false;

    // Sample points: 4 corners + 4 edge midpoints + center = 9 points
    const double pts[][2] = {
        {0.0,              0.0             },  // top-left
        {(double)srcW,     0.0             },  // top-right
        {0.0,              (double)srcH    },  // bottom-left
        {(double)srcW,     (double)srcH    },  // bottom-right
        {srcW * 0.5,       0.0             },  // top-mid
        {srcW * 0.5,       (double)srcH    },  // bottom-mid
        {0.0,              srcH * 0.5      },  // left-mid
        {(double)srcW,     srcH * 0.5      },  // right-mid
        {srcW * 0.5,       srcH * 0.5      },  // center
    };
    constexpr int N = 9;

    // Collect (source pixel) -> (reference pixel) correspondences
    double srcPts[N][2], dstPts[N][2];
    int    nValid = 0;

    for (int i = 0; i < N; ++i) {
        double ra, dec;
        if (!pixelToWorld(srcMeta, pts[i][0], pts[i][1], ra, dec))
            continue;
        double rx, ry;
        if (!worldToPixel(refMeta, ra, dec, rx, ry))
            continue;
        srcPts[nValid][0] = pts[i][0];
        srcPts[nValid][1] = pts[i][1];
        dstPts[nValid][0] = rx;
        dstPts[nValid][1] = ry;
        nValid++;
    }

    if (nValid < 4)
        return false;

    // --- Solve for H using DLT (Ax = 0) ---
    // For each correspondence (sx,sy) -> (dx,dy) we get two equations:
    //   -sx*w  -sy*w  -w   0     0    0  dx*sx*w  dx*sy*w  dx*w
    //    0      0      0  -sx*w -sy*w -w  dy*sx*w  dy*sy*w  dy*w
    // Here w = 1.
    // We use a simplified least-squares approach: build the 8x8 normal
    // equations (fixing H[2][2]=1) and solve via Gauss elimination.

    double A[8][9] = {};  // 8 unknowns, column 8 = RHS

    for (int p = 0; p < nValid; ++p) {
        double sx = srcPts[p][0], sy = srcPts[p][1];
        double dx = dstPts[p][0], dy = dstPts[p][1];

        // Row for x equation: h00*sx + h01*sy + h02 - h20*sx*dx - h21*sy*dx = dx
        double rowX[8] = {sx, sy, 1.0, 0.0, 0.0, 0.0, -sx*dx, -sy*dx};
        // Row for y equation: h10*sx + h11*sy + h12 - h20*sx*dy - h21*sy*dy = dy
        double rowY[8] = {0.0, 0.0, 0.0, sx, sy, 1.0, -sx*dy, -sy*dy};

        // Accumulate A^T * A and A^T * b
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) {
                A[i][j] += rowX[i] * rowX[j] + rowY[i] * rowY[j];
            }
            A[i][8] += rowX[i] * dx + rowY[i] * dy;
        }
    }

    // Gauss elimination with partial pivoting
    for (int col = 0; col < 8; ++col) {
        // Find pivot
        int pivotRow = col;
        double pivotVal = std::abs(A[col][col]);
        for (int row = col + 1; row < 8; ++row) {
            if (std::abs(A[row][col]) > pivotVal) {
                pivotVal = std::abs(A[row][col]);
                pivotRow = row;
            }
        }
        if (pivotVal < 1e-20)
            return false;  // Singular

        // Swap rows
        if (pivotRow != col) {
            for (int j = 0; j < 9; ++j)
                std::swap(A[col][j], A[pivotRow][j]);
        }

        // Eliminate
        for (int row = col + 1; row < 8; ++row) {
            double factor = A[row][col] / A[col][col];
            for (int j = col; j < 9; ++j)
                A[row][j] -= factor * A[col][j];
        }
    }

    // Back-substitution
    double h[8];
    for (int i = 7; i >= 0; --i) {
        h[i] = A[i][8];
        for (int j = i + 1; j < 8; ++j)
            h[i] -= A[i][j] * h[j];
        h[i] /= A[i][i];
    }

    H[0][0] = h[0];  H[0][1] = h[1];  H[0][2] = h[2];
    H[1][0] = h[3];  H[1][1] = h[4];  H[1][2] = h[5];
    H[2][0] = h[6];  H[2][1] = h[7];  H[2][2] = 1.0;

    return true;
}

/**
 * Compute the spherical centre-of-gravity of a set of (RA, Dec) pairs.
 *
 * The algorithm converts each direction to a unit Cartesian vector,
 * averages the vectors, and converts back.  This automatically handles
 * the RA=0/360 wrap-around ambiguity.
 */
bool WCSUtils::computeSequenceCOG(
    const std::vector<std::pair<double,double>>& coords,
    double& ra, double& dec)
{
    if (coords.empty())
        return false;

    double cx = 0.0, cy = 0.0, cz = 0.0;

    for (const auto& [r, d] : coords) {
        double rr = r * DEG_TO_RAD;
        double dd = d * DEG_TO_RAD;
        cx += std::cos(dd) * std::cos(rr);
        cy += std::cos(dd) * std::sin(rr);
        cz += std::sin(dd);
    }

    double n = static_cast<double>(coords.size());
    cx /= n;
    cy /= n;
    cz /= n;

    double norm = std::sqrt(cx * cx + cy * cy + cz * cz);
    if (norm < 1e-15)
        return false;

    dec = std::asin(cz / norm) * RAD_TO_DEG;
    ra  = std::atan2(cy, cx) * RAD_TO_DEG;

    // Normalise RA to [0, 360)
    while (ra < 0.0)    ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;

    return true;
}