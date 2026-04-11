#include "AstapSolver.h"
#include "../io/SimpleTiffWriter.h"

#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <limits>

// ============================================================================
// Construction / Destruction
// ============================================================================

AstapSolver::AstapSolver(QObject* parent) : QObject(parent)
{
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, [this]() { cancelSolve(); });
    }
}

AstapSolver::~AstapSolver()
{
    cancelSolve();
}

// ============================================================================
// Catalog detection
// ============================================================================

bool AstapSolver::containsAstapCatalogs(const QString& path)
{
    QDir dir(path);
    if (!dir.exists()) return false;

    // ASTAP catalogs use a variety of extensions (.dat, .290, numeric sky-zone
    // suffixes like .1476) and filename prefixes:
    //   d = Gaia,  h = Hyghen,  g = Hyperleda,  q = Quasars,
    //   tyc = Tycho,  w = UCAC4
    QStringList filters;
    filters << "d*.*" << "h*.*" << "g*.*" << "q*.*" << "tyc*.*" << "w*.*"
            << "*.290" << "*.50" << "*.g17" << "*.g18" << "*.h17" << "*.h18"
            << "*.290c" << "*.50c" << "*.opt" << "*.dat"
            << "d50_*" << "d05_*" << "h17_*" << "h18_*" << "w08_*";

    auto checkInDir = [&](const QDir& d) -> bool {
        QStringList files = d.entryList(filters, QDir::Files);
        for (const auto& f : files) {
            QFileInfo info(f);
            QString ext = info.suffix();

            // Recognized standard extensions
            if (ext == "dat" || ext == "290" || ext == "50" || ext == "opt" ||
                ext.endsWith("c") || ext.startsWith("g") || ext.startsWith("h"))
                return true;

            // Numeric sky-zone extensions (e.g. .1476)
            if (!ext.isEmpty()) {
                bool ok = false;
                ext.toInt(&ok);
                if (ok) return true;
            }

            // Known ASTAP filename prefixes
            if (f.startsWith("d") || f.startsWith("h") || f.startsWith("g") ||
                f.startsWith("q") || f.startsWith("tyc") || f.startsWith("w"))
                return true;
        }
        return false;
    };

    // Check the directory itself
    if (checkInDir(dir)) return true;

    // Check immediate subdirectories (one level deep)
    QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subDirs) {
        if (checkInDir(QDir(dir.absoluteFilePath(sub)))) return true;
    }

    return false;
}

// ============================================================================
// Database path resolution
// ============================================================================

QString AstapSolver::getAstapDatabasePath(const QString& overridePath)
{
    QSettings settings;
    QString customDb = overridePath.isEmpty()
        ? settings.value("paths/astap_database").toString().trimmed()
        : overridePath.trimmed();

    // Helper: validates a path and resolves macOS .app bundle internals
    auto validateAndResolve = [&](const QString& path) -> QString {
        if (path.isEmpty()) return "";
        QDir dir(path);
        if (!dir.exists()) return "";

#ifdef Q_OS_MAC
        // If the path points to a .app bundle, probe common internal locations
        if (path.endsWith(".app") || path.endsWith(".app/")) {
            QString base = path;
            if (base.endsWith("/")) base.chop(1);

            QStringList internalPotentials;
            internalPotentials << base + "/Contents/Resources/Databases"
                               << base + "/Contents/MacOS/Databases"
                               << base + "/Contents/Resources"
                               << base + "/Contents/MacOS";

            for (const auto& p : internalPotentials) {
                if (containsAstapCatalogs(p)) return QDir(p).absolutePath();
            }
        }
#endif

        if (containsAstapCatalogs(path)) return QDir(path).absolutePath();

        // Try a "Databases" subfolder
        QString dbSub = path + "/Databases";
        if (containsAstapCatalogs(dbSub)) return QDir(dbSub).absolutePath();

        return "";
    };

    // --- Strategy 1: User-specified or settings-stored custom path -----------
    QString resolvedCustom = validateAndResolve(customDb);
    if (!resolvedCustom.isEmpty()) return resolvedCustom;

    // --- Strategy 2: Alongside the detected ASTAP executable -----------------
    QString astapExec = getAstapExecutable();
    if (!astapExec.isEmpty()) {
        QFileInfo exeInfo(astapExec);
        QString astapDir = exeInfo.absolutePath();

        if (containsAstapCatalogs(astapDir))
            return QDir(astapDir).absolutePath();
        if (containsAstapCatalogs(astapDir + "/Databases"))
            return QDir(astapDir + "/Databases").absolutePath();

#ifdef Q_OS_MAC
        // If the executable resides deep in a bundle, check bundle-relative paths
        if (astapDir.endsWith("/Contents/MacOS") ||
            astapDir.endsWith("/Contents/Resources/deps"))
        {
            QString contentsDir = astapDir.contains("/Resources/deps")
                ? QDir(astapDir + "/../..").absolutePath()
                : QDir(astapDir + "/..").absolutePath();

            QStringList bundlePotentials;
            bundlePotentials << contentsDir + "/Resources/Databases"
                             << contentsDir + "/Resources/deps/Databases"
                             << contentsDir + "/Resources";
            for (const auto& p : bundlePotentials) {
                if (containsAstapCatalogs(p)) return QDir(p).absolutePath();
            }
        }
#endif
    }

    // --- Strategy 3: Well-known default installation locations ----------------
    QStringList potentials;

#ifdef Q_OS_WIN
    potentials << "C:/Program Files/astap"
               << "C:/Program Files (x86)/astap"
               << "C:/ASTAP"
               << QStandardPaths::writableLocation(
                      QStandardPaths::DocumentsLocation) + "/ASTAP"
               << QCoreApplication::applicationDirPath() + "/deps"
               << QCoreApplication::applicationDirPath();
#elif defined(Q_OS_MAC)
    QString appDir = QCoreApplication::applicationDirPath(); // Contents/MacOS
    potentials << appDir + "/../Resources/deps/Databases"
               << appDir + "/../Resources/deps"
               << appDir + "/deps"
               << "/usr/local/opt/astap/Databases"
               << "/usr/local/opt/astap"
               << "/opt/homebrew/opt/astap/Databases"
               << "/opt/homebrew/opt/astap"
               << "/Applications/ASTAP.app"
               << QDir::homePath() + "/Library/Application Support/ASTAP/Databases"
               << QDir::homePath() + "/Library/Application Support/ASTAP";
#endif

    for (const QString& p : potentials) {
        QString res = validateAndResolve(p);
        if (!res.isEmpty()) return res;
    }

    return "";
}

