#include "XISFWriter.h"
#include <QFile>
#include <QDomDocument>
#include <QtEndian>
#include <QDateTime>
#include <QSet>
#include <QDebug>
#include <QCoreApplication>

bool XISFWriter::write(const QString& filePath, const ImageBuffer& buffer, int depthInt, QString* errorMsg) {
    ImageBuffer::BitDepth depth = (ImageBuffer::BitDepth)depthInt;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFWriter", "Could not open file for writing: %1").arg(filePath);
        return false;
    }

    int w = buffer.width();
    int h = buffer.height();
    int c = buffer.channels();
    quint64 pixelCount = (quint64)w * h * c;
    
    // Determine Output Type
    QString sampleFormat;
    quint64 dataSize;
    QByteArray dataBytes;
    
    if (depth == ImageBuffer::Depth_16Int) {
        sampleFormat = "UInt16";
        dataSize = pixelCount * sizeof(quint16);
        dataBytes.resize(dataSize);
        quint16* outPtr = reinterpret_cast<quint16*>(dataBytes.data());
        const std::vector<float>& srcData = buffer.data();
        
        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                 for (int x = 0; x < w; ++x) {
                     size_t srcIdx = (y * w + x) * c + ch;
                     size_t dstIdx = (size_t)ch * (w * h) + (y * w + x);
                     float v = srcData[srcIdx];
                     outPtr[dstIdx] = (quint16)std::clamp(v * 65535.0f, 0.0f, 65535.0f);
                 }
            }
        }
    } else if (depth == ImageBuffer::Depth_32Int) {
        sampleFormat = "UInt32"; // XISF supports UInt32 usually
        dataSize = pixelCount * sizeof(quint32);
        dataBytes.resize(dataSize);
        quint32* outPtr = reinterpret_cast<quint32*>(dataBytes.data());
        const std::vector<float>& srcData = buffer.data();
        
        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                 for (int x = 0; x < w; ++x) {
                     size_t srcIdx = (y * w + x) * c + ch;
                     size_t dstIdx = (size_t)ch * (w * h) + (y * w + x);
                     float v = srcData[srcIdx];
                     // Full 32-bit unsigned range
                     double vd = (double)v * 4294967295.0;
                     if (vd > 4294967295.0) vd = 4294967295.0;
                     if (vd < 0.0) vd = 0.0;
                     outPtr[dstIdx] = (quint32)vd;
                 }
            }
        }
    } else {
        // Float32 (Default)
        sampleFormat = "Float32";
        dataSize = pixelCount * sizeof(float);
        dataBytes.resize(dataSize);
        float* outPtr = reinterpret_cast<float*>(dataBytes.data());
        const std::vector<float>& srcData = buffer.data();
        
        for (int ch = 0; ch < c; ++ch) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    size_t srcIdx = (y * w + x) * c + ch;
                    size_t dstIdx = (size_t)ch * (w * h) + (y * w + x);
                    if (srcIdx < srcData.size()) {
                        outPtr[dstIdx] = srcData[srcIdx];
                    }
                }
            }
        }
    }
    
    // --- Header Geometry Negotiation ---
    // Start with minimal header length estimate
    // Loop to stabilize attachment position
    
    QDomDocument doc;
    QByteArray finalXml;
    quint64 finalHeaderLen = 0;
    quint64 attachmentPos = 0;
    
    // Initial guess for position (sig 8 + len 4 + res 4 + header_approx 1000)
    quint64 estimatedPos = 4096; // Just a guess
    
    for (int i = 0; i < 5; ++i) { // Iteration limit
        doc = QDomDocument();
        
        // Root: xisf
        QDomElement root = doc.createElement("xisf");
        root.setAttribute("version", "1.0");
        root.setAttribute("xmlns", "http://www.pixinsight.com/xisf");
        root.setAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
        root.setAttribute("xsi:schemaLocation", "http://www.pixinsight.com/xisf http://pixinsight.com/xisf/xisf-1.0.xsd");
        doc.appendChild(root);
        
        // Image
        QDomElement img = doc.createElement("Image");
        QString geom = QString("%1:%2:%3").arg(w).arg(h).arg(c);
        img.setAttribute("geometry", geom);
        img.setAttribute("sampleFormat", sampleFormat);
        img.setAttribute("bounds", "0:1");
        img.setAttribute("colorSpace", (c==1) ? "Gray" : "RGB");
        img.setAttribute("pixelStorage", "planar"); // We converted to planar
        img.setAttribute("byteOrder", "little");    // Standard for x86/Windows
        img.setAttribute("location", QString("attachment:%1:%2").arg(estimatedPos).arg(dataSize));
        root.appendChild(img);
        
        // Properties / FITS Keywords
        const auto& meta = buffer.metadata();
        
        // Track written keys to avoid duplicates
        QSet<QString> writtenKeys;
        
        // 1. Add WCS Keywords explicitly if we have solving data
        if (meta.ra != 0 || meta.dec != 0) {
            auto addKW = [&](const QString& k, const QString& v, const QString& c = "") {
                QDomElement kw = doc.createElement("FITSKeyword");
                kw.setAttribute("name", k);
                kw.setAttribute("value", v);
                if (!c.isEmpty()) kw.setAttribute("comment", c);
                img.appendChild(kw);
                writtenKeys.insert(k.toUpper());
            };
            
            addKW("CRVAL1", QString::number(meta.ra, 'f', 9));
            addKW("CRVAL2", QString::number(meta.dec, 'f', 9));
            addKW("CRPIX1", QString::number(meta.crpix1, 'f', 3));
            addKW("CRPIX2", QString::number(meta.crpix2, 'f', 3));
            addKW("CD1_1", QString::number(meta.cd1_1, 'e', 10));
            addKW("CD1_2", QString::number(meta.cd1_2, 'e', 10));
            addKW("CD2_1", QString::number(meta.cd2_1, 'e', 10));
            addKW("CD2_2", QString::number(meta.cd2_2, 'e', 10));
        }

        // 2. Add all other Metadata from rawHeaders
        for(const auto& card : meta.rawHeaders) {
            QString keyUpper = card.key.toUpper();
            if (writtenKeys.contains(keyUpper)) continue;
            
            QDomElement kw = doc.createElement("FITSKeyword");
            kw.setAttribute("name", card.key);
            kw.setAttribute("value", card.value);
            kw.setAttribute("comment", card.comment);
            img.appendChild(kw);
            writtenKeys.insert(keyUpper);
        }

        // Serialize
        finalXml = doc.toByteArray();
        finalHeaderLen = finalXml.size();
        
        quint64 actualPos = 16 + finalHeaderLen;
        
        if (actualPos == estimatedPos) break; // Stable!
        
        // Update guess (maybe add small padding perception?)
        // If string representation length changes (e.g. 999 -> 1000), XML grows.
        // We just set estimatedPos = actualPos and repeat.
        estimatedPos = actualPos;
    }
    
    // Write File
    // 1. Signature
    file.write("XISF0100", 8);
    
    // 2. Header Length (UInt32 Little Endian)
    quint32 len32 = (quint32)finalHeaderLen; 
    len32 = qToLittleEndian(len32);
    file.write(reinterpret_cast<const char*>(&len32), 4);
    
    // 3. Reserved (4 bytes zero)
    quint32 zero = 0;
    file.write(reinterpret_cast<const char*>(&zero), 4);
    
    // 4. XML Header
    file.write(finalXml);
    
    // 5. Padding & Data
    qint64 currentPos = file.pos();
    // estimatedPos from logic above is where we PROMISED the data starts.
    // If we are before that, verify we pad.
    
    if (currentPos < (qint64)estimatedPos) {
        int padding = (int)((qint64)estimatedPos - currentPos);
        QByteArray pads(padding, 0);
        file.write(pads);
    } else if (currentPos > (qint64)estimatedPos) {
        // This theoretically shouldn't happen if the loop logic worked, 
        // but if it does, the file is corrupt.
        if (errorMsg) *errorMsg = QCoreApplication::translate("XISFWriter", "XISF Header Alignment Failed");
        return false;
    }
    
    file.write(dataBytes);
    
    return true;
}
