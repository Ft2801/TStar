// =============================================================================
// JSProcessFactory.cpp
//
// Implementation of the factory that creates process, image, and UI control
// instances for the JavaScript scripting environment.
// =============================================================================

#include "JSProcessFactory.h"
#include "JSApi.h"
#include "JSUI.h"
#include "JSHistogramWidget.h"
#include "JSProcesses.h"

#include <QJSEngine>

namespace Scripting {

JSProcessFactory::JSProcessFactory(QJSEngine* engine, MainWindow* mainWindow,
                                   QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_mainWindow(mainWindow)
{
}

// =============================================================================
// Image creation
// =============================================================================

QObject* JSProcessFactory::createImage()
{
    // Created objects are parented to the engine so they participate in GC.
    return new JSImage(m_engine);
}

// =============================================================================
// Process creation
// =============================================================================

QObject* JSProcessFactory::createCurves()
{
    return new JSCurvesProcess(m_engine);
}

QObject* JSProcessFactory::createSaturation()
{
    return new JSSaturationProcess(m_engine);
}

QObject* JSProcessFactory::createSCNR()
{
    return new JSSCNRProcess(m_engine);
}

QObject* JSProcessFactory::createGHS()
{
    return new JSGHSProcess(m_engine);
}

QObject* JSProcessFactory::createStretch()
{
    return new JSStretchProcess(m_engine);
}

QObject* JSProcessFactory::createPixelMath()
{
    return new JSPixelMathProcess(m_engine);
}

QObject* JSProcessFactory::createArcsinhStretch()
{
    return new JSArcsinhStretchProcess(m_engine);
}

QObject* JSProcessFactory::createHistogramStretch()
{
    return new JSHistogramStretchProcess(m_engine);
}

QObject* JSProcessFactory::createStarStretch()
{
    return new JSStarStretchProcess(m_engine);
}

QObject* JSProcessFactory::createMagentaCorrection()
{
    return new JSMagentaCorrectionProcess(m_engine);
}

QObject* JSProcessFactory::createTemperatureTint()
{
    return new JSTemperatureTintProcess(m_engine);
}

QObject* JSProcessFactory::createHistogramWidget()
{
    return new JSHistogramWidget(m_engine);
}

QObject* JSProcessFactory::createGraXpert()
{
    return new JSGraXpertProcess(m_engine);
}

QObject* JSProcessFactory::createStarNet()
{
    return new JSStarNetProcess(m_engine);
}

QObject* JSProcessFactory::createCosmicClarity()
{
    return new JSCosmicClarityProcess(m_engine);
}

QObject* JSProcessFactory::createRAR()
{
    return new JSRARProcess(m_engine);
}

QObject* JSProcessFactory::createChannelCombination()
{
    return new JSChannelCombinationProcess(m_engine);
}

QObject* JSProcessFactory::createPerfectPalette()
{
    return new JSPerfectPaletteProcess(m_engine);
}

QObject* JSProcessFactory::createABE()
{
    return new JSABEProcess(m_engine);
}

QObject* JSProcessFactory::createCBE()
{
    return new JSCBEProcess(m_engine);
}

QObject* JSProcessFactory::createClahe()
{
    return new JSClaheProcess(m_engine);
}

QObject* JSProcessFactory::createMorphology()
{
    return new JSMorphologyProcess(m_engine);
}

QObject* JSProcessFactory::createMultiscaleDecomp()
{
    return new JSMultiscaleDecompProcess(m_engine);
}

QObject* JSProcessFactory::createPCC()
{
    return new JSPCCProcess(m_engine);
}

QObject* JSProcessFactory::createSPCC()
{
    return new JSSPCCProcess(m_engine);
}

QObject* JSProcessFactory::createBackgroundNeutralization()
{
    return new JSBackgroundNeutralizationProcess(m_engine);
}

QObject* JSProcessFactory::createSelectiveColor()
{
    return new JSSelectiveColorProcess(m_engine);
}

QObject* JSProcessFactory::createAberrationInspector()
{
    return new JSAberrationInspectorProcess(m_engine);
}

QObject* JSProcessFactory::createAlignChannels()
{
    return new JSAlignChannelsProcess(m_engine);
}

QObject* JSProcessFactory::createExtractLuminance()
{
    return new JSExtractLuminanceProcess(m_engine);
}

QObject* JSProcessFactory::createRecombineLuminance()
{
    return new JSRecombineLuminanceProcess(m_engine);
}

QObject* JSProcessFactory::createStarRecomposition()
{
    return new JSStarRecompositionProcess(m_engine);
}

QObject* JSProcessFactory::createImageBlending()
{
    return new JSImageBlendingProcess(m_engine);
}

QObject* JSProcessFactory::createDebayer()
{
    return new JSDebayerProcess(m_engine);
}

QObject* JSProcessFactory::createContinuumSubtraction()
{
    return new JSContinuumSubtractionProcess(m_engine);
}

QObject* JSProcessFactory::createNarrowbandNormalization()
{
    return new JSNarrowbandNormalizationProcess(m_engine);
}

QObject* JSProcessFactory::createNBtoRGBStars()
{
    return new JSNBtoRGBStarsProcess(m_engine);
}

QObject* JSProcessFactory::createPlateSolving()
{
    return new JSPlateSolvingProcess(m_engine);
}

QObject* JSProcessFactory::createBinning()
{
    return new JSBinningProcess(m_engine);
}

QObject* JSProcessFactory::createUpscale()
{
    return new JSUpscaleProcess(m_engine);
}

QObject* JSProcessFactory::createStarAnalysis()
{
    return new JSStarAnalysisProcess(m_engine);
}

QObject* JSProcessFactory::createWavescaleHDR()
{
    return new JSWavescaleHDRProcess(m_engine);
}

QObject* JSProcessFactory::createStarHaloRemoval()
{
    return new JSStarHaloRemovalProcess(m_engine);
}

QObject* JSProcessFactory::createBlinkComparator()
{
    return new JSBlinkComparatorProcess(m_engine);
}

QObject* JSProcessFactory::createWCSMosaic()
{
    return new JSWCSMosaicProcess(m_engine);
}

QObject* JSProcessFactory::createAstroSpike()
{
    return new JSAstroSpikeProcess(m_engine);
}

QObject* JSProcessFactory::createCropRotate()
{
    return new JSCropRotateProcess(m_engine);
}

// =============================================================================
// UI control creation
// =============================================================================

QObject* JSProcessFactory::createDialog()
{
    // JSDialog is parented to the engine for GC participation.
    // It requires a MainWindow pointer to use setupToolSubwindow().
    return new JSDialog(m_mainWindow, m_engine);
}

QObject* JSProcessFactory::createVerticalSizer()
{
    return new JSVerticalSizer(m_engine);
}

QObject* JSProcessFactory::createHorizontalSizer()
{
    return new JSHorizontalSizer(m_engine);
}

QObject* JSProcessFactory::createLabel(const QString& text)
{
    auto* lbl = new JSLabel(m_engine);
    if (!text.isEmpty())
        lbl->setText(text);
    return lbl;
}

QObject* JSProcessFactory::createPushButton(const QString& text)
{
    auto* btn = new JSPushButton(m_engine);
    if (!text.isEmpty())
        btn->setText(text);
    return btn;
}

QObject* JSProcessFactory::createSlider()
{
    return new JSSlider(m_engine);
}

QObject* JSProcessFactory::createSpinBox()
{
    return new JSSpinBox(m_engine);
}

QObject* JSProcessFactory::createCheckBox(const QString& text)
{
    auto* cb = new JSCheckBox(m_engine);
    if (!text.isEmpty())
        cb->setText(text);
    return cb;
}

QObject* JSProcessFactory::createComboBox()
{
    return new JSComboBox(m_engine);
}

} // namespace Scripting
