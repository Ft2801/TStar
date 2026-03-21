#include "AstapSolver.h"
#include "../io/SimpleTiffWriter.h"
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <limits>

AstapSolver::AstapSolver(QObject* parent) : QObject(parent) {
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
            cancelSolve();
        });
    }
}

AstapSolver::~AstapSolver() {
    cancelSolve();
}

// Helper to check if a directory contains ASTAP catalog files
bool AstapSolver::containsAstapCatalogs(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) return false;
    
    // ASTAP catalogs use various extensions: .dat, .290, or numeric sky-zone suffixes (e.g. .1476)
    // Common prefixes: d (Gaia), h (Hyghen), g (Hyperleda), q (Quasars), tyc (Tycho), w (UCAC4)
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
            
            // Standard extensions
            if (ext == "dat" || ext == "290" || ext == "50" || ext == "opt" || 
                ext.endsWith("c") || ext.startsWith("g") || ext.startsWith("h")) return true;
            
            // Numeric sky-zone extensions (like .1476)
            if (!ext.isEmpty()) {
                bool ok = false;
                ext.toInt(&ok);
                if (ok) return true;
            }

            // Specific prefixes known to be ASTAP
            if (f.startsWith("d") || f.startsWith("h") || f.startsWith("g") || 
                f.startsWith("q") || f.startsWith("tyc") || f.startsWith("w")) {
                return true;
            }
        }
        return false;
    };

    // 1. Check current directory
    if (checkInDir(dir)) return true;
    
    // 2. Check all subdirectories (one level deep)
    QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subDirs) {
        if (checkInDir(QDir(dir.absoluteFilePath(sub)))) return true;
    }
    
    return false;
}

