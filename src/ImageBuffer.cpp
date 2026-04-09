// ============================================================================
// ImageBuffer.cpp
// Core image buffer implementation: construction, I/O, display, stretching,
// swap management, color profiles, and file saving.
// GHS stretch, geometric transformations, color corrections, wavelet
// decomposition, star detection, saturation, ArcSinh stretch, pixel
// accessors, histogram computation, spline application, and serialization.
// ============================================================================


// ---------------------------------------------------------------------------
// Includes
// ---------------------------------------------------------------------------

#include "ImageBuffer.h"

// Core modules
#include "core/SwapManager.h"
#include "core/SimdOps.h"
#include "core/RobustStatistics.h"
#include "core/ColorProfileManager.h"
#include "core/Version.h"

// I/O modules
#include "io/IccProfileExtractor.h"
#include "io/SimpleTiffWriter.h"
#include "io/SimpleTiffReader.h"
#include "io/XISFReader.h"
#include "io/XISFWriter.h"
#include "io/FitsLoader.h"

// Algorithm modules
#include "algos/StatisticalStretch.h"

// Photometry and astrometry
#include "photometry/StarDetector.h"
#include "astrometry/WCSUtils.h"

// Third-party libraries
#include <opencv2/opencv.hpp>
#include <fitsio.h>
#include <lcms2.h>

// Qt modules
#include <QtConcurrent/QtConcurrent>
#include <QBuffer>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QColorSpace>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDataStream>
#include <QSettings>
#include <QDebug>

// Standard library
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stack>
#include <omp.h>


// ============================================================================
// Construction, Copy, Destruction
// ============================================================================

ImageBuffer::ImageBuffer()
    : m_mutex(std::make_unique<QReadWriteLock>(QReadWriteLock::Recursive))
{
}

ImageBuffer::ImageBuffer(int width, int height, int channels)
    : m_width(width)
    , m_height(height)
    , m_channels(channels)
    , m_mutex(std::make_unique<QReadWriteLock>(QReadWriteLock::Recursive))
{
    m_data.resize(static_cast<size_t>(width) * height * channels, 0.0f);
    m_lastAccess = QDateTime::currentMSecsSinceEpoch();
    SwapManager::instance().registerBuffer(this);
}

ImageBuffer::ImageBuffer(const ImageBuffer& other)
    : m_width(other.m_width)
    , m_height(other.m_height)
    , m_channels(other.m_channels)
    , m_meta(other.m_meta)
    , m_name(other.m_name)
    , m_modified(other.m_modified)
    , m_mask(other.m_mask)
    , m_hasMask(other.m_hasMask)
    , m_mutex(std::make_unique<QReadWriteLock>(QReadWriteLock::Recursive))
    , m_lastAccess(QDateTime::currentMSecsSinceEpoch())
{
    ReadLock otherLock(&other);

    // If the source is swapped to disk, restore it before copying pixel data.
    if (other.m_isSwapped) {
        const_cast<ImageBuffer&>(other).forceSwapIn();
    }

    m_data = other.m_data;
    SwapManager::instance().registerBuffer(this);
}

ImageBuffer& ImageBuffer::operator=(const ImageBuffer& other) {
    if (this == &other) return *this;

    ReadLock otherLock(&other);

    // Clean up any existing swap file before overwriting this buffer.
    if (m_isSwapped && !m_swapFile.isEmpty()) {
        QFile::remove(m_swapFile);
        m_swapFile.clear();
    }
    m_isSwapped = false;

    m_width    = other.m_width;
    m_height   = other.m_height;
    m_channels = other.m_channels;
    m_meta     = other.m_meta;
    m_name     = other.m_name;
    m_modified = other.m_modified;
    m_mask     = other.m_mask;
    m_hasMask  = other.m_hasMask;

    if (!m_mutex) {
        m_mutex = std::make_unique<QReadWriteLock>(QReadWriteLock::Recursive);
    }

    // data() handles swap-in transparently via ReadLock.
    m_data = other.data();

    return *this;
}

ImageBuffer::~ImageBuffer() {
    SwapManager::instance().unregisterBuffer(this);

    if (m_isSwapped && !m_swapFile.isEmpty()) {
        QFile::remove(m_swapFile);
    }
}


// ============================================================================
// Metadata and Header Accessors
// ============================================================================

QString ImageBuffer::getHeaderValue(const QString& key) const {
    for (const auto& card : m_meta.rawHeaders) {
        if (card.key == key) return card.value;
    }

    // Fallback for commonly queried metadata fields.
    if (key == "DATE-OBS") return m_meta.dateObs;
    if (key == "OBJECT")   return m_meta.objectName;

    return QString();
}

void ImageBuffer::addHistoryAction(const QString& action) {
    if (action.isEmpty()) return;

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString version   = TStar::getVersion();
    QString entry     = QString("[%1] [%2] %3").arg(version).arg(timestamp).arg(action);

    // Append to raw headers
    m_meta.rawHeaders.push_back({"HISTORY", entry, ""});
    m_modified = true;
}

void ImageBuffer::mergeHistory(const Metadata& other) {
    for (const auto& card : other.rawHeaders) {
        if (card.key == "HISTORY") {
            m_meta.rawHeaders.push_back(card);
        }
    }
}


// ============================================================================
// Mask Management
// ============================================================================

void ImageBuffer::setMask(const MaskLayer& mask) {
    if (mask.isValid() && mask.width == m_width && mask.height == m_height) {
        m_mask = mask;
        m_hasMask = true;
    }
}

const MaskLayer* ImageBuffer::getMask() const {
    return m_hasMask ? &m_mask : nullptr;
}

bool ImageBuffer::hasMask() const {
    return m_hasMask && m_mask.isValid();
}

void ImageBuffer::removeMask() {
    m_hasMask = false;
    m_mask = MaskLayer();
}

void ImageBuffer::invertMask() {
    if (!hasMask() || m_mask.data.empty()) return;

    #pragma omp parallel for
    for (long long i = 0; i < (long long)m_mask.data.size(); ++i) {
        m_mask.data[i] = 1.0f - m_mask.data[i];
    }
    m_mask.inverted = false;
}


// ============================================================================
// Data Access (with swap-in support)
// ============================================================================

const std::vector<float>& ImageBuffer::data() const {
    if (m_isSwapped) {
        const_cast<ImageBuffer*>(this)->forceSwapIn();
    }
    const_cast<ImageBuffer*>(this)->touch();

    Q_ASSERT(!m_data.empty() || (m_width * m_height * m_channels == 0));
    return m_data;
}

std::vector<float>& ImageBuffer::data() {
    if (m_isSwapped) {
        forceSwapIn();
    }
    touch();

    Q_ASSERT(!m_data.empty() || (m_width * m_height * m_channels == 0));
    return m_data;
}


// ============================================================================
// Swap Management (LRU-based disk paging)
// ============================================================================

void ImageBuffer::touch() {
    m_lastAccess = QDateTime::currentMSecsSinceEpoch();
}

bool ImageBuffer::canSwap() const {
    if (m_isSwapped)    return false;
    if (m_data.empty()) return false;

    // Avoid thrashing by not swapping images smaller than 10 MB.
    size_t bytes = m_data.size() * sizeof(float);
    if (bytes < 10 * 1024 * 1024) return false;

    return true;
}

bool ImageBuffer::trySwapOut() {
    if (!canSwap()) return false;

    qDebug() << "[ImageBuffer::trySwapOut]" << m_name << (void*)this
             << "attempting lock...";

    // Try non-blocking write lock. If it fails, the buffer is in active use.
    if (m_mutex->tryLockForWrite()) {
        if (!m_isSwapped && !m_data.empty()) {
            qDebug() << "[ImageBuffer::trySwapOut]" << m_name
                     << "swapping out" << m_data.size() << "floats";
            doSwapOut();
            m_mutex->unlock();
            return true;
        }
        m_mutex->unlock();
    } else {
        qDebug() << "[ImageBuffer::trySwapOut]" << m_name
                 << "lock failed (in use)";
    }

    return false;
}

void ImageBuffer::forceSwapIn() {
    qDebug() << "[ImageBuffer::forceSwapIn]" << m_name << (void*)this
             << "swapped:" << m_isSwapped;

    m_mutex->lockForWrite();
    if (m_isSwapped) {
        qDebug() << "[ImageBuffer::forceSwapIn]" << m_name
                 << "loading from swap file:" << m_swapFile;
        doSwapIn();
    }
    m_mutex->unlock();
}

void ImageBuffer::doSwapOut() {
    QString tempDir  = QDir::tempPath();
    QString filename = QString("%1/tstar_swap_%2.bin")
                           .arg(tempDir)
                           .arg(reinterpret_cast<quintptr>(this));

    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reinterpret_cast<const char*>(m_data.data()),
                   m_data.size() * sizeof(float));
        file.close();

        m_swapFile = filename;
        m_data.clear();
        m_data.shrink_to_fit();
        m_isSwapped = true;
    } else {
        qWarning() << "Failed to swap out buffer to" << filename;
    }
}

void ImageBuffer::doSwapIn() {
    if (!m_isSwapped || m_swapFile.isEmpty()) return;

    QFile file(m_swapFile);
    if (file.open(QIODevice::ReadOnly)) {
        size_t size = static_cast<size_t>(m_width) * m_height * m_channels;
        m_data.resize(size);

        qint64 bytesRead = file.read(reinterpret_cast<char*>(m_data.data()),
                                     size * sizeof(float));
        if (bytesRead != static_cast<qint64>(size * sizeof(float))) {
            qCritical() << "Swap Read Error: Expected" << size * sizeof(float)
                        << "got" << bytesRead;
            std::fill(m_data.begin(), m_data.end(), 0.0f);
        }

        file.close();
        file.remove();
        m_swapFile.clear();
        m_isSwapped = false;
        m_lastAccess = QDateTime::currentMSecsSinceEpoch();
    } else {
        qCritical() << "Failed to swap in buffer from" << m_swapFile;
    }
}


// ============================================================================
// Data Initialization
// ============================================================================

void ImageBuffer::setData(int width, int height, int channels,
                          const std::vector<float>& data) {
    m_width    = width;
    m_height   = height;
    m_channels = channels;
    m_data     = data;

    if (m_data.empty() && width > 0 && height > 0 && channels > 0) {
        m_data.resize(static_cast<size_t>(width) * height * channels, 0.0f);
    }
}

void ImageBuffer::resize(int width, int height, int channels) {
    m_width    = width;
    m_height   = height;
    m_channels = channels;
    m_data.assign(static_cast<size_t>(width) * height * channels, 0.0f);
}


// ============================================================================
// White Balance
// ============================================================================

void ImageBuffer::applyWhiteBalance(float r, float g, float b,
                                    bool lumaProtected) {
    WriteLock lock(this);

    if (m_channels < 3) return;

    // Back up pixel data if luminance protection or mask blending is needed.
    const bool needsBackup = lumaProtected ||
                             (hasMask() && !m_mask.data.empty());
    std::vector<float> originalData;
    if (needsBackup) {
        originalData = m_data;
    }

    long totalPixels = static_cast<long>(m_width) * m_height;

    if (!lumaProtected) {
        // Fast SIMD path: apply uniform per-channel gain.
        SimdOps::applyGainRGB(m_data.data(),
                              static_cast<size_t>(totalPixels), r, g, b);
    } else {
        // Luminance-protected path: attenuate gain in shadows and highlights
        // to preserve neutral tones at extremes.
        auto smoothstep = [](float edge0, float edge1, float x) -> float {
            float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };

        const float* src = originalData.data();
        float*       dst = m_data.data();

        #pragma omp parallel for
        for (long i = 0; i < totalPixels; ++i) {
            float R = src[i * 3 + 0];
            float G = src[i * 3 + 1];
            float B = src[i * 3 + 2];

            // Perceived luminance using Rec. 709 coefficients.
            float L = 0.2126f * R + 0.7152f * G + 0.0722f * B;

            // Weight: peaks at mid-tones, tapers to zero at black and white.
            float w = smoothstep(0.00f, 0.50f, L) *
                      (1.0f - smoothstep(0.50f, 1.00f, L));

            // Effective gain: interpolate between neutral (1.0) and desired gain.
            float er = 1.0f + (r - 1.0f) * w;
            float eg = 1.0f + (g - 1.0f) * w;
            float eb = 1.0f + (b - 1.0f) * w;

            dst[i * 3 + 0] = R * er;
            dst[i * 3 + 1] = G * eg;
            dst[i * 3 + 2] = B * eb;
        }
    }

    // Manually blend with mask if present.
    // (Avoids calling blendResult which would attempt to re-acquire the lock.)
    if (hasMask() && !m_mask.data.empty() && !originalData.empty()) {
        const bool  maskInverted = m_mask.inverted;
        const bool  protectMode  = (m_mask.mode == "protect");
        const float opacity      = m_mask.opacity;

        #pragma omp parallel for schedule(static)
        for (long pi = 0; pi < totalPixels; ++pi) {
            if ((size_t)pi >= m_mask.data.size()) continue;

            float alpha = m_mask.data[pi];
            if (maskInverted) alpha = 1.0f - alpha;
            if (protectMode)  alpha = 1.0f - alpha;
            alpha *= opacity;

            const float inv_alpha = 1.0f - alpha;
            size_t base = (size_t)pi * 3;

            m_data[base + 0] = m_data[base + 0] * alpha +
                               originalData[base + 0] * inv_alpha;
            m_data[base + 1] = m_data[base + 1] * alpha +
                               originalData[base + 1] * inv_alpha;
            m_data[base + 2] = m_data[base + 2] * alpha +
                               originalData[base + 2] * inv_alpha;
        }
    }
}


// ============================================================================
// File Loading
// ============================================================================

bool ImageBuffer::loadRegion(const QString& filePath,
                             int x, int y, int w, int h,
                             QString* errorMsg) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // FITS files support native region loading.
    if (ext == "fit" || ext == "fits") {
        return FitsLoader::loadRegion(filePath, *this, x, y, w, h, errorMsg);
    }

    // For other formats, load the full image then crop.
    bool loaded = false;

    if (ext == "tif" || ext == "tiff") {
        loaded = loadTiff32(filePath, errorMsg);
    } else if (ext == "xisf") {
        loaded = loadXISF(filePath, errorMsg);
    } else {
        loaded = loadStandard(filePath);
    }

    if (loaded) {
        crop(x, y, w, h);
        return true;
    }

    if (errorMsg && errorMsg->isEmpty()) {
        *errorMsg = "Failed to open file or format not supported";
    }
    return false;
}

bool ImageBuffer::loadStandard(const QString& filePath) {
    std::string stdPath = filePath.toStdString();
    cv::Mat img = cv::imread(stdPath, cv::IMREAD_UNCHANGED);

    if (img.empty()) return false;

    int w  = img.cols;
    int h  = img.rows;
    int ch = img.channels();

    // Normalize to 3-channel RGB for internal consistency.
    if (ch == 1) {
        cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        ch = 3;
    } else if (ch == 4) {
        cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);
        ch = 3;
    } else if (ch != 3) {
        return false;
    }

    // Determine normalization scale based on bit depth.
    cv::Mat floatMat;
    double scale = 1.0;
    switch (img.depth()) {
        case CV_8U:  scale = 1.0 / 255.0;   break;
        case CV_16U: scale = 1.0 / 65535.0;  break;
        case CV_32F: scale = 1.0;            break;
        default:     scale = 1.0 / 255.0;    break;
    }
    img.convertTo(floatMat, CV_32FC3, scale);

    m_width    = w;
    m_height   = h;
    m_channels = 3;
    m_data.resize(static_cast<size_t>(w) * h * 3);

    // Convert BGR (OpenCV) to RGB (internal format).
    for (int y = 0; y < h; ++y) {
        const float* row = floatMat.ptr<float>(y);
        for (int x = 0; x < w; ++x) {
            size_t dstIdx = (static_cast<size_t>(y) * w + x) * 3;
            size_t srcIdx = x * 3;
            m_data[dstIdx + 0] = row[srcIdx + 2]; // R
            m_data[dstIdx + 1] = row[srcIdx + 1]; // G
            m_data[dstIdx + 2] = row[srcIdx + 0]; // B
        }
    }

    // Extract embedded ICC profile if available.
    Metadata& meta = metadata();
    meta.iccData.clear();
    meta.iccProfileName.clear();
    meta.iccProfileType = -1;

    QByteArray iccData;
    if (IccProfileExtractor::extractFromFile(filePath, iccData)) {
        meta.iccData = iccData;
        core::ColorProfile profile(iccData);
        if (profile.isValid()) {
            meta.iccProfileName = profile.name();
            meta.iccProfileType = static_cast<int>(profile.type());
        }
    }

    return true;
}

bool ImageBuffer::loadTiff32(const QString& filePath,
                             QString* errorMsg, QString* debugInfo) {
    std::string stdPath = filePath.toStdString();

    // IMREAD_UNCHANGED preserves original bit depth and channel count.
    cv::Mat img = cv::imread(stdPath, cv::IMREAD_UNCHANGED);

    if (img.empty()) {
        // Fallback to SimpleTiffReader for 32-bit unsigned TIFF support.
        int w, h, c;
        std::vector<float> data;
        if (SimpleTiffReader::readFloat32(filePath, w, h, c, data,
                                         errorMsg, debugInfo)) {
            setData(w, h, c, data);
            return true;
        }
        return false;
    }

    int w  = img.cols;
    int h  = img.rows;
    int ch = img.channels();

    // Normalize channel count for internal consistency.
    if (ch == 1) {
        cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        ch = 3;
    } else if (ch == 4) {
        cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);
        ch = 3;
    }

    // Determine normalization scale and handle special bit depths.
    cv::Mat floatMat;
    double scale = 1.0;
    switch (img.depth()) {
        case CV_8U:  scale = 1.0 / 255.0;   break;
        case CV_16U: scale = 1.0 / 65535.0;  break;
        case CV_32S: {
            // 32-bit signed: try SimpleTiffReader first for unsigned handling.
            int tw, th, tc;
            std::vector<float> tdata;
            if (SimpleTiffReader::readFloat32(filePath, tw, th, tc, tdata,
                                             errorMsg, debugInfo)) {
                setData(tw, th, tc, tdata);
                return true;
            }
            scale = 1.0 / 2147483647.0;
            break;
        }
        case CV_32F: scale = 1.0; break;
        case CV_64F: scale = 1.0; break;
        default:
            if (errorMsg) *errorMsg = QObject::tr("Unsupported TIFF bit depth.");
            return false;
    }

    img.convertTo(floatMat, CV_32FC(ch), scale);

    // Convert BGR to RGB and copy into our interleaved data structure.
    std::vector<float> data(w * h * ch);
    for (int y = 0; y < h; ++y) {
        const float* row = floatMat.ptr<float>(y);
        for (int x = 0; x < w; ++x) {
            int srcIdx = x * ch;
            int dstIdx = (y * w + x) * ch;
            data[dstIdx + 0] = row[srcIdx + 2]; // R
            data[dstIdx + 1] = row[srcIdx + 1]; // G
            data[dstIdx + 2] = row[srcIdx + 0]; // B
        }
    }
    setData(w, h, ch, data);

    // Extract embedded ICC profile.
    Metadata& meta = metadata();
    meta.iccData.clear();
    meta.iccProfileName.clear();
    meta.iccProfileType = -1;

    QByteArray iccData;
    if (IccProfileExtractor::extractFromFile(filePath, iccData)) {
        meta.iccData = iccData;
        core::ColorProfile profile(iccData);
        if (profile.isValid()) {
            meta.iccProfileName = profile.name();
            meta.iccProfileType = static_cast<int>(profile.type());
        }
    }

    if (debugInfo) {
        *debugInfo = QString("Loaded via OpenCV: %1x%2, %3ch, depth=%4")
                         .arg(w).arg(h).arg(ch).arg(img.depth());
    }

    return true;
}

bool ImageBuffer::loadXISF(const QString& filePath, QString* errorMsg) {
    return XISFReader::read(filePath, *this, errorMsg);
}

bool ImageBuffer::saveXISF(const QString& filePath, BitDepth depth,
                           QString* errorMsg) const {
    return XISFWriter::write(filePath, *this, static_cast<int>(depth), errorMsg);
}


// ============================================================================
// Display Pipeline - Statistics and Helpers
// ============================================================================

// LUT resolution for display mapping.
static const int LUT_SIZE = 65536;

/**
 * Safe clamp to [0, 1]. Handles NaN (comparisons return false) and Inf.
 */
static inline float safeClamp01(float v) {
    if (!(v >= 0.0f)) return 0.0f;
    if (v > 1.0f)     return 1.0f;
    return v;
}

/**
 * Per-channel statistics used for auto-stretch computation.
 */
struct ChStats {
    float median;
    float mad;
};

/**
 * Midtone Transfer Function (MTF).
 * Maps input x in [0,1] through a nonlinear curve controlled by midtone m.
 * Formula: y = (m - 1) * x / ((2m - 1) * x - m)
 */
static float mtf_func(float m, float x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    if (m <= 0) return 0;
    if (m >= 1) return x;
    return ((m - 1.0f) * x) / ((2.0f * m - 1.0f) * x - m);
}

/**
 * Compute high-precision channel statistics using exact float values.
 * Uses nth_element for O(N) median computation instead of histogram binning.
 * Suitable for 24-bit and 32-bit float data to avoid quantization artifacts.
 */
static ChStats computeStatsHighPrecision(const std::vector<float>& data,
                                         int width, int height,
                                         int channels, int channelIndex) {
    const float MAD_NORM = 1.4826f;

    long totalPixels = static_cast<long>(width) * height;
    if (totalPixels == 0) return {0.0f, 0.0f};

    // Adaptive subsampling: limit to ~4M samples for performance.
    int step = 1;
    if (totalPixels > 4000000) {
        step = static_cast<int>(
            std::sqrt(static_cast<double>(totalPixels) / 4000000.0));
        if (step < 1) step = 1;
    }

    // Collect samples from the specified channel.
    size_t estSize = (totalPixels / (step * step)) + 1000;
    std::vector<float> samples;
    samples.reserve(estSize);

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            size_t idx = (static_cast<size_t>(y) * width + x) *
                         channels + channelIndex;
            if (idx < data.size()) {
                float v = data[idx];
                if (v >= 0.0f && v <= 1.0f) {
                    samples.push_back(v);
                }
            }
        }
    }

    if (samples.empty()) return {0.0f, 0.0f};

    // Compute median via partial sort.
    size_t n   = samples.size();
    size_t mid = n / 2;
    std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
    float median = samples[mid];

    // Compute MAD by reusing the samples vector for absolute deviations.
    for (size_t i = 0; i < n; ++i) {
        samples[i] = std::fabs(samples[i] - median);
    }
    std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
    float mad = samples[mid] * MAD_NORM;

    return {median, mad};
}

