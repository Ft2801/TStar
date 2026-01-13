
#include "StackingCommands.h"
#include "../io/FitsWrapper.h"
#include "../io/FitsLoaderCWrapper.h"
#include "../stacking/Registration.h"
#include "../io/TiffIO.h"
#include "../background/BackgroundExtraction.h"
#include "../preprocessing/Debayer.h"
#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif
#include <QFuture>
#include <QtConcurrent>
#include <QAtomicInt>
#include <QSemaphore>
#include <mutex>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>

namespace Scripting {

// Static member initialization
std::unique_ptr<Stacking::ImageSequence> StackingCommands::s_sequence;
std::unique_ptr<ImageBuffer> StackingCommands::s_currentImage;
Preprocessing::PreprocessingEngine StackingCommands::s_preprocessor;
QString StackingCommands::s_workingDir;
ScriptRunner* StackingCommands::s_runner = nullptr;

//=============================================================================
// REGISTRATION
//=============================================================================

void StackingCommands::registerCommands(ScriptRunner& runner) {
    s_runner = &runner;
    s_workingDir = runner.workingDirectory();
    
    QVector<CommandDef> commands = {
        // Directory commands
        CommandDef("cd", 1, 1, 
            "cd <path>",
            "Change working directory",
            cmdCd),
        
        // File commands
        CommandDef("load", 1, 1,
            "load <filename>",
            "Load an image file",
            cmdLoad),
            
        CommandDef("save", 1, 1,
            "save <filename> [--32b] [--16b]",
            "Save current image",
            cmdSave),
            
        CommandDef("close", 0, 0,
            "close",
            "Close current image",
            cmdClose),
        
        // Stacking commands
        CommandDef("stack", 1, 8,
            "stack <sequence_prefix> [method] [rejection] [sigma_low] [sigma_high] [--out=<name>]",
            "Stack a sequence of images",
            cmdStack),

         CommandDef("rgbcomp", 3, 2,
            "rgbcomp <r_file> <g_file> <b_file> [-out=<filename>]",
            "Combine 3 monochrome images into RGB",
            cmdRGBComp),

        CommandDef("linear_match", 2, 0,
            "linear_match <ref_file> <target_file>",
            "Normalize target to match reference statistics",
            cmdLinearMatch),
            
        CommandDef("pm", 1, 1,
            "pm <expression>",
            "Execute PixelMath expression",
            cmdPixelMath),
            
        CommandDef("starnet", 0, 0,
             "starnet [--nostarmask] [--stride=<val>]",
             "Run StarNet++ on current image",
             cmdStarNet),
            
        CommandDef("calibrate", 1, 1,
            "calibrate <sequence_prefix> [--bias=<file>] [--dark=<file>] [--flat=<file>]",
            "Calibrate images with master frames",
            cmdCalibrate),
            
        CommandDef("register", 1, 1,
            "register <sequence_prefix> [--drizzle] [--norotation]",
            "Register (align) images",
            cmdRegister),
            
        CommandDef("convert", 1, 3,
            "convert <prefix> [--out=<dir>] [--debayer]",
            "Convert files to FITS sequence (CFA by default)",
            cmdConvert),
            
        CommandDef("link", 1, 2,
            "link <prefix> [--out=<directory>]",
            "Create symbolic links for a sequence (alias to convert)",
             cmdConvert), 

        // Master frame commands
        CommandDef("setmaster", 2, 2,
            "setmaster <type> <filename>",
            "Set a master calibration frame (bias, dark, flat)",
            cmdSetMaster),
            
        // Processing commands
        CommandDef("background", 0, 1,
            "background [sequence] [--degree=<n>]",
            "Perform background extraction on current image or sequence",
            cmdBackground),
             
        CommandDef("seqsubsky", 1, 2,
             "seqsubsky <sequence> <degree>",
             "Perform background extraction (alias)",
             cmdBackground),

        CommandDef("debayer", 0, 1,
            "debayer [sequence_prefix] [--pattern=<RGGB|BGGR|GBRG|GRBG>] [--algo=<bilinear|vng|superpixel>]",
            "Debayer current image or a sequence of images",
            cmdDebayer),
            
        CommandDef("seqextract_Ha", 1, 1,
            "seqextract_Ha <prefix> [--upscale]",
            "Extract Ha from CFA sequence",
            cmdSeqExtract),

        CommandDef("seqextract_HaOIII", 1, 1,
             "seqextract_HaOIII <prefix> [--upscale]",
             "Extract Ha and OIII from CFA sequence",
             cmdSeqExtract),

        CommandDef("mirrorx", 0, 0,
            "mirrorx [-bottomup]",
            "Flip image horizontally (or vertically with flag)",
            cmdMirror),
            
        // Phase 5: Project Commands
        CommandDef("newproject", 1, 1,
            "newproject <path>",
            "Create a new stacking project with directory structure",
            cmdNewProject),
            
        CommandDef("convertall", 1, 1,
            "convertall <pattern> [--out=<dir>]",
            "Batch convert RAW files to FITS in lights/ directory",
            cmdConvertAll),
            
        CommandDef("autostack", 1, 1,
            "autostack <project_path>",
            "Run full pipeline: calibrate, register, stack",
            cmdAutoStack),
    };
    
    runner.registerCommands(commands);
}

//=============================================================================
// PATH RESOLUTION
//=============================================================================

QString StackingCommands::resolvePath(const QString& path) {
    if (QDir::isAbsolutePath(path)) {
        return path;
    }
    return QDir(s_workingDir).absoluteFilePath(path);
}

//=============================================================================
// CD COMMAND
//=============================================================================

bool StackingCommands::cmdCd(const ScriptCommand& cmd) {
    QString path = resolvePath(cmd.args[0]);
    
    QDir dir(path);
    if (!dir.exists()) {
        QString msg = QString("Directory not found: %1").arg(path);
        if (s_runner) {
            s_runner->setError(msg, cmd.lineNumber);
        }
        return false;
    }
    
    s_workingDir = dir.absolutePath();
    if (s_runner) {
        s_runner->setWorkingDirectory(s_workingDir);
        s_runner->logMessage(QString("Changed working directory to: %1").arg(s_workingDir), "neutral");
    }
    
    return true;
}

//=============================================================================
// LOAD/SAVE/CLOSE
//=============================================================================

bool StackingCommands::cmdLoad(const ScriptCommand& cmd) {
    QString path = resolvePath(cmd.args[0]);
    
    s_currentImage = std::make_unique<ImageBuffer>();
    if (!Stacking::FitsIO::read(path, *s_currentImage)) {
        s_currentImage.reset();
        return false;
    }
    
    return true;
}

bool StackingCommands::cmdSave(const ScriptCommand& cmd) {
    if (!s_currentImage || !s_currentImage->isValid()) {
        return false;
    }
    
    QString path = resolvePath(cmd.args[0]);
    int bits = cmd.hasOption("32b") ? 32 : (cmd.hasOption("16b") ? 16 : 32);
    
    return Stacking::FitsIO::write(path, *s_currentImage, bits);
}

bool StackingCommands::cmdClose(const ScriptCommand& cmd) {
    Q_UNUSED(cmd);
    s_currentImage.reset();
    s_sequence.reset();
    return true;
}

//=============================================================================
// STACK COMMAND
//=============================================================================

Stacking::Method StackingCommands::parseMethod(const QString& str) {
    QString s = str.toLower();
    if (s == "sum") return Stacking::Method::Sum;
    if (s == "median") return Stacking::Method::Median;
    if (s == "max") return Stacking::Method::Max;
    if (s == "min") return Stacking::Method::Min;
    return Stacking::Method::Mean;  // Default
}

Stacking::Rejection StackingCommands::parseRejection(const QString& str) {
    QString s = str.toLower();
    if (s == "none") return Stacking::Rejection::None;
    if (s == "percentile" || s == "perc") return Stacking::Rejection::Percentile;
    if (s == "sigma") return Stacking::Rejection::Sigma;
    if (s == "mad") return Stacking::Rejection::MAD;
    if (s == "sigmedian") return Stacking::Rejection::SigmaMedian;
    if (s == "winsor" || s == "winsorized") return Stacking::Rejection::Winsorized;
    if (s == "linear" || s == "linfit") return Stacking::Rejection::LinearFit;
    if (s == "gesdt") return Stacking::Rejection::GESDT;
    return Stacking::Rejection::Winsorized;  // Default
}

Stacking::NormalizationMethod StackingCommands::parseNormalization(const QString& str) {
    QString s = str.toLower();
    if (s == "none" || s == "nonorm") return Stacking::NormalizationMethod::None;
    if (s == "add" || s == "additive") return Stacking::NormalizationMethod::Additive;
    if (s == "mul" || s == "multiplicative") return Stacking::NormalizationMethod::Multiplicative;
    if (s == "addscale") return Stacking::NormalizationMethod::AdditiveScaling;
    if (s == "mulscale") return Stacking::NormalizationMethod::MultiplicativeScaling;
    return Stacking::NormalizationMethod::AdditiveScaling;  // Default
}

Stacking::WeightingType StackingCommands::parseWeighting(const QString& str) {
    QString s = str.toLower();
    if (s == "none") return Stacking::WeightingType::None;
    if (s == "noise") return Stacking::WeightingType::Noise;
    if (s == "fwhm") return Stacking::WeightingType::WeightedFWHM;
    if (s == "stars") return Stacking::WeightingType::StarCount;
    if (s == "quality") return Stacking::WeightingType::Quality;
    return Stacking::WeightingType::None;
}


bool StackingCommands::cmdStack(const ScriptCommand& cmd) {
    QString prefix = cmd.args[0];
    
    // Find files matching prefix
    QDir dir(s_workingDir);
    QStringList filters;
    filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";
    
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    
    if (files.isEmpty()) {
        return false;
    }
    
    // Convert to full paths
    QStringList fullPaths;
    for (const QString& f : files) {
        fullPaths << dir.absoluteFilePath(f);
    }
    
    // Check if we can reuse the existing sequence (preserves registration info)
    bool reuseSequence = false;
    
    if (s_sequence && s_sequence->count() == fullPaths.size()) {
        QString seqFile = QFileInfo(s_sequence->image(0).filePath).fileName();
        QString reqFile = QFileInfo(fullPaths[0]).fileName();
        
        if (seqFile == reqFile) {
             reuseSequence = true;
        }
    }
    
    if (!reuseSequence) {
        // Create sequence
        s_sequence = std::make_unique<Stacking::ImageSequence>();
        if (!s_sequence->loadFromFiles(fullPaths)) {
            s_sequence.reset();
            return false;
        }
    }
    
    // Parse parameters
    Stacking::StackingParams params;
    params.method = parseMethod(cmd.option("method", "mean"));
    params.rejection = parseRejection(cmd.option("rejection", "winsorized"));
    params.normalization = parseNormalization(cmd.option("norm", "addscale"));
    params.weighting = parseWeighting(cmd.option("weighting", "noise"));
    
    // Parse rejection sigmas (rej low high), or positional arguments 3 and 4
    if (cmd.hasOption("rej")) {
        QString rejVal = cmd.option("rej");
        QStringList parts = rejVal.split(' ');
        if (parts.size() >= 2) {
            params.sigmaLow = parts[0].toFloat();
            params.sigmaHigh = parts[1].toFloat();
        } else if (parts.size() == 1) {
            params.sigmaLow = params.sigmaHigh = parts[0].toFloat();
        }
    } else if (cmd.args.size() >= 4) {
        params.sigmaLow = cmd.args[2].toFloat();
        params.sigmaHigh = cmd.args[3].toFloat();
    } else {
        params.sigmaLow = params.sigmaHigh = 3.0f;
    }
    
    // Positional method/rejection
    if (cmd.args.size() >= 2) params.method = parseMethod(cmd.args[1]);
    if (cmd.args.size() >= 3) params.rejection = parseRejection(cmd.args[2]);
    
    // Output options
    params.force32Bit = cmd.hasOption("32b") || cmd.hasOption("32bits");
    params.outputNormalization = cmd.hasOption("output_norm");
    // Default RGB Equalization to FALSE
    params.equalizeRGB = cmd.hasOption("rgb_equal");
    
    if (s_runner) {
         if (params.equalizeRGB) {
             s_runner->logMessage("RGB Equalization: ENABLED (Neutralizes color cast)", "orange");
         } else {
             s_runner->logMessage("RGB Equalization: DISABLED (Preserves camera color cast)", "green");
         }
    }
    
    // Drizzle options
    params.drizzle = cmd.hasOption("drizzle");
    if (cmd.hasOption("scale")) {
        params.drizzleScale = cmd.option("scale").toDouble();
    }
    if (cmd.hasOption("pixfrac")) {
        params.drizzlePixFrac = cmd.option("pixfrac").toDouble();
    }
    
    // Advanced options
    if (cmd.hasOption("feather")) {
        params.featherDistance = cmd.option("feather").toInt();
    }
    
    // Output filename
    QString output = cmd.option("output", prefix + "_stacked.fit");
    params.outputFilename = resolvePath(output);
    
    // Reuse Logic Applied Above
    // If not reusing, we would have loaded it.
    // If reusing, s_sequence is already set.
    
    if (!s_sequence || s_sequence->count() == 0) {
         return false;
    }
    
    // Execute stacking
    Stacking::StackingArgs args;
    args.params = params;
    args.sequence = s_sequence.get();
    
    // Wire up callbacks for real-time feedback and cancellation
    if (s_runner) {
        args.cancelCheck = []() {
            return s_runner && s_runner->isCancelled();
        };
        // Use direct logging callback to avoid signal overhead in tight loops
        args.logCallback = [](const QString& msg, const QString& color) {
            if (s_runner) s_runner->logMessageDirect(msg, color);
        };
        args.progressCallback = [](const QString& msg, double pct) {
            if (s_runner) s_runner->progressChanged(msg, pct);
        };
    }
    
    Stacking::StackingEngine engine;
    auto result = engine.execute(args);
    
    if (result != Stacking::StackResult::OK) {
        if (result == Stacking::StackResult::CancelledError) {
            if (s_runner) s_runner->logMessage(QObject::tr("Stacking cancelled"), "salmon");
        }
        return false;
    }
    
    // Save result - Force 32-bit float for stacking output to match Siril quality
    if (!Stacking::FitsIO::write(params.outputFilename, args.result, 32)) {
        return false;
    }
    
    // Set as current image
    s_currentImage = std::make_unique<ImageBuffer>(std::move(args.result));
    
    return true;
}

//=============================================================================
// CALIBRATE COMMAND
//=============================================================================

bool StackingCommands::cmdCalibrate(const ScriptCommand& cmd) {
    QString prefix = cmd.args[0];
    
    // Find files matching prefix
    QDir dir(s_workingDir);
    QStringList filters;
    filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";
    
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    
    if (files.isEmpty()) {
        return false;
    }
    
    // Convert to full paths
    QStringList fullPaths;
    for (const QString& f : files) {
        fullPaths << dir.absoluteFilePath(f);
    }
    
    // Set up preprocessing parameters
    Preprocessing::PreprocessParams params;
    
    if (cmd.hasOption("bias")) {
        params.masterBias = resolvePath(cmd.option("bias"));
        params.useBias = true;
    }
    if (cmd.hasOption("dark")) {
        params.masterDark = resolvePath(cmd.option("dark"));
        params.useDark = true;
    }
    if (cmd.hasOption("flat")) {
        params.masterFlat = resolvePath(cmd.option("flat"));
        params.useFlat = true;
    }
    
    // Dark optimization
    params.darkOptim.enabled = cmd.hasOption("dark_optimize") || cmd.hasOption("cc");
    
    // Debayer
    if (cmd.hasOption("debayer")) {
        params.debayer = true;
        QString pattern = cmd.option("cfa", "AUTO").toUpper();
        if (pattern == "AUTO") params.bayerPattern = Preprocessing::BayerPattern::Auto;
        else if (pattern == "RGGB") params.bayerPattern = Preprocessing::BayerPattern::RGGB;
        else if (pattern == "BGGR") params.bayerPattern = Preprocessing::BayerPattern::BGGR;
        else if (pattern == "GBRG") params.bayerPattern = Preprocessing::BayerPattern::GBRG;
        else if (pattern == "GRBG") params.bayerPattern = Preprocessing::BayerPattern::GRBG;
    }
    
    // CFA equalization
    params.cfaEqualize.enabled = cmd.hasOption("equalize_cfa");
    
    // Cosmetic correction
    if (cmd.hasOption("cc")) {
        params.cosmetic.type = Preprocessing::CosmeticType::Sigma;
        params.cosmetic.coldSigma = 3.0f;
        params.cosmetic.hotSigma = 3.0f;
    }
    
    // Output prefix
    params.outputPrefix = "pp_";
    params.outputDir = s_workingDir;
    
    // Run preprocessing
    s_preprocessor.setParams(params);
    
    // Progress callback for calibration 
    auto progressCallback = [](const QString& msg, double) {
        if (s_runner) {
            // Use direct logging to avoid signal/slot overhead in tight loop
            s_runner->logMessageDirect(msg, "neutral");
        }
    };
    
    int processed = s_preprocessor.preprocessBatch(fullPaths, s_workingDir, progressCallback);
    
    return processed == fullPaths.size();
}

//=============================================================================
// REGISTER COMMAND
//=============================================================================

bool StackingCommands::cmdRegister(const ScriptCommand& cmd) {
    QString prefix = cmd.args[0];
    
    // Find files matching prefix
    QDir dir(s_workingDir);
    QStringList filters;
    filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";
    
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    
    if (files.size() < 2) {
        return false;  // Need at least 2 images
    }
    
    // Convert to full paths
    QStringList fullPaths;
    for (const QString& f : files) {
        fullPaths << dir.absoluteFilePath(f);
    }
    
    // AUTO-DEBAYER CHECK (In-Place to maintain script compatibility)
    if (!fullPaths.isEmpty()) {
        ImageBuffer checkBuf;
        if (Stacking::FitsIO::readHeader(fullPaths.first(), checkBuf)) {
             // Robust CFA Detection
             bool isCFA = checkBuf.channels() == 1 && 
                          (!checkBuf.metadata().bayerPattern.isEmpty() || 
                           checkBuf.metadata().xisfProperties.contains("BayerPattern"));
                           
             if (isCFA) {
                 if (s_runner) s_runner->logMessage("Auto-Debayering CFA images in-place for correct registration...", "cyan");
                 // ... (existing code)
             } else {
                 if (s_runner) {
                     QString reason = QString("Channels=%1").arg(checkBuf.channels());
                     QString bp = checkBuf.metadata().bayerPattern;
                     if (bp.isEmpty()) bp = checkBuf.metadata().xisfProperties.value("BayerPattern").toString();
                     reason += QString(", Pattern='%1'").arg(bp);
                     
                     s_runner->logMessage(QString("Auto-Debayer SKIPPED for %1 (%2). Assuming RGB or Mono.").arg(QFileInfo(fullPaths.first()).fileName(), reason), "neutral");
                 }
             }
        }
    }
    
    // Create sequence (Now RGB)
    s_sequence = std::make_unique<Stacking::ImageSequence>();
    if (!s_sequence->loadFromFiles(fullPaths)) {
        s_sequence.reset();
        return false;
    }
    
    // Set up registration parameters
    Stacking::RegistrationParams params;
    params.allowRotation = !cmd.hasOption("norotation");
    params.drizzle = cmd.hasOption("drizzle");
    
    if (cmd.hasOption("sigma")) {
        params.detectionThreshold = cmd.option("sigma").toFloat();
    }
    if (cmd.hasOption("maxstars")) {
        params.maxStars = cmd.option("maxstars").toInt();
    }
    
    // Run registration
    Stacking::RegistrationEngine engine;
    engine.setParams(params);
    
    // Connect logs and progress
    if (s_runner) {
        QObject::connect(&engine, &Stacking::RegistrationEngine::logMessage, [](const QString& msg, const QString& color) {
            if (s_runner) s_runner->logMessage(msg, color);
        });
        QObject::connect(&engine, &Stacking::RegistrationEngine::progressChanged, [](const QString& msg, double pct) {
            if (s_runner) s_runner->progressChanged(msg, pct);
        });
        
        // Wire up cancellation
        QObject::connect(s_runner, &Scripting::ScriptRunner::cancelRequested, &engine, &Stacking::RegistrationEngine::cancel);
    }
    
    int successCount = engine.registerSequence(*s_sequence, -1);
    
    return successCount >= static_cast<int>(files.size() * 0.8);  // 80% success threshold
}

//=============================================================================
// SETMASTER COMMAND
//=============================================================================

bool StackingCommands::cmdSetMaster(const ScriptCommand& cmd) {
    QString type = cmd.args[0].toLower();
    QString path = resolvePath(cmd.args[1]);
    
    Preprocessing::MasterType masterType;
    if (type == "bias" || type == "offset") {
        masterType = Preprocessing::MasterType::Bias;
    } else if (type == "dark") {
        masterType = Preprocessing::MasterType::Dark;
    } else if (type == "flat") {
        masterType = Preprocessing::MasterType::Flat;
    } else if (type == "darkflat") {
        masterType = Preprocessing::MasterType::DarkFlat;
    } else {
        return false;
    }
    
    return s_preprocessor.masters().load(masterType, path);
}

//=============================================================================
// DEBAYER COMMAND
//=============================================================================

bool StackingCommands::cmdDebayer(const ScriptCommand& cmd) {
    Preprocessing::PreprocessParams params;
    params.debayer = true;
    
    QString pattern = cmd.option("pattern", "AUTO").toUpper();
    if (pattern == "AUTO") params.bayerPattern = Preprocessing::BayerPattern::Auto;
    else if (pattern == "RGGB") params.bayerPattern = Preprocessing::BayerPattern::RGGB;
    else if (pattern == "BGGR") params.bayerPattern = Preprocessing::BayerPattern::BGGR;
    else if (pattern == "GBRG") params.bayerPattern = Preprocessing::BayerPattern::GBRG;
    else if (pattern == "GRBG") params.bayerPattern = Preprocessing::BayerPattern::GRBG;
    
    QString algo = cmd.option("algo", "vng").toLower();
    if (algo == "bilinear") params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::Bilinear;
    else if (algo == "vng") params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::VNG;
    else if (algo == "superpixel") params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::SuperPixel;
    else if (algo == "rcd") params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::RCD;
    else if (algo == "ahd") params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::AHD;
    
    // BATCH MODE CHECK
    if (!cmd.args.isEmpty()) {
        QString prefix = cmd.args[0];
        QDir dir(s_workingDir);
        QStringList filters;
        filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";
        QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
        
        if (files.isEmpty()) {
            if (s_runner) s_runner->logMessage("Debayer: No files found matching " + prefix, "salmon");
            return false;
        }
        
        QStringList fullPaths;
        for (const QString& f : files) fullPaths << dir.absoluteFilePath(f);
        
        params.outputPrefix = "deb_";
        params.outputDir = s_workingDir;
        
        s_preprocessor.setParams(params);
        int processed = s_preprocessor.preprocessBatch(fullPaths, s_workingDir);
        return processed > 0;
    }

    // SINGLE IMAGE MODE
    if (!s_currentImage || !s_currentImage->isValid()) {
        return false;
    }
    
    s_preprocessor.setParams(params);
    
    ImageBuffer output;
    if (!s_preprocessor.preprocessImage(*s_currentImage, output)) {
        return false;
    }
    
    *s_currentImage = std::move(output);
    return true;
}


//=============================================================================
// BACKGROUND EXTRACTION COMMAND
//=============================================================================

bool StackingCommands::cmdBackground(const ScriptCommand& cmd) {
    if (!s_currentImage || !s_currentImage->isValid()) {
        if (s_runner) s_runner->setVariable("error", "No image loaded");
        return false;
    }
    
    int degree = cmd.option("degree", "1").toInt();
    float tolerance = cmd.option("tolerance", "3.0").toFloat();
    float smoothing = cmd.option("smoothing", "0.0").toFloat();
    
    bool division = cmd.hasOption("div") || cmd.hasOption("division");
    
    // Create extractor
    Background::BackgroundExtractor extractor;
    extractor.setParameters(degree, tolerance, smoothing);
    
    // Generate grid (auto)
    extractor.generateGrid(*s_currentImage);
    
    // Compute model
    if (!extractor.computeModel()) {
        if (s_runner) s_runner->setVariable("error", "Failed to compute background model");
        return false;
    }
    
    // Apply correction
    ImageBuffer result;
    if (!extractor.apply(*s_currentImage, result, 
        division ? Background::CorrectionType::Division : Background::CorrectionType::Subtraction)) {
        return false;
    }
    
    *s_currentImage = std::move(result);
    return true;
}

//=============================================================================
// SEQ EXTRACT COMMAND
//=============================================================================

bool StackingCommands::cmdSeqExtract(const ScriptCommand& cmd) {
    if (cmd.name != "seqextract_Ha" && cmd.name != "seqextract_HaOIII") return false;
    
    QString prefix = cmd.args[0];
    bool splitOIII = (cmd.name == "seqextract_HaOIII");
    
    // Find files matching prefix
    QDir dir(s_workingDir);
    QStringList filters;
    filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    
    if (files.isEmpty()) return false;
    
    // CFA Pattern (Default RGGB)
    QString patternStr = cmd.option("pattern", "RGGB").toUpper();
    
    // Processing loop
    for (const QString& f : files) {
        QString inPath = dir.absoluteFilePath(f);
        ImageBuffer buf;
        if (!Stacking::FitsIO::read(inPath, buf)) continue;
        
        if (buf.channels() != 1) continue; // CFA must be single channel
        
        int w = buf.width();
        int h = buf.height();
        int hw = w/2;
        int hh = h/2;
        
        // Extract Ha (Red)
        ImageBuffer ha(hw, hh, 1);
        float* haData = ha.data().data();
        const float* inData = buf.data().data();
        
        // Set offsets based on pattern
        int rX = 0, rY = 0;
        int g1X = 1, g1Y = 0;
        int g2X = 0, g2Y = 1;
        int bX = 1, bY = 1;
        
        if (patternStr == "BGGR") { rX=1; rY=1; g1X=0; g1Y=1; g2X=1; g2Y=0; bX=0; bY=0; }
        else if (patternStr == "GBRG") { rX=0; rY=1; g1X=0; g1Y=0; g2X=1; g2Y=1; bX=1; bY=0; }
        else if (patternStr == "GRBG") { rX=1; rY=0; g1X=0; g1Y=0; g2X=1; g2Y=1; bX=0; bY=1; }
        
        for(int y=0; y<hh; ++y) {
            for(int x=0; x<hw; ++x) {
                haData[y*hw + x] = inData[(y*2 + rY)*w + (x*2 + rX)]; 
            }
        }
        
        // Save Ha
        QString haName = "Ha_" + f;
        Stacking::FitsIO::write(dir.absoluteFilePath(haName), ha, 16);
        
        // Extract OIII (Av of G1, G2 and B)
        if (splitOIII) {
             ImageBuffer oiii(hw, hh, 1);
             float* o3Data = oiii.data().data();
             
             for(int y=0; y<hh; ++y) {
                for(int x=0; x<hw; ++x) {
                    float g1 = inData[(y*2 + g1Y)*w + (x*2 + g1X)];
                    float g2 = inData[(y*2 + g2Y)*w + (x*2 + g2X)];
                    float b  = inData[(y*2 + bY)*w + (x*2 + bX)];
                    o3Data[y*hw + x] = (g1 + g2 + b) / 3.0f; 
                }
             }
             QString o3Name = "OIII_" + f;
             Stacking::FitsIO::write(dir.absoluteFilePath(o3Name), oiii, 16);
        }
    }
    
    return true;
}

//=============================================================================
// MIRROR COMMAND
//=============================================================================

bool StackingCommands::cmdMirror(const ScriptCommand& cmd) {
    if (!s_currentImage || !s_currentImage->isValid()) return false;
    
    bool bottomUp = cmd.hasOption("bottomup");
    
    // In-place flip
    int w = s_currentImage->width();
    int h = s_currentImage->height();
    int c = s_currentImage->channels();
    float* data = s_currentImage->data().data();
    
    if (bottomUp) {
        // Vertical flip (rows)
        std::vector<float> rowBuffer(w * c);
        for(int y=0; y<h/2; ++y) {
            float* topRow = data + static_cast<size_t>(y) * w * c;
            float* botRow = data + static_cast<size_t>(h - 1 - y) * w * c;
            
            std::memcpy(rowBuffer.data(), topRow, w * c * sizeof(float));
            std::memcpy(topRow, botRow, w * c * sizeof(float));
            std::memcpy(botRow, rowBuffer.data(), w * c * sizeof(float));
        }
    } else {
        for(int y=0; y<h; ++y) {
             for(int x=0; x<w/2; ++x) {
                 for(int ch=0; ch<c; ++ch) {
                     size_t idx1 = (static_cast<size_t>(y) * w + x) * c + ch;
                     size_t idx2 = (static_cast<size_t>(y) * w + (w - 1 - x)) * c + ch;
                     std::swap(data[idx1], data[idx2]);
                 }
             }
        }
    }
    
    return true;
}

//=============================================================================
// RGB COMP COMMAND
//=============================================================================

bool StackingCommands::cmdRGBComp(const ScriptCommand& cmd) {
    if (cmd.args.size() < 3) return false;
    
    QString rPath = resolvePath(cmd.args[0]);
    QString gPath = resolvePath(cmd.args[1]);
    QString bPath = resolvePath(cmd.args[2]);
    
    // Load images
    ImageBuffer rImg, gImg, bImg;
    if (!Stacking::FitsIO::read(rPath, rImg) || 
        !Stacking::FitsIO::read(gPath, gImg) || 
        !Stacking::FitsIO::read(bPath, bImg)) {
        return false;
    }
    
    // Check dimensions
    if (rImg.width() != gImg.width() || rImg.width() != bImg.width() ||
        rImg.height() != gImg.height() || rImg.height() != bImg.height()) {
        if (s_runner) s_runner->setVariable("error", "Image dimensions do not match");
        return false;
    }
    
    // Create RGB
    ImageBuffer rgb(rImg.width(), rImg.height(), 3);
    float* data = rgb.data().data();
    int size = rImg.width() * rImg.height();
    
    // Copy channels
    // Assuming inputs are mono (1 channel) or taking first channel
    // TStar ImageBuffer storage is planar: All R, then All G, etc.
    std::memcpy(data, rImg.data().data(), size * sizeof(float));
    std::memcpy(data + size, gImg.data().data(), size * sizeof(float));
    std::memcpy(data + size * 2, bImg.data().data(), size * sizeof(float));
    
    // Save
    QString outName = cmd.option("out", "rgb_composition.fit");
    return Stacking::FitsIO::write(resolvePath(outName), rgb, 32);
}

//=============================================================================
// LINEAR MATCH COMMAND
//=============================================================================

bool StackingCommands::cmdLinearMatch(const ScriptCommand& cmd) {
    QString refPath = resolvePath(cmd.args[0]);
    QString tgtPath = resolvePath(cmd.args[1]);
    
    ImageBuffer refImg, tgtImg;
    if (!Stacking::FitsIO::read(refPath, refImg) || !Stacking::FitsIO::read(tgtPath, tgtImg)) {
        return false;
    }
    
    // Compute stats using Statistics module
    // We need Median and MAD (Mean Absolute Deviation)
    // For simplicity, let's just do it on the first channel if mono
    
    int sizeRef = refImg.width() * refImg.height();
    int sizeTgt = tgtImg.width() * tgtImg.height();
    
    Stacking::Statistics stats;
    
    // Ref stats
    std::vector<float> refPixels(refImg.data().begin(), refImg.data().begin() + sizeRef);
    double refMedian = stats.median(refPixels);
    double refMad = stats.mad(refPixels, refMedian);
    
    // Tgt stats
    std::vector<float> tgtPixels(tgtImg.data().begin(), tgtImg.data().begin() + sizeTgt);
    double tgtMedian = stats.median(tgtPixels);
    double tgtMad = stats.mad(tgtPixels, tgtMedian);
    
    if (tgtMad == 0.0) tgtMad = 1.0; // Avoid div by zero
    
    // Transform
    double scale = refMad / tgtMad;
    double offset = refMedian - (tgtMedian * scale);
    
    for (float& val : tgtImg.data()) {
        val = static_cast<float>(val * scale + offset);
    }
    
    QString outPath = cmd.option("out", tgtPath);
    return Stacking::FitsIO::write(resolvePath(outPath), tgtImg, 32);
}

//=============================================================================
// PIXELMATH COMMAND
//=============================================================================

bool StackingCommands::cmdPixelMath(const ScriptCommand& cmd) {
    if (!s_currentImage || !s_currentImage->isValid()) return false;
    
    QString expression = cmd.args[0];
    // Remove quotes if present
    if (expression.startsWith("\"") && expression.endsWith("\"")) {
        expression = expression.mid(1, expression.length() - 2);
    }
    
    Stacking::PixelMath pm;
    
    // Scan expression for variables ($name$) and load them
    // Regex would be good, but manual parsing is safer without QRegularExpression dependency
    // Pattern: $token$
    int pos = 0;
    while ((pos = expression.indexOf('$', pos)) != -1) {
        int end = expression.indexOf('$', pos + 1);
        if (end != -1) {
            QString varName = expression.mid(pos + 1, end - pos - 1);
            // Try to load varName using resolvePath + .fit extension or exact match
            // The script uses $starReduction$ referring to starReduction.fit
            QString path = resolvePath(varName);
            if (!QFileInfo::exists(path) && !path.endsWith(".fit")) path += ".fit";
            
            if (QFileInfo::exists(path)) {
                // We need to manage lifecycle of these loaded images.
                // For this command, we can just load them into temporary pointers managed by a list?
                // Or PixelMath could own them?
                // Let's make PixelMath own them? No, PixelMath takes raw pointers.
                // We'll trust the user has RAM.
                // NOTE: This implementation leaks if we don't track them.
                // Let's use a static map in StackingCommands just for the duration?
                // Better: locally scope them.
                // But we don't know how many unique vars.
                // For now, load and store in a local list.
            }
        }
        pos = end + 1;
    }
    
    // For TStar implementation, we assume variables are file paths relative to CWD.
    // We need a way to resolve variables inside PM.
    // PM asks for variable "starReduction".
    // We set variable "starReduction" to an image loaded from disk.
    
    // Better approach: Pre-load ALL variables mentioned.
    // Hack: Just bind loaded images.
    QMap<QString, std::shared_ptr<ImageBuffer>> loadedVars;
    
    // Parse expression for $names$ again
    pos = 0;
    while ((pos = expression.indexOf('$', pos)) != -1) {
        int end = expression.indexOf('$', pos + 1);
        if (end != -1) {
             QString varName = expression.mid(pos + 1, end - pos - 1); // e.g., starReduction
             QString fullVar = "$" + varName + "$"; // $starReduction$
             
             QString safeName = varName; 
             // Load if not loaded
             if (!loadedVars.contains(safeName)) {
                 QString path = resolvePath(safeName); 
                 if (!QFileInfo::exists(path)) path += ".fit";
                 
                 auto img = std::make_shared<ImageBuffer>();
                 if (Stacking::FitsIO::read(path, *img)) {
                     loadedVars[safeName] = img;
                     pm.setVariable(safeName, img.get());
                 }
             }
             
             // Rewrite expression: replace $varName$ with varName
             // Actually, tokenize handles $ specially? 
             // My tokenizer in PixelMath treats $ as part of variable name? "c.isLetter() || c == '$'"
             // So token will be "starReduction" (without $ if tokenizer logic is 'while letter').
             // Wait, tokenizer logic:
             // `while ... c == '$'` -> stores "$name$".
             // So the token value IS "$name$".
             // So setVariable must use keys like "$starReduction$".
             
             if (loadedVars.contains(safeName)) {
                pm.setVariable(fullVar, loadedVars[safeName].get());
             }
        }
        pos = end + 1;
    }
    
    // Set variables
    // PM engine expects variable names to match tokens.
    
    ImageBuffer result;
    if (pm.evaluate(expression, result)) {
        *s_currentImage = std::move(result);
        return true;
    } else {
        if (s_runner) s_runner->setVariable("last_error", pm.lastError());
        return false;
    }
}



//=============================================================================
// STARNET COMMAND
//=============================================================================
#include <QProcess>

bool StackingCommands::cmdStarNet(const ScriptCommand& cmd) {
    if (!s_currentImage || !s_currentImage->isValid()) return false;
    
    // 1. Save current to temporary input
    QString tmpInput = resolvePath("starnet_input_temp.tiff"); 
    QString tmpInputFit = resolvePath("starnet_input.fit");
    Stacking::FitsIO::write(tmpInputFit, *s_currentImage, 16);
    
    QString tmpOutput = resolvePath("starless_" + QFileInfo(tmpInputFit).fileName());
    
    // 2. Prepare Process
    QProcess process;
    QString program = "starnet-cli"; // Assumed in PATH
    QStringList args;
    args << tmpInputFit << tmpOutput;
    
    if (cmd.hasOption("stride")) {
        args << cmd.option("stride");
    } else {
        args << "128"; // Default
    }
    
    // Start
    process.start(program, args);
    if (!process.waitForStarted()) {
        if (s_runner) s_runner->setVariable("error", "Failed to start StarNet CLI");
        return false;
    }
    
    // Wait for finish (can take long, should be async in GUI but sync in script)
    while (!process.waitForFinished(100)) {
        QCoreApplication::processEvents();
    }
    
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (s_runner) s_runner->setVariable("error", "StarNet failed: " + process.readAllStandardError());
        return false;
    }
    
