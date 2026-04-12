// =============================================================================
// JSProcessFactory.h
//
// Factory object registered in the JS global scope. Creates process instances
// and UI control instances via Q_INVOKABLE methods so that scripts can use
// constructor-like syntax:
//
//   var curves  = new Curves();          // calls __factory.createCurves()
//   var img     = new Image();           // calls __factory.createImage()
//   var dialog  = new Dialog();          // calls __factory.createDialog()
//   var btn     = new PushButton();      // calls __factory.createPushButton()
// =============================================================================

#ifndef JSPROCESSFACTORY_H
#define JSPROCESSFACTORY_H

#include <QObject>

class QJSEngine;
class MainWindow;

namespace Scripting {

class JSProcessFactory : public QObject {
    Q_OBJECT

public:
    explicit JSProcessFactory(QJSEngine* engine, MainWindow* mainWindow,
                              QObject* parent = nullptr);
    ~JSProcessFactory() override = default;

    // =========================================================================
    // Image creation
    // =========================================================================

    /** @brief Create an empty JSImage. */
    Q_INVOKABLE QObject* createImage();

    // =========================================================================
    // Process creation
    // =========================================================================

    /** @brief Create a Curves process instance. */
    Q_INVOKABLE QObject* createCurves();

    /** @brief Create a Saturation process instance. */
    Q_INVOKABLE QObject* createSaturation();

    /** @brief Create an SCNR process instance. */
    Q_INVOKABLE QObject* createSCNR();

    /** @brief Create a GHS process instance. */
    Q_INVOKABLE QObject* createGHS();

    /** @brief Create a Stretch process instance. */
    Q_INVOKABLE QObject* createStretch();

    /** @brief Create a PixelMath process instance. */
    Q_INVOKABLE QObject* createPixelMath();

    /** @brief Create an ArcsinhStretch process instance. */
    Q_INVOKABLE QObject* createArcsinhStretch();

    /** @brief Create a HistogramStretch process instance. */
    Q_INVOKABLE QObject* createHistogramStretch();

    /** @brief Create a StarStretch process instance. */
    Q_INVOKABLE QObject* createStarStretch();

    /** @brief Create a MagentaCorrection process instance. */
    Q_INVOKABLE QObject* createMagentaCorrection();

    /** @brief Create a TemperatureTint process instance. */
    Q_INVOKABLE QObject* createTemperatureTint();

    /** @brief Create a GraXpert process instance. */
    Q_INVOKABLE QObject* createGraXpert();

    /** @brief Create a StarNet process instance. */
    Q_INVOKABLE QObject* createStarNet();
    
    /** @brief Create a CosmicClarity process instance. */
    Q_INVOKABLE QObject* createCosmicClarity();

    /** @brief Create a RAR process instance. */
    Q_INVOKABLE QObject* createRAR();

    /** @brief Create a ChannelCombination process instance. */
    Q_INVOKABLE QObject* createChannelCombination();

    /** @brief Create a PerfectPalette process instance. */
    Q_INVOKABLE QObject* createPerfectPalette();

    /** @brief Create an ABE process instance. */
    Q_INVOKABLE QObject* createABE();

    /** @brief Create a CBE process instance. */
    Q_INVOKABLE QObject* createCBE();

    /** @brief Create a Clahe process instance. */
    Q_INVOKABLE QObject* createClahe();

    /** @brief Create a Morphology process instance. */
    Q_INVOKABLE QObject* createMorphology();

    /** @brief Create a MultiscaleDecomp process instance. */
    Q_INVOKABLE QObject* createMultiscaleDecomp();

    /** @brief Create a PCC process instance. */
    Q_INVOKABLE QObject* createPCC();

    /** @brief Create an SPCC process instance. */
    Q_INVOKABLE QObject* createSPCC();

    /** @brief Create a BackgroundNeutralization process instance. */
    Q_INVOKABLE QObject* createBackgroundNeutralization();

    /** @brief Create a SelectiveColor process instance. */
    Q_INVOKABLE QObject* createSelectiveColor();

