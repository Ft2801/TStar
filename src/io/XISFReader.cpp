#include "XISFReader.h"
#include "CompressionUtils.h"
#include <QFile>
#include <QDataStream>
#include <QXmlStreamReader>
#include <QDebug>
#include <cmath>
#include <QtEndian>
#include <QCoreApplication>

// For base64 decoding
#include <QByteArray>

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

    if (info.width <= 0 || info.height <= 0) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "No valid image found in XISF header.");
        return false;
    }

    // Store file path in metadata
    info.meta.filePath = filePath;
    
    // 6. Read Data Block
    QByteArray rawData = readDataBlock(file, info, errorMsg);
    if (rawData.isEmpty() && info.dataSize > 0) {
        return false;  // Error already set
    }
    
    // 7. Handle Compression
    if (info.compressionCodec != CompressionUtils::Codec_None) {
        QByteArray decompressed = CompressionUtils::decompress(
            rawData, info.compressionCodec, info.uncompressedSize, errorMsg);
        if (decompressed.isEmpty()) {
            return false;
        }
        
        // Handle byte shuffling
        if (info.shuffleItemSize > 0) {
            rawData = CompressionUtils::unshuffle(decompressed, info.shuffleItemSize);
        } else {
            rawData = decompressed;
        }
    }
    
    // 8. Convert to float
    std::vector<float> pixelData;
    if (!convertToFloat(rawData, info, pixelData, errorMsg)) {
        return false;
    }
    
    // 9. Convert planar to interleaved if needed
    long long totalPixels = (long long)info.width * info.height * info.channels;
    std::vector<float> finalData;
    
    if (info.channels > 1 && (info.pixelStorage.isEmpty() || info.pixelStorage.toLower() == "planar")) {
        finalData.resize(totalPixels);
        planarToInterleaved(pixelData, finalData, info.width, info.height, info.channels);
    } else {
        finalData = std::move(pixelData);
    }
    
    // 10. Set buffer data and metadata
    buffer.setMetadata(info.meta);
    buffer.setData(info.width, info.height, info.channels, finalData);
    
    return true;
}

