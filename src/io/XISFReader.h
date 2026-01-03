#ifndef XISFREADER_H
#define XISFREADER_H

#include <QString>
#include <QVariant>
#include <QMap>
#include <QFile>
#include <QXmlStreamReader>
#include <vector>
#include "../ImageBuffer.h"
#include "CompressionUtils.h"


/**
 * @brief Information about a single XISF image block
 */
struct XISFImageInfo {
    int index;            // Image index (0-based)
    QString name;         // Image ID/name if present
    int width;
    int height;
    int channels;
    QString sampleFormat; // e.g. "Float32", "UInt16"
    QString colorSpace;   // "Gray" or "RGB"
};

class XISFReader {
public:

    /**
     * @brief Read first image from XISF file (for backward compatibility)
     */
    static bool read(const QString& filePath, ImageBuffer& buffer, QString* errorMsg = nullptr);
    
    /**
     * @brief List all images in an XISF file
     * 
     * Matches SASPro's XISF.get_images_metadata() behavior.
     * Each Image element in the XISF header becomes an entry.
     * 
     * @param filePath Path to XISF file
     * @param errorMsg Optional error message output
     * @return List of XISFImageInfo for each image block
     */
    static QList<XISFImageInfo> listImages(const QString& filePath, QString* errorMsg = nullptr);
    
    /**
     * @brief Read a specific image from XISF file by index
     * 
     * Matches SASPro's XISF.read_image(n) behavior.
     * 
     * @param filePath Path to XISF file
     * @param imageIndex 0-based index of the image to read
     * @param buffer Output image buffer
     * @param errorMsg Optional error message output
     * @return true on success
     */
    static bool readImage(const QString& filePath, int imageIndex, 
                          ImageBuffer& buffer, QString* errorMsg = nullptr);

    struct XISFProperty {
        QString id;
        QString type;
        QVariant value;
        QString format;
        QString comment;
    };

private:

    struct XISFHeaderInfo {
        int width = 0;
        int height = 0;
        int channels = 1;
        long long dataLocation = 0;
        long long dataSize = 0;
        QString sampleFormat;     // "Float32", "Float64", "UInt16", "UInt32", "UInt8"
        QString colorSpace;       // "Gray", "RGB"
        QString pixelStorage;     // "planar" (default) or "normal"
        QString byteOrder;        // "little" (default) or "big"
        QString imageId;          // Image ID for naming
        
        // Compression info
        CompressionUtils::Codec compressionCodec = CompressionUtils::Codec_None;
        long long uncompressedSize = 0;
        int shuffleItemSize = 0;
        
        // Location type
        enum LocationType { Attachment, Inline, Embedded };
        LocationType locationType = Attachment;
        QString inlineEncoding;   // "base64" or "hex"
        QString embeddedData;     // Raw embedded data
        
        // Metadata
        ImageBuffer::Metadata meta;
        
        // XISFProperties
        QMap<QString, XISFProperty> properties;
        
        // Additional XISF core elements
        double resolutionH = 72.0;
        double resolutionV = 72.0;
        QString resolutionUnit = "inch";
        bool hasICCProfile = false;
        bool hasThumbnail = false;
    };
    
    // Header parsing
    static bool parseHeader(const QByteArray& headerXml, XISFHeaderInfo& info, QString* errorMsg);
    
    // Parse ALL image blocks from header (for multi-image support)
    static bool parseAllImages(const QByteArray& headerXml, QList<XISFHeaderInfo>& images, QString* errorMsg);
    
    // Data block reading
    static QByteArray readDataBlock(QFile& file, const XISFHeaderInfo& info, QString* errorMsg);
    static QByteArray readAttachedDataBlock(QFile& file, long long position, long long size, QString* errorMsg);
    static QByteArray decodeInlineData(const QString& data, const QString& encoding, QString* errorMsg);
    static QByteArray decodeEmbeddedData(const QString& embeddedXml, QString* errorMsg);
    
    // Data conversion
    static bool convertToFloat(const QByteArray& rawData, const XISFHeaderInfo& info,
                               std::vector<float>& outData, QString* errorMsg);
    
    // Planar to interleaved conversion
    static void planarToInterleaved(const std::vector<float>& planar,
                                    std::vector<float>& interleaved,
                                    int width, int height, int channels);
    
    // Property parsing helpers
    static XISFProperty parseProperty(const QXmlStreamReader& xml);
    static QVariant parsePropertyValue(const QString& type, const QString& valueStr,
                                       const QString& textContent, int length = 0,
                                       int rows = 0, int columns = 0);
    
    // Helper to get sample format item size
    static int getSampleSize(const QString& format);
    
    // Internal: load image from parsed header info
    static bool loadImage(QFile& file, const XISFHeaderInfo& info, ImageBuffer& buffer, QString* errorMsg);
};

#endif // XISFREADER_H
