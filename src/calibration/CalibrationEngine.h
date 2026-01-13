
#ifndef CALIBRATION_ENGINE_H
#define CALIBRATION_ENGINE_H

#include "../ImageBuffer.h"
#include "../preprocessing/PreprocessingTypes.h"
#include <QString>

namespace Calibration {

/**
 * @brief Core calibration functions
 */
class CalibrationEngine {
public:

    static float findOptimalDarkScale(const ImageBuffer& light, 
                                     const ImageBuffer& masterDark,
                                     const Preprocessing::DarkOptimParams& params);

    static float evaluateCalibratedNoise(const ImageBuffer& light,
                                        const ImageBuffer& masterDark,
                                        float k,
                                        const QRect& rect);

    static double computeFlatNormalization(const ImageBuffer& masterFlat);

    /**
     * @brief Fix CCD banding artifacts
     * @param image Image to fix (in-place)
     */
    static void fixBanding(ImageBuffer& image);

    /**
     * @brief Fix bad CCD lines
     * @param image Image to fix (in-place)
     */
    static void fixBadLines(ImageBuffer& image);

    /**
     * @brief Fix X-Trans sensor artifacts (Fujifilm)
     * @param image Image to fix (in-place)
     */
    static void fixXTransArtifacts(ImageBuffer& image);

    /**
     * @brief Equalize CFA channels in a master flat
     * @param flat Master flat image (in-place)
     * @param pattern Bayer pattern of the sensor
     */
    static void equalizeCFAChannels(ImageBuffer& flat, Preprocessing::BayerPattern pattern);

    /**
     * @brief Automatically detect deviant (hot/cold) pixels from a master dark
     * @param dark Master dark frame
     * @param hotSigma Sigma threshold for hot pixels
     * @param coldSigma Sigma threshold for cold pixels
     * @return List of deviant pixel coordinates
     */
    static std::vector<QPoint> findDeviantPixels(const ImageBuffer& dark, 
                                                 float hotSigma, 
                                                 float coldSigma);

    /**
     * @brief Apply master flat to light frame with normalization
     * @param light Light frame (in-place)
     * @param masterFlat Master flat
     * @param normalization Normalization factor
     */
    static bool applyFlat(ImageBuffer& light, const ImageBuffer& masterFlat, double normalization);
};

} // namespace Calibration

#endif // CALIBRATION_ENGINE_H
