#ifndef IMAGEBUFFER_H
#define IMAGEBUFFER_H

#include <vector>
#include <QImage>
#include <QString>
#include <QMap>
#include <QVariant>
#include <QMutex>
#include <QReadWriteLock>
#include <QDebug>
#include "photometry/CatalogClient.h"
#include "photometry/PCCResult.h"
#include "algos/CubicSpline.h"
#include "core/MaskLayer.h"
#include <QTransform>

class ImageBuffer {
public:
    ImageBuffer();
    ImageBuffer(int width, int height, int channels);
    ~ImageBuffer();
    
    // Custom copy to handle non-copyable mutex
    ImageBuffer(const ImageBuffer& other);
    ImageBuffer& operator=(const ImageBuffer& other);

    // Move is allowed
    ImageBuffer(ImageBuffer&& other) noexcept = default;
    ImageBuffer& operator=(ImageBuffer&& other) noexcept = default;

    // WCS Reframing Helper (Centralized logic for Geometry Ops)
    // Updates CRPIX and CD Matrix based on a linear transform
    void reframeWCS(const QTransform& trans, int oldWidth, int oldHeight);

    void setData(int width, int height, int channels, const std::vector<float>& data);
    void resize(int width, int height, int channels);
    
    // Raw Access
    const std::vector<float>& data() const { Q_ASSERT(!m_data.empty()); return m_data; }
    std::vector<float>& data() { Q_ASSERT(!m_data.empty()); return m_data; }
    
    bool loadStandard(const QString& filePath);
    
    int width() const { Q_ASSERT(m_width > 0); return m_width; }
    int height() const { Q_ASSERT(m_height > 0); return m_height; }
    
    /**
     * @brief Load a rectangular region of the image
     */
    bool loadRegion(const QString& filePath, int x, int y, int w, int h, QString* errorMsg = nullptr);
    
    // Header Access
    QString getHeaderValue(const QString& key) const;

    enum BitDepth { Depth_8Int, Depth_16Int, Depth_32Int, Depth_32Float };
    enum DisplayMode { Display_Linear, Display_AutoStretch, Display_ArcSinh, Display_Sqrt, Display_Log, Display_Histogram };
    int channels() const { Q_ASSERT(m_channels > 0); return m_channels; }
    size_t size() const { return static_cast<size_t>(m_width) * m_height * m_channels; }
    bool isValid() const { return !m_data.empty() && m_width > 0 && m_height > 0; }

    // Display
    // Replaces the boolean autostretch with mode and link option
    // Optional overrideLUT: if provided (size 3x65536), it is used instead of internal logic.
    QImage getDisplayImage(DisplayMode mode = Display_Linear, bool linked = true, const std::vector<std::vector<float>>* overrideLUT = nullptr, int maxWidth = 0, int maxHeight = 0, bool showMask = false, bool inverted = false, bool falseColor = false) const; 

    // Mask Support
    void setMask(const MaskLayer& mask);
    const MaskLayer* getMask() const;
    bool hasMask() const;
    void removeMask();
    void invertMask();

    // Saving and Processing
    // The original BitDepth enum was here, now it's moved up.
    // enum BitDepth {
    //     Depth_32Float,
    //     Depth_32Int,
    //     Depth_16Int,
    //     Depth_8Int
    // };

    bool save(const QString& filePath, const QString& format, BitDepth depth, QString* errorMsg = nullptr) const;
    bool loadTiff32(const QString& filePath, QString* errorMsg = nullptr, QString* debugInfo = nullptr);

    struct StretchParams {
        float targetMedian = 0.25f;       // Target median brightness (0-1)
        bool linked = true;               // Link channels (same stats for all)
        bool normalize = false;           // Normalize to max after stretch
        bool applyCurves = false;         // Apply S-curve boost
        float curvesBoost = 0.0f;         // Curves strength (0-1)
        
        // Black Point Control
        float blackpointSigma = 5.0f;     // Sigma for black point calculation
        bool noBlackClip = false;         // Disable black clipping (use min instead)
        
        // HDR Highlight Compression
        bool hdrCompress = false;         // Enable HDR highlight compression
        float hdrAmount = 0.0f;           // HDR compression strength (0-1)
        float hdrKnee = 0.75f;            // HDR knee point (where compression starts)
        
        // Luminance-Only Stretch
        bool lumaOnly = false;            // Stretch luminance only (preserve colors)
        int lumaMode = 0;                 // 0=Rec709, 1=Rec601, 2=Rec2020
        
        // High Dynamic Range Mode (VeraLux-style)
        bool highRange = false;           // Enable high-range rescaling
        float hrPedestal = 0.001f;        // Minimum output value
        float hrSoftCeilPct = 99.0f;      // Soft ceiling percentile
        float hrHardCeilPct = 99.99f;     // Hard ceiling percentile
        float hrSoftclipThreshold = 0.98f; // Softclip start point
        float hrSoftclipRolloff = 2.0f;   // Softclip rolloff strength
    };
    
    
    // XISF Support
    bool loadXISF(const QString& filePath, QString* errorMsg = nullptr);
    bool saveXISF(const QString& filePath, BitDepth depth, QString* errorMsg = nullptr) const;