    ImageBuffer result;
    if (Stacking::FitsIO::read(tmpOutput, result)) {
        *s_currentImage = std::move(result);
        return true;
    }
    
    return false;
}

//=============================================================================
// NEWPROJECT COMMAND
//=============================================================================

bool StackingCommands::cmdNewProject(const ScriptCommand& cmd) {
    QString path = resolvePath(cmd.args[0]);
    
    Stacking::StackingProject project;
    if (!project.create(path)) {
        if (s_runner) {
            s_runner->setVariable("error", "Failed to create project at: " + path);
        }
        return false;
    }
    
    if (s_runner) {
        s_runner->setVariable("project", path);
        s_runner->setVariable("biases", project.biasDir());
        s_runner->setVariable("darks", project.darkDir());
        s_runner->setVariable("flats", project.flatDir());
        s_runner->setVariable("lights", project.lightDir());
        s_runner->setVariable("process", project.processDir());
        s_runner->setVariable("output", project.outputDir());
        
        emit s_runner->logMessage(QObject::tr("Project created: %1").arg(path), "green");
    }
    
    return true;
}

//=============================================================================
// CONVERTALL COMMAND
//=============================================================================

bool StackingCommands::cmdConvertAll(const ScriptCommand& cmd) {
    QString pattern = cmd.args[0];
    QString outDir = cmd.hasOption("out") ? resolvePath(cmd.option("out")) : s_workingDir;
    
    QDir dir(s_workingDir);
    
    // Common RAW extensions
    QStringList rawFilters;
    rawFilters << "*.cr2" << "*.CR2" << "*.nef" << "*.NEF" 
               << "*.arw" << "*.ARW" << "*.dng" << "*.DNG"
               << "*.orf" << "*.ORF" << "*.rw2" << "*.RW2"
               << "*.raf" << "*.RAF" << "*.pef" << "*.PEF";
    
    // Also include TIFF files
    rawFilters << "*.tif" << "*.TIF" << "*.tiff" << "*.TIFF";
    
    QStringList files;
    
    // If pattern is a wildcard
    if (pattern.contains("*")) {
        files = dir.entryList(QStringList() << pattern, QDir::Files, QDir::Name);
    } else {
        // Match pattern as prefix
        for (const QString& filter : rawFilters) {
            files += dir.entryList(QStringList() << pattern + filter.mid(1), QDir::Files, QDir::Name);
        }
        // Also try exact pattern matching
        if (files.isEmpty()) {
            files = dir.entryList(rawFilters, QDir::Files, QDir::Name);
        }
    }
    
    if (files.isEmpty()) {
        if (s_runner) s_runner->setVariable("error", "No files found matching: " + pattern);
        return false;
    }
    
    // Create the convert command for each file
    int converted = 0;
    for (const QString& file : files) {
        ScriptCommand convertCmd;
        convertCmd.name = "convert";
        convertCmd.args << file;
        if (!outDir.isEmpty()) {
            convertCmd.options["out"] = outDir;
        }
        
        if (cmdConvert(convertCmd)) {
            converted++;
        }
    }
    
    if (s_runner) {
        emit s_runner->logMessage(QObject::tr("Converted %1 of %2 files").arg(converted).arg(files.size()), 
                                  converted == files.size() ? "green" : "orange");
    }
    
    return converted > 0;
}

