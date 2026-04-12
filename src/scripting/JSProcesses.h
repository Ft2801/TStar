// =============================================================================
// JSProcesses.h
//
// Scriptable process wrappers for TStar image processing algorithms.
// Each class wraps a specific ImageBuffer method, exposing its parameters
// as Q_PROPERTYs and providing an executeOn(JSImage*) entry point.
//
// Supported processes:
//   - JSCurvesProcess      → ImageBuffer::applySpline()
//   - JSSaturationProcess  → ImageBuffer::applySaturation()
//   - JSSCNRProcess        → ImageBuffer::applySCNR()
//   - JSGHSProcess         → ImageBuffer::applyGHS()
//   - JSStretchProcess     → ImageBuffer::performTrueStretch()
// =============================================================================

#ifndef JSPROCESSES_H
#define JSPROCESSES_H

#include "JSProcessBase.h"
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QPointer>
#include <QMap>

namespace Scripting {

class JSImage;

// =============================================================================
// JSCurvesProcess
// =============================================================================

class JSCurvesProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QVariantList points READ points  WRITE setPoints)
    Q_PROPERTY(bool red            READ red     WRITE setRed)
    Q_PROPERTY(bool green          READ green   WRITE setGreen)
    Q_PROPERTY(bool blue           READ blue    WRITE setBlue)

public:
    explicit JSCurvesProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Curves"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QVariantList points() const { return m_points; }
    void setPoints(const QVariantList& pts) { m_points = pts; }

    bool red()   const { return m_red; }
    bool green() const { return m_green; }
    bool blue()  const { return m_blue; }
    void setRed(bool v)   { m_red = v; }
    void setGreen(bool v) { m_green = v; }
    void setBlue(bool v)  { m_blue = v; }

private:
    QVariantList m_points;
    bool m_red   = true;
    bool m_green = true;
    bool m_blue  = true;
};

// =============================================================================
// JSSaturationProcess
// =============================================================================

class JSSaturationProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double amount  READ amount  WRITE setAmount)
    Q_PROPERTY(bool protect   READ protect WRITE setProtect)

public:
    explicit JSSaturationProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Saturation"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double amount()  const { return m_amount; }
    bool   protect() const { return m_protect; }

    void setAmount(double v) { m_amount = v; }
    void setProtect(bool v)  { m_protect = v; }

private:
    double m_amount  = 1.0;
    bool   m_protect = false;
};

// =============================================================================
// JSSCNRProcess
// =============================================================================

class JSSCNRProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int channel   READ channel WRITE setChannel)
    Q_PROPERTY(double amount READ amount  WRITE setAmount)

public:
    explicit JSSCNRProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("SCNR"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int    channel() const { return m_channel; }
    double amount()  const { return m_amount; }

    void setChannel(int v)   { m_channel = v; }
    void setAmount(double v) { m_amount = v; }

private:
    int    m_channel = 1;  // default: green
    double m_amount  = 1.0;
};

// =============================================================================
// JSGHSProcess
// =============================================================================

class JSGHSProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double D         READ D         WRITE setD)
    Q_PROPERTY(double B         READ B         WRITE setB)
    Q_PROPERTY(double SP        READ SP        WRITE setSP)
    Q_PROPERTY(double LP        READ LP        WRITE setLP)
    Q_PROPERTY(double HP        READ HP        WRITE setHP)
    Q_PROPERTY(double BP        READ BP        WRITE setBP)
    Q_PROPERTY(int mode         READ mode      WRITE setMode)
    Q_PROPERTY(int colorMode    READ colorMode WRITE setColorMode)
    Q_PROPERTY(int clipMode     READ clipMode  WRITE setClipMode)
    Q_PROPERTY(bool red         READ red       WRITE setRed)
    Q_PROPERTY(bool green       READ green     WRITE setGreen)
    Q_PROPERTY(bool blue        READ blue      WRITE setBlue)

public:
    explicit JSGHSProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("GHS"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double D()  const { return m_D; }
    double B()  const { return m_B; }
    double SP() const { return m_SP; }
    double LP() const { return m_LP; }
    double HP() const { return m_HP; }
    double BP() const { return m_BP; }
    int mode()      const { return m_mode; }
    int colorMode() const { return m_colorMode; }
    int clipMode()  const { return m_clipMode; }
    bool red()   const { return m_red; }
    bool green() const { return m_green; }
    bool blue()  const { return m_blue; }

    void setD(double v)  { m_D = v; }
    void setB(double v)  { m_B = v; }
    void setSP(double v) { m_SP = v; }
    void setLP(double v) { m_LP = v; }
    void setHP(double v) { m_HP = v; }
    void setBP(double v) { m_BP = v; }
    void setMode(int v)      { m_mode = v; }
    void setColorMode(int v) { m_colorMode = v; }
    void setClipMode(int v)  { m_clipMode = v; }
    void setRed(bool v)   { m_red = v; }
    void setGreen(bool v) { m_green = v; }
    void setBlue(bool v)  { m_blue = v; }

private:
    double m_D  = 0.0;
    double m_B  = 0.5;
    double m_SP = 0.0;
    double m_LP = 0.0;
    double m_HP = 1.0;
    double m_BP = 0.0;
    int m_mode      = 0;
    int m_colorMode = 0;
    int m_clipMode  = 0;
    bool m_red   = true;
    bool m_green = true;
    bool m_blue  = true;
};

// =============================================================================
// JSStretchProcess
// =============================================================================

class JSStretchProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double targetMedian    READ targetMedian    WRITE setTargetMedian)
    Q_PROPERTY(bool linked            READ linked          WRITE setLinked)
    Q_PROPERTY(bool normalize         READ normalize       WRITE setNormalize)
    Q_PROPERTY(double blackpointSigma READ blackpointSigma WRITE setBlackpointSigma)
    Q_PROPERTY(bool noBlackClip       READ noBlackClip     WRITE setNoBlackClip)
    Q_PROPERTY(bool hdrCompress       READ hdrCompress     WRITE setHdrCompress)
    Q_PROPERTY(double hdrAmount       READ hdrAmount       WRITE setHdrAmount)
    Q_PROPERTY(double hdrKnee         READ hdrKnee         WRITE setHdrKnee)
    Q_PROPERTY(bool lumaOnly          READ lumaOnly        WRITE setLumaOnly)

