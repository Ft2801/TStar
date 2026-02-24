#include "RawLoader.h"
#include "../ImageBuffer.h"

#include <QFileInfo>
#include <QDateTime>
#include <algorithm>
#include <cmath>

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#ifdef HAVE_LIBRAW

// Cross-platform file open (handles wide paths on Windows)
static int rawOpenFile(libraw_data_t* lr, const QString& filePath)
{
#if defined(_WIN32)
    // Use the wide-char API so Unicode/long paths work correctly on Windows.
    const std::wstring wpath = filePath.toStdWString();
    return libraw_open_wfile(lr, wpath.c_str());
#else
    return libraw_open_file(lr, filePath.toUtf8().constData());
#endif
}

// Derive the Bayer pattern string from the libraw filter mask and image margins.
static QString deduceBayerPattern(libraw_data_t* lr)
{
    unsigned int f = lr->idata.filters;

    // X-Trans (Fujifilm) – 6x6 pattern, not a simple Bayer grid
    if (f == 9)
        return "XTRANS";

    // Foveon / no-filter sensors
    if (f == 0)
        return QString();

    // Standard 2×2 Bayer from the 4-bit filter code
    // Bits at positions (row%2, col%2) encode R=0, G=1, B=2
    // We look at the top-left 2×2 block of the *visible* image area
    int left = lr->rawdata.sizes.left_margin;
    int top  = lr->rawdata.sizes.top_margin;

    char pat[5] = {0};
    const char* desc = lr->idata.cdesc; // "RGBG" or "RGGB" etc.
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            int row = top + r;
            int col = left + c;
            int idx = (f >> ((((row) << 1 & 14) + (col & 1)) << 1)) & 3;
            pat[r * 2 + c] = desc[idx];
        }
    }

    QString pattern = QString::fromLatin1(pat, 4);

    // Fuji "diagonal" sensors sometimes report different patterns
    if (pattern != "RGGB" && pattern != "BGGR" &&
        pattern != "GRBG" && pattern != "GBRG") {
        // Fall back to a simple decode from the raw mask
        switch (f & 0xffffffff) {
            case 0x94949494: pattern = "RGGB"; break;
            case 0x16161616: pattern = "BGGR"; break;
            case 0x61616161: pattern = "GRBG"; break;
            case 0x49494949: pattern = "GBRG"; break;
            default:         pattern = "RGGB"; break;
        }
    }

    return pattern;
}

// Approximate sensor pixel pitch from the camera format code
static float estimatePixelPitch(libraw_data_t* lr)
{
    float sWidth = 0.0f;
    switch (lr->lens.makernotes.CameraFormat) {
        case LIBRAW_FORMAT_APSC:
            sWidth = (qstrncmp(lr->idata.make, "Canon", 5) == 0) ? 22.3f : 23.6f;
            break;
        case LIBRAW_FORMAT_FF:
            sWidth = (qstrncmp(lr->idata.make, "Sony", 4) == 0) ? 35.6f : 36.0f;
            break;
        case LIBRAW_FORMAT_FT:   sWidth = 17.3f; break;
        case LIBRAW_FORMAT_APSH: sWidth = 28.7f; break;
        case LIBRAW_FORMAT_MF:   sWidth = 44.0f; break;
        default: break;
    }
    if (sWidth <= 0.0f) return 0.0f;
    float pitch = sWidth / static_cast<float>(lr->sizes.width) * 1000.0f;
    return std::round(pitch * 100.0f) / 100.0f;
}

