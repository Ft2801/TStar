// =============================================================================
// JSRuntime.cpp
//
// Implementation of the TStar JavaScript runtime environment.
// =============================================================================

#include "JSRuntime.h"
#include "JSApi.h"
#include "JSApp.h"
#include "JSProcessFactory.h"
#include "MainWindow.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QThread>
#include <QDebug>
#include <QCoreApplication>

namespace Scripting {

// =============================================================================
// JSRuntime
// =============================================================================

JSRuntime::JSRuntime(MainWindow* mainWindow, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
    registerAPIs();
}

JSRuntime::~JSRuntime()
{
    cancelExecution();
}

MainWindow* JSRuntime::mainWindow() const
{
    return m_mainWindow;
}

void JSRuntime::registerAPIs()
{
    // -------------------------------------------------------------------------
    // 1. Console object â€” provides console.log/warn/error/info
    // -------------------------------------------------------------------------

    m_console = new JSConsole(this, this);
    QJSValue consoleVal = m_engine.newQObject(m_console);
    m_engine.globalObject().setProperty("Console", consoleVal);

    // Also create a lowercase 'console' with standard JS semantics
    m_engine.evaluate(
        "var console = {"
        "  log:   function() { var a=[]; for(var i=0;i<arguments.length;i++) a.push(String(arguments[i])); Console.log(a.join(' ')); },"
        "  warn:  function() { var a=[]; for(var i=0;i<arguments.length;i++) a.push(String(arguments[i])); Console.warn(a.join(' ')); },"
        "  error: function() { var a=[]; for(var i=0;i<arguments.length;i++) a.push(String(arguments[i])); Console.error(a.join(' ')); },"
        "  info:  function() { var a=[]; for(var i=0;i<arguments.length;i++) a.push(String(arguments[i])); Console.info(a.join(' ')); }"
        "};"
    );

    // -------------------------------------------------------------------------
    // 2. Application context
    // -------------------------------------------------------------------------

    if (m_mainWindow) {
        m_app = new JSApp(m_mainWindow, this, this);
        QJSValue appVal = m_engine.newQObject(m_app);
        m_engine.globalObject().setProperty("App", appVal);
    }

    // -------------------------------------------------------------------------
    // 3. Process & UI factory
    //
    // The factory now receives MainWindow* so it can construct JSDialog
    // objects that need access to setupToolSubwindow().
    // -------------------------------------------------------------------------

    m_factory = new JSProcessFactory(&m_engine, m_mainWindow, this);
    QJSValue factoryVal = m_engine.newQObject(m_factory);
    m_engine.globalObject().setProperty("__factory", factoryVal);

    // -------------------------------------------------------------------------
    // 4. Global constructor shims â€” Processes
    //    These mimic PixInsight's pattern: var c = new Curves();
    // -------------------------------------------------------------------------

    m_engine.evaluate(
        "function Curves()            { return __factory.createCurves(); }\n"
        "function Saturation()        { return __factory.createSaturation(); }\n"
        "function SCNR()              { return __factory.createSCNR(); }\n"
        "function GHS()               { return __factory.createGHS(); }\n"
        "function GHT()               { return __factory.createGHS(); }\n"
        "function Stretch()           { return __factory.createStretch(); }\n"
        "function PixelMath()         { return __factory.createPixelMath(); }\n"
        "function ArcsinhStretch()    { return __factory.createArcsinhStretch(); }\n"
        "function HistogramStretch()  { return __factory.createHistogramStretch(); }\n"
        "function StarStretch()       { return __factory.createStarStretch(); }\n"
        "function MagentaCorrection() { return __factory.createMagentaCorrection(); }\n"
        "function TemperatureTint()   { return __factory.createTemperatureTint(); }\n"
        "function GraXpert()          { return __factory.createGraXpert(); }\n"
        "function StarNet()           { return __factory.createStarNet(); }\n"
        "function CosmicClarity()     { return __factory.createCosmicClarity(); }\n"
        "function RAR()               { return __factory.createRAR(); }\n"
        "function ChannelCombination(){ return __factory.createChannelCombination(); }\n"
        "function PerfectPalette()    { return __factory.createPerfectPalette(); }\n"
        "function Image()             { return __factory.createImage(); }\n"
        "function ABE()               { return __factory.createABE(); }\n"
        "function CBE()               { return __factory.createCBE(); }\n"
        "function PCC()               { return __factory.createPCC(); }\n"
        "function SPCC()              { return __factory.createSPCC(); }\n"
        "function BackgroundNeutralization() { return __factory.createBackgroundNeutralization(); }\n"
        "function SelectiveColor()    { return __factory.createSelectiveColor(); }\n"
        "function AberrationInspector() { return __factory.createAberrationInspector(); }\n"
        "function AlignChannels()     { return __factory.createAlignChannels(); }\n"
        "function ExtractLuminance()  { return __factory.createExtractLuminance(); }\n"
        "function RecombineLuminance(){ return __factory.createRecombineLuminance(); }\n"
        "function StarRecomposition() { return __factory.createStarRecomposition(); }\n"
        "function ImageBlending()     { return __factory.createImageBlending(); }\n"
        "function Debayer()           { return __factory.createDebayer(); }\n"
        "function ContinuumSubtraction() { return __factory.createContinuumSubtraction(); }\n"
        "function NarrowbandNormalization() { return __factory.createNarrowbandNormalization(); }\n"
        "function NBtoRGBStars()      { return __factory.createNBtoRGBStars(); }\n"
        "function PlateSolving()      { return __factory.createPlateSolving(); }\n"
        "function Binning()           { return __factory.createBinning(); }\n"
        "function Upscale()           { return __factory.createUpscale(); }\n"
        "function StarAnalysis()      { return __factory.createStarAnalysis(); }\n"
        "function WavescaleHDR()      { return __factory.createWavescaleHDR(); }\n"
        "function Clahe()             { return __factory.createClahe(); }\n"
        "function StarHaloRemoval()   { return __factory.createStarHaloRemoval(); }\n"
        "function Morphology()        { return __factory.createMorphology(); }\n"
        "function MultiscaleDecomp()  { return __factory.createMultiscaleDecomp(); }\n"
        "function BlinkComparator()   { return __factory.createBlinkComparator(); }\n"
        "function WCSMosaic()         { return __factory.createWCSMosaic(); }\n"
        "function AstroSpike()        { return __factory.createAstroSpike(); }\n"
        "function CropRotate()        { return __factory.createCropRotate(); }\n"
    );

    // -------------------------------------------------------------------------
    // 5. Global constructor shims â€” UI Controls
    //
    //    Matches the PixInsight / TStar scripting style:
    //      var d    = new Dialog();
    //      var lbl  = new Label("text");
    //      var btn  = new PushButton("OK");
    //      var sl   = new Slider();
    //      var sp   = new SpinBox();
    //      var cb   = new CheckBox("Enable");
    //      var drop = new ComboBox();
    //      var vs   = new VerticalSizer();
    //      var hs   = new HorizontalSizer();
    // -------------------------------------------------------------------------

    m_engine.evaluate(
        "function Dialog()            { return __factory.createDialog(); }\n"
        "function VerticalSizer()     { return __factory.createVerticalSizer(); }\n"
        "function HorizontalSizer()   { return __factory.createHorizontalSizer(); }\n"
        "function Label(text)         { return __factory.createLabel(text || ''); }\n"
        "function PushButton(text)    { return __factory.createPushButton(text || ''); }\n"
        "function Slider()            { return __factory.createSlider(); }\n"
        "function SpinBox()           { return __factory.createSpinBox(); }\n"
        "function CheckBox(text)      { return __factory.createCheckBox(text || ''); }\n"
        "function ComboBox()          { return __factory.createComboBox(); }\n"
        "function ScriptHistogram()   { return __factory.createHistogramWidget(); }\n"
    );

    // -------------------------------------------------------------------------
    // 6. Utility helpers in global scope
    // -------------------------------------------------------------------------

    m_engine.evaluate(
        "function sleep(ms) { App.sleep(ms); }\n"
    );
}

// =============================================================================
// Script evaluation
// =============================================================================

QString JSRuntime::evaluate(const QString& script, const QString& fileName)
{
    m_engine.setInterrupted(false);
    QJSValue result = m_engine.evaluate(script, fileName.isEmpty() ? "script" : fileName);

    if (result.isError()) {
        QString errorMsg = QString("%1:%2 â€” %3")
            .arg(fileName.isEmpty() ? "Script" : fileName)
            .arg(result.property("lineNumber").toInt())
            .arg(result.toString());

        // Include stack trace if available
        QJSValue stack = result.property("stack");
        if (!stack.isUndefined()) {
            errorMsg += "\n" + stack.toString();
        }

        emit standardError(errorMsg);
        return errorMsg;
    }

    if (!result.isUndefined()) {
        return result.toString();
    }
    return QString();
}

QString JSRuntime::evaluateFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString err = tr("Cannot open script file: %1").arg(filePath);
        emit standardError(err);
        return err;
    }

    QTextStream in(&file);
    QString script = in.readAll();
    file.close();

    return evaluate(script, QFileInfo(filePath).fileName());
}

