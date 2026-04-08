// =============================================================================
// ImageBuffer.h
//
// Core image data container for TStar. Stores interleaved 32-bit floating-point
// pixel data (range 0.0 - 1.0) along with FITS/XISF metadata, WCS astrometry,
// mask layers, and history snapshots. Provides thread-safe access via RAII
// read/write locks and supports memory-pressure-driven swap-to-disk.
// =============================================================================

#ifndef IMAGEBUFFER_H
#define IMAGEBUFFER_H

// ---- Standard library -------------------------------------------------------
#include <vector>
#include <memory>

// ---- Qt core ----------------------------------------------------------------
#include <QImage>
#include <QString>
#include <QMap>
#include <QVariant>
#include <QMutex>
#include <QReadWriteLock>
#include <QDebug>
#include <QTransform>
#include <QDateTime>
#include <QDataStream>

// ---- Project headers --------------------------------------------------------
#include "photometry/CatalogClient.h"
#include "photometry/PCCResult.h"
#include "algos/CubicSpline.h"
#include "core/MaskLayer.h"

// =============================================================================
// ImageBuffer class
// =============================================================================

class ImageBuffer {
public:
    // ---- Construction / destruction -----------------------------------------

    ImageBuffer();
    ImageBuffer(int width, int height, int channels);
    ~ImageBuffer();

    // Custom copy constructor/assignment (mutex is not copyable)
    ImageBuffer(const ImageBuffer& other);
    ImageBuffer& operator=(const ImageBuffer& other);

    // Move semantics (defaulted)
    ImageBuffer(ImageBuffer&& other) noexcept = default;
    ImageBuffer& operator=(ImageBuffer&& other) noexcept = default;

    // ---- Data access --------------------------------------------------------

    /// Set pixel data, replacing current contents.
    void setData(int width, int height, int channels, const std::vector<float>& data);

    /// Resize the buffer, discarding existing pixel data.
    void resize(int width, int height, int channels);

    /// Access the raw interleaved pixel data.
    /// IMPORTANT: Calling data() will trigger a swap-in if the buffer has been
    /// paged out to disk. Prefer using ReadLock/WriteLock RAII guards instead.
    const std::vector<float>& data() const;
    std::vector<float>& data();

    /// Mark this buffer as recently accessed without reading data.
    void touch();

    /// Timestamp of last access, used by the swap manager for LRU eviction.
    qint64 getLastAccessTime() const { return m_lastAccess; }

    // ---- Dimension accessors ------------------------------------------------

    int width()    const { Q_ASSERT(m_width > 0);    return m_width; }
    int height()   const { Q_ASSERT(m_height > 0);   return m_height; }
    int channels() const { Q_ASSERT(m_channels > 0); return m_channels; }

    size_t size() const {
        return static_cast<size_t>(m_width) * m_height * m_channels;
    }

    bool isValid() const {
        return !m_data.empty() && m_width > 0 && m_height > 0;
    }

    // ---- Pixel value access -------------------------------------------------

    float getPixelValue(int x, int y, int c) const;

    float value(int x, int y, int c = 0) const {
        return getPixelValue(x, y, c);
    }

    float& value(int x, int y, int c = 0) {
        return m_data[(static_cast<size_t>(y) * m_width + x) * m_channels + c];
    }

    float getPixelFlat(size_t index, int c) const;
    float getAreaMean(int x, int y, int w, int h, int c) const;

    // ---- Swap management (disk paging) --------------------------------------

    bool isSwapped() const { return m_isSwapped; }

    /// Returns true if the buffer is eligible for swap-out.
    bool canSwap() const;

    /// Attempt to page pixel data out to a temporary file. Called by SwapManager.
    bool trySwapOut();

    /// Page pixel data back into memory from the temporary file.
    void forceSwapIn();

    // ---- File I/O -----------------------------------------------------------

    bool loadStandard(const QString& filePath);

    /// Load a rectangular sub-region from a file.
    bool loadRegion(const QString& filePath, int x, int y, int w, int h,
                    QString* errorMsg = nullptr);