// ============================================================================
// Executable path resolution
// ============================================================================

QString AstapSolver::getAstapExecutable()
{
    QSettings settings;

#ifdef Q_OS_WIN
    // Bundled binaries shipped alongside TStar
    QString bundledCli = QCoreApplication::applicationDirPath() + "/deps/astap_cli.exe";
    if (QFile::exists(bundledCli)) return bundledCli;

    QString bundled = QCoreApplication::applicationDirPath() + "/deps/astap.exe";
    if (QFile::exists(bundled)) return bundled;

    // Standard system installation paths
    QString sysPaths[] = {
        "C:/Program Files/astap/astap_cli.exe",
        "C:/Program Files (x86)/astap/astap_cli.exe",
        "C:/Program Files/astap/astap.exe",
        "C:/Program Files (x86)/astap/astap.exe"
    };
    for (const auto& p : sysPaths) {
        if (QFile::exists(p)) return p;
    }

#elif defined(Q_OS_MAC)
    // Bundled binaries inside the TStar.app bundle
    const QString appDir = QCoreApplication::applicationDirPath();
    QString bundledCandidates[] = {
        appDir + "/../Resources/deps/astap",
        appDir + "/../Resources/deps/astap_cli",
        appDir + "/../Resources/astap",
        appDir + "/astap"
    };
    for (const auto& p : bundledCandidates) {
        if (QFile::exists(p)) return p;
    }

    // System-wide ASTAP.app
    if (QFile::exists("/Applications/ASTAP.app/Contents/MacOS/astap"))
        return "/Applications/ASTAP.app/Contents/MacOS/astap";

    // Homebrew installations
    if (QFile::exists("/usr/local/opt/astap/bin/astap"))
        return "/usr/local/opt/astap/bin/astap";
    if (QFile::exists("/usr/local/opt/astap/astap"))
        return "/usr/local/opt/astap/astap";
    if (QFile::exists("/usr/local/bin/astap"))
        return "/usr/local/bin/astap";
    if (QFile::exists("/opt/homebrew/bin/astap"))
        return "/opt/homebrew/bin/astap";

#else
    // Linux default
    if (QFile::exists("/opt/astap/astap")) return "/opt/astap/astap";
#endif

    // Legacy / manual override path (kept for backwards compatibility)
    QString customPath = settings.value("paths/astap").toString();
    if (!customPath.isEmpty()) {
#ifdef Q_OS_MAC
        // Resolve .app bundles to the internal binary
        if (customPath.endsWith(".app") || customPath.endsWith(".app/")) {
            QString internal = customPath;
            if (internal.endsWith("/")) internal.chop(1);
            internal += "/Contents/MacOS/astap";
            if (QFile::exists(internal)) return internal;
        }
#endif
        QFileInfo customInfo(customPath);

        // Only accept regular files as executables (not directories)
        if (!customInfo.exists() || !customInfo.isFile())
            return "";

        // Prefer the CLI variant when the GUI binary is configured
        if (customInfo.fileName().compare("astap.exe", Qt::CaseInsensitive) == 0) {
            QString cliSibling = customInfo.absolutePath() + "/astap_cli.exe";
            if (QFile::exists(cliSibling))
                return cliSibling;
        }
        return customPath;
    }

    return "";
}