public:
    explicit JSStretchProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Stretch"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double targetMedian()    const { return m_targetMedian; }
    bool   linked()          const { return m_linked; }
    bool   normalize()       const { return m_normalize; }
    double blackpointSigma() const { return m_blackpointSigma; }
    bool   noBlackClip()     const { return m_noBlackClip; }
    bool   hdrCompress()     const { return m_hdrCompress; }
    double hdrAmount()       const { return m_hdrAmount; }
    double hdrKnee()         const { return m_hdrKnee; }
    bool   lumaOnly()        const { return m_lumaOnly; }

    void setTargetMedian(double v)    { m_targetMedian = v; }
    void setLinked(bool v)            { m_linked = v; }
    void setNormalize(bool v)         { m_normalize = v; }
    void setBlackpointSigma(double v) { m_blackpointSigma = v; }
    void setNoBlackClip(bool v)       { m_noBlackClip = v; }
    void setHdrCompress(bool v)       { m_hdrCompress = v; }
    void setHdrAmount(double v)       { m_hdrAmount = v; }
    void setHdrKnee(double v)         { m_hdrKnee = v; }
    void setLumaOnly(bool v)          { m_lumaOnly = v; }

private:
    double m_targetMedian    = 0.25;
    bool   m_linked          = true;
    bool   m_normalize       = false;
    double m_blackpointSigma = 5.0;
    bool   m_noBlackClip     = false;
    bool   m_hdrCompress     = false;
    double m_hdrAmount       = 0.0;
    double m_hdrKnee         = 0.75;
    bool   m_lumaOnly        = false;
};

// =============================================================================
// JSPixelMathProcess
// =============================================================================

class JSPixelMathProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QString expression READ expression WRITE setExpression)
    Q_PROPERTY(bool rescale       READ rescale    WRITE setRescale)

public:
    explicit JSPixelMathProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("PixelMath"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    Q_INVOKABLE void setReference(const QString& varId, QObject* image);

    QString expression() const { return m_expression; }
    bool    rescale()    const { return m_rescale; }

    void setExpression(const QString& v) { m_expression = v; }
    void setRescale(bool v)              { m_rescale = v; }

private:
    QString m_expression;
    bool    m_rescale = false;
    QMap<QString, QPointer<JSImage>> m_references;
};

// =============================================================================
// JSArcsinhStretchProcess
// =============================================================================

class JSArcsinhStretchProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double stretchFactor  READ stretchFactor  WRITE setStretchFactor)
    Q_PROPERTY(double blackPoint     READ blackPoint     WRITE setBlackPoint)
    Q_PROPERTY(bool   humanLuminance READ humanLuminance WRITE setHumanLuminance)

public:
    explicit JSArcsinhStretchProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("ArcsinhStretch"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double stretchFactor()  const { return m_stretchFactor; }
    double blackPoint()     const { return m_blackPoint; }
    bool   humanLuminance() const { return m_humanLuminance; }

    void setStretchFactor(double v)  { m_stretchFactor = v; }
    void setBlackPoint(double v)     { m_blackPoint = v; }
    void setHumanLuminance(bool v)   { m_humanLuminance = v; }

private:
    double m_stretchFactor  = 1.0;
    double m_blackPoint     = 0.0;
    bool   m_humanLuminance = true;
};

// =============================================================================
// JSHistogramStretchProcess
// =============================================================================

class JSHistogramStretchProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double shadows    READ shadows    WRITE setShadows)
    Q_PROPERTY(double midtones   READ midtones   WRITE setMidtones)
    Q_PROPERTY(double highlights READ highlights WRITE setHighlights)
    Q_PROPERTY(bool red          READ red        WRITE setRed)
    Q_PROPERTY(bool green        READ green      WRITE setGreen)
    Q_PROPERTY(bool blue         READ blue       WRITE setBlue)

public:
    explicit JSHistogramStretchProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("HistogramStretch"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double shadows()    const { return m_shadows; }
    double midtones()   const { return m_midtones; }
    double highlights() const { return m_highlights; }
    bool red()   const { return m_red; }
    bool green() const { return m_green; }
    bool blue()  const { return m_blue; }

    void setShadows(double v)    { m_shadows = v; }
    void setMidtones(double v)   { m_midtones = v; }
    void setHighlights(double v) { m_highlights = v; }
    void setRed(bool v)   { m_red = v; }
    void setGreen(bool v) { m_green = v; }
    void setBlue(bool v)  { m_blue = v; }

private:
    double m_shadows    = 0.0;
    double m_midtones   = 0.5;
    double m_highlights = 1.0;
    bool m_red   = true;
    bool m_green = true;
    bool m_blue  = true;
};

// =============================================================================
// JSStarStretchProcess
// =============================================================================

class JSStarStretchProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double stretchAmount READ stretchAmount WRITE setStretchAmount)
    Q_PROPERTY(double colorBoost    READ colorBoost    WRITE setColorBoost)
    Q_PROPERTY(bool   scnr          READ scnr          WRITE setScnr)

public:
    explicit JSStarStretchProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("StarStretch"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double stretchAmount() const { return m_stretchAmount; }
    double colorBoost()    const { return m_colorBoost; }
    bool   scnr()          const { return m_scnr; }

    void setStretchAmount(double v) { m_stretchAmount = v; }
    void setColorBoost(double v)    { m_colorBoost = v; }
    void setScnr(bool v)            { m_scnr = v; }

private:
    double m_stretchAmount = 1.0;
    double m_colorBoost    = 1.0;
    bool   m_scnr          = false;
};

// =============================================================================
// JSMagentaCorrectionProcess
// =============================================================================

class JSMagentaCorrectionProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double amount    READ amount    WRITE setAmount)
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold)

public:
    explicit JSMagentaCorrectionProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("MagentaCorrection"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double amount()    const { return m_amount; }
    double threshold() const { return m_threshold; }

    void setAmount(double v)    { m_amount = v; }
    void setThreshold(double v) { m_threshold = v; }

private:
    double m_amount    = 0.5;
    double m_threshold = 0.1;
};

// =============================================================================
// JSTemperatureTintProcess
// =============================================================================

class JSTemperatureTintProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature)
    Q_PROPERTY(double tint        READ tint        WRITE setTint)

public:
    explicit JSTemperatureTintProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("TemperatureTint"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double temperature() const { return m_temperature; }
    double tint()        const { return m_tint; }

    void setTemperature(double v) { m_temperature = v; }
    void setTint(double v)        { m_tint = v; }

private:
    double m_temperature = 0.0;
    double m_tint        = 0.0;
};

// =============================================================================
// JSGraXpertProcess (AI Background Extraction & Denoise)
// =============================================================================

class JSGraXpertProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(bool isDenoise READ isDenoise WRITE setIsDenoise)
    Q_PROPERTY(double strength READ strength WRITE setStrength)
    Q_PROPERTY(double smoothing READ smoothing WRITE setSmoothing)
    Q_PROPERTY(QString aiVersion READ aiVersion WRITE setAiVersion)
    Q_PROPERTY(bool useGpu READ useGpu WRITE setUseGpu)

public:
    explicit JSGraXpertProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("GraXpert"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    bool isDenoise() const { return m_isDenoise; }
    double strength() const { return m_strength; }
    double smoothing() const { return m_smoothing; }
    QString aiVersion() const { return m_aiVersion; }
    bool useGpu() const { return m_useGpu; }

    void setIsDenoise(bool v) { m_isDenoise = v; }
    void setStrength(double v) { m_strength = v; }
    void setSmoothing(double v) { m_smoothing = v; }
    void setAiVersion(const QString& v) { m_aiVersion = v; }
    void setUseGpu(bool v) { m_useGpu = v; }

private:
    bool m_isDenoise = false;
    double m_strength = 0.5, m_smoothing = 1.0;
    QString m_aiVersion = "Latest (auto)";
    bool m_useGpu = true;
};

// =============================================================================
// JSStarNetProcess (AI Star Removal)
// =============================================================================

class JSStarNetProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int stride READ stride WRITE setStride)
    Q_PROPERTY(bool isLinear READ linear WRITE setLinear)

    Q_PROPERTY(bool generateMask READ generateMask WRITE setGenerateMask)
    Q_PROPERTY(double upsample READ upsample WRITE setUpsample)
    Q_PROPERTY(bool useGpu READ useGpu WRITE setUseGpu)

public:
    explicit JSStarNetProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("StarNet"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int stride() const { return m_stride; }
    bool linear() const { return m_isLinear; }

    bool generateMask() const { return m_generateMask; }
    double upsample() const { return m_upsample; }
    bool useGpu() const { return m_useGpu; }

    void setStride(int v) { m_stride = v; }
    void setLinear(bool v) { m_isLinear = v; }

    void setGenerateMask(bool v) { m_generateMask = v; }
    void setUpsample(double v) { m_upsample = v; }
    void setUseGpu(bool v) { m_useGpu = v; }

private:
    int m_stride = 256;
    bool m_isLinear = true;

    bool m_generateMask = false;
    double m_upsample = 1.0;
    bool m_useGpu = true;
};

// =============================================================================
// JSCosmicClarityProcess (AI Denoise/Sharpen)
// =============================================================================

class JSCosmicClarityProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int  mode READ mode WRITE setMode)
    Q_PROPERTY(int sharpenMode READ sharpenMode WRITE setSharpenMode)

    Q_PROPERTY(double stellarAmount READ stellarAmount WRITE setStellarAmount)
    Q_PROPERTY(double nonStellarAmount READ nonStellarAmount WRITE setNonStellarAmount)
    Q_PROPERTY(double nonStellarPSF READ nonStellarPSF WRITE setNonStellarPSF)
    Q_PROPERTY(bool separateChannelsSharpen READ separateChannelsSharpen WRITE setSeparateChannelsSharpen)
    Q_PROPERTY(bool autoPSF READ autoPSF WRITE setAutoPSF)
    Q_PROPERTY(double denoiseLum READ denoiseLum WRITE setDenoiseLum)
    Q_PROPERTY(double denoiseColor READ denoiseColor WRITE setDenoiseColor)
    Q_PROPERTY(int denoiseMode READ denoiseMode WRITE setDenoiseMode)
    Q_PROPERTY(bool separateChannelsDenoise READ separateChannelsDenoise WRITE setSeparateChannelsDenoise)
    Q_PROPERTY(int scaleFactor READ scaleFactor WRITE setScaleFactor)
    Q_PROPERTY(bool useGpu READ useGpu WRITE setUseGpu)

public:
    explicit JSCosmicClarityProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("CosmicClarity"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int mode() const { return m_mode; }
    int sharpenMode() const { return m_sharpenMode; }

    double stellarAmount() const { return m_stellarAmount; }
    double nonStellarAmount() const { return m_nonStellarAmount; }
    double nonStellarPSF() const { return m_nonStellarPSF; }
    bool separateChannelsSharpen() const { return m_separateChannelsSharpen; }
    bool autoPSF() const { return m_autoPSF; }
    double denoiseLum() const { return m_denoiseLum; }
    double denoiseColor() const { return m_denoiseColor; }
    int denoiseMode() const { return m_denoiseMode; }
    bool separateChannelsDenoise() const { return m_separateChannelsDenoise; }
    int scaleFactor() const { return m_scaleFactor; }
    bool useGpu() const { return m_useGpu; }

    void setMode(int v) { m_mode = v; }
    void setSharpenMode(int v) { m_sharpenMode = v; }

    void setStellarAmount(double v) { m_stellarAmount = v; }
    void setNonStellarAmount(double v) { m_nonStellarAmount = v; }
    void setNonStellarPSF(double v) { m_nonStellarPSF = v; }
    void setSeparateChannelsSharpen(bool v) { m_separateChannelsSharpen = v; }
    void setAutoPSF(bool v) { m_autoPSF = v; }
    void setDenoiseLum(double v) { m_denoiseLum = v; }
    void setDenoiseColor(double v) { m_denoiseColor = v; }
    void setDenoiseMode(int v) { m_denoiseMode = v; }
    void setSeparateChannelsDenoise(bool v) { m_separateChannelsDenoise = v; }
    void setScaleFactor(int v) { m_scaleFactor = v; }
    void setUseGpu(bool v) { m_useGpu = v; }

private:
    int m_mode = 0;
    int m_sharpenMode = 0;

    double m_stellarAmount = 0.5, m_nonStellarAmount = 0.5, m_nonStellarPSF = 1.0;
    bool m_separateChannelsSharpen = false, m_autoPSF = true;
    double m_denoiseLum = 0.5, m_denoiseColor = 0.5;
    int m_denoiseMode = 0;
    bool m_separateChannelsDenoise = false;
    int m_scaleFactor = 1;
    bool m_useGpu = true;
};

// =============================================================================
// JSRARProcess (AI Aberration Removal)
// =============================================================================

class JSRARProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double strength READ strength WRITE setStrength)
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath)
    Q_PROPERTY(int patchSize READ patchSize WRITE setPatchSize)
    Q_PROPERTY(int overlap READ overlap WRITE setOverlap)
    Q_PROPERTY(int provider READ provider WRITE setProvider)