    /// Retrieve a FITS/XISF header value by keyword.
    QString getHeaderValue(const QString& key) const;

    // ---- Enumerations -------------------------------------------------------

    /// Bit depth for file output.
    enum BitDepth {
        Depth_8Int,
        Depth_16Int,
        Depth_32Int,
        Depth_32Float
    };

    /// Display transfer function applied during visualization.
    enum DisplayMode {
        Display_Linear,
        Display_AutoStretch,
        Display_ArcSinh,
        Display_Sqrt,
        Display_Log,
        Display_Histogram
    };

    /// Channel isolation for display.
    enum ChannelView {
        ChannelRGB = 0,
        ChannelR,
        ChannelG,
        ChannelB
    };

    // ---- Display image generation -------------------------------------------

    /// Generate a QImage suitable for on-screen display.
    /// @param mode           Transfer function to apply.
    /// @param linked         If true, compute stretch stats from combined channels.
    /// @param overrideLUT    Optional pre-computed LUT (3 channels x 65536 entries).
    /// @param maxWidth       Downsample limit (0 = no limit).
    /// @param maxHeight      Downsample limit (0 = no limit).
    /// @param showMask       Overlay the mask layer on the display image.
    /// @param inverted       Invert the output luminance.
    /// @param falseColor     Apply a false-color palette.
    /// @param autoStretchTargetMedian  Target median brightness for AutoStretch.
    /// @param channelView    Which channel(s) to display.
    QImage getDisplayImage(
        DisplayMode mode                  = Display_Linear,
        bool linked                       = true,
        const std::vector<std::vector<float>>* overrideLUT = nullptr,
        int maxWidth                      = 0,
        int maxHeight                     = 0,
        bool showMask                     = false,
        bool inverted                     = false,
        bool falseColor                   = false,
        float autoStretchTargetMedian     = 0.25f,
        ChannelView channelView           = ChannelRGB
    ) const;

    // ---- Auto-stretch parameters --------------------------------------------

    /// Screen-transfer-function parameters computed with the same statistics
    /// as getDisplayImage(Display_AutoStretch). Useful for keeping dialog
    /// previews pixel-identical to the live display.
    struct STFParams {
        float shadows    = 0.0f;
        float midtones   = 0.25f;
        float highlights = 1.0f;
    };

    STFParams computeAutoStretchParams(
        bool linked                     = true,
        float targetMedian              = 0.25f,
        const std::vector<bool>& activeChannels = {}
    ) const;

    // ---- Mask support -------------------------------------------------------

    void setMask(const MaskLayer& mask);
    const MaskLayer* getMask() const;
    bool hasMask() const;
    void removeMask();
    void invertMask();

    // ---- File save / load ---------------------------------------------------

    bool save(const QString& filePath, const QString& format, BitDepth depth,
              QString* errorMsg = nullptr) const;

    bool loadTiff32(const QString& filePath,
                    QString* errorMsg = nullptr, QString* debugInfo = nullptr);

    bool loadXISF(const QString& filePath, QString* errorMsg = nullptr);
    bool saveXISF(const QString& filePath, BitDepth depth,
                  QString* errorMsg = nullptr) const;

    // ---- Stretch parameters -------------------------------------------------

    struct StretchParams {
        float targetMedian      = 0.25f;    ///< Target median brightness [0..1]
        bool  linked            = true;     ///< Link channels (use combined stats)
        bool  normalize         = false;    ///< Normalize to max after stretch
        bool  applyCurves       = false;    ///< Apply S-curve boost
        float curvesBoost       = 0.0f;    ///< Curves strength [0..1]

        // Black point control
        float blackpointSigma   = 5.0f;    ///< Sigma multiplier for black point
        bool  noBlackClip       = false;   ///< Disable black clipping (use data min)

        // HDR highlight compression
        bool  hdrCompress       = false;   ///< Enable HDR highlight compression
        float hdrAmount         = 0.0f;    ///< Compression strength [0..1]
        float hdrKnee           = 0.75f;   ///< Knee point (compression onset)