bool XISFReader::parseHeader(const QByteArray& headerXml, XISFHeaderInfo& info, QString* errorMsg) {
    QXmlStreamReader xml(headerXml);
    
    bool foundImage = false;
    
    while (!xml.atEnd()) {
        xml.readNext();
        
        if (xml.isStartElement()) {
            QString elemName = xml.name().toString();
            
            if (elemName == "Image" && !foundImage) {
                foundImage = true;
                
                // Parse geometry
                QString geom = xml.attributes().value("geometry").toString();
                QStringList parts = geom.split(':');
                if (parts.size() >= 2) {
                    info.width = parts[0].toInt();
                    info.height = parts[1].toInt();
                    info.channels = (parts.size() > 2) ? parts[2].toInt() : 1;
                }
                
                // Sample format
                info.sampleFormat = xml.attributes().value("sampleFormat").toString();
                if (info.sampleFormat.isEmpty()) info.sampleFormat = "Float32";
                
                // Color space
                info.colorSpace = xml.attributes().value("colorSpace").toString();
                
                // Pixel storage
                info.pixelStorage = xml.attributes().value("pixelStorage").toString();
                
                // Byte order
                info.byteOrder = xml.attributes().value("byteOrder").toString();
                if (!info.byteOrder.isEmpty() && info.byteOrder.toLower() == "big") {
                    if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
                        "Big-endian XISF files are not supported.");
                    return false;
                }
                
                // Parse location
                QString loc = xml.attributes().value("location").toString();
                QStringList locParts = loc.split(':');
                if (locParts.size() >= 1) {
                    QString locType = locParts[0].toLower();
                    if (locType == "attachment" && locParts.size() >= 3) {
                        info.locationType = XISFHeaderInfo::Attachment;
                        info.dataLocation = locParts[1].toLongLong();
                        info.dataSize = locParts[2].toLongLong();
                    } else if (locType == "inline" && locParts.size() >= 2) {
                        info.locationType = XISFHeaderInfo::Inline;
                        info.inlineEncoding = locParts[1];
                    } else if (locType == "embedded") {
                        info.locationType = XISFHeaderInfo::Embedded;
                    }
                }
                
                // Parse compression
                QString compression = xml.attributes().value("compression").toString();
                if (!compression.isEmpty()) {
                    if (!CompressionUtils::parseCompressionAttr(
                            compression, info.compressionCodec, 
                            info.uncompressedSize, info.shuffleItemSize)) {
                        qWarning() << "Failed to parse compression:" << compression;
                    }
                }
                
                // Parse Image children
                while (!(xml.isEndElement() && xml.name() == "Image") && !xml.atEnd()) {
                    xml.readNext();
                    
                    if (xml.isStartElement()) {
                        QString childName = xml.name().toString();
                        
                        if (childName == "FITSKeyword") {
                            QString name = xml.attributes().value("name").toString();
                            QString val = xml.attributes().value("value").toString();
                            QString comment = xml.attributes().value("comment").toString();
                            
                            // Store in raw headers
                            info.meta.rawHeaders.push_back({name, val, comment});
                            
                            // Extract specific metadata
                            if (name == "FOCALLEN") info.meta.focalLength = val.toDouble();
                            else if (name == "XPIXSZ" || name == "PIXSIZE") info.meta.pixelSize = val.toDouble();
                            else if (name == "RA") info.meta.ra = val.toDouble();
                            else if (name == "DEC") info.meta.dec = val.toDouble();
                            else if (name == "DATE-OBS") info.meta.dateObs = val;
                            else if (name == "OBJECT") info.meta.objectName = val;
                            // WCS Keywords
                            else if (name == "CRVAL1") info.meta.ra = val.toDouble();
                            else if (name == "CRVAL2") info.meta.dec = val.toDouble();
                            else if (name == "CRPIX1") info.meta.crpix1 = val.toDouble();
                            else if (name == "CRPIX2") info.meta.crpix2 = val.toDouble();
                            else if (name == "CD1_1") info.meta.cd1_1 = val.toDouble();
                            else if (name == "CD1_2") info.meta.cd1_2 = val.toDouble();
                            else if (name == "CD2_1") info.meta.cd2_1 = val.toDouble();
                            else if (name == "CD2_2") info.meta.cd2_2 = val.toDouble();
                            else if (name == "CTYPE1") info.meta.ctype1 = val;
                            else if (name == "CTYPE2") info.meta.ctype2 = val;
                            else if (name == "EQUINOX") info.meta.equinox = val.toDouble();
                            // SIP coefficients
                            else if (name == "A_ORDER") info.meta.sipOrderA = val.toInt();
                            else if (name == "B_ORDER") info.meta.sipOrderB = val.toInt();
                            else if (name == "AP_ORDER") info.meta.sipOrderAP = val.toInt();
                            else if (name == "BP_ORDER") info.meta.sipOrderBP = val.toInt();
                            else if (name.startsWith("A_") || name.startsWith("B_") ||
                                     name.startsWith("AP_") || name.startsWith("BP_")) {
                                info.meta.sipCoeffs[name] = val.toDouble();
                            }
                        }
                        else if (childName == "Property") {
                            XISFProperty prop = parseProperty(xml);
                            if (!prop.id.isEmpty()) {
                                info.properties[prop.id] = prop;
                                
                                if (prop.id == "PCL:AstrometricSolution:ReferenceImageCoordinates") {
                                    // Format: [x, y] - reference pixel
                                    QVariantList coords = prop.value.toList();
                                    if (coords.size() >= 2) {
                                        info.meta.crpix1 = coords[0].toDouble();
                                        info.meta.crpix2 = coords[1].toDouble();
                                    }
                                }
                                else if (prop.id == "PCL:AstrometricSolution:ReferenceCelestialCoordinates") {
                                    // Format: [ra, dec] in degrees
                                    QVariantList coords = prop.value.toList();
                                    if (coords.size() >= 2) {
                                        info.meta.ra = coords[0].toDouble();
                                        info.meta.dec = coords[1].toDouble();
                                    }
                                }
                                else if (prop.id == "PCL:AstrometricSolution:LinearTransformationMatrix") {
                                    // 2x2 matrix: [[cd1_1, cd1_2], [cd2_1, cd2_2]]
                                    QVariantList matrix = prop.value.toList();
                                    if (matrix.size() >= 2) {
                                        QVariantList row0 = matrix[0].toList();
                                        QVariantList row1 = matrix[1].toList();
                                        if (row0.size() >= 2 && row1.size() >= 2) {
                                            info.meta.cd1_1 = row0[0].toDouble();
                                            info.meta.cd1_2 = row0[1].toDouble();
                                            info.meta.cd2_1 = row1[0].toDouble();
                                            info.meta.cd2_2 = row1[1].toDouble();
                                        }
                                    }
                                }
                            }
                        }
                        else if (childName == "Resolution") {
                            info.resolutionH = xml.attributes().value("horizontal").toDouble();
                            info.resolutionV = xml.attributes().value("vertical").toDouble();
                            QString unit = xml.attributes().value("unit").toString();
                            if (!unit.isEmpty()) info.resolutionUnit = unit;
                        }
                        else if (childName == "ICCProfile") {
                            info.hasICCProfile = true;
                        }
                        else if (childName == "Thumbnail") {
                            info.hasThumbnail = true;
                        }
                    }
                }
            }
            else if (elemName == "Metadata") {
                // Parse file-level metadata
                while (!(xml.isEndElement() && xml.name() == "Metadata") && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == "Property") {
                        XISFProperty prop = parseProperty(xml);
                        if (!prop.id.isEmpty()) {
                            info.properties[prop.id] = prop;
                        }
                    }
                }
            }
        }
    }
    
    if (xml.hasError()) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "XML Parse Error: %1").arg(xml.errorString());
        return false;
    }
    
    return foundImage;
}

