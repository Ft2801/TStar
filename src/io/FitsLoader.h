#ifndef FITSLOADER_H
#define FITSLOADER_H

#include <QString>
#include <QMap>
#include <QVariant>
#include "ImageBuffer.h"

/**
 * @brief Information about a single FITS extension/HDU
 */
struct FitsExtensionInfo {
    int index;           // HDU index (0-based)
    QString name;        // Extension name (or index as string if unnamed)
    int width;
    int height;
    int channels;
    QString dtype;       // Data type description
    int bitpix;          // FITS BITPIX value
};

class FitsLoader {
public:
    /**
     * @brief Load a FITS file into an ImageBuffer (loads FIRST image HDU only)
     * For multi-extension loading, use listExtensions() + loadExtension()
     * 
     * Supports:
     * - Regular FITS (.fits, .fit)
     * - Gzip compressed FITS (.fits.gz, .fit.gz)
     * - All standard data types (int8, int16, int32, uint8, uint16, uint32, float32, float64)
     * - Multi-HDU navigation (uses first image HDU)
     * - Full header preservation including WCS and SIP
     * 
     * @param filePath Path to FITS file
     * @param buffer Output image buffer
     * @param errorMsg Optional error message output
     * @return true on success
     */
    static bool load(const QString& filePath, ImageBuffer& buffer, QString* errorMsg = nullptr);

    /**
     * @brief List all image extensions in a FITS file
     * 
     * Matches SASPro's list_fits_extensions() behavior:
     * Returns a map where key = extension name (or index as string if unnamed)
     * 
     * @param filePath Path to FITS file
     * @param errorMsg Optional error message output
     * @return Map of extension key to FitsExtensionInfo
     */
    static QMap<QString, FitsExtensionInfo> listExtensions(const QString& filePath, QString* errorMsg = nullptr);
    
    /**
     * @brief Load a specific FITS extension by name or index
     * 
     * Matches SASPro's load_fits_extension() behavior.
     * 
     * @param filePath Path to FITS file
     * @param extensionKey Extension name or index (as string)
     * @param buffer Output image buffer
     * @param errorMsg Optional error message output
     * @return true on success
     */
    static bool loadExtension(const QString& filePath, const QString& extensionKey, 
                              ImageBuffer& buffer, QString* errorMsg = nullptr);
    
    /**
     * @brief Load a specific FITS extension by index
     */
    static bool loadExtension(const QString& filePath, int hduIndex, 
                              ImageBuffer& buffer, QString* errorMsg = nullptr);

private:
    /**
     * @brief Parse RA string in various formats
     * Supports decimal degrees or HMS format (HH:MM:SS.ss or HH MM SS.ss)
     */
    static double parseRAString(const QString& str, bool* ok = nullptr);
    
    /**
     * @brief Parse Dec string in various formats
     * Supports decimal degrees or DMS format (DD:MM:SS.ss or DD MM SS.ss)
     */
    static double parseDecString(const QString& str, bool* ok = nullptr);
    
    /**
     * @brief Read SIP coefficients from FITS header
     */
    static void readSIPCoefficients(void* fptr, ImageBuffer::Metadata& meta);
    
    /**
     * @brief Internal helper to load image data from a specific HDU
     */
    static bool loadHDU(void* fptr, int hduIndex, ImageBuffer& buffer, QString* errorMsg);
};

#endif // FITSLOADER_H
