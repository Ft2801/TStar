/**
 * @file MasterFrames.h
 * @brief Master calibration frame management
 * 
 * Handles loading, caching, and creating master calibration frames
 * (bias, dark, flat) for image preprocessing.
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef MASTER_FRAMES_H
#define MASTER_FRAMES_H

#include "PreprocessingTypes.h"
#include "../ImageBuffer.h"
#include "../stacking/StackingTypes.h"
#include <QString>
#include <QStringList>
#include <memory>
#include <unordered_map>

namespace Preprocessing {

/**
 * @brief Manages master calibration frames
 * 
 * Provides loading, caching, and creation of master frames.
 * Supports bias, dark, and flat calibration frames.
 */
class MasterFrames {
public:
    MasterFrames() = default;
    ~MasterFrames() = default;
    
    // Non-copyable
    MasterFrames(const MasterFrames&) = delete;
    MasterFrames& operator=(const MasterFrames&) = delete;
    
    //=========================================================================
    // LOADING
    //=========================================================================
    
    /**
     * @brief Load a master frame from file
     * @param type Type of master frame
     * @param path Path to master frame file
     * @return true if successful
     */
    bool load(MasterType type, const QString& path);
    
    /**
     * @brief Check if a master frame is loaded
     */
    bool isLoaded(MasterType type) const;
    
    /**
     * @brief Get a loaded master frame
     * @return Pointer to buffer, or nullptr if not loaded
     */
    const ImageBuffer* get(MasterType type) const;
    ImageBuffer* get(MasterType type);
    
    /**
     * @brief Get statistics for a master frame
     */
    const MasterStats& stats(MasterType type) const;
    
    /**
     * @brief Unload a master frame
     */
    void unload(MasterType type);
    
    /**
     * @brief Unload all master frames
     */
    void clear();
    
    //=========================================================================
    // CREATION
    //=========================================================================
    
    /**
     * @brief Create a master bias from individual bias frames
     * @param files List of bias frame paths
     * @param output Path to save master bias
     * @param method Stacking method to use
     * @param rejection Rejection algorithm
     * @param sigmaLow Low sigma for rejection
     * @param sigmaHigh High sigma for rejection
     * @param progress Progress callback
     * @return true if successful
     */
    bool createMasterBias(
        const QStringList& files,
        const QString& output,
        Stacking::Method method = Stacking::Method::Mean,
        Stacking::Rejection rejection = Stacking::Rejection::Winsorized,
        float sigmaLow = 3.0f,
        float sigmaHigh = 3.0f,
        ProgressCallback progress = nullptr
    );
    
    /**
     * @brief Create a master dark from individual dark frames
     * @param files List of dark frame paths
     * @param output Path to save master dark
     * @param masterBias Optional master bias to subtract
     * @param method Stacking method
     * @param rejection Rejection algorithm
     * @param sigmaLow Low sigma for rejection
     * @param sigmaHigh High sigma for rejection
     * @param progress Progress callback
     * @return true if successful
     */
    bool createMasterDark(
        const QStringList& files,
        const QString& output,
        const QString& masterBias = QString(),
        Stacking::Method method = Stacking::Method::Mean,
        Stacking::Rejection rejection = Stacking::Rejection::Winsorized,
        float sigmaLow = 3.0f,
        float sigmaHigh = 3.0f,
        ProgressCallback progress = nullptr
    );
    
    /**
     * @brief Create a master flat from individual flat frames
     * @param files List of flat frame paths
     * @param output Path to save master flat
     * @param masterBias Optional master bias to subtract
     * @param masterDark Optional master dark to subtract
     * @param method Stacking method
     * @param rejection Rejection algorithm
     * @param sigmaLow Low sigma for rejection
     * @param sigmaHigh High sigma for rejection
     * @param progress Progress callback
     * @return true if successful
     */
    bool createMasterFlat(
        const QStringList& files,
        const QString& output,
        const QString& masterBias = QString(),
        const QString& masterDark = QString(),
        Stacking::Method method = Stacking::Method::Mean,
        Stacking::Rejection rejection = Stacking::Rejection::Winsorized,
        float sigmaLow = 3.0f,
        float sigmaHigh = 3.0f,
        ProgressCallback progress = nullptr
    );
    
    //=========================================================================
    // VALIDATION
    //=========================================================================
    
    /**
     * @brief Check if master frames are compatible with a target image
     * @param target Image to check against
     * @return Error message, or empty string if compatible
     */
    QString validateCompatibility(const ImageBuffer& target) const;
    
    /**
     * @brief Check dark frame temperature compatibility
     * @param targetTemp Target dark temperature
     * @param tolerance Temperature tolerance in degrees
     * @return true if compatible
     */
    bool checkDarkTemperature(double targetTemp, double tolerance = 5.0) const;
    
    /**
     * @brief Check dark frame exposure compatibility
     * @param targetExposure Target exposure time
     * @param tolerance Relative tolerance (0.1 = 10%)
     * @return true if compatible (or dark optimization will be used)
     */
    bool checkDarkExposure(double targetExposure, double tolerance = 0.1) const;
    
private:
    struct MasterData {
        std::unique_ptr<ImageBuffer> buffer;
        MasterStats stats;
        QString path;
    };
    
    std::unordered_map<int, MasterData> m_masters;
    
    /**
     * @brief Compute statistics for a master frame
     */
    void computeStats(MasterType type);
    
    /**
     * @brief Get index for master type
     */
    static int typeIndex(MasterType type) { return static_cast<int>(type); }
};

} // namespace Preprocessing

#endif // MASTER_FRAMES_H