XISFReader::XISFProperty XISFReader::parseProperty(const QXmlStreamReader& xml) {
    XISFProperty prop;
    prop.id = xml.attributes().value("id").toString();
    prop.type = xml.attributes().value("type").toString();
    prop.format = xml.attributes().value("format").toString();
    prop.comment = xml.attributes().value("comment").toString();
    
    QString valueStr = xml.attributes().value("value").toString();
    int length = xml.attributes().value("length").toInt();
    int rows = xml.attributes().value("rows").toInt();
    int columns = xml.attributes().value("columns").toInt();
    
    // Get text content for inline/embedded data
    // Note: For a real implementation, we'd need to read until end element
    QString textContent;
    
    prop.value = parsePropertyValue(prop.type, valueStr, textContent, length, rows, columns);
    
    return prop;
}

QVariant XISFReader::parsePropertyValue(const QString& type, const QString& valueStr,
                                        const QString& textContent, int length,
                                        int rows, int columns) {
    Q_UNUSED(textContent);
    Q_UNUSED(length);
    Q_UNUSED(rows);
    Q_UNUSED(columns);
    
    // Handle scalar types
    if (type == "Boolean") {
        return valueStr.toLower() == "true";
    }
    else if (type == "Int8" || type == "Byte") {
        return valueStr.toInt();
    }
    else if (type == "Int16" || type == "Short") {
        return valueStr.toInt();
    }
    else if (type == "Int32" || type == "Int") {
        return valueStr.toInt();
    }
    else if (type == "Int64") {
        return valueStr.toLongLong();
    }
    else if (type == "UInt8") {
        return valueStr.toUInt();
    }
    else if (type == "UInt16") {
        return valueStr.toUInt();
    }
    else if (type == "UInt32" || type == "UInt") {
        return valueStr.toUInt();
    }
    else if (type == "UInt64") {
        return valueStr.toULongLong();
    }
    else if (type == "Float32" || type == "Float") {
        return valueStr.toFloat();
    }
    else if (type == "Float64" || type == "Double") {
        return valueStr.toDouble();
    }
    else if (type == "String") {
        return valueStr.isEmpty() ? textContent : valueStr;
    }
    else if (type == "TimePoint") {
        return valueStr;  // Keep as string for now
    }
    
    // For Vector/Matrix types, would need to parse from inline/attached data
    // Return empty for now - full implementation would decode the data block
    return valueStr;
}

QByteArray XISFReader::readDataBlock(QFile& file, const XISFHeaderInfo& info, QString* errorMsg) {
    switch (info.locationType) {
        case XISFHeaderInfo::Attachment:
            return readAttachedDataBlock(file, info.dataLocation, info.dataSize, errorMsg);
            
        case XISFHeaderInfo::Inline:
            // For inline data, we need to get it from the property text
            // This would have been captured during header parsing
            if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
                "Inline XISF data not fully supported yet.");
            return QByteArray();
            
        case XISFHeaderInfo::Embedded:
            if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
                "Embedded XISF data not fully supported yet.");
            return QByteArray();
            
        default:
            if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
                "Unknown data location type.");
            return QByteArray();
    }
}

