#ifndef SERFILE_H
#define SERFILE_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <memory>
#include "../ImageBuffer.h"

class SERFile {
public:
    enum ColorID {
        MONO = 0,
        BAYER_RGGB = 8,
        BAYER_GRBG = 9,
        BAYER_GBRG = 10,
        BAYER_BGGR = 11,
        BAYER_CYYM = 16,
        BAYER_YCMY = 17,
        BAYER_YMCY = 18,
        BAYER_MYYC = 19,
        RGB = 100,
        BGR = 101
    };

    SERFile();
    ~SERFile();

    bool open(const std::string& filename);
    void close();
    
    // Header properties
    int width() const;
    int height() const;
    int frameCount() const;
    int bitDepth() const;
    ColorID colorID() const;
    bool isBayer() const;
    
    // Read frame
    bool readFrame(int index, ImageBuffer& buffer);
    
    // Write support (basic)
    static bool write(const std::string& filename, const std::vector<ImageBuffer>& frames, ColorID colorId, int bitDepth);

private:
    struct Header {
        char fileID[14];           // "LUCAM-RECORDER"
        int32_t luID;             // 0
        int32_t colorID;
        int32_t littleEndian;
        int32_t imageWidth;
        int32_t imageHeight;
        int32_t pixelDepth;
        int32_t frameCount;
        int32_t observer[2];      // Unused
        int32_t instrument[2];    // Unused
        int32_t telescope[2];     // Unused
        int64_t dateTime;         // Win32 FILETIME
        int64_t dateTimeUTC;      // Win32 FILETIME UTC
    };
    
    Header m_header;
    std::ifstream m_file;
    std::string m_filename;
    std::vector<int64_t> m_timestamps;
    
    bool readHeader();
    bool readTimestamps();
};

#endif // SERFILE_H
