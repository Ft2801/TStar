#include "RawLoader.h"
#include "../ImageBuffer.h"
#include "IccProfileExtractor.h"
#include "core/ColorProfileManager.h"

#include <QFileInfo>
#include <QDateTime>

#include <algorithm>
#include <cmath>

#ifdef HAVE_LIBRAW
#  include <libraw/libraw.h>
#endif


// =============================================================================
// Internal helpers (compiled only when LibRaw is available)
// =============================================================================

#ifdef HAVE_LIBRAW

/**
 * @brief Opens a RAW file with the appropriate LibRaw API for the platform.
 *
 * On Windows the wide-character API is used to correctly handle Unicode and
 * long path names. On all other platforms the UTF-8 API is used.
 */
static int rawOpenFile(libraw_data_t* lr, const QString& filePath)
{
#if defined(_WIN32)
    const std::wstring wpath = filePath.toStdWString();
    return libraw_open_wfile(lr, wpath.c_str());
#else
    return libraw_open_file(lr, filePath.toUtf8().constData());
#endif
}

/**
 * @brief Derives the Bayer pattern string from the LibRaw filter mask.
 *
 * Handles three sensor types:
 *   - X-Trans (Fujifilm): 6x6 pattern, returned as a 36-character string.
 *   - Foveon / filterless: returns an empty string.
 *   - Standard 2x2 Bayer: returns one of "RGGB", "BGGR", "GRBG", or "GBRG".
 */
static QString deduceBayerPattern(libraw_data_t* lr)
{
    const unsigned int f = lr->idata.filters;

    // X-Trans sensors use a 6x6 repeating pattern.
    if (f == 9)
    {
        QString pat;
        const char* desc = lr->idata.cdesc;
        for (int r = 0; r < 6; ++r)
            for (int c = 0; c < 6; ++c)
                pat += desc[static_cast<unsigned char>(lr->idata.xtrans[r][c])];
        return pat;
    }

    // Foveon and filterless sensors have no Bayer pattern.
    if (f == 0)
        return QString();

    // Decode the standard 2x2 Bayer pattern from the top-left corner of the
    // visible image area, using the LibRaw filter mask bit encoding.
    const int left = lr->rawdata.sizes.left_margin;
    const int top  = lr->rawdata.sizes.top_margin;
    const char* desc = lr->idata.cdesc;

    char pat[5] = {0};
    for (int r = 0; r < 2; ++r)
    {
        for (int c = 0; c < 2; ++c)
        {
            const int row = top + r;
            const int col = left + c;
            const int idx = (f >> ((((row) << 1 & 14) + (col & 1)) << 1)) & 3;
            pat[r * 2 + c] = desc[idx];
        }
    }

    QString pattern = QString::fromLatin1(pat, 4);

    // Some Fuji diagonal sensors report non-standard patterns. Fall back to a
    // mask-based lookup when the decoded string is not one of the four canonical
    // 2x2 patterns.
    if (pattern != "RGGB" && pattern != "BGGR" &&
        pattern != "GRBG" && pattern != "GBRG")
    {
        switch (f & 0xffffffff)
        {
            case 0x94949494: pattern = "RGGB"; break;
            case 0x16161616: pattern = "BGGR"; break;
            case 0x61616161: pattern = "GRBG"; break;
            case 0x49494949: pattern = "GBRG"; break;
            default:         pattern = "RGGB"; break;
        }
    }

    return pattern;
}

/**
 * @brief Estimates the sensor pixel pitch (micrometres) from the camera format code.
 *
 * Uses a lookup table of typical sensor widths for common formats. Returns 0
 * when the format is unknown or unrecognised.
 */
static float estimatePixelPitch(libraw_data_t* lr)
{
    float sWidth = 0.0f;

    switch (lr->lens.makernotes.CameraFormat)
    {
        case LIBRAW_FORMAT_APSC:
            // Canon APS-C: 22.3 mm; other brands: 23.6 mm.
            sWidth = (qstrncmp(lr->idata.make, "Canon", 5) == 0) ? 22.3f : 23.6f;
            break;
        case LIBRAW_FORMAT_FF:
            // Sony full-frame: 35.6 mm; others: 36.0 mm.
            sWidth = (qstrncmp(lr->idata.make, "Sony", 4) == 0) ? 35.6f : 36.0f;
            break;
        case LIBRAW_FORMAT_FT:   sWidth = 17.3f; break;
        case LIBRAW_FORMAT_APSH: sWidth = 28.7f; break;
        case LIBRAW_FORMAT_MF:   sWidth = 44.0f; break;
        default: break;
    }

    if (sWidth <= 0.0f)
        return 0.0f;

    const float pitch = sWidth / static_cast<float>(lr->sizes.width) * 1000.0f;
    return std::round(pitch * 100.0f) / 100.0f;
}

#endif // HAVE_LIBRAW


// =============================================================================
// Supported extension registry
// =============================================================================