public:
    explicit JSRARProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("RAR"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double strength() const { return m_strength; }
    QString modelPath() const { return m_modelPath; }
    int patchSize() const { return m_patchSize; }
    int overlap() const { return m_overlap; }
    int provider() const { return m_provider; }

    void setStrength(double v) { m_strength = v; }
    void setModelPath(const QString& v) { m_modelPath = v; }
    void setPatchSize(int v) { m_patchSize = v; }
    void setOverlap(int v) { m_overlap = v; }
    void setProvider(int v) { m_provider = v; }

private:
    double m_strength = 1.0;
    QString m_modelPath = "";
    int m_patchSize = 512, m_overlap = 32, m_provider = 0;
};

// =============================================================================
// JSChannelCombinationProcess
// =============================================================================

class JSChannelCombinationProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QString red READ red WRITE setRed)
    Q_PROPERTY(QString green READ green WRITE setGreen)
    Q_PROPERTY(QString blue READ blue WRITE setBlue)
    Q_PROPERTY(QString lum READ lum WRITE setLum)

public:
    explicit JSChannelCombinationProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("ChannelCombination"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QString red() const { return m_red; }
    QString green() const { return m_green; }
    QString blue() const { return m_blue; }
    QString lum() const { return m_lum; }

    void setRed(const QString& v) { m_red = v; }
    void setGreen(const QString& v) { m_green = v; }
    void setBlue(const QString& v) { m_blue = v; }
    void setLum(const QString& v) { m_lum = v; }

private:
    QString m_red;
    QString m_green;
    QString m_blue;
    QString m_lum;
};

// =============================================================================
// JSPerfectPaletteProcess
// =============================================================================

class JSPerfectPaletteProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int palette READ palette WRITE setPalette)

public:
    explicit JSPerfectPaletteProcess(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("PerfectPalette"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int palette() const { return m_palette; }
    void setPalette(int v) { m_palette = v; }

private:
    int m_palette = 0; // 0=SHO, 1=HOO, etc.
};

// =============================================================================
// Generic stub process wrapper macro.
//
// These processes are registered but not yet fully implemented.  They accept
// a generic QVariantMap "params" property that can be read/written from JS.
// When the native C++ implementation is wired up, each stub can be replaced
// with a fully typed class like the ones above.
// =============================================================================

// =============================================================================
// JSABEProcess (Automatic Background Extraction)
// =============================================================================

class JSABEProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int degree       READ degree    WRITE setDegree)
    Q_PROPERTY(int samples      READ samples   WRITE setSamples)
    Q_PROPERTY(int down         READ down      WRITE setDown)
    Q_PROPERTY(int patch        READ patch     WRITE setPatch)
    Q_PROPERTY(bool rbf         READ rbf       WRITE setRbf)
    Q_PROPERTY(double smooth    READ smooth    WRITE setSmooth)
    Q_PROPERTY(bool normalize   READ normalize WRITE setNormalize)

public:
    explicit JSABEProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("ABE"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int degree() const { return m_degree; }
    int samples() const { return m_samples; }
    int down() const { return m_down; }
    int patch() const { return m_patch; }
    bool rbf() const { return m_rbf; }
    double smooth() const { return m_smooth; }
    bool normalize() const { return m_normalize; }

    void setDegree(int v) { m_degree = v; }
    void setSamples(int v) { m_samples = v; }
    void setDown(int v) { m_down = v; }
    void setPatch(int v) { m_patch = v; }
    void setRbf(bool v) { m_rbf = v; }
    void setSmooth(double v) { m_smooth = v; }
    void setNormalize(bool v) { m_normalize = v; }

private:
    int m_degree = 4;
    int m_samples = 20;
    int m_down = 2;
    int m_patch = 5;
    bool m_rbf = false;
    double m_smooth = 0.5;
    bool m_normalize = true;
};

// =============================================================================
// JSCBEProcess (Catalog Background Extraction)
// =============================================================================

class JSCBEProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QString survey       READ survey       WRITE setSurvey)
    Q_PROPERTY(int scale            READ scale        WRITE setScale)
    Q_PROPERTY(bool protectStars    READ protectStars WRITE setProtectStars)
    Q_PROPERTY(bool gradientMap     READ gradientMap  WRITE setGradientMap)

public:
    explicit JSCBEProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("CBE"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QString survey() const { return m_survey; }
    int scale() const { return m_scale; }
    bool protectStars() const { return m_protectStars; }
    bool gradientMap() const { return m_gradientMap; }

    void setSurvey(const QString& v) { m_survey = v; }
    void setScale(int v) { m_scale = v; }
    void setProtectStars(bool v) { m_protectStars = v; }
    void setGradientMap(bool v) { m_gradientMap = v; }

private:
    QString m_survey = "DSS2/color";
    int m_scale = 1;
    bool m_protectStars = true;
    bool m_gradientMap = false;
};

// =============================================================================
// JSPCCProcess (Photometric Color Calibration)
// =============================================================================

class JSPCCProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(bool neutralizeBackground READ neutralizeBackground WRITE setNeutralizeBackground)

public:
    explicit JSPCCProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("PCC"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    bool neutralizeBackground() const { return m_neutralizeBackground; }
    void setNeutralizeBackground(bool v) { m_neutralizeBackground = v; }

private:
    bool m_neutralizeBackground = true;
};

// =============================================================================
// JSSPCCProcess (Spectrophotometric Color Calibration)
// =============================================================================

class JSSPCCProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QString whiteRef       READ whiteRef       WRITE setWhiteRef)
    Q_PROPERTY(QString rFilter        READ rFilter        WRITE setRFilter)
    Q_PROPERTY(QString gFilter        READ gFilter        WRITE setGFilter)
    Q_PROPERTY(QString bFilter        READ bFilter        WRITE setBFilter)
    Q_PROPERTY(QString sensor         READ sensor         WRITE setSensor)
    Q_PROPERTY(QString bgMethod       READ bgMethod       WRITE setBgMethod)
    Q_PROPERTY(double sepThreshold    READ sepThreshold   WRITE setSepThreshold)
    Q_PROPERTY(int maxStars           READ maxStars       WRITE setMaxStars)
    Q_PROPERTY(bool gaiaFallback      READ gaiaFallback   WRITE setGaiaFallback)
    Q_PROPERTY(bool useFullMatrix     READ useFullMatrix  WRITE setUseFullMatrix)
    Q_PROPERTY(bool linearMode        READ linearMode     WRITE setLinearMode)
    Q_PROPERTY(bool runGradient       READ runGradient    WRITE setRunGradient)
    Q_PROPERTY(QString gradientMethod READ gradientMethod WRITE setGradientMethod)