/**
 * Compute channel statistics using either high-precision (float) or
 * histogram-binned method, depending on application settings.
 */
static ChStats computeStats(const std::vector<float>& data,
                             int width, int height,
                             int channels, int channelIndex) {
    // Check user preference for 24-bit precision mode.
    QSettings settings;
    if (settings.value("display/24bit_stf", true).toBool()) {
        return computeStatsHighPrecision(data, width, height,
                                         channels, channelIndex);
    }

    // Histogram-based statistics: O(N) with 16-bit quantization.
    const int   HIST_SIZE = 65536;
    const float MAD_NORM  = 1.4826f;

    std::vector<int> hist(HIST_SIZE, 0);

    long totalPixels = static_cast<long>(width) * height;
    if (totalPixels == 0) return {0.0f, 0.0f};

    // Adaptive subsampling for large images.
    int step = 1;
    if (totalPixels > 4000000) {
        step = static_cast<int>(
            std::sqrt(static_cast<double>(totalPixels) / 4000000.0));
        if (step < 1) step = 1;
    }

    long count = 0;

    // Build histogram from subsampled channel data.
    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            size_t idx = (static_cast<size_t>(y) * width + x) *
                         channels + channelIndex;
            if (idx < data.size()) {
                float v = std::max(0.0f, std::min(1.0f, data[idx]));
                int iVal = static_cast<int>(v * (HIST_SIZE - 1) + 0.5f);
                hist[iVal]++;
                count++;
            }
        }
    }

    if (count == 0) return {0.0f, 0.0f};

    // Find median bin.
    long medianIdx   = -1;
    long currentSum  = 0;
    long medianLevel = count / 2;

    for (int i = 0; i < HIST_SIZE; ++i) {
        currentSum += hist[i];
        if (currentSum >= medianLevel) {
            medianIdx = i;
            break;
        }
    }

    float median = (float)medianIdx / (HIST_SIZE - 1);

    // Compute MAD histogram (absolute deviations from the median bin).
    std::vector<int> madHist(HIST_SIZE, 0);
    for (int i = 0; i < HIST_SIZE; ++i) {
        if (hist[i] > 0) {
            int dev = std::abs(i - (int)medianIdx);
            madHist[dev] += hist[i];
        }
    }

    // Find median of the MAD histogram.
    currentSum = 0;
    long madIdx = -1;
    for (int i = 0; i < HIST_SIZE; ++i) {
        currentSum += madHist[i];
        if (currentSum >= medianLevel) {
            madIdx = i;
            break;
        }
    }

    float rawMad = (float)madIdx / (HIST_SIZE - 1);
    float mad    = rawMad * MAD_NORM;

    return {median, mad};
}


// ============================================================================
// Standard STF (Screen Transfer Function) Computation
// ============================================================================

/**
 * Standard MTF with safety guards for edge-case inputs.
 */
[[maybe_unused]]
static float standardMTF(float x, float m, float lo, float hi) {
    if (x <= lo)  return 0.f;
    if (x >= hi)  return 1.f;
    if (hi <= lo) return 0.5f;

    float xp = (x - lo) / (hi - lo);

    float denom = ((2.f * m - 1.f) * xp) - m;
    if (std::fabs(denom) < 1e-9f) return 0.5f;

    float result = ((m - 1.f) * xp) / denom;

    if (std::isnan(result) || std::isinf(result)) return 0.5f;
    return std::clamp(result, 0.f, 1.f);
}

/**
 * Parameters for a standard screen transfer function.
 */
struct StandardSTFParams {
    float shadows;    // Black point (low clip)
    float midtones;   // Midtone balance (0-1)
    float highlights; // White point (high clip)
};

/**
 * Compute auto-stretch STF parameters for a single channel.
 * Uses median and MAD to derive shadow clipping and midtone mapping.
 */
[[maybe_unused]]
static StandardSTFParams computeStandardSTF(const std::vector<float>& data,
                                            [[maybe_unused]] int w,
                                            [[maybe_unused]] int h,
                                            int ch, int channelIdx) {
    const float AS_DEFAULT_SHADOWS_CLIPPING   = -2.80f;
    const float AS_DEFAULT_TARGET_BACKGROUND  =  0.25f;

    StandardSTFParams result;
    result.highlights = 1.0f;
    result.shadows    = 0.0f;
    result.midtones   = 0.25f;

    if (data.empty() || ch <= 0) return result;

    // Extract single-channel data.
    std::vector<float> chData;
    chData.reserve(data.size() / ch);
    for (size_t i = channelIdx; i < data.size(); i += ch) {
        chData.push_back(data[i]);
    }
    if (chData.size() < 2) return result;

    // Compute median.
    std::vector<float> sorted = chData;
    std::sort(sorted.begin(), sorted.end());
    float median = sorted[sorted.size() / 2];

    // Handle near-zero median (common in star masks or dark frames).
    if (median < 1e-6f) {
        double sum = 0;
        for (float v : chData) sum += v;
        median = (float)(sum / chData.size());
        if (median < 1e-6f) median = 0.0001f;
    }

    // Compute MAD.
    std::vector<float> deviations(sorted.size());
    for (size_t i = 0; i < sorted.size(); ++i) {
        deviations[i] = std::fabs(sorted[i] - median);
    }
    std::sort(deviations.begin(), deviations.end());
    float mad = deviations[deviations.size() / 2];
    if (mad < 1e-9f) mad = 0.001f;

    float sigma = 1.4826f * mad;

    // Shadow clipping: median + clipping_factor * sigma (factor is negative).
    float c0 = median + AS_DEFAULT_SHADOWS_CLIPPING * sigma;
    if (c0 < 0.f) c0 = 0.f;

    // Distance from shadow point to median.
    float m2 = median - c0;

    result.shadows    = c0;
    result.highlights = 1.0f;

    // Derive midtone balance from the target background level.
    float target = AS_DEFAULT_TARGET_BACKGROUND;
    if (m2 <= 1e-9f || m2 >= 1.f) {
        result.midtones = 0.001f;
    } else {
        float xp    = m2;
        float denom = ((2.f * target - 1.f) * xp) - target;
        if (std::fabs(denom) < 1e-9f) {
            result.midtones = 0.25f;
        } else {
            result.midtones = ((target - 1.f) * xp) / denom;
        }

        if (std::isnan(result.midtones) || std::isinf(result.midtones)) {
            result.midtones = 0.25f;
        } else {
            result.midtones = std::clamp(result.midtones, 0.00001f, 0.99999f);
        }
    }

    return result;
}


// ============================================================================
// Auto-Stretch Parameter Computation
// ============================================================================

ImageBuffer::STFParams ImageBuffer::computeAutoStretchParams(
        bool linked, float targetMedian,
        const std::vector<bool>& activeChannels) const {

    ReadLock lock(this);

    STFParams result;
    if (m_data.empty()) return result;

    const float shadowClip = -2.8f;

    // Compute per-channel statistics.
    std::vector<ChStats> stats(m_channels);
    for (int c = 0; c < m_channels; ++c) {
        stats[c] = computeStats(m_data, m_width, m_height, m_channels, c);
    }

    // Filter channels based on the active mask, excluding empty channels.
    std::vector<ChStats> activeStats;
    for (int c = 0; c < m_channels; ++c) {
        if (c < (int)activeChannels.size() && activeChannels[c]) {
            if (stats[c].median > 1e-9f || stats[c].mad > 1e-9f) {
                activeStats.push_back(stats[c]);
            }
        }
    }

    // Fallback: if no channels were explicitly active, use all non-empty ones.
    if (activeStats.empty()) {
        for (int c = 0; c < m_channels; ++c) {
            if (stats[c].median > 1e-9f || stats[c].mad > 1e-9f) {
                activeStats.push_back(stats[c]);
            }
        }
    }

    // Final fallback: use all channels regardless.
    if (activeStats.empty()) {
        for (int c = 0; c < m_channels; ++c) {
            activeStats.push_back(stats[c]);
        }
    }

    if (linked) {
        // Linked mode: compute a single set of parameters from averaged stats.
        float avgMed = 0.0f, avgMad = 0.0f;
        for (const auto& s : activeStats) {
            avgMed += s.median;
            avgMad += s.mad;
        }
        avgMed /= activeStats.size();
        avgMad /= activeStats.size();

        float shadow = std::max(0.0f, avgMed + shadowClip * avgMad);
        if (shadow >= avgMed) {
            shadow = std::max(0.0f, avgMed - 0.001f);
        }
        float mid = avgMed - shadow;
        if (mid <= 0.0f) mid = 0.5f;

        result.shadows    = shadow;
        result.midtones   = mtf_func(targetMedian, mid);
        result.highlights = 1.0f;
    } else {
        // Independent mode: average per-channel parameters.
        for (const auto& s : activeStats) {
            float shadow = std::max(0.0f, s.median + shadowClip * s.mad);
            if (shadow >= s.median) {
                shadow = std::max(0.0f, s.median - 0.001f);
            }
            float mid = s.median - shadow;
            if (mid <= 0.0f) mid = 0.5f;

            result.shadows  += shadow;
            result.midtones += mtf_func(targetMedian, mid);
        }
        result.shadows  /= activeStats.size();
        result.midtones /= activeStats.size();
        result.highlights = 1.0f;
    }

    return result;
}


// ============================================================================
// Display Image Generation
// ============================================================================

QImage ImageBuffer::getDisplayImage(
        DisplayMode mode, bool linked,
        const std::vector<std::vector<float>>* overrideLUT,
        int maxWidth, int maxHeight,
        bool showMask, bool inverted, bool falseColor,
        float autoStretchTargetMedian,
        ChannelView channelView) const {

    ReadLock lock(this);
    if (m_data.empty()) return QImage();

    // --- Channel view helper ---
    // For R/G/B single-channel views, render the selected channel as grayscale.
    auto applyChannelView = [&](QImage& img) {
        if (channelView == ChannelRGB || m_channels != 3) return;
        if (img.format() != QImage::Format_RGB888) return;

        int ch = 0;
        if      (channelView == ChannelG) ch = 1;
        else if (channelView == ChannelB) ch = 2;

        for (int y = 0; y < img.height(); ++y) {
            uchar* p = img.scanLine(y);
            for (int x = 0; x < img.width(); ++x, p += 3) {
                uchar v = p[ch];
                p[0] = v; p[1] = v; p[2] = v;
            }
        }
    };

    // Check for high-definition (24-bit) stretch mode.
    QSettings settings;
    bool use24Bit = settings.value("display/24bit_stf", true).toBool();

    // -----------------------------------------------------------------------
    // Direct 24-bit / High Definition Mode
    // Bypasses LUT for direct float computation to eliminate banding.
    // Active only for AutoStretch when no override LUT is present.
    // -----------------------------------------------------------------------
    if (use24Bit && mode == Display_AutoStretch &&
        (!overrideLUT || overrideLUT->empty())) {

        // Compute output dimensions with uniform downsampling.
        int outW = m_width, outH = m_height;
        int stepX = 1, stepY = 1;

        if (maxWidth  > 0 && m_width  > maxWidth)  stepX = m_width  / maxWidth;
        if (maxHeight > 0 && m_height > maxHeight) stepY = m_height / maxHeight;

        int step = std::max(stepX, stepY);
        if (step < 1) step = 1;
        stepX = stepY = step;

        outW = m_width  / stepX;
        outH = m_height / stepY;

        QImage::Format fmt = (m_channels == 1 && !falseColor)
                                 ? QImage::Format_Grayscale8
                                 : QImage::Format_RGB888;
        QImage result(outW, outH, fmt);

        // Compute per-channel MTF stretch parameters.
        struct MtfParams { float shadow; float midtone; float norm; };
        std::vector<MtfParams> params(std::max(3, m_channels));

        std::vector<ChStats> stats(m_channels);
        for (int c = 0; c < m_channels; ++c) {
            stats[c] = computeStats(m_data, m_width, m_height, m_channels, c);
        }

        const float targetBG   = autoStretchTargetMedian;
        const float shadowClip = -2.8f;

        if (linked && m_channels == 3) {
            float avgMed = 0.0f, avgMad = 0.0f;
            int activeChannels = 0;
            for (int c = 0; c < 3; ++c) {
                if (stats[c].median > 1e-9f || stats[c].mad > 1e-9f) {
                    avgMed += stats[c].median;
                    avgMad += stats[c].mad;
                    activeChannels++;
                }
            }
            if (activeChannels > 0) {
                avgMed /= activeChannels;
                avgMad /= activeChannels;
            } else {
                avgMed = (stats[0].median + stats[1].median +
                          stats[2].median) / 3.0f;
                avgMad = (stats[0].mad + stats[1].mad +
                          stats[2].mad) / 3.0f;
            }

            float shadow = std::max(0.0f, avgMed + shadowClip * avgMad);
            if (shadow >= avgMed) {
                shadow = std::max(0.0f, avgMed - 0.001f);
            }
            float mid = avgMed - shadow;
            if (mid <= 0) mid = 0.5f;

            float m    = mtf_func(targetBG, mid);
            float norm = 1.0f / (1.0f - shadow + 1e-9f);

            for (int c = 0; c < 3; ++c) {
                params[c] = {shadow, m, norm};
            }
        } else {
            for (int c = 0; c < m_channels; ++c) {
                float shadow = std::max(0.0f,
                    stats[c].median + shadowClip * stats[c].mad);
                if (shadow >= stats[c].median) {
                    shadow = std::max(0.0f, stats[c].median - 0.001f);
                }
                float mid = stats[c].median - shadow;
                if (mid <= 0) mid = 0.5f;

                float m    = mtf_func(targetBG, mid);
                float norm = 1.0f / (1.0f - shadow + 1e-9f);
                params[c]  = {shadow, m, norm};
            }
        }

        // Prepare SIMD parameters for the optimized row processing path.
        SimdOps::STFParams stf;
        for (int k = 0; k < 3; ++k) {
            stf.shadow[k]   = params[k].shadow;
            stf.midtones[k] = params[k].midtone;
            stf.invRange[k] = params[k].norm;
        }

        // Render the display image row by row.
        #pragma omp parallel for
        for (int y = 0; y < outH; ++y) {
            uchar* dest = result.scanLine(y);
            int srcY = y * stepY;
            if (srcY >= m_height) srcY = m_height - 1;

            // Optimized SIMD path for full-resolution, packed 3-channel data.
            if (stepX == 1 && m_channels == 3 && !falseColor) {
                const float* srcRow = m_data.data() +
                                      (size_t)srcY * m_width * 3;
                SimdOps::applySTF_Row(srcRow, dest, outW, stf, inverted);

                // Overlay mask visualization (red tint).
                if (m_hasMask && showMask) {
                    const float* maskRow = m_mask.data.data() +
                                           (size_t)srcY * m_mask.width;
                    for (int x = 0; x < outW; ++x) {
                        float maskAlpha = maskRow[x];
                        if (m_mask.inverted)          maskAlpha = 1.0f - maskAlpha;
                        if (m_mask.mode == "protect") maskAlpha = 1.0f - maskAlpha;
                        maskAlpha *= m_mask.opacity;

                        if (maskAlpha > 0) {
                            int r = dest[x * 3 + 0];
                            int g = dest[x * 3 + 1];
                            int b = dest[x * 3 + 2];

                            r = r * (1.0f - maskAlpha) + 255 * maskAlpha;
                            g = g * (1.0f - maskAlpha);
                            b = b * (1.0f - maskAlpha);

                            dest[x * 3 + 0] = std::min(255, r);
                            dest[x * 3 + 1] = std::min(255, g);
                            dest[x * 3 + 2] = std::min(255, b);
                        }
                    }
                }
                continue;
            }

            // Scalar fallback for subsampled, masked, or single-channel modes.
            const float* srcPtr    = m_data.data();
            size_t srcIdxBase      = (size_t)srcY * m_width * m_channels;
            const float* maskRow   = nullptr;

            if (m_hasMask && showMask && srcY < m_mask.height) {
                maskRow = m_mask.data.data() + (size_t)srcY * m_mask.width;
            }

            for (int x = 0; x < outW; ++x) {
                int srcX = x * stepX;
                if (srcX >= m_width) srcX = m_width - 1;

                size_t srcIdx = srcIdxBase + srcX * m_channels;
                float r_out, g_out, b_out;

                // Per-channel stretch with sanitization.
                float v[3];
                for (int c = 0; c < 3; ++c) {
                    if (c < m_channels) v[c] = srcPtr[srcIdx + c];
                    else                v[c] = 0;

                    v[c] = safeClamp01(v[c]);
                    v[c] = (v[c] - params[c].shadow) * params[c].norm;
                    v[c] = mtf_func(params[c].midtone, safeClamp01(v[c]));
                    if (inverted) v[c] = 1.0f - v[c];
                    v[c] = safeClamp01(v[c]);
                }

                r_out = v[0]; g_out = v[1]; b_out = v[2];

                // False color heatmap rendering.
                if (falseColor) {
                    float intensity = (m_channels == 3)
                        ? (0.2989f * r_out + 0.5870f * g_out + 0.1140f * b_out)
                        : r_out;
                    intensity = std::clamp(intensity, 0.0f, 1.0f);

                    auto hsvToRgb = [](float h, float s, float v)
                            -> std::tuple<float, float, float> {
                        if (s <= 0.0f) return {v, v, v};
                        float hh = (h < 360.0f) ? h : 0.0f;
                        hh /= 60.0f;
                        int   i  = static_cast<int>(hh);
                        float ff = hh - i;
                        float p  = v * (1.0f - s);
                        float q  = v * (1.0f - (s * ff));
                        float t  = v * (1.0f - (s * (1.0f - ff)));
                        switch (i % 6) {
                            case 0: return {v, t, p};
                            case 1: return {q, v, p};
                            case 2: return {p, v, t};
                            case 3: return {p, q, v};
                            case 4: return {t, p, v};
                            default: return {v, p, q};
                        }
                    };

                    float hue = (1.0f - intensity) * 300.0f;
                    auto [rf, gf, bf] = hsvToRgb(hue, 1.0f, 1.0f);
                    r_out = rf; g_out = gf; b_out = bf;
                }

                // Mask overlay.
                if (m_hasMask && showMask) {
                    int   maskX     = x * stepX;
                    float maskAlpha = maskRow ? maskRow[maskX] : 0.0f;
                    if (m_mask.inverted)          maskAlpha = 1.0f - maskAlpha;
                    if (m_mask.mode == "protect") maskAlpha = 1.0f - maskAlpha;
                    maskAlpha *= m_mask.opacity;

                    r_out = r_out * (1.0f - maskAlpha) + 1.0f * maskAlpha;
                    g_out = g_out * (1.0f - maskAlpha);
                    b_out = b_out * (1.0f - maskAlpha);
                }

                // Write output pixel.
                if (fmt == QImage::Format_Grayscale8) {
                    dest[x] = static_cast<uchar>(r_out * 255.0f + 0.5f);
                } else {
                    dest[x * 3 + 0] = static_cast<uchar>(r_out * 255.0f + 0.5f);
                    dest[x * 3 + 1] = static_cast<uchar>(g_out * 255.0f + 0.5f);
                    dest[x * 3 + 2] = static_cast<uchar>(b_out * 255.0f + 0.5f);
                }
            }
        }

        applyChannelView(result);
        return result;
    }

    // -----------------------------------------------------------------------
    // Standard LUT-based Display Path
    // -----------------------------------------------------------------------

    // Generate per-channel LUTs.
    std::vector<std::vector<float>> luts(
        std::max(3, m_channels), std::vector<float>(LUT_SIZE));

    if (overrideLUT && overrideLUT->size() == 3 &&
        !overrideLUT->at(0).empty()) {
        luts = *overrideLUT;

    } else if (mode == Display_AutoStretch) {
        std::vector<ChStats> stats(m_channels);
        for (int c = 0; c < m_channels; ++c) {
            stats[c] = computeStats(m_data, m_width, m_height, m_channels, c);
        }

        const float targetBG   = autoStretchTargetMedian;
        const float shadowClip = -2.8f;

        if (linked && m_channels == 3) {
            float avgMed = 0.0f, avgMad = 0.0f;
            int activeChannels = 0;
            for (int c = 0; c < 3; ++c) {
                if (stats[c].median > 1e-9f || stats[c].mad > 1e-9f) {
                    avgMed += stats[c].median;
                    avgMad += stats[c].mad;
                    activeChannels++;
                }
            }
            if (activeChannels > 0) {
                avgMed /= activeChannels;
                avgMad /= activeChannels;
            } else {
                avgMed = (stats[0].median + stats[1].median +
                          stats[2].median) / 3.0f;
                avgMad = (stats[0].mad + stats[1].mad +
                          stats[2].mad) / 3.0f;
            }

            float shadow = std::max(0.0f, avgMed + shadowClip * avgMad);
            if (shadow >= avgMed) {
                shadow = std::max(0.0f, avgMed - 0.001f);
            }
            float mid = avgMed - shadow;
            if (mid <= 0) mid = 0.5f;

            float m = mtf_func(targetBG, mid);
            for (int c = 0; c < 3; ++c) {
                for (int i = 0; i < LUT_SIZE; ++i) {
                    float x     = (float)i / (LUT_SIZE - 1);
                    float normX = (x - shadow) / (1.0f - shadow + 1e-9f);
                    luts[c][i]  = mtf_func(m, normX);
                }
            }
        } else {
            for (int c = 0; c < m_channels; ++c) {
                float shadow = std::max(0.0f,
                    stats[c].median + shadowClip * stats[c].mad);
                if (shadow >= stats[c].median) {
                    shadow = std::max(0.0f, stats[c].median - 0.001f);
                }
                float mid = stats[c].median - shadow;
                if (mid <= 0) mid = 0.5f;

                float m = mtf_func(targetBG, mid);
                for (int i = 0; i < LUT_SIZE; ++i) {
                    float x     = (float)i / (LUT_SIZE - 1);
                    float normX = (x - shadow) / (1.0f - shadow + 1e-9f);
                    luts[c][i]  = mtf_func(m, normX);
                }
            }
        }

    } else if (mode == Display_ArcSinh) {
        float strength = 100.0f;
        for (int c = 0; c < m_channels; ++c) {
            for (int i = 0; i < LUT_SIZE; ++i) {
                luts[c][i] = std::asinh((float)i / (LUT_SIZE - 1) * strength) /
                             std::asinh(strength);
            }
        }

    } else if (mode == Display_Sqrt) {
        for (int c = 0; c < m_channels; ++c) {
            for (int i = 0; i < LUT_SIZE; ++i) {
                luts[c][i] = std::sqrt((float)i / (LUT_SIZE - 1));
            }
        }

    } else if (mode == Display_Log) {
        const float scale   = 65535.0f / 10.0f;
        const float norm    = 1.0f / std::log(scale);
        const float min_val = 10.0f / 65535.0f;

        for (int c = 0; c < m_channels; ++c) {
            for (int i = 0; i < LUT_SIZE; ++i) {
                float x = (float)i / (LUT_SIZE - 1);
                if (x < min_val) {
                    luts[c][i] = 0.0f;
                } else {
                    luts[c][i] = std::log(x * scale) * norm;
                }
            }
        }

    } else if (mode == Display_Histogram) {
        // Histogram equalization.
        std::vector<std::vector<int>> hists(
            m_channels, std::vector<int>(LUT_SIZE, 0));

        // Subsample for performance on large images.
        int  skip  = 1;
        long total = m_data.size();
        if (total > 2000000) skip = 4;

        for (long i = 0; i < total; i += skip * m_channels) {
            for (int c = 0; c < m_channels; ++c) {
                float v   = m_data[i + c];
                int   idx = std::clamp((int)(v * (LUT_SIZE - 1)),
                                       0, LUT_SIZE - 1);
                hists[c][idx]++;
            }
        }

        // Build CDF-based equalization LUT per channel.
        for (int c = 0; c < m_channels; ++c) {
            long cdf    = 0;
            long minCdf = 0;
            long N      = 0;
            for (int count : hists[c]) N += count;

            // Find first non-zero bin for proper CDF scaling.
            for (int i = 0; i < LUT_SIZE; ++i) {
                if (hists[c][i] > 0) {
                    minCdf = hists[c][i];
                    break;
                }
            }

            for (int i = 0; i < LUT_SIZE; ++i) {
                cdf += hists[c][i];
                float val = 0.0f;
                if (N > minCdf) {
                    val = (float)(cdf - minCdf) / (float)(N - minCdf);
                } else {
                    val = (float)i / (LUT_SIZE - 1);
                }
                luts[c][i] = std::clamp(val, 0.0f, 1.0f);
            }
        }

    } else {
        // Linear (identity) LUT.
        for (int c = 0; c < m_channels; ++c) {
            for (int i = 0; i < LUT_SIZE; ++i) {
                luts[c][i] = (float)i / (LUT_SIZE - 1);
            }
        }
    }

    // Compute output dimensions with aspect-preserving downsampling.
    int outW  = m_width;
    int outH  = m_height;
    int stepX = 1, stepY = 1;

    if (maxWidth  > 0 && m_width  > maxWidth)  stepX = m_width  / maxWidth;
    if (maxHeight > 0 && m_height > maxHeight) stepY = m_height / maxHeight;

    int step = std::max(stepX, stepY);
    if (step < 1) step = 1;
    stepX = stepY = step;

    outW = m_width  / stepX;
    outH = m_height / stepY;

    QImage::Format fmt = (m_channels == 1 && !falseColor)
                             ? QImage::Format_Grayscale8
                             : QImage::Format_RGB888;
    QImage result(outW, outH, fmt);

    // HSV-to-RGB helper for false color rendering.
    auto hsvToRgb = [](float h, float s, float v,
                       uchar& r, uchar& g, uchar& b) {
        if (s <= 0.0f) {
            r = g = b = static_cast<uchar>(v * 255.0f);
            return;
        }
        float hh = h;
        if (hh >= 360.0f) hh = 0.0f;
        hh /= 60.0f;
        int   i  = static_cast<int>(hh);
        float ff = hh - i;
        float p  = v * (1.0f - s);
        float q  = v * (1.0f - (s * ff));
        float t  = v * (1.0f - (s * (1.0f - ff)));

        float rr, gg, bb;
        switch (i) {
            case 0: rr = v; gg = t; bb = p; break;
            case 1: rr = q; gg = v; bb = p; break;
            case 2: rr = p; gg = v; bb = t; break;
            case 3: rr = p; gg = q; bb = v; break;
            case 4: rr = t; gg = p; bb = v; break;
            default: rr = v; gg = p; bb = q; break;
        }
        r = static_cast<uchar>(rr * 255.0f);
        g = static_cast<uchar>(gg * 255.0f);
        b = static_cast<uchar>(bb * 255.0f);
    };

    // Render scanlines with LUT mapping.
    #pragma omp parallel for
    for (int y = 0; y < outH; ++y) {
        uchar* dest = result.scanLine(y);
        int srcY = y * stepY;
        if (srcY >= m_height) srcY = m_height - 1;

        size_t srcIdxBase = (size_t)srcY * m_width * m_channels;

        // Precompute mask row pointer for this scanline.
        const float* maskRow = nullptr;
        if (m_hasMask && (overrideLUT != nullptr || showMask)) {
            int maskY = y * stepY;
            if (maskY < m_mask.height) {
                maskRow = m_mask.data.data() + (size_t)maskY * m_mask.width;
            }
        }

        for (int x = 0; x < outW; ++x) {
            int srcX = x * stepX;
            if (srcX >= m_width) srcX = m_width - 1;

            float r_out, g_out, b_out;

            // Compute mask alpha for preview blending.
            float maskAlpha     = 0.0f;
            bool  applyMaskBlend = (m_hasMask && overrideLUT != nullptr);
            if (applyMaskBlend) {
                int maskX = x * stepX;
                maskAlpha = maskRow ? maskRow[maskX] : 0.0f;
                if (m_mask.inverted)          maskAlpha = 1.0f - maskAlpha;
                if (m_mask.mode == "protect") maskAlpha = 1.0f - maskAlpha;
                maskAlpha *= m_mask.opacity;
            }

            if (m_channels == 1) {
                size_t idx = srcIdxBase + srcX;
                if (idx >= m_data.size()) continue;

                float v   = safeClamp01(m_data[idx]);
                int   iVal = static_cast<int>(v * (LUT_SIZE - 1));
                float out = luts[0][iVal];

                if (applyMaskBlend) {
                    out = out * maskAlpha + v * (1.0f - maskAlpha);
                }
                if (inverted) out = 1.0f - out;

                r_out = g_out = b_out = out;
            } else {
                size_t base = srcIdxBase + (size_t)srcX * m_channels;
                if (base + 2 >= m_data.size()) continue;

                float r = safeClamp01(m_data[base + 0]);
                float g = safeClamp01(m_data[base + 1]);
                float b = safeClamp01(m_data[base + 2]);

                int ir = static_cast<int>(r * (LUT_SIZE - 1));
                int ig = static_cast<int>(g * (LUT_SIZE - 1));
                int ib = static_cast<int>(b * (LUT_SIZE - 1));

                r_out = luts[0][ir];
                g_out = luts[1][ig];
                b_out = luts[2][ib];

                if (applyMaskBlend) {
                    r_out = r_out * maskAlpha + r * (1.0f - maskAlpha);
                    g_out = g_out * maskAlpha + g * (1.0f - maskAlpha);
                    b_out = b_out * maskAlpha + b * (1.0f - maskAlpha);
                }
                if (inverted) {
                    r_out = 1.0f - r_out;
                    g_out = 1.0f - g_out;
                    b_out = 1.0f - b_out;
                }
            }

            // False color heatmap.
            if (falseColor) {
                float intensity = (m_channels == 3)
                    ? (0.2989f * r_out + 0.5870f * g_out + 0.1140f * b_out)
                    : r_out;
                intensity = std::clamp(intensity, 0.0f, 1.0f);

                float hue = (1.0f - intensity) * 300.0f;
                uchar r8, g8, b8;
                hsvToRgb(hue, 1.0f, 1.0f, r8, g8, b8);

                dest[x * 3 + 0] = r8;
                dest[x * 3 + 1] = g8;
                dest[x * 3 + 2] = b8;
            } else {
                if (m_channels == 1) {
                    dest[x] = static_cast<uchar>(
                        std::clamp(r_out, 0.0f, 1.0f) * 255.0f);
                } else {
                    dest[x * 3 + 0] = static_cast<uchar>(
                        std::clamp(r_out, 0.0f, 1.0f) * 255.0f);
                    dest[x * 3 + 1] = static_cast<uchar>(
                        std::clamp(g_out, 0.0f, 1.0f) * 255.0f);
                    dest[x * 3 + 2] = static_cast<uchar>(
                        std::clamp(b_out, 0.0f, 1.0f) * 255.0f);
                }
            }

            // Mask overlay visualization (red tint).
            if (showMask && m_hasMask) {
                int   maskX = x * stepX;
                float mVal  = maskRow ? maskRow[maskX] : 0.0f;
                if (m_mask.inverted) mVal = 1.0f - mVal;

                if (mVal > 0) {
                    float overlayAlpha = 0.5f * mVal;

                    int r = (fmt == QImage::Format_Grayscale8)
                                ? dest[x] : dest[x * 3 + 0];
                    int g = (fmt == QImage::Format_Grayscale8)
                                ? dest[x] : dest[x * 3 + 1];
                    int b = (fmt == QImage::Format_Grayscale8)
                                ? dest[x] : dest[x * 3 + 2];

                    if (fmt == QImage::Format_Grayscale8) {
                        int val = dest[x];
                        dest[x] = std::clamp(
                            static_cast<int>(val * (1.0f + overlayAlpha)),
                            0, 255);
                    } else {
                        float r_f = r * (1.0f - overlayAlpha) +
                                    255.0f * overlayAlpha;
                        float g_f = g * (1.0f - overlayAlpha);
                        float b_f = b * (1.0f - overlayAlpha);

                        dest[x * 3 + 0] = static_cast<uchar>(
                            std::clamp(r_f, 0.0f, 255.0f));
                        dest[x * 3 + 1] = static_cast<uchar>(
                            std::clamp(g_f, 0.0f, 255.0f));
                        dest[x * 3 + 2] = static_cast<uchar>(
                            std::clamp(b_f, 0.0f, 255.0f));
                    }
                }
            }
        }
    }

    applyChannelView(result);
    return result;
}


