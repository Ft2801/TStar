#pragma once
#ifndef SENSORPROFILE_H
#define SENSORPROFILE_H

#include <QString>
#include <map>
#include <vector>

/**
 * @brief Sensor profile management for photometry and calibration
 * 
 * Stores sensor characteristics needed for:
 * - Aperture photometry (gain, read noise, full well)
 * - Color calibration (quantum efficiency per channel)
 * - Deconvolution parameters (optimal PSF assumptions)
 */

struct SensorProfile {
    // Identification
    QString name;           // e.g. "Canon EOS R7", "ZWO ASI533MM-Pro"
    QString type;           // "DSLR", "CCD", "CMOS", "eMOS"
    
    // Sensor geometry
    double pixelWidth;      // Pixels in microns
    double pixelHeight;     // Pixels in microns
    int sensorWidth;        // Sensor width in pixels
    int sensorHeight;       // Sensor height in pixels
    
    // Photometry parameters
    double gain;            // e-/ADU (electrons per analog unit)
    double readNoise;       // RMS electrons
    double darkCurrent;     // e-/s at reference temperature (usually 25°C)
    double fullWell;        // Electrons (saturation level)
    double maxADU;          // Maximum ADU value (usually 65535 for 16-bit)
    
    // Color channel QE (Quantum Efficiency) curves
    // Typical ranges: [300nm, 1100nm] for visible to NIR
    std::map<int, double>   qeRed;      // Wavelength (nm) -> QE [0,1]
    std::map<int, double>   qeGreen;
    std::map<int, double>   qeBlue;
    
    // Temperature compensation
    double tempRefC;        // Reference temperature in Celsius (for dark current)
    double darkCurrentGradient;  // Change in dark current per °C
    
    // Serialization flag
    bool isCustom = false;  // true if user-defined, false if built-in
};

/**
 * @brief Built-in sensor profile database
 */
class SensorProfileDatabase {
public:
    static SensorProfileDatabase& instance();
    
    // Load profile by name
    bool loadProfile(const QString& name, SensorProfile& out);
    
    // Register custom profile
    void registerProfile(const SensorProfile& profile);
    
    // List all available profiles
    std::vector<QString> listProfiles() const;
    
    // Get default profile (generic CCD assumptions)
    static SensorProfile getDefault();
    
private:
    SensorProfileDatabase() = default;
    std::map<QString, SensorProfile> m_profiles;
    
    void initBuiltins();
};

#endif // SENSORPROFILE_H