namespace RawLoader {

static const QStringList& supportedExtensions()
{
    static const QStringList exts = {
        "cr2", "cr3", "crw",           // Canon
        "nef", "nrw",                   // Nikon
        "arw", "srf", "sr2",            // Sony
        "dng",                          // Adobe / universal DNG
        "orf", "ori",                   // Olympus
        "rw2",                          // Panasonic
        "raf",                          // Fujifilm
        "pef", "ptx",                   // Pentax
        "raw", "rwl",                   // Leica / miscellaneous
        "mrw",                          // Minolta
        "3fr",                          // Hasselblad
        "ari",                          // ARRI
        "bay",                          // Casio
        "cap", "iiq",                   // Phase One
        "eip",                          // Phase One (packed)
        "erf",                          // Epson
        "fff",                          // Hasselblad (alternate)
        "gpr",                          // GoPro
        "k25", "kc2", "kdc",            // Kodak
        "mdc",                          // Minolta (alternate)
        "mef",                          // Mamiya
        "mos",                          // Leaf
        "obm",                          // Various (LibRaw-supported)
        "r3d",                          // RED
        "rwz",                          // Rawzor
        "srw",                          // Samsung
        "x3f",                          // Sigma
    };
    return exts;
}


// =============================================================================
// Public API
// =============================================================================

bool isSupportedExtension(const QString& ext)
{
    return supportedExtensions().contains(ext.toLower());
}

QString filterString()
{
    QStringList parts;
    for (const QString& e : supportedExtensions())
    {
        parts << ("*." + e);
        parts << ("*." + e.toUpper());
    }
    return "RAW Files (" + parts.join(' ') + ")";
}

bool load(const QString& filePath, ImageBuffer& buf, QString* errorMsg)
{
#ifndef HAVE_LIBRAW
    Q_UNUSED(filePath)
    Q_UNUSED(buf)
    if (errorMsg)
        *errorMsg = "RAW support not compiled in (HAVE_LIBRAW missing).";
    return false;

#else
    libraw_data_t* lr = libraw_init(0);
    if (!lr)
    {
        if (errorMsg) *errorMsg = "Failed to initialise LibRaw.";
        return false;
    }

    // Disable automatic image processing; we want raw CFA data only.
    lr->params.user_flip    = 0;  // Suppress auto-rotation.
    lr->params.output_color = 0;  // Keep the raw colour space.

    int ret = rawOpenFile(lr, filePath);
    if (ret != LIBRAW_SUCCESS)
    {
        if (errorMsg)
            *errorMsg = QString("LibRaw open error: %1").arg(libraw_strerror(ret));
        libraw_close(lr);
        return false;
    }

    ret = libraw_unpack(lr);
    if (ret != LIBRAW_SUCCESS)
    {
        if (errorMsg)
            *errorMsg = QString("LibRaw unpack error: %1").arg(libraw_strerror(ret));
        libraw_close(lr);
        return false;
    }

    // Validate that raw sensor data is available.
    if (!lr->rawdata.raw_image &&
        !lr->rawdata.color3_image &&
        !lr->rawdata.color4_image)
    {
        if (errorMsg)
            *errorMsg = "No RAW sensor data available in this file.";
        libraw_close(lr);
        return false;
    }

    // Determine the visible image dimensions, accounting for margin regions.
    const int rawW = lr->rawdata.sizes.raw_width;
    const int left = lr->rawdata.sizes.left_margin;
    const int top  = lr->rawdata.sizes.top_margin;
    int       visW = lr->rawdata.sizes.width;
    int       visH = lr->rawdata.sizes.height;

    // Apply a correction for Fuji's diagonal sensor layout.
    if (lr->rawdata.ioparams.fuji_width)
    {
        const int rightMargin = rawW - lr->rawdata.ioparams.fuji_width - left;
        visW = rawW - rightMargin;
        visH = lr->rawdata.sizes.raw_height;
    }

    // Compute the black-level and white-level for normalisation.
    const float black   = static_cast<float>(lr->color.black);
    const float maximum = static_cast<float>(lr->color.maximum);
    float       range   = maximum - black;
    if (range <= 0.0f) range = 65535.0f;

    if (lr->rawdata.raw_image)
    {
        // Allocate the output buffer as a single-channel (CFA) float image.
        buf.resize(visW, visH, 1);
        float*               dst = buf.data().data();
        const unsigned short* src = lr->rawdata.raw_image;

        // Copy and normalise the visible sensor area.
        for (int y = 0; y < visH; ++y)
        {
            const int srcRow = y + top;
            for (int x = 0; x < visW; ++x)
            {
                const float val = static_cast<float>(src[srcRow * rawW + (x + left)]);
                dst[static_cast<size_t>(y) * visW + x] =
                    std::max(0.0f, (val - black) / range);
            }
        }
    }
    else if (lr->rawdata.color3_image)
    {
        // 3-channel RGB raw data (e.g. Foveon, some DNGs)
        buf.resize(visW, visH, 3);
        float* dst = buf.data().data();
        const unsigned short (*src)[3] = reinterpret_cast<const unsigned short(*)[3]>(lr->rawdata.color3_image);

        for (int y = 0; y < visH; ++y)
        {
            const int srcRow = y + top;
            for (int x = 0; x < visW; ++x)
            {
                const size_t srcIdx = srcRow * rawW + (x + left);
                const size_t dstIdx = (static_cast<size_t>(y) * visW + x) * 3;
                dst[dstIdx + 0] = std::max(0.0f, (src[srcIdx][0] - black) / range);
                dst[dstIdx + 1] = std::max(0.0f, (src[srcIdx][1] - black) / range);
                dst[dstIdx + 2] = std::max(0.0f, (src[srcIdx][2] - black) / range);
            }
        }
    }
    else if (lr->rawdata.color4_image)
    {
        // 4-channel RGBG raw data
        buf.resize(visW, visH, 3);
        float* dst = buf.data().data();
        const unsigned short (*src)[4] = reinterpret_cast<const unsigned short(*)[4]>(lr->rawdata.color4_image);

        for (int y = 0; y < visH; ++y)
        {
            const int srcRow = y + top;
            for (int x = 0; x < visW; ++x)
            {
                const size_t srcIdx = srcRow * rawW + (x + left);
                const size_t dstIdx = (static_cast<size_t>(y) * visW + x) * 3;
                dst[dstIdx + 0] = std::max(0.0f, (src[srcIdx][0] - black) / range);
                // Average the two green channels
                dst[dstIdx + 1] = std::max(0.0f, (((src[srcIdx][1] + src[srcIdx][3]) / 2.0f) - black) / range);
                dst[dstIdx + 2] = std::max(0.0f, (src[srcIdx][2] - black) / range);
            }
        }
    }

    // ---- Metadata -----------------------------------------------------------

    ImageBuffer::Metadata& meta = buf.metadata();

    if (lr->rawdata.raw_image)
    {
        meta.isMono       = true;
        meta.bayerPattern = deduceBayerPattern(lr);
    }
    else
    {
        meta.isMono       = false;
        meta.bayerPattern = ""; // No Bayer matrix for 3/4 channel raw
    }
    
    meta.exposure     = lr->other.shutter;
    meta.focalLength  = lr->other.focal_len;
    meta.filePath     = filePath;

    // Estimate pixel pitch from the camera format if LibRaw exposes it.
    const float pitch = estimatePixelPitch(lr);
    if (pitch > 0.0f)
        meta.pixelSize = static_cast<double>(pitch);

    // Observation timestamp.
    if (lr->other.timestamp > 0)
    {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(lr->other.timestamp),
            QTimeZone::utc());
        meta.dateObs = dt.toString(Qt::ISODate);
    }