    // Geometric Ops
    void crop(int x, int y, int w, int h);
    void rotate(float angleDegrees); // Positive = Clockwise
    
    // Interactive Crop with Subpixel & Rotation
    void cropRotated(float cx, float cy, float w, float h, float angleDegrees);

    // New Geometry Tools
    void rotate90();   // CW
    void rotate180();
    void rotate270();  // CW (90 CCW)
    void mirrorX();    // Horizontal Flip
    void mirrorY();    // Vertical Flip

    // Apply permanent stretch to m_data
    void performTrueStretch(const StretchParams& params);
    
    // Luminance-only stretch (preserves colors)
    void performLumaOnlyStretch(const StretchParams& params);
    
    // Compute LUT for TrueStretch (for Preview)
    std::vector<std::vector<float>> computeTrueStretchLUT(const StretchParams& params, int size = 65536) const;

    // Math Ops
    void multiply(float factor);
    // Applies a mask-aware blend between processed data (this) and original data.
    // If mask exists, result = processed * mask + original * (1-mask).
    void blendResult(const ImageBuffer& original, bool inverseMask = false);

    void applyWhiteBalance(float r, float g, float b);
    void subtract(float r, float g, float b); // Subtract offsets per channel (clamped to 0)
    void applyPCC(float kr, float kg, float kb, float br, float bg, float bb, float bg_mean); // Standard application

    // GHS Parameters
    // GHS Parameters
    enum GHSMode { GHS_GeneralizedHyperbolic, GHS_InverseGeneralizedHyperbolic, GHS_Linear, GHS_ArcSinh, GHS_InverseArcSinh };
    enum GHSColorMode { GHS_Independent, GHS_WeightedLuminance, GHS_EvenWeightedLuminance, GHS_Saturation };
    enum GHSClipMode { GHS_Clip, GHS_Rescale, GHS_ClipRGBBlend, GHS_RescaleGlobal };

    struct GHSParams {
        double D = 0.0;  // Stretch Factor
        double B = 0.5;  // Local Intensity
        double SP = 0.0; // Symmetry Point
        double LP = 0.0; // Shadow Protection
        double HP = 1.0; // Highlight Protection
        double BP = 0.0; // Black Point
        GHSMode mode = GHS_GeneralizedHyperbolic;
        GHSColorMode colorMode = GHS_Independent;
        GHSClipMode clipMode = GHS_Clip;
        bool inverse = false;
        bool applyLog = false;
        bool channels[3] = {true, true, true}; // R, G, B
    };
    
    // Apply Generalized Hyperbolic Stretch
    void applyGHS(const GHSParams& params);
    
    // Apply Spline Transformation (Curves)
    void applySpline(const SplineData& spline, const bool channels[3]);
    
    // White Balance
    
    // Compute LUT for GHS (for Preview)
    std::vector<float> computeGHSLUT(const GHSParams& params, int size = 65536) const;
    
    // SCNR
    void applySCNR(float amount, int method);
    
    // Color Saturation
    struct SaturationParams {
        float amount = 1.0f;     // 1.0 = No change, >1.0 = Boost
        float bgFactor = 1.0f; // Power factor for intensity masking
        float hueCenter = 0.0f;  // 0-360
        float hueWidth = 360.0f; // 0-360
        float hueSmooth = 30.0f; // Transition width
    };
    void applySaturation(const SaturationParams& params);
    void applyArcSinh(float stretchFactor);
    void applyArcSinh(float stretchFactor, float blackPoint, bool humanLuminance); // Full version
    
    // Stats Helper
    float getChannelMedian(int channelIndex) const;
    float getChannelMAD(int channelIndex, float median) const;
    float getRobustMedian(int channelIndex, float t0, float t1) const; 
    float getPixelValue(int x, int y, int c) const;
    float value(int x, int y, int c = 0) const { return getPixelValue(x, y, c); }
    float& value(int x, int y, int c = 0) { 
        return m_data[(static_cast<size_t>(y) * m_width + x) * m_channels + c]; 
    }
    float getPixelFlat(size_t index, int c) const;
    float getAreaMean(int x, int y, int w, int h, int c) const;

    struct Metadata {
        double focalLength = 0;
        double pixelSize = 0;
        double exposure = 0;
        double ra = 0;
        double dec = 0;
        
        // WCS Matrix
        double crpix1 = 0, crpix2 = 0;
        double cd1_1 = 0, cd1_2 = 0, cd2_1 = 0, cd2_2 = 0;
        
        // Additional WCS parameters
        QString ctype1;   // e.g., "RA---TAN", "RA---TAN-SIP"
        QString ctype2;   // e.g., "DEC--TAN", "DEC--TAN-SIP"
        double equinox = 2000.0;
        double lonpole = 180.0;
        double latpole = 0.0;
        
