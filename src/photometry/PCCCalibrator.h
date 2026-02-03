#ifndef PCCCALIBRATOR_H
#define PCCCALIBRATOR_H

#include <vector>
#include "StarDetector.h"
#include "CatalogClient.h"
#include "../ImageBuffer.h"

#include "PCCResult.h"


class PCCCalibrator {
public:
    PCCCalibrator();
    
    // Legacy method using DetectedStar
    PCCResult calibrate(const std::vector<DetectedStar>& imageStarsR,
                        const std::vector<DetectedStar>& imageStarsG,
                        const std::vector<DetectedStar>& imageStarsB,
                        const std::vector<CatalogStar>& catalogStars,
                        int width, int height);
    
    // NEW: Standard calibrate using aperture photometry on image data directly
    PCCResult calibrateWithAperture(const ImageBuffer& image,
                                    const std::vector<CatalogStar>& catalogStars);
                        
    void setWCS(double crval1, double crval2, double crpix1, double crpix2, double cd1_1, double cd1_2, double cd2_1, double cd2_2);
    void setSIP(int a_order, int b_order, int ap_order, int bp_order, const std::map<std::string, double>& coeffs);

private:
    // WCS Data
    double m_crval1, m_crval2, m_crpix1, m_crpix2;
    double m_cd11, m_cd12, m_cd21, m_cd22;
    
    // SIP Distortion
    bool m_useSip = false;
    int m_sipOrderA = 0, m_sipOrderB = 0;
    int m_sipOrderAP = 0, m_sipOrderBP = 0;
    std::map<std::pair<int,int>, double> m_sipA;
    std::map<std::pair<int,int>, double> m_sipB;
    std::map<std::pair<int,int>, double> m_sipAP;
    std::map<std::pair<int,int>, double> m_sipBP;
    
    double calculateSIP(double u, double v, const std::map<std::pair<int,int>, double>& coeffs, int order) const;
    
    void pixelToWorld(double x, double y, double& ra, double& dec);
    void worldToPixel(double ra, double dec, double& x, double& y);
};

#endif // PCCCALIBRATOR_H