        // Luminance-only stretch
        bool  lumaOnly          = false;   ///< Stretch luminance only (preserve hue)
        int   lumaMode          = 0;       ///< 0=Rec.709, 1=Rec.601, 2=Rec.2020

        // High dynamic range mode
        bool  highRange             = false;   ///< Enable high-range rescaling
        float hrPedestal            = 0.001f;  ///< Minimum output value
        float hrSoftCeilPct         = 99.0f;   ///< Soft ceiling percentile
        float hrHardCeilPct         = 99.99f;  ///< Hard ceiling percentile
        float hrSoftclipThreshold   = 0.98f;   ///< Softclip start point
        float hrSoftclipRolloff     = 2.0f;    ///< Softclip rolloff strength
    };

    // ---- Stretch operations -------------------------------------------------

    /// Apply a permanent midtone-transfer stretch to the pixel data.
    void performTrueStretch(const StretchParams& params);

    /// Bake the current display transformation into pixel data at full precision.
    void applyDisplayTransform(
        DisplayMode mode    = Display_Linear,
        bool linked         = true,
        float targetMedian  = 0.25f,
        bool inverted       = false,
        bool falseColor     = false
    );

    /// Luminance-only stretch that preserves chromaticity.
    void performLumaOnlyStretch(const StretchParams& params);

    /// Compute a preview LUT for TrueStretch (3 channels x @p size entries).
    std::vector<std::vector<float>> computeTrueStretchLUT(
        const StretchParams& params, int size = 65536
    ) const;

    // ---- Geometric operations -----------------------------------------------

    void crop(int x, int y, int w, int h);
    void rotate(float angleDegrees);  ///< Positive = clockwise

    /// Crop with sub-pixel precision and rotation.
    void cropRotated(float cx, float cy, float w, float h, float angleDegrees);

    void rotate90();    ///< 90 degrees clockwise
    void rotate180();
    void rotate270();   ///< 90 degrees counter-clockwise
    void mirrorX();     ///< Horizontal flip
    void mirrorY();     ///< Vertical flip

    /// Interpolation method for resampling operations.
    enum InterpolationMethod {
        Interpolation_Nearest,
        Interpolation_Linear,
        Interpolation_Cubic,
        Interpolation_Lanczos
    };

    void bin(int factor);
    void resample(int newWidth, int newHeight,
                  InterpolationMethod method = Interpolation_Cubic);

    // ---- WCS (World Coordinate System) --------------------------------------

    /// Update CRPIX and CD matrix based on a linear geometric transform.
    void reframeWCS(const QTransform& trans, int oldWidth, int oldHeight);

    /// Synchronize in-memory WCS struct values back to the rawHeaders vector.
    void syncWcsToHeaders();

    // ---- Math / color operations --------------------------------------------

    void multiply(float factor);

    /// Blend processed data (this) with original data using the mask.
    /// result = processed * mask + original * (1 - mask).
    void blendResult(const ImageBuffer& original, bool inverseMask = false);

    /// Apply per-channel white balance multipliers.
    /// @param lumaProtected  Blend shadows/highlights toward neutral to avoid
    ///                       colour casts in near-black/near-white regions.
    void applyWhiteBalance(float r, float g, float b, bool lumaProtected = false);

    /// Subtract per-channel offsets, clamping to zero.
    void subtract(float r, float g, float b);

    /// Apply Photometric Color Calibration coefficients.
    void applyPCC(float kr, float kg, float kb,
                  float br, float bg, float bb, float bg_mean);

    // ---- Generalized Hyperbolic Stretch (GHS) -------------------------------

    enum GHSMode {
        GHS_GeneralizedHyperbolic,
        GHS_InverseGeneralizedHyperbolic,
        GHS_Linear,
        GHS_ArcSinh,
        GHS_InverseArcSinh
    };

    enum GHSColorMode {
        GHS_Independent,
        GHS_WeightedLuminance,
        GHS_EvenWeightedLuminance,
        GHS_Saturation
    };