// ============================================================================
// Cancellation
// ============================================================================

void AstapSolver::cancelSolve()
{
    m_cancelRequested.store(true);
}

// ============================================================================
// Process termination (graceful -> kill -> platform-specific forced kill)
// ============================================================================

bool AstapSolver::terminateAstapProcess(QProcess& process, int terminateWaitMs)
{
    if (process.state() == QProcess::NotRunning)
        return true;

    process.terminate();
    if (process.waitForFinished(terminateWaitMs))
        return true;

    process.kill();
    if (process.waitForFinished(terminateWaitMs))
        return true;

#ifdef Q_OS_WIN
    const qint64 pid = process.processId();
    if (pid > 0) {
        QProcess::execute("taskkill",
                          QStringList() << "/PID" << QString::number(pid)
                                        << "/T" << "/F");
    }
#endif

    return process.state() == QProcess::NotRunning;
}

// ============================================================================
// solve --- Main entry point
// ============================================================================

void AstapSolver::solve(const ImageBuffer& image,
                        double raHint, double decHint,
                        double radiusDeg, double pixelScale)
{
    emit logMessage(tr("Starting ASTAP Solver. Center: %1, %2 Radius: %3 deg")
                        .arg(raHint).arg(decHint).arg(radiusDeg));

    QString astapExec = getAstapExecutable();
    if (astapExec.isEmpty()) {
        NativeSolveResult res;
        res.errorMsg = tr("ASTAP executable not found.");
        emit finished(res);
        return;
    }

    m_cancelRequested.store(false);

    QPointer<AstapSolver> safeThis(this);
    const QString astapPath = astapExec;

    (void)QtConcurrent::run([safeThis, image, raHint, decHint, radiusDeg, pixelScale, astapPath]() {
        if (!safeThis) return;

        NativeSolveResult res;

        // -- Temporary file paths -----------------------------------------
        QString tempDir  = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString tempTiff = tempDir + "/tstar_astap_solve_" + uniqueId + ".tif";
        QString tempIni  = tempDir + "/tstar_astap_solve_" + uniqueId + ".ini";
        QString tempWcs  = tempDir + "/tstar_astap_solve_" + uniqueId + ".wcs";

        // Cleanup previous failed/stale temp files
        QFile::remove(tempTiff);
        QFile::remove(tempIni);
        QFile::remove(tempWcs);

        auto emitLog = [&](const QString& msg) {
            if (safeThis) emit safeThis->logMessage(msg);
        };

        emitLog(tr("Saving temporary image for ASTAP..."));

        // -- Validate image buffer ----------------------------------------
        const int width    = image.width();
        const int height   = image.height();
        const int channels = image.channels();

        if (width <= 0 || height <= 0 || channels <= 0 || image.data().empty()) {
            res.errorMsg = tr("Invalid image buffer for ASTAP solve.");
            if (safeThis) emit safeThis->finished(res);
            return;
        }

        // -- Convert to normalized single-channel luminance ---------------
        std::vector<float> mono(static_cast<size_t>(width) *
                                static_cast<size_t>(height));
        const float* src = image.data().data();

        float minV = std::numeric_limits<float>::max();
        float maxV = std::numeric_limits<float>::lowest();

        for (size_t i = 0; i < mono.size(); ++i) {
            float v = 0.0f;
            if (channels == 1) {
                v = src[i];
            } else {
                const size_t base = i * static_cast<size_t>(channels);
                int sampleCount = std::min(channels, 3);
                for (int c = 0; c < sampleCount; ++c)
                    v += src[base + static_cast<size_t>(c)];
                v /= static_cast<float>(sampleCount);
            }

            if (!std::isfinite(v)) v = 0.0f;
            v = std::clamp(v, 0.0f, 1.0f);

            mono[i] = v;
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }

        const float range = maxV - minV;
        if (range > 1e-6f) {
            for (float& v : mono)
                v = (v - minV) / range;
        }

        // -- Write temporary TIFF -----------------------------------------
        if (!SimpleTiffWriter::write(tempTiff, width, height, 1,
                                     SimpleTiffWriter::Format_uint16, mono))
        {
            res.errorMsg = tr("Failed to save temporary image.");
            if (safeThis) emit safeThis->finished(res);
            return;
        }

        // -- Calculate FOV and coordinate parameters ----------------------
        double fovDeg = (std::min(width, height) * pixelScale) / 3600.0;
        if (fovDeg > 12.0) {
            double fovReduced = fovDeg * 0.92;
            emitLog(tr("Large FOV detected: reducing from %1 deg to %2 deg for search stability")
                        .arg(fovDeg, 0, 'f', 2).arg(fovReduced, 0, 'f', 2));
            fovDeg = fovReduced;
        }

        double raHours = raHint / 15.0;
        double spdDeg  = decHint + 90.0;

        emitLog(tr("Calculated FOV (Height): %1 degrees (dims=%2x%3, scale=%4 arcsec/px)")
                    .arg(fovDeg, 0, 'f', 4).arg(width).arg(height)
                    .arg(pixelScale, 0, 'f', 3));

        // -- Build command-line arguments ---------------------------------
        QStringList commonArgs;
        commonArgs << "-f" << tempTiff << "-progress" << "-z" << "0" << "-s" << "500";

        QString dbPath = safeThis->getAstapDatabasePath();
        if (!dbPath.isEmpty()) {
            commonArgs << "-d" << dbPath;
            emitLog(tr("ASTAP Database found: %1").arg(dbPath));
        } else {
            emitLog(tr("Note: ASTAP database not explicitly located by TStar. ASTAP will use its internal search paths."));
        }

        QStringList hintedArgs;
        bool hasHint = (raHint != 0.0 || decHint != 0.0);
        if (hasHint) {
            hintedArgs << "-ra"  << QString::number(raHours, 'f', 6)
                       << "-spd" << QString::number(spdDeg,  'f', 6)
                       << "-r"   << QString::number(radiusDeg, 'f', 2);
        }

        QStringList scaleArgs;
        if (fovDeg > 0.0) scaleArgs << "-fov" << QString::number(fovDeg, 'f', 4);

        QSettings settings;
        QString extra = settings.value("paths/astap_extra").toString();
        QStringList extraArgs;
        if (!extra.isEmpty()) extraArgs = extra.split(' ', Qt::SkipEmptyParts);

        // -- Parsing lambda -----------------------------------------------
        auto parseOutputs = [&]() -> bool {
            if (QFile::exists(tempIni)) {
                if (safeThis && safeThis->parseAstapIni(tempIni, height, res)) {
                    res.success = true;
                    return true;
                }
                QFile f(tempIni);
                if (f.open(QIODevice::ReadOnly)) {
                    QString content = QString::fromUtf8(f.readAll());
                    if (content.contains("PLTSOLVD=F"))
                        res.errorMsg = tr("ASTAP was unable to solve the image.");
                    else
                        res.errorMsg = tr("Failed to parse ASTAP output files.");
                }
                return false;
            }
            if (QFile::exists(tempWcs)) {
                if (safeThis && safeThis->parseAstapWCS(tempWcs, height, res)) {
                    res.success = true;
                    return true;
                }
                res.errorMsg = tr("Failed to parse ASTAP WCS file.");
                return false;
            }
            res.errorMsg = tr("ASTAP failed to solve the image.");
            return false;
        };

        // -- Attempt lambda -----------------------------------------------
        auto executeAttempt = [&](const QString& label, const QStringList& args) -> bool {
            QFile::remove(tempIni);
            QFile::remove(tempWcs);
            emitLog(tr("ASTAP attempt: %1").arg(label));

            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(astapPath, args);

            if (!p.waitForStarted(10000)) {
                res.errorMsg = tr("Failed to start ASTAP process.");
                return false;
            }

            if (safeThis) {
                QMutexLocker locker(&safeThis->m_processMutex);
                safeThis->m_runningProcess = &p;
            }

            auto clearRunningProcess = [&]() {
                if (safeThis) {
                    QMutexLocker locker(&safeThis->m_processMutex);
                    if (safeThis->m_runningProcess == &p)
                        safeThis->m_runningProcess = nullptr;
                }
            };

            auto flushOutput = [&]() {
                QString output = p.readAllStandardOutput();
                const QStringList lines = output.split('\n');
                for (const QString& line : lines) {
                    QString t = line.trimmed();
                    if (!t.isEmpty()) emitLog(t);
                }
            };

            QElapsedTimer timer;
            timer.start();
            while (!p.waitForFinished(500)) {
                flushOutput();
                if (!safeThis || safeThis->m_cancelRequested.load()) {
                    emitLog(tr("ASTAP solve cancelled."));
                    if (safeThis) safeThis->terminateAstapProcess(p, 2000);
                    clearRunningProcess();
                    res.errorMsg = tr("ASTAP solve cancelled.");
                    return false;
                }
                if (timer.elapsed() > 120000) {
                    emitLog(tr("ASTAP timed out."));
                    if (safeThis) safeThis->terminateAstapProcess(p, 2000);
                    clearRunningProcess();
                    res.errorMsg = tr("ASTAP timed out.");
                    return false;
                }
            }
            flushOutput();
            clearRunningProcess();
            return parseOutputs();
        };

        // -- Sequence -----------------------------------------------------
        std::vector<QPair<QString, QStringList>> attempts;
        if (hasHint) {
            attempts.push_back({tr("hinted solve (with scale)"), commonArgs + hintedArgs + scaleArgs + extraArgs});
            if (!scaleArgs.isEmpty())
                attempts.push_back({tr("hinted solve (no scale)"), commonArgs + hintedArgs + extraArgs});
            attempts.push_back({tr("blind solve"), commonArgs + QStringList{"-r", "180.0"} + extraArgs});
        } else {
            attempts.push_back({tr("blind solve"), commonArgs + QStringList{"-r", "180.0"} + scaleArgs + extraArgs});
            if (!scaleArgs.isEmpty())
                attempts.push_back({tr("blind solve (no scale)"), commonArgs + QStringList{"-r", "180.0"} + extraArgs});
        }

        for (const auto& att : attempts) {
            if (executeAttempt(att.first, att.second)) break;
        }

        // -- Cleanup and Finish -------------------------------------------
        QFile::remove(tempTiff);
        QFile::remove(tempIni);
        QFile::remove(tempWcs);

        if (safeThis) emit safeThis->finished(res);
    });
}

