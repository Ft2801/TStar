#include "CompressionUtils.h"
#include <QDebug>
#include <QCoreApplication>
#include <cstring>

// Compression library includes
#include <zlib.h>

// LZ4 - conditionally include if available
#ifdef HAVE_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

// Zstandard - conditionally include if available
#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

int CompressionUtils::defaultLevel(Codec codec) {
    switch (codec) {
        case Codec_Zlib: return 6;   // zlib default
        case Codec_LZ4: return 0;    // lz4 has no level
        case Codec_LZ4HC: return 9;  // lz4hc recommended
        case Codec_Zstd: return 3;   // zstd default
        default: return 0;
    }
}

QString CompressionUtils::codecName(Codec codec) {
    switch (codec) {
        case Codec_Zlib: return "zlib";
        case Codec_LZ4: return "lz4";
        case Codec_LZ4HC: return "lz4hc";
        case Codec_Zstd: return "zstd";
        default: return "";
    }
}

CompressionUtils::Codec CompressionUtils::parseCodecName(const QString& name) {
    QString lower = name.toLower();
    // Handle shuffle suffix
    if (lower.endsWith("+sh")) {
        lower = lower.left(lower.length() - 3);
    }
    
    if (lower == "zlib") return Codec_Zlib;
    if (lower == "lz4") return Codec_LZ4;
    if (lower == "lz4hc") return Codec_LZ4HC;
    if (lower == "zstd") return Codec_Zstd;
    return Codec_None;
}

bool CompressionUtils::parseCompressionAttr(const QString& compressionStr,
                                            Codec& codec,
                                            qint64& uncompressedSize,
                                            int& shuffleItemSize) {
    // Format: "codec:uncompressed_size" or "codec+sh:uncompressed_size:item_size"
    // Examples: "zlib:123456", "lz4hc+sh:123456:4"
    
    if (compressionStr.isEmpty()) {
        codec = Codec_None;
        uncompressedSize = 0;
        shuffleItemSize = 0;
        return true;
    }
    
    QStringList parts = compressionStr.split(':');
    if (parts.size() < 2) {
        return false;
    }
    
    QString codecStr = parts[0];
    bool hasShuffle = codecStr.endsWith("+sh", Qt::CaseInsensitive);
    
    codec = parseCodecName(codecStr);
    if (codec == Codec_None && !codecStr.isEmpty()) {
        qWarning() << "Unknown compression codec:" << codecStr;
        return false;
    }
    
    bool ok;
    uncompressedSize = parts[1].toLongLong(&ok);
    if (!ok) return false;
    
    shuffleItemSize = 0;
    if (hasShuffle && parts.size() >= 3) {
        shuffleItemSize = parts[2].toInt(&ok);
        if (!ok) shuffleItemSize = 0;
    }
    
    return true;
}

QString CompressionUtils::buildCompressionAttr(Codec codec, qint64 uncompressedSize, int shuffleItemSize) {
    if (codec == Codec_None) return QString();
    
    QString result = codecName(codec);
    if (shuffleItemSize > 0) {
        result += "+sh";
    }
    result += ":" + QString::number(uncompressedSize);
    if (shuffleItemSize > 0) {
        result += ":" + QString::number(shuffleItemSize);
    }
    return result;
}

QByteArray CompressionUtils::shuffle(const QByteArray& data, int itemSize) {
    if (itemSize <= 1 || data.isEmpty()) {
        return data;
    }
    
    qint64 dataSize = data.size();
    qint64 numItems = dataSize / itemSize;
    
    QByteArray result(dataSize, 0);
    const uchar* src = reinterpret_cast<const uchar*>(data.constData());
    uchar* dst = reinterpret_cast<uchar*>(result.data());
    
    // Reorder: group first bytes together, then second bytes, etc.
    // Original: [a0 a1 a2 a3][b0 b1 b2 b3][c0 c1 c2 c3]...
    // Shuffled: [a0 b0 c0...][a1 b1 c1...][a2 b2 c2...][a3 b3 c3...]
    for (qint64 i = 0; i < numItems; ++i) {
        for (int b = 0; b < itemSize; ++b) {
            dst[b * numItems + i] = src[i * itemSize + b];
        }
    }
    
    return result;
}

