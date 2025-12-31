#include "XISFReader.h"
#include <QFile>
#include <QDataStream>
#include <QXmlStreamReader>
#include <QDebug>
#include <cmath>
#include <QtEndian>
#include <QCoreApplication>

bool XISFReader::read(const QString& filePath, ImageBuffer& buffer, QString* errorMsg) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Could not open file: %1").arg(filePath);
        return false;
    }

    // 1. Read Signature (8 bytes)
    QByteArray sig = file.read(8);
    if (sig != "XISF0100") {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Invalid XISF Signature. File is not XISF 1.0.");
        return false;
    }

    // 2. Read Header Length (4 bytes, little endian)
    uchar lenBytes[4];
    if (file.read((char*)lenBytes, 4) != 4) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Unexpected EOF reading header length.");
        return false;
    }
    quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);

    // 3. Skip Reserved (4 bytes)
    file.read(4);

    // 4. Read XML Header
    if (headerLen == 0 || headerLen > 100 * 1024 * 1024) { // Safety limit 100MB
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Invalid Header Length: %1").arg(headerLen);
        return false;
    }
    
    QByteArray xmlBytes = file.read(headerLen);
    if (xmlBytes.size() != (int)headerLen) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Incomplete XML Header.");
        return false;
    }

    // 5. Parse XML
    XISFHeaderInfo info;
    if (!parseHeader(xmlBytes, info, errorMsg)) {
        return false; 
    }

    // Check for unsupported features
    if (!info.compression.isEmpty()) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Compressed XISF files are not yet supported (detected: %1).").arg(info.compression);
        return false;
    }
    
    if (!info.byteOrder.isEmpty() && info.byteOrder == "big") {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Big-endian XISF files are not supported.");
        return false;
    }

    if (info.width <= 0 || info.height <= 0) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "No valid image found in XISF header.");
        return false;
    }
    
    // 6. Seek to Data
    qint64 currentPos = info.dataLocation;
    if (currentPos == 0) {
       // Primary image monolithic fallback
       currentPos = 16 + headerLen;
    }
    
    if (!file.seek(currentPos)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Seek error to data block at %1").arg(currentPos);
        return false;   
    }
    
    long long totalPixels = (long long)info.width * info.height * info.channels;
    std::vector<float> finalData(totalPixels);
    
    // Read based on format
    if (info.sampleFormat == "Float32") {
        QByteArray raw = file.read(totalPixels * 4);
        if (raw.size() < (int)(totalPixels * 4)) {
             if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Unexpected EOF reading Float32 data.");
             return false;
        }
        const float* fptr = reinterpret_cast<const float*>(raw.constData());
        for(size_t i=0; i<totalPixels; ++i) finalData[i] = fptr[i]; 
    } 
    else if (info.sampleFormat == "UInt16") {
        QByteArray raw = file.read(totalPixels * 2);
        if (raw.size() < (int)(totalPixels * 2)) {
             if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Unexpected EOF reading UInt16 data.");
             return false;
        }
        const quint16* uptr = reinterpret_cast<const quint16*>(raw.constData());
        for(size_t i=0; i<totalPixels; ++i) finalData[i] = (float)uptr[i] / 65535.0f;
    }
    else if (info.sampleFormat == "UInt8") {
        QByteArray raw = file.read(totalPixels);
        if (raw.size() < (int)totalPixels) {
             if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Unexpected EOF reading UInt8 data.");
             return false;
        }
        const quint8* uptr = reinterpret_cast<const quint8*>(raw.constData());
        for(size_t i=0; i<totalPixels; ++i) finalData[i] = (float)uptr[i] / 255.0f;
    }
    else {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Unsupported Sample Format: %1").arg(info.sampleFormat);
        return false;
    }
    
    buffer.setMetadata(info.meta);
    
    // Convert Planar (XISF default) to Interleaved (ImageBuffer default)
    if (info.channels > 1) {
        std::vector<float> interleaved(totalPixels);
        long planeSize = info.width * info.height;
        for (int c = 0; c < info.channels; ++c) {
            for (long i = 0; i < planeSize; ++i) {
                interleaved[i * info.channels + c] = finalData[(long long)c * planeSize + i];
            }
        }
        buffer.setData(info.width, info.height, info.channels, interleaved);
    } else {
        buffer.setData(info.width, info.height, info.channels, finalData);
    }
    
    return true;
}

bool XISFReader::parseHeader(const QByteArray& headerXml, XISFHeaderInfo& info, QString* errorMsg) {
    QXmlStreamReader xml(headerXml);
    
    bool foundImage = false;
    
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == "Image") {
                if (foundImage) continue; 
                
                // Attributes
                QString geom = xml.attributes().value("geometry").toString();
                QStringList parts = geom.split(':');
                if (parts.size() >= 2) {
                    info.width = parts[0].toInt();
                    info.height = parts[1].toInt();
                    info.channels = (parts.size() > 2) ? parts[2].toInt() : 1;
                }
                
                info.sampleFormat = xml.attributes().value("sampleFormat").toString();
                info.byteOrder = xml.attributes().value("byteOrder").toString();
                info.compression = xml.attributes().value("compression").toString();

                QString loc = xml.attributes().value("location").toString();
                QStringList locParts = loc.split(':');
                if (locParts.size() >= 3 && locParts[0] == "attachment") {
                    info.dataLocation = locParts[1].toLongLong();
                    info.dataSize = locParts[2].toLongLong();
                } else if (locParts.size() >= 2 && locParts[0] == "inline") {
                    if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Inline XISF data not supported.");
                    return false;
                }
                
                // Image children
                while(!(xml.isEndElement() && xml.name() == "Image") && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement()) {
                        if (xml.name() == "FITSKeyword") {
                            QString name = xml.attributes().value("name").toString();
                            QString val = xml.attributes().value("value").toString();
                            QString comment = xml.attributes().value("comment").toString();
                            info.meta.rawHeaders.push_back({name, val, comment});
                            
                            if(name == "FOCALLEN") info.meta.focalLength = val.toDouble();
                            if(name == "XPIXSZ") info.meta.pixelSize = val.toDouble();
                            if(name == "RA") info.meta.ra = val.toDouble();
                            if(name == "DEC") info.meta.dec = val.toDouble();
                            if(name == "DATE-OBS") info.meta.dateObs = val;
                            if(name == "OBJECT") info.meta.objectName = val;
                        }
                    }
                }
                foundImage = true;
            }
        }
    }
    
    if (xml.hasError()) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "XML Parse Error: %1").arg(xml.errorString());
        return false;
    }
    
    return true;
}