// ============================================================================
// Destructive Display Transform (applies stretch permanently to pixel data)
// ============================================================================

void ImageBuffer::applyDisplayTransform(DisplayMode mode, bool linked,
                                        float targetMedian,
                                        bool inverted, bool falseColor) {
    if (m_data.empty()) return;

    struct ChannelTransform {
        float shadow = 0.0f;
        float m      = 0.5f;
        float scale  = 1.0f;
        float offset = 0.0f;
        bool  active = true;
    };
    std::vector<ChannelTransform> txs(m_channels);

    if (mode == Display_AutoStretch) {
        std::vector<ChStats> stats(m_channels);
        for (int c = 0; c < m_channels; ++c) {
            stats[c] = computeStats(m_data, m_width, m_height, m_channels, c);
        }

        const float targetBG   = targetMedian;
        const float shadowClip = -2.8f;

        if (linked && m_channels == 3) {
            float avgMed = (stats[0].median + stats[1].median +
                            stats[2].median) / 3.0f;
            float avgMad = (stats[0].mad + stats[1].mad +
                            stats[2].mad) / 3.0f;
            float shadow = std::max(0.0f, avgMed + shadowClip * avgMad);
            if (shadow >= avgMed) {
                shadow = std::max(0.0f, avgMed - 0.001f);
            }
            float mid = avgMed - shadow;
            if (mid <= 0) mid = 0.5f;

            float m = mtf_func(targetBG, mid);
            for (int c = 0; c < 3; ++c) {
                txs[c].shadow = shadow;
                txs[c].m      = m;
            }
        } else {
            for (int c = 0; c < m_channels; ++c) {
                float shadow = std::max(0.0f,
                    stats[c].median + shadowClip * stats[c].mad);
                if (shadow >= stats[c].median) {
                    shadow = std::max(0.0f, stats[c].median - 0.001f);
                }
                float mid = stats[c].median - shadow;
                if (mid <= 0) mid = 0.5f;
                txs[c].shadow = shadow;
                txs[c].m      = mtf_func(targetBG, mid);
            }
        }

    } else if (mode == Display_ArcSinh) {
        const float strength = 100.0f;
        const float norm     = 1.0f / std::asinh(strength);
        for (int c = 0; c < m_channels; ++c) {
            txs[c].scale = strength;
            txs[c].m     = norm;
        }

    } else if (mode == Display_Log) {
        const float scale   = 65535.0f / 10.0f;
        const float norm    = 1.0f / std::log(scale);
        const float min_val = 10.0f / 65535.0f;
        for (int c = 0; c < m_channels; ++c) {
            txs[c].scale  = scale;
            txs[c].m      = norm;
            txs[c].offset = min_val;
        }
    }

    size_t totalPixels = static_cast<size_t>(m_width) * m_height;
    int    chCount     = m_channels;

    #pragma omp parallel for
    for (size_t i = 0; i < totalPixels; ++i) {
        float r_val = 0, g_val = 0, b_val = 0;

        for (int c = 0; c < chCount; ++c) {
            float& v   = m_data[i * chCount + c];
            float  cin  = safeClamp01(v);
            float  cout = cin;

            if (mode == Display_AutoStretch) {
                float normX = (cin - txs[c].shadow) /
                              (1.0f - txs[c].shadow + 1e-9f);
                cout = mtf_func(txs[c].m, normX);
            } else if (mode == Display_ArcSinh) {
                cout = std::asinh(cin * txs[c].scale) * txs[c].m;
            } else if (mode == Display_Sqrt) {
                cout = std::sqrt(cin);
            } else if (mode == Display_Log) {
                cout = (cin < txs[c].offset)
                           ? 0.0f
                           : std::log(cin * txs[c].scale) * txs[c].m;
            }

            if (inverted) cout = 1.0f - cout;
            v = cout;

            if      (c == 0) r_val = cout;
            else if (c == 1) g_val = cout;
            else if (c == 2) b_val = cout;
        }

        // Apply false color heatmap if enabled.
        if (falseColor && chCount >= 1) {
            if (chCount == 1) {
                r_val = g_val = b_val = m_data[i];
            }

            float intensity = (chCount == 3)
                ? (0.2989f * r_val + 0.5870f * g_val + 0.1140f * b_val)
                : r_val;
            intensity = std::clamp(intensity, 0.0f, 1.0f);

            float h  = (1.0f - intensity) * 300.0f;
            float hh = h / 60.0f;
            int   ii = static_cast<int>(hh);
            float ff = hh - ii;
            float q  = 1.0f - ff;
            float t  = ff;

            float rr = 0, gg = 0, bb = 0;
            switch (ii) {
                case 0: rr = 1.0f; gg = t;    bb = 0.0f; break;
                case 1: rr = q;    gg = 1.0f; bb = 0.0f; break;
                case 2: rr = 0.0f; gg = 1.0f; bb = t;    break;
                case 3: rr = 0.0f; gg = q;    bb = 1.0f; break;
                case 4: rr = t;    gg = 0.0f; bb = 1.0f; break;
                default: rr = 1.0f; gg = 0.0f; bb = q;   break;
            }

            if (chCount >= 3) {
                m_data[i * chCount + 0] = rr;
                m_data[i * chCount + 1] = gg;
                m_data[i * chCount + 2] = bb;
            }
        }
    }

    m_modified = true;
}


// ============================================================================
// ICC Profile Helpers (static, internal)
// ============================================================================

/**
 * Create an lcms2 ICC profile handle for a standard color space.
 */
static cmsHPROFILE createStandardIccProfile(core::StandardProfile type) {
    switch (type) {
        case core::StandardProfile::sRGB:
            return cmsCreate_sRGBProfile();

        case core::StandardProfile::AdobeRGB: {
            cmsCIExyY      wp       = {0.3127, 0.3290, 1.0};
            cmsCIExyYTRIPLE primaries = {
                {0.6400, 0.3300, 1.0},
                {0.2100, 0.7100, 1.0},
                {0.1500, 0.0600, 1.0}
            };
            cmsToneCurve* gamma    = cmsBuildGamma(nullptr, 2.2);
            cmsToneCurve* curves[3] = {gamma, gamma, gamma};
            cmsHPROFILE   h        = cmsCreateRGBProfile(&wp, &primaries, curves);
            cmsFreeToneCurve(gamma);
            return h;
        }

        case core::StandardProfile::ProPhotoRGB: {
            cmsCIExyY      wp       = {0.3457, 0.3585, 1.0};
            cmsCIExyYTRIPLE primaries = {
                {0.7347, 0.2653, 1.0},
                {0.1596, 0.8404, 1.0},
                {0.0366, 0.0001, 1.0}
            };
            cmsToneCurve* gamma    = cmsBuildGamma(nullptr, 1.8);
            cmsToneCurve* curves[3] = {gamma, gamma, gamma};
            cmsHPROFILE   h        = cmsCreateRGBProfile(&wp, &primaries, curves);
            cmsFreeToneCurve(gamma);
            return h;
        }

        case core::StandardProfile::LinearRGB: {
            cmsCIExyY      wp       = {0.3127, 0.3290, 1.0};
            cmsCIExyYTRIPLE primaries = {
                {0.6400, 0.3300, 1.0},
                {0.3000, 0.6000, 1.0},
                {0.1500, 0.0600, 1.0}
            };
            cmsToneCurve* gamma    = cmsBuildGamma(nullptr, 1.0);
            cmsToneCurve* curves[3] = {gamma, gamma, gamma};
            cmsHPROFILE   h        = cmsCreateRGBProfile(&wp, &primaries, curves);
            cmsFreeToneCurve(gamma);
            return h;
        }

        default:
            return nullptr;
    }
}

/**
 * Serialize a standard ICC profile to raw bytes.
 */
static QByteArray buildStandardIccBytes(core::StandardProfile type) {
    QByteArray out;
    cmsHPROFILE h = createStandardIccProfile(type);
    if (!h) return out;

    cmsUInt32Number size = 0;
    if (cmsSaveProfileToMem(h, nullptr, &size) && size > 0) {
        out.resize(static_cast<int>(size));
        if (!cmsSaveProfileToMem(h, out.data(), &size)) {
            out.clear();
        }
    }
    cmsCloseProfile(h);
    return out;
}

/**
 * Resolve the effective ICC profile data for this image, preferring
 * embedded raw data over standard profile type identifiers.
 */
static QByteArray effectiveIccProfileData(
        const ImageBuffer::Metadata& meta) {
    if (!meta.iccData.isEmpty()) {
        return meta.iccData;
    }
    if (meta.iccProfileType >= 0 &&
        meta.iccProfileType <= static_cast<int>(core::StandardProfile::LinearRGB)) {
        return buildStandardIccBytes(
            static_cast<core::StandardProfile>(meta.iccProfileType));
    }
    return QByteArray();
}

/**
 * Apply an ICC color space to a QImage for display-accurate rendering.
 */
static void applyIccToQImage(QImage& img, const QByteArray& iccData) {
    if (iccData.isEmpty()) return;

    const QColorSpace cs = QColorSpace::fromIccProfile(iccData);
    if (cs.isValid()) {
        img.setColorSpace(cs);
    }
}


// ============================================================================
// File Saving
// ============================================================================