    /** @brief Create an AberrationInspector process instance. */
    Q_INVOKABLE QObject* createAberrationInspector();

    /** @brief Create an AlignChannels process instance. */
    Q_INVOKABLE QObject* createAlignChannels();

    /** @brief Create an ExtractLuminance process instance. */
    Q_INVOKABLE QObject* createExtractLuminance();

    /** @brief Create a RecombineLuminance process instance. */
    Q_INVOKABLE QObject* createRecombineLuminance();

    /** @brief Create a StarRecomposition process instance. */
    Q_INVOKABLE QObject* createStarRecomposition();

    /** @brief Create an ImageBlending process instance. */
    Q_INVOKABLE QObject* createImageBlending();

    /** @brief Create a Debayer process instance. */
    Q_INVOKABLE QObject* createDebayer();

    /** @brief Create a ContinuumSubtraction process instance. */
    Q_INVOKABLE QObject* createContinuumSubtraction();

    /** @brief Create a NarrowbandNormalization process instance. */
    Q_INVOKABLE QObject* createNarrowbandNormalization();

    /** @brief Create an NBtoRGBStars process instance. */
    Q_INVOKABLE QObject* createNBtoRGBStars();

    /** @brief Create a PlateSolving process instance. */
    Q_INVOKABLE QObject* createPlateSolving();

    /** @brief Create a Binning process instance. */
    Q_INVOKABLE QObject* createBinning();

    /** @brief Create an Upscale process instance. */
    Q_INVOKABLE QObject* createUpscale();

    /** @brief Create a StarAnalysis process instance. */
    Q_INVOKABLE QObject* createStarAnalysis();

    /** @brief Create a WavescaleHDR process instance. */
    Q_INVOKABLE QObject* createWavescaleHDR();

    /** @brief Create a StarHaloRemoval process instance. */
    Q_INVOKABLE QObject* createStarHaloRemoval();

    /** @brief Create a BlinkComparator process instance. */
    Q_INVOKABLE QObject* createBlinkComparator();

    /** @brief Create a WCSMosaic process instance. */
    Q_INVOKABLE QObject* createWCSMosaic();

    /** @brief Create an AstroSpike process instance. */
    Q_INVOKABLE QObject* createAstroSpike();

    /** @brief Create a CropRotate process instance. */
    Q_INVOKABLE QObject* createCropRotate();

    // =========================================================================
    // UI control creation
    // =========================================================================

    /** @brief Create a JSDialog (root tool window container). */
    Q_INVOKABLE QObject* createDialog();

    /** @brief Create a JSVerticalSizer. */
    Q_INVOKABLE QObject* createVerticalSizer();

    /** @brief Create a JSHorizontalSizer. */
    Q_INVOKABLE QObject* createHorizontalSizer();

    /** @brief Create a JSLabel with optional initial text. */
    Q_INVOKABLE QObject* createLabel(const QString& text = QString());

    /** @brief Create a JSPushButton with optional label text. */
    Q_INVOKABLE QObject* createPushButton(const QString& text = QString());

    /** @brief Create a JSSlider (horizontal, integer range). */
    Q_INVOKABLE QObject* createSlider();

    /** @brief Create a JSSpinBox (floating-point value input). */
    Q_INVOKABLE QObject* createSpinBox();

    /** @brief Create a JSCheckBox with optional label text. */
    Q_INVOKABLE QObject* createCheckBox(const QString& text = QString());

    /** @brief Create a JSComboBox (drop-down selector). */
    Q_INVOKABLE QObject* createComboBox();

    /**
     * @brief Create a JSHistogramWidget - an embeddable histogram display.
     *
     * The widget renders channel histograms, supports ghost overlays, log
     * scale, grid lines, and a transform curve overlay.  Feed it with data
     * from JSImage::computeHistogram() inside your dialog callbacks.
     */
    Q_INVOKABLE QObject* createHistogramWidget();

private:
    QJSEngine*   m_engine;
    MainWindow*  m_mainWindow;  ///< Required for JSDialog construction.
};

} // namespace Scripting

#endif // JSPROCESSFACTORY_H
