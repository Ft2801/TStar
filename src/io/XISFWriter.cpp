#include "XISFWriter.h"
#include "CompressionUtils.h"
#include <QFile>
#include <QDomDocument>
#include <QtEndian>
#include <QDateTime>
#include <QSet>
#include <QDebug>
#include <QCoreApplication>
#include <cmath>

bool XISFWriter::write(const QString& filePath, const ImageBuffer& buffer, 
                       int depthInt, QString* errorMsg) {
    ImageBuffer::BitDepth depth = static_cast<ImageBuffer::BitDepth>(depthInt);
    WriteOptions options;
    return write(filePath, buffer, depth, options, errorMsg);
}

bool XISFWriter::write(const QString& filePath, const ImageBuffer& buffer,
                       ImageBuffer::BitDepth depth, const WriteOptions& options,
                       QString* errorMsg) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFWriter", 
            "Could not open file for writing: %1").arg(filePath);
        return false;
    }

    int w = buffer.width();
    int h = buffer.height();
    int c = buffer.channels();
    
    // Prepare image data (interleaved -> planar, convert to target format)
    QByteArray rawData = prepareImageData(buffer, depth);
    if (rawData.isEmpty()) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFWriter", 
            "Failed to prepare image data.");
        return false;
    }
    
    // Apply compression if requested
    QByteArray dataToWrite = rawData;
    QString compressionAttr;
    
    if (options.codec != CompressionUtils::Codec_None) {
        QByteArray toCompress = rawData;
        int itemSize = bytesPerSample(depth);
        
        // Apply byte shuffling if requested
        if (options.shuffle && itemSize > 1) {
            toCompress = CompressionUtils::shuffle(rawData, itemSize);
        }
        
        // Compress
        QByteArray compressed = CompressionUtils::compress(
            toCompress, options.codec, options.compressionLevel, errorMsg);
        
        // Only use compression if it actually reduces size
        if (!compressed.isEmpty() && compressed.size() < rawData.size()) {
            dataToWrite = compressed;
            compressionAttr = CompressionUtils::buildCompressionAttr(
                options.codec, rawData.size(), 
                options.shuffle ? itemSize : 0);
            qDebug() << "XISF compression:" << rawData.size() << "->" 
                     << compressed.size() << "bytes"
                     << "(" << QString::number(100.0 * compressed.size() / rawData.size(), 'f', 1) << "%)";
        } else {
            // Compression didn't help or failed, write uncompressed
            qDebug() << "XISF compression skipped (no size reduction)";
        }
    }
    
    quint64 dataSize = dataToWrite.size();
    
    // Iteratively build header to stabilize attachment position
    // (changing position changes header size which changes position...)
    quint64 estimatedPos = 4096;  // Initial guess
    QByteArray finalXml;
    
    for (int iteration = 0; iteration < 5; ++iteration) {
        quint64 alignedPos = alignPosition(estimatedPos, options.blockAlignment);
        
        finalXml = buildHeader(buffer, depth, options, dataSize, alignedPos, compressionAttr);
        
        quint64 actualPos = 16 + finalXml.size();  // sig(8) + len(4) + reserved(4) + xml
        actualPos = alignPosition(actualPos, options.blockAlignment);
        
        if (actualPos == alignedPos) {
            break;  // Stable!
        }
        estimatedPos = actualPos;
    }
    
    quint64 attachmentPos = alignPosition(16 + finalXml.size(), options.blockAlignment);
    
    // Write file
    // 1. Signature
    file.write("XISF0100", 8);
    
    // 2. Header Length (UInt32 Little Endian)
    quint32 len32 = static_cast<quint32>(finalXml.size());
    len32 = qToLittleEndian(len32);
    file.write(reinterpret_cast<const char*>(&len32), 4);
    
    // 3. Reserved (4 bytes zero)
    quint32 zero = 0;
    file.write(reinterpret_cast<const char*>(&zero), 4);
    
    // 4. XML Header
    file.write(finalXml);
    
    // 5. Padding to alignment
    qint64 currentPos = file.pos();
    if (currentPos < static_cast<qint64>(attachmentPos)) {
        int padding = static_cast<int>(attachmentPos - currentPos);
        QByteArray pads(padding, 0);
        file.write(pads);
    }
    
    // 6. Data block
    file.write(dataToWrite);
    
    return true;
}

