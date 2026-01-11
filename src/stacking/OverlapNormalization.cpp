#include "OverlapNormalization.h"
#include "MathUtils.h"
#include "Statistics.h"
#include <cmath>
#include <algorithm>

namespace Stacking {

size_t OverlapNormalization::computeOverlapRegion(const RegistrationData& regI, const RegistrationData& regJ,
                                                   int widthI, int heightI, int widthJ, int heightJ,
                                                   QRect& areaI, QRect& areaJ)
{
    // Get translation from homographies
    double dxI = regI.H[0][2];
    double dyI = regI.H[1][2];
    double dxJ = regJ.H[0][2];
    double dyJ = regJ.H[1][2];
    
    // Relative shift: J relative to I
    int dx = static_cast<int>(std::round(dxJ - dxI));
    int dy = static_cast<int>(std::round(dyI - dyJ)); // Note: Y is often inverted
    
    // Compute overlap in I's coordinate system
    int x_tli = std::max(0, dx);
    int y_tli = std::max(0, dy);
    int x_bri = std::min(widthI, dx + widthJ);
    int y_bri = std::min(heightI, dy + heightJ);
    
    // Overlap in J's coordinate system
    int x_tlj = std::max(0, -dx);
    int y_tlj = std::max(0, -dy);
    int x_brj = std::min(widthJ, -dx + widthI);
    int y_brj = std::min(heightJ, -dy + heightI);
    
    if (x_tli < x_bri && y_tli < y_bri) {
        areaI = QRect(x_tli, y_tli, x_bri - x_tli, y_bri - y_tli);
        areaJ = QRect(x_tlj, y_tlj, x_brj - x_tlj, y_brj - y_tlj);
        return static_cast<size_t>(areaI.width()) * areaI.height();
    }
    
    return 0;
}

bool OverlapNormalization::computeOverlapStats(const ImageBuffer& imgI, const ImageBuffer& imgJ,
                                               const QRect& areaI, const QRect& areaJ,
                                               int channel, OverlapStats& stats)
{
    if (areaI.width() != areaJ.width() || areaI.height() != areaJ.height()) {
        return false;
    }
    
    size_t count = static_cast<size_t>(areaI.width()) * areaI.height();
    if (count < 10) return false; // Too few pixels
    
    std::vector<float> dataI, dataJ;
    dataI.reserve(count);
    dataJ.reserve(count);
    
    for (int y = 0; y < areaI.height(); ++y) {
        for (int x = 0; x < areaI.width(); ++x) {
            float vI = imgI.value(areaI.x() + x, areaI.y() + y, channel);
            float vJ = imgJ.value(areaJ.x() + x, areaJ.y() + y, channel);
            
            // Skip zeros (out of bounds / masked)
            if (vI > 0.0f && vJ > 0.0f) {
                dataI.push_back(vI);
                dataJ.push_back(vJ);
            }
        }
    }
    
    if (dataI.size() < 10) return false;
    
    stats.pixelCount = dataI.size();
    stats.medianI = Statistics::quickMedian(dataI);
    stats.medianJ = Statistics::quickMedian(dataJ);
    stats.madI = Statistics::mad(dataI, stats.medianI);
    stats.madJ = Statistics::mad(dataJ, stats.medianJ);
    
    // Robust location/scale (approximation using median/MAD)
    stats.locationI = stats.medianI;
    stats.locationJ = stats.medianJ;
    stats.scaleI = stats.madI * 1.4826; // MAD to sigma scale
    stats.scaleJ = stats.madJ * 1.4826;
    
    return true;
}

bool OverlapNormalization::solveCoefficients(const std::vector<OverlapStats>& allStats,
                                              int numImages, int refIndex, bool additive,
                                              std::vector<double>& coeffs)
{
    
    coeffs.assign(numImages, additive ? 0.0 : 1.0);
    
    // Find all pairs involving the reference
    for (const auto& stat : allStats) {
        if (stat.imgI == refIndex) {
            // Image J vs Reference
            int j = stat.imgJ;
            if (additive) {
                coeffs[j] = stat.medianI - stat.medianJ;
            } else {
                coeffs[j] = (stat.medianJ > 0) ? stat.medianI / stat.medianJ : 1.0;
            }
        } else if (stat.imgJ == refIndex) {
            // Image I vs Reference
            int i = stat.imgI;
            if (additive) {
                coeffs[i] = stat.medianJ - stat.medianI;
            } else {
                coeffs[i] = (stat.medianI > 0) ? stat.medianJ / stat.medianI : 1.0;
            }
        }
    }
    
    // For images not directly overlapping with reference, propagate through chain
    // (Full implementation would solve the complete system)
    
    return true;
}

} // namespace Stacking