#endif // HAVE_LIBRAW

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace RawLoader {

static const QStringList& supportedExtensions()
{
    static const QStringList exts = {
        "cr2",  "cr3",  "crw",          // Canon
        "nef",  "nrw",                   // Nikon
        "arw",  "srf",  "sr2",           // Sony
        "dng",                           // Adobe / universal
        "orf",  "ori",                   // Olympus
        "rw2",                           // Panasonic
        "raf",                           // Fujifilm
        "pef",  "ptx",                   // Pentax
        "raw",  "rwl",                   // Leica / other
        "mrw",                           // Minolta
        "3fr",                           // Hasselblad
        "ari",                           // ARRI
        "bay",                           // Casio
        "cap",  "iiq",                   // Phase One
        "eip",                           // Phase One (packed)
        "erf",                           // Epson
        "fff",                           // Hasselblad
        "gpr",                           // GoPro
        "k25",  "kc2",  "kdc",           // Kodak
        "mdc",                           // Minolta
        "mef",                           // Mamiya
        "mos",                           // Leaf
        "obm",                           // ??? (some libraw-supported)
        "r3d",                           // RED
        "rwz",                           // Rawzor
        "srw",                           // Samsung
        "x3f",                           // Sigma
    };
    return exts;
}

bool isSupportedExtension(const QString& ext)
{
    return supportedExtensions().contains(ext.toLower());
}

QString filterString()
{
    QStringList parts;
    for (const QString& e : supportedExtensions()) {
        parts << ("*." + e);
        parts << ("*." + e.toUpper());
    }
    return "RAW Files (" + parts.join(' ') + ")";
}

bool load(const QString& filePath, ImageBuffer& buf, QString* errorMsg)
{
#ifndef HAVE_LIBRAW
    if (errorMsg)
        *errorMsg = "RAW support not compiled in (HAVE_LIBRAW missing).";
    return false;
#else
    libraw_data_t* lr = libraw_init(0);
    if (!lr) {
        if (errorMsg) *errorMsg = "Failed to initialise LibRaw.";
        return false;
    }

    // Disable automatic processing – we want raw CFA data only
    lr->params.user_flip   = 0;   // no auto-rotation
    lr->params.output_color = 0;  // keep raw colour space

    int ret = rawOpenFile(lr, filePath);
    if (ret != LIBRAW_SUCCESS) {
        if (errorMsg)
            *errorMsg = QString("LibRaw open error: %1").arg(libraw_strerror(ret));
        libraw_close(lr);
        return false;
    }

    ret = libraw_unpack(lr);
    if (ret != LIBRAW_SUCCESS) {
        if (errorMsg)
            *errorMsg = QString("LibRaw unpack error: %1").arg(libraw_strerror(ret));
        libraw_close(lr);
        return false;
    }

    // Make sure we have raw sensor data (some DNGs built from Lightroom may lack it)
    if (!lr->rawdata.raw_image && !lr->rawdata.color3_image && !lr->rawdata.color4_image) {
        if (errorMsg)
            *errorMsg = "No RAW sensor data available in this file.";
        libraw_close(lr);
        return false;
    }

    if (!lr->rawdata.raw_image) {
        // Pixel data is already a processed colour image – not CFA.
        if (errorMsg)
            *errorMsg = "This file has no Bayer CFA data (already processed DNG?). "
                        "Open it as a TIFF/JPEG instead.";
        libraw_close(lr);
        return false;
    }

    // Dimensions
    const int rawW  = lr->rawdata.sizes.raw_width;
    const int left  = lr->rawdata.sizes.left_margin;
    const int top   = lr->rawdata.sizes.top_margin;
    int visW        = lr->rawdata.sizes.width;
    int visH        = lr->rawdata.sizes.height;

    // Fuji diagonal sensor correction
    if (lr->rawdata.ioparams.fuji_width) {
        int rightMargin = rawW - lr->rawdata.ioparams.fuji_width - left;
        visW = rawW - rightMargin;
        visH = lr->rawdata.sizes.raw_height;
    }

    // Black / white levels
    float black   = static_cast<float>(lr->color.black);
    float maximum = static_cast<float>(lr->color.maximum);
    float range   = maximum - black;
    if (range <= 0.0f) range = 65535.0f;

    // Allocate output: single-channel (CFA / Bayer) float image
    buf.resize(visW, visH, 1);
    float* dst = buf.data().data();

    const unsigned short* src = lr->rawdata.raw_image;
    const size_t size = static_cast<size_t>(visW) * visH;

    // Copy + normalise
    for (int y = 0; y < visH; ++y) {
        int srcRow = y + top;
        for (int x = 0; x < visW; ++x) {
            float val = static_cast<float>(src[srcRow * rawW + (x + left)]);
            dst[static_cast<size_t>(y) * visW + x] =
                std::max(0.0f, (val - black) / range);
        }
    }

    // ---- Metadata ------------------------------------------------
    ImageBuffer::Metadata& meta = buf.metadata();

    meta.isMono        = true;
    meta.bayerPattern  = deduceBayerPattern(lr);
    meta.exposure      = lr->other.shutter;
    meta.focalLength   = lr->other.focal_len;
    meta.filePath      = filePath;

    // Pixel pitch
    float pitch = estimatePixelPitch(lr);
    if (pitch > 0.0f)
        meta.pixelSize = static_cast<double>(pitch);

    // Date/time
    if (lr->other.timestamp > 0) {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(lr->other.timestamp),
            QTimeZone::utc());
        meta.dateObs = dt.toString(Qt::ISODate);
    }

    // Camera model as "instrume" equivalent stored in raw headers
    QString instrume = QString("%1 %2")
                           .arg(QString::fromLatin1(lr->idata.make).trimmed())
                           .arg(QString::fromLatin1(lr->idata.model).trimmed());

    // Store extra info as raw header cards for the header panel
    meta.rawHeaders.push_back({"INSTRUME", instrume, "Camera make and model"});
    meta.rawHeaders.push_back({"BAYERPAT", meta.bayerPattern, "Bayer pattern"});

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
    meta.rawHeaders.push_back({"BITPIX",  "16", "Original ADC bit depth"});

    libraw_recycle(lr);
    libraw_close(lr);

    Q_UNUSED(size)
    return true;
#endif // HAVE_LIBRAW
}

} // namespace RawLoader