    enum GHSClipMode {
        GHS_Clip,
        GHS_Rescale,
        GHS_ClipRGBBlend,
        GHS_RescaleGlobal
    };

    struct GHSParams {
        double D          = 0.0;    ///< Stretch factor
        double B          = 0.5;    ///< Local intensity
        double SP         = 0.0;    ///< Symmetry point
        double LP         = 0.0;    ///< Shadow protection
        double HP         = 1.0;    ///< Highlight protection
        double BP         = 0.0;    ///< Black point
        GHSMode      mode      = GHS_GeneralizedHyperbolic;
        GHSColorMode colorMode = GHS_Independent;
        GHSClipMode  clipMode  = GHS_Clip;
        bool inverse       = false;
        bool applyLog      = false;
        bool channels[3]   = {true, true, true};  ///< Per-channel enable (R, G, B)
    };

    void applyGHS(const GHSParams& params);
    std::vector<float> computeGHSLUT(const GHSParams& params,
                                     int size = 65536) const;

    // ---- Curves (spline transformation) -------------------------------------

    void applySpline(const SplineData& spline, const bool channels[3]);

    // ---- SCNR (Subtractive Chromatic Noise Reduction) -----------------------

    void applySCNR(float amount, int method);

    // ---- Magenta correction -------------------------------------------------

    /// @param mod_b      Correction strength: 0.0 = maximum, 1.0 = none.
    /// @param threshold  Luminance threshold below which correction is applied.
    /// @param withStarMask  Protect stars from correction using a generated mask.
    void applyMagentaCorrection(float mod_b, float threshold, bool withStarMask);

    // ---- Color saturation ---------------------------------------------------

    struct SaturationParams {
        float amount    = 1.0f;      ///< 1.0 = no change, >1.0 = boost
        float bgFactor  = 1.0f;      ///< Power factor for intensity masking
        float hueCenter = 0.0f;      ///< Hue center [0..360]
        float hueWidth  = 360.0f;    ///< Hue range [0..360]
        float hueSmooth = 30.0f;     ///< Transition width in degrees
    };

    void applySaturation(const SaturationParams& params);

    // ---- ArcSinh stretch ----------------------------------------------------

    void applyArcSinh(float stretchFactor);
    void applyArcSinh(float stretchFactor, float blackPoint, bool humanLuminance);

    // ---- Statistics ---------------------------------------------------------

    float getChannelMedian(int channelIndex) const;
    float getChannelMAD(int channelIndex, float median) const;
    float getRobustMedian(int channelIndex, float t0, float t1) const;

    /// Compute the number of pixels at or below 0 and at or above 1.
    void computeClippingStats(long& lowClip, long& highClip) const;

    /// Compute per-channel histogram with the specified number of bins.
    std::vector<std::vector<int>> computeHistogram(int bins = 256) const;

    // ---- Metadata -----------------------------------------------------------

    struct Metadata {
        // Basic acquisition parameters
        double focalLength = 0;
        double pixelSize   = 0;
        double exposure    = 0;
        double ra          = 0;
        double dec         = 0;

        // WCS linear transform
        double crpix1 = 0, crpix2 = 0;
        double cd1_1  = 0, cd1_2  = 0;
        double cd2_1  = 0, cd2_2  = 0;

        // WCS projection parameters
        QString ctype1;              ///< e.g. "RA---TAN", "RA---TAN-SIP"
        QString ctype2;              ///< e.g. "DEC--TAN", "DEC--TAN-SIP"
        double  equinox  = 2000.0;
        double  lonpole  = 180.0;
        double  latpole  = 0.0;

        // SIP distortion coefficients
        int sipOrderA  = 0;
        int sipOrderB  = 0;
        int sipOrderAP = 0;
        int sipOrderBP = 0;
        QMap<QString, double> sipCoeffs;  ///< e.g. "A_1_0" -> value

