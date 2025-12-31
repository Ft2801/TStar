#ifndef XISFWRITER_H
#define XISFWRITER_H

#include <QString>
#include "../ImageBuffer.h"

class XISFWriter {
public:
    static bool write(const QString& filePath, const ImageBuffer& buffer, int depth, QString* errorMsg = nullptr);

private:
    static QByteArray createXmlHeader(const ImageBuffer& buffer, quint64 dataSize, quint64 headerSize, quint64* outHeaderLength);
};

#endif // XISFWRITER_H