bool ImageBuffer::save(const QString& filePath, const QString& format,
                       BitDepth depth, QString* errorMsg) const {
    if (m_data.empty()) return false;

    const QByteArray iccToEmbed = effectiveIccProfileData(m_meta);

    // --- XISF ---
    if (format.compare("xisf", Qt::CaseInsensitive) == 0) {
        if (!iccToEmbed.isEmpty() && m_meta.iccData.isEmpty()) {
            ImageBuffer tmp(*this);
            ImageBuffer::Metadata meta = tmp.metadata();
            meta.iccData = iccToEmbed;
            tmp.setMetadata(meta);
            return XISFWriter::write(filePath, tmp, depth, errorMsg);
        }
        return XISFWriter::write(filePath, *this, depth, errorMsg);
    }

    // --- FITS ---
    if (format.compare("fits", Qt::CaseInsensitive) == 0 ||
        format.compare("fit",  Qt::CaseInsensitive) == 0) {

        fitsfile* fptr;
        int status = 0;

        // CFITSIO convention: prefix "!" to overwrite existing files.
        QString outName = "!" + filePath;

        if (fits_create_file(&fptr, outName.toUtf8().constData(), &status)) {
            if (errorMsg) {
                *errorMsg = "CFITSIO Create File Error: " +
                            QString::number(status);
            }
            return false;
        }

        // Map internal bit depth to FITS BITPIX values.
        int bitpix = FLOAT_IMG;
        if      (depth == Depth_32Int) bitpix = LONG_IMG;
        else if (depth == Depth_16Int) bitpix = SHORT_IMG;
        else if (depth == Depth_8Int)  bitpix = BYTE_IMG;

        long naxes[3] = {(long)m_width, (long)m_height, (long)m_channels};
        int  naxis    = (m_channels > 1) ? 3 : 2;

        if (fits_create_img(fptr, bitpix, naxis, naxes, &status)) {
            if (errorMsg) {
                *errorMsg = "CFITSIO Create Image Error: " +
                            QString::number(status);
            }
            fits_close_file(fptr, &status);
            return false;
        }

        long nelements = m_width * m_height * m_channels;

        if (depth == Depth_32Float) {
            // Convert interleaved RGB to planar layout for FITS.
            std::vector<float> planarData(nelements);
            if (m_channels == 1) {
                planarData = m_data;
            } else {
                long planeSize = m_width * m_height;
                for (int c = 0; c < m_channels; ++c) {
                    for (long i = 0; i < planeSize; ++i) {
                        planarData[c * planeSize + i] =
                            m_data[i * m_channels + c];
                    }
                }
            }

            if (fits_write_img(fptr, TFLOAT, 1, nelements,
                               planarData.data(), &status)) {
                if (errorMsg) {
                    *errorMsg = "CFITSIO Write Error: " +
                                QString::number(status);
                }
                fits_close_file(fptr, &status);
                return false;
            }
        } else {
            // Integer depth: set BZERO/BSCALE for unsigned representation.
            double bscale = 1.0;
            double bzero  = 0.0;

            if (depth == Depth_16Int) {
                bzero  = 32768.0;
                bscale = 1.0;
                fits_write_key(fptr, TDOUBLE, "BZERO",  &bzero,
                               "offset for unsigned integers", &status);
                fits_write_key(fptr, TDOUBLE, "BSCALE", &bscale,
                               "scaling", &status);
            } else if (depth == Depth_32Int) {
                bzero  = 2147483648.0;
                bscale = 1.0;
                fits_write_key(fptr, TDOUBLE, "BZERO",  &bzero,
                               "offset for unsigned integers", &status);
                fits_write_key(fptr, TDOUBLE, "BSCALE", &bscale,
                               "scaling", &status);
            }

            // Scale [0,1] float data to the target integer range.
            float maxVal = (depth == Depth_16Int) ? 65535.0f
                         : (depth == Depth_32Int) ? 4294967295.0f
                         : 255.0f;

            std::vector<float> planarData(nelements);
            if (m_channels == 1) {
                for (int i = 0; i < (int)nelements; ++i) {
                    planarData[i] = m_data[i] * maxVal;
                }
            } else {
                long planeSize = m_width * m_height;
                for (int c = 0; c < m_channels; ++c) {
                    for (long i = 0; i < planeSize; ++i) {
                        planarData[c * planeSize + i] =
                            m_data[i * m_channels + c] * maxVal;
                    }
                }
            }

            if (fits_write_img(fptr, TFLOAT, 1, nelements,
                               planarData.data(), &status)) {
                if (errorMsg) {
                    *errorMsg = "CFITSIO Write Error: " +
                                QString::number(status);
                }
                fits_close_file(fptr, &status);
                return false;
            }
        }

        // Write original FITS header cards, skipping structural keywords.
        for (const auto& card : m_meta.rawHeaders) {
            QString key = card.key.trimmed().toUpper();
            if (key == "SIMPLE" || key == "BITPIX" || key == "NAXIS"  ||
                key == "NAXIS1" || key == "NAXIS2" || key == "NAXIS3" ||
                key == "EXTEND" || key == "BZERO"  || key == "BSCALE") {
                continue;
            }

            if (key == "HISTORY") {
                fits_write_history(fptr,
                    card.value.toUtf8().constData(), &status);
            } else if (key == "COMMENT") {
                fits_write_comment(fptr,
                    card.value.toUtf8().constData(), &status);
            } else {
                // Heuristic type detection for header values.
                bool isLong;
                long lVal = card.value.toLong(&isLong);

                bool   isDouble;
                double dVal = card.value.toDouble(&isDouble);

                if (isLong) {
                    fits_write_key(fptr, TLONG,
                        key.toUtf8().constData(), &lVal,
                        card.comment.toUtf8().constData(), &status);
                } else if (isDouble) {
                    fits_write_key(fptr, TDOUBLE,
                        key.toUtf8().constData(), &dVal,
                        card.comment.toUtf8().constData(), &status);
                } else {
                    fits_write_key(fptr, TSTRING,
                        key.toUtf8().constData(),
                        (void*)card.value.toUtf8().constData(),
                        card.comment.toUtf8().constData(), &status);
                }
            }
            if (status) status = 0;
        }

        // Write WCS keywords explicitly to ensure correctness.
        if (m_meta.ra != 0 || m_meta.dec != 0) {
            const char* ctype1 = "RA---TAN";
            const char* ctype2 = "DEC--TAN";
            fits_update_key(fptr, TSTRING, "CTYPE1", (void*)ctype1,
                            "Coordinate type", &status);
            fits_update_key(fptr, TSTRING, "CTYPE2", (void*)ctype2,
                            "Coordinate type", &status);

            double equinox = 2000.0;
            fits_update_key(fptr, TDOUBLE, "EQUINOX", &equinox,
                            "Equinox of coordinates", &status);

            fits_update_key(fptr, TDOUBLE, "CRVAL1",
                            (void*)&m_meta.ra, "RA at reference pixel", &status);
            fits_update_key(fptr, TDOUBLE, "CRVAL2",
                            (void*)&m_meta.dec, "Dec at reference pixel", &status);
            fits_update_key(fptr, TDOUBLE, "CRPIX1",
                            (void*)&m_meta.crpix1, "Reference pixel x", &status);
            fits_update_key(fptr, TDOUBLE, "CRPIX2",
                            (void*)&m_meta.crpix2, "Reference pixel y", &status);
            fits_update_key(fptr, TDOUBLE, "CD1_1",
                            (void*)&m_meta.cd1_1, "", &status);
            fits_update_key(fptr, TDOUBLE, "CD1_2",
                            (void*)&m_meta.cd1_2, "", &status);
            fits_update_key(fptr, TDOUBLE, "CD2_1",
                            (void*)&m_meta.cd2_1, "", &status);
            fits_update_key(fptr, TDOUBLE, "CD2_2",
                            (void*)&m_meta.cd2_2, "", &status);
            status = 0;
        }

        // Write observation metadata.
        if (m_meta.exposure > 0) {
            fits_update_key(fptr, TDOUBLE, "EXPTIME",
                            (void*)&m_meta.exposure,
                            "Exposure time in seconds", &status);
            status = 0;
        }
        if (m_meta.ccdTemp != 0) {
            fits_update_key(fptr, TDOUBLE, "CCD-TEMP",
                            (void*)&m_meta.ccdTemp,
                            "Sensor temperature in C", &status);
            status = 0;
        }

        // Embed ICC profile as a FITS extension.
        if (!iccToEmbed.isEmpty()) {
            long iccAxes[1] = {static_cast<long>(iccToEmbed.size())};
            if (fits_create_img(fptr, BYTE_IMG, 1, iccAxes, &status) == 0) {
                const char* extName = "ICC_PROFILE";
                const char* comment = "Embedded ICC profile";
                fits_update_key(fptr, TSTRING, "EXTNAME",
                                (void*)extName, comment, &status);
                if (status == 0) {
                    fits_write_img(fptr, 11, 1, iccAxes[0],
                                   (void*)iccToEmbed.constData(), &status);
                }
            }
            if (status != 0) {
                if (errorMsg) {
                    *errorMsg = "CFITSIO ICC Write Error: " +
                                QString::number(status);
                }
                fits_close_file(fptr, &status);
                return false;
            }
        }

        fits_close_file(fptr, &status);
        return true;

    // --- TIFF ---
    } else if (format.compare("tiff", Qt::CaseInsensitive) == 0 ||
               format.compare("tif",  Qt::CaseInsensitive) == 0) {

        SimpleTiffWriter::Format fmt = SimpleTiffWriter::Format_uint8;
        if      (depth == Depth_16Int)   fmt = SimpleTiffWriter::Format_uint16;
        else if (depth == Depth_32Int)   fmt = SimpleTiffWriter::Format_uint32;
        else if (depth == Depth_32Float) fmt = SimpleTiffWriter::Format_float32;

        if (!SimpleTiffWriter::write(filePath, m_width, m_height, m_channels,
                                     fmt, m_data, iccToEmbed, errorMsg)) {
            return false;
        }
        return true;

    // --- Standard image formats (JPEG, PNG, etc.) ---
    } else {
        QString fmtLower = format.toLower();

        if (fmtLower == "jpg" || fmtLower == "jpeg") {
            QImage saveImg = getDisplayImage(Display_Linear);
            applyIccToQImage(saveImg, iccToEmbed);

            QImageWriter writer(filePath);
            writer.setFormat("jpeg");
            writer.setQuality(100);
            if (!writer.write(saveImg)) {
                if (errorMsg) *errorMsg = writer.errorString();
                return false;
            }
            return true;

        } else if (fmtLower == "png") {
            const int    w    = m_width;
            const int    h    = m_height;
            const int    c    = m_channels;
            const float* src  = m_data.data();
            const bool   use16 = (depth == Depth_16Int ||
                                  depth == Depth_32Int ||
                                  depth == Depth_32Float);

            QImage saveImg;
            if (c == 1) {
                saveImg = QImage(w, h, use16 ? QImage::Format_Grayscale16
                                             : QImage::Format_Grayscale8);
                if (saveImg.isNull()) {
                    if (errorMsg) {
                        *errorMsg = "Failed to allocate PNG image buffer.";
                    }
                    return false;
                }
                for (int y = 0; y < h; ++y) {
                    if (use16) {
                        quint16* row = reinterpret_cast<quint16*>(
                            saveImg.scanLine(y));
                        for (int x = 0; x < w; ++x) {
                            float v = std::clamp(src[y * w + x], 0.0f, 1.0f);
                            row[x]  = static_cast<quint16>(v * 65535.0f + 0.5f);
                        }
                    } else {
                        uchar* row = saveImg.scanLine(y);
                        for (int x = 0; x < w; ++x) {
                            float v = std::clamp(src[y * w + x], 0.0f, 1.0f);
                            row[x]  = static_cast<uchar>(v * 255.0f + 0.5f);
                        }
                    }
                }
            } else {
                saveImg = QImage(w, h, use16 ? QImage::Format_RGBA64
                                             : QImage::Format_RGB888);
                if (saveImg.isNull()) {
                    if (errorMsg) {
                        *errorMsg = "Failed to allocate PNG image buffer.";
                    }
                    return false;
                }
                for (int y = 0; y < h; ++y) {
                    if (use16) {
                        QRgba64* row = reinterpret_cast<QRgba64*>(
                            saveImg.scanLine(y));
                        for (int x = 0; x < w; ++x) {
                            const size_t i = static_cast<size_t>(y) * w + x;
                            quint16 r = static_cast<quint16>(
                                std::clamp(src[i * 3 + 0], 0.0f, 1.0f) *
                                65535.0f + 0.5f);
                            quint16 g = static_cast<quint16>(
                                std::clamp(src[i * 3 + 1], 0.0f, 1.0f) *
                                65535.0f + 0.5f);
                            quint16 b = static_cast<quint16>(
                                std::clamp(src[i * 3 + 2], 0.0f, 1.0f) *
                                65535.0f + 0.5f);
                            row[x] = qRgba64(r, g, b, 65535);
                        }
                    } else {
                        uchar* row = saveImg.scanLine(y);
                        for (int x = 0; x < w; ++x) {
                            const size_t i = static_cast<size_t>(y) * w + x;
                            row[x * 3 + 0] = static_cast<uchar>(
                                std::clamp(src[i * 3 + 0], 0.0f, 1.0f) *
                                255.0f + 0.5f);
                            row[x * 3 + 1] = static_cast<uchar>(
                                std::clamp(src[i * 3 + 1], 0.0f, 1.0f) *
                                255.0f + 0.5f);
                            row[x * 3 + 2] = static_cast<uchar>(
                                std::clamp(src[i * 3 + 2], 0.0f, 1.0f) *
                                255.0f + 0.5f);
                        }
                    }
                }
            }

            applyIccToQImage(saveImg, iccToEmbed);
            QImageWriter writer(filePath, "png");
            writer.setCompression(9);
            if (!writer.write(saveImg)) {
                if (errorMsg) *errorMsg = writer.errorString();
                return false;
            }
            return true;

        } else {
            // Fallback: save as 8-bit via QImage for any other format.
            QImage saveImg = getDisplayImage(Display_Linear);
            if (!saveImg.save(filePath, format.toLatin1().constData())) {
                if (errorMsg) *errorMsg = "Failed to write image.";
                return false;
            }
            return true;
        }
    }
}


// ============================================================================
// Statistical Stretch (TrueStretch) - Helpers
// ============================================================================

/**
 * Stretch transfer function: maps rescaled value r through a nonlinear
 * curve controlled by the rescaled median and target median.
 */
[[maybe_unused]]
static float stretch_fn(float r, float med, float target) {
    if (r < 0) r = 0;
    float num = (med - 1.0f) * target * r;
    float den = med * (target + r - 1.0f) - target * r;
    if (std::abs(den) < 1e-12f) den = 1e-12f;
    return num / den;
}

/**
 * Piecewise-linear curve interpolation for post-stretch tone adjustment.
 */
static float apply_curve(float val,
                         const std::vector<float>& x,
                         const std::vector<float>& y) {
    if (val <= 0) return 0;
    if (val >= 1) return 1;

    for (size_t i = 0; i < x.size() - 1; ++i) {
        if (val >= x[i] && val <= x[i + 1]) {
            float t = (val - x[i]) / (x[i + 1] - x[i]);
            return y[i] + t * (y[i + 1] - y[i]);
        }
    }
    return val;
}

/**
 * Legacy statistics helper for TrueStretch parameter computation.
 */
struct TrueStretchStats {
    float median;
    float bp;   // Black point
    float den;  // Denominator (1 - bp)
};

[[maybe_unused]]
static TrueStretchStats getTrueStretchStats(const std::vector<float>& data,
                                            int stride, float nSig,
                                            int offset, int channels) {
    std::vector<float> sample;
    float minVal = 1e30f;

    size_t limit = data.size();
    sample.reserve(limit / (stride * channels) + 100);

    double sum   = 0;
    double sumSq = 0;
    long   count = 0;

    for (size_t i = offset; i < limit; i += stride * channels) {
        float v = data[i];
        sample.push_back(v);
        if (v < minVal) minVal = v;
        sum   += v;
        sumSq += v * v;
        count++;
    }

    if (sample.empty()) return {0.5f, 0.0f, 1.0f};

    size_t mid = sample.size() / 2;
    std::nth_element(sample.begin(), sample.begin() + mid, sample.end());
    float median = sample[mid];

    double mean   = sum / count;
    double var    = (sumSq / count) - (mean * mean);
    double stdDev = std::sqrt(std::max(0.0, var));

    float bp  = std::max(minVal, median - nSig * (float)stdDev);
    float den = 1.0f - bp;
    if (den < 1e-12f) den = 1e-12f;

    return {median, bp, den};
}


// ============================================================================
// Statistical Stretch (TrueStretch) - Main Implementation
// ============================================================================

void ImageBuffer::performTrueStretch(const StretchParams& params) {
    WriteLock lock(this);
    if (m_data.empty()) return;

    // Preserve original for mask blending.
    ImageBuffer original;
    if (hasMask()) original = *this;

    // Delegate to luminance-only path for color images when requested.
    if (params.lumaOnly && m_channels == 3) {
        performLumaOnlyStretch(params);
        if (hasMask()) blendResult(original);
        return;
    }

    // Step 1: Compute robust channel statistics.
    std::vector<StatisticalStretch::ChannelStats> stats;
    int stride = (m_width * m_height) / 100000 + 1;

    if (params.linked) {
        StatisticalStretch::ChannelStats s =
            StatisticalStretch::computeStats(
                m_data, stride, 0, 1,
                params.blackpointSigma, params.noBlackClip);
        stats.push_back(s);
    } else {
        for (int c = 0; c < m_channels; ++c) {
            StatisticalStretch::ChannelStats s =
                StatisticalStretch::computeStats(
                    m_data, stride, c, m_channels,
                    params.blackpointSigma, params.noBlackClip);
            stats.push_back(s);
        }
    }

    // Precompute rescaled medians (clamped to avoid singularities).
    std::vector<float> medRescaled;
    if (params.linked) {
        float mr = (stats[0].median - stats[0].blackpoint) /
                   stats[0].denominator;
        mr = std::clamp(mr, 1e-6f, 1.0f - 1e-6f);
        medRescaled.push_back(mr);
    } else {
        for (int c = 0; c < m_channels; ++c) {
            float mr = (stats[c].median - stats[c].blackpoint) /
                       stats[c].denominator;
            mr = std::clamp(mr, 1e-6f, 1.0f - 1e-6f);
            medRescaled.push_back(mr);
        }
    }

    // Step 2: Apply the main nonlinear stretch.
    long total = static_cast<long>(m_data.size());
    int  ch    = m_channels;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        int c    = static_cast<int>(i % ch);
        int sIdx = params.linked ? 0 : c;

        float bp  = stats[sIdx].blackpoint;
        float den = stats[sIdx].denominator;
        float mr  = medRescaled[sIdx];
        float v   = m_data[i];

        // Rescale relative to the black point.
        float rescaled = (v - bp) / den;
        if (rescaled < 0.0f) rescaled = 0.0f;

        // Apply the statistical stretch formula.
        float out = StatisticalStretch::stretchFormula(
                        rescaled, mr, params.targetMedian);

        m_data[i] = std::clamp(out, 0.0f, 1.0f);
    }

    // Step 3: Optional post-stretch curves adjustment.
    if (params.applyCurves && params.curvesBoost > 0) {
        StatisticalStretch::applyCurvesAdjustment(
            m_data, params.targetMedian, params.curvesBoost);
    }

    // Step 4: Optional HDR highlight compression.
    if (params.hdrCompress && params.hdrAmount > 0.0f) {
        if (m_channels == 3) {
            StatisticalStretch::hdrCompressColorLuminance(
                m_data, m_width, m_height,
                params.hdrAmount, params.hdrKnee, params.lumaMode);
        } else {
            StatisticalStretch::hdrCompressHighlights(
                m_data, params.hdrAmount, params.hdrKnee);
        }
    }

    // Step 5: Optional normalization to peak value.
    if (params.normalize) {
        float mx = 0.0f;
        for (float v : m_data) {
            if (v > mx) mx = v;
        }
        if (mx > 1e-9f) {
            #pragma omp parallel for
            for (long i = 0; i < total; ++i) {
                m_data[i] /= mx;
            }
        }
    }

    // Step 6: Optional high-range rescaling.
    if (params.highRange) {
        StatisticalStretch::highRangeRescale(
            m_data, m_width, m_height, m_channels,
            params.targetMedian,
            params.hrPedestal, params.hrSoftCeilPct, params.hrHardCeilPct,
            params.blackpointSigma, params.hrSoftclipThreshold);
    }

    // Apply mask blending if a mask is active.
    if (hasMask()) {
        blendResult(original);
    }
}


// ============================================================================
// Luminance-Only Stretch
// ============================================================================

void ImageBuffer::performLumaOnlyStretch(const StretchParams& params) {
    if (m_data.empty() || m_channels != 3) return;

    long pixelCount = static_cast<long>(m_width) * m_height;
    auto weights    = StatisticalStretch::getLumaWeights(params.lumaMode);

    // Step 1: Extract luminance channel.
    std::vector<float> luminance(pixelCount);

    #pragma omp parallel for
    for (long i = 0; i < pixelCount; ++i) {
        size_t idx   = i * 3;
        luminance[i] = weights[0] * m_data[idx]     +
                       weights[1] * m_data[idx + 1] +
                       weights[2] * m_data[idx + 2];
    }

    // Step 2: Compute statistics on the luminance channel.
    int stride = pixelCount / 100000 + 1;
    StatisticalStretch::ChannelStats stats =
        StatisticalStretch::computeStats(
            luminance, stride, 0, 1,
            params.blackpointSigma, params.noBlackClip);

    float medRescaled = (stats.median - stats.blackpoint) /
                        stats.denominator;

    // Step 3: Stretch the luminance.
    std::vector<float> stretchedLuma(pixelCount);

    #pragma omp parallel for
    for (long i = 0; i < pixelCount; ++i) {
        float v        = luminance[i];
        float rescaled = (v - stats.blackpoint) / stats.denominator;
        if (rescaled < 0.0f) rescaled = 0.0f;

        float out       = StatisticalStretch::stretchFormula(
                              rescaled, medRescaled, params.targetMedian);
        stretchedLuma[i] = std::clamp(out, 0.0f, 1.0f);
    }

    // Step 4: Optional curves adjustment on luminance.
    if (params.applyCurves && params.curvesBoost > 0) {
        StatisticalStretch::applyCurvesAdjustment(
            stretchedLuma, params.targetMedian, params.curvesBoost);
    }

    // Step 5: Optional HDR compression on luminance.
    if (params.hdrCompress && params.hdrAmount > 0.0f) {
        StatisticalStretch::hdrCompressHighlights(
            stretchedLuma, params.hdrAmount, params.hdrKnee);
    }

    // Step 6: Recombine by scaling RGB channels proportionally.
    // This preserves the original color ratios (chromaticity).
    #pragma omp parallel for
    for (long i = 0; i < pixelCount; ++i) {
        size_t idx  = i * 3;
        float  oldL = luminance[i];
        float  newL = stretchedLuma[i];

        if (oldL > 1e-10f) {
            float scale = newL / oldL;
            m_data[idx]     = std::clamp(m_data[idx]     * scale, 0.0f, 1.0f);
            m_data[idx + 1] = std::clamp(m_data[idx + 1] * scale, 0.0f, 1.0f);
            m_data[idx + 2] = std::clamp(m_data[idx + 2] * scale, 0.0f, 1.0f);
        } else {
            // Near-black pixels: set to uniform gray at the new luminance.
            m_data[idx]     = newL;
            m_data[idx + 1] = newL;
            m_data[idx + 2] = newL;
        }
    }

    // Step 7: Optional high-range rescaling.
    if (params.highRange) {
        StatisticalStretch::highRangeRescale(
            m_data, m_width, m_height, m_channels,
            params.targetMedian,
            params.hrPedestal, params.hrSoftCeilPct, params.hrHardCeilPct,
            params.blackpointSigma, params.hrSoftclipThreshold);
    }

    // Step 8: Optional normalization.
    if (params.normalize) {
        float mx = 0.0f;
        for (float v : m_data) {
            if (v > mx) mx = v;
        }
        if (mx > 1e-9f) {
            long total = static_cast<long>(m_data.size());
            #pragma omp parallel for
            for (long i = 0; i < total; ++i) {
                m_data[i] /= mx;
            }
        }
    }
}


// ============================================================================
// TrueStretch Preview LUT Computation
// ============================================================================

