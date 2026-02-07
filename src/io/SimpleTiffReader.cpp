#include "SimpleTiffReader.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QCoreApplication>

// Structs for internal use
struct TiffTag {
    quint16 tag;
    quint16 type;
    quint32 count;
    quint32 valueOrOffset; // If count*size > 4, this is offset
};

// Tag Constants
const quint16 TAG_ImageWidth = 256;
const quint16 TAG_ImageLength = 257;
const quint16 TAG_BitsPerSample = 258;
const quint16 TAG_Compression = 259;
const quint16 TAG_PhotometricInterpretation = 262;
const quint16 TAG_StripOffsets = 273;
const quint16 TAG_SamplesPerPixel = 277;
const quint16 TAG_RowsPerStrip = 278;
const quint16 TAG_StripByteCounts = 279;
const quint16 TAG_SampleFormat = 339; // 3 = Float
const quint16 TAG_PlanarConfiguration = 284; // 1 = Chunky, 2 = Planar
const quint16 TAG_TileWidth = 322;
const quint16 TAG_TileLength = 323;
const quint16 TAG_TileOffsets = 324;
const quint16 TAG_TileByteCounts = 325;

bool SimpleTiffReader::readInfo(const QString& path, int& width, int& height, int& channels, int& bitsPerSample, QString* errorMsg) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "File open failed: %1").arg(file.errorString());
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint16 byteOrder;
    stream >> byteOrder;
    if (byteOrder == 0x4949) stream.setByteOrder(QDataStream::LittleEndian);
    else if (byteOrder == 0x4D4D) stream.setByteOrder(QDataStream::BigEndian);
    else return false;

    quint16 version;
    stream >> version;
    if (version != 42) return false;

    quint32 ifdOffset;
    stream >> ifdOffset;
    if (!file.seek(ifdOffset)) return false;

    quint16 numEntries;
    stream >> numEntries;

    quint32 t_width = 0, t_height = 0;
    quint16 t_bits = 0, t_channels = 1;

    for (int i = 0; i < numEntries; ++i) {
        quint16 tag, type;
        quint32 count, val;
        stream >> tag >> type >> count >> val;

        if (tag == TAG_ImageWidth) t_width = val;
        else if (tag == TAG_ImageLength) t_height = val;
        else if (tag == TAG_SamplesPerPixel) t_channels = val;
        else if (tag == TAG_BitsPerSample) {
            if (count == 1) t_bits = val;
            else if (count == 2) t_bits = val & 0xFFFF;
            else {
                qint64 savePos = file.pos();
                if (file.seek(val)) {
                    quint16 v; stream >> v; t_bits = v;
                }
                file.seek(savePos);
            }
        }
    }

    width = t_width;
    height = t_height;
    channels = t_channels;
    bitsPerSample = t_bits;

    return (width > 0 && height > 0);
}

