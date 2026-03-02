#ifndef WCSUTILS_H
#define WCSUTILS_H

#include "../ImageBuffer.h"

class WCSUtils {
public:
    using Metadata = ImageBuffer::Metadata;
    
    static void cdeltPcToCD(double cdelt1, double cdelt2,
                            double pc1_1, double pc1_2, double pc2_1, double pc2_2,
                            double& cd1_1, double& cd1_2, double& cd2_1, double& cd2_2);
    
    static void cdToCdeltCrota(double cd1_1, double cd1_2, double cd2_1, double cd2_2,
                               double& cdelt1, double& cdelt2, double& crota2);
    
    static bool hasValidWCS(const Metadata& meta);
    
    static double pixelScale(const Metadata& meta);

    static double imageRotation(const Metadata& meta);

    /**
     * Position angle of the image Y-axis, measured East of North (degrees, CCW positive).
     * This is the value expected by the hips2fits `rotation_angle` parameter.
     * Computed as atan2(CD1_2, CD2_2) — the angle of increasing-row direction from North.
     * Returns 0.0 if WCS is invalid.
     */
    static double positionAngle(const Metadata& meta);

    /**
     * Returns true if the image has a mirrored (East-right) orientation.
     * Formally: the determinant of the CD matrix > 0.
     * Standard astronomical images have det < 0 (East-left).
     * When true the reference image from hips2fits must be flipped horizontally
     * before use as a gradient reference.
     */
    static bool isParityFlipped(const Metadata& meta);
    
    static bool pixelToWorld(const Metadata& meta, double px, double py,
                             double& ra, double& dec);
    

    static bool worldToPixel(const Metadata& meta, double ra, double dec,
                             double& px, double& py);

    static void applySIP(const Metadata& meta, double u, double v,
                         double& du, double& dv);
    
    static void applySIPInverse(const Metadata& meta, double u, double v,
                                double& u0, double& v0);
    
    static bool getFieldCenter(const Metadata& meta, int width, int height,
                               double& ra, double& dec);
    
    static bool getFieldOfView(const Metadata& meta, int width, int height,
                               double& fovX, double& fovY);

private:
    // TAN projection (gnomonic) helpers
    static void tanProject(double ra, double dec, double ra0, double dec0,
                           double& xi, double& eta);
    
    static void tanDeproject(double xi, double eta, double ra0, double dec0,
                             double& ra, double& dec);
    
    // Helper to get SIP coefficient
    static double getSIPCoeff(const Metadata& meta, const QString& prefix, int i, int j);
};

#endif // WCSUTILS_H
