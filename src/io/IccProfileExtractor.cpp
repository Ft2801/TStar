#include "IccProfileExtractor.h"
#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QDebug>
#include <QtEndian>
#include <QXmlStreamReader>

bool IccProfileExtractor::extractFromFile(const QString& filePath, QByteArray& iccData) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    
    // Try format-specific extractors based on file extension
    if (ext == "tif" || ext == "tiff") {
        return extractFromTiff(filePath, iccData);
    } else if (ext == "png") {
        return extractFromPng(filePath, iccData);
    } else if (ext == "jpg" || ext == "jpeg") {
        return extractFromJpeg(filePath, iccData);
    } else if (ext == "fits" || ext == "fit") {
        return extractFromFits(filePath, iccData);
    } else if (ext == "xisf") {
        return extractFromXisf(filePath, iccData);
    } else if (ext == "cr2" || ext == "crw" || ext == "nef" || ext == "nrw" || 
               ext == "arw" || ext == "dng" || ext == "raf" || ext == "orf" || 
               ext == "rw2" || ext == "raw") {
        return extractFromRaw(filePath, iccData);
    }
    
    return false;
}

bool IccProfileExtractor::extractFromTiff(const QString& filePath, QByteArray& iccData) {
    // TIFF ICC profile tag: 0x8773 (34675 in decimal)
    const quint16 TIFF_ICC_PROFILE_TAG = 34675;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QDataStream stream(&file);
    
    // Read byte order indicator
    quint16 byteOrder;
    stream >> byteOrder;
    if (byteOrder == 0x4949) {
        stream.setByteOrder(QDataStream::LittleEndian);
    } else if (byteOrder == 0x4D4D) {
        stream.setByteOrder(QDataStream::BigEndian);
    } else {
        return false;
    }
    
    // Check TIFF version (should be 42)
    quint16 version;
    stream >> version;
    if (version != 42) {
        return false;
    }
    
    // Read IFD offset
    quint32 ifdOffset;
    stream >> ifdOffset;
    
    // Seek to first IFD
    if (!file.seek(ifdOffset)) {
        return false;
    }
    
    // Read IFD entries
    quint16 numEntries;
    stream >> numEntries;
    
    for (int i = 0; i < numEntries; ++i) {
        quint16 tag, type;
        quint32 count, value;
        stream >> tag >> type >> count >> value;
        
        if (tag == TIFF_ICC_PROFILE_TAG) {
            // Found ICC profile tag
            // Type 7 = UNDEFINED (raw bytes)
            // Value contains either the data (if <= 4 bytes) or offset to data
            qint64 savePos = file.pos();
            
            if (count <= 4) {
                // Data is in the value field
                iccData.clear();
                iccData.resize(count);
                QByteArray temp = QByteArray::fromRawData((char*)&value, count);
                iccData = temp;
            } else {
                // Data is at offset specified by value
                if (file.seek(value)) {
                    iccData = file.read(count);
                }
            }
            
            file.seek(savePos);
            return !iccData.isEmpty();
        }
    }
    
    return false;
}

#include <zlib.h>

bool IccProfileExtractor::extractFromPng(const QString& filePath, QByteArray& iccData) {
    // PNG ICC profile chunk: "iCCP"
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    // PNG signature: 137 80 78 71 13 10 26 10 (in decimal)
    QByteArray pngSig = file.read(8);
    if (pngSig.size() != 8 || pngSig != QByteArray("\x89PNG\r\n\x1a\n")) {
        return false;
    }
    
    // Read chunks
    while (!file.atEnd()) {
        QByteArray lenData = file.read(4);
        if (lenData.size() != 4) break;
        
        quint32 length = (quint8)lenData[0] << 24 | (quint8)lenData[1] << 16 |
                         (quint8)lenData[2] << 8 | (quint8)lenData[3];
        
        QByteArray chunkType = file.read(4);
        if (chunkType.size() != 4) break;
        
        if (chunkType == "iCCP") {
            // Found ICC profile chunk
            // Format: profile name (null-terminated) + compression method + compressed ICC profile
            QByteArray chunkData = file.read(length);
            
            // Skip profile name (read until null terminator)
            int nameLen = chunkData.indexOf('\0') + 1;
            if (nameLen > 0 && nameLen < chunkData.size()) {
                // Skip compression method (1 byte)
                int startIdx = nameLen + 1;
                if (startIdx < chunkData.size()) {
                    // Remaining data is zlib-compressed ICC profile
                    QByteArray compressedData = chunkData.mid(startIdx);
                    
                    // Simple decompression using qUncompress (requires prepending 4-byte size)
                    // But we don't know the uncompressed size easily. 
                    // Better use zlib's uncompress or a loop.
                    // For ICC profiles, we can estimate a max size (e.g., 2MB)
                    unsigned long uncompressedSize = 2 * 1024 * 1024; 
                    iccData.resize(uncompressedSize);
                    
                    int res = uncompress((Bytef*)iccData.data(), &uncompressedSize, 
                                       (const Bytef*)compressedData.data(), compressedData.size());
                    
                    if (res == Z_OK) {
                        iccData.resize(uncompressedSize);
                        return true;
                    } else if (res == Z_BUF_ERROR) {
                        // Buffer too small, try one more time with 10MB (extremely large for ICC)
                        uncompressedSize = 10 * 1024 * 1024;
                        iccData.resize(uncompressedSize);
                        if (uncompress((Bytef*)iccData.data(), &uncompressedSize, 
                                     (const Bytef*)compressedData.data(), compressedData.size()) == Z_OK) {
                            iccData.resize(uncompressedSize);
                            return true;
                        }
                    }
                }
            }
        } else if (chunkType == "IEND") {
            break;  // End of PNG chunks
        } else {
            // Skip other chunks
            file.read(length);
        }
        
        // Skip CRC (4 bytes)
        file.read(4);
    }
    
    return false;
}

