#ifndef COMPRESSIONUTILS_H
#define COMPRESSIONUTILS_H

#include <QByteArray>
#include <QString>
#include <cstdint>

/**
 * @brief Compression utilities for XISF file format support.
 * 
 * Supports the following codecs as per XISF 1.0 specification:
 * - zlib: General purpose compression (level 1-9, default 6)
 * - lz4: Fast compression (no level setting)
 * - lz4hc: High compression LZ4 (level 1-12, default 9)
 * - zstd: Zstandard compression (level 1-22, default 3)
 * 
 * Also supports byte shuffling for improved compression of typed data.
 */
class CompressionUtils {
public:
    /**
     * @brief Supported compression codecs
     */
    enum Codec {
        Codec_None = 0,
        Codec_Zlib = 1,
        Codec_LZ4 = 2,
        Codec_LZ4HC = 3,
        Codec_Zstd = 4
    };
    
    /**
     * @brief Default compression levels for each codec
     */
    static int defaultLevel(Codec codec);
    
    /**
     * @brief Parse XISF compression attribute string
     * 
     * Format: "codec:uncompressed_size" or "codec+sh:uncompressed_size:item_size"
     * Examples: "zlib:123456", "lz4hc+sh:123456:4"
     * 
     * @param compressionStr The compression attribute value
     * @param codec Output codec type
     * @param uncompressedSize Output uncompressed data size
     * @param shuffleItemSize Output shuffle item size (0 if no shuffling)
     * @return true if parsing succeeded
     */
    static bool parseCompressionAttr(const QString& compressionStr,
                                     Codec& codec,
                                     qint64& uncompressedSize,
                                     int& shuffleItemSize);
    
    /**
     * @brief Build XISF compression attribute string
     * 
     * @param codec Compression codec
     * @param uncompressedSize Original data size
     * @param shuffleItemSize Shuffle item size (0 for no shuffling)
     * @return Formatted compression attribute string
     */
    static QString buildCompressionAttr(Codec codec, qint64 uncompressedSize, int shuffleItemSize = 0);
    
    /**
     * @brief Decompress data using the specified codec
     * 
     * @param data Compressed data
     * @param codec Compression codec used
     * @param uncompressedSize Expected uncompressed size
     * @param errorMsg Optional error message output
     * @return Decompressed data, or empty on error
     */
    static QByteArray decompress(const QByteArray& data, Codec codec,
                                 qint64 uncompressedSize, QString* errorMsg = nullptr);
    
    /**
     * @brief Compress data using the specified codec
     * 
     * @param data Raw data to compress
     * @param codec Compression codec to use
     * @param level Compression level (-1 for default)
     * @param errorMsg Optional error message output
     * @return Compressed data, or empty on error
     */
    static QByteArray compress(const QByteArray& data, Codec codec,
                               int level = -1, QString* errorMsg = nullptr);
    
    /**
     * @brief Apply byte shuffling for better compression
     * 
     * Reorders bytes so that the first bytes of each item are grouped together,
     * then the second bytes, etc. This improves compression for typed data.
     * 
     * @param data Input data
     * @param itemSize Size of each data item (e.g., 4 for float32)
     * @return Shuffled data
     */
    static QByteArray shuffle(const QByteArray& data, int itemSize);
    
    /**
     * @brief Reverse byte shuffling
     * 
     * @param data Shuffled data
     * @param itemSize Size of each data item
     * @return Unshuffled data
     */
    static QByteArray unshuffle(const QByteArray& data, int itemSize);
    
    /**
     * @brief Get codec name as string
     */
    static QString codecName(Codec codec);
    
    /**
     * @brief Parse codec name from string
     */
    static Codec parseCodecName(const QString& name);

private:
    // Individual codec implementations
    static QByteArray decompressZlib(const QByteArray& data, qint64 uncompressedSize, QString* errorMsg);
    static QByteArray decompressLZ4(const QByteArray& data, qint64 uncompressedSize, QString* errorMsg);
    static QByteArray decompressZstd(const QByteArray& data, qint64 uncompressedSize, QString* errorMsg);
    
    static QByteArray compressZlib(const QByteArray& data, int level, QString* errorMsg);
    static QByteArray compressLZ4(const QByteArray& data, QString* errorMsg);
    static QByteArray compressLZ4HC(const QByteArray& data, int level, QString* errorMsg);
    static QByteArray compressZstd(const QByteArray& data, int level, QString* errorMsg);
};

#endif // COMPRESSIONUTILS_H