// ============================================================================
// FITS keyword parser (file-local helper)
// ============================================================================

static double parseFitsValue(const QString& content, const QString& key)
{
    // FITS header format: "KEY     =              VALUE / comment"
    QRegularExpression re(
        key + "\\s*=\\s*([-+]?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)");
    auto match = re.match(content);
    if (match.hasMatch())
        return match.captured(1).toDouble();
    return 0.0;
}

// ============================================================================
// ASTAP output parsers
// ============================================================================

bool AstapSolver::parseAstapWCS(const QString& wcsFile, int imageHeight,
                                NativeSolveResult& res)
{
    QFile file(wcsFile);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QString content = QString::fromUtf8(file.readAll());

    res.crval1 = parseFitsValue(content, "CRVAL1");
    res.crval2 = parseFitsValue(content, "CRVAL2");
    res.crpix1 = parseFitsValue(content, "CRPIX1");

    // ASTAP solved a TIFF with bottom-up FITS Y convention. Convert back
    // to TStar's top-down raw memory Y coordinate system.
    res.crpix2 = imageHeight - parseFitsValue(content, "CRPIX2") + 1.0;

    res.cd[0][0] =  parseFitsValue(content, "CD1_1");
    res.cd[0][1] = -parseFitsValue(content, "CD1_2");  // dRA/dY inverted
    res.cd[1][0] =  parseFitsValue(content, "CD2_1");
    res.cd[1][1] = -parseFitsValue(content, "CD2_2");  // dDec/dY inverted

    return res.cd[0][0] != 0.0;
}

bool AstapSolver::parseAstapIni(const QString& iniFile, int imageHeight,
                                NativeSolveResult& res)
{
    QFile file(iniFile);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QString content = QString::fromUtf8(file.readAll());

    // Check explicit solve-status flags
    if (content.contains("PLTSOLVD=F")) return false;

    // If solve status is absent, attempt to parse keywords regardless

    res.crval1 = parseFitsValue(content, "CRVAL1");
    res.crval2 = parseFitsValue(content, "CRVAL2");
    res.crpix1 = parseFitsValue(content, "CRPIX1");

    // Y-axis reflection for ASTAP raw-TIFF coordinate convention
    res.crpix2 = imageHeight - parseFitsValue(content, "CRPIX2") + 1.0;

    res.cd[0][0] =  parseFitsValue(content, "CD1_1");
    res.cd[0][1] = -parseFitsValue(content, "CD1_2");
    res.cd[1][0] =  parseFitsValue(content, "CD2_1");
    res.cd[1][1] = -parseFitsValue(content, "CD2_2");

    return res.cd[0][0] != 0.0;
}