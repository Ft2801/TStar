#ifndef FITSLOADER_H
#define FITSLOADER_H

#include <QString>
#include <QMap>
#include <QVariant>
#include "ImageBuffer.h"

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
    static bool load(const QString& filePath, ImageBuffer& buffer, QString* errorMsg = nullptr);
    
    static bool loadMetadata(const QString& filePath, ImageBuffer& buffer, QString* errorMsg = nullptr);

    static QMap<QString, FitsExtensionInfo> listExtensions(const QString& filePath, QString* errorMsg = nullptr);
    
    static bool loadExtension(const QString& filePath, const QString& extensionKey, 
                              ImageBuffer& buffer, QString* errorMsg = nullptr);
    
    static bool loadExtension(const QString& filePath, int hduIndex, 
                              ImageBuffer& buffer, QString* errorMsg = nullptr);

    /**
     * @brief Load a rectangular region from the primary HDU
     */
    static bool loadRegion(const QString& filePath, 
                          ImageBuffer& buffer, 
                          int x, int y, int w, int h, 
                          QString* errorMsg = nullptr);

private:
    static double parseRAString(const QString& str, bool* ok = nullptr);
    
    static double parseDecString(const QString& str, bool* ok = nullptr);
    
    static void readSIPCoefficients(void* fptr, ImageBuffer::Metadata& meta);
    
    static void readCommonMetadata(void* fptr, ImageBuffer::Metadata& meta);
    
    static bool loadHDU(void* fptr, int hduIndex, ImageBuffer& buffer, QString* errorMsg,
                       int x = 0, int y = 0, int w = 0, int h = 0);
};

#endif // FITSLOADER_H
