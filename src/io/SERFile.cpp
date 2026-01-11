#include "SERFile.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>

SERFile::SERFile() {
    memset(&m_header, 0, sizeof(Header));
}

SERFile::~SERFile() {
    close();
}

bool SERFile::open(const std::string& filename) {
    close();
    
    m_file.open(filename, std::ios::binary);
    if (!m_file.is_open()) return false;
    
    m_filename = filename;
    
    if (!readHeader()) {
        close();
        return false;
    }
    
    // Timestamps block follows header and frames
    // We'll read them on demand or pre-load if needed.
    // Spec: Timestamps are AFTER image data.
    
    return true;
}

void SERFile::close() {
    if (m_file.is_open()) m_file.close();
    m_timestamps.clear();
}

int SERFile::width() const { return m_header.imageWidth; }
int SERFile::height() const { return m_header.imageHeight; }
int SERFile::frameCount() const { return m_header.frameCount; }
int SERFile::bitDepth() const { return m_header.pixelDepth; }
SERFile::ColorID SERFile::colorID() const { return static_cast<ColorID>(m_header.colorID); }

bool SERFile::isBayer() const {
    int id = m_header.colorID;
    return (id >= 8 && id <= 19);
}

bool SERFile::readHeader() {
    m_file.seekg(0);
    m_file.read(reinterpret_cast<char*>(&m_header), sizeof(Header)); // 178 bytes
    
    // Check ID
    // Note: sizeof(Header) might be padded by compiler. 
    // Manual read is safer but struct is mostly 4-byte packed.
    // Let's verify string.
    std::string id(m_header.fileID, 14);
    if (id != "LUCAM-RECORDER") return false;
    
    return true;
}

bool SERFile::readFrame(int index, ImageBuffer& buffer) {
    if (index < 0 || index >= m_header.frameCount) return false;
    
    int w = m_header.imageWidth;
    int h = m_header.imageHeight;
    int bpp = m_header.pixelDepth;
    int channels = (m_header.colorID == RGB || m_header.colorID == BGR) ? 3 : 1;
    
    size_t frameSize = static_cast<size_t>(w) * h * channels * ((bpp > 8) ? 2 : 1);
    uint64_t offset = 178 + static_cast<uint64_t>(index) * frameSize;
    
    m_file.seekg(offset);
    if (m_file.fail()) return false;
    
    std::vector<uint8_t> rawData(frameSize);
    m_file.read(reinterpret_cast<char*>(rawData.data()), frameSize);
    if (m_file.gcount() != static_cast<std::streamsize>(frameSize)) return false;
    
    // Convert to ImageBuffer (float)
    std::vector<float> floatData(static_cast<size_t>(w) * h * channels);
    
    bool is16Bit = (bpp > 8);
    float norm = is16Bit ? 65535.0f : 255.0f;
    
    if (is16Bit) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(rawData.data());
        #pragma omp parallel for
        for (int i = 0; i < w * h * channels; ++i) {
            floatData[i] = src[i] / norm;
        }
    } else {
        const uint8_t* src = rawData.data();
        #pragma omp parallel for
        for (int i = 0; i < w * h * channels; ++i) {
            floatData[i] = src[i] / norm;
        }
    }
    
    // Handle Color ID
    if (m_header.colorID == RGB) {
        buffer.setData(w, h, 3, floatData); // Already RGB
    } else if (m_header.colorID == BGR) {
        // Swap B and R
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            float b = floatData[i * 3 + 0];
            float g = floatData[i * 3 + 1];
            float r = floatData[i * 3 + 2];
            floatData[i * 3 + 0] = r;
            floatData[i * 3 + 1] = g;
            floatData[i * 3 + 2] = b;
        }
        buffer.setData(w, h, 3, floatData);
    } else {
        buffer.setData(w, h, 1, floatData); // Mono or Bayer (handled as mono)
    }
    
    // Metadata
    buffer.metadata().bitDepth = QString::number(bpp);
    // buffer.metadata().dateObs = ... (Need to read timestamp block)
    
    return true;
}

bool SERFile::write(const std::string& filename, const std::vector<ImageBuffer>& frames, ColorID colorId, int bitDepth) {
    if (frames.empty()) return false;
    
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) return false;
    
    int w = frames[0].width();
    int h = frames[0].height();
    int count = static_cast<int>(frames.size());
    
    // Write Header
    Header head;
    memset(&head, 0, sizeof(Header));
    memcpy(head.fileID, "LUCAM-RECORDER", 14);
    head.colorID = colorId;
    head.littleEndian = 1; // Always LE
    head.imageWidth = w;
    head.imageHeight = h;
    head.pixelDepth = bitDepth;
    head.frameCount = count;
    
    // Get time from first frame if possible, else now
    QDateTime now = QDateTime::currentDateTime();
    // Convert to Win32 FILETIME (approx)
    uint64_t ft = (now.toMSecsSinceEpoch() + 11644473600000) * 10000;
    head.dateTime = ft;
    head.dateTimeUTC = ft;
    
    out.write(reinterpret_cast<char*>(&head), sizeof(Header));
    
    // Write Data
    bool is16Bit = (bitDepth > 8);
    float maxVal = is16Bit ? 65535.0f : 255.0f;
    
    for (const auto& img : frames) {
        int channels = img.channels();
        size_t pixels = static_cast<size_t>(w) * h * channels;
        const float* src = img.data().data();
        
        if (is16Bit) {
            std::vector<uint16_t> buf(pixels);
            for(size_t i=0; i<pixels; ++i) buf[i] = static_cast<uint16_t>(std::clamp(src[i] * maxVal, 0.0f, maxVal));
            out.write(reinterpret_cast<char*>(buf.data()), pixels * 2);
        } else {
            std::vector<uint8_t> buf(pixels);
            for(size_t i=0; i<pixels; ++i) buf[i] = static_cast<uint8_t>(std::clamp(src[i] * maxVal, 0.0f, maxVal));
            out.write(reinterpret_cast<char*>(buf.data()), pixels);
        }
    }
    
    return true;
}