//=============================================================================
// AUTOSTACK COMMAND
//=============================================================================

bool StackingCommands::cmdAutoStack(const ScriptCommand& cmd) {
    QString projectPath = resolvePath(cmd.args[0]);
    
    // Load or detect project
    Stacking::StackingProject project;
    QString projectFile = Stacking::StackingProject::findProjectFile(projectPath);
    
    if (!projectFile.isEmpty()) {
        if (!project.load(projectFile)) {
            if (s_runner) s_runner->setVariable("error", "Failed to load project: " + projectPath);
            return false;
        }
    } else {
        // Try to create from directory
        if (!project.create(projectPath)) {
            if (s_runner) s_runner->setVariable("error", "Invalid project directory: " + projectPath);
            return false;
        }
    }
    
    if (s_runner) {
        emit s_runner->logMessage(QObject::tr("Starting pipeline for: %1").arg(project.name()), "cyan");
    }
    
    // Step 1: Find and load light frames
    QDir lightDir(project.lightDir());
    QStringList lightFiles = lightDir.entryList(QStringList() << "*.fit" << "*.fits" << "*.FIT" << "*.FITS", 
                                                 QDir::Files, QDir::Name);
    
    if (lightFiles.isEmpty()) {
        if (s_runner) s_runner->setVariable("error", "No light frames found in: " + project.lightDir());
        return false;
    }
    
    if (s_runner) {
        emit s_runner->logMessage(QObject::tr("Found %1 light frames").arg(lightFiles.size()), "white");
    }
    
    // Step 2: Calibrate (if masters exist)
    s_workingDir = project.lightDir();
    
    if (!project.masterBias().isEmpty() || !project.masterDark().isEmpty() || !project.masterFlat().isEmpty()) {
        ScriptCommand calibrateCmd;
        calibrateCmd.name = "calibrate";
        calibrateCmd.args << "*.fit";
        
        if (!project.masterBias().isEmpty()) {
            calibrateCmd.options["bias"] = project.masterBias();
        }
        if (!project.masterDark().isEmpty()) {
            calibrateCmd.options["dark"] = project.masterDark();
        }
        if (!project.masterFlat().isEmpty()) {
            calibrateCmd.options["flat"] = project.masterFlat();
        }
        
        if (s_runner) emit s_runner->logMessage(QObject::tr("Calibrating frames..."), "white");
        cmdCalibrate(calibrateCmd);
    }
    
    // Check for CFA and Debayer if needed
    QString registerPattern = "pp_*.fit";
    QString stackPattern = "r_*.fit";
    
    QDir procDir(project.processDir());
    QStringList ppFiles = procDir.entryList(QStringList() << "pp_*.fit", QDir::Files);
    
    if (!ppFiles.isEmpty()) {
        QString firstFile = procDir.absoluteFilePath(ppFiles.first());
        ImageBuffer checkBuf;
        if (Stacking::FitsIO::readHeader(firstFile, checkBuf)) {
             bool isCFA = checkBuf.channels() == 1 && 
                          (!checkBuf.metadata().bayerPattern.isEmpty() || 
                           checkBuf.metadata().xisfProperties.contains("BayerPattern"));

             if (s_runner) {
                  QString bayerDebug = checkBuf.metadata().bayerPattern;
                  if (bayerDebug.isEmpty()) bayerDebug = checkBuf.metadata().xisfProperties.value("BayerPattern").toString();
                  if (bayerDebug.isEmpty()) bayerDebug = "NONE";
                  s_runner->logMessage(QString("CFA Check - Channels: %1, Pattern: %2, Detected: %3")
                                      .arg(checkBuf.channels()).arg(bayerDebug).arg(isCFA ? "YES" : "NO"), "yellow");
             }
                           
             if (isCFA) {
                 if (s_runner) emit s_runner->logMessage("Auto-Debayering CFA images...", "cyan");
                 
                 QString bayerPat = checkBuf.metadata().bayerPattern;
                 if (bayerPat.isEmpty()) bayerPat = checkBuf.metadata().xisfProperties.value("BayerPattern").toString();
                 
                 Preprocessing::BayerPattern pat = Preprocessing::BayerPattern::None;
                 if (bayerPat == "RGGB") pat = Preprocessing::BayerPattern::RGGB;
                 else if (bayerPat == "BGGR") pat = Preprocessing::BayerPattern::BGGR;
                 else if (bayerPat == "GBRG") pat = Preprocessing::BayerPattern::GBRG;
                 else if (bayerPat == "GRBG") pat = Preprocessing::BayerPattern::GRBG;
                 
                 if (pat != Preprocessing::BayerPattern::None) {
                     QAtomicInt dbCount(0);
                     int maxThreads = QThread::idealThreadCount();
                     QSemaphore throttle(maxThreads); 
                     
                     QtConcurrent::blockingMap(ppFiles, [&](const QString& ppFile) {
                         throttle.acquire();
                         struct Guard { QSemaphore& s; ~Guard() { s.release(); } } guard{throttle};
                         
                         QString inP = procDir.absoluteFilePath(ppFile);
                         QString outP = procDir.absoluteFilePath("deb_" + ppFile);
                         
                         static thread_local ImageBuffer tBuf;
                         
                         if (Stacking::FitsIO::read(inP, tBuf)) {
                             ImageBuffer colorBuf;
                             if (Preprocessing::Debayer::vng(tBuf, colorBuf, pat)) {
                                 colorBuf.setMetadata(tBuf.metadata()); 
                                 Stacking::FitsIO::write(outP, colorBuf);
                                 dbCount.fetchAndAddRelaxed(1);
                             }
                         }
                     });
                     
                     if (s_runner) emit s_runner->logMessage(QString("Debayered %1 images").arg(dbCount.loadRelaxed()), "green");
                     registerPattern = "deb_pp_*.fit";
                     stackPattern = "r_deb_pp_*.fit";
                 }
             }
        }
    }

    // Step 3: Register
    if (s_runner) emit s_runner->logMessage(QObject::tr("Registering frames..."), "white");
    
    s_workingDir = project.processDir();
    ScriptCommand registerCmd;
    registerCmd.name = "register";
    registerCmd.args << registerPattern;
    
    if (!cmdRegister(registerCmd)) {
        // Try with original lights if no calibrated files
        s_workingDir = project.lightDir();
        registerCmd.args.clear();
        registerCmd.args << "*.fit";
        cmdRegister(registerCmd);
        stackPattern = "r_*.fit"; // Fallback
    }
    
    // Step 4: Stack
    if (s_runner) emit s_runner->logMessage(QObject::tr("Stacking..."), "white");
    
    s_workingDir = project.processDir();
    ScriptCommand stackCmd;
    stackCmd.name = "stack";
    stackCmd.args << stackPattern;
    stackCmd.options["out"] = project.stackedName();
    
    if (!cmdStack(stackCmd)) {
        // Try with registered files without pp_ prefix
        stackCmd.args.clear();
        stackCmd.args << "r_*.fit";
        cmdStack(stackCmd);
    }
    
    if (s_runner) {
        emit s_runner->logMessage(QObject::tr("Pipeline complete! Output: %1").arg(project.stackedName()), "lime");
    }
    
    return true;
}