public:
    explicit JSSPCCProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("SPCC"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QString whiteRef() const { return m_whiteRef; }
    QString rFilter()  const { return m_rFilter; }
    QString gFilter()  const { return m_gFilter; }
    QString bFilter()  const { return m_bFilter; }
    QString sensor()   const { return m_sensor; }
    QString bgMethod() const { return m_bgMethod; }
    double sepThreshold() const { return m_sepThreshold; }
    int maxStars() const { return m_maxStars; }
    bool gaiaFallback() const { return m_gaiaFallback; }
    bool useFullMatrix() const { return m_useFullMatrix; }
    bool linearMode() const { return m_linearMode; }
    bool runGradient() const { return m_runGradient; }
    QString gradientMethod() const { return m_gradientMethod; }

    void setWhiteRef(const QString& v) { m_whiteRef = v; }
    void setRFilter(const QString& v)  { m_rFilter = v; }
    void setGFilter(const QString& v)  { m_gFilter = v; }
    void setBFilter(const QString& v)  { m_bFilter = v; }
    void setSensor(const QString& v)   { m_sensor = v; }
    void setBgMethod(const QString& v) { m_bgMethod = v; }
    void setSepThreshold(double v)     { m_sepThreshold = v; }
    void setMaxStars(int v)            { m_maxStars = v; }
    void setGaiaFallback(bool v)       { m_gaiaFallback = v; }
    void setUseFullMatrix(bool v)      { m_useFullMatrix = v; }
    void setLinearMode(bool v)         { m_linearMode = v; }
    void setRunGradient(bool v)        { m_runGradient = v; }
    void setGradientMethod(const QString& v) { m_gradientMethod = v; }

private:
    QString m_whiteRef = "G2V";
    QString m_rFilter = "(None)";
    QString m_gFilter = "(None)";
    QString m_bFilter = "(None)";
    QString m_sensor = "(None)";
    QString m_bgMethod = "Simple";
    double m_sepThreshold = 5.0;
    int m_maxStars = 300;
    bool m_gaiaFallback = true;
    bool m_useFullMatrix = true;
    bool m_linearMode = true;
    bool m_runGradient = false;
    QString m_gradientMethod = "poly3";
};

// =============================================================================
// JSBackgroundNeutralizationProcess
// =============================================================================

class JSBackgroundNeutralizationProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int left   READ left   WRITE setLeft)
    Q_PROPERTY(int top    READ top    WRITE setTop)
    Q_PROPERTY(int width  READ width  WRITE setWidth)
    Q_PROPERTY(int height READ height WRITE setHeight)

public:
    explicit JSBackgroundNeutralizationProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("BackgroundNeutralization"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int left()   const { return m_left; }
    int top()    const { return m_top; }
    int width()  const { return m_width; }
    int height() const { return m_height; }

    void setLeft(int v)   { m_left = v; }
    void setTop(int v)    { m_top = v; }
    void setWidth(int v)  { m_width = v; }
    void setHeight(int v) { m_height = v; }

private:
    int m_left = 0, m_top = 0, m_width = 100, m_height = 100;
};

// =============================================================================
// JSSelectiveColorProcess
// =============================================================================

class JSSelectiveColorProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double hueStart    READ hueStart    WRITE setHueStart)
    Q_PROPERTY(double hueEnd      READ hueEnd      WRITE setHueEnd)
    Q_PROPERTY(double smoothness  READ smoothness  WRITE setSmoothness)
    Q_PROPERTY(double minChroma   READ minChroma   WRITE setMinChroma)
    Q_PROPERTY(double intensity   READ intensity   WRITE setIntensity)
    Q_PROPERTY(bool   invert      READ invert      WRITE setInvert)
    Q_PROPERTY(int    cyan        READ cyan        WRITE setCyan)
    Q_PROPERTY(int    magenta     READ magenta     WRITE setMagenta)
    Q_PROPERTY(int    yellow      READ yellow      WRITE setYellow)
    Q_PROPERTY(int    red         READ red         WRITE setRed)
    Q_PROPERTY(int    green       READ green       WRITE setGreen)
    Q_PROPERTY(int    blue        READ blue        WRITE setBlue)
    Q_PROPERTY(int    luminance   READ luminance   WRITE setLuminance)
    Q_PROPERTY(int    saturation   READ saturation  WRITE setSaturation)
    Q_PROPERTY(int    contrast    READ contrast    WRITE setContrast)

public:
    explicit JSSelectiveColorProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("SelectiveColor"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double hueStart() const { return m_hueStart; }
    double hueEnd() const { return m_hueEnd; }
    double smoothness() const { return m_smoothness; }
    double minChroma() const { return m_minChroma; }
    double intensity() const { return m_intensity; }
    bool invert() const { return m_invert; }
    int cyan() const { return m_cyan; }
    int magenta() const { return m_magenta; }
    int yellow() const { return m_yellow; }
    int red() const { return m_red; }
    int green() const { return m_green; }
    int blue() const { return m_blue; }
    int luminance() const { return m_luminance; }
    int saturation() const { return m_saturation; }
    int contrast() const { return m_contrast; }

    void setHueStart(double v) { m_hueStart = v; }
    void setHueEnd(double v) { m_hueEnd = v; }
    void setSmoothness(double v) { m_smoothness = v; }
    void setMinChroma(double v) { m_minChroma = v; }
    void setIntensity(double v) { m_intensity = v; }
    void setInvert(bool v) { m_invert = v; }
    void setCyan(int v) { m_cyan = v; }
    void setMagenta(int v) { m_magenta = v; }
    void setYellow(int v) { m_yellow = v; }
    void setRed(int v) { m_red = v; }
    void setGreen(int v) { m_green = v; }
    void setBlue(int v) { m_blue = v; }
    void setLuminance(int v) { m_luminance = v; }
    void setSaturation(int v) { m_saturation = v; }
    void setContrast(int v) { m_contrast = v; }

private:
    double m_hueStart = 0.0, m_hueEnd = 30.0, m_smoothness = 1.0, m_minChroma = 0.0, m_intensity = 1.0;
    bool m_invert = false;
    int m_cyan = 0, m_magenta = 0, m_yellow = 0, m_red = 0, m_green = 0, m_blue = 0, m_luminance = 0, m_saturation = 0, m_contrast = 0;
};

// =============================================================================
// JSAlignChannelsProcess
// =============================================================================

class JSAlignChannelsProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(bool allowRotation READ allowRotation WRITE setAllowRotation)
    Q_PROPERTY(bool allowScale    READ allowScale    WRITE setAllowScale)
    Q_PROPERTY(double threshold   READ threshold     WRITE setThreshold)