QByteArray XISFWriter::prepareImageData(const ImageBuffer& buffer, ImageBuffer::BitDepth depth) {
    int w = buffer.width();
    int h = buffer.height();
    int c = buffer.channels();
    quint64 pixelCount = static_cast<quint64>(w) * h * c;
    
    const std::vector<float>& srcData = buffer.data();
    QByteArray result;
    
    if (depth == ImageBuffer::Depth_16Int) {
        result.resize(pixelCount * sizeof(quint16));
        quint16* outPtr = reinterpret_cast<quint16*>(result.data());
        
        // Interleaved to planar conversion
        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    size_t srcIdx = (y * w + x) * c + ch;
                    size_t dstIdx = static_cast<size_t>(ch) * (w * h) + (y * w + x);
                    float v = srcData[srcIdx];
                    outPtr[dstIdx] = static_cast<quint16>(std::clamp(v * 65535.0f, 0.0f, 65535.0f));
                }
            }
        }
    }
    else if (depth == ImageBuffer::Depth_32Int) {
        result.resize(pixelCount * sizeof(quint32));
        quint32* outPtr = reinterpret_cast<quint32*>(result.data());
        
        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    size_t srcIdx = (y * w + x) * c + ch;
                    size_t dstIdx = static_cast<size_t>(ch) * (w * h) + (y * w + x);
                    float v = srcData[srcIdx];
                    double vd = static_cast<double>(v) * 4294967295.0;
                    vd = std::clamp(vd, 0.0, 4294967295.0);
                    outPtr[dstIdx] = static_cast<quint32>(vd);
                }
            }
        }
    }
    else {
        // Float32 (Default)
        result.resize(pixelCount * sizeof(float));
        float* outPtr = reinterpret_cast<float*>(result.data());
        
        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    size_t srcIdx = (y * w + x) * c + ch;
                    size_t dstIdx = static_cast<size_t>(ch) * (w * h) + (y * w + x);
                    if (srcIdx < srcData.size()) {
                        outPtr[dstIdx] = srcData[srcIdx];
                    }
                }
            }
        }
    }
    
    return result;
}