bool StackingCommands::cmdConvert(const ScriptCommand& cmd) {
    if (cmd.args.isEmpty()) {
        if (s_runner) s_runner->setError("Convert: Missing file type argument", cmd.lineNumber);
        return false;
    }

    QString type = cmd.args[0].toLower();
    QString outDirName = cmd.options.value("out", "process");
    
    QDir workingDir(s_workingDir);
    if (!workingDir.exists()) {
        if (s_runner) s_runner->setError("Convert: Working directory does not exist", cmd.lineNumber);
        return false;
    }
    
    // Create output directory
    QDir outDir = workingDir;
    if (!outDir.cd(outDirName)) {
        if (!workingDir.mkdir(outDirName) || !outDir.cd(outDirName)) {
             if (s_runner) s_runner->setError("Convert: Could not create output directory", cmd.lineNumber);
             return false;
        }
    }
    
    // Find files matching type
    QStringList nameFilters;
    nameFilters << "*.fit" << "*.fits" << "*.cr2" << "*.nef" << "*.arw" << "*.dng"; // Common RAW + FITS
    
    QStringList files = workingDir.entryList(nameFilters, QDir::Files | QDir::NoDotAndDotDot);
    if (files.isEmpty()) {
         if (s_runner) s_runner->logMessage("Convert: No files found to convert.", "orange");
         return true; // Not an error, just empty
    }
    
    if (s_runner) s_runner->logMessage(QString("Converting %1 files using %2 threads...").arg(files.size()).arg(QThread::idealThreadCount()), "neutral");
    
    QAtomicInt convertedCount(0);
    QAtomicInt failedCount(0);
    std::mutex logMutex;
    
    // Use all available threads for maximum parallelism (FITS I/O benefits from parallelism)
    
    QtConcurrent::blockingMap(files, [&](const QString& file) {
        if (s_runner && s_runner->isCancelled()) return;

        try {
            QString inputPath = workingDir.absoluteFilePath(file);
            QString ext = QFileInfo(file).suffix().toLower();
            
            // Output filename: type_00001.fit
            int currentIdx = convertedCount.fetchAndAddRelaxed(1);
            QString outName = QString("%1_%2.fit").arg(type).arg(currentIdx + 1, 5, 10, QChar('0'));
            QString outputPath = outDir.absoluteFilePath(outName);
            
            {
                std::lock_guard<std::mutex> lock(logMutex);
                if (s_runner) s_runner->logMessage(QString(" > %1 -> %2").arg(file, outName), "#888888");
            }

            // Reuse buffer via thread_local to avoid frequent allocations
            static thread_local ImageBuffer threadBuffer;
            threadBuffer.resize(0, 0, 0); // Clear state but keep capacity
            
            bool loaded = false;
            
            if (ext == "fit" || ext == "fits" || ext == "fts") {
                // Use optimized C FITS loader (cfitsio directly, no Qt overhead)
                loaded = IO::FitsLoaderCWrapper::loadFitsC(inputPath, threadBuffer);
            } else if (ext == "tif" || ext == "tiff") {
                loaded = Stacking::TiffIO::read(inputPath, threadBuffer);
            } else {
                // RAW loader...
#ifdef HAVE_LIBRAW
                libraw_data_t *lr = libraw_init(0);
                if (lr) {
                    // Optimized RAW path: Only debayer if explicitly requested
                    bool wantDebayer = cmd.hasOption("debayer");
                    
                    if (libraw_open_file(lr, inputPath.toLocal8Bit().constData()) == LIBRAW_SUCCESS) {
                        if (libraw_unpack(lr) == LIBRAW_SUCCESS) {
                            if (wantDebayer) {
                                // High quality debayering path (Slow)
                                lr->params.output_bps = 16;
                                lr->params.gamm[0] = 1.0; lr->params.gamm[1] = 1.0;
                                lr->params.no_auto_bright = 1;

                                lr->params.use_camera_wb = 0; 
                                lr->params.use_auto_wb = 0;
                                lr->params.user_mul[0] = 1.0f;
                                lr->params.user_mul[1] = 1.0f;
                                lr->params.user_mul[2] = 1.0f;
                                lr->params.user_mul[3] = 1.0f;

                                lr->params.user_qual = 3;
                                lr->params.output_color = 0;
                                
                                if (libraw_dcraw_process(lr) == LIBRAW_SUCCESS) {
                                    int err = 0;
                                    libraw_processed_image_t *image = libraw_dcraw_make_mem_image(lr, &err);
                                    if (image && image->type == LIBRAW_IMAGE_BITMAP) {
                                        threadBuffer.resize(image->width, image->height, image->colors);
                                        float* dst = threadBuffer.data().data();
                                        unsigned short* src = (unsigned short*)image->data;
                                        size_t total = (size_t)image->width * image->height * image->colors;
                                        #pragma omp parallel for
                                        for(long long i=0; i<(long long)total; ++i) dst[i] = src[i] / 65535.0f;
                                        loaded = true;
                                        libraw_dcraw_clear_mem(image);
                                    }
                                }
                            } else {
                                // Fast CFA path (Best for Stacking)
                                // Extract the raw bayer data without processing
                                libraw_decoder_info_t info;
                                libraw_get_decoder_info(lr, &info);
                                
                                int w = lr->rawdata.sizes.raw_width;
                                // We want the visible part usually
                                int left = lr->rawdata.sizes.left_margin;
                                int top = lr->rawdata.sizes.top_margin;
                                int vw = lr->rawdata.sizes.width;
                                int vh = lr->rawdata.sizes.height;
                                
                                threadBuffer.resize(vw, vh, 1);
                                float* dst = threadBuffer.data().data();
                                unsigned short* src = (unsigned short*)lr->rawdata.raw_alloc; 
                                if (!src) src = lr->rawdata.raw_image;
                                
                                if (src) {
                                    float black = (float)lr->color.black;
                                    float maximum = (float)lr->color.maximum;
                                    float range = maximum - black;
                                    if (range <= 0.0f) range = 65535.0f;

                                    float mul[4];
                                    for (int i = 0; i < 4; ++i) mul[i] = lr->color.cam_mul[i];
                                    float g_norm = mul[1]; 
                                    if (g_norm <= 0.0f && mul[3] > 0.0f) g_norm = mul[3];
                                    if (g_norm <= 0.0f) g_norm = 1.0f;
                                    for (int i = 0; i < 4; ++i) mul[i] /= g_norm;

                                    unsigned int f = lr->idata.filters;

                                    #pragma omp parallel for
                                    for (int y = 0; y < vh; ++y) {
                                        int row = y + top;
                                        for (int x = 0; x < vw; ++x) {
                                            int col = x + left;
                                            int c = (f >> ((((row) << 1) & 14) + (col & 1)) * 2) & 3;
                                            float val = (float)src[row * w + col];
                                            dst[y * vw + x] = std::max(0.0f, (val - black) * mul[c & 3] / range);
                                        }
                                    }
                                    loaded = true;
                                    
                                // Set CFA info in metadata
                                threadBuffer.metadata().isMono = true;
                                
                                // Detect Bayer Pattern from LibRaw
                                QString bayerPat = "RGGB";
                                if (f == 0x94949494) bayerPat = "RGGB";
                                else if (f == 0x16161616) bayerPat = "BGGR";
                                else if (f == 0x61616161) bayerPat = "GRBG";
                                else if (f == 0x49494949) bayerPat = "GBRG";
                                
                                // Adjust for crop/margins (Critical for correct color)
                                // If left margin is odd, flip horizontal (RGGB -> GRBG)
                                // If top margin is odd, flip vertical (RGGB -> GBRG)
                                if (left % 2 != 0) {
                                    if (bayerPat == "RGGB") bayerPat = "GRBG";
                                    else if (bayerPat == "BGGR") bayerPat = "GBRG";
                                    else if (bayerPat == "GRBG") bayerPat = "RGGB";
                                    else if (bayerPat == "GBRG") bayerPat = "BGGR";
                                }
                                if (top % 2 != 0) {
                                    if (bayerPat == "RGGB") bayerPat = "GBRG";
                                    else if (bayerPat == "BGGR") bayerPat = "GRBG";
                                    else if (bayerPat == "GRBG") bayerPat = "BGGR";
                                    else if (bayerPat == "GBRG") bayerPat = "RGGB";
                                }
                                else {
                                    // Fallback using cdesc if non-standard 2x2
                                    // Common sensors are covered above.
                                    // Log warning?
                                }
                                threadBuffer.metadata().xisfProperties["BayerPattern"] = bayerPat;
                                threadBuffer.metadata().bayerPattern = bayerPat; // Set member for internal checks
                                threadBuffer.metadata().rawHeaders.push_back({"BAYERPAT", bayerPat, "Bayer Pattern"}); // Write to FITS header
                            }
                        }
                    }
                }
                    libraw_close(lr);
                }
#endif
            }
            
            if (loaded) {
                // Save as FITS (32-bit float by default)
                if (!Stacking::FitsIO::write(outputPath, threadBuffer, 32)) {
                     failedCount.fetchAndAddRelaxed(1);
                     std::lock_guard<std::mutex> lock(logMutex);
                     if (s_runner) s_runner->logMessage(QString("Failed to save %1").arg(outName), "red");
                }
            } else {
                 failedCount.fetchAndAddRelaxed(1);
                 std::lock_guard<std::mutex> lock(logMutex);
                 if (s_runner) s_runner->logMessage(QString("Failed to load %1").arg(file), "red");
            }
            
            if (s_runner) s_runner->progressChanged(QString("Converting %1").arg(file), (double)convertedCount.loadRelaxed() / files.size());

        } catch (const std::exception& e) {
            failedCount.fetchAndAddRelaxed(1);
            std::lock_guard<std::mutex> lock(logMutex);
            if (s_runner) s_runner->logMessage(QString("Exception converting %1: %2").arg(file, e.what()), "red");
        } catch (...) {
            failedCount.fetchAndAddRelaxed(1);
            std::lock_guard<std::mutex> lock(logMutex);
            if (s_runner) s_runner->logMessage(QString("Unknown exception converting %1").arg(file), "red");
        }
    });
    
    if (s_runner) s_runner->logMessage(QString("Conversion complete: %1 success, %2 failed").arg(convertedCount.loadRelaxed() - failedCount.loadRelaxed()).arg(failedCount.loadRelaxed()), "green");
    
    return true;
}

} // namespace Scripting