        // Descriptive fields
        QString objectName;
        QString dateObs;
        QString filePath;            ///< Source file path for reference
        QString bitDepth;            ///< Original bit depth description
        bool    isMono     = false;  ///< True if originally a mono image
        int64_t stackCount = 0;      ///< Number of images combined
        double  ccdTemp    = 0.0;
        QString bayerPattern;        ///< e.g. "RGGB", "GBRG"

        // Persisted catalog stars for Photometric Color Calibration
        std::vector<CatalogStar> catalogStars;
        PCCResult pccResult;

        // Raw header storage (key, value, comment triples)
        struct HeaderCard {
            QString key;
            QString value;
            QString comment;
        };
        std::vector<HeaderCard> rawHeaders;

        // XISF properties and embedded ICC profile
        QVariantMap xisfProperties;
        QByteArray  iccData;

        // Color profile management
        QString iccProfileName;              ///< Human-readable name (e.g. "sRGB")
        int     iccProfileType      = -1;    ///< StandardProfile enum (-1 = custom/none)
        bool    colorProfileHandled = false;  ///< Prevents repeated mismatch warnings

        // Serialization support
        friend QDataStream& operator<<(QDataStream& out, const Metadata& meta);
        friend QDataStream& operator>>(QDataStream& in, Metadata& meta);
    };

    void            setMetadata(const Metadata& meta) { m_meta = meta; }
    const Metadata& metadata() const { return m_meta; }
    Metadata&       metadata()       { return m_meta; }

    // ---- Name tracking ------------------------------------------------------

    void    setName(const QString& name) { m_name = name; }
    QString name() const { return m_name; }

    // ---- Modification state -------------------------------------------------

    bool isModified() const               { return m_modified; }
    void setModified(bool modified)       { m_modified = modified; }

    /// Append a HISTORY record to the image metadata.
    void addHistoryAction(const QString& action);

    /// Append all HISTORY records from another metadata object.
    void mergeHistory(const Metadata& other);

    // ---- Metadata pruning ---------------------------------------------------

    /// Remove catalog stars that fall outside the current image bounds.
    void filterCatalogStars();

    // ---- Serialization (snapshot persistence) -------------------------------

    friend QDataStream& operator<<(QDataStream& out, const ImageBuffer& buffer);
    friend QDataStream& operator>>(QDataStream& in, ImageBuffer& buffer);

    // ---- Star detection & masking -------------------------------------------

    /// Basic star detection result.
    struct DetectedStar {
        float x, y;
        float flux;
        float peak;
        float a, b, theta;  ///< Ellipse semi-axes and position angle
        float hfr;           ///< Half-flux radius
    };

    /// High-quality star detection result (DAOFIND-style).
    struct HQStar {
        float x, y;
        float brightness;    ///< Relative brightness [0..1]
        float radius;        ///< FWHM-based radius in pixels
        float r, g, b;       ///< Average color in the star core
    };

    /// Extract stars from a single-channel buffer.
    /// @param sigma   Detection threshold in units of background RMS.
    /// @param minArea Minimum connected-pixel area for a valid detection.
    static std::vector<DetectedStar> extractStars(
        const std::vector<float>& src, int w, int h,
        float sigma = 3.0f, int minArea = 5
    );

    /// High-quality star detection using a refined DAOFIND-style algorithm.
    /// @param channel  -1 = luminance, 0-2 = specific R/G/B channel.
    std::vector<HQStar> detectStarsHQ(int channel = -1) const;

    /// Generate a smooth star mask from detected stars.
    /// Returns a flat float buffer (0.0 to 1.0) of size targetW * targetH.
    std::vector<float> generateHQStarMask(
        const std::vector<HQStar>& stars,
        int targetW = -1, int targetH = -1
    ) const;

    // ---- Wavelet utilities --------------------------------------------------

    /// Separable convolution with reflect-boundary padding and optional
    /// dilation (a-trous algorithm).
    static std::vector<float> convolveSepReflect(
        const std::vector<float>& src, int w, int h,
        const std::vector<float>& kernel, int scale = 0
    );