        // SIP Distortion Coefficients
        int sipOrderA = 0;   // A_ORDER
        int sipOrderB = 0;   // B_ORDER
        int sipOrderAP = 0;  // AP_ORDER (inverse)
        int sipOrderBP = 0;  // BP_ORDER (inverse)
        QMap<QString, double> sipCoeffs;  // "A_1_0" -> value, "B_2_1" -> value, etc.
        
        QString objectName;
        QString dateObs;
        QString filePath;      // Source file path for reference
        QString bitDepth;      // Original bit depth info
        bool isMono = false;   // True if originally mono image
        int64_t stackCount = 0; // Number of images combined
        double ccdTemp = 0.0;
        QString bayerPattern; // e.g. "RGGB", "GBRG" etc. from BAYERPAT header

        // Persisted Catalog Stars for PCC
        std::vector<CatalogStar> catalogStars;
        PCCResult pccResult;
        
        // Raw Header Storage (Key, Value, Comment)
        struct HeaderCard {
             QString key;
             QString value;
             QString comment;
        };
        std::vector<HeaderCard> rawHeaders;
        
        QVariantMap xisfProperties;
    };

    void setMetadata(const Metadata& meta) { m_meta = meta; }
    const Metadata& metadata() const { return m_meta; }
    Metadata& metadata() { return m_meta; }
    
    // Computes clipping stats (pixels <= 0 and >= 1) - Parallelized
    void computeClippingStats(long& lowClip, long& highClip) const;

    // Histogram
    std::vector<std::vector<int>> computeHistogram(int bins = 256) const;

    // Name Tracking
    void setName(const QString& name) { m_name = name; }
    QString name() const { return m_name; }

    bool isModified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }
    
    // Synchronize WCS struct values back to rawHeaders vector
    void syncWcsToHeaders();
    
    // Thread Safety: Lock/Unlock for multi-threaded access
    // Usage: buffer.lockRead(); /* read data */ buffer.unlock();
    // Or:    buffer.lockWrite(); /* modify data */ buffer.unlock();
    void lockRead() const { if (!m_mutex) m_mutex = std::make_unique<QReadWriteLock>(); m_mutex->lockForRead(); }
    void lockWrite() { if (!m_mutex) m_mutex = std::make_unique<QReadWriteLock>(); m_mutex->lockForWrite(); }
    void unlock() const { if (m_mutex) m_mutex->unlock(); }
    
    // RAII Helper for automatic lock management
    class ReadLock {
    public:
        explicit ReadLock(const ImageBuffer* buf) : m_buf(buf) { if (m_buf) m_buf->lockRead(); }
        ~ReadLock() { if (m_buf) m_buf->unlock(); }
        ReadLock(const ReadLock&) = delete;
        ReadLock& operator=(const ReadLock&) = delete;
    private:
        const ImageBuffer* m_buf;
    };
    
    class WriteLock {
    public:
        explicit WriteLock(ImageBuffer* buf) : m_buf(buf) { if (m_buf) m_buf->lockWrite(); }
        ~WriteLock() { if (m_buf) m_buf->unlock(); }
        WriteLock(const WriteLock&) = delete;
        WriteLock& operator=(const WriteLock&) = delete;
    private:
        ImageBuffer* m_buf;
    };

private:
    int m_width = 0;
    int m_height = 0;
    int m_channels = 1; 
    std::vector<float> m_data; // Interleaved 32-bit float (0.0 - 1.0)
    Metadata m_meta;
    QString m_name;
    bool m_modified = false;
    
    // Mask
    MaskLayer m_mask;
    bool m_hasMask = false;
    
    // Thread Safety: Read-Write lock for concurrent access
    // Multiple readers allowed, exclusive write access required
    mutable std::unique_ptr<QReadWriteLock> m_mutex;

    // Agile Autostretch (Display only)
    std::vector<float> computeAgileLUT(int channelIndex, float targetMedian = 0.25f);
    
public:
    // Wavelet Utilities
    // Convolve 1D Separable with Reflect Padding and optional holes (dilation) based on scale
    // Returns a SINGLE CHANNEL float buffer.
    static std::vector<float> convolveSepReflect(const std::vector<float>& src, int w, int h, const std::vector<float>& kernel, int scale = 0);
    
    // Decompose into planes (Detail 0..N-1, Residual). Expects Single Channel input.
    // Returns vector of flat float buffers.
    static std::vector<std::vector<float>> atrousDecompose(const std::vector<float>& src, int w, int h, int n_scales);
    
    // Reconstruct from planes
    static std::vector<float> atrousReconstruct(const std::vector<std::vector<float>>& planes, int w, int h);
    
    // Star Extraction Struct
    struct DetectedStar {
        float x, y;
        float flux;
        float peak;
        float a, b, theta; // Ellipse parameters
        float hfr; // Half Flux Radius (~FWHM/2.35 usually, or direct HFR)
    };
    
    // Extract stars (Single Channel input)
    // sigma: detection threshold above background RMS
    static std::vector<DetectedStar> extractStars(const std::vector<float>& src, int w, int h, float sigma = 3.0f, int minArea = 5);
    
};

#endif // IMAGEBUFFER_H
