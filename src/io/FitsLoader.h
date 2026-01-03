#ifndef FITSLOADER_H
#define FITSLOADER_H

#include <QString>
#include "ImageBuffer.h"

class FitsLoader {
public:
    /**
     * @brief Load a FITS file into an ImageBuffer
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
};

#endif // FITSLOADER_H