public:
    explicit JSAlignChannelsProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("AlignChannels"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    bool allowRotation() const { return m_allowRotation; }
    bool allowScale()    const { return m_allowScale; }
    double threshold()   const { return m_threshold; }

    void setAllowRotation(bool v) { m_allowRotation = v; }
    void setAllowScale(bool v)    { m_allowScale = v; }
    void setThreshold(double v)   { m_threshold = v; }

private:
    bool m_allowRotation = true;
    bool m_allowScale = false;
    double m_threshold = 5.0;
};

// =============================================================================
// JSDebayerProcess
// =============================================================================

class JSDebayerProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int     pattern READ pattern WRITE setPattern)

    Q_PROPERTY(int     method  READ method  WRITE setMethod)

public:
    explicit JSDebayerProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("Debayer"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int     pattern() const { return m_pattern; }

    int     method()  const { return m_method; }

    void setPattern(int v) { m_pattern = v; }

    void setMethod(int v)  { m_method = v; }

private:
    int     m_pattern = 0; // RGGB, BGGR, GRBG, GBRG

    int     m_method = 0; // 0=Bilinear, 1=EdgeAware
};

// =============================================================================
// JSMorphologyProcess
// =============================================================================

class JSMorphologyProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int operation   READ operation   WRITE setOperation)
    Q_PROPERTY(int kernelSize  READ kernelSize  WRITE setKernelSize)
    Q_PROPERTY(int iterations READ iterations WRITE setIterations)

public:
    explicit JSMorphologyProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("Morphology"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int operation() const { return m_operation; }
    int kernelSize() const { return m_kernelSize; }
    int iterations() const { return m_iterations; }

    void setOperation(int v) { m_operation = v; }
    void setKernelSize(int v) { m_kernelSize = v; }
    void setIterations(int v) { m_iterations = v; }

private:
    int m_operation = 0; // 0=Erosion, 1=Dilation, 2=Opening, 3=Closing
    int m_kernelSize = 3;
    int m_iterations = 1;
};

// =============================================================================
// Generic stub process wrapper macro.
//
// These processes are registered but not yet fully implemented.  They accept
// a generic QVariantMap "params" property that can be read/written from JS.
// When the native C++ implementation is wired up, each stub can be replaced
// with a fully typed class like the ones above.
// =============================================================================

// =============================================================================
// JSClaheProcess
// =============================================================================

class JSClaheProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double clipLimit READ clipLimit WRITE setClipLimit)
    Q_PROPERTY(int    gridSize  READ gridSize  WRITE setGridSize)
    Q_PROPERTY(double opacity   READ opacity   WRITE setOpacity)

public:
    explicit JSClaheProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}

    QString name() const override { return QStringLiteral("Clahe"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double clipLimit() const { return m_clipLimit; }
    int    gridSize()  const { return m_gridSize; }
    double opacity()   const { return m_opacity; }

    void setClipLimit(double v) { m_clipLimit = v; }
    void setGridSize(int v)  { m_gridSize = v; }
    void setOpacity(double v) { m_opacity = v; }

private:
    double m_clipLimit = 2.0;
    int    m_gridSize = 8;
    double m_opacity = 1.0;
};

// =============================================================================
// JSExtractLuminanceProcess
// =============================================================================

class JSExtractLuminanceProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int method READ method WRITE setMethod)
    Q_PROPERTY(double weightR READ weightR WRITE setWeightR)
    Q_PROPERTY(double weightG READ weightG WRITE setWeightG)
    Q_PROPERTY(double weightB READ weightB WRITE setWeightB)

public:
    explicit JSExtractLuminanceProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("ExtractLuminance"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int method() const { return m_method; }
    double weightR() const { return m_weightR; }
    double weightG() const { return m_weightG; }
    double weightB() const { return m_weightB; }

    void setMethod(int v) { m_method = v; }
    void setWeightR(double v) { m_weightR = v; }
    void setWeightG(double v) { m_weightG = v; }
    void setWeightB(double v) { m_weightB = v; }

private:
    int m_method = 0; // REC709
    double m_weightR = 0.2126, m_weightG = 0.7152, m_weightB = 0.0722;
};

// =============================================================================
// JSRecombineLuminanceProcess
// =============================================================================

class JSRecombineLuminanceProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QObject* luminanceSource READ luminanceSource WRITE setLuminanceSource)
    Q_PROPERTY(int colorSpace READ colorSpace WRITE setColorSpace)
    Q_PROPERTY(double blend READ blend WRITE setBlend)

public:
    explicit JSRecombineLuminanceProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("RecombineLuminance"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QObject* luminanceSource() const { return m_luminanceSource; }
    int colorSpace() const { return m_colorSpace; }
    double blend() const { return m_blend; }

    void setLuminanceSource(QObject* v) { m_luminanceSource = v; }
    void setColorSpace(int v) { m_colorSpace = v; }
    void setBlend(double v) { m_blend = v; }

private:
    QObject* m_luminanceSource = nullptr;
    int m_colorSpace = 0; // HSL
    double m_blend = 1.0;
};

// =============================================================================
// JSNBtoRGBStarsProcess
// =============================================================================

class JSNBtoRGBStarsProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QString haPath READ haPath WRITE setHaPath)
    Q_PROPERTY(QString oiiiPath READ oiiiPath WRITE setOiiiPath)
    Q_PROPERTY(QString siiPath READ siiPath WRITE setSiiPath)
    Q_PROPERTY(double ratio READ ratio WRITE setRatio)
    Q_PROPERTY(double stretch READ stretch WRITE setStretch)
    Q_PROPERTY(double saturation READ saturation WRITE setSaturation)

public:
    explicit JSNBtoRGBStarsProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("NBtoRGBStars"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QString haPath() const { return m_haPath; }
    QString oiiiPath() const { return m_oiiiPath; }
    QString siiPath() const { return m_siiPath; }
    double ratio() const { return m_ratio; }
    double stretch() const { return m_stretch; }
    double saturation() const { return m_saturation; }

    void setHaPath(const QString& v) { m_haPath = v; }
    void setOiiiPath(const QString& v) { m_oiiiPath = v; }
    void setSiiPath(const QString& v) { m_siiPath = v; }
    void setRatio(double v) { m_ratio = v; }
    void setStretch(double v) { m_stretch = v; }
    void setSaturation(double v) { m_saturation = v; }

private:
    QString m_haPath, m_oiiiPath, m_siiPath;
    double m_ratio = 0.3, m_stretch = 5.0, m_saturation = 1.0;
};

// =============================================================================
// JSBinningProcess
// =============================================================================

class JSBinningProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int factor READ factor WRITE setFactor)
    Q_PROPERTY(int method READ method WRITE setMethod)

public:
    explicit JSBinningProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("Binning"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int factor() const { return m_factor; }
    int method() const { return m_method; }

    void setFactor(int v) { m_factor = v; }
    void setMethod(int v) { m_method = v; }

private:
    int m_factor = 2;
    int m_method = 0; // Sum
};

class JSPlateSolvingProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double raHint     READ raHint     WRITE setRaHint)
    Q_PROPERTY(double decHint    READ decHint    WRITE setDecHint)
    Q_PROPERTY(double radius     READ radius     WRITE setRadius)
    Q_PROPERTY(double pixelScale READ pixelScale WRITE setPixelScale)

public:
    explicit JSPlateSolvingProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("PlateSolving"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double raHint()     const { return m_raHint; }
    double decHint()    const { return m_decHint; }
    double radius()     const { return m_radius; }
    double pixelScale() const { return m_pixelScale; }

    void setRaHint(double v)     { m_raHint = v; }
    void setDecHint(double v)    { m_decHint = v; }
    void setRadius(double v)     { m_radius = v; }
    void setPixelScale(double v) { m_pixelScale = v; }

private:
    double m_raHint = 0.0, m_decHint = 0.0, m_radius = 5.0, m_pixelScale = 0.0;
};

// =============================================================================
// JSUpscaleProcess
// =============================================================================

class JSUpscaleProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double factor READ factor WRITE setFactor)
    Q_PROPERTY(int method READ method WRITE setMethod)

public:
    explicit JSUpscaleProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("Upscale"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double factor() const { return m_factor; }
    int method() const { return m_method; }

    void setFactor(double v) { m_factor = v; }
    void setMethod(int v) { m_method = v; }

private:
    double m_factor = 2.0;
    int m_method = 1; // Bilinear
};

// =============================================================================
// JSCropRotateProcess
// =============================================================================

class JSCropRotateProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int left READ left WRITE setLeft)
    Q_PROPERTY(int top READ top WRITE setTop)
    Q_PROPERTY(int width READ width WRITE setWidth)
    Q_PROPERTY(int height READ height WRITE setHeight)
    Q_PROPERTY(double angle READ angle WRITE setAngle)

public:
    explicit JSCropRotateProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("CropRotate"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int left() const { return m_left; }
    int top() const { return m_top; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double angle() const { return m_angle; }

    void setLeft(int v) { m_left = v; }
    void setTop(int v) { m_top = v; }
    void setWidth(int v) { m_width = v; }
    void setHeight(int v) { m_height = v; }
    void setAngle(double v) { m_angle = v; }

private:
    int m_left = 0, m_top = 0, m_width = 0, m_height = 0;
    double m_angle = 0.0;
};

// =============================================================================
// JSStarAnalysisProcess
// =============================================================================

class JSStarAnalysisProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold)

public:
    explicit JSStarAnalysisProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("StarAnalysis"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double threshold() const { return m_threshold; }
    void setThreshold(double v) { m_threshold = v; }

private:
    double m_threshold = 3.0;
};

// =============================================================================
// JSWavescaleHDRProcess
// =============================================================================

class JSWavescaleHDRProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int layers READ layers WRITE setLayers)
    Q_PROPERTY(double amount READ amount WRITE setAmount)

public:
    explicit JSWavescaleHDRProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("WavescaleHDR"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int layers() const { return m_layers; }
    double amount() const { return m_amount; }
    void setLayers(int v) { m_layers = v; }
    void setAmount(double v) { m_amount = v; }

private:
    int m_layers = 5;
    double m_amount = 0.5;
};

// =============================================================================
// JSStarHaloRemovalProcess
// =============================================================================

class JSStarHaloRemovalProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(double radius READ radius WRITE setRadius)
    Q_PROPERTY(double strength READ strength WRITE setStrength)
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold)

public:
    explicit JSStarHaloRemovalProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("StarHaloRemoval"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    double radius() const { return m_radius; }
    double strength() const { return m_strength; }
    double threshold() const { return m_threshold; }

    void setRadius(double v) { m_radius = v; }
    void setStrength(double v) { m_strength = v; }
    void setThreshold(double v) { m_threshold = v; }

private:
    double m_radius = 50.0, m_strength = 1.0, m_threshold = 3.0;
};

// =============================================================================
// JSAstroSpikeProcess (Diffraction Spike Generator)
// =============================================================================

class JSAstroSpikeProcess : public JSProcessBase {
    Q_OBJECT
    // Geometry
    Q_PROPERTY(int quantity READ quantity WRITE setQuantity)
    Q_PROPERTY(double angle READ angle WRITE setAngle)
    Q_PROPERTY(double length READ length WRITE setLength)
    Q_PROPERTY(double spikeWidth READ spikeWidth WRITE setSpikeWidth)
    Q_PROPERTY(double globalScale READ globalScale WRITE setGlobalScale)
    Q_PROPERTY(double intensity READ intensity WRITE setIntensity)
    
    // Appearance
    Q_PROPERTY(double colorSaturation READ colorSaturation WRITE setColorSaturation)
    Q_PROPERTY(double hueShift READ hueShift WRITE setHueShift)
    
    // Secondary
    Q_PROPERTY(double secondaryIntensity READ secondaryIntensity WRITE setSecondaryIntensity)
    Q_PROPERTY(double secondaryLength READ secondaryLength WRITE setSecondaryLength)
    Q_PROPERTY(double secondaryOffset READ secondaryOffset WRITE setSecondaryOffset)