bool SimpleTiffReader::readFloat32(const QString& path, int& width, int& height, int& channels, std::vector<float>& data, QString* errorMsg, QString* debugInfo) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "File open failed: %1").arg(file.errorString());
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian); // Default attempt

    // 1. Header (8 bytes)
    quint16 byteOrder;
    stream >> byteOrder;

    if (byteOrder == 0x4949) {
        stream.setByteOrder(QDataStream::LittleEndian);
    } else if (byteOrder == 0x4D4D) {
        stream.setByteOrder(QDataStream::BigEndian);
    } else {
        if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "Invalid TIFF byte order marker.");
        return false;
    }

    quint16 version;
    stream >> version;
    if (version != 42) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "Invalid TIFF version (not 42).");
        return false;
    }

    quint32 ifdOffset;
    stream >> ifdOffset;

    // 2. Read IFD
    if (!file.seek(ifdOffset)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "Could not seek to IFD.");
        return false;
    }

    quint16 numEntries;
    stream >> numEntries;

    // Parsed properties
    quint32 t_width = 0;
    quint32 t_height = 0;
    quint16 t_bitsPerSample = 0;
    quint16 t_samplesPerPixel = 1;
    quint16 t_compression = 1;
    quint16 t_sampleFormat = 1; // Default uint
    quint16 t_planarConfig = 1; // Default Chunky
    quint32 t_tileWidth = 0;
    quint32 t_tileLength = 0;
    std::vector<quint32> stripOffsets;
    std::vector<quint32> stripByteCounts;
    std::vector<quint32> tileOffsets;
    std::vector<quint32> tileByteCounts;

    for (int i = 0; i < numEntries; ++i) {
        quint16 tag, type;
        quint32 count, val;
        stream >> tag >> type >> count >> val;

        if (tag == TAG_ImageWidth) t_width = val;
        else if (tag == TAG_ImageLength) t_height = val;
        else if (tag == TAG_SamplesPerPixel) t_samplesPerPixel = val;
        else if (tag == TAG_Compression) t_compression = val; // 1 = None
        else if (tag == TAG_SampleFormat) t_sampleFormat = val; // 3 = Float
        else if (tag == TAG_PlanarConfiguration) t_planarConfig = val;
        else if (tag == TAG_BitsPerSample) {
            if (count == 1) {
                t_bitsPerSample = val;
            } else if (count == 2) {
                t_bitsPerSample = val & 0xFFFF; 
            } else {
                qint64 savePos = file.pos();
                if (file.seek(val)) {
                    quint16 v; 
                    stream >> v;
                    t_bitsPerSample = v;
                }
                file.seek(savePos);
            }
        }
        else if (tag == TAG_StripOffsets) {
             if (count == 1) stripOffsets.push_back(val);
             else {
                 // Value is offset to array of offsets
                 qint64 savePos = file.pos();
                 file.seek(val);
                 for(quint32 k=0; k<count; ++k) {
                     quint32 off; stream >> off;
                     stripOffsets.push_back(off);
                 }
                 file.seek(savePos);
             }
        }
        else if (tag == TAG_StripByteCounts) {
             if (count == 1) stripByteCounts.push_back(val);
             else {
                 qint64 savePos = file.pos();
                 file.seek(val);
                 for(quint32 k=0; k<count; ++k) {
                     quint32 cnt; stream >> cnt;
                     stripByteCounts.push_back(cnt);
                 }
                 file.seek(savePos);
             }
        }
        else if (tag == TAG_TileWidth) t_tileWidth = val;
        else if (tag == TAG_TileLength) t_tileLength = val;
        else if (tag == TAG_TileOffsets) {
             if (count == 1) tileOffsets.push_back(val);
             else {
                 qint64 savePos = file.pos();
                 file.seek(val);
                 for(quint32 k=0; k<count; ++k) { quint32 off; stream >> off; tileOffsets.push_back(off); }
                 file.seek(savePos);
             }
        }
        else if (tag == TAG_TileByteCounts) {
             if (count == 1) tileByteCounts.push_back(val);
             else {
                 qint64 savePos = file.pos();
                 file.seek(val);
                 for(quint32 k=0; k<count; ++k) { quint32 cnt; stream >> cnt; tileByteCounts.push_back(cnt); }
                 file.seek(savePos);
             }
        }
    }

    // Validation
    if (t_compression != 1) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "Compressed TIFFs not supported (only Uncompressed).");
        return false;
    }
    // Primarily supports 32-bit float; warning if otherwise
    if (t_sampleFormat != 3) {
        // If it's not float, warned.
        // It might be 1 (uint), if 16-bit or 8-bit.
    }
    
    // 3. Read Data (Raw)
    width = t_width;
    height = t_height;
    channels = t_samplesPerPixel;
    
    // We'll read into a raw buffer first
    std::vector<float> rawData;
    rawData.resize(width * height * channels, 0.0f);


    
    // Debug Stats
    float dMin = 1e9f, dMax = -1e9f;
    int nanCount = 0;
    // double sum = 0.0; // Optimized out for speed if large

    bool isTiled = (!tileOffsets.empty());
    
    if (isTiled) {
        if (tileOffsets.size() != tileByteCounts.size()) { if(errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "Tile offset/count mismatch."); return false; }
        
        // Rows / Cols of TILES
        int tilesAcross = (width + t_tileWidth - 1) / t_tileWidth;
        int tilesDown = (height + t_tileLength - 1) / t_tileLength;
        int tilesPerPlane = tilesAcross * tilesDown;
        
        // Loop Tiles
        for (size_t t = 0; t < tileOffsets.size(); ++t) {
            if (!file.seek(tileOffsets[t])) continue;
            QByteArray bytes = file.read(tileByteCounts[t]);
            QDataStream ds(bytes);
            ds.setByteOrder(stream.byteOrder());
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
            
            // Tile Coordinates
            // If Planar, Tile Index includes planes.
            // If Chunky, 1 plane of tiles.
            int planeIdx = 0;
            int tileInPlane = t;
            
            if (t_planarConfig == 2) {
                planeIdx = t / tilesPerPlane;
                tileInPlane = t % tilesPerPlane;
            }
            
            int ty = (tileInPlane / tilesAcross) * t_tileLength;
            int tx = (tileInPlane % tilesAcross) * t_tileWidth;
            
            // Read Tile Data
            // Tile is W x H x (Samples if Chunky, 1 if Planar)
            int samplesInTile = (t_planarConfig == 1) ? channels : 1;
            int bytesPerSample = (t_bitsPerSample > 0) ? (t_bitsPerSample/8) : 4;
            if (t_bitsPerSample == 0 && t_sampleFormat == 3) bytesPerSample=4;
            
            // Iterate Tile Rows
            // Total samples in tile buffer
            int totalTileSamples = bytes.size() / bytesPerSample;
            int readIdx = 0;

            for (int r = 0; r < (int)t_tileLength; ++r) {
                int imgY = ty + r;
                if (imgY >= height) break;
                
                for (int c = 0; c < (int)t_tileWidth; ++c) {
                    int imgX = tx + c;
                    if (imgX >= width) {
                        // Skip samples for rest of row in tile
                         // But we must advance readIdx by samplesInTile
                        readIdx += samplesInTile;
                        continue; 
                    }
                    
                    for (int sIdx = 0; sIdx < samplesInTile; ++sIdx) {
                        if (readIdx >= totalTileSamples) break;
                        
                        float f = 0.0f;
                        // Read float
                        if (t_sampleFormat == 3) {
                             if (bytesPerSample==4) ds >> f;
                             else if (bytesPerSample==8) { double d; ds >> d; f=(float)d; }
                        } else if (t_sampleFormat == 1) {
                             if (bytesPerSample == 1) { quint8 v; ds >> v; f = v / 255.0f; }
                             else if (bytesPerSample == 2) { quint16 v; ds >> v; f = v / 65535.0f; }
                             else if (bytesPerSample == 4) { quint32 v; ds >> v; f = v / 4294967295.0f; } 
                        }
                        readIdx++;

                        // Write to rawData
                        // If Planar (Config 2): rawData is Plane0, Plane1...
                        // If Chunky (Config 1): rawData is RGB, RGB...
                        
                        size_t destIdx = 0;
                        if (t_planarConfig == 2) {
                            // Plane Offset + Y*W + X
                            // samplesInTile is 1. sIdx is 0. planeIdx is current Channel.
                            destIdx = (size_t)planeIdx * (width * height) + (imgY * width + imgX);
                        } else {
                            // Chunky: (Y*W + X)*Channels + sIdx
                            destIdx = ((size_t)imgY * width + imgX) * channels + sIdx;
                        }
                        
                        if (destIdx < rawData.size()) rawData[destIdx] = f;
                        
                        if (std::isnan(f)) { nanCount++; }
                        else {
                            if (f < dMin) dMin = f;
                            if (f > dMax) dMax = f;
                        }
                    }
                }
            }
        }
        
    } else {
        // STRIPS Logic (Existing but refined for Planar)
        if (stripOffsets.size() != stripByteCounts.size()) {
            if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffReader", "Strip offset/count mismatch.");
            return false;
        }
        
        // Similar simplified loop but purely sequential IF Chunky.
        // If Planar, multiple strips per plane.
        // TIFF 6.0: "If SamplesPerPixel > 1, PlanarConfig=2, then each component is stored in a separate strip or strips..."
        // Format: All strips for Plane 0, then All strips for Plane 1.
        
        // Sequential read into rawData functions correctly for both Chunky and Planar Strips
        // Planar re-ordering is handled in post-processing step
        
        size_t currentOutIdx = 0;
        for (size_t s = 0; s < stripOffsets.size(); ++s) {
            if (!file.seek(stripOffsets[s])) continue;
            QByteArray bytes = file.read(stripByteCounts[s]);
            QDataStream ds(bytes);
            ds.setByteOrder(stream.byteOrder());
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
            
            int bytesPerSample = (t_bitsPerSample > 0) ? (t_bitsPerSample / 8) : 4; 
            if (t_bitsPerSample == 0 && t_sampleFormat == 3) bytesPerSample = 4;
            int numSamples = bytes.size() / bytesPerSample;
            
            for (int k = 0; k < numSamples; ++k) {
                if (currentOutIdx >= rawData.size()) break;
                
                 float f = 0.0f;
                 if (t_sampleFormat == 3) {
                     if (bytesPerSample==4) ds >> f;
                     else if (bytesPerSample==8) { double d; ds >> d; f=(float)d; }
                 } else if (t_sampleFormat == 1) {
                     if (bytesPerSample == 1) { quint8 v; ds >> v; f = v / 255.0f; }
                     else if (bytesPerSample == 2) { quint16 v; ds >> v; f = v / 65535.0f; }
                     else if (bytesPerSample == 4) { quint32 v; ds >> v; f = v / 4294967295.0f; } 
                 }
                 
                 if (std::isnan(f)) { nanCount++; f=0.0f; }
                 else {
                     if (f < dMin) dMin = f;
                     if (f > dMax) dMax = f;
                 }
                 rawData[currentOutIdx++] = f;
            }
        }
    }
    
    // Auto-Normalization
    qDebug() << "TIFF Stats:" << path << "Min:" << dMin << "Max:" << dMax << "NaNs:" << nanCount;
    if (debugInfo) {
        *debugInfo = QString("TIFF Stats: Min=%1 Max=%2 NaNs=%3").arg(dMin).arg(dMax).arg(nanCount);
    }
    
    if (dMax > 1.0f) {
        float scale = 1.0f / dMax;
        qDebug() << "Auto-Normalizing with scale:" << scale;
        if (debugInfo) *debugInfo += QString(" [Normalized 1/%1]").arg(dMax);
        for(float& v : rawData) v *= scale;
    }

    // Handle Planar -> Interleaved
    if (t_planarConfig == 2 && channels > 1) {
        data.resize(width * height * channels);
        int planeSize = width * height;
        for (int i = 0; i < planeSize; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                int srcIdx = ch * planeSize + i;
                int dstIdx = i * channels + ch;
                if ((int)srcIdx < (int)rawData.size()) {
                    data[dstIdx] = rawData[srcIdx];
                }
            }
        }
    } else {
        data = std::move(rawData);
    }

    return true;
}