QByteArray CompressionUtils::unshuffle(const QByteArray& data, int itemSize) {
    if (itemSize <= 1 || data.isEmpty()) {
        return data;
    }
    
    qint64 dataSize = data.size();
    qint64 numItems = dataSize / itemSize;
    
    QByteArray result(dataSize, 0);
    const uchar* src = reinterpret_cast<const uchar*>(data.constData());
    uchar* dst = reinterpret_cast<uchar*>(result.data());
    
    // Reverse the shuffle operation
    for (qint64 i = 0; i < numItems; ++i) {
        for (int b = 0; b < itemSize; ++b) {
            dst[i * itemSize + b] = src[b * numItems + i];
        }
    }
    
    return result;
}

QByteArray CompressionUtils::decompress(const QByteArray& data, Codec codec,
                                        qint64 uncompressedSize, QString* errorMsg) {
    switch (codec) {
        case Codec_None:
            return data;
        case Codec_Zlib:
            return decompressZlib(data, uncompressedSize, errorMsg);
        case Codec_LZ4:
        case Codec_LZ4HC:
            return decompressLZ4(data, uncompressedSize, errorMsg);
        case Codec_Zstd:
            return decompressZstd(data, uncompressedSize, errorMsg);
        default:
            if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Unknown compression codec");
            return QByteArray();
    }
}

QByteArray CompressionUtils::compress(const QByteArray& data, Codec codec, int level, QString* errorMsg) {
    if (level < 0) level = defaultLevel(codec);
    
    switch (codec) {
        case Codec_None:
            return data;
        case Codec_Zlib:
            return compressZlib(data, level, errorMsg);
        case Codec_LZ4:
            return compressLZ4(data, errorMsg);
        case Codec_LZ4HC:
            return compressLZ4HC(data, level, errorMsg);
        case Codec_Zstd:
            return compressZstd(data, level, errorMsg);
        default:
            if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Unknown compression codec");
            return QByteArray();
    }
}

// ============================================================================
// Zlib Implementation
// ============================================================================

QByteArray CompressionUtils::decompressZlib(const QByteArray& data, qint64 uncompressedSize, QString* errorMsg) {
    if (data.isEmpty()) return QByteArray();
    
    QByteArray result(uncompressedSize, 0);
    
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    strm.avail_in = static_cast<uInt>(data.size());
    strm.next_out = reinterpret_cast<Bytef*>(result.data());
    strm.avail_out = static_cast<uInt>(uncompressedSize);
    
    int ret = inflateInit(&strm);
    if (ret != Z_OK) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Zlib inflateInit failed: %1").arg(ret);
        return QByteArray();
    }
    
    ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    
    if (ret != Z_STREAM_END) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Zlib inflate failed: %1").arg(ret);
        return QByteArray();
    }
    
    return result;
}

QByteArray CompressionUtils::compressZlib(const QByteArray& data, int level, QString* errorMsg) {
    if (data.isEmpty()) return QByteArray();
    
    // Worst case: output slightly larger than input
    uLongf destLen = compressBound(data.size());
    QByteArray result(destLen, 0);
    
    int ret = compress2(
        reinterpret_cast<Bytef*>(result.data()), &destLen,
        reinterpret_cast<const Bytef*>(data.constData()), data.size(),
        level
    );
    
    if (ret != Z_OK) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Zlib compress failed: %1").arg(ret);
        return QByteArray();
    }
    
    result.resize(destLen);
    return result;
}

// ============================================================================
// LZ4 Implementation
// ============================================================================