void JSRuntime::evaluateAsync(const QString& script, const QString& fileName)
{
    if (m_running) {
        emit standardError(tr("A script is already running."));
        return;
    }

    m_running = true;
    emit scriptStarted();

    QThread* thread = new QThread();
    m_activeWorker = new ScriptWorker(m_mainWindow, script, fileName);
    m_activeWorker->moveToThread(thread);

    connect(thread, &QThread::started,  m_activeWorker, &ScriptWorker::run);
    connect(m_activeWorker, &ScriptWorker::finished, this, [this](bool success) {
        m_running = false;
        m_activeWorker = nullptr;
        emit scriptFinished(success);
    });
    
    // Forward logging
    connect(m_activeWorker, &ScriptWorker::standardOutput, this, &JSRuntime::standardOutput);
    connect(m_activeWorker, &ScriptWorker::standardError,  this, &JSRuntime::standardError);

    // Cleanup
    connect(m_activeWorker, &ScriptWorker::finished, thread, &QThread::quit);
    connect(m_activeWorker, &ScriptWorker::finished, m_activeWorker, &ScriptWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    m_currentWorkerThread = thread;
    thread->start();
}

void JSRuntime::cancelExecution()
{
    if (m_running && m_activeWorker) {
        QMetaObject::invokeMethod(m_activeWorker, "requestCancellation", Qt::QueuedConnection);
    }
}

// =============================================================================
// ScriptWorker Implementation
// =============================================================================

ScriptWorker::ScriptWorker(MainWindow* mainWindow, const QString& script, const QString& fileName)
    : m_mainWindow(mainWindow)
    , m_script(script)
    , m_fileName(fileName)
{
}

ScriptWorker::~ScriptWorker()
{
    // Ensure we clear any remaining references to the thread's runtime
    m_runtime = nullptr;
}

void ScriptWorker::run()
{
    // Create a local runtime for this thread. 
    // This creates its own QJSEngine in this thread.
    JSRuntime runtime(m_mainWindow);
    m_runtime = &runtime;
    
    connect(&runtime, &JSRuntime::standardOutput, this, &ScriptWorker::standardOutput);
    connect(&runtime, &JSRuntime::standardError,  this, &ScriptWorker::standardError);

    // If cancellation was requested before we even started
    if (m_isCancelled) {
        runtime.engine()->setInterrupted(true);
    }

    runtime.evaluate(m_script, m_fileName);
    
    // Check if evaluation finished because of interruption
    bool success = !runtime.engine()->isInterrupted();
    if (!success) {
        emit standardError(tr("Script execution was cancelled."));
    }

    m_runtime = nullptr;
    emit finished(success);
}

void ScriptWorker::requestCancellation()
{
    m_isCancelled = true;
    if (m_runtime) {
        // Explicitly stop the background engine.
        // QJSEngine::setInterrupted is thread-safe.
        m_runtime->engine()->setInterrupted(true);
    }
}

// =============================================================================
// API Reference Builder (static)
// =============================================================================

QVariantList JSRuntime::buildApiReference()
{
    QVariantList ref;

    // -------------------------------------------------------------------------
    // Console
    // -------------------------------------------------------------------------
    {
        QVariantMap obj;
        obj["name"]        = "Console";
        obj["description"] = "Logging facility. Output appears in the Script Console.";
        obj["type"]        = "global";

        QVariantList methods;
        methods << QVariantMap{{"name", "log(msg)"},   {"desc", "Print an informational message."}};
        methods << QVariantMap{{"name", "warn(msg)"},  {"desc", "Print a warning message (yellow)."}};
        methods << QVariantMap{{"name", "error(msg)"}, {"desc", "Print an error message (red)."}};
        methods << QVariantMap{{"name", "info(msg)"},  {"desc", "Print an info message with [INFO] prefix."}};
        obj["methods"] = methods;

        obj["example"] = "Console.log('Hello, TStar!');\nConsole.warn('Low memory');\nConsole.error('File not found');";
        ref << obj;
    }

    // -------------------------------------------------------------------------
    // App
    // -------------------------------------------------------------------------
    {
        QVariantMap obj;
        obj["name"]        = "App";
        obj["description"] = "Application context. Access open image windows, open files, and control the app.";
        obj["type"]        = "global";

        QVariantList props;
        props << QVariantMap{{"name", "version"}, {"desc", "Current application version string."}};
        props << QVariantMap{{"name", "appName"}, {"desc", "Always returns \"TStar\"."}};
        obj["properties"] = props;

        QVariantList methods;
        methods << QVariantMap{{"name", "activeWindow()"},      {"desc", "Returns JSView for the focused image window, or null."}};
        methods << QVariantMap{{"name", "windows()"},           {"desc", "Returns array of JSView for all open image windows."}};
        methods << QVariantMap{{"name", "open(filePath)"},      {"desc", "Open an image file and return its JSView."}};
        methods << QVariantMap{{"name", "sleep(ms)"},           {"desc", "Pause execution for the given number of milliseconds."}};
        methods << QVariantMap{{"name", "log(message, type)"}, {"desc", "Log to the app console. type: 0=Info, 1=Success, 2=Warning, 3=Error."}};
        obj["methods"] = methods;

        obj["example"] = "var view = App.activeWindow();\nif (view) {\n  Console.log('Active: ' + view.title);\n}";
        ref << obj;
    }

    // -------------------------------------------------------------------------
    // Dialog (UI)
    // -------------------------------------------------------------------------
    {
        QVariantMap obj;
        obj["name"]        = "Dialog";
        obj["description"] = "Creates a native TStar tool dialog window. Supports layout-based UI with Labels, Buttons, Sliders, and more.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "windowTitle"}, {"desc", "Title bar text. Default: \"Script Dialog\"."}};
        props << QVariantMap{{"name", "width"},        {"desc", "Initial dialog width in pixels. Default: 380."}};
        props << QVariantMap{{"name", "height"},       {"desc", "Initial dialog height in pixels. Default: 240."}};
        obj["properties"] = props;

        QVariantList methods;
        methods << QVariantMap{{"name", "setSizer(layout)"}, {"desc", "Set the root VerticalSizer or HorizontalSizer."}};
        methods << QVariantMap{{"name", "execute()"},        {"desc", "Show the dialog and block the script until it is closed. Returns 1 (ok) or 0 (cancel)."}};
        methods << QVariantMap{{"name", "ok()"},             {"desc", "Close the dialog with result 1 (from a button callback)."}};
        methods << QVariantMap{{"name", "cancel()"},         {"desc", "Close the dialog with result 0 (from a button callback)."}};
        obj["methods"] = methods;

        obj["example"] =
            "var d = new Dialog();\n"
            "d.windowTitle = 'My Tool';\n"
            "var sizer = new VerticalSizer();\n"
            "var lbl = new Label('Smoothing:');\n"
            "sizer.add(lbl);\n"
            "var sl = new Slider();\n"
            "sl.min = 0; sl.max = 100;\n"
            "sizer.add(sl);\n"
            "var btnOk = new PushButton('Apply');\n"
            "btnOk.onClick = function() { d.ok(); };\n"
            "sizer.add(btnOk);\n"
            "d.setSizer(sizer);\n"
            "if (d.execute() === 1) {\n"
            "  Console.log('Value: ' + sl.value);\n"
            "}";
        ref << obj;
    }

    // -------------------------------------------------------------------------
    // UI â€” Layout sizers
    // -------------------------------------------------------------------------
    {
        QVariantMap obj;
        obj["name"]        = "VerticalSizer / HorizontalSizer";
        obj["description"] = "Layout containers that stack controls vertically or horizontally. Nest them to build complex UIs.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "spacing"}, {"desc", "Pixels between items. Default: 6."}};
        props << QVariantMap{{"name", "margin"},  {"desc", "Content margin in pixels. Default: 0."}};
        obj["properties"] = props;

        QVariantList methods;
        methods << QVariantMap{{"name", "add(control, stretch)"}, {"desc", "Add a control or nested sizer. Optional stretch factor (default 0)."}};
        methods << QVariantMap{{"name", "addStretch(factor)"},    {"desc", "Insert a flexible spacer. Expands to fill available space."}};
        methods << QVariantMap{{"name", "addSpacing(pixels)"},    {"desc", "Insert a fixed gap of the given pixel size."}};
        obj["methods"] = methods;

        obj["example"] =
            "var vs = new VerticalSizer();\n"
            "var hs = new HorizontalSizer();\n"
            "hs.add(new Label('Name:'));\n"
            "hs.addStretch();\n"
            "vs.add(hs);\n"
            "vs.addSpacing(8);\n"
            "d.setSizer(vs);";
        ref << obj;
    }

    // -------------------------------------------------------------------------
    // UI â€” Widgets
    // -------------------------------------------------------------------------
    {
        QVariantMap obj;
        obj["name"]        = "Label";
        obj["description"] = "A static text label.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "text"},      {"desc", "The label text (supports HTML)."}};
        props << QVariantMap{{"name", "wordWrap"},  {"desc", "Enable word-wrapping. Default: false."}};
        props << QVariantMap{{"name", "alignment"}, {"desc", "Qt alignment flags as int. Default: Left+VCenter."}};
        obj["properties"] = props;

        obj["example"] = "var lbl = new Label('<b>Smoothing Factor</b>');\nlbl.wordWrap = true;";
        ref << obj;
    }
    {
        QVariantMap obj;
        obj["name"]        = "PushButton";
        obj["description"] = "A clickable button. Assign a JS function to onClick.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "text"},      {"desc", "Button label."}};
        props << QVariantMap{{"name", "checkable"}, {"desc", "If true, the button acts as a toggle. Default: false."}};
        props << QVariantMap{{"name", "checked"},   {"desc", "Current toggle state."}};
        props << QVariantMap{{"name", "onClick"},   {"desc", "JS function called when clicked."}};
        obj["properties"] = props;

        obj["example"] = "var btn = new PushButton('Apply');\nbtn.onClick = function() {\n  Console.log('Clicked!');\n  d.ok();\n};";
        ref << obj;
    }
    {
        QVariantMap obj;
        obj["name"]        = "Slider";
        obj["description"] = "A horizontal integer range slider.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "min"},            {"desc", "Minimum value. Default: 0."}};
        props << QVariantMap{{"name", "max"},            {"desc", "Maximum value. Default: 100."}};
        props << QVariantMap{{"name", "value"},          {"desc", "Current value."}};
        props << QVariantMap{{"name", "step"},           {"desc", "Single step increment. Default: 1."}};
        props << QVariantMap{{"name", "onValueChanged"}, {"desc", "JS function(newValue) called on every change."}};
        obj["properties"] = props;

        obj["example"] = "var sl = new Slider();\nsl.min = 0; sl.max = 200; sl.value = 50;\nsl.onValueChanged = function(v) { Console.log(v); };";
        ref << obj;
    }
    {
        QVariantMap obj;
        obj["name"]        = "SpinBox";
        obj["description"] = "A floating-point numeric input with up/down arrows.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "min"},            {"desc", "Minimum value. Default: 0.0."}};
        props << QVariantMap{{"name", "max"},            {"desc", "Maximum value. Default: 1.0."}};
        props << QVariantMap{{"name", "value"},          {"desc", "Current value."}};
        props << QVariantMap{{"name", "step"},           {"desc", "Increment per click. Default: 0.01."}};
        props << QVariantMap{{"name", "precision"},      {"desc", "Decimal places shown. Default: 2."}};
        props << QVariantMap{{"name", "suffix"},         {"desc", "Unit suffix string (e.g. \" px\")."}};
        props << QVariantMap{{"name", "onValueChanged"}, {"desc", "JS function(newValue) called on change."}};
        obj["properties"] = props;

        obj["example"] = "var sp = new SpinBox();\nsp.min = 0.0; sp.max = 5.0; sp.step = 0.05;\nsp.suffix = ' Ïƒ';";
        ref << obj;
    }
    {
        QVariantMap obj;
        obj["name"]        = "CheckBox";
        obj["description"] = "A toggleable checkbox with a text label.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "text"},      {"desc", "Label text."}};
        props << QVariantMap{{"name", "checked"},   {"desc", "True when checked. Default: false."}};
        props << QVariantMap{{"name", "onToggled"}, {"desc", "JS function(checked) called on state change."}};
        obj["properties"] = props;

        obj["example"] = "var cb = new CheckBox('Link channels');\ncb.checked = true;\ncb.onToggled = function(v) { Console.log('Linked: ' + v); };";
        ref << obj;
    }
    {
        QVariantMap obj;
        obj["name"]        = "ComboBox";
        obj["description"] = "A drop-down selection list.";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "selectedIndex"},  {"desc", "Currently selected item index. Default: 0."}};
        props << QVariantMap{{"name", "currentText"},    {"desc", "Text of the currently selected item (read-only)."}};
        props << QVariantMap{{"name", "items"},          {"desc", "Array of all item strings (read-only)."}};
        props << QVariantMap{{"name", "onIndexChanged"}, {"desc", "JS function(index) called when selection changes."}};
        obj["properties"] = props;

        QVariantList methods;
        methods << QVariantMap{{"name", "addItem(text)"},  {"desc", "Append an item to the list."}};
        methods << QVariantMap{{"name", "clearItems()"},   {"desc", "Remove all items."}};
        obj["methods"] = methods;

        obj["example"] =
            "var cb = new ComboBox();\n"
            "cb.addItem('Linear');\n"
            "cb.addItem('Logarithmic');\n"
            "cb.selectedIndex = 0;\n"
            "cb.onIndexChanged = function(i) { Console.log('Mode: ' + i); };";
        ref << obj;
    }

    {
        QVariantMap obj;
        obj["name"]        = "ScriptHistogram";
        obj["description"] = "An embeddable histogram display widget. Shows per-channel histograms with optional ghost overlay, log scale, grid, and transform curve. Feed data from JSImage::computeHistogram().";
        obj["type"]        = "ui";

        QVariantList props;
        props << QVariantMap{{"name", "logScale"},  {"desc", "Use logarithmic Y axis. Default: false."}};
        props << QVariantMap{{"name", "showGrid"},  {"desc", "Draw grid lines. Default: true."}};
        props << QVariantMap{{"name", "showCurve"}, {"desc", "Draw transform curve overlay. Default: true."}};
        props << QVariantMap{{"name", "minHeight"}, {"desc", "Minimum height in pixels. Default: 120."}};
        obj["properties"] = props;

        QVariantList methods;
        methods << QVariantMap{{"name", "setData(data, channels)"},      {"desc", "Feed histogram bins from computeHistogram(). data is Array of Arrays."}};
        methods << QVariantMap{{"name", "setGhostData(data, channels)"}, {"desc", "Feed a reference histogram shown as a faint overlay."}};
        methods << QVariantMap{{"name", "setCurve(lut)"},                {"desc", "Set the transform curve overlay. lut is an Array of [0..1] floats."}};
        methods << QVariantMap{{"name", "clear()"},                      {"desc", "Remove all data and the curve overlay."}};
        obj["methods"] = methods;

        obj["example"] =
            "var hist = new ScriptHistogram();\n"
            "hist.logScale = false;\n"
            "hist.showGrid = true;\n"
            "sizer.add(hist, 1); // stretch=1 so it expands\n"
            "// Feed data:\n"
            "var data = view.image.computeHistogram(256);\n"
            "hist.setData(data, view.image.channels);";
        ref << obj;
    }

    // -------------------------------------------------------------------------
    // Image
    // -------------------------------------------------------------------------
    {
        QVariantMap obj;
        obj["name"]        = "Image";
        obj["description"] = "A standalone image buffer. Can load from disk, be processed independently, and saved back.";
        obj["type"]        = "data";

        QVariantList props;
        props << QVariantMap{{"name", "width"},    {"desc", "Image width in pixels (read-only)."}};
        props << QVariantMap{{"name", "height"},   {"desc", "Image height in pixels (read-only)."}};
        props << QVariantMap{{"name", "channels"}, {"desc", "Number of channels â€” 1 (mono) or 3 (RGB) (read-only)."}};
        props << QVariantMap{{"name", "isValid"},  {"desc", "True if the image contains data (read-only)."}};
        obj["properties"] = props;

        QVariantList methods;
        methods << QVariantMap{{"name", "load(filePath)"},               {"desc", "Load from FITS, TIFF, XISF, or PNG."}};
        methods << QVariantMap{{"name", "save(filePath, format, bits)"}, {"desc", "Save to disk. format: fits/tiff/xisf/png; bits: 0=8, 1=16, 2=32int, 3=32float."}};
        methods << QVariantMap{{"name", "getPixel(x, y, ch)"},           {"desc", "Read a single pixel value [0..1]."}};
        methods << QVariantMap{{"name", "setPixel(x, y, ch, v)"},        {"desc", "Write a single pixel value [0..1]."}};
        methods << QVariantMap{{"name", "clone()"},                      {"desc", "Return a deep copy of this image."}};
        methods << QVariantMap{{"name", "medians()"},                    {"desc", "Per-channel median values as array."}};
        methods << QVariantMap{{"name", "getStatistics()"},              {"desc", "Object with min/max/mean/median per channel."}};
        methods << QVariantMap{{"name", "computeHistogram(bins)"},       {"desc", "Per-channel histogram as nested array."}};
        methods << QVariantMap{{"name", "getHeaderValue(key)"},          {"desc", "Read a FITS header value by keyword."}};
        obj["methods"] = methods;

        obj["example"] =
            "var img = new Image();\n"
            "img.load('/path/to/image.fits');\n"
            "Console.log('Size: ' + img.width + 'x' + img.height);\n"
            "var s = new Stretch();\n"
            "s.executeOn(img);\n"
            "img.save('/path/to/output.fits');";
        ref << obj;
    }

    // -------------------------------------------------------------------------
    // Process objects (abbreviated â€” kept from original implementation)
    // -------------------------------------------------------------------------

    auto addProcess = [&](const QString& name, const QString& desc,
                           const QVariantList& props, const QString& example) {
        QVariantMap obj;
        obj["name"]        = name;
        obj["description"] = desc;
        obj["type"]        = "process";
        obj["properties"]  = props;

        QVariantList methods;
        methods << QVariantMap{{"name", "executeOn(image)"}, {"desc", QString("Apply %1 to the given JSImage.").arg(name)}};
        methods << QVariantMap{{"name", "parameters()"},     {"desc", "Return current parameter map."}};
        obj["methods"] = methods;
        obj["example"] = example;
        ref << obj;
    };

    addProcess("Curves",
        "Non-linear tone-curve adjustment. Individual curve points are set via the 'curve' property.",
        { QVariantMap{{"name","curve"},     {"desc","Array of [x,y] control points in [0..1] range."}},
          QVariantMap{{"name","channel"},   {"desc","0=Luminance, 1=Red, 2=Green, 3=Blue, 4=Saturation."}} },
        "var c = new Curves();\nc.channel = 0;\nc.curve = [[0,0],[0.5,0.6],[1,1]];\nc.executeOn(view.image);\nview.refresh();"
    );

    addProcess("Saturation",
        "Adjusts per-hue saturation using a curve.",
        { QVariantMap{{"name","amount"},  {"desc","Global saturation multiplier. Default: 1.0."}},
          QVariantMap{{"name","protect"}, {"desc","Protect near-neutral colours. Default: false."}} },
        "var s = new Saturation();\ns.amount = 1.4;\ns.executeOn(view.image);\nview.refresh();"
    );

    addProcess("GHS",
        "Generalised Hyperbolic Stretch â€” flexible non-linear histogram transform.",
        { QVariantMap{{"name","D"},  {"desc","Stretch intensity (logâ‚â‚€ scale). Default: 0.0."}},
          QVariantMap{{"name","B"},  {"desc","Symmetry parameter. Default: 0.0."}},
          QVariantMap{{"name","SP"}, {"desc","Stretch point [0..1]. Default: 0.0."}},
          QVariantMap{{"name","HP"}, {"desc","Highlight protection [0..1]. Default: 1.0."}},
          QVariantMap{{"name","BP"}, {"desc","Black point. Default: 0.0."}} },
        "var g = new GHS();\ng.D = 5.0; g.B = 0.3; g.SP = 0.15;\ng.executeOn(view.image);\nview.refresh();"
    );

    addProcess("Stretch",
        "Statistical auto-stretch. Computes optimal midtone and black-point from image statistics.",
        { QVariantMap{{"name","targetMedian"},    {"desc","Target median brightness [0..1]. Default: 0.25."}},
          QVariantMap{{"name","linked"},          {"desc","Link RGB channels. Default: true."}},
          QVariantMap{{"name","blackpointSigma"}, {"desc","Sigma threshold for black point. Default: 5.0."}} },
        "var s = new Stretch();\ns.targetMedian = 0.25;\ns.executeOn(view.image);\nview.refresh();"
    );

    addProcess("PixelMath",
        "Evaluates mathematical expressions on image pixels.",
        { QVariantMap{{"name","expression"}, {"desc","The math expression, e.g. \"(r+g+b)/3\"."}},
          QVariantMap{{"name","rescale"},    {"desc","Rescale result to [0..1]. Default: false."}} },
        "var pm = new PixelMath();\npm.expression = \"(r + g + b) / 3\";\npm.executeOn(view.image);\nview.refresh();"
    );

    addProcess("ArcsinhStretch",
        "Color-preserving non-linear stretch based on the inverse hyperbolic sine.",
        { QVariantMap{{"name","stretchFactor"},  {"desc","Stretch strength. Default: 1.0."}},
          QVariantMap{{"name","blackPoint"},     {"desc","Black clipping point. Default: 0.0."}},
          QVariantMap{{"name","humanLuminance"}, {"desc","Perceptual luminance weighting. Default: true."}} },
        "var a = new ArcsinhStretch();\na.stretchFactor = 100; a.blackPoint = 0.01;\na.executeOn(view.image);\nview.refresh();"
    );

    addProcess("HistogramStretch",
        "Traditional MTF histogram transform.",
        { QVariantMap{{"name","shadows"},    {"desc","Shadow clip [0..1]. Default: 0.0."}},
          QVariantMap{{"name","midtones"},   {"desc","Midtone balance [0..1]. Default: 0.5."}},
          QVariantMap{{"name","highlights"}, {"desc","Highlight clip [0..1]. Default: 1.0."}} },
        "var h = new HistogramStretch();\nh.midtones = 0.05;\nh.executeOn(view.image);\nview.refresh();"
    );

    addProcess("StarStretch",
        "Specialized non-linear growth pipeline for star images.",
        { QVariantMap{{"name","stretchAmount"}, {"desc","Growth factor. Default: 1.0."}},
          QVariantMap{{"name","colorBoost"},    {"desc","Saturation multiplier. Default: 1.0."}} },
        "var s = new StarStretch();\ns.stretchAmount = 5.0;\ns.executeOn(view.image);\nview.refresh();"
    );

    addProcess("MagentaCorrection",
        "Removes magenta halos from stars.",
        { QVariantMap{{"name","amount"},    {"desc","Correction strength [0..1]. Default: 0.5."}},
          QVariantMap{{"name","threshold"}, {"desc","Minimum value to correct. Default: 0.1."}} },
        "var m = new MagentaCorrection();\nm.amount = 0.8;\nm.executeOn(view.image);\nview.refresh();"
    );

    addProcess("TemperatureTint",
        "White balance adjustment via color temperature and tint.",
        { QVariantMap{{"name","temperature"}, {"desc","Warm/Cool shift [-100..100]. Default: 0."}},
          QVariantMap{{"name","tint"},        {"desc","Green/Magenta shift [-100..100]. Default: 0."}} },
        "var t = new TemperatureTint();\nt.temperature = 10;\nt.executeOn(view.image);\nview.refresh();"
    );

    addProcess("SCNR",
        "Selective colour noise reduction â€” removes unwanted channel contamination.",
        { QVariantMap{{"name","channel"}, {"desc","Target channel to suppress (default: green = 1)."}},
          QVariantMap{{"name","amount"},  {"desc","Suppression strength [0..1]. Default: 1.0."}} },
        "var s = new SCNR();\ns.amount = 0.8;\ns.executeOn(view.image);\nview.refresh();"
    );

    addProcess("GraXpert",
        "AI-powered background extraction and advanced noise reduction.",
        { QVariantMap{{"name","operation"}, {"desc","0=Bg Extraction, 1=Denoise. Default: 0."}},
          QVariantMap{{"name","tolerance"}, {"desc","Tolerance for gradient calculation."}},
          QVariantMap{{"name","smoothing"}, {"desc","Smoothing level."}},
          QVariantMap{{"name","denoiseStrength"}, {"desc","Denoise strength for operation 1."}} },
        "var gx = new GraXpert();\ngx.operation = 1;\ngx.denoiseStrength = 0.7;\ngx.executeOn(view.image);\nview.refresh();"
    );

    addProcess("StarNet",
        "AI-powered star removal network separating stars from the background.",
        { QVariantMap{{"name","stride"},  {"desc","Patch stride. Default: 256"}},
          QVariantMap{{"name","linear"},  {"desc","Linear mode processing. Default: true"}} },
        "var sn = new StarNet();\nsn.stride = 256;\nsn.executeOn(view.image);\nview.refresh();"
    );

    addProcess("CosmicClarity",
        "AI-powered denoise and sharpen process.",
        { QVariantMap{{"name","mode"}, {"desc","0=Both, 1=Denoise, 2=Sharpen. Default: 0"}} },
        "var cc = new CosmicClarity();\ncc.mode = 0;\ncc.executeOn(view.image);\nview.refresh();"
    );

    addProcess("RAR",
        "AI-powered residual aberration removal.",
        { QVariantMap{{"name","strength"}, {"desc","Removal strength. Default: 1.0"}} },
        "var rar = new RAR();\nrar.strength = 1.0;\nrar.executeOn(view.image);\nview.refresh();"
    );

    addProcess("ChannelCombination",
        "Combine multiple grayscale images into a single RGB image.",
        { QVariantMap{{"name","red"},   {"desc","Image ID for the red channel"}},
          QVariantMap{{"name","green"}, {"desc","Image ID for the green channel"}},
          QVariantMap{{"name","blue"},  {"desc","Image ID for the blue channel"}},
          QVariantMap{{"name","lum"},   {"desc","Optional Image ID for luminance channel"}} },
        "var cc = new ChannelCombination();\ncc.red = 'Image01';\ncc.executeOn(view.image);\nview.refresh();"
    );

    addProcess("PerfectPalette",
        "Automatically combine narrowband channels into standard palettes.",
        { QVariantMap{{"name","palette"}, {"desc","Target palette (0=SHO, 1=HOO, etc)."}} },
        "var pp = new PerfectPalette();\npp.palette = 0;\npp.executeOn(view.image);\nview.refresh();"
    );

    addProcess("ABE",
        "Automatic Background Extraction — removes gradients from the image.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new ABE();\np.params = {};\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("CBE",
        "Manual/Custom Background Extraction.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new CBE();\np.params = {};\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("PCC",
        "Photometric Color Calibration using plate-solved star data.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new PCC();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("SPCC",
        "Spectrophotometric Color Calibration.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new SPCC();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("BackgroundNeutralization",
        "Neutralize the background to a flat neutral grey.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new BackgroundNeutralization();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("SelectiveColor",
        "Hue-targeted colour adjustment.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new SelectiveColor();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("AberrationInspector",
        "Analyse star shapes across the field for optical aberrations.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new AberrationInspector();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("AlignChannels",
        "Align R/G/B channels to correct chromatic shift.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new AlignChannels();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("ExtractLuminance",
        "Extract the luminance channel from an RGB image.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new ExtractLuminance();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("RecombineLuminance",
        "Recombine a luminance image with an RGB image.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new RecombineLuminance();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("StarRecomposition",
        "Recompose separated stars back into the image.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new StarRecomposition();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("ImageBlending",
        "Blend two images together with a configurable mode.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new ImageBlending();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("Debayer",
        "Debayer (demosaic) a raw Bayer-pattern image to RGB.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new Debayer();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("ContinuumSubtraction",
        "Subtract broadband continuum from a narrowband image.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new ContinuumSubtraction();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("NarrowbandNormalization",
        "Normalize narrowband channel intensities.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new NarrowbandNormalization();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("NBtoRGBStars",
        "Map narrowband data into RGB star colours.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new NBtoRGBStars();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("PlateSolving",
        "Astrometric plate-solving to embed WCS coordinates.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new PlateSolving();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("Binning",
        "Bin pixels to reduce resolution and increase SNR.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new Binning();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("Upscale",
        "AI-powered or bicubic image upscaling.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new Upscale();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("StarAnalysis",
        "Detect and measure stars: FWHM, eccentricity, flux.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new StarAnalysis();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("WavescaleHDR",
        "Wavelet-based HDR compression for nebula detail.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new WavescaleHDR();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("Clahe",
        "Contrast-Limited Adaptive Histogram Equalisation.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new Clahe();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("StarHaloRemoval",
        "Reduce halos around bright stars.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new StarHaloRemoval();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("Morphology",
        "Morphological operations: erosion, dilation, opening, closing.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new Morphology();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("MultiscaleDecomp",
        "Multi-scale wavelet decomposition and reconstruction.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new MultiscaleDecomp();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("BlinkComparator",
        "Visual blink comparison between two images.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new BlinkComparator();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("WCSMosaic",
        "WCS-aligned mosaic assembly from multiple frames.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new WCSMosaic();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("AstroSpike",
        "Add synthetic diffraction spikes to bright stars.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new AstroSpike();\np.executeOn(view.image);\nview.refresh();"
    );

    addProcess("CropRotate",
        "Crop and rotate the image.",
        { QVariantMap{{"name","params"}, {"desc","Generic parameter map."}} },
        "var p = new CropRotate();\np.executeOn(view.image);\nview.refresh();"
    );

    return ref;
}

// =============================================================================
// JSConsole
// =============================================================================

JSConsole::JSConsole(JSRuntime* runtime, QObject* parent)
    : QObject(parent)
    , m_runtime(runtime)
{
}

void JSConsole::log(const QString& msg)
{
    emit m_runtime->standardOutput(msg);
}

void JSConsole::warn(const QString& msg)
{
    emit m_runtime->standardOutput(QString("[WARN] %1").arg(msg));
}

void JSConsole::error(const QString& msg)
{
    emit m_runtime->standardError(msg);
}

void JSConsole::info(const QString& msg)
{
    emit m_runtime->standardOutput(QString("[INFO] %1").arg(msg));
}

} // namespace Scripting
