
#ifndef REJECTION_MAPS_H
#define REJECTION_MAPS_H

#include "../ImageBuffer.h"
#include "StackingTypes.h"
#include <vector>

namespace Stacking {

/**
 * @brief Rejection map types
 */
enum class RejectionMapType {
    Low,        ///< Low rejection only
    High,       ///< High rejection only
    Combined    ///< Both low and high in single map
};

/**
 * @brief Rejection map generator and manager
 */
class RejectionMaps {
public:
    /**
     * @brief Initialize rejection maps for a stacking operation
     * @param width Image width
     * @param height Image height
     * @param channels Number of channels
     * @param createLow Create low rejection map
     * @param createHigh Create high rejection map
     */
    void initialize(int width, int height, int channels,
                   bool createLow = true, bool createHigh = true);
    
    /**
     * @brief Record a pixel rejection
     * @param x X coordinate
     * @param y Y coordinate
     * @param channel Channel index
     * @param rejectionType -1 for low, +1 for high
     */
    void recordRejection(int x, int y, int channel, int rejectionType);
    
    /**
     * @brief Get low rejection map
     * @return Pointer to low rejection map buffer
     */
    ImageBuffer* getLowMap();
    
    /**
     * @brief Get high rejection map
     * @return Pointer to high rejection map buffer
     */
    ImageBuffer* getHighMap();
    
    /**
     * @brief Get combined rejection map
     * @return Combined map (low in red, high in blue channel)
     */
    ImageBuffer getCombinedMap() const;
    
    /**
     * @brief Check if maps are initialized
     */
    bool isInitialized() const { return m_initialized; }
    
    /**
     * @brief Get total low rejections
     */
    int64_t totalLowRejections() const { return m_totalLow; }
    
    /**
     * @brief Get total high rejections
     */
    int64_t totalHighRejections() const { return m_totalHigh; }
    
    /**
     * @brief Save rejection maps to files
     * @param basePath Base path for output files
     * @param prefix Filename prefix
     * @return True if successful
     */
    bool save(const QString& basePath, const QString& prefix) const;
    
    /**
     * @brief Clear all data
     */
    void clear();
    
private:
    std::unique_ptr<ImageBuffer> m_lowMap;
    std::unique_ptr<ImageBuffer> m_highMap;
    int m_width = 0;
    int m_height = 0;
    int m_channels = 0;
    int64_t m_totalLow = 0;
    int64_t m_totalHigh = 0;
    bool m_initialized = false;
};

} // namespace Stacking

#endif // REJECTION_MAPS_H