    // Camera identification.
    const QString instrume =
        QString("%1 %2")
            .arg(QString::fromLatin1(lr->idata.make).trimmed())
            .arg(QString::fromLatin1(lr->idata.model).trimmed());

    // Populate raw header cards for the metadata panel.
    meta.rawHeaders.push_back({"INSTRUME", instrume,              "Camera make and model"});
    
    if (meta.isMono)
        meta.rawHeaders.push_back({"BAYERPAT", meta.bayerPattern,     "Bayer pattern"});

    if (lr->other.iso_speed > 0.0f)
        meta.rawHeaders.push_back({"ISO",
            QString::number(static_cast<int>(lr->other.iso_speed)), "ISO speed"});

    if (lr->other.shutter > 0.0f)
        meta.rawHeaders.push_back({"EXPTIME",
            QString::number(lr->other.shutter, 'f', 6), "Exposure time [s]"});

    if (lr->other.aperture > 0.0f)
        meta.rawHeaders.push_back({"APERTURE",
            QString::number(lr->other.aperture, 'f', 1), "Aperture (f-number)"});

    if (lr->other.focal_len > 0.0f)
        meta.rawHeaders.push_back({"FOCALLEN",
            QString::number(static_cast<int>(lr->other.focal_len)), "Focal length [mm]"});

    if (!meta.dateObs.isEmpty())
        meta.rawHeaders.push_back({"DATE-OBS", meta.dateObs, "Date of observation (UTC)"});

    meta.rawHeaders.push_back({"NAXIS1",  QString::number(visW), "Image width"});
    meta.rawHeaders.push_back({"NAXIS2",  QString::number(visH), "Image height"});
    meta.rawHeaders.push_back({"BITPIX",  "16",                  "Original ADC bit depth"});
    meta.rawHeaders.push_back({"ROWORDER", "TOP-DOWN",           "Image row order"});

    // Attempt ICC profile extraction.
    if (IccProfileExtractor::extractFromFile(filePath, meta.iccData))
    {
        core::ColorProfile profile(meta.iccData);
        if (profile.isValid())
        {
            meta.iccProfileName = profile.name();
            meta.iccProfileType = static_cast<int>(profile.type());
        }
    }

    libraw_recycle(lr);
    libraw_close(lr);
    return true;
#endif // HAVE_LIBRAW
}

} // namespace RawLoader