QByteArray CompressionUtils::decompressLZ4(const QByteArray& data, qint64 uncompressedSize, QString* errorMsg) {
#ifdef HAVE_LZ4
    if (data.isEmpty()) return QByteArray();
    
    QByteArray result(uncompressedSize, 0);
    
    int decompressedSize = LZ4_decompress_safe(
        data.constData(),
        result.data(),
        data.size(),
        static_cast<int>(uncompressedSize)
    );
    
    if (decompressedSize < 0) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "LZ4 decompress failed");
        return QByteArray();
    }
    
    result.resize(decompressedSize);
    return result;
#else
    if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "LZ4 support not compiled in");
    Q_UNUSED(data);
    Q_UNUSED(uncompressedSize);
    return QByteArray();
#endif
}

QByteArray CompressionUtils::compressLZ4(const QByteArray& data, QString* errorMsg) {
#ifdef HAVE_LZ4
    if (data.isEmpty()) return QByteArray();
    
    int maxSize = LZ4_compressBound(data.size());
    QByteArray result(maxSize, 0);
    
    int compressedSize = LZ4_compress_default(
        data.constData(),
        result.data(),
        data.size(),
        maxSize
    );
    
    if (compressedSize <= 0) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "LZ4 compress failed");
        return QByteArray();
    }
    
    result.resize(compressedSize);
    return result;
#else
    if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "LZ4 support not compiled in");
    Q_UNUSED(data);
    return QByteArray();
#endif
}

QByteArray CompressionUtils::compressLZ4HC(const QByteArray& data, int level, QString* errorMsg) {
#ifdef HAVE_LZ4
    if (data.isEmpty()) return QByteArray();
    
    int maxSize = LZ4_compressBound(data.size());
    QByteArray result(maxSize, 0);
    
    int compressedSize = LZ4_compress_HC(
        data.constData(),
        result.data(),
        data.size(),
        maxSize,
        level
    );
    
    if (compressedSize <= 0) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "LZ4HC compress failed");
        return QByteArray();
    }
    
    result.resize(compressedSize);
    return result;
#else
    if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "LZ4 support not compiled in");
    Q_UNUSED(data);
    Q_UNUSED(level);
    return QByteArray();
#endif
}

// ============================================================================
// Zstandard Implementation
// ============================================================================

QByteArray CompressionUtils::decompressZstd(const QByteArray& data, qint64 uncompressedSize, QString* errorMsg) {
#ifdef HAVE_ZSTD
    if (data.isEmpty()) return QByteArray();
    
    QByteArray result(uncompressedSize, 0);
    
    size_t decompressedSize = ZSTD_decompress(
        result.data(),
        static_cast<size_t>(uncompressedSize),
        data.constData(),
        static_cast<size_t>(data.size())
    );
    
    if (ZSTD_isError(decompressedSize)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Zstd decompress failed: %1")
            .arg(QString::fromUtf8(ZSTD_getErrorName(decompressedSize)));
        return QByteArray();
    }
    
    result.resize(static_cast<int>(decompressedSize));
    return result;
#else
    if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Zstd support not compiled in");
    Q_UNUSED(data);
    Q_UNUSED(uncompressedSize);
    return QByteArray();
#endif
}

QByteArray CompressionUtils::compressZstd(const QByteArray& data, int level, QString* errorMsg) {
#ifdef HAVE_ZSTD
    if (data.isEmpty()) return QByteArray();
    
    size_t maxSize = ZSTD_compressBound(static_cast<size_t>(data.size()));
    QByteArray result(static_cast<int>(maxSize), 0);
    
    size_t compressedSize = ZSTD_compress(
        result.data(),
        maxSize,
        data.constData(),
        static_cast<size_t>(data.size()),
        level
    );
    
    if (ZSTD_isError(compressedSize)) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Zstd compress failed: %1")
            .arg(QString::fromUtf8(ZSTD_getErrorName(compressedSize)));
        return QByteArray();
    }
    
    result.resize(static_cast<int>(compressedSize));
    return result;
#else
    if (errorMsg) *errorMsg = QCoreApplication::translate("CompressionUtils", "Zstd support not compiled in");
    Q_UNUSED(data);
    Q_UNUSED(level);
    return QByteArray();
#endif
}
