#include "SimpleTiffWriter.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QCoreApplication>

// TIFF Tags
#define TAG_ImageWidth 256
#define TAG_ImageLength 257 // Height
#define TAG_BitsPerSample 258
#define TAG_Compression 259
#define TAG_PhotometricInterpretation 262
#define TAG_StripOffsets 273
#define TAG_SamplesPerPixel 277
#define TAG_RowsPerStrip 278
#define TAG_StripByteCounts 279
#define TAG_XResolution 282
#define TAG_YResolution 283
#define TAG_PlanarConfiguration 284
#define TAG_ResolutionUnit 296
#define TAG_SampleFormat 339

struct TiffEntry {
    uint16_t tag;
    uint16_t type; // 1=BYTE, 3=SHORT, 4=LONG, 5=RATIONAL
    uint32_t count;
    uint32_t valueOrOffset;
};

// Helper to write entry
void writeEntry(QDataStream& out, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
    out << tag << type << count << value;
}

bool SimpleTiffWriter::write(const QString& filename, int width, int height, int channels, Format fmt, const std::vector<float>& data, QString* errorMsg) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("SimpleTiffWriter", "Could not open file for writing.");
        return false;
    }
    
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian); // "II"
    
    // Header
    out.writeRawData("II", 2);
    out << (uint16_t)42;
    uint32_t ifdOffset = 8; // Header is 8 bytes
    out << ifdOffset;
    
    // Determine props
    int bitsPerSample = 8;
    int sampleFormat = 1; // Uint
    if (fmt == Format_uint16) bitsPerSample = 16;
    if (fmt == Format_uint32) { bitsPerSample = 32; sampleFormat = 1; }
    if (fmt == Format_float32) { bitsPerSample = 32; sampleFormat = 3; }
    
    int photometric = (channels == 1) ? 1 : 2; // BlackIsZero or RGB
    
    // Prepare Data Buffer
    QByteArray dataBuf;
    QDataStream dOut(&dataBuf, QIODevice::WriteOnly);
    dOut.setByteOrder(QDataStream::LittleEndian);
    dOut.setFloatingPointPrecision(QDataStream::SinglePrecision);
    
    size_t totalPixels = (size_t)width * height;
    
    for (size_t i = 0; i < totalPixels; ++i) {
        for (int c = 0; c < channels; ++c) {
            float val = data[i * channels + c];
            if (fmt == Format_uint8) {
                uint8_t v = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, val * 255.0f)));
                dOut << v;
            } else if (fmt == Format_uint16) {
                uint16_t v = static_cast<uint16_t>(std::max(0.0f, std::min(65535.0f, val * 65535.0f)));
                dOut << v;
            } else if (fmt == Format_uint32) {
                uint32_t v = static_cast<uint32_t>(std::max(0.0f, val * 4294967295.0f)); // Range check?
                dOut << v;
            } else if (fmt == Format_float32) {
                dOut << val;
            }
        }
    }
    
    // Calculate Offsets
    // Header: 8 bytes
    // IFD: 2 + numEntries*12 + 4
    // Values that don't fit in 4 bytes (e.g. bitsPerSample array, XRed/YRes, Data) go after IFD.
    
    int numEntries = 12;
    uint32_t ifdSize = 2 + numEntries * 12 + 4;
    uint32_t extraOffset = ifdOffset + ifdSize;
    
    // BitsPerSample Array (if channels > 1)
    uint32_t bitsOffset = 0;
    if (channels > 1) {
        bitsOffset = extraOffset;
        extraOffset += channels * 2; // SHORTs
    }
    
    // Resolution Rationals (2 * 2 * 4 bytes = 16)
    uint32_t xResOffset = extraOffset;
    extraOffset += 8;
    uint32_t yResOffset = extraOffset;
    extraOffset += 8;
    
    // Strip Data
    uint32_t stripOffset = extraOffset;
    uint32_t stripByteCount = dataBuf.size();
    
    // Write IFD
    out << (uint16_t)numEntries;
    
    // 1. Width
    writeEntry(out, TAG_ImageWidth, 4, 1, width); // LONG
    // 2. Height
    writeEntry(out, TAG_ImageLength, 4, 1, height);
    // 3. BitsPerSample
    if (channels == 1) {
        writeEntry(out, TAG_BitsPerSample, 3, 1, bitsPerSample);
    } else {
        writeEntry(out, TAG_BitsPerSample, 3, channels, bitsOffset);
    }
    // 4. Compression (1)
    writeEntry(out, TAG_Compression, 3, 1, 1);
    // 5. Photometric
    writeEntry(out, TAG_PhotometricInterpretation, 3, 1, photometric);
    // 6. StripOffsets
    writeEntry(out, TAG_StripOffsets, 4, 1, stripOffset);
    // 7. SamplesPerPixel
    writeEntry(out, TAG_SamplesPerPixel, 3, 1, channels);
    // 8. RowsPerStrip
    writeEntry(out, TAG_RowsPerStrip, 4, 1, height);
    // 9. StripByteCounts
    writeEntry(out, TAG_StripByteCounts, 4, 1, stripByteCount);
    // 10. XResolution
    writeEntry(out, TAG_XResolution, 5, 1, xResOffset);
    // 11. YResolution
    writeEntry(out, TAG_YResolution, 5, 1, yResOffset);
    // 12. SampleFormat
    writeEntry(out, TAG_SampleFormat, 3, 1, sampleFormat); // Use 1 for uint instead of count if same
    // Wait, SampleFormat usually 1 value if all same.
    
    out << (uint32_t)0; // Next IFD
    
    // Write Extra Data
    
    // BitsPerSample Array
    if (channels > 1) {
        for (int i=0; i<channels; ++i) out << (uint16_t)bitsPerSample;
    }
    
    // XRes (Rational: Num, Denom) - 72 dpi
    out << (uint32_t)72 << (uint32_t)1;
    // YRes
    out << (uint32_t)72 << (uint32_t)1;
    
    // Strip Data
    out.writeRawData(dataBuf.data(), dataBuf.size());
    
    file.close();
    return true;
}