QString AstapSolver::getAstapDatabasePath(const QString& overridePath) {
    QSettings settings;
    QString customDb = overridePath.isEmpty() ? 
        settings.value("paths/astap_database").toString().trimmed() : overridePath.trimmed();
    
    // Helper to validate and optionally resolve .app bundles
    auto validateAndResolve = [&](const QString& path) -> QString {
        if (path.isEmpty()) return "";
        QDir dir(path);
        if (!dir.exists()) return "";

#ifdef Q_OS_MAC
        // On macOS, if the path is a .app bundle, check internal common locations
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
        
        // Check "Databases" subfolder
        QString dbSub = path + "/Databases";
        if (containsAstapCatalogs(dbSub)) return QDir(dbSub).absolutePath();

        return "";
    };

    // 1. Try custom database path first, but validate/resolve it
    QString resolvedCustom = validateAndResolve(customDb);
    if (!resolvedCustom.isEmpty()) return resolvedCustom;

    // 2. Try to find catalogs alongside the detected executable
    QString astapExec = getAstapExecutable();
    if (!astapExec.isEmpty()) {
        QFileInfo exeInfo(astapExec);
        QString astapDir = exeInfo.absolutePath();
        
        if (containsAstapCatalogs(astapDir)) return QDir(astapDir).absolutePath();
        if (containsAstapCatalogs(astapDir + "/Databases")) return QDir(astapDir + "/Databases").absolutePath();

#ifdef Q_OS_MAC
        // If the executable is deep in a bundle, also check bundle-relative Resources
        if (astapDir.endsWith("/Contents/MacOS") || astapDir.endsWith("/Contents/Resources/deps")) {
            QString contentsDir = astapDir.contains("/Resources/deps") ? 
                QDir(astapDir + "/../..").absolutePath() : QDir(astapDir + "/..").absolutePath();
            
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

    // 3. Search multiple potential default locations (Synchronized with SettingsDialog)
    QStringList potentials;
#ifdef Q_OS_WIN
    potentials << "C:/Program Files/astap" 
               << "C:/Program Files (x86)/astap" 
               << "C:/ASTAP"
               << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/ASTAP"
               << QCoreApplication::applicationDirPath() + "/deps"
               << QCoreApplication::applicationDirPath();
#elif defined(Q_OS_MAC)
    QString appDir = QCoreApplication::applicationDirPath(); // Contents/MacOS
    potentials << appDir + "/../Resources/deps/Databases"
               << appDir + "/../Resources/deps"
               << appDir + "/deps" // In case it's flat
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

QString AstapSolver::getAstapExecutable() {
    QSettings settings;
#ifdef Q_OS_WIN
    // Check bundled
    QString bundledCli = QCoreApplication::applicationDirPath() + "/deps/astap_cli.exe";
    if (QFile::exists(bundledCli)) return bundledCli;
    QString bundled = QCoreApplication::applicationDirPath() + "/deps/astap.exe";
    if (QFile::exists(bundled)) return bundled;
    // Check default system
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
    // Bundle locations in TStar.app
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

    // System path
    if (QFile::exists("/Applications/ASTAP.app/Contents/MacOS/astap")) {
        return "/Applications/ASTAP.app/Contents/MacOS/astap";
    }
    // Homebrew formula install (brew install astap)
    if (QFile::exists("/usr/local/opt/astap/bin/astap")) return "/usr/local/opt/astap/bin/astap";
    if (QFile::exists("/usr/local/opt/astap/astap")) return "/usr/local/opt/astap/astap";
    if (QFile::exists("/usr/local/bin/astap")) return "/usr/local/bin/astap";
    if (QFile::exists("/opt/homebrew/bin/astap")) return "/opt/homebrew/bin/astap";
#else
    if (QFile::exists("/opt/astap/astap")) return "/opt/astap/astap";
#endif

    // Legacy/manual override path: keep as fallback for compatibility.
    QString customPath = settings.value("paths/astap").toString();
    if (!customPath.isEmpty()) {
        #ifdef Q_OS_MAC
        // If user selected the .app bundle, convert to internal binary when possible.
        if (customPath.endsWith(".app") || customPath.endsWith(".app/")) {
            QString internal = customPath;
            if (internal.endsWith("/")) internal.chop(1);
            internal += "/Contents/MacOS/astap";
            if (QFile::exists(internal)) return internal;
        }
        #endif

        QFileInfo customInfo(customPath);

        // Accept only files as executables. Directories are not valid executable paths.
        if (!customInfo.exists() || !customInfo.isFile()) {
            return "";
        }

        // Prefer CLI binary when a GUI binary is configured.
        if (customInfo.fileName().compare("astap.exe", Qt::CaseInsensitive) == 0) {
            QString cliSibling = customInfo.absolutePath() + "/astap_cli.exe";
            if (QFile::exists(cliSibling)) {
                return cliSibling;
            }
        }
        return customPath;
    }

    return "";
}

void AstapSolver::cancelSolve() {
    m_cancelRequested.store(true);
}

bool AstapSolver::terminateAstapProcess(QProcess& process, int terminateWaitMs) {
    if (process.state() == QProcess::NotRunning) {
        return true;
    }

    process.terminate();
    if (process.waitForFinished(terminateWaitMs)) {
        return true;
    }

    process.kill();
    if (process.waitForFinished(terminateWaitMs)) {
        return true;
    }

#ifdef Q_OS_WIN
    const qint64 pid = process.processId();
    if (pid > 0) {
        QProcess::execute("taskkill", QStringList() << "/PID" << QString::number(pid) << "/T" << "/F");
    }
#endif

    return process.state() == QProcess::NotRunning;
}

void AstapSolver::solve(const ImageBuffer& image, double raHint, double decHint, double radiusDeg, double pixelScale) {
    emit logMessage(tr("Starting ASTAP Solver. Center: %1, %2 Radius: %3 deg").arg(raHint).arg(decHint).arg(radiusDeg));
    
    QString astapExec = getAstapExecutable();
    if (astapExec.isEmpty()) {
        NativeSolveResult res;
        res.errorMsg = tr("ASTAP executable not found.");
        emit finished(res);
        return;
    }

    m_cancelRequested.store(false);

    QThreadPool::globalInstance()->start([this, image, raHint, decHint, radiusDeg, pixelScale, astapExec]() {
        NativeSolveResult res;
        
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempTiff = tempDir + "/tstar_astap_solve.tif";
        QString tempIni = tempDir + "/tstar_astap_solve.ini";
        QString tempWcs = tempDir + "/tstar_astap_solve.wcs";
        
        QFile::remove(tempTiff);
        QFile::remove(tempIni);
        QFile::remove(tempWcs);
        
        emit logMessage(tr("Saving temporary image for ASTAP..."));
        
        const int width = image.width();
        const int height = image.height();
        const int channels = image.channels();
        if (width <= 0 || height <= 0 || channels <= 0 || image.data().empty()) {
            res.errorMsg = tr("Invalid image buffer for ASTAP solve.");
            QMetaObject::invokeMethod(this, "finished", Qt::QueuedConnection, Q_ARG(NativeSolveResult, res));
            return;
        }

        // ASTAP is most reliable with a normalized single-channel luminance image.
        std::vector<float> mono(static_cast<size_t>(width) * static_cast<size_t>(height));
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
                for (int c = 0; c < sampleCount; ++c) {
                    v += src[base + static_cast<size_t>(c)];
                }
                v /= static_cast<float>(sampleCount);
            }

            if (!std::isfinite(v)) {
                v = 0.0f;
            }

            v = std::clamp(v, 0.0f, 1.0f);
            mono[i] = v;
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }

        const float range = maxV - minV;
        if (range > 1e-6f) {
            for (float& v : mono) {
                v = (v - minV) / range;
            }
        }

        if (!SimpleTiffWriter::write(tempTiff, width, height, 1, SimpleTiffWriter::Format_uint16, mono)) {
            res.errorMsg = tr("Failed to save temporary image.");
            QMetaObject::invokeMethod(this, "finished", Qt::QueuedConnection, Q_ARG(NativeSolveResult, res));
            return;
        }

        // Calculate parameters - ASTAP CLI expects the FOV of the HEIGHT dimension 
        // if width > height (landscape) or the WIDTH if height > width (portrait).
        // Standard definition: FOV of the largest dimension? 
        // The ASTAP manual says: "-fov <v> image height (or width if smaller) in degrees"
        // Actually, it's usually defined as height.
        double fovDeg = (std::min(width, height) * pixelScale) / 3600.0;
        
        // For large FOV (>12°), apply conservative factor to account for optical distortion
        // across wide field of view
        if (fovDeg > 12.0) {
            double fovReduced = fovDeg * 0.92;  // Use 92% of calculated FOV for large fields
            emit logMessage(tr("Large FOV detected: reducing from %1° to %2° for search stability")
                            .arg(fovDeg, 0, 'f', 2).arg(fovReduced, 0, 'f', 2));
            fovDeg = fovReduced;
        }
        
        double raHours = raHint / 15.0;
        double spdDeg = decHint + 90.0;
        
        emit logMessage(tr("Calculated FOV (Height): %1 degrees (dims=%2x%3, scale=%4 arcsec/px)")
                        .arg(fovDeg, 0, 'f', 4).arg(width).arg(height).arg(pixelScale, 0, 'f', 3));

        QStringList commonArgs;
        commonArgs << "-f" << tempTiff;
        commonArgs << "-progress";
        commonArgs << "-z" << "0";
        commonArgs << "-s" << "500"; // Explicit star limit for stability

        // Add database path if available
        QString dbPath = getAstapDatabasePath();
        if (!dbPath.isEmpty()) {
            commonArgs << "-d" << dbPath;
            emit logMessage(tr("ASTAP Database found: %1").arg(dbPath));
        } else {
            // ASTAP has its own internal logic to find databases if not specified.
            // We only warn if we can't find it to pass specifically, but it's often not fatal.
            emit logMessage(tr("Note: ASTAP database not explicitly located by TStar; ASTAP will use its internal search paths."));
        }

        QStringList hintedArgs;
        bool hasHint = (raHint != 0.0 || decHint != 0.0);
        if (hasHint) {
            hintedArgs << "-ra" << QString::number(raHours, 'f', 6);   // RA in hours
            hintedArgs << "-spd" << QString::number(spdDeg, 'f', 6);   // South pole distance in degrees
            hintedArgs << "-r" << QString::number(radiusDeg, 'f', 2);
        }

        QStringList scaleArgs;
        if (fovDeg > 0.0) {
            scaleArgs << "-fov" << QString::number(fovDeg, 'f', 4);
        }

        // Add extra user-defined args from settings
        QSettings settings;
        QString extra = settings.value("paths/astap_extra").toString();
        QStringList extraArgs;
        if (!extra.isEmpty()) {
            extraArgs = extra.split(' ', Qt::SkipEmptyParts);
        }

        auto parseOutputs = [&]() -> bool {
            if (QFile::exists(tempIni)) {
                bool ok = parseAstapIni(tempIni, height, res);
                if (ok) {
                    res.success = true;
                    return true;
                }

                QFile f(tempIni);
                if (f.open(QIODevice::ReadOnly)) {
                    QString content = QString::fromUtf8(f.readAll());
                    if (content.contains("PLTSOLVD=F")) {
                        res.errorMsg = tr("ASTAP was unable to solve the image.");
                    } else {
                        res.errorMsg = tr("Failed to parse ASTAP output files.");
                        emit logMessage("INI Content: " + content);
                    }
                }
                return false;
            }

            if (QFile::exists(tempWcs)) {
                if (parseAstapWCS(tempWcs, height, res)) {
                    res.success = true;
                    return true;
                }

                res.errorMsg = tr("Failed to parse ASTAP WCS file.");
                QFile f(tempWcs);
                if (f.open(QIODevice::ReadOnly)) {
                    emit logMessage("WCS Content: " + QString::fromUtf8(f.readAll()));
                }
                return false;
            }

            res.errorMsg = tr("ASTAP failed to solve the image.");
            return false;
        };

        auto executeAttempt = [&](const QString& label, const QStringList& args) -> bool {
            QFile::remove(tempIni);
            QFile::remove(tempWcs);

            emit logMessage(tr("ASTAP attempt: %1").arg(label));
            
            // Log full path-quoted command for clarity
            QString cmd = "\"" + astapExec + "\" " + args.join(" ");
            emit logMessage(QString("Executing: %1").arg(cmd));

            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels); // Combine stdout/stderr
            p.start(astapExec, args);
            if (!p.waitForStarted(10000)) {
                res.errorMsg = tr("Failed to start ASTAP process.");
                emit logMessage(tr("QProcess start error: %1").arg(p.errorString()));
                return false;
            }

            {
                QMutexLocker locker(&m_processMutex);
                m_runningProcess = &p;
            }

            auto clearRunningProcess = [&]() {
                QMutexLocker locker(&m_processMutex);
                if (m_runningProcess == &p) {
                    m_runningProcess = nullptr;
                }
            };

            auto flushOutput = [&]() {
                QString output = p.readAllStandardOutput() + p.readAllStandardError();
                const QStringList lines = output.split('\n');
                for (const QString& line : lines) {
                    QString t = line.trimmed();
                    if (!t.isEmpty()) {
                        emit logMessage(t);
                    }
                }
            };

            QElapsedTimer timer;
            timer.start();
            constexpr qint64 kAttemptTimeoutMs = 120000;
            bool finished = false;
            while (!(finished = p.waitForFinished(500))) {
                flushOutput();

                if (m_cancelRequested.load()) {
                    emit logMessage(tr("ASTAP solve cancelled."));
                    terminateAstapProcess(p, 2000);
                    clearRunningProcess();
                    res.errorMsg = tr("ASTAP solve cancelled.");
                    return false;
                }

                if (timer.elapsed() > kAttemptTimeoutMs) {
                    emit logMessage(tr("ASTAP timed out after %1 seconds. Terminating process...").arg(kAttemptTimeoutMs / 1000));
                    terminateAstapProcess(p, 2000);
                    clearRunningProcess();
                    res.errorMsg = tr("ASTAP timed out.");
                    return false;
                }
            }

            Q_UNUSED(finished);
            flushOutput();
            clearRunningProcess();

            emit logMessage(tr("ASTAP exit code: %1").arg(p.exitCode()));
            if (p.exitStatus() != QProcess::NormalExit) {
                emit logMessage(tr("ASTAP process did not exit normally."));
            }

            return parseOutputs();
        };

        std::vector<QPair<QString, QStringList>> attempts;
        if (hasHint) {
            attempts.push_back({tr("hinted solve (with scale/FOV)"), commonArgs + hintedArgs + scaleArgs + extraArgs});
            if (!scaleArgs.isEmpty()) {
                attempts.push_back({tr("hinted solve (without scale/FOV)"), commonArgs + hintedArgs + extraArgs});
            }
            attempts.push_back({tr("blind solve"), commonArgs + QStringList{"-r", "180.0"} + extraArgs});
        } else {
            attempts.push_back({tr("blind solve"), commonArgs + QStringList{"-r", "180.0"} + scaleArgs + extraArgs});
            if (!scaleArgs.isEmpty()) {
                attempts.push_back({tr("blind solve (without scale/FOV)"), commonArgs + QStringList{"-r", "180.0"} + extraArgs});
            }
        }

        for (const auto& attempt : attempts) {
            if (executeAttempt(attempt.first, attempt.second)) {
                break;
            }
        }
        
        // Cleanup
        QFile::remove(tempTiff);
        QFile::remove(tempIni);
        QFile::remove(tempWcs);
        
        QMetaObject::invokeMethod(this, "finished", Qt::QueuedConnection, Q_ARG(NativeSolveResult, res));
    });
}