std::vector<std::vector<float>> ImageBuffer::computeTrueStretchLUT(
        const StretchParams& params, int size) const {

    if (m_data.empty()) return {};

    // Compute robust channel statistics.
    std::vector<StatisticalStretch::ChannelStats> stats;
    int stride = (m_width * m_height) / 100000 + 1;

    if (params.linked) {
        StatisticalStretch::ChannelStats s =
            StatisticalStretch::computeStats(
                m_data, stride, 0, 1,
                params.blackpointSigma, params.noBlackClip);
        stats.push_back(s);
    } else {
        for (int c = 0; c < m_channels; ++c) {
            StatisticalStretch::ChannelStats s =
                StatisticalStretch::computeStats(
                    m_data, stride, c, m_channels,
                    params.blackpointSigma, params.noBlackClip);
            stats.push_back(s);
        }
    }

    // Prepare optional post-stretch curves.
    std::vector<float> cx, cy;
    bool useCurves = (params.applyCurves && params.curvesBoost > 0);
    if (useCurves) {
        float tm  = params.targetMedian;
        float cb  = params.curvesBoost;
        float p3x = 0.25f * (1.0f - tm) + tm;
        float p4x = 0.75f * (1.0f - tm) + tm;
        float p3y = std::pow(p3x, (1.0f - cb));
        float p4y = std::pow(std::pow(p4x, (1.0f - cb)), (1.0f - cb));
        cx = {0.0f, 0.5f * tm, tm, p3x, p4x, 1.0f};
        cy = {0.0f, 0.5f * tm, tm, p3y, p4y, 1.0f};
    }

    // Precompute rescaled medians.
    std::vector<float> medRescaled;
    if (params.linked) {
        float mr = (stats[0].median - stats[0].blackpoint) /
                   stats[0].denominator;
        mr = std::clamp(mr, 1e-6f, 1.0f - 1e-6f);
        medRescaled.push_back(mr);
    } else {
        for (int c = 0; c < m_channels; ++c) {
            float mr = (stats[c].median - stats[c].blackpoint) /
                       stats[c].denominator;
            mr = std::clamp(mr, 1e-6f, 1.0f - 1e-6f);
            medRescaled.push_back(mr);
        }
    }

    // Build per-channel LUTs.
    std::vector<std::vector<float>> luts(
        m_channels, std::vector<float>(size));

    // Compute normalization factors if requested.
    std::vector<float> maxVals(m_channels, 1.0f);
    if (params.normalize) {
        float globalMax = 0.0f;
        for (int c = 0; c < m_channels; ++c) {
            int   sIdx = params.linked ? 0 : c;
            float bp   = stats[sIdx].blackpoint;
            float den  = stats[sIdx].denominator;
            float mr   = medRescaled[sIdx];

            float rescaled = (1.0f - bp) / den;
            if (rescaled < 0.0f) rescaled = 0.0f;

            float out = StatisticalStretch::stretchFormula(
                            rescaled, mr, params.targetMedian);
            if (useCurves) out = apply_curve(out, cx, cy);
            if (out > globalMax) globalMax = out;
        }
        if (globalMax > 1e-9f) {
            std::fill(maxVals.begin(), maxVals.end(), globalMax);
        }
    }

    // Populate the LUT entries.
    #pragma omp parallel for
    for (int i = 0; i < size; ++i) {
        float inVal = static_cast<float>(i) / (size - 1);

        for (int c = 0; c < m_channels; ++c) {
            int   sIdx = params.linked ? 0 : c;
            float bp   = stats[sIdx].blackpoint;
            float den  = stats[sIdx].denominator;
            float mr   = medRescaled[sIdx];

            float rescaled = (inVal - bp) / den;
            if (rescaled < 0.0f) rescaled = 0.0f;

            float out = StatisticalStretch::stretchFormula(
                            rescaled, mr, params.targetMedian);

            if (useCurves) out = apply_curve(out, cx, cy);
            if (out < 0.0f) out = 0.0f;

            if (params.normalize) {
                if (maxVals[c] > 1e-9f) {
                    out /= maxVals[c];
                }
            }

            out = std::clamp(out, 0.0f, 1.0f);
            luts[c][i] = out;
        }
    }

    return luts;
}

// ============================================================================
// Generalized Hyperbolic Stretch (GHS) - Helper Functions
// ============================================================================

#include "GHSAlgo.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

/**
 * Branchless RGB blending function for GHS color-preserving clip modes.
 * Blends between the per-channel stretched values (tf) and the
 * luminance-scaled values (sf), with automatic clipping to [0,1].
 *
 * @param r, g, b      Pointers to current pixel RGB values (modified in place)
 * @param sf0-sf2      Luminance-scaled (stretched-factor) RGB values
 * @param tf0-tf2      Per-channel independently stretched RGB values
 * @param do_channel   Per-channel enable flags
 * @param m_CB         Color blend factor (0 = fully luminance, 1 = fully independent)
 */
static inline void rgbblend_func(float* r, float* g, float* b,
                                 float sf0, float sf1, float sf2,
                                 float tf0, float tf1, float tf2,
                                 const bool* do_channel, float m_CB) {
    // Compute maximum values for source and target.
    float sfmax = std::max({sf0, sf1, sf2});
    float tfmax = std::max({tf0, tf1, tf2});

    // Difference between source and target maxima.
    float d = sfmax - tfmax;

    // Build branchless condition masks (0.0 or 1.0).
    float mask_cond = (tfmax + m_CB * d > 1.0f) ? 1.0f : 0.0f;
    float mask_dnz  = (d != 0.0f)               ? 1.0f : 0.0f;
    float full_mask = mask_cond * mask_dnz;

    // Safe denominator: use d if non-zero, otherwise 1.0 to avoid division by zero.
    float safe_d = mask_dnz * d + (1.0f - mask_dnz) * 1.0f;

    // Compute the blend factor k, clamped to prevent out-of-range values.
    float candidate = (1.0f - tfmax) / safe_d;
    float limited   = std::min(m_CB, candidate);
    float k         = full_mask * limited + (1.0f - full_mask) * m_CB;

    float one_minus_k = 1.0f - k;

    // Per-channel enable masks (branchless).
    float mr = do_channel[0] ? 1.0f : 0.0f;
    float mg = do_channel[1] ? 1.0f : 0.0f;
    float mb = do_channel[2] ? 1.0f : 0.0f;

    // Blend: enabled channels get the interpolated result, disabled keep original.
    *r = mr * (one_minus_k * tf0 + k * sf0) + (1.0f - mr) * (*r);
    *g = mg * (one_minus_k * tf1 + k * sf1) + (1.0f - mg) * (*g);
    *b = mb * (one_minus_k * tf2 + k * sf2) + (1.0f - mb) * (*b);
}


// ============================================================================
// RGB-HSL Color Space Conversion Helpers
// ============================================================================

/**
 * Convert RGB to HSL color space.
 * All values are in [0, 1]. Hue is normalized to [0, 1] (representing 0-360 degrees).
 */
static inline void rgb_to_hsl(float r, float g, float b,
                               float& h, float& s, float& l) {
    float v  = std::max({r, g, b});
    float m  = std::min({r, g, b});
    float vm = v - m;

    l = (m + v) / 2.0f;

    if (vm == 0.0f) {
        h = 0.0f;
        s = 0.0f;
        return;
    }

    s = (l <= 0.5f) ? (vm / (v + m)) : (vm / (2.0f - v - m));

    float r2 = (v - r) / vm;
    float g2 = (v - g) / vm;
    float b2 = (v - b) / vm;

    float hr = (r == v) ? (g == m ? 5.0f + b2 : 1.0f - g2) : 0.0f;
    float hg = (g == v) ? (b == m ? 1.0f + r2 : 3.0f - b2) : 0.0f;
    float hb = (b == v) ? (r == m ? 3.0f + g2 : 5.0f - r2) : 0.0f;

    h = (hr + hg + hb) / 6.0f;
}

/**
 * Convert HSL to RGB color space.
 * All input and output values are in [0, 1].
 */
static inline void hsl_to_rgb(float h, float s, float l,
                               float& r, float& g, float& b) {
    h = std::fmod(h, 1.0f);
    if (h < 0) h += 1.0f;

    float v = (l <= 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
    if (v <= 0.0f) {
        r = g = b = 0.0f;
        return;
    }

    float m      = l + l - v;
    float sv     = (v - m) / v;
    float h6     = h * 6.0f;
    int   sextant = static_cast<int>(h6);
    float fract  = h6 - sextant;
    float vsf    = v * sv * fract;
    float mid1   = m + vsf;
    float mid2   = v - vsf;

    r = (sextant == 0 || sextant == 5) ? v :
        (sextant == 2 || sextant == 3) ? m :
        (sextant == 4)                 ? mid1 : mid2;

    g = (sextant == 1 || sextant == 2) ? v :
        (sextant == 4 || sextant == 5) ? m :
        (sextant == 0)                 ? mid1 : mid2;

    b = (sextant == 3 || sextant == 4) ? v :
        (sextant == 0 || sextant == 1) ? m :
        (sextant == 2)                 ? mid1 : mid2;
}


// ============================================================================
// GHS Stretch Application
// ============================================================================

void ImageBuffer::applyGHS(const GHSParams& params) {
    WriteLock lock(this);
    if (m_data.empty()) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    // --- Map external parameters to internal algorithm parameters ---
    GHSAlgo::GHSParams algoParams;
    algoParams.D  = (float)params.D;
    algoParams.B  = (float)params.B;
    algoParams.SP = (float)params.SP;
    algoParams.LP = (float)params.LP;
    algoParams.HP = (float)params.HP;
    algoParams.BP = (float)params.BP;

    switch (params.mode) {
        case GHS_GeneralizedHyperbolic:
            algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL;  break;
        case GHS_InverseGeneralizedHyperbolic:
            algoParams.type = GHSAlgo::STRETCH_PAYNE_INVERSE; break;
        case GHS_Linear:
            algoParams.type = GHSAlgo::STRETCH_LINEAR;        break;
        case GHS_ArcSinh:
            algoParams.type = GHSAlgo::STRETCH_ASINH;         break;
        case GHS_InverseArcSinh:
            algoParams.type = GHSAlgo::STRETCH_INVASINH;      break;
        default:
            algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL;  break;
    }

    // Adjust control points for non-zero black point (non-linear modes only).
    if (algoParams.type != GHSAlgo::STRETCH_LINEAR && algoParams.BP > 0.0f) {
        float den = 1.0f - algoParams.BP;
        if (den > 1e-6f) {
            float invDen = 1.0f / den;
            algoParams.SP = (algoParams.SP - algoParams.BP) * invDen;
            algoParams.LP = (algoParams.LP - algoParams.BP) * invDen;
            algoParams.HP = (algoParams.HP - algoParams.BP) * invDen;
        }
    }

    // Precompute algorithm constants.
    GHSAlgo::GHSComputeParams cp;
    GHSAlgo::setup(cp, algoParams.B, algoParams.D,
                   algoParams.LP, algoParams.SP, algoParams.HP,
                   algoParams.type);

    long n_pixels = (long)m_width * m_height;

    // --- Determine luminance weighting factors based on color model ---
    float factor_red   = 0.2126f;
    float factor_green = 0.7152f;
    float factor_blue  = 0.0722f;

    int active_count = 0;
    for (int c = 0; c < 3; ++c) {
        if (params.channels[c]) active_count++;
    }

    if (params.colorMode == GHS_EvenWeightedLuminance) {
        float f = (active_count > 0) ? (1.0f / active_count) : 0.0f;
        factor_red = factor_green = factor_blue = f;
    } else if (params.colorMode == GHS_WeightedLuminance && m_channels == 3) {
        if (active_count < 3) {
            float f = (active_count > 0) ? (1.0f / active_count) : 0.0f;
            factor_red = factor_green = factor_blue = f;
        }
    }

    bool active_channels[3] = {
        params.channels[0], params.channels[1], params.channels[2]
    };
    float m_CB = 1.0f;

    float local_global_max = -FLT_MAX;

    // --- Main pixel processing loop ---
    #pragma omp parallel for reduction(max:local_global_max)
    for (long i = 0; i < n_pixels; ++i) {

        // Mono or independent channel mode: stretch each channel separately.
        if (m_channels < 3 || params.colorMode == GHS_Independent) {
            for (int c = 0; c < m_channels; ++c) {
                if (m_channels == 3 && !params.channels[c]) continue;

                float v   = m_data[i * m_channels + c];
                float civ = std::max(0.0f, std::min(1.0f, v));

                m_data[i * m_channels + c] =
                    (civ == 0.0f) ? 0.0f
                                  : GHSAlgo::compute(civ, algoParams, cp);
            }
        }
        // Saturation mode: stretch the saturation component in HSL space.
        else if (m_channels == 3 && params.colorMode == GHS_Saturation) {
            size_t idx = i * 3;
            float r = m_data[idx + 0];
            float g = m_data[idx + 1];
            float b = m_data[idx + 2];

            float h, s, l;
            rgb_to_hsl(r, g, b, h, s, l);

            float cs    = std::max(0.0f, std::min(1.0f, s));
            float new_s = (cs == 0.0f) ? 0.0f
                                        : GHSAlgo::compute(cs, algoParams, cp);

            hsl_to_rgb(h, new_s, l, r, g, b);
            m_data[idx + 0] = r;
            m_data[idx + 1] = g;
            m_data[idx + 2] = b;
        }
        // Luminance-based modes: stretch the weighted luminance, then rescale RGB.
        else if (m_channels == 3 &&
                 (params.colorMode == GHS_WeightedLuminance ||
                  params.colorMode == GHS_EvenWeightedLuminance)) {
            size_t idx = i * 3;
            float f[3] = {
                std::max(0.0f, std::min(1.0f, m_data[idx])),
                std::max(0.0f, std::min(1.0f, m_data[idx + 1])),
                std::max(0.0f, std::min(1.0f, m_data[idx + 2]))
            };

            float sf[3];
            float tf[3] = {0, 0, 0};

            // Compute weighted luminance from active channels.
            float fbar = (active_channels[0] ? factor_red   * f[0] : 0) +
                         (active_channels[1] ? factor_green * f[1] : 0) +
                         (active_channels[2] ? factor_blue  * f[2] : 0);

            // Stretch the luminance value.
            float sfbar = (fbar == 0.0f)
                ? 0.0f
                : GHSAlgo::compute(
                      std::min(1.0f, std::max(0.0f, fbar)),
                      algoParams, cp);

            // Compute the scaling factor to apply to all channels.
            float stretch_factor = (fbar == 0.0f) ? 0.0f : sfbar / fbar;

            for (int c = 0; c < 3; ++c) {
                sf[c] = f[c] * stretch_factor;
            }

            // Compute per-channel stretched values for RGB blend mode.
            if (params.clipMode == GHS_ClipRGBBlend) {
                for (int c = 0; c < 3; ++c) {
                    tf[c] = active_channels[c]
                        ? ((f[c] == 0.0f) ? 0.0f
                                          : GHSAlgo::compute(f[c], algoParams, cp))
                        : 0.0f;
                }
            }

            // Apply the selected clip mode.
            if (params.clipMode == GHS_ClipRGBBlend) {
                rgbblend_func(&f[0], &f[1], &f[2],
                              sf[0], sf[1], sf[2],
                              tf[0], tf[1], tf[2],
                              active_channels, m_CB);
                for (int c = 0; c < 3; ++c) {
                    m_data[idx + c] = active_channels[c] ? f[c] : f[c];
                }
            } else if (params.clipMode == GHS_Rescale) {
                float maxval = std::max({sf[0], sf[1], sf[2]});
                if (maxval > 1.0f) {
                    float invmax = 1.0f / maxval;
                    sf[0] *= invmax;
                    sf[1] *= invmax;
                    sf[2] *= invmax;
                }
                for (int c = 0; c < 3; ++c) {
                    m_data[idx + c] = active_channels[c] ? sf[c] : f[c];
                }
            } else if (params.clipMode == GHS_RescaleGlobal) {
                float maxval = std::max({sf[0], sf[1], sf[2]});
                if (maxval > local_global_max) local_global_max = maxval;
                for (int c = 0; c < 3; ++c) {
                    m_data[idx + c] = active_channels[c] ? sf[c] : f[c];
                }
            } else {
                // Default: hard clip to [0, 1].
                for (int c = 0; c < 3; ++c) {
                    m_data[idx + c] = active_channels[c]
                        ? std::max(0.0f, std::min(1.0f, sf[c]))
                        : f[c];
                }
            }
        }
    }

    // Second pass for global rescale mode.
    if (params.colorMode != GHS_Independent &&
        params.clipMode == GHS_RescaleGlobal &&
        m_channels == 3) {
        if (local_global_max > 0.0f) {
            float invMax = 1.0f / local_global_max;
            #pragma omp parallel for
            for (long i = 0; i < n_pixels; ++i) {
                for (int c = 0; c < 3; ++c) {
                    if (active_channels[c]) {
                        m_data[i * 3 + c] *= invMax;
                    }
                }
            }
        }
    }

    if (hasMask()) {
        blendResult(original);
    }
}


// ============================================================================
// GHS Preview LUT Computation
// ============================================================================

std::vector<float> ImageBuffer::computeGHSLUT(const GHSParams& params,
                                               int size) const {
    std::vector<float> lut(size);

    GHSAlgo::GHSParams algoParams;
    algoParams.D  = (float)params.D;
    algoParams.B  = (float)params.B;
    algoParams.SP = (float)params.SP;
    algoParams.LP = (float)params.LP;
    algoParams.HP = (float)params.HP;
    algoParams.BP = (float)params.BP;

    // Map stretch mode.
    if      (params.mode == GHS_GeneralizedHyperbolic)
        algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL;
    else if (params.mode == GHS_InverseGeneralizedHyperbolic)
        algoParams.type = GHSAlgo::STRETCH_PAYNE_INVERSE;
    else if (params.mode == GHS_ArcSinh)
        algoParams.type = GHSAlgo::STRETCH_ASINH;
    else if (params.mode == GHS_InverseArcSinh)
        algoParams.type = GHSAlgo::STRETCH_INVASINH;
    else
        algoParams.type = GHSAlgo::STRETCH_LINEAR;

    // Adjust control points for non-zero black point.
    if (algoParams.type != GHSAlgo::STRETCH_LINEAR && algoParams.BP > 0.0f) {
        float den = 1.0f - algoParams.BP;
        if (den > 1e-6f) {
            float invDen = 1.0f / den;
            algoParams.SP = (algoParams.SP - algoParams.BP) * invDen;
            algoParams.LP = (algoParams.LP - algoParams.BP) * invDen;
            algoParams.HP = (algoParams.HP - algoParams.BP) * invDen;
        }
    }

    GHSAlgo::GHSComputeParams cp;
    GHSAlgo::setup(cp, algoParams.B, algoParams.D,
                   algoParams.LP, algoParams.SP, algoParams.HP,
                   algoParams.type);

    for (int i = 0; i < size; ++i) {
        float in = (float)i / (size - 1);
        lut[i] = GHSAlgo::compute(in, algoParams, cp);
    }

    return lut;
}


// ============================================================================
// Mask-Based Result Blending
// ============================================================================

void ImageBuffer::blendResult(const ImageBuffer& original, bool inverseMask) {
    WriteLock lock(this);
    if (!hasMask() || m_mask.data.empty()) return;

    // Acquire read access to the original buffer after the write lock on this
    // buffer to maintain a consistent lock ordering and prevent deadlocks.
    ReadLock origLock(&original);
    if (m_data.size() != original.m_data.size()) return;

    long n_pixels = (long)m_width * m_height;
    int  ch       = m_channels;

    // Hoist branch-invariant flags out of the inner loop.
    const bool  maskInverted = m_mask.inverted;
    const bool  protectMode  = (m_mask.mode == "protect");
    const float opacity      = m_mask.opacity;

    #pragma omp parallel for schedule(static)
    for (long pi = 0; pi < n_pixels; ++pi) {
        if ((size_t)pi >= m_mask.data.size()) continue;

        float alpha = m_mask.data[pi];
        if (maskInverted) alpha = 1.0f - alpha;
        if (inverseMask)  alpha = 1.0f - alpha;
        if (protectMode)  alpha = 1.0f - alpha;
        alpha *= opacity;

        const float inv_alpha = 1.0f - alpha;

        // Blend: Result = Processed * alpha + Original * (1 - alpha)
        size_t base = (size_t)pi * ch;
        for (int c = 0; c < ch; ++c) {
            m_data[base + c] = m_data[base + c] * alpha +
                               original.m_data[base + c] * inv_alpha;
        }
    }
}


// ============================================================================
// WCS (World Coordinate System) Helpers
// ============================================================================

/**
 * Update WCS metadata (CRPIX, CD matrix) after a geometric transformation.
 * The transform maps old pixel coordinates to new pixel coordinates.
 * The CD matrix is updated using the Jacobian of the inverse transform:
 *   CD_new = CD_old * J(transform^{-1})
 */
void ImageBuffer::reframeWCS(const QTransform& trans,
                              [[maybe_unused]] int oldWidth,
                              [[maybe_unused]] int oldHeight) {
    if (m_meta.ra == 0 && m_meta.dec == 0) return;

    // Map the reference pixel through the forward transform (0-indexed).
    double crpix1_0 = m_meta.crpix1 - 1.0;
    double crpix2_0 = m_meta.crpix2 - 1.0;

    QPointF pOld(crpix1_0, crpix2_0);
    QPointF pNew = trans.map(pOld);

    m_meta.crpix1 = pNew.x() + 1.0;
    m_meta.crpix2 = pNew.y() + 1.0;

    // Update the CD matrix: CD_new = CD_old * J_inv
    // where J_inv is the Jacobian of the inverse transform.
    bool invertible = false;
    QTransform inv = trans.inverted(&invertible);
    if (!invertible) return;

    double old_cd11 = m_meta.cd1_1;
    double old_cd12 = m_meta.cd1_2;
    double old_cd21 = m_meta.cd2_1;
    double old_cd22 = m_meta.cd2_2;

    m_meta.cd1_1 = old_cd11 * inv.m11() + old_cd12 * inv.m21();
    m_meta.cd1_2 = old_cd11 * inv.m12() + old_cd12 * inv.m22();
    m_meta.cd2_1 = old_cd21 * inv.m11() + old_cd22 * inv.m21();
    m_meta.cd2_2 = old_cd21 * inv.m12() + old_cd22 * inv.m22();

    syncWcsToHeaders();
}

/**
 * Synchronize computed WCS parameters back into the raw FITS header cards.
 */
void ImageBuffer::syncWcsToHeaders() {
    auto setKey = [&](const QString& key, double val, const QString& comment) {
        bool found = false;
        QString valStr = QString::number(val, 'f', 9);

        for (auto& card : m_meta.rawHeaders) {
            if (card.key == key) {
                card.value = valStr;
                found = true;
                break;
            }
        }
        if (!found) {
            m_meta.rawHeaders.push_back({key, valStr, comment});
        }
    };

    setKey("CRPIX1", m_meta.crpix1, "Reference pixel axis 1");
    setKey("CRPIX2", m_meta.crpix2, "Reference pixel axis 2");
    setKey("CD1_1",  m_meta.cd1_1,  "PC matrix 1_1");
    setKey("CD1_2",  m_meta.cd1_2,  "PC matrix 1_2");
    setKey("CD2_1",  m_meta.cd2_1,  "PC matrix 2_1");
    setKey("CD2_2",  m_meta.cd2_2,  "PC matrix 2_2");
}


// ============================================================================
// Catalog Star Filtering
// ============================================================================

void ImageBuffer::filterCatalogStars() {
    ReadLock lock(this);
    filterCatalogStarsInternal();
}

/**
 * Remove catalog stars whose projected pixel positions fall outside
 * the current image bounds. Assumes caller holds an appropriate lock.
 */
void ImageBuffer::filterCatalogStarsInternal() {
    if (m_meta.catalogStars.empty()) return;

    // Verify WCS data is present.
    bool hasWcs = (m_meta.ra != 0 || m_meta.dec != 0 ||
                   !m_meta.ctype1.isEmpty());
    if (!hasWcs) return;

    std::vector<CatalogStar> filtered;
    filtered.reserve(m_meta.catalogStars.size());

    for (const auto& star : m_meta.catalogStars) {
        double px, py;
        if (WCSUtils::worldToPixel(m_meta, star.ra, star.dec, px, py)) {
            if (px >= 0 && px < m_width && py >= 0 && py < m_height) {
                filtered.push_back(star);
            }
        }
    }

    m_meta.catalogStars = std::move(filtered);
}


// ============================================================================
// Geometric Operations - Crop
// ============================================================================

void ImageBuffer::crop(int x, int y, int w, int h) {
    WriteLock lock(this);
    if (m_data.empty()) return;

    int oldW = m_width;
    int oldH = m_height;

    // Clamp crop region to image bounds.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > m_width)  w = m_width  - x;
    if (y + h > m_height) h = m_height - y;
    if (w <= 0 || h <= 0) return;

    // Copy the cropped region.
    std::vector<float> newData(w * h * m_channels);
    for (int ry = 0; ry < h; ++ry) {
        int srcY         = y + ry;
        int srcIdxStart  = (srcY * m_width + x) * m_channels;
        int destIdxStart = (ry * w) * m_channels;
        int copySize     = w * m_channels;

        for (int k = 0; k < copySize; ++k) {
            newData[destIdxStart + k] = m_data[srcIdxStart + k];
        }
    }

    m_width  = w;
    m_height = h;
    m_data   = newData;

    // Update WCS: crop is a translation by (-x, -y).
    QTransform t;
    t.translate(-x, -y);
    reframeWCS(t, oldW, oldH);

    // Crop the mask if present.
    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.crop(x, y, w, h);
        m_mask.data   = maskBuf.data();
        m_mask.width  = w;
        m_mask.height = h;
    }

    filterCatalogStarsInternal();
}


