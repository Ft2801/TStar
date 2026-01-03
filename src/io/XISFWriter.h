#ifndef XISFWRITER_H
#define XISFWRITER_H

#include <QString>
#include <QDomDocument>
#include <QDomElement>
#include "../ImageBuffer.h"
#include "CompressionUtils.h"

class XISFWriter {
public:
    struct WriteOptions {
        CompressionUtils::Codec codec = CompressionUtils::Codec_None;
        int compressionLevel = -1;  // -1 = use default for codec
        bool shuffle = false;       // Enable byte shuffling before compression
        int blockAlignment = 4096;  // Block alignment size
        bool preserveHeaders = true; // Preserve all raw headers from metadata
        QString creatorApp = "TStar";
    };


    static bool write(const QString& filePath, const ImageBuffer& buffer, 
                      int depth, QString* errorMsg = nullptr);


    static bool write(const QString& filePath, const ImageBuffer& buffer,
                      ImageBuffer::BitDepth depth, const WriteOptions& options,
                      QString* errorMsg = nullptr);

private:
    // Build XML header and return data block ready for writing
    static QByteArray buildHeader(const ImageBuffer& buffer, ImageBuffer::BitDepth depth,
                                  const WriteOptions& options,
                                  quint64 dataSize, quint64 attachmentPosition,
                                  const QString& compressionAttr);
    
    // Prepare image data (convert to output format and arrange as planar)
    static QByteArray prepareImageData(const ImageBuffer& buffer, ImageBuffer::BitDepth depth);
    
    // Add FITSKeyword elements to the image element
    static void addFITSKeywords(QDomDocument& doc, QDomElement& imageElem,
                                const ImageBuffer::Metadata& meta);
    
    // Helper to align position to block boundary
    static quint64 alignPosition(quint64 pos, int alignment);
    
    // Calculate sample format string
    static QString sampleFormatString(ImageBuffer::BitDepth depth);
    
    // Get bytes per sample
    static int bytesPerSample(ImageBuffer::BitDepth depth);
};

#endif // XISFWRITER_H