    // Halo
    Q_PROPERTY(bool enableHalo READ enableHalo WRITE setEnableHalo)
    Q_PROPERTY(double haloIntensity READ haloIntensity WRITE setHaloIntensity)
    Q_PROPERTY(double haloScale READ haloScale WRITE setHaloScale)

public:
    explicit JSAstroSpikeProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("AstroSpike"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int quantity() const { return m_quantity; }
    double angle() const { return m_angle; }
    double length() const { return m_length; }
    double spikeWidth() const { return m_spikeWidth; }
    double globalScale() const { return m_globalScale; }
    double intensity() const { return m_intensity; }
    double colorSaturation() const { return m_colorSaturation; }
    double hueShift() const { return m_hueShift; }
    double secondaryIntensity() const { return m_secondaryIntensity; }
    double secondaryLength() const { return m_secondaryLength; }
    double secondaryOffset() const { return m_secondaryOffset; }
    bool enableHalo() const { return m_enableHalo; }
    double haloIntensity() const { return m_haloIntensity; }
    double haloScale() const { return m_haloScale; }

    void setQuantity(int v) { m_quantity = v; }
    void setAngle(double v) { m_angle = v; }
    void setLength(double v) { m_length = v; }
    void setSpikeWidth(double v) { m_spikeWidth = v; }
    void setGlobalScale(double v) { m_globalScale = v; }
    void setIntensity(double v) { m_intensity = v; }
    void setColorSaturation(double v) { m_colorSaturation = v; }
    void setHueShift(double v) { m_hueShift = v; }
    void setSecondaryIntensity(double v) { m_secondaryIntensity = v; }
    void setSecondaryLength(double v) { m_secondaryLength = v; }
    void setSecondaryOffset(double v) { m_secondaryOffset = v; }
    void setEnableHalo(bool v) { m_enableHalo = v; }
    void setHaloIntensity(double v) { m_haloIntensity = v; }
    void setHaloScale(double v) { m_haloScale = v; }

private:
    int m_quantity = 4;
    double m_angle = 0.0, m_length = 100.0, m_spikeWidth = 1.0, m_globalScale = 1.0, m_intensity = 0.5;
    double m_colorSaturation = 1.0, m_hueShift = 0.0;
    double m_secondaryIntensity = 0.0, m_secondaryLength = 50.0, m_secondaryOffset = 45.0;
    bool m_enableHalo = false;
    double m_haloIntensity = 0.5, m_haloScale = 10.0;
};

// =============================================================================
// JSStarRecompositionProcess
// =============================================================================

class JSStarRecompositionProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QObject* starLayer READ starLayer WRITE setStarLayer)
    Q_PROPERTY(double stretch READ stretch WRITE setStretch)

public:
    explicit JSStarRecompositionProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("StarRecomposition"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QObject* starLayer() const { return m_starLayer; }
    double stretch() const { return m_stretch; }
    void setStarLayer(QObject* v) { m_starLayer = v; }
    void setStretch(double v) { m_stretch = v; }

private:
    QObject* m_starLayer = nullptr;
    double m_stretch = 1.0;
};

// =============================================================================
// JSNarrowbandNormalizationProcess
// =============================================================================

class JSNarrowbandNormalizationProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int scenario READ scenario WRITE setScenario)
    Q_PROPERTY(int mode READ mode WRITE setMode)
    Q_PROPERTY(double shadowBoost READ shadowBoost WRITE setShadowBoost)
    Q_PROPERTY(double highlights READ highlights WRITE setHighlights)

public:
    explicit JSNarrowbandNormalizationProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("NarrowbandNormalization"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int scenario() const { return m_scenario; }
    int mode() const { return m_mode; }
    double shadowBoost() const { return m_shadowBoost; }
    double highlights() const { return m_highlights; }

    void setScenario(int v) { m_scenario = v; }
    void setMode(int v) { m_mode = v; }
    void setShadowBoost(double v) { m_shadowBoost = v; }
    void setHighlights(double v) { m_highlights = v; }

private:
    int m_scenario = 0; // SHO
    int m_mode = 1;     // non-linear
    double m_shadowBoost = 0.0;
    double m_highlights = 0.0;
};

// =============================================================================
// JSContinuumSubtractionProcess
// =============================================================================

class JSContinuumSubtractionProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(QObject* continuumSource READ continuumSource WRITE setContinuumSource)
    Q_PROPERTY(double redFactor READ redFactor WRITE setRedFactor)
    Q_PROPERTY(double greenFactor READ greenFactor WRITE setGreenFactor)
    Q_PROPERTY(double blueFactor READ blueFactor WRITE setBlueFactor)

public:
    explicit JSContinuumSubtractionProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("ContinuumSubtraction"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    QObject* continuumSource() const { return m_continuumSource; }
    double redFactor() const { return m_redFactor; }
    double greenFactor() const { return m_greenFactor; }
    double blueFactor() const { return m_blueFactor; }

    void setContinuumSource(QObject* v) { m_continuumSource = v; }
    void setRedFactor(double v) { m_redFactor = v; }
    void setGreenFactor(double v) { m_greenFactor = v; }
    void setBlueFactor(double v) { m_blueFactor = v; }

private:
    QObject* m_continuumSource = nullptr;
    double m_redFactor = 1.0, m_greenFactor = 1.0, m_blueFactor = 1.0;
};

// =============================================================================
// JSMultiscaleDecompProcess
// =============================================================================

class JSMultiscaleDecompProcess : public JSProcessBase {
    Q_OBJECT
    Q_PROPERTY(int layers READ layers WRITE setLayers)
    Q_PROPERTY(double detailAmount READ detailAmount WRITE setDetailAmount)

public:
    explicit JSMultiscaleDecompProcess(QObject* parent = nullptr) : JSProcessBase(parent) {}
    QString name() const override { return QStringLiteral("MultiscaleDecomp"); }
    Q_INVOKABLE bool executeOn(QObject* target) override;
    Q_INVOKABLE QVariantMap parameters() const override;

    int layers() const { return m_layers; }
    double detailAmount() const { return m_detailAmount; }
    void setLayers(int v) { m_layers = v; }
    void setDetailAmount(double v) { m_detailAmount = v; }

private:
    int m_layers = 5;
    double m_detailAmount = 1.0;
};

// =============================================================================
// Placeholder for UI-only and future tools
// =============================================================================

#define TSTAR_DECLARE_SIMPLE_PROCESS(ClassName, JsName)                      \
class ClassName : public JSProcessBase {                                     \
    Q_OBJECT                                                                 \
public:                                                                      \
    explicit ClassName(QObject* parent = nullptr) : JSProcessBase(parent) {} \
    QString name() const override { return QStringLiteral(JsName); }         \
    Q_INVOKABLE bool executeOn(QObject* target) override;                    \
    Q_INVOKABLE QVariantMap parameters() const override {                    \
        return QVariantMap();                                                \
    }                                                                        \
};

TSTAR_DECLARE_SIMPLE_PROCESS(JSAberrationInspectorProcess,       "AberrationInspector")
TSTAR_DECLARE_SIMPLE_PROCESS(JSImageBlendingProcess,             "ImageBlending")
TSTAR_DECLARE_SIMPLE_PROCESS(JSBlinkComparatorProcess,           "BlinkComparator")
TSTAR_DECLARE_SIMPLE_PROCESS(JSWCSMosaicProcess,                 "WCSMosaic")

#undef TSTAR_DECLARE_SIMPLE_PROCESS

} // namespace Scripting

#endif // JSPROCESSES_H