QByteArray XISFReader::readAttachedDataBlock(QFile& file, long long position, long long size, QString* errorMsg) {
    if (!file.seek(position)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "Seek error to data block at %1").arg(position);
        return QByteArray();
    }
    
    QByteArray data = file.read(size);
    if (data.size() != size) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "Incomplete data read: expected %1 bytes, got %2").arg(size).arg(data.size());
        return QByteArray();
    }
    
    return data;
}

QByteArray XISFReader::decodeInlineData(const QString& data, const QString& encoding, QString* errorMsg) {
    if (encoding.toLower() == "base64") {
        return QByteArray::fromBase64(data.toLatin1());
    }
    else if (encoding.toLower() == "hex") {
        return QByteArray::fromHex(data.toLatin1());
    }
    else {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "Unknown inline encoding: %1").arg(encoding);
        return QByteArray();
    }
}

QByteArray XISFReader::decodeEmbeddedData(const QString& embeddedXml, QString* errorMsg) {
    // Parse the embedded Data element
    QXmlStreamReader xml(embeddedXml);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == "Data") {
            QString encoding = xml.attributes().value("encoding").toString();
            QString text = xml.readElementText();
            return decodeInlineData(text, encoding, errorMsg);
        }
    }
    
    if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
        "Could not parse embedded data element.");
    return QByteArray();
}

int XISFReader::getSampleSize(const QString& format) {
    if (format == "Float32" || format == "Float") return 4;
    if (format == "Float64" || format == "Double") return 8;
    if (format == "UInt16" || format == "Int16" || format == "Short" || format == "UShort") return 2;
    if (format == "UInt32" || format == "Int32" || format == "Int" || format == "UInt") return 4;
    if (format == "UInt8" || format == "Int8" || format == "Byte") return 1;
    return 4;  // Default to Float32
}

bool XISFReader::convertToFloat(const QByteArray& rawData, const XISFHeaderInfo& info,
                                std::vector<float>& outData, QString* errorMsg) {
    long long totalPixels = (long long)info.width * info.height * info.channels;
    int sampleSize = getSampleSize(info.sampleFormat);
    
    if (rawData.size() < totalPixels * sampleSize) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "Data block too small: expected %1 bytes, got %2")
            .arg(totalPixels * sampleSize).arg(rawData.size());
        return false;
    }
    
    outData.resize(totalPixels);
    
    if (info.sampleFormat == "Float32" || info.sampleFormat == "Float") {
        const float* fptr = reinterpret_cast<const float*>(rawData.constData());
        for (long long i = 0; i < totalPixels; ++i) {
            outData[i] = fptr[i];
        }
    }
    else if (info.sampleFormat == "Float64" || info.sampleFormat == "Double") {
        const double* dptr = reinterpret_cast<const double*>(rawData.constData());
        for (long long i = 0; i < totalPixels; ++i) {
            outData[i] = static_cast<float>(dptr[i]);
        }
    }
    else if (info.sampleFormat == "UInt16") {
        const quint16* uptr = reinterpret_cast<const quint16*>(rawData.constData());
        for (long long i = 0; i < totalPixels; ++i) {
            outData[i] = static_cast<float>(uptr[i]) / 65535.0f;
        }
    }
    else if (info.sampleFormat == "UInt32") {
        const quint32* uptr = reinterpret_cast<const quint32*>(rawData.constData());
        for (long long i = 0; i < totalPixels; ++i) {
            outData[i] = static_cast<float>(uptr[i]) / 4294967295.0f;
        }
    }
    else if (info.sampleFormat == "UInt8") {
        const quint8* uptr = reinterpret_cast<const quint8*>(rawData.constData());
        for (long long i = 0; i < totalPixels; ++i) {
            outData[i] = static_cast<float>(uptr[i]) / 255.0f;
        }
    }
    else {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "Unsupported sample format: %1").arg(info.sampleFormat);
        return false;
    }
    
    return true;
}

void XISFReader::planarToInterleaved(const std::vector<float>& planar,
                                     std::vector<float>& interleaved,
                                     int width, int height, int channels) {
    long planeSize = (long)width * height;
    
    for (int c = 0; c < channels; ++c) {
        for (long i = 0; i < planeSize; ++i) {
            interleaved[i * channels + c] = planar[(long)c * planeSize + i];
        }
    }
}

// ============================================================================
// Multi-Image Support (SASPro compatible)
// ============================================================================