static double parseFitsValue(const QString& content, const QString& key) {
    // FITS header lines are "KEY     =                VALUE / comment"
    // We look for key, then =, then value.
    QRegularExpression re(key + "\\s*=\\s*([-+]?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)");
    auto match = re.match(content);
    if (match.hasMatch()) return match.captured(1).toDouble();
    return 0.0;
}

bool AstapSolver::parseAstapWCS(const QString& wcsFile, int imageHeight, NativeSolveResult& res) {
    QFile file(wcsFile);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QString content = QString::fromUtf8(file.readAll());
    
    res.crval1 = parseFitsValue(content, "CRVAL1");
    res.crval2 = parseFitsValue(content, "CRVAL2");
    res.crpix1 = parseFitsValue(content, "CRPIX1");
    // ASTAP solved a TIFF, so it evaluated Y upside down relative to TStar.
    // Convert back from bottom-up FITS Y to TStar's raw memory Y.
    res.crpix2 = imageHeight - parseFitsValue(content, "CRPIX2") + 1.0; 
    res.cd[0][0] = parseFitsValue(content, "CD1_1");
    res.cd[0][1] = -parseFitsValue(content, "CD1_2"); // dRA/dY inverted
    res.cd[1][0] = parseFitsValue(content, "CD2_1");
    res.cd[1][1] = -parseFitsValue(content, "CD2_2"); // dDec/dY inverted
    
    return res.cd[0][0] != 0.0;
}

bool AstapSolver::parseAstapIni(const QString& iniFile, int imageHeight, NativeSolveResult& res) {
    QFile file(iniFile);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QString content = QString::fromUtf8(file.readAll());
    
    if (content.contains("PLTSOLVD=F")) return false;
    if (!content.contains("PLTSOLVD=T") && !content.contains("Solved=T")) {
        // Just try to parse keywords anyway if solve status is missing
    }
    
    res.crval1 = parseFitsValue(content, "CRVAL1");
    res.crval2 = parseFitsValue(content, "CRVAL2");
    res.crpix1 = parseFitsValue(content, "CRPIX1");
    // Apply Y-axis reflection for ASTAP solving a raw TIFF image
    res.crpix2 = imageHeight - parseFitsValue(content, "CRPIX2") + 1.0;
    res.cd[0][0] = parseFitsValue(content, "CD1_1");
    res.cd[0][1] = -parseFitsValue(content, "CD1_2");
    res.cd[1][0] = parseFitsValue(content, "CD2_1");
    res.cd[1][1] = -parseFitsValue(content, "CD2_2");
    
    return res.cd[0][0] != 0.0;
}
