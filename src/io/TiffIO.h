
#ifndef TIFF_IO_H
#define TIFF_IO_H

#include "../ImageBuffer.h"
#include "SimpleTiffReader.h"
#include "SimpleTiffWriter.h"
#include <QString>

namespace Stacking {

/**
 * @brief Unified TIFF I/O operations
 */
class TiffIO {
public:
    /**
     * @brief Read a TIFF file into an ImageBuffer
     * @param path File path
     * @param buffer Output buffer
     * @return true if successful
     */
    static bool read(const QString& path, ImageBuffer& buffer) {
        int w, h, c;
        std::vector<float> data;
        if (!SimpleTiffReader::readFloat32(path, w, h, c, data)) {
            return false;
        }
        buffer.setData(w, h, c, data);
        return true;
    }
    
    /**
     * @brief Read basic info from a TIFF file without loading pixels
     * @param path File path
     * @param width Output width
     * @param height Output height
     * @param channels Output number of channels
     * @param bits Output bits per channel
     * @return true if successful
     */
    static bool readInfo(const QString& path, int& width, int& height, 
                        int& channels, int& bits) {
        return SimpleTiffReader::readInfo(path, width, height, channels, bits);
    }
    
    /**
     * @brief Write an ImageBuffer to a TIFF file
     * @param path File path
     * @param buffer Image to write
     * @param bitDepth Output bit depth (8, 16, or 32)
     * @return true if successful
     */
    static bool write(const QString& path, const ImageBuffer& buffer, int bitDepth = 16) {
        SimpleTiffWriter::Format fmt = SimpleTiffWriter::Format_uint16;
        if (bitDepth == 8) fmt = SimpleTiffWriter::Format_uint8;
        else if (bitDepth == 32) fmt = SimpleTiffWriter::Format_float32;
        
        return SimpleTiffWriter::write(path, buffer.width(), buffer.height(), 
                                      buffer.channels(), fmt, buffer.data());
    }
};

} // namespace Stacking

#endif // TIFF_IO_H