QList<XISFImageInfo> XISFReader::listImages(const QString& filePath, QString* errorMsg) {
    QList<XISFImageInfo> result;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Could not open file: %1").arg(filePath);
        return result;
    }

    // Read Signature
    QByteArray sig = file.read(8);
    if (sig != "XISF0100") {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Invalid XISF Signature.");
        return result;
    }

    // Read Header Length
    uchar lenBytes[4];
    if (file.read((char*)lenBytes, 4) != 4) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Unexpected EOF.");
        return result;
    }
    quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);

    // Skip Reserved
    file.read(4);

    // Read XML Header
    if (headerLen == 0 || headerLen > 100 * 1024 * 1024) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Invalid Header Length: %1").arg(headerLen);
        return result;
    }
    
    QByteArray xmlBytes = file.read(headerLen);
    
    // Parse all images from header
    QList<XISFHeaderInfo> images;
    if (!parseAllImages(xmlBytes, images, errorMsg)) {
        return result;
    }
    
    // Convert to public info structure
    for (int i = 0; i < images.size(); ++i) {
        const XISFHeaderInfo& info = images[i];
        XISFImageInfo imgInfo;
        imgInfo.index = i;
        imgInfo.name = info.imageId.isEmpty() ? QString::number(i) : info.imageId;
        imgInfo.width = info.width;
        imgInfo.height = info.height;
        imgInfo.channels = info.channels;
        imgInfo.sampleFormat = info.sampleFormat;
        imgInfo.colorSpace = info.colorSpace;
        result.append(imgInfo);
    }
    
    return result;
}

bool XISFReader::readImage(const QString& filePath, int imageIndex, 
                           ImageBuffer& buffer, QString* errorMsg) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Could not open file: %1").arg(filePath);
        return false;
    }

    // Read Signature
    QByteArray sig = file.read(8);
    if (sig != "XISF0100") {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Invalid XISF Signature.");
        return false;
    }

    // Read Header Length
    uchar lenBytes[4];
    if (file.read((char*)lenBytes, 4) != 4) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", "Unexpected EOF.");
        return false;
    }
    quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);

    // Skip Reserved
    file.read(4);

    // Read XML Header
    QByteArray xmlBytes = file.read(headerLen);
    
    // Parse all images
    QList<XISFHeaderInfo> images;
    if (!parseAllImages(xmlBytes, images, errorMsg)) {
        return false;
    }
    
    if (imageIndex < 0 || imageIndex >= images.size()) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "Image index %1 out of range (file has %2 images)").arg(imageIndex).arg(images.size());
        return false;
    }
    
    XISFHeaderInfo& info = images[imageIndex];
    info.meta.filePath = filePath;
    
    return loadImage(file, info, buffer, errorMsg);
}

