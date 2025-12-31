#ifndef XISFREADER_H
#define XISFREADER_H

#include <QString>
#include <vector>
#include "../ImageBuffer.h"

class XISFReader {
public:
    static bool read(const QString& filePath, ImageBuffer& buffer, QString* errorMsg = nullptr);

private:
    struct XISFHeaderInfo {
        int width = 0;
        int height = 0;
        int channels = 1;
        long long dataLocation = 0;
        long long dataSize = 0;
        QString sampleFormat; // "Float32", "UInt16", etc.
        QString compression;  // e.g. "zlib:123456"
        QString byteOrder;    // "little" (default) or "big"
        ImageBuffer::Metadata meta;
    };
    
    // Internal helper to parse XML
    static bool parseHeader(const QByteArray& headerXml, XISFHeaderInfo& info, QString* errorMsg);
};

#endif // XISFREADER_H