// ============================================================================
// Geometric Operations - Arbitrary Rotation
// ============================================================================

void ImageBuffer::rotate(float angleDegrees) {
    WriteLock lock(this);
    if (m_data.empty()) return;
    if (std::abs(angleDegrees) < 0.1f) return;

    int oldW = m_width;
    int oldH = m_height;

    // Convert to radians (negative for image coordinate convention).
    float theta = -angleDegrees * 3.14159265f / 180.0f;
    float cosT  = std::cos(theta);
    float sinT  = std::sin(theta);

    // Compute bounding box of the rotated image.
    float x0 = 0,       y0 = 0;
    float x1 = m_width, y1 = 0;
    float x2 = 0,       y2 = m_height;
    float x3 = m_width, y3 = m_height;

    auto rotX = [&](float x, float y) { return x * cosT - y * sinT; };
    auto rotY = [&](float x, float y) { return x * sinT + y * cosT; };

    float rx0 = rotX(x0, y0), ry0 = rotY(x0, y0);
    float rx1 = rotX(x1, y1), ry1 = rotY(x1, y1);
    float rx2 = rotX(x2, y2), ry2 = rotY(x2, y2);
    float rx3 = rotX(x3, y3), ry3 = rotY(x3, y3);

    float minX = std::min({rx0, rx1, rx2, rx3});
    float maxX = std::max({rx0, rx1, rx2, rx3});
    float minY = std::min({ry0, ry1, ry2, ry3});
    float maxY = std::max({ry0, ry1, ry2, ry3});

    int newW = static_cast<int>(std::ceil(maxX - minX));
    int newH = static_cast<int>(std::ceil(maxY - minY));

    std::vector<float> newData(newW * newH * m_channels, 0.0f);

    float centerX    = m_width  / 2.0f;
    float centerY    = m_height / 2.0f;
    float newCenterX = newW / 2.0f;
    float newCenterY = newH / 2.0f;

    ImageBuffer original;
    if (hasMask()) original = *this;

    // Inverse mapping with bilinear interpolation.
    #pragma omp parallel for
    for (int y = 0; y < newH; ++y) {
        for (int x = 0; x < newW; ++x) {
            float dx = x - newCenterX;
            float dy = y - newCenterY;

            // Inverse rotation to find source coordinates.
            float srcX = dx * std::cos(-theta) - dy * std::sin(-theta) + centerX;
            float srcY = dx * std::sin(-theta) + dy * std::cos(-theta) + centerY;

            // Bilinear interpolation within valid bounds.
            if (srcX >= 0 && srcX < m_width - 1 &&
                srcY >= 0 && srcY < m_height - 1) {
                int   px = static_cast<int>(srcX);
                int   py = static_cast<int>(srcY);
                float fx = srcX - px;
                float fy = srcY - py;

                int idx00 = (py       * m_width + px)     * m_channels;
                int idx01 = (py       * m_width + px + 1) * m_channels;
                int idx10 = ((py + 1) * m_width + px)     * m_channels;
                int idx11 = ((py + 1) * m_width + px + 1) * m_channels;

                for (int c = 0; c < m_channels; ++c) {
                    float v00 = m_data[idx00 + c];
                    float v01 = m_data[idx01 + c];
                    float v10 = m_data[idx10 + c];
                    float v11 = m_data[idx11 + c];

                    float top = v00 * (1 - fx) + v01 * fx;
                    float bot = v10 * (1 - fx) + v11 * fx;
                    float val = top * (1 - fy) + bot * fy;

                    newData[(y * newW + x) * m_channels + c] = val;
                }
            }
        }
    }

    m_width  = newW;
    m_height = newH;
    m_data   = newData;

    // Apply the same rotation to the mask.
    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(original.width(), original.height(), 1, m_mask.data);
        maskBuf.rotate(angleDegrees);
        m_mask.data   = maskBuf.data();
        m_mask.width  = maskBuf.width();
        m_mask.height = maskBuf.height();
    }

    // Update WCS: rotation around the center of the image.
    QTransform wcsTrans;
    wcsTrans.translate(newCenterX, newCenterY);
    wcsTrans.rotate(angleDegrees);
    wcsTrans.translate(-centerX, -centerY);
    reframeWCS(wcsTrans, oldW, oldH);
}


// ============================================================================
// Color Corrections - SCNR (Subtractive Chromatic Noise Reduction)
// ============================================================================

void ImageBuffer::applySCNR(float amount, int method) {
    WriteLock lock(this);
    if (m_data.empty() || m_channels < 3) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    long total = static_cast<long>(m_width) * m_height;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long  idx = i * m_channels;
        float r   = m_data[idx + 0];
        float g   = m_data[idx + 1];
        float b   = m_data[idx + 2];

        // Compute reference level based on the selected method.
        float ref = 0.0f;
        switch (method) {
            case 0:  ref = (r + b) / 2.0f;    break; // Average Neutral
            case 1:  ref = std::max(r, b);     break; // Maximum Neutral
            case 2:  ref = std::min(r, b);     break; // Minimum Neutral
            default: ref = (r + b) / 2.0f;    break;
        }

        // Reduce green only where it exceeds the reference.
        float mask  = std::max(0.0f, g - ref);
        float g_new = g - amount * mask;

        m_data[idx + 1] = g_new;
    }

    if (hasMask()) {
        blendResult(original);
    }
}


// ============================================================================
// Color Corrections - Magenta Correction
// ============================================================================

void ImageBuffer::applyMagentaCorrection(float mod_b, float threshold,
                                          bool withStarMask) {
    if (mod_b >= 1.0f) return;

    // Star mask generation requires a read lock internally, so it must be
    // performed before acquiring the write lock to avoid deadlock.
    std::vector<float> starMask;
    if (withStarMask) {
        auto stars = detectStarsHQ();
        starMask = generateHQStarMask(stars);
    }

    WriteLock lock(this);
    if (m_data.empty() || m_channels < 3) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    long total = static_cast<long>(m_width) * m_height;

    // Suppress blue channel in magenta-dominant pixels (HSV hue range 0.40-0.99).
    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long  idx = i * 3;
        float r   = m_data[idx + 0];
        float g   = m_data[idx + 1];
        float b   = m_data[idx + 2];

        float h, s, v_hsv;
        rgbToHsv(r, g, b, h, s, v_hsv);

        if (h >= 0.40f && h <= 0.99f) {
            float luminance = 0.299f * r + 0.587f * g + 0.114f * b;

            bool apply = false;
            if (withStarMask) {
                if (starMask[i] > 0.01f) apply = true;
            } else {
                if (luminance > threshold) apply = true;
            }

            if (apply) {
                // Blend blue toward green to neutralize the magenta cast.
                float target_blue = g;
                float b_new = b * mod_b + target_blue * (1.0f - mod_b);
                m_data[idx + 2] = std::max(0.0f, std::min(1.0f, b_new));
            }
        }
    }

    if (hasMask()) {
        blendResult(original);
    }
}


// ============================================================================
// Color Space Conversion - RGB to HSV
// ============================================================================

void ImageBuffer::rgbToHsv(float r, float g, float b,
                            float& h, float& s, float& v) {
    float cmax  = std::max({r, g, b});
    float cmin  = std::min({r, g, b});
    float delta = cmax - cmin;

    v = cmax;

    if (delta == 0.0f) {
        s = 0.0f;
        h = 0.0f;
        return;
    }

    s = delta / cmax;

    if      (cmax == r) h = (g - b) / delta;
    else if (cmax == g) h = (b - r) / delta + 2.0f;
    else                h = (r - g) / delta + 4.0f;

    h /= 6.0f;
    if (h < 0.0f) h += 1.0f;
}


// ============================================================================
// Geometric Operations - Rotated Crop
// ============================================================================

void ImageBuffer::cropRotated(float cx, float cy, float w, float h,
                               float angleDegrees) {
    WriteLock lock(this);
    if (m_data.empty()) return;
    if (w <= 1 || h <= 1) return;

    [[maybe_unused]] int oldW = m_width;
    [[maybe_unused]] int oldH = m_height;

    int outW = static_cast<int>(w);
    int outH = static_cast<int>(h);

    ImageBuffer original;
    if (hasMask()) original = *this;

    std::vector<float> newData(outW * outH * m_channels);

    float theta = angleDegrees * 3.14159265f / 180.0f;
    float cosT  = std::cos(theta);
    float sinT  = std::sin(theta);
    float halfW = w / 2.0f;
    float halfH = h / 2.0f;

    // Inverse mapping with bilinear interpolation.
    #pragma omp parallel for
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            float dx = x - halfW;
            float dy = y - halfH;

            // Map destination pixel back to source coordinates.
            float srcDX = dx * cosT - dy * sinT;
            float srcDY = dx * sinT + dy * cosT;
            float srcX  = cx + srcDX;
            float srcY  = cy + srcDY;

            if (srcX >= 0 && srcX < m_width - 1 &&
                srcY >= 0 && srcY < m_height - 1) {
                int   px = static_cast<int>(srcX);
                int   py = static_cast<int>(srcY);
                float fx = srcX - px;
                float fy = srcY - py;

                int idx00 = (py       * m_width + px)     * m_channels;
                int idx01 = (py       * m_width + px + 1) * m_channels;
                int idx10 = ((py + 1) * m_width + px)     * m_channels;
                int idx11 = ((py + 1) * m_width + px + 1) * m_channels;

                for (int c = 0; c < m_channels; ++c) {
                    float v00 = m_data[idx00 + c];
                    float v01 = m_data[idx01 + c];
                    float v10 = m_data[idx10 + c];
                    float v11 = m_data[idx11 + c];

                    float top = v00 * (1 - fx) + v01 * fx;
                    float bot = v10 * (1 - fx) + v11 * fx;
                    float val = top * (1 - fy) + bot * fy;

                    newData[(y * outW + x) * m_channels + c] = val;
                }
            } else {
                // Out-of-bounds: fill with black.
                for (int c = 0; c < m_channels; ++c) {
                    newData[(y * outW + x) * m_channels + c] = 0.0f;
                }
            }
        }
    }

    m_width  = outW;
    m_height = outH;
    m_data   = newData;

    // Update CRPIX (reference pixel position in screen-space, Y-down).
    {
        double crpix1_0 = m_meta.crpix1 - 1.0;
        double crpix2_0 = m_meta.crpix2 - 1.0;

        double u  = crpix1_0 - static_cast<double>(cx);
        double v  = crpix2_0 - static_cast<double>(cy);
        double ct = static_cast<double>(cosT);
        double st = static_cast<double>(sinT);

        // Forward map: old coords -> new coords.
        double newU =  u * ct + v * st;
        double newV = -u * st + v * ct;

        m_meta.crpix1 = newU + static_cast<double>(halfW) + 1.0;
        m_meta.crpix2 = newV + static_cast<double>(halfH) + 1.0;
    }

    // Update CD matrix.
    // The pixel loop rotates by +theta in screen space. In astronomical Y-up
    // coordinates the effective rotation is also +theta.
    // CD_new = CD_old * R(+theta)
    {
        double old_cd11 = m_meta.cd1_1;
        double old_cd12 = m_meta.cd1_2;
        double old_cd21 = m_meta.cd2_1;
        double old_cd22 = m_meta.cd2_2;

        double ct = static_cast<double>(cosT);
        double st = static_cast<double>(sinT);

        m_meta.cd1_1 = old_cd11 *   ct  + old_cd12 * st;
        m_meta.cd1_2 = old_cd11 * (-st) + old_cd12 * ct;
        m_meta.cd2_1 = old_cd21 *   ct  + old_cd22 * st;
        m_meta.cd2_2 = old_cd21 * (-st) + old_cd22 * ct;
    }

    syncWcsToHeaders();

    // Apply the same transform to the mask.
    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(original.width(), original.height(), 1, m_mask.data);
        maskBuf.cropRotated(cx, cy, w, h, angleDegrees);
        m_mask.data   = maskBuf.data();
        m_mask.width  = maskBuf.width();
        m_mask.height = maskBuf.height();
    }

    filterCatalogStarsInternal();
}


// ============================================================================
// Pixel Accessors
// ============================================================================

float ImageBuffer::getPixelValue(int x, int y, int c) const {
    ReadLock lock(this);
    if (m_data.empty()) return 0.0f;
    if (x < 0 || x >= m_width || y < 0 || y >= m_height ||
        c < 0 || c >= m_channels) return 0.0f;
    return m_data[(static_cast<size_t>(y) * m_width + x) * m_channels + c];
}

float ImageBuffer::getPixelFlat(size_t index, int c) const {
    ReadLock lock(this);
    if (m_data.empty()) return 0.0f;

    if (m_channels == 1) {
        if (index >= m_data.size()) return 0.0f;
        return m_data[index];
    }

    size_t idx = index * m_channels + c;
    if (idx >= m_data.size()) return 0.0f;
    return m_data[idx];
}


// ============================================================================
// Channel Statistics
// ============================================================================

float ImageBuffer::getChannelMedian(int channelIndex) const {
    ReadLock lock(this);
    if (m_data.empty()) return 0.0f;

    std::vector<float> chData;
    chData.reserve(m_data.size() / m_channels);
    int ch = m_channels;
    for (size_t i = channelIndex; i < m_data.size(); i += ch) {
        chData.push_back(m_data[i]);
    }

    return RobustStatistics::getMedian(chData);
}

float ImageBuffer::getAreaMean(int x, int y, int w, int h, int c) const {
    qDebug() << "[ImageBuffer::getAreaMean] Request:"
             << x << y << w << h << "ch:" << c
             << "buf:" << m_name << (void*)this;

    ReadLock lock(this);

    qDebug() << "[ImageBuffer::getAreaMean] Lock acquired. Data size:"
             << m_data.size() << "Width:" << m_width
             << "Height:" << m_height;

    // Intersect the requested rectangle with image bounds.
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(m_width,  x + w);
    int y1 = std::min(m_height, y + h);

    if (x1 <= x0 || y1 <= y0) return 0.0f;
    if (m_data.empty()) return 0.0f;

    double sum   = 0.0;
    long   count = static_cast<long>(x1 - x0) * (y1 - y0);

    #pragma omp parallel for reduction(+:sum)
    for (int iy = y0; iy < y1; ++iy) {
        for (int ix = x0; ix < x1; ++ix) {
            size_t idx = (static_cast<size_t>(iy) * m_width + ix) *
                         m_channels + c;
            sum += m_data[idx];
        }
    }

    return (count > 0) ? (float)(sum / count) : 0.0f;
}


// ============================================================================
// Clipping Statistics
// ============================================================================

void ImageBuffer::computeClippingStats(long& lowClip, long& highClip) const {
    ReadLock lock(this);
    lowClip  = 0;
    highClip = 0;

    long   tempLow  = 0;
    long   tempHigh = 0;
    size_t n        = m_data.size();

    #pragma omp parallel for reduction(+:tempLow, tempHigh)
    for (size_t i = 0; i < n; ++i) {
        float v = m_data[i];
        if (v <= 0.0f)      tempLow++;
        else if (v >= 1.0f) tempHigh++;
    }

    lowClip  = tempLow;
    highClip = tempHigh;
}


// ============================================================================
// Histogram Computation
// ============================================================================

std::vector<std::vector<int>> ImageBuffer::computeHistogram(int bins) const {
    ReadLock lock(this);
    if (m_data.empty() || bins <= 0) return {};

    int numThreads = omp_get_max_threads();
    if (numThreads < 1) numThreads = 1;

    // Per-thread local histograms to avoid contention.
    std::vector<std::vector<std::vector<int>>> localHists(
        numThreads,
        std::vector<std::vector<int>>(m_channels, std::vector<int>(bins, 0)));

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        #pragma omp for
        for (long long i = 0; i < (long long)m_data.size(); ++i) {
            int   c = i % m_channels;
            float v = m_data[i];

            if (v < 0.0f)      v = 0.0f;
            else if (v > 1.0f) v = 1.0f;

            int b = static_cast<int>(v * (bins - 1) + 0.5f);
            localHists[tid][c][b]++;
        }
    }

    // Merge thread-local histograms.
    std::vector<std::vector<int>> hist(
        m_channels, std::vector<int>(bins, 0));
    for (int t = 0; t < numThreads; ++t) {
        for (int c = 0; c < m_channels; ++c) {
            for (int b = 0; b < bins; ++b) {
                hist[c][b] += localHists[t][c][b];
            }
        }
    }

    return hist;
}


// ============================================================================
// Geometric Operations - Fixed-Angle Rotations
// ============================================================================

void ImageBuffer::rotate90() {
    WriteLock lock(this);
    if (m_data.empty()) return;

    int oldW = m_width;
    int oldH = m_height;
    int ch   = m_channels;

    std::vector<float> newData(oldW * oldH * ch);

    #pragma omp parallel for
    for (int y = 0; y < oldH; ++y) {
        for (int x = 0; x < oldW; ++x) {
            int newX   = oldH - 1 - y;
            int newY   = x;
            int oldIdx = (y * oldW + x) * ch;
            int newIdx = (newY * oldH + newX) * ch;
            for (int c = 0; c < ch; ++c) {
                newData[newIdx + c] = m_data[oldIdx + c];
            }
        }
    }

    m_width  = oldH;
    m_height = oldW;
    m_data   = std::move(newData);

    // WCS: (x,y) -> (H-1-y, x) = Translate(H-1, 0) * Rotate(90)
    QTransform t;
    t.translate(oldH - 1, 0);
    t.rotate(90);
    reframeWCS(t, oldW, oldH);

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.rotate90();
        m_mask.data   = maskBuf.data();
        m_mask.width  = maskBuf.width();
        m_mask.height = maskBuf.height();
    }
}