bool XISFReader::parseAllImages(const QByteArray& headerXml, QList<XISFHeaderInfo>& images, QString* errorMsg) {
    QXmlStreamReader xml(headerXml);
    
    int imageIndex = 0;
    
    while (!xml.atEnd()) {
        xml.readNext();
        
        if (xml.isStartElement() && xml.name() == "Image") {
            XISFHeaderInfo info;
            
            // Parse geometry
            QString geom = xml.attributes().value("geometry").toString();
            QStringList parts = geom.split(':');
            if (parts.size() >= 2) {
                info.width = parts[0].toInt();
                info.height = parts[1].toInt();
                info.channels = (parts.size() > 2) ? parts[2].toInt() : 1;
            }
            
            // Skip invalid images
            if (info.width <= 0 || info.height <= 0) {
                continue;
            }
            
            // Image ID
            info.imageId = xml.attributes().value("id").toString();
            if (info.imageId.isEmpty()) {
                info.imageId = QString("Image_%1").arg(imageIndex);
            }
            
            // Sample format
            info.sampleFormat = xml.attributes().value("sampleFormat").toString();
            if (info.sampleFormat.isEmpty()) info.sampleFormat = "Float32";
            
            // Color space
            info.colorSpace = xml.attributes().value("colorSpace").toString();
            
            // Pixel storage
            info.pixelStorage = xml.attributes().value("pixelStorage").toString();
            
            // Byte order
            info.byteOrder = xml.attributes().value("byteOrder").toString();
            
            // Parse location
            QString loc = xml.attributes().value("location").toString();
            QStringList locParts = loc.split(':');
            if (locParts.size() >= 1) {
                QString locType = locParts[0].toLower();
                if (locType == "attachment" && locParts.size() >= 3) {
                    info.locationType = XISFHeaderInfo::Attachment;
                    info.dataLocation = locParts[1].toLongLong();
                    info.dataSize = locParts[2].toLongLong();
                } else if (locType == "inline" && locParts.size() >= 2) {
                    info.locationType = XISFHeaderInfo::Inline;
                    info.inlineEncoding = locParts[1];
                } else if (locType == "embedded") {
                    info.locationType = XISFHeaderInfo::Embedded;
                }
            }
            
            // Parse compression
            QString compression = xml.attributes().value("compression").toString();
            if (!compression.isEmpty()) {
                CompressionUtils::parseCompressionAttr(
                    compression, info.compressionCodec, 
                    info.uncompressedSize, info.shuffleItemSize);
            }
            
            // Parse Image children (FITSKeywords, Properties)
            while (!(xml.isEndElement() && xml.name() == "Image") && !xml.atEnd()) {
                xml.readNext();
                
                if (xml.isStartElement()) {
                    QString childName = xml.name().toString();
                    
                    if (childName == "FITSKeyword") {
                        QString name = xml.attributes().value("name").toString();
                        QString val = xml.attributes().value("value").toString();
                        QString comment = xml.attributes().value("comment").toString();
                        
                        info.meta.rawHeaders.push_back({name, val, comment});
                        
                        if (name == "FOCALLEN") info.meta.focalLength = val.toDouble();
                        else if (name == "XPIXSZ" || name == "PIXSIZE") info.meta.pixelSize = val.toDouble();
                        else if (name == "RA") info.meta.ra = val.toDouble();
                        else if (name == "DEC") info.meta.dec = val.toDouble();
                        else if (name == "DATE-OBS") info.meta.dateObs = val;
                        else if (name == "OBJECT") info.meta.objectName = val;
                        else if (name == "CRVAL1") info.meta.ra = val.toDouble();
                        else if (name == "CRVAL2") info.meta.dec = val.toDouble();
                        else if (name == "CRPIX1") info.meta.crpix1 = val.toDouble();
                        else if (name == "CRPIX2") info.meta.crpix2 = val.toDouble();
                        else if (name == "CD1_1") info.meta.cd1_1 = val.toDouble();
                        else if (name == "CD1_2") info.meta.cd1_2 = val.toDouble();
                        else if (name == "CD2_1") info.meta.cd2_1 = val.toDouble();
                        else if (name == "CD2_2") info.meta.cd2_2 = val.toDouble();
                    }
                    else if (childName == "Property") {
                        XISFProperty prop = parseProperty(xml);
                        info.properties[prop.id] = prop;
                    }
                }
            }
            
            images.append(info);
            imageIndex++;
        }
    }
    
    if (xml.hasError()) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFReader", 
            "XML Parse Error: %1").arg(xml.errorString());
        return false;
    }
    
    return true;
}

bool XISFReader::loadImage(QFile& file, const XISFHeaderInfo& info, ImageBuffer& buffer, QString* errorMsg) {
    // Read Data Block
    QByteArray rawData = readDataBlock(file, info, errorMsg);
    if (rawData.isEmpty() && info.dataSize > 0) {
        return false;
    }
    
    // Handle Compression
    if (info.compressionCodec != CompressionUtils::Codec_None) {
        QByteArray decompressed = CompressionUtils::decompress(
            rawData, info.compressionCodec, info.uncompressedSize, errorMsg);
        if (decompressed.isEmpty()) {
            return false;
        }
        
        if (info.shuffleItemSize > 0) {
            rawData = CompressionUtils::unshuffle(decompressed, info.shuffleItemSize);
        } else {
            rawData = decompressed;
        }
    }
    
    // Convert to float
    std::vector<float> pixelData;
    if (!convertToFloat(rawData, info, pixelData, errorMsg)) {
        return false;
    }
    
    // Convert planar to interleaved if needed
    long long totalPixels = (long long)info.width * info.height * info.channels;
    std::vector<float> finalData;
    
    if (info.channels > 1 && (info.pixelStorage.isEmpty() || info.pixelStorage.toLower() == "planar")) {
        finalData.resize(totalPixels);
        planarToInterleaved(pixelData, finalData, info.width, info.height, info.channels);
    } else {
        finalData = std::move(pixelData);
    }
    
    // Set buffer data and metadata
    buffer.setMetadata(info.meta);
    buffer.setData(info.width, info.height, info.channels, finalData);
    
    return true;
}

