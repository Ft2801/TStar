#ifndef DISTORTION_H
#define DISTORTION_H

#include "StackingTypes.h"
#include <vector>
#include <QPointF>

namespace Stacking {

/**
 * @brief Class for handling SIP (Simple Imaging Polynomial) Distortion
 */
class Distortion {
public:
    /**
     * @brief Apply Forward distortion (Pixel -> Sky/Intermediate)
     * Uses A and B polynomials.
     * x' = x + f(x, y)
     */
    static QPointF applyForward(const QPointF& p, const RegistrationData& reg);

    /**
     * @brief Apply Reverse distortion (Sky/Intermediate -> Pixel)
     * Uses AP and BP polynomials.
     * x = x' + f(x', y')
     */
    static QPointF applyReverse(const QPointF& p, const RegistrationData& reg);

    /**
     * @brief Calculate polynomial value for a specific coefficient set
     */
    static double computePoly(double u, double v, int order, const std::vector<std::vector<double>>& coeffs);

    /**
     * @brief Transform pixel coordinates from Target to Reference using SIP + Homography
     * 
     * Pipeline:
     * 1. Target Pixel -> Apply Forward SIP (Target) -> Target Intermediate
     * 2. Target Intermediate -> Apply Homography -> Reference Intermediate
     * 3. Reference Intermediate -> Apply Reverse SIP (Ref) -> Reference Pixel
     * 
     * Note: "Reference" usually defines the tangible plane.
     * If we only correct one image relative to reference:
     * We map: Output(Ref) -> Input(Src).
     * Output (Ref Pixel) --(Distortion)--> Ref Sky --(WCS/Homography)--> Src Sky --(Distortion^-1)--> Src Pixel.
     * 
     * TStar uses Homography between pixels. With distortion:
     * We need to linearize the image first.
     * 
     * @param p Point in Source Image coordinates
     * @param reg Source Registration Data (contains SIP for Source)
     * @param refReg Reference Registration Data (contains SIP for Reference)
     * @return Transformed point in Reference coordinates
     */
     // Wait, usually we need Inverse Transform for stacking (Output -> Input).
     // Output (Ref grid) -> Input (Src pixel).
     static QPointF transformRefToSrc(const QPointF& outP, const RegistrationData& refReg, const RegistrationData& srcReg);

     /**
      * @brief Apply just the SIP correction to a point (Forward)
      */
     static void correctDistortion(double x, double y, const RegistrationData& reg, double& outX, double& outY);
};

} // namespace Stacking

#endif // DISTORTION_H