void ImageBuffer::rotate180() {
    WriteLock lock(this);
    if (m_data.empty()) return;

    int h  = m_height;
    int w  = m_width;
    int ch = m_channels;

    // Swap pixels from opposite ends toward the center.
    #pragma omp parallel for
    for (int y = 0; y < h / 2; ++y) {
        int y2 = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            int x2   = w - 1 - x;
            int idx1 = (y  * w + x)  * ch;
            int idx2 = (y2 * w + x2) * ch;
            for (int c = 0; c < ch; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }

    // Handle the middle row for odd-height images.
    if (h % 2 != 0) {
        int y = h / 2;
        for (int x = 0; x < w / 2; ++x) {
            int x2   = w - 1 - x;
            int idx1 = (y * w + x)  * ch;
            int idx2 = (y * w + x2) * ch;
            for (int c = 0; c < ch; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }

    // WCS: (x,y) -> (W-1-x, H-1-y) = Translate(W-1, H-1) * Rotate(180)
    QTransform t;
    t.translate(w - 1, h - 1);
    t.rotate(180);
    reframeWCS(t, w, h);

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(w, h, 1, m_mask.data);
        maskBuf.rotate180();
        m_mask.data   = maskBuf.data();
        m_mask.width  = w;
        m_mask.height = h;
    }
}

void ImageBuffer::rotate270() {
    WriteLock lock(this);
    if (m_data.empty()) return;

    int oldW = m_width;
    int oldH = m_height;
    int ch   = m_channels;

    std::vector<float> newData(oldW * oldH * ch);

    #pragma omp parallel for
    for (int y = 0; y < oldH; ++y) {
        for (int x = 0; x < oldW; ++x) {
            int newX   = y;
            int newY   = oldW - 1 - x;
            int oldIdx = (y * oldW + x) * ch;
            int newIdx = (newY * oldH + newX) * ch;
            for (int c = 0; c < ch; ++c) {
                newData[newIdx + c] = m_data[oldIdx + c];
            }
        }
    }

    m_width  = oldH;
    m_height = oldW;
    m_data   = std::move(newData);

    // WCS: (x,y) -> (y, W-1-x) = Translate(0, W-1) * Rotate(270)
    QTransform t;
    t.translate(0, oldW - 1);
    t.rotate(270);
    reframeWCS(t, oldW, oldH);

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.rotate270();
        m_mask.data   = maskBuf.data();
        m_mask.width  = maskBuf.width();
        m_mask.height = maskBuf.height();
    }
}


// ============================================================================
// Geometric Operations - Mirror
// ============================================================================

void ImageBuffer::mirrorX() {
    WriteLock lock(this);
    if (m_data.empty()) return;

    int w = m_width;
    int h = m_height;

    #pragma omp parallel for
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width / 2; ++x) {
            int x2 = m_width - 1 - x;

            size_t idx1 = (y * m_width + x)  * m_channels;
            size_t idx2 = (y * m_width + x2) * m_channels;

            for (int c = 0; c < m_channels; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }

    // WCS: x' = W-1-x, y' = y = Translate(W-1, 0) * Scale(-1, 1)
    QTransform t;
    t.translate(w - 1, 0);
    t.scale(-1, 1);
    reframeWCS(t, w, h);

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(w, h, 1, m_mask.data);
        maskBuf.mirrorX();
        m_mask.data = maskBuf.data();
    }
}

void ImageBuffer::mirrorY() {
    WriteLock lock(this);
    if (m_data.empty()) return;

    int h  = m_height;
    int w  = m_width;
    int ch = m_channels;

    #pragma omp parallel for
    for (int y = 0; y < h / 2; ++y) {
        int y2 = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            int idx1 = (y  * w + x) * ch;
            int idx2 = (y2 * w + x) * ch;
            for (int c = 0; c < ch; ++c) {
                std::swap(m_data[idx1 + c], m_data[idx2 + c]);
            }
        }
    }

    // WCS: x' = x, y' = H-1-y = Translate(0, H-1) * Scale(1, -1)
    QTransform t;
    t.translate(0, h - 1);
    t.scale(1, -1);
    reframeWCS(t, w, h);

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(m_width, m_height, 1, m_mask.data);
        maskBuf.mirrorY();
        m_mask.data = maskBuf.data();
    }
}


// ============================================================================
// Geometric Operations - Binning and Resampling
// ============================================================================

void ImageBuffer::bin(int factor) {
    if (factor <= 1 || m_data.empty()) return;

    WriteLock lock(this);

    int oldW = m_width;
    int oldH = m_height;
    int ch   = m_channels;
    int newW = oldW / factor;
    int newH = oldH / factor;
    if (newW <= 0 || newH <= 0) return;

    std::vector<float> newData(
        static_cast<size_t>(newW) * newH * ch, 0.0f);
    float scale = 1.0f / (factor * factor);

    #pragma omp parallel for
    for (int y = 0; y < newH; ++y) {
        for (int x = 0; x < newW; ++x) {
            for (int c = 0; c < ch; ++c) {
                double sum = 0;
                for (int fy = 0; fy < factor; ++fy) {
                    for (int fx = 0; fx < factor; ++fx) {
                        sum += m_data[
                            ((static_cast<size_t>(y) * factor + fy) * oldW +
                             (x * factor + fx)) * ch + c];
                    }
                }
                newData[(static_cast<size_t>(y) * newW + x) * ch + c] =
                    static_cast<float>(sum * scale);
            }
        }
    }

    m_width  = newW;
    m_height = newH;
    m_data   = std::move(newData);

    // WCS: binning is a uniform scale by 1/factor.
    QTransform t;
    t.scale(1.0 / factor, 1.0 / factor);
    reframeWCS(t, oldW, oldH);

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.bin(factor);
        m_mask.data   = maskBuf.data();
        m_mask.width  = maskBuf.width();
        m_mask.height = maskBuf.height();
    }
}

void ImageBuffer::resample(int newWidth, int newHeight,
                            InterpolationMethod method) {
    if (newWidth <= 0 || newHeight <= 0 || m_data.empty()) return;

    WriteLock lock(this);

    int oldW = m_width;
    int oldH = m_height;
    int ch   = m_channels;

    // Map interpolation method to OpenCV constant.
    int interpolation = cv::INTER_CUBIC;
    switch (method) {
        case Interpolation_Nearest: interpolation = cv::INTER_NEAREST; break;
        case Interpolation_Linear:  interpolation = cv::INTER_LINEAR;  break;
        case Interpolation_Cubic:   interpolation = cv::INTER_CUBIC;   break;
        case Interpolation_Lanczos: interpolation = cv::INTER_LANCZOS4; break;
    }

    // Wrap data in an OpenCV Mat (zero-copy).
    cv::Mat src;
    if      (ch == 1) src = cv::Mat(oldH, oldW, CV_32FC1, m_data.data());
    else if (ch == 3) src = cv::Mat(oldH, oldW, CV_32FC3, m_data.data());
    else              return;

    cv::Mat dst;
    cv::resize(src, dst, cv::Size(newWidth, newHeight), 0, 0, interpolation);

    m_width  = newWidth;
    m_height = newHeight;

    // Copy resampled data back to our vector.
    size_t newSize = static_cast<size_t>(newWidth) * newHeight * ch;
    if (dst.isContinuous()) {
        m_data.assign((float*)dst.data, (float*)dst.data + newSize);
    } else {
        m_data.resize(newSize);
        for (int r = 0; r < newHeight; ++r) {
            std::copy(dst.ptr<float>(r),
                      dst.ptr<float>(r) + newWidth * ch,
                      m_data.begin() +
                          (static_cast<size_t>(r) * newWidth * ch));
        }
    }

    // WCS: resampling is a non-uniform scale.
    QTransform t;
    t.scale((double)newWidth / oldW, (double)newHeight / oldH);
    reframeWCS(t, oldW, oldH);

    if (hasMask()) {
        ImageBuffer maskBuf;
        maskBuf.setData(oldW, oldH, 1, m_mask.data);
        maskBuf.resample(newWidth, newHeight, method);
        m_mask.data   = maskBuf.data();
        m_mask.width  = maskBuf.width();
        m_mask.height = maskBuf.height();
    }
}


// ============================================================================
// Arithmetic Operations
// ============================================================================

void ImageBuffer::multiply(float factor) {
    WriteLock lock(this);
    if (m_data.empty()) return;

    #pragma omp parallel for
    for (size_t i = 0; i < m_data.size(); ++i) {
        m_data[i] = std::max(0.0f, std::min(1.0f, m_data[i] * factor));
    }
}

void ImageBuffer::subtract(float r, float g, float b) {
    WriteLock lock(this);
    if (m_data.empty()) return;

    int  ch    = m_channels;
    long total = static_cast<long>(m_width) * m_height;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        size_t idx = i * ch;
        if (ch == 1) {
            m_data[idx] = std::max(0.0f, m_data[idx] - r);
        } else {
            m_data[idx + 0] = std::max(0.0f, m_data[idx + 0] - r);
            m_data[idx + 1] = std::max(0.0f, m_data[idx + 1] - g);
            m_data[idx + 2] = std::max(0.0f, m_data[idx + 2] - b);
        }
    }
}


// ============================================================================
// Robust Statistics Accessors
// ============================================================================

float ImageBuffer::getChannelMAD(int channelIndex, float median) const {
    if (m_data.empty()) return 0.0f;

    std::vector<float> chData;
    chData.reserve(m_data.size() / m_channels);
    int ch = m_channels;
    for (size_t i = channelIndex; i < m_data.size(); i += ch) {
        chData.push_back(m_data[i]);
    }

    return RobustStatistics::getMAD(chData, median);
}

/**
 * Compute a sigma-clipped median for a single channel.
 * Pixels outside [median + t0*sigma, median + t1*sigma] are excluded,
 * then the median of the remaining samples is returned.
 */
float ImageBuffer::getRobustMedian(int channelIndex,
                                    float t0, float t1) const {
    if (m_data.empty()) return 0.0f;

    std::vector<float> chData;
    chData.reserve(m_data.size() / m_channels);
    int ch = m_channels;
    for (size_t i = channelIndex; i < m_data.size(); i += ch) {
        chData.push_back(m_data[i]);
    }

    float med   = RobustStatistics::getMedian(chData);
    float mad   = RobustStatistics::getMAD(chData, med);
    float sigma = 1.4826f * mad;

    float lower = med + t0 * sigma;
    float upper = med + t1 * sigma;

    std::vector<float> valid;
    valid.reserve(chData.size());
    for (float v : chData) {
        if (v >= lower && v <= upper) valid.push_back(v);
    }

    return RobustStatistics::getMedian(valid);
}


// ============================================================================
// Photometric Color Calibration (PCC) Application
// ============================================================================

void ImageBuffer::applyPCC(float kr, float kg, float kb,
                            float br, float bg, float bb,
                            float bg_mean) {
    if (m_data.empty() || m_channels < 3) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    // PCC formula: P' = (P - Bg) * K + Bg_Mean = P*K + (Bg_Mean - Bg*K)
    float offsetR = bg_mean - br * kr;
    float offsetG = bg_mean - bg * kg;
    float offsetB = bg_mean - bb * kb;

    long total = static_cast<long>(m_width) * m_height;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long idx = i * 3;

        float r = m_data[idx + 0] * kr + offsetR;
        float g = m_data[idx + 1] * kg + offsetG;
        float b = m_data[idx + 2] * kb + offsetB;

        m_data[idx + 0] = std::clamp(r, 0.0f, 1.0f);
        m_data[idx + 1] = std::clamp(g, 0.0f, 1.0f);
        m_data[idx + 2] = std::clamp(b, 0.0f, 1.0f);
    }

    if (hasMask()) {
        blendResult(original);
    }
}


// ============================================================================
// Cubic Spline Application
// ============================================================================

void ImageBuffer::applySpline(const SplineData& spline,
                               const bool channels[3]) {
    if (m_data.empty()) return;
    if (spline.n < 2) return;

    ImageBuffer original;
    if (hasMask()) original = *this;

    int  ch    = m_channels;
    long total = static_cast<long>(m_width) * m_height;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long idx = i * ch;
        for (int c = 0; c < ch; ++c) {
            bool apply = false;
            if      (ch == 3) apply = channels[c];
            else if (ch == 1) apply = channels[0];
            else              apply = (c < 3) ? channels[c] : true;

            if (!apply) continue;

            float v   = m_data[idx + c];
            float out = CubicSpline::interpolate(v, spline);
            m_data[idx + c] = out;
        }
    }

    if (hasMask()) {
        blendResult(original);
    }
}


// ============================================================================
// Wavelet Decomposition (A Trous Algorithm)
// ============================================================================

// B3 spline kernel: [1, 4, 6, 4, 1] / 16
static const std::vector<float> KERNEL_B3 = {
    1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f,
    4.0f / 16.0f, 1.0f / 16.0f
};

/**
 * Separable convolution with reflected boundary padding.
 * The kernel spacing doubles with each wavelet scale (a trous algorithm).
 */
std::vector<float> ImageBuffer::convolveSepReflect(
        const std::vector<float>& src, int w, int h,
        const std::vector<float>& kernel, int scale) {

    if (src.empty() || kernel.empty()) return src;

    std::vector<float> tmp(static_cast<size_t>(w) * h);
    std::vector<float> out(static_cast<size_t>(w) * h);

    int kSize  = (int)kernel.size();
    int center = kSize / 2;
    int step   = 1 << scale; // Kernel sample spacing: 2^scale

    // Horizontal pass.
    #pragma omp parallel for
    for (int y = 0; y < h; ++y) {
        long rowOff = (long)y * w;
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < kSize; ++k) {
                int offset = (k - center) * step;
                int sx = x + offset;
                // Reflect at boundaries.
                if (sx < 0)  sx = -sx;
                if (sx >= w) sx = 2 * w - 2 - sx;
                if (sx < 0)  sx = 0;
                sum += src[rowOff + sx] * kernel[k];
            }
            tmp[rowOff + x] = sum;
        }
    }

    // Vertical pass.
    #pragma omp parallel for
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            float sum = 0.0f;
            for (int k = 0; k < kSize; ++k) {
                int offset = (k - center) * step;
                int sy = y + offset;
                if (sy < 0)  sy = -sy;
                if (sy >= h) sy = 2 * h - 2 - sy;
                if (sy < 0)  sy = 0;
                sum += tmp[(long)sy * w + x] * kernel[k];
            }
            out[(long)y * w + x] = sum;
        }
    }

    return out;
}

/**
 * Decompose an image into wavelet detail planes + residual using the
 * a trous algorithm with the B3 spline kernel.
 */
std::vector<std::vector<float>> ImageBuffer::atrousDecompose(
        const std::vector<float>& src, int w, int h, int n_scales) {

    std::vector<std::vector<float>> planes;
    if (src.empty()) return planes;

    std::vector<float> current = src;

    for (int s = 0; s < n_scales; ++s) {
        std::vector<float> smooth =
            convolveSepReflect(current, w, h, KERNEL_B3, s);

        // Detail = Current - Smooth
        std::vector<float> detail(static_cast<size_t>(w) * h);
        #pragma omp parallel for
        for (size_t i = 0; i < detail.size(); ++i) {
            detail[i] = current[i] - smooth[i];
        }
        planes.push_back(detail);

        current = smooth;
    }

    // Residual (lowest-frequency component).
    planes.push_back(current);
    return planes;
}

/**
 * Reconstruct an image from its wavelet decomposition by summing all planes.
 */
std::vector<float> ImageBuffer::atrousReconstruct(
        const std::vector<std::vector<float>>& planes, int w, int h) {

    if (planes.empty()) return {};

    std::vector<float> out(static_cast<size_t>(w) * h, 0.0f);

    for (const auto& plane : planes) {
        #pragma omp parallel for
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] += plane[i];
        }
    }

    return out;
}


// ============================================================================
// Star Extraction (Connected Components)
// ============================================================================

/**
 * Extract star candidates from a single-channel image using sigma-based
 * thresholding and flood-fill connected component analysis.
 * Returns centroids, flux, shape parameters, and HFR estimates.
 */
std::vector<ImageBuffer::DetectedStar> ImageBuffer::extractStars(
        const std::vector<float>& src, int w, int h,
        float sigma, int minArea) {

    std::vector<DetectedStar> stars;
    if (src.empty()) return stars;

    // Estimate background level and noise.
    ChStats stats  = computeStats(src, w, h, 1, 0);
    float bg       = stats.median;
    float rms      = stats.mad;
    float thresholdVal = bg + sigma * rms;

    // Visited pixel tracker.
    std::vector<uint8_t> visited(static_cast<size_t>(w) * h, 0);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            long idx = (long)y * w + x;
            if (visited[idx]) continue;

            float v = src[idx];
            if (v > thresholdVal) {
                // Flood-fill to find connected above-threshold region.
                std::vector<std::pair<int, int>> stack;
                stack.push_back({x, y});
                visited[idx] = 1;

                double sumFlux = 0;
                double sumX = 0, sumY = 0;
                double sumX2 = 0, sumY2 = 0, sumXY = 0;
                float  peak  = v;
                int    count = 0;

                while (!stack.empty()) {
                    auto p = stack.back();
                    stack.pop_back();
                    int  cx   = p.first;
                    int  cy   = p.second;
                    long cidx = (long)cy * w + cx;

                    // Background-subtracted flux for proper weighting.
                    float val = src[cidx] - bg;
                    if (val < 0) val = 0;

                    if (src[cidx] > peak) peak = src[cidx];

                    sumFlux += val;
                    sumX    += cx * val;
                    sumY    += cy * val;
                    sumX2   += cx * cx * val;
                    sumY2   += cy * cy * val;
                    sumXY   += cx * cy * val;
                    count++;

                    // 4-connectivity neighbors.
                    const int dx[] = {1, -1, 0, 0};
                    const int dy[] = {0, 0, 1, -1};

                    for (int k = 0; k < 4; ++k) {
                        int nx = cx + dx[k];
                        int ny = cy + dy[k];
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            long nidx = (long)ny * w + nx;
                            if (!visited[nidx] && src[nidx] > thresholdVal) {
                                visited[nidx] = 1;
                                stack.push_back({nx, ny});
                            }
                        }
                    }
                }

                if (count >= minArea && sumFlux > 0) {
                    double mx = sumX / sumFlux;
                    double my = sumY / sumFlux;

                    // Second-order moments for shape characterization.
                    double u20 = (sumX2 / sumFlux) - (mx * mx);
                    double u02 = (sumY2 / sumFlux) - (my * my);
                    double u11 = (sumXY / sumFlux) - (mx * my);

                    // Eigenvalues of the covariance matrix.
                    double delta = std::sqrt(
                        std::abs((u20 - u02) * (u20 - u02) + 4 * u11 * u11));
                    double lam1 = (u20 + u02 + delta) / 2.0;
                    double lam2 = (u20 + u02 - delta) / 2.0;

                    float a     = (float)std::sqrt(std::max(0.0, lam1));
                    float b     = (float)std::sqrt(std::max(0.0, lam2));
                    float theta = (float)(0.5 * std::atan2(2.0 * u11,
                                                            u20 - u02));
                    float hfr   = 2.0f * a;

                    DetectedStar star;
                    star.x     = (float)mx;
                    star.y     = (float)my;
                    star.flux  = (float)sumFlux;
                    star.peak  = peak;
                    star.a     = a;
                    star.b     = b;
                    star.theta = theta;
                    star.hfr   = hfr;

                    stars.push_back(star);
                }
            }
        }
    }

    return stars;
}


// ============================================================================
// Saturation Adjustment
// ============================================================================

void ImageBuffer::applySaturation(const SaturationParams& params) {
    WriteLock lock(this);
    if (m_channels != 3 || m_data.empty()) return;

    // Capture mask state and original data for blending.
    const bool         hasM         = hasMask();
    const bool         maskInverted = m_mask.inverted;
    const bool         protectMode  = (m_mask.mode == "protect");
    const float        maskOpacity  = m_mask.opacity;
    std::vector<float> originalData;
    if (hasM)
        originalData = m_data; // Copy current pixels before modification.

    #pragma omp parallel for
    for (int i = 0; i < (int)(m_data.size() / 3); ++i) {
        float r = m_data[i * 3 + 0];
        float g = m_data[i * 3 + 1];
        float b = m_data[i * 3 + 2];

        // Cache original values for output clamping.
        float origR = r, origG = g, origB = b;

        // Compute luminance and chrominance components.
        float lum = (r + g + b) / 3.0f;
        float cr  = r - lum;
        float cg  = g - lum;
        float cb  = b - lum;

        // Approximate hue from chrominance for selective saturation.
        float hue    = 0.0f;
        float chroma = std::sqrt(cr * cr + cg * cg + cb * cb);

        if (chroma > 1e-7f) {
            hue = std::atan2(std::sqrt(3.0f) * (cg - cb),
                             2.0f * cr - cg - cb);
            hue = hue * 180.0f / 3.14159265f;
            if (hue < 0.0f) hue += 360.0f;
        }

        // Compute hue-selective weight with smooth falloff.
        float hueWeight = 1.0f;
        if (params.hueWidth < 359.0f) {
            float d = std::abs(hue - params.hueCenter);
            if (d > 180.0f) d = 360.0f - d;

            float halfWidth = params.hueWidth / 2.0f;
            if      (d <= halfWidth)                     hueWeight = 1.0f;
            else if (d >= halfWidth + params.hueSmooth)  hueWeight = 0.0f;
            else hueWeight = 1.0f - (d - halfWidth) / params.hueSmooth;
        }

        // Background luminance mask: reduce effect in dark areas.
        float lumClamped = std::max(0.0f, std::min(1.0f, lum));
        float mask       = std::pow(lumClamped, params.bgFactor);

        // Compute and apply the effective saturation boost.
        float boost = 1.0f + (params.amount - 1.0f) * mask * hueWeight;
        boost = std::max(0.0f, boost);

        cr *= boost;
        cg *= boost;
        cb *= boost;

        // Reconstruct RGB from modified chrominance.
        r = lum + cr;
        g = lum + cg;
        b = lum + cb;

        // Clamp to prevent runaway values while preserving HDR headroom.
        float maxOrig = std::max({origR, origG, origB, 1.0f});
        r = std::max(0.0f, std::min(maxOrig, r));
        g = std::max(0.0f, std::min(maxOrig, g));
        b = std::max(0.0f, std::min(maxOrig, b));

        m_data[i * 3 + 0] = r;
        m_data[i * 3 + 1] = g;
        m_data[i * 3 + 2] = b;
    }

    // Manual blending avoids recursive WriteLock calls and potential OpenMP deadlocks.
    if (hasM && !m_mask.data.empty() && !originalData.empty()) {
        long total = static_cast<long>(m_width) * m_height;
        #pragma omp parallel for schedule(static)
        for (long pi = 0; pi < total; ++pi) {
            if ((size_t)pi >= m_mask.data.size()) continue;

            float alpha = m_mask.data[pi];
            if (maskInverted) alpha = 1.0f - alpha;
            if (protectMode)  alpha = 1.0f - alpha;
            alpha *= maskOpacity;

            const float inv_alpha = 1.0f - alpha;
            size_t      base      = (size_t)pi * 3;
            for (int c = 0; c < 3; ++c) {
                m_data[base + c] = m_data[base + c] * alpha +
                                   originalData[base + c] * inv_alpha;
            }
        }
    }
    setModified(true);
}


// ============================================================================
// ArcSinh Stretch
// ============================================================================

