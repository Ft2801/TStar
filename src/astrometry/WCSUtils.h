#ifndef WCSUTILS_H
#define WCSUTILS_H

// ============================================================================
// WCSUtils
//
// Utility class for World Coordinate System (WCS) transformations.
// Supports the TAN (gnomonic) projection with optional SIP distortion
// polynomials. Provides conversions between pixel coordinates and
// equatorial sky coordinates (RA, Dec).
// ============================================================================

#include "../ImageBuffer.h"
#include <vector>

class WCSUtils {
public:
    using Metadata = ImageBuffer::Metadata;

    // -- CD / CDELT / PC conversions --------------------------------------

    /** Converts CDELT + PC matrix representation to a CD matrix. */
    static void cdeltPcToCD(double cdelt1, double cdelt2,
                            double pc1_1, double pc1_2,
                            double pc2_1, double pc2_2,
                            double& cd1_1, double& cd1_2,
                            double& cd2_1, double& cd2_2);

    /** Converts a CD matrix to CDELT + CROTA2 representation. */
    static void cdToCdeltCrota(double cd1_1, double cd1_2,
                               double cd2_1, double cd2_2,
                               double& cdelt1, double& cdelt2,
                               double& crota2);

    // -- WCS validity and derived quantities ------------------------------

    /** Returns true if the metadata contains a valid, non-degenerate WCS. */
    static bool hasValidWCS(const Metadata& meta);

    /** Returns the average pixel scale in arcseconds per pixel. */
    static double pixelScale(const Metadata& meta);

    /** Returns the image rotation angle in degrees (0--360). */
    static double imageRotation(const Metadata& meta);

    /**
     * Returns the position angle of the image Y-axis, measured East of
     * North in degrees (CCW positive). This is the value expected by the
     * hips2fits rotation_angle parameter.
     * Computed as atan2(CD1_2, CD2_2) with restored standard FITS signs.
     */
    static double positionAngle(const Metadata& meta);

    /**
     * Returns true if the image has a mirrored (East-right) orientation.
     * Standard astronomical images have det(CD) < 0 (East-left).
     * In our internal coordinate convention (negated Y-axis CD terms),
     * det > 0 is normal and det < 0 indicates mirroring.
     */
    static bool isParityFlipped(const Metadata& meta);

    // -- Coordinate transformations ---------------------------------------

    /** Converts pixel coordinates (0-indexed) to world (RA, Dec). */
    static bool pixelToWorld(const Metadata& meta, double px, double py,
                             double& ra, double& dec);

    /** Converts world (RA, Dec) to pixel coordinates (0-indexed). */
    static bool worldToPixel(const Metadata& meta, double ra, double dec,
                             double& px, double& py);

    // -- SIP distortion ---------------------------------------------------

    /** Applies the forward SIP distortion polynomial. */
    static void applySIP(const Metadata& meta, double u, double v,
                         double& du, double& dv);

    /** Applies the inverse SIP distortion (direct or iterative). */
    static void applySIPInverse(const Metadata& meta, double u, double v,
                                double& u0, double& v0);

    // -- Field geometry ---------------------------------------------------

    /** Returns the (RA, Dec) of the image centre via pixelToWorld. */
    static bool getFieldCenter(const Metadata& meta, int width, int height,
                               double& ra, double& dec);

    /** Returns the field of view in degrees (X and Y axes). */
    static bool getFieldOfView(const Metadata& meta, int width, int height,
                               double& fovX, double& fovY);

    // -- Astrometric registration helpers ---------------------------------

    /**
     * Compute a homography matrix that maps pixel coordinates from
     * a source image (with its own WCS) into the coordinate system of
     * a reference image (with its own WCS), by mapping the four
     * corners through both WCS solutions.
     *
     * @param srcMeta   WCS metadata of the source image.
     * @param srcW      Source image width.
     * @param srcH      Source image height.
     * @param refMeta   WCS metadata of the reference image.
     * @param refW      Reference image width.
     * @param refH      Reference image height.
     * @param H         Output 3x3 homography (source -> reference pixels).
     * @return true on success.
     */
    static bool computeHomographyFromWCS(
        const Metadata& srcMeta, int srcW, int srcH,
        const Metadata& refMeta, int refW, int refH,
        double H[3][3]);

    /**
     * Compute the spherical center-of-gravity (mean direction) of a
     * set of (RA, Dec) coordinate pairs.  Handles RA wrap-around by
     * converting to Cartesian, averaging, and converting back.
     *
     * @param coords  Vector of (RA, Dec) pairs in degrees.
     * @param ra      Output mean RA in degrees.
     * @param dec     Output mean Dec in degrees.
     * @return true if the result is valid.
     */
    static bool computeSequenceCOG(
        const std::vector<std::pair<double,double>>& coords,
        double& ra, double& dec);

private:
    // -- TAN (gnomonic) projection helpers --------------------------------

    /** Forward gnomonic projection: (RA, Dec) -> tangent plane (xi, eta). */
    static bool tanProject(double ra, double dec, double ra0, double dec0,
                           double& xi, double& eta);

    /** Inverse gnomonic projection: tangent plane (xi, eta) -> (RA, Dec). */
    static void tanDeproject(double xi, double eta, double ra0, double dec0,
                             double& ra, double& dec);

    /** Retrieves a named SIP coefficient from the metadata map. */
    static double getSIPCoeff(const Metadata& meta,
                              const QString& prefix, int i, int j);
};

#endif // WCSUTILS_H