    /// A-trous wavelet decomposition into detail planes and a residual.
    static std::vector<std::vector<float>> atrousDecompose(
        const std::vector<float>& src, int w, int h, int n_scales
    );

    /// Reconstruct a signal from its wavelet detail planes and residual.
    static std::vector<float> atrousReconstruct(
        const std::vector<std::vector<float>>& planes, int w, int h
    );

    // ---- Thread-safety (read/write locking) ---------------------------------

    void lockRead() const {
        if (!m_mutex)
            m_mutex = std::make_unique<QReadWriteLock>(QReadWriteLock::Recursive);
        m_mutex->lockForRead();
    }

    void lockWrite() {
        if (!m_mutex)
            m_mutex = std::make_unique<QReadWriteLock>(QReadWriteLock::Recursive);
        m_mutex->lockForWrite();
    }

    void unlock() const {
        if (m_mutex) m_mutex->unlock();
    }

    // ---- RAII lock guards ---------------------------------------------------

    /// Acquires a read lock and ensures the buffer is swapped in.
    /// On destruction, automatically releases the lock.
    class ReadLock {
    public:
        explicit ReadLock(const ImageBuffer* buf) : m_buf(buf) {
            if (!m_buf) return;
            auto* mutable_buf = const_cast<ImageBuffer*>(m_buf);

            // Retry loop to avoid a TOCTOU race between swap-in and lock
            // acquisition. SwapManager could re-swap the buffer between
            // forceSwapIn() and lockRead(). Holding the read lock first
            // prevents further swap-outs; if data is still swapped, we
            // drop to a write lock, swap in, and retry. In practice this
            // loop executes at most twice.
            while (true) {
                m_buf->lockRead();
                if (!m_buf->m_isSwapped) return;

                // Cannot swap in under a read lock (would need write).
                m_buf->unlock();
                mutable_buf->lockWrite();
                if (m_buf->m_isSwapped) mutable_buf->doSwapIn();
                mutable_buf->m_mutex->unlock();
            }
        }

        ~ReadLock() { if (m_buf) m_buf->unlock(); }

        ReadLock(const ReadLock&) = delete;
        ReadLock& operator=(const ReadLock&) = delete;

    private:
        const ImageBuffer* m_buf;
    };

    /// Acquires a write lock and ensures the buffer is swapped in.
    class WriteLock {
    public:
        explicit WriteLock(ImageBuffer* buf) : m_buf(buf) {
            if (m_buf) {
                m_buf->lockWrite();
                if (m_buf->m_isSwapped) m_buf->doSwapIn();
            }
        }

        ~WriteLock() { if (m_buf) m_buf->unlock(); }

        WriteLock(const WriteLock&) = delete;
        WriteLock& operator=(const WriteLock&) = delete;

    private:
        ImageBuffer* m_buf;
    };

    // =========================================================================
    // Private implementation
    // =========================================================================

private:
    // ---- Internal helpers ---------------------------------------------------

    void filterCatalogStarsInternal();
    static void rgbToHsv(float r, float g, float b, float& h, float& s, float& v);

    /// Compute a per-channel agile auto-stretch LUT (display-only path).
    std::vector<float> computeAgileLUT(int channelIndex, float targetMedian = 0.25f);

    // ---- Swap internals -----------------------------------------------------

    void doSwapIn();
    void doSwapOut();

    // ---- Data members -------------------------------------------------------

    int m_width    = 0;
    int m_height   = 0;
    int m_channels = 1;

    std::vector<float> m_data;   ///< Interleaved 32-bit float pixels [0.0 .. 1.0]
    Metadata           m_meta;
    QString            m_name;
    bool               m_modified = false;

    // Mask layer
    MaskLayer m_mask;
    bool      m_hasMask = false;

    // Thread safety: multiple readers / exclusive writer
    mutable std::unique_ptr<QReadWriteLock> m_mutex;

    // Disk-swap state
    mutable qint64 m_lastAccess = 0;
    bool           m_isSwapped  = false;
    QString        m_swapFile;
};

#endif // IMAGEBUFFER_H