void ImageBuffer::applyArcSinh(float stretchFactor) {
    WriteLock lock(this);
    if (m_data.empty()) return;

    // Capture mask state and original data for blending.
    const bool         hasM         = hasMask();
    const bool         maskInverted = m_mask.inverted;
    const bool         protectMode  = (m_mask.mode == "protect");
    const float        maskOpacity  = m_mask.opacity;
    std::vector<float> originalData;
    if (hasM)
        originalData = m_data; // Copy current pixels before modification.

    float norm  = (stretchFactor == 0.0f) ? 1.0f : 1.0f / std::asinh(stretchFactor);
    long  total = static_cast<long>(m_width) * m_height;
    int   ch    = m_channels;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        long idx = i * ch;
        for (int c = 0; c < ch; ++c) {
            float v   = m_data[idx + c];
            float out = (stretchFactor == 0.0f) ? v : std::asinh(v * stretchFactor) * norm;
            m_data[idx + c] = std::clamp(out, 0.0f, 1.0f);
        }
    }

    // Manual blending avoids recursive WriteLock calls and potential OpenMP deadlocks.
    if (hasM && !m_mask.data.empty() && !originalData.empty()) {
        #pragma omp parallel for schedule(static)
        for (long pi = 0; pi < total; ++pi) {
            if ((size_t)pi >= m_mask.data.size()) continue;

            float alpha = m_mask.data[pi];
            if (maskInverted)
                alpha = 1.0f - alpha;
            if (protectMode)
                alpha = 1.0f - alpha;
            alpha *= maskOpacity;

            const float inv_alpha = 1.0f - alpha;
            size_t      base      = (size_t)pi * ch;
            for (int c = 0; c < ch; ++c) {
                m_data[base + c] = m_data[base + c] * alpha +
                                   originalData[base + c] * inv_alpha;
            }
        }
    }
}

/**
 * Color-preserving ArcSinh stretch with black point subtraction.
 * Stretches luminance and applies the same scale factor to all channels
 * to preserve color ratios.
 */
void ImageBuffer::applyArcSinh(float stretchFactor, float blackPoint,
                                bool humanLuminance) {
    WriteLock lock(this);
    if (m_data.empty()) return;

    // Capture mask state and original data for blending.
    const bool         hasM         = hasMask();
    const bool         maskInverted = m_mask.inverted;
    const bool         protectMode  = (m_mask.mode == "protect");
    const float        maskOpacity  = m_mask.opacity;
    std::vector<float> originalData;
    if (hasM)
        originalData = m_data;

    long total    = static_cast<long>(m_width) * m_height;
    int  channels = m_channels;

    float asinh_beta = std::asinh(stretchFactor);
    float factor_r   = humanLuminance ? 0.2126f : 0.3333f;
    float factor_g   = humanLuminance ? 0.7152f : 0.3333f;
    float factor_b   = humanLuminance ? 0.0722f : 0.3333f;
    float offset     = blackPoint;

    #pragma omp parallel for
    for (long i = 0; i < total; ++i) {
        if (channels == 3) {
            long  idx = i * 3;
            float rv  = m_data[idx + 0];
            float gv  = m_data[idx + 1];
            float bv  = m_data[idx + 2];

            // Subtract black point and normalize.
            float rp = std::max(0.0f, (rv - offset) / (1.0f - offset));
            float gp = std::max(0.0f, (gv - offset) / (1.0f - offset));
            float bp = std::max(0.0f, (bv - offset) / (1.0f - offset));

            // Compute weighted luminance and stretch factor.
            float x = factor_r * rp + factor_g * gp + factor_b * bp;
            float k = (x <= 1e-9f)
                          ? 0.0f
                          : (stretchFactor == 0.0f
                                 ? 1.0f
                                 : std::asinh(stretchFactor * x) / (x * asinh_beta));

            m_data[idx + 0] = std::clamp(rp * k, 0.0f, 1.0f);
            m_data[idx + 1] = std::clamp(gp * k, 0.0f, 1.0f);
            m_data[idx + 2] = std::clamp(bp * k, 0.0f, 1.0f);

        } else if (channels == 1) {
            float v  = m_data[i];
            float xp = std::max(0.0f, (v - offset) / (1.0f - offset));
            float k  = (xp <= 1e-9f)
                           ? 0.0f
                           : (stretchFactor == 0.0f
                                  ? 1.0f
                                  : std::asinh(stretchFactor * xp) / (xp * asinh_beta));
            m_data[i] = std::clamp(xp * k, 0.0f, 1.0f);
        }
    }

    // Manual blending to avoid recursive lock issues.
    if (hasM && !m_mask.data.empty() && !originalData.empty()) {
        #pragma omp parallel for schedule(static)
        for (long pi = 0; pi < total; ++pi) {
            if ((size_t)pi >= m_mask.data.size()) continue;

            float alpha = m_mask.data[pi];
            if (maskInverted)
                alpha = 1.0f - alpha;
            if (protectMode)
                alpha = 1.0f - alpha;
            alpha *= maskOpacity;

            const float inv_alpha = 1.0f - alpha;
            size_t      base      = (size_t)pi * channels;
            for (int c = 0; c < channels; ++c) {
                m_data[base + c] = m_data[base + c] * alpha +
                                   originalData[base + c] * inv_alpha;
            }
        }
    }
}


// ============================================================================
// High-Quality Star Detection
// ============================================================================

std::vector<ImageBuffer::HQStar> ImageBuffer::detectStarsHQ(
        int channel) const {

    ReadLock lock(this);

    const int w  = m_width;
    const int h  = m_height;
    const int ch = m_channels;

    // Select target channel: green for color, first channel for mono.
    int targetCh = (channel < 0)
        ? (ch >= 3 ? 1 : 0)
        : std::clamp(channel, 0, ch - 1);

    // Apply Gaussian blur to reduce noise before detection.
    std::vector<float> blurred(w * h);
    StarDetector::gaussianBlur(m_data.data(), blurred.data(),
                               w, h, ch, targetCh, 1.5f);

    // Compute robust background and noise estimates.
    ChStats stats = computeStats(m_data, w, h, ch, targetCh);
    float bg      = stats.median;
    float bgnoise = stats.mad;

    const float threshold = std::min(
        std::max(bg + 5.0f * bgnoise, bg + 0.01f), 0.98f);

    std::vector<HQStar> stars;
    const int r = 3; // Local maximum search radius.

    for (int y = r; y < h - r; ++y) {
        for (int x = r; x < w - r; ++x) {
            const float pixel = blurred[y * w + x];
            if (pixel <= threshold) continue;

            // Verify strict local maximum within the search radius.
            bool isMax = true;
            for (int dy = -r; dy <= r && isMax; ++dy) {
                for (int dx = -r; dx <= r && isMax; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const float neighbor = blurred[(y + dy) * w + (x + dx)];
                    if (neighbor > pixel) {
                        isMax = false;
                    } else if (neighbor == pixel &&
                               (dx < 0 || (dx == 0 && dy < 0))) {
                        isMax = false;
                    }
                }
            }
            if (!isMax) continue;

            const int peakX = x;
            const int peakY = y;
            x += r; // Skip ahead to avoid re-detecting the same star.

            // Detect saturated plateaus.
            float minhigh8 = 1.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    float nv = blurred[(peakY + dy) * w + (peakX + dx)];
                    if (nv < minhigh8) minhigh8 = nv;
                }
            }

            bool hasSaturated = (pixel >= 0.94f) &&
                                ((pixel - minhigh8) <= 0.04f);

            int pCx = peakX, pCy = peakY;
            int plateauExtR = 0, plateauExtD = 0;

            if (hasSaturated) {
                const float satFloor = pixel - 0.04f;
                while (pCx + plateauExtR + 1 < w &&
                       blurred[pCy * w + (pCx + plateauExtR + 1)] >= satFloor)
                    ++plateauExtR;
                while (pCy + plateauExtD + 1 < h &&
                       blurred[(pCy + plateauExtD + 1) * w + pCx] >= satFloor)
                    ++plateauExtD;

                pCx = std::min(peakX + plateauExtR / 2, w - 1);
                pCy = std::min(peakY + plateauExtD / 2, h - 1);
                if (plateauExtR > r) x += (plateauExtR - r);
            }

            // Sub-pixel centroid refinement using intensity-weighted moments.
            const int refineR = 3;
            float refinedCx = 0.f, refinedCy = 0.f, totalW = 0.f;
            float peakVal = 0.f;

            for (int py = std::max(0, pCy - refineR);
                 py <= std::min(h - 1, pCy + refineR); ++py) {
                for (int px = std::max(0, pCx - refineR);
                     px <= std::min(w - 1, pCx + refineR); ++px) {
                    const float val    = m_data[(py * w + px) * ch + targetCh];
                    const float weight = val * val;
                    refinedCx += px * weight;
                    refinedCy += py * weight;
                    totalW    += weight;
                    if (val > peakVal) peakVal = val;
                }
            }

            float finalCx = (totalW > 0.f) ? refinedCx / totalW : (float)pCx;
            float finalCy = (totalW > 0.f) ? refinedCy / totalW : (float)pCy;

            // Estimate radius from the FWHM (half-maximum scan in 4 directions).
            const float halfMax = bg + (peakVal - bg) * 0.5f;
            const int   maxScan = 40;

            auto scanHalfWidth = [&](int dx, int dy) -> float {
                for (int step = 1; step <= maxScan; ++step) {
                    const int spx = std::clamp(
                        (int)std::round(finalCx) + dx * step, 0, w - 1);
                    const int spy = std::clamp(
                        (int)std::round(finalCy) + dy * step, 0, h - 1);
                    const float val =
                        m_data[(spy * w + spx) * ch + targetCh];
                    if (val < halfMax) {
                        float prevVal = m_data[
                            ((spy - dy) * w + (spx - dx)) * ch + targetCh];
                        float denom = prevVal - val;
                        return (float)(step - 1) +
                               (denom > 1e-6f
                                    ? (prevVal - halfMax) / denom
                                    : 0.5f);
                    }
                }
                return (float)maxScan;
            };

            float radius = (scanHalfWidth(1, 0)  + scanHalfWidth(-1, 0) +
                             scanHalfWidth(0, 1)  + scanHalfWidth(0, -1)) * 0.25f;
            radius = std::max(radius, 0.5f);

            // Compute normalized brightness and star color.
            float brightness = std::clamp(
                (peakVal - bg) / (1.0f - bg + 1e-6f), 0.0f, 1.0f);

            float sr = 1, sg = 1, sb = 1;
            if (ch >= 3) {
                sr = m_data[(pCy * w + pCx) * ch + 0];
                sg = m_data[(pCy * w + pCx) * ch + 1];
                sb = m_data[(pCy * w + pCx) * ch + 2];
            } else {
                sr = sg = sb = m_data[pCy * w + pCx];
            }

            stars.push_back({finalCx, finalCy, brightness, radius,
                             sr, sg, sb});
        }
    }

    // Sort by brightness (descending) and merge nearby duplicates.
    std::sort(stars.begin(), stars.end(),
              [](const HQStar& a, const HQStar& b) {
                  return a.brightness > b.brightness;
              });

    std::vector<HQStar> merged;
    for (const auto& s : stars) {
        bool dup = false;
        for (const auto& existing : merged) {
            float dx     = s.x - existing.x;
            float dy     = s.y - existing.y;
            float mergeR = (existing.radius + s.radius) * 0.9f;
            if (dx * dx + dy * dy < mergeR * mergeR) {
                dup = true;
                break;
            }
        }
        if (!dup) merged.push_back(s);
    }

    return merged;
}


// ============================================================================
// Star Mask Generation
// ============================================================================

/**
 * Generate a soft star mask from detected star positions.
 * Each star is rendered as a soft circle that fades linearly from 1.0
 * at the center to 0.0 at twice the core radius.
 */
std::vector<float> ImageBuffer::generateHQStarMask(
        const std::vector<HQStar>& stars,
        int targetW, int targetH) const {

    ReadLock lock(this);

    const int originalW = m_width;
    const int originalH = m_height;
    const int w = (targetW > 0) ? targetW : originalW;
    const int h = (targetH > 0) ? targetH : originalH;

    std::vector<float> mask(static_cast<size_t>(w) * h, 0.0f);
    if (stars.empty()) return mask;

    float scaleX    = (float)w / originalW;
    float scaleY    = (float)h / originalH;
    float meanScale = (scaleX + scaleY) / 2.0f;

    for (auto s : stars) {
        // Scale star coordinates to target resolution.
        s.x      *= scaleX;
        s.y      *= scaleY;
        s.radius *= meanScale;

        float coreR = s.radius * 2.5f;
        int x0 = std::max(0,     (int)(s.x - coreR * 2.5f));
        int x1 = std::min(w - 1, (int)(s.x + coreR * 2.5f));
        int y0 = std::max(0,     (int)(s.y - coreR * 2.5f));
        int y1 = std::min(h - 1, (int)(s.y + coreR * 2.5f));

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float dx   = x - s.x;
                float dy   = y - s.y;
                float dist = std::sqrt(dx * dx + dy * dy);

                float val = 0.0f;
                if (dist < coreR) {
                    val = 1.0f;
                } else if (dist < coreR * 2.0f) {
                    val = 1.0f - (dist - coreR) / coreR;
                }

                size_t idx = static_cast<size_t>(y) * w + x;
                mask[idx]  = std::max(mask[idx], val);
            }
        }
    }

    // Smooth the mask with a Gaussian blur for natural transitions.
    cv::Mat mat(h, w, CV_32FC1, mask.data());
    cv::Mat blurredMask;
    cv::GaussianBlur(mat, blurredMask, cv::Size(0, 0), 2.0 * meanScale);
    std::memcpy(mask.data(), blurredMask.ptr<float>(),
                mask.size() * sizeof(float));

    return mask;
}


// ============================================================================
// Serialization - Template Helpers for std::vector
// ============================================================================

template<typename T>
QDataStream& operator<<(QDataStream& out, const std::vector<T>& v) {
    out << (quint32)v.size();
    for (const auto& item : v) out << item;
    return out;
}

template<typename T>
QDataStream& operator>>(QDataStream& in, std::vector<T>& v) {
    quint32 size;
    in >> size;
    if (in.status() != QDataStream::Ok || size > 1000000000) {
        in.setStatus(QDataStream::ReadPastEnd);
        return in;
    }
    v.resize(size);
    for (quint32 i = 0; i < size; ++i) in >> v[i];
    return in;
}


// ============================================================================
// Serialization - CatalogStar
// ============================================================================

QDataStream& operator<<(QDataStream& out, const CatalogStar& star) {
    out << star.ra << star.dec << star.magB
        << star.magV << star.B_V << star.teff;
    return out;
}

QDataStream& operator>>(QDataStream& in, CatalogStar& star) {
    in >> star.ra >> star.dec >> star.magB
       >> star.magV >> star.B_V >> star.teff;
    return in;
}


// ============================================================================
// Serialization - PCCResult
// ============================================================================

QDataStream& operator<<(QDataStream& out, const PCCResult& res) {
    out << res.valid
        << res.R_factor << res.G_factor << res.B_factor
        << res.bg_r     << res.bg_g     << res.bg_b
        << res.CatRG    << res.ImgRG    << res.CatBG    << res.ImgBG
        << res.slopeRG  << res.iceptRG  << res.slopeBG  << res.iceptBG;
    return out;
}

QDataStream& operator>>(QDataStream& in, PCCResult& res) {
    in >> res.valid
       >> res.R_factor >> res.G_factor >> res.B_factor
       >> res.bg_r     >> res.bg_g     >> res.bg_b
       >> res.CatRG    >> res.ImgRG    >> res.CatBG    >> res.ImgBG
       >> res.slopeRG  >> res.iceptRG  >> res.slopeBG  >> res.iceptBG;
    return in;
}


// ============================================================================
// Serialization - HeaderCard
// ============================================================================

QDataStream& operator<<(QDataStream& out,
                         const ImageBuffer::Metadata::HeaderCard& card) {
    out << card.key << card.value << card.comment;
    return out;
}

QDataStream& operator>>(QDataStream& in,
                         ImageBuffer::Metadata::HeaderCard& card) {
    in >> card.key >> card.value >> card.comment;
    return in;
}


// ============================================================================
// Serialization - Metadata
// ============================================================================

QDataStream& operator<<(QDataStream& out,
                         const ImageBuffer::Metadata& meta) {
    out << meta.focalLength << meta.pixelSize << meta.exposure
        << meta.ra << meta.dec
        << meta.crpix1 << meta.crpix2
        << meta.cd1_1 << meta.cd1_2 << meta.cd2_1 << meta.cd2_2
        << meta.ctype1 << meta.ctype2
        << meta.equinox << meta.lonpole << meta.latpole
        << meta.sipOrderA << meta.sipOrderB
        << meta.sipOrderAP << meta.sipOrderBP << meta.sipCoeffs
        << meta.objectName << meta.dateObs << meta.filePath
        << meta.bitDepth << meta.isMono
        << (qint64)meta.stackCount << meta.ccdTemp << meta.bayerPattern;

    // Catalog stars.
    out << (quint32)meta.catalogStars.size();
    for (const auto& s : meta.catalogStars) out << s;

    out << meta.pccResult;

    // Raw FITS headers.
    out << (quint32)meta.rawHeaders.size();
    for (const auto& c : meta.rawHeaders) out << c;

    // Color profile data.
    out << meta.xisfProperties;
    out << meta.iccData;
    out << meta.iccProfileName;
    out << meta.iccProfileType;
    out << meta.colorProfileHandled;

    return out;
}

QDataStream& operator>>(QDataStream& in, ImageBuffer::Metadata& meta) {
    in >> meta.focalLength >> meta.pixelSize >> meta.exposure
       >> meta.ra >> meta.dec
       >> meta.crpix1 >> meta.crpix2
       >> meta.cd1_1 >> meta.cd1_2 >> meta.cd2_1 >> meta.cd2_2
       >> meta.ctype1 >> meta.ctype2
       >> meta.equinox >> meta.lonpole >> meta.latpole
       >> meta.sipOrderA >> meta.sipOrderB
       >> meta.sipOrderAP >> meta.sipOrderBP >> meta.sipCoeffs
       >> meta.objectName >> meta.dateObs >> meta.filePath
       >> meta.bitDepth >> meta.isMono;

    qint64 sc;
    in >> sc;
    meta.stackCount = sc;

    in >> meta.ccdTemp >> meta.bayerPattern;

    // Catalog stars.
    quint32 starSize;
    in >> starSize;
    meta.catalogStars.resize(starSize);
    for (quint32 i = 0; i < starSize; ++i) in >> meta.catalogStars[i];

    in >> meta.pccResult;

    // Raw FITS headers.
    quint32 cardSize;
    in >> cardSize;
    meta.rawHeaders.resize(cardSize);
    for (quint32 i = 0; i < cardSize; ++i) in >> meta.rawHeaders[i];

    in >> meta.xisfProperties;
    in >> meta.iccData;

    // Backward-compatible deserialization for newer fields.
    if (in.status() == QDataStream::Ok && in.device()->bytesAvailable()) {
        in >> meta.iccProfileName;
        if (in.device()->bytesAvailable()) {
            in >> meta.iccProfileType;
        } else {
            meta.iccProfileType = -1;
        }
        if (in.device()->bytesAvailable()) {
            in >> meta.colorProfileHandled;
        } else {
            meta.colorProfileHandled = false;
        }
    } else {
        meta.iccProfileName.clear();
        meta.iccProfileType      = -1;
        meta.colorProfileHandled = false;
    }

    return in;
}


// ============================================================================
// Serialization - MaskLayer
// ============================================================================

QDataStream& operator<<(QDataStream& out, const MaskLayer& mask) {
    out << mask.data << mask.width << mask.height
        << mask.id << mask.name
        << mask.mode << mask.inverted << mask.visible << mask.opacity;
    return out;
}

QDataStream& operator>>(QDataStream& in, MaskLayer& mask) {
    in >> mask.data >> mask.width >> mask.height
       >> mask.id >> mask.name
       >> mask.mode >> mask.inverted >> mask.visible >> mask.opacity;
    return in;
}


// ============================================================================
// Serialization - ImageBuffer (Binary Project Format)
// ============================================================================

QDataStream& operator<<(QDataStream& out, const ImageBuffer& buffer) {
    ImageBuffer::ReadLock lock(&buffer);

    // Write magic signature and format version.
    out.writeRawData("TSNP", 4);
    out << (qint32)1;

    // Image dimensions.
    out << (qint32)buffer.m_width
        << (qint32)buffer.m_height
        << (qint32)buffer.m_channels;

    // Pixel data as raw bytes for maximum I/O throughput.
    quint64 totalFloats = (quint64)buffer.m_data.size();
    out << totalFloats;
    if (totalFloats > 0) {
        out.writeRawData(
            reinterpret_cast<const char*>(buffer.m_data.data()),
            totalFloats * sizeof(float));
    }

    // Metadata and state.
    out << buffer.m_meta;
    out << buffer.m_name << buffer.m_modified << buffer.m_hasMask;
    if (buffer.m_hasMask) out << buffer.m_mask;

    return out;
}

QDataStream& operator>>(QDataStream& in, ImageBuffer& buffer) {
    ImageBuffer::WriteLock lock(&buffer);

    // Validate magic signature.
    char magic[4];
    if (in.readRawData(magic, 4) != 4 ||
        std::memcmp(magic, "TSNP", 4) != 0) {
        in.setStatus(QDataStream::ReadPastEnd);
        return in;
    }

    qint32 version;
    in >> version;

    // Read image dimensions.
    qint32 w, h, ch;
    in >> w >> h >> ch;
    buffer.m_width    = w;
    buffer.m_height   = h;
    buffer.m_channels = ch;

    // Read pixel data.
    quint64 totalFloats;
    in >> totalFloats;
    buffer.m_data.resize(totalFloats);
    if (totalFloats > 0) {
        in.readRawData(
            reinterpret_cast<char*>(buffer.m_data.data()),
            totalFloats * sizeof(float));
    }

    // Read metadata and state.
    in >> buffer.m_meta;
    in >> buffer.m_name >> buffer.m_modified >> buffer.m_hasMask;
    if (buffer.m_hasMask) in >> buffer.m_mask;

    return in;
}