bool IccProfileExtractor::extractFromJpeg(const QString& filePath, QByteArray& iccData) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    // Check JPEG SOI marker (0xFFD8)
    quint8 byte1, byte2;
    if (file.read((char*)&byte1, 1) != 1 || file.read((char*)&byte2, 1) != 1) return false;
    if (byte1 != 0xFF || byte2 != 0xD8) return false;
    
    QMap<int, QByteArray> segments;
    
    // Scan for APP2 markers
    while (!file.atEnd()) {
        quint8 m1, m2;
        if (file.read((char*)&m1, 1) != 1 || file.read((char*)&m2, 1) != 1) break;
        if (m1 != 0xFF) continue;
        
        if (m2 == 0xE2) { // APP2
            quint16 length;
            file.read((char*)&length, 2);
            length = (quint8)((uchar*)&length)[0] << 8 | (quint8)((uchar*)&length)[1];
            
            if (length > 2) {
                QByteArray appData = file.read(length - 2);
                if (appData.startsWith("ICC_PROFILE\0")) {
                    // Format: "ICC_PROFILE\0" (12 bytes) + index (1 byte) + count (1 byte) + data
                    if (appData.size() > 14) {
                        int index = (quint8)appData[12];
                        // int count = (quint8)appData[13]; // Unused
                        segments[index] = appData.mid(14);
                    }
                }
            }
        } else if (m2 == 0xDA) { // SOS - Start of Scan, stop searching
            break;
        } else if (m2 >= 0xD0 && m2 <= 0xD9) {
            // No length
        } else {
            // Skip marker with length
            quint16 length;
            file.read((char*)&length, 2);
            length = (quint8)((uchar*)&length)[0] << 8 | (quint8)((uchar*)&length)[1];
            if (length > 2) file.read(length - 2);
        }
    }
    
    if (segments.isEmpty()) return false;
    
    // Concatenate segments in order
    iccData.clear();
    for (int i = 1; i <= segments.size(); ++i) {
        if (!segments.contains(i)) return false; // Missing segment
        iccData.append(segments[i]);
    }
    
    return !iccData.isEmpty();
}

#include <fitsio.h>
// Undefine TBYTE immediately to avoid conflict with Windows headers (typedef WCHAR TBYTE)
#ifdef TBYTE
#undef TBYTE
#endif

bool IccProfileExtractor::extractFromFits(const QString& filePath, QByteArray& iccData) {
    fitsfile *fptr;
    int status = 0;
    
    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status)) {
        return false;
    }
    
    int num_hdus;
    fits_get_num_hdus(fptr, &num_hdus, &status);
    
    for (int hdu = 1; hdu <= num_hdus; hdu++) {
        int hdutype;
        fits_movabs_hdu(fptr, hdu, &hdutype, &status);
        
        char extname[FLEN_VALUE];
        if (fits_read_key(fptr, TSTRING, "EXTNAME", extname, NULL, &status) == 0) {
            QString name = QString::fromUtf8(extname).trimmed().toUpper();
            if (name == "ICC_PROFILE" || name == "ICCPROFILE") {
                // Read binary data from this HDU
                long naxes[2];
                int naxis;
                fits_get_img_dim(fptr, &naxis, &status);
                if (naxis > 0) {
                    fits_get_img_size(fptr, 2, naxes, &status);
                    long nelements = naxes[0];
                    if (naxis > 1) nelements *= naxes[1];
                    
                    iccData.resize(nelements);
                    // Use literal 11 (TBYTE) to avoid macro conflicts with Windows headers
                    fits_read_img(fptr, 11, 1, nelements, NULL, iccData.data(), NULL, &status);
                    
                    if (status == 0) {
                        fits_close_file(fptr, &status);
                        return true;
                    }
                }
            }
        }
        status = 0; // Reset status for next HDU if one fails
    }
    
    fits_close_file(fptr, &status);
    return false;
}

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

bool IccProfileExtractor::extractFromRaw(const QString& filePath, QByteArray& iccData) {
#ifdef HAVE_LIBRAW
    LibRaw processor;
    
    // Only open the file, don't unpack yet
    if (processor.open_file(filePath.toUtf8().constData()) != LIBRAW_SUCCESS) {
        return false;
    }
    
    // Check if ICC profile exists
    if (processor.imgdata.color.profile_length > 0 && processor.imgdata.color.profile) {
        iccData = QByteArray((const char*)processor.imgdata.color.profile, processor.imgdata.color.profile_length);
        return true;
    }
    
    return false;
#else
    Q_UNUSED(filePath)
    Q_UNUSED(iccData)
    return false;
#endif
}

bool IccProfileExtractor::extractFromXisf(const QString& filePath, QByteArray& iccData) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    // Check signature
    QByteArray sig = file.read(8);
    if (sig != "XISF0100") return false;

    // Read header length
    uchar lenBytes[4];
    if (file.read((char*)lenBytes, 4) != 4) return false;
    quint32 headerLen = qFromLittleEndian<quint32>(lenBytes);

    // Skip reserved
    file.read(4);

    if (headerLen == 0 || headerLen > 20 * 1024 * 1024) return false; // 20MB safety

    QByteArray xmlBytes = file.read(headerLen);
    QXmlStreamReader xml(xmlBytes);

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == "ICCProfile") {
            // XISF stores ICC profiles inline as base64 or in an attachment.
            // Primarily support base64 for now as it's the most common.
            QString profileData = xml.readElementText();
            if (!profileData.isEmpty()) {
                iccData = QByteArray::fromBase64(profileData.toUtf8());
                return !iccData.isEmpty();
            }
        }
    }
    return false;
}