QByteArray XISFWriter::buildHeader(const ImageBuffer& buffer, ImageBuffer::BitDepth depth,
                                   const WriteOptions& options,
                                   quint64 dataSize, quint64 attachmentPosition,
                                   const QString& compressionAttr) {
    int w = buffer.width();
    int h = buffer.height();
    int c = buffer.channels();
    
    QDomDocument doc;
    
    // Root: xisf
    QDomElement root = doc.createElement("xisf");
    root.setAttribute("version", "1.0");
    root.setAttribute("xmlns", "http://www.pixinsight.com/xisf");
    root.setAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
    root.setAttribute("xsi:schemaLocation", "http://www.pixinsight.com/xisf http://pixinsight.com/xisf/xisf-1.0.xsd");
    doc.appendChild(root);
    
    // Image element
    QDomElement img = doc.createElement("Image");
    QString geom = QString("%1:%2:%3").arg(w).arg(h).arg(c);
    img.setAttribute("geometry", geom);
    img.setAttribute("sampleFormat", sampleFormatString(depth));
    img.setAttribute("bounds", "0:1");
    img.setAttribute("colorSpace", (c == 1) ? "Gray" : "RGB");
    img.setAttribute("pixelStorage", "planar");
    img.setAttribute("byteOrder", "little");
    
    // Location with optional compression
    QString location = QString("attachment:%1:%2").arg(attachmentPosition).arg(dataSize);
    img.setAttribute("location", location);
    
    // Add compression attribute if present
    if (!compressionAttr.isEmpty()) {
        img.setAttribute("compression", compressionAttr);
    }
    
    root.appendChild(img);
    
    // Add FITS keywords from metadata
    addFITSKeywords(doc, img, buffer.metadata());
    
    // Add file-level Metadata element
    QDomElement metadata = doc.createElement("Metadata");
    
    // Add creation time property
    QDomElement creationTime = doc.createElement("Property");
    creationTime.setAttribute("id", "XISF:CreationTime");
    creationTime.setAttribute("type", "TimePoint");
    creationTime.setAttribute("value", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    metadata.appendChild(creationTime);
    
    // Add creator application property
    QDomElement creatorApp = doc.createElement("Property");
    creatorApp.setAttribute("id", "XISF:CreatorApplication");
    creatorApp.setAttribute("type", "String");
    creatorApp.setAttribute("value", options.creatorApp);
    metadata.appendChild(creatorApp);
    
    // Add block alignment property
    QDomElement blockAlign = doc.createElement("Property");
    blockAlign.setAttribute("id", "XISF:BlockAlignmentSize");
    blockAlign.setAttribute("type", "UInt32");
    blockAlign.setAttribute("value", QString::number(options.blockAlignment));
    metadata.appendChild(blockAlign);
    
    // Add compression codec info if compression was used
    if (!compressionAttr.isEmpty()) {
        QDomElement compCodecs = doc.createElement("Property");
        compCodecs.setAttribute("id", "XISF:CompressionCodecs");
        compCodecs.setAttribute("type", "String");
        compCodecs.setAttribute("value", CompressionUtils::codecName(options.codec));
        metadata.appendChild(compCodecs);
    }
    
    root.appendChild(metadata);
    
    return doc.toByteArray();
}

void XISFWriter::addFITSKeywords(QDomDocument& doc, QDomElement& imageElem,
                                 const ImageBuffer::Metadata& meta) {
    QSet<QString> writtenKeys;
    
    // Helper lambda
    auto addKW = [&](const QString& key, const QString& value, const QString& comment = QString()) {
        QDomElement kw = doc.createElement("FITSKeyword");
        kw.setAttribute("name", key);
        kw.setAttribute("value", value);
        if (!comment.isEmpty()) kw.setAttribute("comment", comment);
        imageElem.appendChild(kw);
        writtenKeys.insert(key.toUpper());
    };
    
    // Check if we have valid WCS
    double cd_det = meta.cd1_1 * meta.cd2_2 - meta.cd1_2 * meta.cd2_1;
    bool hasValidWCS = (meta.ra != 0 || meta.dec != 0) && std::abs(cd_det) > 1e-20;
    
    if (hasValidWCS) {
        // CTYPE keywords
        QString ctype1 = meta.ctype1.isEmpty() ? "RA---TAN" : meta.ctype1;
        QString ctype2 = meta.ctype2.isEmpty() ? "DEC--TAN" : meta.ctype2;
        addKW("CTYPE1", QString("'%1'").arg(ctype1), "Coordinate type");
        addKW("CTYPE2", QString("'%1'").arg(ctype2), "Coordinate type");
        addKW("EQUINOX", QString::number(meta.equinox, 'f', 1), "Equinox of coordinates");
        
        // Reference coordinates
        addKW("CRVAL1", QString::number(meta.ra, 'f', 9));
        addKW("CRVAL2", QString::number(meta.dec, 'f', 9));
        addKW("CRPIX1", QString::number(meta.crpix1, 'f', 3));
        addKW("CRPIX2", QString::number(meta.crpix2, 'f', 3));
        
        // CD matrix
        addKW("CD1_1", QString::number(meta.cd1_1, 'e', 10));
        addKW("CD1_2", QString::number(meta.cd1_2, 'e', 10));
        addKW("CD2_1", QString::number(meta.cd2_1, 'e', 10));
        addKW("CD2_2", QString::number(meta.cd2_2, 'e', 10));
        
        // SIP coefficients if present
        if (meta.sipOrderA > 0) {
            addKW("A_ORDER", QString::number(meta.sipOrderA));
        }
        if (meta.sipOrderB > 0) {
            addKW("B_ORDER", QString::number(meta.sipOrderB));
        }
        if (meta.sipOrderAP > 0) {
            addKW("AP_ORDER", QString::number(meta.sipOrderAP));
        }
        if (meta.sipOrderBP > 0) {
            addKW("BP_ORDER", QString::number(meta.sipOrderBP));
        }
        
        // Write SIP coefficients
        for (auto it = meta.sipCoeffs.constBegin(); it != meta.sipCoeffs.constEnd(); ++it) {
            addKW(it.key(), QString::number(it.value(), 'e', 15));
        }
    }
    
    // Add other metadata
    if (meta.focalLength > 0 && !writtenKeys.contains("FOCALLEN")) {
        addKW("FOCALLEN", QString::number(meta.focalLength, 'f', 2), "Focal length (mm)");
    }
    if (meta.pixelSize > 0 && !writtenKeys.contains("XPIXSZ")) {
        addKW("XPIXSZ", QString::number(meta.pixelSize, 'f', 3), "Pixel size (um)");
    }
    if (!meta.objectName.isEmpty() && !writtenKeys.contains("OBJECT")) {
        addKW("OBJECT", QString("'%1'").arg(meta.objectName), "Object name");
    }
    if (!meta.dateObs.isEmpty() && !writtenKeys.contains("DATE-OBS")) {
        addKW("DATE-OBS", QString("'%1'").arg(meta.dateObs), "Observation date");
    }
    
    // Write all raw headers that haven't been written yet
    for (const auto& card : meta.rawHeaders) {
        QString keyUpper = card.key.toUpper();
        if (writtenKeys.contains(keyUpper)) continue;
        
        // Skip structural keywords
        if (keyUpper == "SIMPLE" || keyUpper == "BITPIX" || 
            keyUpper == "NAXIS" || keyUpper.startsWith("NAXIS") ||
            keyUpper == "EXTEND" || keyUpper == "BZERO" || keyUpper == "BSCALE") {
            continue;
        }
        
        QDomElement kw = doc.createElement("FITSKeyword");
        kw.setAttribute("name", card.key);
        kw.setAttribute("value", card.value);
        if (!card.comment.isEmpty()) {
            kw.setAttribute("comment", card.comment);
        }
        imageElem.appendChild(kw);
        writtenKeys.insert(keyUpper);
    }
}

quint64 XISFWriter::alignPosition(quint64 pos, int alignment) {
    if (alignment <= 0) return pos;
    quint64 rem = pos % alignment;
    if (rem == 0) return pos;
    return pos + (alignment - rem);
}

QString XISFWriter::sampleFormatString(ImageBuffer::BitDepth depth) {
    switch (depth) {
        case ImageBuffer::Depth_16Int: return "UInt16";
        case ImageBuffer::Depth_32Int: return "UInt32";
        case ImageBuffer::Depth_32Float:
        default: return "Float32";
    }
}

int XISFWriter::bytesPerSample(ImageBuffer::BitDepth depth) {
    switch (depth) {
        case ImageBuffer::Depth_16Int: return 2;
        case ImageBuffer::Depth_32Int: return 4;
        case ImageBuffer::Depth_32Float:
        default: return 4;
    }
}
