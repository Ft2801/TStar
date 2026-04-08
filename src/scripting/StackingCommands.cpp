/**
 * @file StackingCommands.cpp
 * @brief Implementation of all stacking and preprocessing script commands.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "StackingCommands.h"
#include "../io/FitsWrapper.h"
#include "../io/FitsLoaderCWrapper.h"
#include "../io/FitsHeaderUtils.h"
#include "../stacking/Registration.h"
#include "../io/TiffIO.h"
#include "../background/BackgroundExtraction.h"
#include "../preprocessing/Debayer.h"
#include "../core/ResourceManager.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QFuture>
#include <QtConcurrent>
#include <QAtomicInt>
#include <QSemaphore>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QSettings>
#include <QRegularExpression>
#include <QEventLoop>
#include "../astrometry/NativePlateSolver.h"
#include "../astrometry/AstapSolver.h"

#include <mutex>

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace Scripting {

// ============================================================================
// Static member initialization
// ============================================================================

std::unique_ptr<Stacking::ImageSequence>  StackingCommands::s_sequence;
std::unique_ptr<ImageBuffer>              StackingCommands::s_currentImage;
QString                                   StackingCommands::s_currentFilename;
Stacking::FramingMode                     StackingCommands::s_framingMode = Stacking::FramingMode::Reference;
Preprocessing::PreprocessingEngine        StackingCommands::s_preprocessor;
QString                                   StackingCommands::s_workingDir;
ScriptRunner*                             StackingCommands::s_runner = nullptr;

// ============================================================================
// Command registration
// ============================================================================

void StackingCommands::initCurrentImage(const ImageBuffer& image)
{
    s_currentImage = std::make_unique<ImageBuffer>(image);
    s_currentFilename.clear();
}

void StackingCommands::registerCommands(ScriptRunner& runner)
{
    s_runner     = &runner;
    s_workingDir = runner.workingDirectory();

    QVector<CommandDef> commands = {

        // -- Directory -------------------------------------------------------
        CommandDef("cd", 1, 1,
            "cd <path>",
            "Change working directory",
            cmdCd),

        // -- File I/O --------------------------------------------------------
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

        // -- Stacking --------------------------------------------------------
        CommandDef("mosaic", 0, 8,
            "mosaic [prefix] [--out=<name>]",
            "Stitch a sequence of images into a mosaic",
            cmdMosaic),
        CommandDef("stack", 0, 8,
            "stack [prefix] [method] [rejection] [sigma_lo] [sigma_hi] [--out=<name>]",
            "Stack a sequence of images",
            cmdStack),
        CommandDef("rgbcomp", 3, 2,
            "rgbcomp <r_file> <g_file> <b_file> [-out=<filename>]",
            "Combine three monochrome images into an RGB composite",
            cmdRGBComp),
        CommandDef("linear_match", 2, 0,
            "linear_match <ref_file> <target_file>",
            "Normalize target to match reference image statistics",
            cmdLinearMatch),
        CommandDef("pm", 1, 1,
            "pm <expression>",
            "Evaluate a PixelMath expression",
            cmdPixelMath),
        CommandDef("starnet", 0, 0,
            "starnet [--nostarmask] [--stride=<val>]",
            "Run StarNet++ star removal on the current image",
            cmdStarNet),
        CommandDef("calibrate", 1, 1,
            "calibrate <prefix> [--bias=<file>] [--dark=<file>] [--flat=<file>]",
            "Calibrate images with master bias, dark, and flat frames",
            cmdCalibrate),
        CommandDef("register", 0, 1,
            "register [prefix] [--drizzle] [--norotation]",
            "Register (align) images in a sequence",
            cmdRegister),
        CommandDef("convert", 1, 3,
            "convert <prefix> [--out=<dir>] [--debayer]",
            "Convert source files to a FITS sequence (CFA by default)",
            cmdConvert),
        CommandDef("link", 1, 2,
            "link <prefix> [--out=<directory>]",
            "Create symbolic links for a sequence (alias for convert)",
            cmdConvert),

        // -- Master frames ---------------------------------------------------
        CommandDef("setmaster", 2, 2,
            "setmaster <type> <filename>",
            "Set a master calibration frame (bias, dark, flat)",
            cmdSetMaster),

        // -- Processing ------------------------------------------------------
        CommandDef("background", 0, 1,
            "background [sequence] [--degree=<n>]",
            "Perform background extraction on the current image or sequence",
            cmdBackground),
        CommandDef("seqsubsky", 1, 2,
            "seqsubsky <sequence> <degree>",
            "Perform background extraction (alias for background)",
            cmdBackground),
        CommandDef("debayer", 0, 1,
            "debayer [prefix] [--pattern=<RGGB|BGGR|GBRG|GRBG>] "
            "[--algo=<bilinear|vng|superpixel>]",
            "Debayer the current image or a sequence of images",
            cmdDebayer),

        // -- CFA Extraction --------------------------------------------------
        CommandDef("seqextract_Ha", 1, 1,
            "seqextract_Ha <prefix> [--upscale]",
            "Extract Ha channel from a CFA sequence",
            cmdSeqExtract),
        CommandDef("seqextract_HaOIII", 1, 1,
            "seqextract_HaOIII <prefix> [--upscale]",
            "Extract Ha and OIII channels from a CFA sequence",
            cmdSeqExtract),

        // -- Geometry --------------------------------------------------------
        CommandDef("mirrorx", 0, 0,
            "mirrorx [-bottomup]",
            "Flip image horizontally (or vertically with -bottomup)",
            cmdMirror),
        CommandDef("mirrory", 0, 0,
            "mirrory",
            "Flip image vertically (alias for mirrorx -bottomup)",
            cmdMirror),
        CommandDef("rotate", 1, 1,
            "rotate <angle>",
            "Rotate the current image by the specified angle in degrees",
            cmdRotate),
        CommandDef("resample", 1, 1,
            "resample <factor|width=...|height=...>",
            "Resample (resize) the current image",
            cmdResample),
        CommandDef("crop", 0, 4,
            "crop [x y width height]",
            "Crop the current image to the specified region",
            cmdCrop),

        // -- Project / Pipeline ----------------------------------------------
        CommandDef("newproject", 1, 1,
            "newproject <path>",
            "Create a new stacking project with the standard directory layout",
            cmdNewProject),
        CommandDef("convertall", 1, 1,
            "convertall <pattern> [--out=<dir>]",
            "Batch-convert RAW files to FITS in the lights directory",
            cmdConvertAll),
        CommandDef("autostack", 1, 1,
            "autostack <project_path>",
            "Run the full pipeline: calibrate, register, stack",
            cmdAutoStack),
        CommandDef("seqapplyreg", 1, 1,
            "seqapplyreg <name> [--prefix=] [--framing=min|max|ref] [-filter-round=<X>%|k]",
            "Apply stored registration transforms to a sequence",
            cmdSeqApplyReg),
        
        CommandDef("seqplatesolve", 1, 3,
            "seqplatesolve <name> [-force] [-nocache]",
            "Plate solve the reference frame of a sequence",
            cmdSeqPlateSolve),

        // -- Metadata --------------------------------------------------------
        CommandDef("update_key", 2, 3,
            "update_key <key> <value> [comment]",
            "Update a FITS header keyword in the current image",
            cmdUpdateKey),
        CommandDef("stat", 0, 0,
            "stat",
            "Log basic image statistics (min, max, mean)",
            cmdStat),

        // -- Pixel-level operations ------------------------------------------
        CommandDef("thresh", 2, 2,
            "thresh <lo> <hi>",
            "Apply low and high thresholding",
            cmdThreshold),
        CommandDef("threshlo", 1, 1,
            "threshlo <value>",
            "Clamp pixels below the threshold to zero",
            cmdThreshold),
        CommandDef("threshhi", 1, 1,
            "threshhi <value>",
            "Clamp pixels above the threshold to one",
            cmdThreshold),
        CommandDef("offset", 1, 1,
            "offset <value>",
            "Add a constant offset to all pixels",
            cmdMath),
        CommandDef("fmul", 1, 1,
            "fmul <factor>",
            "Multiply all pixels by a constant factor",
            cmdMath),
        CommandDef("neg", 0, 0,
            "neg",
            "Invert the image (compute the negative)",
            cmdMath),
    };

    runner.registerCommands(commands);
}

// ============================================================================
// Path resolution
// ============================================================================

QString StackingCommands::resolvePath(const QString& path)
{
    if (path.isEmpty()) return QString();
    QFileInfo fi(path);
    if (fi.isAbsolute()) return path;

    QString resolved = QDir(s_workingDir).absoluteFilePath(path);
    
    // Diagnostic logging for script debugging
    if (s_runner) {
        s_runner->logMessageDirect(
            QObject::tr("Resolving relative path: '%1' -> '%2'")
                .arg(path).arg(resolved), 
            "neutral"
        );
    }

    return resolved;
}

// ============================================================================
// cd -- Change working directory
// ============================================================================

bool StackingCommands::cmdCd(const ScriptCommand& cmd)
{
    QString path = resolvePath(cmd.args[0]);

    QDir dir(path);
    if (!dir.exists()) {
        QString msg = QString("Directory not found: %1").arg(path);
        if (s_runner)
            s_runner->setError(msg, cmd.lineNumber);
        return false;
    }

    s_workingDir = dir.absolutePath();
    QDir::setCurrent(s_workingDir);

    if (s_runner) {
        s_runner->setWorkingDirectory(s_workingDir);
        s_runner->logMessage(
            QObject::tr("Changed working directory to: %1").arg(s_workingDir),
            "neutral");
    }

    return true;
}

// ============================================================================
// load -- Load an image file
// ============================================================================

bool StackingCommands::cmdLoad(const ScriptCommand& cmd)
{
    QString path = resolvePath(cmd.args[0]);

    s_currentImage = std::make_unique<ImageBuffer>();
    if (!Stacking::FitsIO::read(path, *s_currentImage)) {
        s_currentImage.reset();
        return false;
    }

    // Store the basename so downstream commands (e.g. starnet) can derive
    // output filenames consistently.
    s_currentFilename = QFileInfo(path).fileName();

    // Notify the UI that a new image is available for display.
    if (s_runner) {
        QString title = QFileInfo(path).fileName();
        emit s_runner->imageLoaded(title);
    }

    return true;
}

// ============================================================================
// Helper for save command templating
// ============================================================================
static QString resolveFilenameFormat(QString path, const ImageBuffer& img) {
    if (!path.contains("$")) return path;

    QRegularExpression rx("\\$([A-Za-z0-9_\\-]+)(?::([^\\$]+))?\\$");
    QRegularExpressionMatchIterator i = rx.globalMatch(path);
    int offset = 0;
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString keyword = match.captured(1).toUpper();
        QString format = match.captured(2);
        
        QString value = "";
        const auto& meta = img.metadata();
        
        // Custom logic for EXPTIME if we want to format it nicely
        if (keyword == "STACKCNT" && meta.stackCount > 0) {
            value = QString::number(meta.stackCount);
        } else if (keyword == "EXPTIME" && meta.exposure > 0.0) {
            if (format.contains("%d") || format.contains("d")) {
                value = QString::number(qRound(meta.exposure));
            } else {
                value = QString::number(meta.exposure, 'f', 2);
            }
        } else {
            // Find keyword in rawHeaders
            bool found = false;
            for (const auto& card : meta.rawHeaders) {
                if (card.key.toUpper() == keyword) {
                    value = card.value.trimmed();
                    // Strip quotes if they exist around the value
                    if (value.startsWith("'") && value.endsWith("'")) {
                        value = value.mid(1, value.length() - 2).trimmed();
                    }
                    found = true;
                    break;
                }
            }
            
            if (found) {
                if (format == "dm12" && keyword == "DATE-OBS") {
                    value = value.left(10).replace("-", ""); // Format dm12 usually means yymmdd or yyyymmdd or just stripped date. Let's provide a safe substring.
                } else if (format.contains("d")) {
                    value = QString::number(qRound(value.toDouble()));
                }
            }
        }
        
        if (value.isEmpty()) value = "UNKNOWN";

        path.replace(match.capturedStart() + offset, match.capturedLength(), value);
        offset += value.length() - match.capturedLength();
    }
    return path;
}

// ============================================================================
// save -- Save current image
// ============================================================================

bool StackingCommands::cmdSave(const ScriptCommand& cmd)
{
    if (!s_currentImage || !s_currentImage->isValid()) {
        if (s_runner)
            s_runner->logMessage(QObject::tr("Save: no image loaded"), "red");
        return false;
    }

    // Determine output path from positional argument or option.
    QString path;
    if (cmd.args.size() > 0) {
        path = resolvePath(resolveFilenameFormat(cmd.args[0], *s_currentImage));
    } else {
        path = cmd.option("out", cmd.option("output", ""));
        if (path.isEmpty()) {
            if (s_runner)
                s_runner->logMessage(
                    QObject::tr("Save: no filename specified (use positional arg or -out=...)"),
                    "red");
            return false;
        }
        path = resolvePath(resolveFilenameFormat(path, *s_currentImage));
    }

    // Select output bit depth.
    int bits = cmd.hasOption("32b") ? 32
             : cmd.hasOption("16b") ? 16
             : 32;

    if (!Stacking::FitsIO::write(path, *s_currentImage, bits)) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("Save: failed to write %1").arg(path), "red");
        return false;
    }

    s_currentFilename = QFileInfo(path).fileName();

    // Ensure the file has a FITS extension.
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext != "fit" && ext != "fits" && ext != "fts")
        path += ".fit";

    return true;
}

// ============================================================================
// close -- Release the current image and sequence
// ============================================================================

bool StackingCommands::cmdClose(const ScriptCommand& cmd)
{
    Q_UNUSED(cmd);
    s_currentImage.reset();
    s_sequence.reset();
    return true;
}

// ============================================================================
// Stacking parameter parsers
// ============================================================================

Stacking::Method StackingCommands::parseMethod(const QString& str)
{
    QString s = str.toLower();
    if (s == "sum")    return Stacking::Method::Sum;
    if (s == "median") return Stacking::Method::Median;
    if (s == "max")    return Stacking::Method::Max;
    if (s == "min")    return Stacking::Method::Min;
    if (s == "mosaic") return Stacking::Method::Mosaic;
    return Stacking::Method::Mean;
}

Stacking::Rejection StackingCommands::parseRejection(const QString& str)
{
    QString s = str.toLower();
    if (s == "none")                           return Stacking::Rejection::None;
    if (s == "percentile" || s == "perc")      return Stacking::Rejection::Percentile;
    if (s == "sigma")                          return Stacking::Rejection::Sigma;
    if (s == "mad")                            return Stacking::Rejection::MAD;
    if (s == "sigmedian")                      return Stacking::Rejection::SigmaMedian;
    if (s == "winsor" || s == "winsorized")    return Stacking::Rejection::Winsorized;
    if (s == "linear" || s == "linfit")        return Stacking::Rejection::LinearFit;
    if (s == "gesdt")                          return Stacking::Rejection::GESDT;
    return Stacking::Rejection::Winsorized;
}

Stacking::NormalizationMethod StackingCommands::parseNormalization(
    const QString& str)
{
    QString s = str.toLower();
    if (s == "none" || s == "nonorm")  return Stacking::NormalizationMethod::None;
    if (s == "add" || s == "additive") return Stacking::NormalizationMethod::Additive;
    if (s == "mul" || s == "multiplicative")
                                       return Stacking::NormalizationMethod::Multiplicative;
    if (s == "addscale")               return Stacking::NormalizationMethod::AdditiveScaling;
    if (s == "mulscale")               return Stacking::NormalizationMethod::MultiplicativeScaling;
    return Stacking::NormalizationMethod::AdditiveScaling;
}

Stacking::WeightingType StackingCommands::parseWeighting(const QString& str)
{
    QString s = str.toLower();
    if (s == "none")                     return Stacking::WeightingType::None;
    if (s == "noise")                    return Stacking::WeightingType::Noise;
    if (s == "fwhm" || s == "wfwhm")    return Stacking::WeightingType::WeightedFWHM;
    if (s == "stars")                    return Stacking::WeightingType::StarCount;
    if (s == "quality")                  return Stacking::WeightingType::Quality;
    return Stacking::WeightingType::None;
}

// ============================================================================
// stack -- Stack a sequence of images
// ============================================================================

bool StackingCommands::cmdStack(const ScriptCommand& cmd)
{
    QString prefix;
    if (!cmd.args.isEmpty()) {
        prefix = cmd.args[0];
    } else if (s_sequence && s_sequence->isValid()) {
        // Use current sequence if no prefix provided.
    } else {
        return false;
    }

    // -- Discover matching files ---------------------------------------------
    QStringList fullPaths;
    if (!prefix.isEmpty()) {
        QDir dir(s_workingDir);
        QStringList filters;
        filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";

        QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
        if (files.isEmpty())
            return false;

        for (const QString& f : files)
            fullPaths << dir.absoluteFilePath(f);
    }

    // -- Reuse existing sequence if it matches (preserves registration) ------
    bool reuseSequence = false;
    
    // Smart Reuse: If the user asks for 'r_PREFIX', they probably mean the 
    // registered version of the sequence we already have in memory.
    QString effectivePrefix = prefix;
    if (prefix.startsWith("r_") && prefix.length() > 2) {
        effectivePrefix = prefix.mid(2);
    }

    if (s_sequence && s_sequence->count() > 0) {
        QString currentPrefix = QFileInfo(s_sequence->image(0).filePath).baseName();
        // Simple heuristic: if the base name of our memory sequence matches the requested prefix, reuse it.
        if (currentPrefix.contains(effectivePrefix) || effectivePrefix.contains(currentPrefix)) {
            reuseSequence = true;
        }
    }

    if (!reuseSequence) {
        s_sequence = std::make_unique<Stacking::ImageSequence>();
        if (!s_sequence->loadFromFiles(fullPaths)) {
            s_sequence.reset();
            return false;
        }
    }

    // -- Parse stacking parameters -------------------------------------------
    Stacking::StackingParams params;
    params.method        = parseMethod(cmd.option("method", "mean"));
    params.rejection     = parseRejection(cmd.option("rejection", "none"));
    params.normalization = parseNormalization(cmd.option("norm", "addscale"));
    params.weighting     = parseWeighting(cmd.option("weighting", "noise"));

    // Rejection sigma thresholds.
    if (cmd.hasOption("rej")) {
        QString    rejVal = cmd.option("rej");
        QStringList parts = rejVal.split(' ');
        if (parts.size() >= 2) {
            params.sigmaLow  = parts[0].toFloat();
            params.sigmaHigh = parts[1].toFloat();
        } else if (parts.size() == 1) {
            params.sigmaLow = params.sigmaHigh = parts[0].toFloat();
        }
    } else if (cmd.args.size() >= 4) {
        params.sigmaLow  = cmd.args[2].toFloat();
        params.sigmaHigh = cmd.args[3].toFloat();
    } else {
        params.sigmaLow = params.sigmaHigh = 3.0f;
    }

    // Positional overrides for method and rejection.
    if (cmd.args.size() >= 2)
        params.method    = parseMethod(cmd.args[1]);
    if (cmd.args.size() >= 3)
        params.rejection = parseRejection(cmd.args[2]);

    // Output and processing options.
    params.force32Bit           = cmd.hasOption("32b") || cmd.hasOption("32bits");
    params.outputNormalization  = cmd.hasOption("output_norm");
    params.equalizeRGB          = cmd.hasOption("rgb_equal");

    if (s_runner) {
        if (params.equalizeRGB)
            s_runner->logMessage(
                QObject::tr("RGB Equalization: ENABLED (Neutralizes color cast)"), "orange");
        else
            s_runner->logMessage(
                QObject::tr("RGB Equalization: DISABLED (Preserves camera color cast)"), "green");
    }

    // Drizzle options.
    params.drizzle     = cmd.hasOption("drizzle");
    params.drizzleFast = cmd.hasOption("fast_drizzle") || cmd.hasOption("fast");
    if (cmd.hasOption("scale"))
        params.drizzleScale = cmd.option("scale").toDouble();
    if (cmd.hasOption("pixfrac"))
        params.drizzlePixFrac = cmd.option("pixfrac").toDouble();

    // Bayer Pattern override (CFA Drizzle)
    if (cmd.hasOption("cfa") || cmd.hasOption("bayer")) {
        QString pattern = cmd.option("cfa", cmd.option("bayer")).toUpper();
        if      (pattern == "RGGB") params.bayerPattern = Preprocessing::BayerPattern::RGGB;
        else if (pattern == "BGGR") params.bayerPattern = Preprocessing::BayerPattern::BGGR;
        else if (pattern == "GBRG") params.bayerPattern = Preprocessing::BayerPattern::GBRG;
        else if (pattern == "GRBG") params.bayerPattern = Preprocessing::BayerPattern::GRBG;
        else params.bayerPattern = Preprocessing::BayerPattern::Auto;
    }

    if (params.drizzle && s_runner) {
        // Validate registration
        bool hasReg = false;
        if (s_sequence && s_sequence->count() > 0) {
            hasReg = s_sequence->image(0).registration.hasRegistration;
        }

        if (!hasReg) {
            s_runner->logMessage(
                QObject::tr("WARNING: Drizzle requested but no registration data found. Registration must be performed before stacking for Drizzle to be effective."), "salmon");
        }

        QString modeStr = params.drizzleFast ? QObject::tr("Fast (Nearest-Neighbor)") : QObject::tr("Slow (Polygon-Clipping)");
        s_runner->logMessage(
            QString(QObject::tr("Drizzle ENABLED: Scale=%1x, PixFrac=%2, Mode=%3"))
                .arg(params.drizzleScale, 0, 'f', 1)
                .arg(params.drizzlePixFrac, 0, 'f', 2)
                .arg(modeStr), "cyan");

        if (params.drizzleFast && std::abs(params.drizzlePixFrac - 1.0) > 0.01) {
            s_runner->logMessage(
                QObject::tr("NOTE: PixFrac is ignored in Fast Drizzle mode."), "orange");
        }
    }

    // Feathering.
    if (cmd.hasOption("feather"))
        params.featherDistance = cmd.option("feather").toInt();

    // Output filename (ensure .fit extension).
    QString output = cmd.option("out",
                       cmd.option("output", prefix + "_stacked.fit"));
    params.outputFilename = resolvePath(output);
    {
        QString ext = QFileInfo(params.outputFilename).suffix().toLower();
        if (ext != "fit" && ext != "fits" && ext != "fts")
            params.outputFilename += ".fit";
    }

    if (cmd.hasOption("maximize")) {
        params.framingMode = Stacking::FramingMode::Union;
    } else {
        params.framingMode = s_framingMode;
    }

    params.upscaleAtStacking = cmd.hasOption("upscale");
    params.createRejectionMaps = cmd.hasOption("rejmap") || cmd.hasOption("rejmaps");
    params.overlapNormalization = cmd.hasOption("overlap_norm");
    params.fastNormalization = cmd.hasOption("fastnorm");

    if (!s_sequence || s_sequence->count() == 0)
        return false;

    // -- Execute the stacking engine -----------------------------------------
    Stacking::StackingArgs args;
    args.params   = params;
    args.sequence = s_sequence.get();

    if (s_runner) {
        args.cancelCheck = []() {
            return s_runner && s_runner->isCancelled();
        };
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
        if (result == Stacking::StackResult::CancelledError && s_runner)
            s_runner->logMessage(
                QObject::tr("Stacking cancelled"), "salmon");
        return false;
    }

    // -- Transfer FITS headers from the reference frame to the output --------
    if (args.result.isValid() && s_sequence && s_sequence->count() > 0) {
        int refIdx = args.params.refImageIndex;
        if (refIdx < 0 || refIdx >= s_sequence->count())
            refIdx = 0;

        const auto& refMeta = s_sequence->image(refIdx).metadata;
        auto&       outMeta = args.result.metadata();

        static const QStringList skipKeys = {
            "SIMPLE", "BITPIX", "NAXIS", "NAXIS1", "NAXIS2", "NAXIS3",
            "EXTEND", "BZERO",  "BSCALE", "END",   "HISTORY"
        };

        for (const auto& card : refMeta.rawHeaders) {
            if (!skipKeys.contains(card.key.toUpper()))
                outMeta.rawHeaders.push_back(card);
        }

        args.result.syncWcsToHeaders();
    }

    // -- Write the stacked result (always 32-bit float) ----------------------
    if (!Stacking::FitsIO::write(params.outputFilename, args.result, 32))
        return false;

    s_currentImage = std::make_unique<ImageBuffer>(std::move(args.result));
    return true;
}

// ============================================================================
// calibrate -- Calibrate images with master frames
// ============================================================================

bool StackingCommands::cmdCalibrate(const ScriptCommand& cmd)
{
    QString prefix = cmd.args[0];

    // Discover matching files.
    QDir dir(s_workingDir);
    QStringList filters;
    filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";

    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return false;

    QStringList fullPaths;
    for (const QString& f : files)
        fullPaths << dir.absoluteFilePath(f);

    // -- Configure preprocessing parameters ----------------------------------
    Preprocessing::PreprocessParams params = s_preprocessor.params();

    if (cmd.hasOption("bias")) {
        params.masterBias = resolvePath(cmd.option("bias"));
        params.useBias    = true;
    }
    if (cmd.hasOption("dark")) {
        params.masterDark = resolvePath(cmd.option("dark"));
        params.useDark    = true;
    }
    if (cmd.hasOption("flat")) {
        params.masterFlat = resolvePath(cmd.option("flat"));
        params.useFlat    = true;
    }

    // Dark optimization.
    params.darkOptim.enabled = cmd.hasOption("dark_optimize");

    // Optional debayering during calibration.
    if (cmd.hasOption("debayer")) {
        params.debayer = true;
        QString pattern = cmd.option("cfa", "AUTO").toUpper();
        if (pattern.isEmpty()) pattern = "AUTO";

        if      (pattern == "AUTO") params.bayerPattern = Preprocessing::BayerPattern::Auto;
        else if (pattern == "RGGB") params.bayerPattern = Preprocessing::BayerPattern::RGGB;
        else if (pattern == "BGGR") params.bayerPattern = Preprocessing::BayerPattern::BGGR;
        else if (pattern == "GBRG") params.bayerPattern = Preprocessing::BayerPattern::GBRG;
        else if (pattern == "GRBG") params.bayerPattern = Preprocessing::BayerPattern::GRBG;
    }

    // CFA channel equalization.
    params.cfaEqualize.enabled = cmd.hasOption("equalize_cfa");

    // Sigma-based cosmetic correction.
    if (cmd.hasOption("cc")) {
        params.cosmetic.type      = Preprocessing::CosmeticType::Sigma;
        params.cosmetic.coldSigma = 3.0f;
        params.cosmetic.hotSigma  = 3.0f;
    }

    params.outputPrefix = "pp_";
    params.outputDir    = s_workingDir;

    s_preprocessor.setParams(params);

    // Progress callback.
    auto progressCallback = [](const QString& msg, double) {
        if (s_runner)
            s_runner->logMessageDirect(msg, "neutral");
    };

    int processed = s_preprocessor.preprocessBatch(
        fullPaths, s_workingDir, progressCallback);

    if (processed == fullPaths.size()) {
        // Update the active sequence with the newly calibrated files.
        QStringList outPaths;
        QDir dir(s_workingDir);
        for (const QString& f : files) {
            outPaths << dir.absoluteFilePath("pp_" + f);
        }
        s_sequence = std::make_unique<Stacking::ImageSequence>();
        s_sequence->loadFromFiles(outPaths);
        return true;
    }

    return false;
}

// ============================================================================
// register -- Register (align) images
// ============================================================================

bool StackingCommands::cmdRegister(const ScriptCommand& cmd)
{
    QString prefix;
    if (!cmd.args.isEmpty()) {
        prefix = cmd.args[0];
    } else if (s_sequence && s_sequence->isValid()) {
        // Already have a sequence to register.
    } else {
        return false;
    }

    if (!prefix.isEmpty()) {
        QDir dir(s_workingDir);
        QStringList filters;
        filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";

        QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
        if (files.size() < 2)
            return false;

        QStringList fullPaths;
        for (const QString& f : files)
            fullPaths << dir.absoluteFilePath(f);

        // Create a fresh sequence.
        s_sequence = std::make_unique<Stacking::ImageSequence>();
        if (!s_sequence->loadFromFiles(fullPaths)) {
            s_sequence.reset();
            return false;
        }
    }

    // Configure registration parameters.
    Stacking::RegistrationParams params;
    params.allowRotation = !cmd.hasOption("norotation");
    params.drizzle       = cmd.hasOption("drizzle");

    if (cmd.hasOption("sigma"))
        params.detectionThreshold = cmd.option("sigma").toFloat();
    if (cmd.hasOption("maxstars"))
        params.maxStars = cmd.option("maxstars").toInt();

    // Ensure output directory is set to s_workingDir for r_ files
    params.outputDirectory = s_workingDir;

    // Run registration.
    Stacking::RegistrationEngine engine;
    engine.setParams(params);

    if (s_runner) {
        QObject::connect(
            &engine, &Stacking::RegistrationEngine::logMessage,
            [](const QString& msg, const QString& color) {
                if (s_runner) s_runner->logMessage(msg, color);
            });
        QObject::connect(
            &engine, &Stacking::RegistrationEngine::progressChanged,
            [](const QString& msg, double pct) {
                if (s_runner) s_runner->progressChanged(msg, pct);
            });
        QObject::connect(
            s_runner, &Scripting::ScriptRunner::cancelRequested,
            &engine,  &Stacking::RegistrationEngine::cancel);
    }

    int successCount = engine.registerSequence(*s_sequence, -1);

    // Consider registration successful if at least 80 % of frames aligned.
    return successCount >= static_cast<int>(s_sequence->count() * 0.8);
}

// ============================================================================
// setmaster -- Set a master calibration frame
// ============================================================================

bool StackingCommands::cmdSetMaster(const ScriptCommand& cmd)
{
    QString type = cmd.args[0].toLower();
    QString path = resolvePath(cmd.args[1]);

    Preprocessing::MasterType masterType;
    if      (type == "bias" || type == "offset") masterType = Preprocessing::MasterType::Bias;
    else if (type == "dark")                     masterType = Preprocessing::MasterType::Dark;
    else if (type == "flat")                     masterType = Preprocessing::MasterType::Flat;
    else if (type == "darkflat")                 masterType = Preprocessing::MasterType::DarkFlat;
    else                                         return false;

    Preprocessing::PreprocessParams params = s_preprocessor.params();
    if      (masterType == Preprocessing::MasterType::Bias)     { params.masterBias = path; params.useBias = true; }
    else if (masterType == Preprocessing::MasterType::Dark)     { params.masterDark = path; params.useDark = true; }
    else if (masterType == Preprocessing::MasterType::Flat)     { params.masterFlat = path; params.useFlat = true; }
    else if (masterType == Preprocessing::MasterType::DarkFlat) { params.masterDarkFlat = path; }

    s_preprocessor.setParams(params);
    return s_preprocessor.masters().isLoaded(masterType);
}

// ============================================================================
// debayer -- Debayer the current image or a sequence
// ============================================================================

bool StackingCommands::cmdDebayer(const ScriptCommand& cmd)
{
    Preprocessing::PreprocessParams params;
    params.debayer = true;

    // Parse Bayer pattern.
    QString pattern = cmd.option("pattern", "AUTO").toUpper();
    if (pattern.isEmpty()) pattern = "AUTO";

    if      (pattern == "AUTO") params.bayerPattern = Preprocessing::BayerPattern::Auto;
    else if (pattern == "RGGB") params.bayerPattern = Preprocessing::BayerPattern::RGGB;
    else if (pattern == "BGGR") params.bayerPattern = Preprocessing::BayerPattern::BGGR;
    else if (pattern == "GBRG") params.bayerPattern = Preprocessing::BayerPattern::GBRG;
    else if (pattern == "GRBG") params.bayerPattern = Preprocessing::BayerPattern::GRBG;

    // Parse debayer algorithm.
    QString algo = cmd.option("algo", "vng").toLower();
    if      (algo == "bilinear")   params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::Bilinear;
    else if (algo == "vng")        params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::VNG;
    else if (algo == "superpixel") params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::SuperPixel;
    else if (algo == "rcd")        params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::RCD;
    else if (algo == "ahd")        params.debayerAlgorithm = Preprocessing::DebayerAlgorithm::AHD;

    // -- Batch mode: prefix argument provided --------------------------------
    if (!cmd.args.isEmpty()) {
        QString prefix = cmd.args[0];
        QDir dir(s_workingDir);
        QStringList filters;
        filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";
        QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);

        if (files.isEmpty()) {
            if (s_runner)
                s_runner->logMessage(
                    QObject::tr("Debayer: No files found matching ") + prefix, "salmon");
            return false;
        }

        QStringList fullPaths;
        for (const QString& f : files)
            fullPaths << dir.absoluteFilePath(f);

        params.outputPrefix = "deb_";
        params.outputDir    = s_workingDir;

        s_preprocessor.setParams(params);
        int processed = s_preprocessor.preprocessBatch(
            fullPaths, s_workingDir);

        if (processed == fullPaths.size()) {
            QStringList outPaths;
            for (const QString& f : files)
                outPaths << dir.absoluteFilePath("deb_" + f);

            s_sequence = std::make_unique<Stacking::ImageSequence>();
            s_sequence->loadFromFiles(outPaths);
            return true;
        }
        return false;
    }

    // -- Single image mode ---------------------------------------------------
    if (!s_currentImage || !s_currentImage->isValid())
        return false;

    s_preprocessor.setParams(params);

    ImageBuffer output;
    if (!s_preprocessor.preprocessImage(*s_currentImage, output))
        return false;

    *s_currentImage = std::move(output);
    return true;
}

// ============================================================================
// background -- Background extraction
// ============================================================================

bool StackingCommands::cmdBackground(const ScriptCommand& cmd)
{
    // -- Step 1: Determine target (sequence or current image) -----------------
    QString sequencePrefix;
    if (!cmd.args.isEmpty()) {
        sequencePrefix = cmd.args[0];
    } else if (s_sequence && s_sequence->count() > 0) {
        // Use the currently active sequence if no prefix specified.
    }

    int   degree    = cmd.option("degree",    "2").toInt();
    float tolerance = cmd.option("tolerance", "3.0").toFloat();
    float smoothing = cmd.option("smoothing", "0.5").toFloat();
    bool  division  = cmd.hasOption("div") || cmd.hasOption("division");

    Background::BackgroundExtractor extractor;
    extractor.setParameters(degree, tolerance, smoothing);

    auto corrType = division
        ? Background::CorrectionType::Division
        : Background::CorrectionType::Subtraction;

    // -- Step 2: Handle sequence processing ----------------------------------
    if (!sequencePrefix.isEmpty() || (s_sequence && s_sequence->count() > 0)) {
        QStringList files;
        QDir dir(s_workingDir);

        if (!sequencePrefix.isEmpty()) {
            QStringList filters;
            filters << sequencePrefix + "*.fit" << sequencePrefix + "*.fits" << sequencePrefix + "*.fts";
            files = dir.entryList(filters, QDir::Files, QDir::Name);
        } else {
            for (int i = 0; i < s_sequence->count(); ++i) {
                files << s_sequence->image(i).fileName();
            }
        }

        if (files.isEmpty()) {
            if (s_runner)
                s_runner->setVariable("error", "No sequence files found");
            return false;
        }

        if (s_runner)
            s_runner->logMessage(
                QObject::tr("Batch background extraction: processing %1 files")
                    .arg(files.size()), "cyan");

        int processed = 0;
        for (int i = 0; i < files.size(); ++i) {
            QString path = dir.absoluteFilePath(files[i]);
            ImageBuffer buf;
            if (!Stacking::FitsIO::read(path, buf)) {
                if (s_runner)
                    s_runner->logMessage(QObject::tr("Failed to load: %1").arg(files[i]), "orange");
                continue;
            }

            extractor.generateGrid(buf);
            if (!extractor.computeModel()) {
                if (s_runner)
                    s_runner->logMessage(QObject::tr("Failed to compute model for: %1").arg(files[i]), "orange");
                continue;
            }

            ImageBuffer result;
            if (!extractor.apply(buf, result, corrType))
                continue;

            QString outName = "bkg_" + files[i];
            QString outPath = dir.absoluteFilePath(outName);
            if (Stacking::FitsIO::write(outPath, result, 32)) {
                processed++;
            }

            if (s_runner)
                s_runner->progressChanged(
                    QString("Backgr. Extraction: %1").arg(files[i]),
                    (double)(i + 1) / files.size());
        }

        if (processed > 0) {
            // Update active sequence with background-removed files.
            QStringList outPaths;
            for (int i = 0; i < files.size(); ++i) {
                outPaths << dir.absoluteFilePath("bkg_" + files[i]);
            }
            s_sequence = std::make_unique<Stacking::ImageSequence>();
            s_sequence->loadFromFiles(outPaths);
        }

        if (s_runner)
            s_runner->logMessage(
                QString(QObject::tr("Batch removal complete: %1 of %2 files processed"))
                    .arg(processed).arg(files.size()),
                processed > 0 ? "green" : "red");

        return processed > 0;
    }

    // -- Step 3: Fallback to current image -----------------------------------
    if (!s_currentImage || !s_currentImage->isValid()) {
        if (s_runner)
            s_runner->setVariable("error", "No image or sequence loaded");
        return false;
    }

    extractor.generateGrid(*s_currentImage);
    if (!extractor.computeModel()) {
        if (s_runner)
            s_runner->setVariable("error", "Failed to compute background model");
        return false;
    }

    ImageBuffer result;
    if (!extractor.apply(*s_currentImage, result, corrType))
        return false;

    *s_currentImage = std::move(result);
    if (s_runner)
        s_runner->logMessage(QObject::tr("Background extraction applied to current image"), "green");

    return true;
}

// ============================================================================
// seqextract_Ha / seqextract_HaOIII -- Extract narrowband from CFA
// ============================================================================

bool StackingCommands::cmdSeqExtract(const ScriptCommand& cmd)
{
    if (cmd.name != "seqextract_Ha" && cmd.name != "seqextract_HaOIII")
        return false;

    QString prefix    = cmd.args[0];
    bool    splitOIII = (cmd.name == "seqextract_HaOIII");

    QDir dir(s_workingDir);
    QStringList filters;
    filters << prefix + "*.fit" << prefix + "*.fits" << prefix + "*.fts";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return false;

    QString patternStr = cmd.option("pattern", "RGGB").toUpper();

    for (const QString& f : files) {
        QString     inPath = dir.absoluteFilePath(f);
        ImageBuffer buf;
        if (!Stacking::FitsIO::read(inPath, buf))
            continue;

        // CFA data must be single-channel.
        if (buf.channels() != 1)
            continue;

        const int w  = buf.width();
        const int h  = buf.height();
        const int hw = w / 2;
        const int hh = h / 2;

        const float* inData = buf.data().data();

        // Determine sub-pixel offsets for each color site.
        int rX = 0, rY = 0;
        int g1X = 1, g1Y = 0;
        int g2X = 0, g2Y = 1;
        int bX = 1, bY = 1;

        if      (patternStr == "BGGR") { rX=1; rY=1; g1X=0; g1Y=1; g2X=1; g2Y=0; bX=0; bY=0; }
        else if (patternStr == "GBRG") { rX=0; rY=1; g1X=0; g1Y=0; g2X=1; g2Y=1; bX=1; bY=0; }
        else if (patternStr == "GRBG") { rX=1; rY=0; g1X=0; g1Y=0; g2X=1; g2Y=1; bX=0; bY=1; }

        // -- Extract Ha (Red channel) ----------------------------------------
        ImageBuffer ha(hw, hh, 1);
        float* haData = ha.data().data();

        for (int y = 0; y < hh; ++y)
            for (int x = 0; x < hw; ++x)
                haData[y * hw + x] =
                    inData[(y * 2 + rY) * w + (x * 2 + rX)];

        QString haName = "Ha_" + f;
        Stacking::FitsIO::write(dir.absoluteFilePath(haName), ha, 16);

        // -- Extract OIII (average of G1, G2, B) -----------------------------
        if (splitOIII) {
            ImageBuffer oiii(hw, hh, 1);
            float* o3Data = oiii.data().data();

            for (int y = 0; y < hh; ++y) {
                for (int x = 0; x < hw; ++x) {
                    float g1 = inData[(y * 2 + g1Y) * w + (x * 2 + g1X)];
                    float g2 = inData[(y * 2 + g2Y) * w + (x * 2 + g2X)];
                    float b  = inData[(y * 2 + bY)  * w + (x * 2 + bX)];
                    o3Data[y * hw + x] = (g1 + g2 + b) / 3.0f;
                }
            }

            QString o3Name = "OIII_" + f;
            Stacking::FitsIO::write(
                dir.absoluteFilePath(o3Name), oiii, 16);
        }
    }

    return true;
}

// ============================================================================
// mirrorx / mirrory -- Flip image
// ============================================================================

bool StackingCommands::cmdMirror(const ScriptCommand& cmd)
{
    if (!s_currentImage || !s_currentImage->isValid())
        return false;

    bool bottomUp = cmd.hasOption("bottomup");

    const int w = s_currentImage->width();
    const int h = s_currentImage->height();
    const int c = s_currentImage->channels();
    float* data = s_currentImage->data().data();

    if (bottomUp) {
        // Vertical flip: swap rows from top and bottom towards the center.
        std::vector<float> rowBuffer(w * c);
        for (int y = 0; y < h / 2; ++y) {
            float* topRow = data + static_cast<size_t>(y)         * w * c;
            float* botRow = data + static_cast<size_t>(h - 1 - y) * w * c;

            std::memcpy(rowBuffer.data(), topRow, w * c * sizeof(float));
            std::memcpy(topRow, botRow, w * c * sizeof(float));
            std::memcpy(botRow, rowBuffer.data(), w * c * sizeof(float));
        }
    } else {
        // Horizontal flip: swap columns left-to-right within each row.
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w / 2; ++x) {
                for (int ch = 0; ch < c; ++ch) {
                    size_t idx1 = (static_cast<size_t>(y) * w + x) * c + ch;
                    size_t idx2 = (static_cast<size_t>(y) * w + (w - 1 - x)) * c + ch;
                    std::swap(data[idx1], data[idx2]);
                }
            }
        }
    }

    return true;
}

// ============================================================================
// rgbcomp -- Combine three monochrome images into an RGB composite
// ============================================================================

bool StackingCommands::cmdRGBComp(const ScriptCommand& cmd)
{
    if (cmd.args.size() < 3)
        return false;

    QString rPath = resolvePath(cmd.args[0]);
    QString gPath = resolvePath(cmd.args[1]);
    QString bPath = resolvePath(cmd.args[2]);

    ImageBuffer rImg, gImg, bImg;
    if (!Stacking::FitsIO::read(rPath, rImg) ||
        !Stacking::FitsIO::read(gPath, gImg) ||
        !Stacking::FitsIO::read(bPath, bImg))
        return false;

    // Verify dimensions match.
    if (rImg.width()  != gImg.width()  || rImg.width()  != bImg.width() ||
        rImg.height() != gImg.height() || rImg.height() != bImg.height()) {
        if (s_runner)
            s_runner->setVariable("error", "Image dimensions do not match");
        return false;
    }

    // Construct an interleaved RGB image buffer.
    const int w = rImg.width();
    const int h = rImg.height();
    ImageBuffer rgb(w, h, 3);
    float* data = rgb.data().data();
    const float* rData = rImg.data().data();
    const float* gData = gImg.data().data();
    const float* bData = bImg.data().data();

    #pragma omp parallel for
    for (int i = 0; i < w * h; ++i) {
        data[i * 3 + 0] = rData[i];
        data[i * 3 + 1] = gData[i];
        data[i * 3 + 2] = bData[i];
    }

    QString outName = cmd.option("out", "rgb_composition.fit");
    return Stacking::FitsIO::write(resolvePath(outName), rgb, 32);
}

// ============================================================================
// linear_match -- Normalize target image to match reference statistics
// ============================================================================

bool StackingCommands::cmdLinearMatch(const ScriptCommand& cmd)
{
    QString refPath = resolvePath(cmd.args[0]);
    QString tgtPath = resolvePath(cmd.args[1]);

    ImageBuffer refImg, tgtImg;
    if (!Stacking::FitsIO::read(refPath, refImg) ||
        !Stacking::FitsIO::read(tgtPath, tgtImg))
        return false;

    const int sizeRef = refImg.width() * refImg.height();
    const int sizeTgt = tgtImg.width() * tgtImg.height();

    Stacking::Statistics stats;

    // Compute median and MAD for the first channel of each image.
    std::vector<float> refPixels(
        refImg.data().begin(), refImg.data().begin() + sizeRef);
    double refMedian = stats.median(refPixels);
    double refMad    = stats.mad(refPixels, refMedian);

    std::vector<float> tgtPixels(
        tgtImg.data().begin(), tgtImg.data().begin() + sizeTgt);
    double tgtMedian = stats.median(tgtPixels);
    double tgtMad    = stats.mad(tgtPixels, tgtMedian);

    if (tgtMad == 0.0)
        tgtMad = 1.0; // Guard against division by zero.

    // Apply linear transform: pixel = pixel * scale + offset.
    double scale  = refMad / tgtMad;
    double offset = refMedian - (tgtMedian * scale);

    for (float& val : tgtImg.data())
        val = static_cast<float>(val * scale + offset);

    QString outPath = cmd.option("out", tgtPath);
    return Stacking::FitsIO::write(resolvePath(outPath), tgtImg, 32);
}

// ============================================================================
// pm -- Evaluate a PixelMath expression
// ============================================================================

bool StackingCommands::cmdPixelMath(const ScriptCommand& cmd)
{
    if (!s_currentImage || !s_currentImage->isValid())
        return false;

    QString expression = cmd.args[0];

    // Strip surrounding quotes if present.
    if (expression.startsWith("\"") && expression.endsWith("\""))
        expression = expression.mid(1, expression.length() - 2);

    Stacking::PixelMath pm;

    // Pre-load all $varName$ image references found in the expression.
    // Each variable is resolved as a file path relative to the working
    // directory, with an implicit ".fit" extension appended if the bare
    // path does not exist on disk.
    QMap<QString, std::shared_ptr<ImageBuffer>> loadedVars;
    int pos = 0;

    while ((pos = expression.indexOf('$', pos)) != -1) {
        const int end = expression.indexOf('$', pos + 1);
        if (end == -1)
            break;

        const QString varName = expression.mid(pos + 1, end - pos - 1);
        if (!varName.isEmpty() && !loadedVars.contains(varName)) {
            QString path = resolvePath(varName);
            if (!QFileInfo::exists(path))
                path += ".fit";

            auto img = std::make_shared<ImageBuffer>();
            if (Stacking::FitsIO::read(path, *img)) {
                loadedVars[varName] = img;
                // Register under both bare and $-delimited names so that
                // either tokenizer convention is satisfied.
                pm.setVariable(varName, img.get());
                pm.setVariable("$" + varName + "$", img.get());
            } else {
                if (s_runner)
                    s_runner->logMessage(
                        QObject::tr("PixelMath: could not load variable "
                                "'%1' from '%2'")
                            .arg(varName, path),
                        "orange");
            }
        }
        pos = end + 1;
    }

    ImageBuffer result;
    if (pm.evaluate(expression, result)) {
        *s_currentImage = std::move(result);
        return true;
    }

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("PixelMath error: %1").arg(pm.lastError()), "red");
    return false;
}

// ============================================================================
// starnet -- Run StarNet++ star removal
// ============================================================================

bool StackingCommands::cmdStarNet(const ScriptCommand& cmd)
{
    if (!s_currentImage || !s_currentImage->isValid()) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: no image loaded"), "red");
        return false;
    }

    // -- Locate the StarNet executable ---------------------------------------
    QSettings settings("TStar", "TStar");
    QString starnetExe = settings.value("paths/starnet").toString();

    if (starnetExe.isEmpty() || !QFileInfo::exists(starnetExe)) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet executable not configured or not found. "
                            "Please set it in Settings \u2192 StarNet Executable."),
                "red");
        return false;
    }

    // -- Derive file names from the current image stem -----------------------
    QString stem = s_currentFilename.isEmpty()
        ? QStringLiteral("image")
        : QFileInfo(s_currentFilename).completeBaseName();

    QString inputTif  = resolvePath("starnet_"  + stem + ".tif");
    QString outputTif = resolvePath("starless_" + stem + ".tif");
    QString outputFit = resolvePath("starless_" + stem + ".fit");

    // -- Write a 16-bit TIFF for StarNet input -------------------------------
    if (!Stacking::TiffIO::write(inputTif, *s_currentImage, 16)) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: failed to write input TIFF: %1")
                    .arg(inputTif),
                "red");
        return false;
    }

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("StarNet: input TIFF saved: %1").arg(inputTif),
            "neutral");

    // -- Switch to StarNet's directory so it can find weight files ------------
    QString starnetDir  = QFileInfo(starnetExe).absolutePath();
    QString previousDir = QDir::currentPath();
    QDir::setCurrent(starnetDir);

    // -- Build command-line arguments ----------------------------------------
    QStringList args;
    bool isTorchFormat = starnetExe.contains("torch", Qt::CaseInsensitive);

    if (isTorchFormat) {
        args << QLatin1String("-i") << inputTif
             << QLatin1String("-o") << outputTif;
    } else {
        // Standard v1/v2: positional arguments.
        args << inputTif << outputTif;
    }

    if (cmd.hasOption("stride"))
        args << cmd.option("stride");

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("StarNet: starting %1 %2")
                .arg(starnetExe, args.join(" ")),
            "neutral");

    // -- Launch the process --------------------------------------------------
    QProcess process;
    process.start(starnetExe, args);

    if (!process.waitForStarted(5000)) {
        QDir::setCurrent(previousDir);
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: failed to start -- check executable "
                            "path and permissions."),
                "red");
        QFile::remove(inputTif);
        return false;
    }

    // -- Poll stdout for real-time progress ----------------------------------
    // StarNet emits progress as "N% finished" token pairs on stdout.
    QString accumulatedOut;

    auto flushAccumulated = [&]() {
        QStringList tokens = accumulatedOut.split(
            QRegularExpression("[\\s]+"), Qt::SkipEmptyParts);

        for (int ti = 0; ti < tokens.size(); ++ti) {
            const QString& tok = tokens[ti];

            if (tok.endsWith('%') && ti + 1 < tokens.size() &&
                tokens[ti + 1].compare("finished",
                                       Qt::CaseInsensitive) == 0) {
                QString pct = tok.left(tok.size() - 1);
                bool ok;
                int pctVal = pct.toInt(&ok);
                if (ok && s_runner) {
                    s_runner->logMessage(
                        QObject::tr("StarNet: %1% completed").arg(pct),
                        "neutral");
                    s_runner->progressChanged(
                        QObject::tr("StarNet processing..."),
                        pctVal / 100.0);
                }
                ++ti; // Skip the "finished" token.
            } else if (tok.compare("Done!", Qt::CaseInsensitive) == 0 ||
                       tok.compare("Done",  Qt::CaseInsensitive) == 0) {
                if (s_runner)
                    s_runner->logMessage(
                        QObject::tr("StarNet: Done!"), "neutral");
            }
        }
        accumulatedOut.clear();
    };

    while (!process.waitForFinished(200)) {
        QCoreApplication::processEvents();

        QByteArray chunk = process.readAllStandardOutput();
        if (!chunk.isEmpty()) {
            accumulatedOut += QString::fromUtf8(chunk);
            flushAccumulated();
        }

        if (s_runner && s_runner->isCancelled()) {
            process.kill();
            process.waitForFinished(3000);
            QDir::setCurrent(previousDir);
            QFile::remove(inputTif);
            return false;
        }
    }

    // Flush any remaining output after the process exits.
    accumulatedOut += QString::fromUtf8(process.readAllStandardOutput());
    flushAccumulated();

    QDir::setCurrent(previousDir);

    // Log non-trivial stderr (ignore TensorFlow informational messages).
    QString stdErr = QString::fromUtf8(
        process.readAllStandardError()).trimmed();
    if (!stdErr.isEmpty() && !stdErr.startsWith("2") && s_runner)
        s_runner->logMessage(
            QObject::tr("StarNet: %1").arg(stdErr), "orange");

    // Check exit status.
    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: process exit code: %1")
                    .arg(process.exitCode()),
                "red");
        QFile::remove(inputTif);
        return false;
    }

    // -- Verify the output TIFF exists ---------------------------------------
    if (!QFileInfo::exists(outputTif)) {
        if (s_runner) {
            QString outputDir = QFileInfo(outputTif).absolutePath();
            QDir tifDir(outputDir);
            QStringList tifFiles =
                tifDir.entryList({"*.tif", "*.tiff"}, QDir::Files);
            QString filesMsg = tifFiles.isEmpty()
                ? QObject::tr("(no .tif files found)")
                : QObject::tr("Found: %1").arg(tifFiles.join(", "));
            s_runner->logMessage(
                QObject::tr("StarNet: output TIFF not found: %1\n%2")
                    .arg(outputTif, filesMsg),
                "red");
        }
        QFile::remove(inputTif);
        return false;
    }

    // -- Read the starless TIFF via OpenCV -----------------------------------
    cv::Mat mat = cv::imread(
        outputTif.toStdString(),
        cv::IMREAD_UNCHANGED | cv::IMREAD_ANYCOLOR | cv::IMREAD_ANYDEPTH);

    if (mat.empty()) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: OpenCV failed to read output TIFF: %1")
                    .arg(outputTif),
                "red");
        QFile::remove(inputTif);
        QFile::remove(outputTif);
        return false;
    }

    // -- Convert cv::Mat to float32 RGB ImageBuffer --------------------------
    const int w    = mat.cols;
    const int h    = mat.rows;
    const int cvCh = mat.channels();

    std::vector<float> floatData(w * h * cvCh);
    bool  is16bit = (mat.depth() == CV_16U);
    bool  is8bit  = (mat.depth() == CV_8U);
    float scale   = is16bit ? (1.0f / 65535.0f)
                  : is8bit  ? (1.0f / 255.0f)
                  : 1.0f;

    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            int dstBase = (r * w + c) * cvCh;

            if (cvCh == 3) {
                if (is16bit) {
                    cv::Vec3w px = mat.at<cv::Vec3w>(r, c);
                    floatData[dstBase + 0] = px[2] * scale; // BGR -> RGB
                    floatData[dstBase + 1] = px[1] * scale;
                    floatData[dstBase + 2] = px[0] * scale;
                } else if (is8bit) {
                    cv::Vec3b px = mat.at<cv::Vec3b>(r, c);
                    floatData[dstBase + 0] = px[2] * scale;
                    floatData[dstBase + 1] = px[1] * scale;
                    floatData[dstBase + 2] = px[0] * scale;
                } else {
                    cv::Vec3f px = mat.at<cv::Vec3f>(r, c);
                    floatData[dstBase + 0] = px[2];
                    floatData[dstBase + 1] = px[1];
                    floatData[dstBase + 2] = px[0];
                }
            } else {
                // Monochrome.
                if      (is16bit) floatData[dstBase] = mat.at<quint16>(r, c) * scale;
                else if (is8bit)  floatData[dstBase] = mat.at<quint8>(r, c)  * scale;
                else              floatData[dstBase] = mat.at<float>(r, c);
            }
        }
    }

    ImageBuffer starless;
    starless.setData(w, h, cvCh, floatData);

    if (!starless.isValid()) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: loaded TIFF produced invalid buffer."),
                "red");
        QFile::remove(inputTif);
        QFile::remove(outputTif);
        return false;
    }

    // -- Save as 32-bit FITS for subsequent PixelMath references -------------
    if (!Stacking::FitsIO::write(outputFit, starless, 32)) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: could not save starless FITS: %1")
                    .arg(outputFit),
                "orange");
    } else {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("StarNet: starless FITS saved: %1")
                    .arg(outputFit),
                "green");
    }

    // -- Clean up temporary TIFF files ---------------------------------------
    QFile::remove(inputTif);
    QFile::remove(outputTif);

    // Update the current image state for subsequent commands.
    s_currentFilename = QFileInfo(outputFit).fileName();
    *s_currentImage   = std::move(starless);

    return true;
}

// ============================================================================
// newproject -- Create a new stacking project
// ============================================================================

bool StackingCommands::cmdNewProject(const ScriptCommand& cmd)
{
    QString path = resolvePath(cmd.args[0]);

    Stacking::StackingProject project;
    if (!project.create(path)) {
        if (s_runner)
            s_runner->setVariable("error",
                "Failed to create project at: " + path);
        return false;
    }

    if (s_runner) {
        s_runner->setVariable("project", path);
        s_runner->setVariable("biases",  project.biasDir());
        s_runner->setVariable("darks",   project.darkDir());
        s_runner->setVariable("flats",   project.flatDir());
        s_runner->setVariable("lights",  project.lightDir());
        s_runner->setVariable("process", project.processDir());
        s_runner->setVariable("output",  project.outputDir());

        emit s_runner->logMessage(
            QObject::tr("Project created: %1").arg(path), "green");
    }

    return true;
}

// ============================================================================
// convertall -- Batch-convert RAW files to FITS
// ============================================================================

bool StackingCommands::cmdConvertAll(const ScriptCommand& cmd)
{
    QString pattern = cmd.args[0];
    QString outDir  = cmd.hasOption("out")
        ? resolvePath(cmd.option("out"))
        : s_workingDir;

    QDir dir(s_workingDir);

    // Supported RAW and TIFF extensions.
    QStringList rawFilters;
    rawFilters << "*.cr2" << "*.CR2" << "*.nef" << "*.NEF"
               << "*.arw" << "*.ARW" << "*.dng" << "*.DNG"
               << "*.orf" << "*.ORF" << "*.rw2" << "*.RW2"
               << "*.raf" << "*.RAF" << "*.pef" << "*.PEF"
               << "*.tif" << "*.TIF" << "*.tiff" << "*.TIFF";

    QStringList files;

    if (pattern.contains("*")) {
        files = dir.entryList(QStringList() << pattern,
                              QDir::Files, QDir::Name);
    } else {
        // Treat pattern as a filename prefix.
        for (const QString& filter : rawFilters)
            files += dir.entryList(
                QStringList() << pattern + filter.mid(1),
                QDir::Files, QDir::Name);

        if (files.isEmpty())
            files = dir.entryList(rawFilters, QDir::Files, QDir::Name);
    }

    if (files.isEmpty()) {
        if (s_runner)
            s_runner->setVariable("error",
                "No files found matching: " + pattern);
        return false;
    }

    int converted = 0;
    for (const QString& file : files) {
        ScriptCommand convertCmd;
        convertCmd.name = "convert";
        convertCmd.args << file;
        if (!outDir.isEmpty())
            convertCmd.options["out"] = outDir;

        if (cmdConvert(convertCmd))
            converted++;
    }

    if (s_runner) {
        emit s_runner->logMessage(
            QObject::tr("Converted %1 of %2 files")
                .arg(converted).arg(files.size()),
            converted == files.size() ? "green" : "orange");
    }

    return converted > 0;
}

// ============================================================================
// autostack -- Full pipeline: calibrate, register, stack
// ============================================================================

bool StackingCommands::cmdAutoStack(const ScriptCommand& cmd)
{
    QString projectPath = resolvePath(cmd.args[0]);

    // -- Load or create the project ------------------------------------------
    Stacking::StackingProject project;
    QString projectFile =
        Stacking::StackingProject::findProjectFile(projectPath);

    if (!projectFile.isEmpty()) {
        if (!project.load(projectFile)) {
            if (s_runner)
                s_runner->setVariable("error",
                    "Failed to load project: " + projectPath);
            return false;
        }
    } else {
        if (!project.create(projectPath)) {
            if (s_runner)
                s_runner->setVariable("error",
                    "Invalid project directory: " + projectPath);
            return false;
        }
    }

    if (s_runner)
        emit s_runner->logMessage(
            QObject::tr("Starting pipeline for: %1").arg(project.name()),
            "cyan");

    // -- Step 1: Discover light frames ---------------------------------------
    QDir lightDir(project.lightDir());
    QStringList lightFiles = lightDir.entryList(
        QStringList() << "*.fit" << "*.fits" << "*.FIT" << "*.FITS",
        QDir::Files, QDir::Name);

    if (lightFiles.isEmpty()) {
        if (s_runner)
            s_runner->setVariable("error",
                "No light frames found in: " + project.lightDir());
        return false;
    }

    if (s_runner)
        emit s_runner->logMessage(
            QObject::tr("Found %1 light frames").arg(lightFiles.size()),
            "white");

    // -- Step 2: Calibrate (if master frames are available) -------------------
    s_workingDir = project.lightDir();

    if (!project.masterBias().isEmpty() ||
        !project.masterDark().isEmpty() ||
        !project.masterFlat().isEmpty()) {

        ScriptCommand calibrateCmd;
        calibrateCmd.name = "calibrate";
        calibrateCmd.args << "*.fit";

        if (!project.masterBias().isEmpty())
            calibrateCmd.options["bias"] = project.masterBias();
        if (!project.masterDark().isEmpty())
            calibrateCmd.options["dark"] = project.masterDark();
        if (!project.masterFlat().isEmpty())
            calibrateCmd.options["flat"] = project.masterFlat();

        if (s_runner)
            emit s_runner->logMessage(
                QObject::tr("Calibrating frames..."), "white");
        cmdCalibrate(calibrateCmd);
    }

    // -- Check for CFA data and auto-debayer if needed -----------------------
    QString registerPattern = "pp_*.fit";
    QString stackPattern    = "r_*.fit";

    QDir procDir(project.processDir());
    QStringList ppFiles =
        procDir.entryList(QStringList() << "pp_*.fit", QDir::Files);

    if (!ppFiles.isEmpty()) {
        QString     firstFile = procDir.absoluteFilePath(ppFiles.first());
        ImageBuffer checkBuf;

        if (Stacking::FitsIO::readHeader(firstFile, checkBuf)) {
            bool isCFA = checkBuf.channels() == 1 &&
                (!checkBuf.metadata().bayerPattern.isEmpty() ||
                 checkBuf.metadata().xisfProperties.contains("BayerPattern"));

            if (s_runner) {
                QString bayerDebug = checkBuf.metadata().bayerPattern;
                if (bayerDebug.isEmpty())
                    bayerDebug = checkBuf.metadata()
                        .xisfProperties.value("BayerPattern").toString();
                if (bayerDebug.isEmpty())
                    bayerDebug = "NONE";

                s_runner->logMessage(
                    QObject::tr("CFA Check - Channels: %1, Pattern: %2, "
                            "Detected: %3")
                        .arg(checkBuf.channels())
                        .arg(bayerDebug)
                        .arg(isCFA ? "YES" : "NO"),
                    "yellow");
            }

            if (isCFA) {
                if (s_runner)
                    emit s_runner->logMessage(
                        QObject::tr("Auto-Debayering CFA images..."), "cyan");

                QString bayerPat = checkBuf.metadata().bayerPattern;
                if (bayerPat.isEmpty())
                    bayerPat = checkBuf.metadata()
                        .xisfProperties.value("BayerPattern").toString();

                Preprocessing::BayerPattern pat =
                    Preprocessing::BayerPattern::None;
                if      (bayerPat == "RGGB") pat = Preprocessing::BayerPattern::RGGB;
                else if (bayerPat == "BGGR") pat = Preprocessing::BayerPattern::BGGR;
                else if (bayerPat == "GBRG") pat = Preprocessing::BayerPattern::GBRG;
                else if (bayerPat == "GRBG") pat = Preprocessing::BayerPattern::GRBG;

                if (pat != Preprocessing::BayerPattern::None) {
                    QAtomicInt dbCount(0);
                    int        maxThreads = QThread::idealThreadCount();
                    QSemaphore throttle(maxThreads);

                    QtConcurrent::blockingMap(ppFiles,
                        [&](const QString& ppFile) {
                            throttle.acquire();
                            struct Guard {
                                QSemaphore& s;
                                ~Guard() { s.release(); }
                            } guard{throttle};

                            QString inP  = procDir.absoluteFilePath(ppFile);
                            QString outP = procDir.absoluteFilePath(
                                "deb_" + ppFile);

                            static thread_local ImageBuffer tBuf;

                            if (Stacking::FitsIO::read(inP, tBuf)) {
                                ImageBuffer colorBuf;
                                if (Preprocessing::Debayer::vng(
                                        tBuf, colorBuf, pat)) {
                                    colorBuf.setMetadata(tBuf.metadata());
                                    Stacking::FitsIO::write(outP, colorBuf);
                                    dbCount.fetchAndAddRelaxed(1);
                                }
                            }
                        });

                    if (s_runner)
                        emit s_runner->logMessage(
                            QObject::tr("Debayered %1 images")
                                .arg(dbCount.loadRelaxed()),
                            "green");

                    registerPattern = "deb_pp_*.fit";
                    stackPattern    = "r_deb_pp_*.fit";
                }
            }
        }
    }

    // -- Step 3: Register ----------------------------------------------------
    if (s_runner)
        emit s_runner->logMessage(
            QObject::tr("Registering frames..."), "white");

    s_workingDir = project.processDir();
    ScriptCommand registerCmd;
    registerCmd.name = "register";
    registerCmd.args << registerPattern;

    if (!cmdRegister(registerCmd)) {
        // Fallback: try raw lights if no calibrated files were found.
        s_workingDir = project.lightDir();
        registerCmd.args.clear();
        registerCmd.args << "*.fit";
        cmdRegister(registerCmd);
        stackPattern = "r_*.fit";
    }

    // -- Step 4: Stack -------------------------------------------------------
    if (s_runner)
        emit s_runner->logMessage(QObject::tr("Stacking..."), "white");

    s_workingDir = project.processDir();
    ScriptCommand stackCmd;
    stackCmd.name = "stack";
    stackCmd.args << stackPattern;
    stackCmd.options["out"] = project.stackedName();

    if (!cmdStack(stackCmd)) {
        stackCmd.args.clear();
        stackCmd.args << "r_*.fit";
        cmdStack(stackCmd);
    }

    if (s_runner)
        emit s_runner->logMessage(
            QObject::tr("Pipeline complete! Output: %1")
                .arg(project.stackedName()),
            "lime");

    return true;
}

// ============================================================================
// convert / link -- Convert source files to a FITS sequence
// ============================================================================

bool StackingCommands::cmdConvert(const ScriptCommand& cmd)
{
    if (cmd.args.isEmpty()) {
        if (s_runner)
            s_runner->setError(
                "Convert: Missing file type argument", cmd.lineNumber);
        return false;
    }

    QString type       = cmd.args[0].toLower();
    QString outDirName = cmd.options.value("out", "process");

    QDir workingDir(s_workingDir);
    if (!workingDir.exists()) {
        if (s_runner)
            s_runner->setError(
                "Convert: Working directory does not exist", cmd.lineNumber);
        return false;
    }

    // Create the output directory if necessary.
    QDir outDir = workingDir;
    if (!outDir.cd(outDirName)) {
        if (!workingDir.mkdir(outDirName) || !outDir.cd(outDirName)) {
            if (s_runner)
                s_runner->setError(
                    "Convert: Could not create output directory",
                    cmd.lineNumber);
            return false;
        }
    }

    // Discover files to convert (FITS + common RAW extensions).
    QStringList nameFilters;
    nameFilters << "*.fit" << "*.fits" << "*.cr2"
                << "*.nef" << "*.arw"  << "*.dng";

    QStringList files = workingDir.entryList(
        nameFilters, QDir::Files | QDir::NoDotAndDotDot);

    if (files.isEmpty()) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("Convert: No files found to convert."), "orange");
        return true; // Empty directory is not an error.
    }

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Converting %1 files using %2 threads...")
                .arg(files.size())
                .arg(ResourceManager::instance().maxThreads()),
            "neutral");

    QAtomicInt convertedCount(0);
    QAtomicInt failedCount(0);
    std::mutex logMutex;

    QtConcurrent::blockingMap(files, [&](const QString& file) {
        if (s_runner && s_runner->isCancelled())
            return;

        try {
            QString inputPath = workingDir.absoluteFilePath(file);
            QString ext       = QFileInfo(file).suffix().toLower();

            // Generate a sequential output filename: type_00001.fit
            int     currentIdx = convertedCount.fetchAndAddRelaxed(1);
            QString outName = QString("%1_%2.fit")
                .arg(type)
                .arg(currentIdx + 1, 5, 10, QChar('0'));
            QString outputPath = outDir.absoluteFilePath(outName);

            {
                std::lock_guard<std::mutex> lock(logMutex);
                if (s_runner)
                    s_runner->logMessage(
                        QObject::tr(" > %1 -> %2").arg(file, outName),
                        "#888888");
            }

            // Thread-local buffer to reduce heap churn.
            static thread_local ImageBuffer threadBuffer;
            threadBuffer.resize(0, 0, 0);
            threadBuffer.setMetadata(ImageBuffer::Metadata());

            bool loaded = false;

            // -- FITS input --------------------------------------------------
            if (ext == "fit" || ext == "fits" || ext == "fts") {
                loaded = IO::FitsLoaderCWrapper::loadFitsC(
                    inputPath, threadBuffer);
            }
            // -- TIFF input --------------------------------------------------
            else if (ext == "tif" || ext == "tiff") {
                loaded = Stacking::TiffIO::read(inputPath, threadBuffer);
            }
            // -- RAW input (LibRaw) ------------------------------------------
            else {
#ifdef HAVE_LIBRAW
                libraw_data_t* lr = libraw_init(0);
                if (lr) {
                    // Suppress non-fatal decompression warnings.
                    struct NoOpHandler {
                        static void callback(void*, const char*,
                                             const INT64) {}
                    };
                    libraw_set_dataerror_handler(
                        lr, NoOpHandler::callback, nullptr);

                    bool wantDebayer = cmd.hasOption("debayer");

                    int openRet = libraw_open_file(
                        lr, inputPath.toLocal8Bit().constData());

                    if (openRet == LIBRAW_SUCCESS) {
                        int unpackRet = libraw_unpack(lr);

                        // Accept LIBRAW_DATA_ERROR as non-fatal.
                        if (unpackRet == LIBRAW_SUCCESS ||
                            unpackRet == LIBRAW_DATA_ERROR) {

                            if (unpackRet == LIBRAW_DATA_ERROR) {
                                std::lock_guard<std::mutex> lock(logMutex);
                                if (s_runner)
                                    s_runner->logMessage(
                                        QObject::tr("Warning: minor data error "
                                                "in %1 (non-fatal, "
                                                "continuing)")
                                            .arg(file),
                                        "orange");
                            }

                            if (wantDebayer) {
                                // Full debayering path.
                                lr->params.output_bps     = 16;
                                lr->params.gamm[0]        = 1.0;
                                lr->params.gamm[1]        = 1.0;
                                lr->params.no_auto_bright  = 1;
                                lr->params.use_camera_wb   = 0;
                                lr->params.use_auto_wb     = 0;
                                lr->params.user_mul[0]     = 1.0f;
                                lr->params.user_mul[1]     = 1.0f;
                                lr->params.user_mul[2]     = 1.0f;
                                lr->params.user_mul[3]     = 1.0f;
                                lr->params.user_qual       = 3;
                                lr->params.output_color    = 0;

                                if (libraw_dcraw_process(lr) ==
                                    LIBRAW_SUCCESS) {
                                    int err = 0;
                                    libraw_processed_image_t* image =
                                        libraw_dcraw_make_mem_image(lr, &err);

                                    if (image && image->type ==
                                                 LIBRAW_IMAGE_BITMAP) {
                                        threadBuffer.resize(
                                            image->width, image->height,
                                            image->colors);
                                        float*           dst = threadBuffer.data().data();
                                        unsigned short*  src = (unsigned short*)image->data;
                                        size_t total = (size_t)image->width *
                                                       image->height *
                                                       image->colors;
                                        #pragma omp parallel for
                                        for (long long i = 0;
                                             i < (long long)total; ++i)
                                            dst[i] = src[i] / 65535.0f;

                                        loaded = true;
                                        libraw_dcraw_clear_mem(image);
                                    }
                                }
                            } else {
                                // Fast CFA path: extract raw Bayer data
                                // without any processing. Calibration
                                // (bias/dark/flat) handles the rest.
                                if (lr->rawdata.raw_image == nullptr &&
                                    (lr->rawdata.color3_image ||
                                     lr->rawdata.color4_image)) {
                                    std::lock_guard<std::mutex> lock(logMutex);
                                    if (s_runner)
                                        s_runner->logMessage(
                                            QObject::tr("Cannot open %1 in CFA "
                                                    "mode: no RAW data "
                                                    "available")
                                                .arg(file),
                                            "red");
                                    libraw_close(lr);
                                    failedCount.fetchAndAddRelaxed(1);
                                    return;
                                }

                                const int raw_width = lr->sizes.raw_width;
                                const int left      = lr->rawdata.sizes.left_margin;
                                const int top_m     = lr->rawdata.sizes.top_margin;
                                const int vw        = lr->sizes.iwidth;
                                const int vh        = lr->sizes.iheight;

                                threadBuffer.resize(vw, vh, 1);
                                float*          dst = threadBuffer.data().data();
                                unsigned short* src = lr->rawdata.raw_image;

                                if (src) {
                                    float maximum = (float)lr->color.maximum;
                                    if (maximum <= 0.0f) maximum = 65535.0f;

                                    unsigned int f = lr->idata.filters;

                                    // Normalize by sensor maximum only;
                                    // no black subtraction or white balance.
                                    int offset = raw_width * top_m + left;
                                    #pragma omp parallel for
                                    for (int y = 0; y < vh; ++y) {
                                        for (int x = 0; x < vw; ++x) {
                                            float val = (float)src[
                                                offset + x + (raw_width * y)];
                                            dst[y * vw + x] = val / maximum;
                                        }
                                    }
                                    loaded = true;

                                    // -- Populate CFA metadata ---------------
                                    threadBuffer.metadata().isMono = true;

                                    QString bayerPat = "RGGB";
                                    if      (f == 0x94949494) bayerPat = "RGGB";
                                    else if (f == 0x16161616) bayerPat = "BGGR";
                                    else if (f == 0x61616161) bayerPat = "GRBG";
                                    else if (f == 0x49494949) bayerPat = "GBRG";

                                    // Adjust pattern for crop margins.
                                    if (left % 2 != 0) {
                                        if      (bayerPat == "RGGB") bayerPat = "GRBG";
                                        else if (bayerPat == "BGGR") bayerPat = "GBRG";
                                        else if (bayerPat == "GRBG") bayerPat = "RGGB";
                                        else if (bayerPat == "GBRG") bayerPat = "BGGR";
                                    }
                                    if (top_m % 2 != 0) {
                                        if      (bayerPat == "RGGB") bayerPat = "GBRG";
                                        else if (bayerPat == "BGGR") bayerPat = "GRBG";
                                        else if (bayerPat == "GRBG") bayerPat = "BGGR";
                                        else if (bayerPat == "GBRG") bayerPat = "RGGB";
                                    }

                                    auto& meta = threadBuffer.metadata();
                                    meta.xisfProperties["BayerPattern"] = bayerPat;
                                    meta.bayerPattern = bayerPat;
                                    meta.rawHeaders.push_back(
                                        {"BAYERPAT", bayerPat, "Bayer Pattern"});
                                    meta.rawHeaders.push_back(
                                        {"ROWORDER", "TOP-DOWN",
                                         "Row order of image data"});

                                    // Generic RAW metadata.
                                    meta.exposure    = lr->other.shutter;
                                    meta.focalLength = lr->other.focal_len;

                                    if (lr->other.timestamp > 0) {
                                        QDateTime dt = QDateTime::fromSecsSinceEpoch(
                                            static_cast<qint64>(lr->other.timestamp),
                                            QTimeZone::utc());
                                        meta.dateObs = dt.toString(Qt::ISODateWithMs);
                                        meta.rawHeaders.push_back(
                                            {"DATE-OBS", meta.dateObs,
                                             "Observation date"});
                                    }
                                    if (lr->other.iso_speed > 0.0f)
                                        meta.rawHeaders.push_back(
                                            {"ISOSPEED",
                                             QString::number(
                                                 static_cast<int>(lr->other.iso_speed)),
                                             "ISO speed"});
                                    if (lr->other.shutter > 0.0f)
                                        meta.rawHeaders.push_back(
                                            {"EXPTIME",
                                             QString::number(lr->other.shutter, 'f', 6),
                                             "Exposure time [s]"});
                                    if (lr->other.aperture > 0.0f)
                                        meta.rawHeaders.push_back(
                                            {"APERTURE",
                                             QString::number(lr->other.aperture, 'f', 1),
                                             "Aperture (f-number)"});
                                    if (lr->other.focal_len > 0.0f)
                                        meta.rawHeaders.push_back(
                                            {"FOCALLEN",
                                             QString::number(
                                                 static_cast<int>(lr->other.focal_len)),
                                             "Focal length [mm]"});
                                }
                            }
                        }
                    }
                    libraw_close(lr);
                }
#endif // HAVE_LIBRAW
            }

            // -- Write the converted file ------------------------------------
            if (loaded) {
                if (!Stacking::FitsIO::write(
                        outputPath, threadBuffer, 32)) {
                    failedCount.fetchAndAddRelaxed(1);
                    std::lock_guard<std::mutex> lock(logMutex);
                    if (s_runner)
                        s_runner->logMessage(
                            QObject::tr("Failed to save %1").arg(outName),
                            "red");
                }
            } else {
                failedCount.fetchAndAddRelaxed(1);
                std::lock_guard<std::mutex> lock(logMutex);
                if (s_runner)
                    s_runner->logMessage(
                        QObject::tr("Failed to load %1").arg(file), "red");
            }

            if (s_runner)
                s_runner->progressChanged(
                    QString("Converting %1").arg(file),
                    (double)convertedCount.loadRelaxed() / files.size());

        } catch (const std::exception& e) {
            failedCount.fetchAndAddRelaxed(1);
            std::lock_guard<std::mutex> lock(logMutex);
            if (s_runner)
                s_runner->logMessage(
                    QObject::tr("Exception converting %1: %2")
                        .arg(file, e.what()),
                    "red");
        } catch (...) {
            failedCount.fetchAndAddRelaxed(1);
            std::lock_guard<std::mutex> lock(logMutex);
            if (s_runner)
                s_runner->logMessage(
                    QObject::tr("Unknown exception converting %1").arg(file),
                    "red");
        }
    });

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Conversion complete: %1 success, %2 failed")
                .arg(convertedCount.loadRelaxed() -
                     failedCount.loadRelaxed())
                .arg(failedCount.loadRelaxed()),
            "green");

    return true;
}

// ============================================================================
// seqplatesolve -- Plate solve the reference frame of a sequence
// ============================================================================

bool StackingCommands::cmdSeqPlateSolve(const ScriptCommand& cmd)
{
    if (!s_sequence || s_sequence->count() == 0) {
        if (s_runner) s_runner->logMessage(QObject::tr("seqplatesolve: No sequence loaded. Load a sequence first."), "red");
        return false;
    }

    if (cmd.args.isEmpty()) {
        if (s_runner) s_runner->logMessage(QObject::tr("seqplatesolve: sequence name argument missing."), "red");
        return false;
    }

    QString name = cmd.args[0];
    bool force = cmd.hasOption("force");
    bool nocache = cmd.hasOption("nocache");

    if (s_runner) {
        s_runner->logMessage(QObject::tr("Plate solving reference frame for sequence '%1'...").arg(name), "cyan");
        if (force) s_runner->logMessage(QObject::tr("Options: -force"), "neutral");
        if (nocache) s_runner->logMessage(QObject::tr("Options: -nocache"), "neutral");
    }

    // In TStar, registration transforms are computed via sequence plate solving
    // and caching WCS, then matching frames into RegistrationData.
    
    QSettings settings("TStar Team", "TStar");
    QString engine = settings.value("Astrometry/SolverEngine", "native").toString();
    
    auto& refEntry = s_sequence->image(0);
    ImageBuffer refImg;
    QString errorMsg;
    if (!FitsLoader::load(refEntry.filePath, refImg, &errorMsg)) {
        if (s_runner) s_runner->logMessage(QObject::tr("seqplatesolve: Cannot load reference image: ") + errorMsg, "red");
        return false;
    }
    
    const auto& meta = refImg.metadata();
    
    if (!force && FitsHeaderUtils::hasValidWCS(meta)) {
        if (s_runner) s_runner->logMessage(QObject::tr("Reference image '%1' is already plate-solved. Using existing WCS data.").arg(QFileInfo(refEntry.filePath).fileName()), "green");
        return true;
    }
    
    double raHint = meta.ra != 0.0 ? meta.ra : 0.0;
    double decHint = meta.dec != 0.0 ? meta.dec : 0.0;
    double scale = (meta.focalLength > 0 && meta.pixelSize > 0) 
        ? ((meta.pixelSize / meta.focalLength) * 206.265) : -1.0;
    
    NativeSolveResult solvedResult;
    bool didSolve = false;
    
    if (s_runner) s_runner->logMessage(QObject::tr("Using solver engine: %1").arg(engine), "neutral");
    
    QEventLoop loop;
    
    if (engine == "astap") {
        AstapSolver solver;
        QObject::connect(&solver, &AstapSolver::logMessage, [](const QString& m){
            if (s_runner) s_runner->logMessage(m, "neutral");
        });
        QObject::connect(&solver, &AstapSolver::finished, [&](const NativeSolveResult& r){
            solvedResult = r;
            didSolve = true;
            loop.quit();
        });
        solver.solve(refImg, raHint, decHint, 15.0, scale);
        loop.exec();
    } else {
        NativePlateSolver solver;
        QObject::connect(&solver, &NativePlateSolver::logMessage, [](const QString& m){
            if (s_runner) s_runner->logMessage(m, "neutral");
        });
        QObject::connect(&solver, &NativePlateSolver::finished, [&](const NativeSolveResult& r){
            solvedResult = r;
            didSolve = true;
            loop.quit();
        });
        solver.solve(refImg, raHint, decHint, 15.0, scale);
        loop.exec();
    }
    
    if (didSolve && solvedResult.success) {
        if (s_runner) s_runner->logMessage(QObject::tr("seqplatesolve: Plate solving complete and successful."), "green");
    } else {
        if (s_runner) s_runner->logMessage(QObject::tr("seqplatesolve: Plate solving failed."), "red");
    }
    
    return true;
}

// ============================================================================
// seqapplyreg -- Apply stored registration transforms
// ============================================================================

bool StackingCommands::cmdSeqApplyReg(const ScriptCommand& cmd)
{
    if (cmd.args.isEmpty())
        return false;

    QString prefix = cmd.args[0];

    if (!s_sequence || s_sequence->count() == 0) {
        if (s_runner)
            s_runner->logMessage(
                QObject::tr("seqapplyreg: No sequence loaded. Run register first."),
                "red");
        return false;
    }

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Configuring registration application for sequence: ") + prefix, "cyan");

    QString outPrefix = cmd.option("prefix", "r_");
    QString framing = cmd.option("framing", "ref").toLower();
    
    // Save framing mode globally so stack uses it.
    if (framing == "min") {
        s_framingMode = Stacking::FramingMode::Intersection;
    } else if (framing == "max" || framing == "cog") {
        s_framingMode = Stacking::FramingMode::Union;
    } else {
        s_framingMode = Stacking::FramingMode::Reference;
    }
    
    if (s_runner) s_runner->logMessage(QObject::tr("Framing mode set to: %1").arg(framing), "neutral");

    // Process image filtering logic: -filter-round
    QString filterRound = cmd.option("filter-round", "");
    if (!filterRound.isEmpty()) {
        bool isPercent = filterRound.endsWith("%");
        bool isK = filterRound.endsWith("k");
        
        float val = filterRound.left(filterRound.length() - 1).toFloat();
        if (s_runner) s_runner->logMessage(QObject::tr("Filtering sequence by roundness: %1%2").arg(val).arg(isPercent ? "%" : (isK ? "k" : "")), "neutral");

        std::vector<std::pair<double, int>> rounds;
        for (int i = 0; i < s_sequence->count(); ++i) {
            auto& img = s_sequence->image(i);
            if (img.selected && img.quality.hasMetrics) {
                rounds.push_back({img.quality.roundness, i});
            }
        }
        
        if (!rounds.empty()) {
            if (isPercent) {
                // Keep the top val percent of images with highest roundness (closer to 1.0)
                std::sort(rounds.begin(), rounds.end(), std::greater<std::pair<double, int>>());
                int keepCount = std::max(1, static_cast<int>(std::round(rounds.size() * (val / 100.0f))));
                int disabled = 0;
                for (size_t i = keepCount; i < rounds.size(); ++i) {
                    s_sequence->image(rounds[i].second).selected = false;
                    disabled++;
                }
                if (s_runner) s_runner->logMessage(QObject::tr("Roundness filter (percent): excluded %1 out of %2 frames.").arg(disabled).arg(rounds.size()), "green");
            } else if (isK) {
                // k-sigma filtering 
                std::sort(rounds.begin(), rounds.end());
                double median = rounds[rounds.size() / 2].first;
                
                // Compute MAD (Median Absolute Deviation)
                std::vector<double> absDevs;
                for (const auto& r : rounds) {
                    absDevs.push_back(std::abs(r.first - median));
                }
                std::sort(absDevs.begin(), absDevs.end());
                double mad = absDevs[absDevs.size() / 2];
                double sigma = mad * 1.4826; // Scale to standard deviation
                
                // Exclude outliers based on computed k-sigma threshold.
                double threshold = median - val * sigma;
                
                int disabled = 0;
                for (const auto& r : rounds) {
                    if (r.first < threshold) {
                        s_sequence->image(r.second).selected = false;
                        disabled++;
                    }
                }
                if (s_runner) s_runner->logMessage(QObject::tr("Roundness filter (k-sigma, %1k): threshold=%2, excluded %3 out of %4 frames.")
                                                   .arg(val).arg(threshold).arg(disabled).arg(rounds.size()), "green");
            }
        } else {
            if (s_runner) s_runner->logMessage(QObject::tr("No roundness data available to perform filtering. Run register first to calculate metrics."), "red");
        }
    }

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Note: TStar applies registration and framing dynamically during 'stack'."),
            "yellow");

    return true;
}

// ============================================================================
// rotate -- Rotate the current image
// ============================================================================

bool StackingCommands::cmdRotate(const ScriptCommand& cmd)
{
    if (!s_currentImage)
        return false;

    float angle = cmd.args[0].toFloat();

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Rotating image by %1 degrees").arg(angle), "neutral");

    s_currentImage->rotate(angle);
    s_currentImage->setModified(true);
    return true;
}

// ============================================================================
// resample -- Resize the current image
// ============================================================================

bool StackingCommands::cmdResample(const ScriptCommand& cmd)
{
    if (!s_currentImage)
        return false;

    float factor = 1.0f;
    if (cmd.args.size() > 0)
        factor = cmd.args[0].toFloat();

    int newW = static_cast<int>(s_currentImage->width()  * factor);
    int newH = static_cast<int>(s_currentImage->height() * factor);

    if (cmd.hasOption("width"))  newW = cmd.option("width").toInt();
    if (cmd.hasOption("height")) newH = cmd.option("height").toInt();

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Resampling image to %1x%2").arg(newW).arg(newH),
            "neutral");

    // Use Lanczos interpolation via OpenCV.
    cv::Mat mat(
        s_currentImage->height(), s_currentImage->width(),
        CV_MAKETYPE(CV_32F, s_currentImage->channels()),
        s_currentImage->data().data());

    cv::Mat resized;
    cv::resize(mat, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LANCZOS4);

    s_currentImage->resize(newW, newH, s_currentImage->channels());
    std::memcpy(
        s_currentImage->data().data(), resized.data,
        (size_t)newW * newH * s_currentImage->channels() * sizeof(float));

    s_currentImage->setModified(true);
    return true;
}

// ============================================================================
// update_key -- Update a FITS header keyword
// ============================================================================

bool StackingCommands::cmdUpdateKey(const ScriptCommand& cmd)
{
    if (!s_currentImage)
        return false;

    QString key     = cmd.args[0].toUpper();
    QString value   = cmd.args[1];
    QString comment = (cmd.args.size() > 2) ? cmd.args[2] : "";

    auto& meta = s_currentImage->metadata();
    bool  found = false;

    for (auto& card : meta.rawHeaders) {
        if (card.key.toUpper() == key) {
            card.value = value;
            if (!comment.isEmpty())
                card.comment = comment;
            found = true;
            break;
        }
    }

    if (!found)
        meta.rawHeaders.push_back({key, value, comment});

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Updated FITS header: %1 = %2").arg(key, value),
            "green");
    return true;
}

// ============================================================================
// crop -- Crop the current image
// ============================================================================

bool StackingCommands::cmdCrop(const ScriptCommand& cmd)
{
    if (!s_currentImage)
        return false;

    if (cmd.args.size() < 4)
        return false; // No interactive crop support; all four args required.

    int x = cmd.args[0].toInt();
    int y = cmd.args[1].toInt();
    int w = cmd.args[2].toInt();
    int h = cmd.args[3].toInt();

    if (s_runner)
        s_runner->logMessage(
            QObject::tr("Cropping to %1,%2 size %3x%4")
                .arg(x).arg(y).arg(w).arg(h),
            "neutral");

    s_currentImage->crop(x, y, w, h);
    s_currentImage->setModified(true);
    return true;
}

// ============================================================================
// stat -- Log basic image statistics
// ============================================================================

bool StackingCommands::cmdStat(const ScriptCommand& cmd)
{
    Q_UNUSED(cmd);
    if (!s_currentImage)
        return false;

    float       minVal = 1.0f;
    float       maxVal = 0.0f;
    float       sum    = 0.0f;
    const auto& d      = s_currentImage->data();

    for (float v : d) {
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
        sum += v;
    }
    float mean = sum / d.size();

    if (s_runner) {
        s_runner->logMessage(
            QObject::tr("Image Stats: Min=%1, Max=%2, Mean=%3")
                .arg(minVal).arg(maxVal).arg(mean),
            "white");
        s_runner->setVariable("STAT_MIN",  QString::number(minVal));
        s_runner->setVariable("STAT_MAX",  QString::number(maxVal));
        s_runner->setVariable("STAT_MEAN", QString::number(mean));
    }

    return true;
}

// ============================================================================
// thresh / threshlo / threshhi -- Pixel thresholding
// ============================================================================

bool StackingCommands::cmdThreshold(const ScriptCommand& cmd)
{
    if (!s_currentImage)
        return false;

    float lo = 0.0f;
    float hi = 1.0f;

    if (cmd.name == "thresh" && cmd.args.size() >= 2) {
        lo = cmd.args[0].toFloat();
        hi = cmd.args[1].toFloat();
    } else if (cmd.name == "threshlo") {
        lo = cmd.args[0].toFloat();
    } else if (cmd.name == "threshhi") {
        hi = cmd.args[0].toFloat();
    }

    auto& d = s_currentImage->data();
    for (float& v : d) {
        if (v < lo) v = 0.0f;
        if (v > hi) v = 1.0f;
    }

    s_currentImage->setModified(true);
    return true;
}

// ============================================================================
// offset / fmul / neg -- Simple arithmetic on all pixels
// ============================================================================

bool StackingCommands::cmdMath(const ScriptCommand& cmd)
{
    if (!s_currentImage)
        return false;

    float val = 0.0f;
    if (!cmd.args.isEmpty())
        val = cmd.args[0].toFloat();

    auto& d = s_currentImage->data();

    if (cmd.name == "offset") {
        for (float& v : d) v += val;
    } else if (cmd.name == "fmul") {
        for (float& v : d) v *= val;
    } else if (cmd.name == "neg") {
        for (float& v : d) v = 1.0f - v;
    }

    s_currentImage->setModified(true);
    return true;
}

// ========================================================================
// cmdMosaic (mosaic union)
// ========================================================================
bool StackingCommands::cmdMosaic(const ScriptCommand& cmd)
{
    ScriptCommand modifiedCmd = cmd;
    modifiedCmd.name = "stack";
    modifiedCmd.options["method"] = "mosaic";
    
    // Default to 'mosaic' method in positional argument
    if (modifiedCmd.args.size() >= 2) {
        modifiedCmd.args[1] = "mosaic";
    } else if (modifiedCmd.args.size() == 1) {
        modifiedCmd.args.push_back("mosaic");
    } else if (modifiedCmd.args.isEmpty()) {
        modifiedCmd.args.push_back("");      // prefix
        modifiedCmd.args.push_back("mosaic"); // method
    }
    
    if (s_runner) {
        s_runner->logMessage(QObject::tr("Starting Mosaic Computation (overlap blending)..."), "green");
    }

    return cmdStack(modifiedCmd);
}

} // namespace Scripting


