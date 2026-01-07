#include "CosmicClarityRunner.h"
#include "ImageBuffer.h"
#include <QSettings>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QRegularExpression>
#include <QEventLoop>

CosmicClarityRunner::CosmicClarityRunner(QObject* parent) : QObject(parent) {}

QString CosmicClarityRunner::getCosmicFolder() {
    QSettings s;
    return s.value("paths/cosmic_clarity").toString();
}

QString CosmicClarityRunner::getExecutableName(CosmicClarityParams::Mode mode) {
    if (mode == CosmicClarityParams::Mode_SuperRes) {
#ifdef Q_OS_WIN
        return "setiastrocosmicclarity_superres.exe";
#else
        return "setiastrocosmicclarity_superres";
#endif
    }
    
    bool isWin = true;

    if (mode == CosmicClarityParams::Mode_Denoise) {
        return isWin ? "SetiAstroCosmicClarity_denoise.exe" : "SetiAstroCosmicClarity_denoise";
    }
    
    // Sharpen / Both modes use main executable
    return isWin ? "SetiAstroCosmicClarity.exe" : "SetiAstroCosmicClarity";
}

bool CosmicClarityRunner::run(const ImageBuffer& input, ImageBuffer& output, const CosmicClarityParams& params, QString* errorMsg) {
    QString root = getCosmicFolder();
    if (root.isEmpty() || !QDir(root).exists()) {
        if (errorMsg) *errorMsg = "Cosmic Clarity folder not configured or not found. Please set it in Settings.";
        return false;
    }
    m_stop = false;

    QDir rootDir(root);
    QDir inputDir(rootDir.filePath("input"));
    QDir outputDir(rootDir.filePath("output"));

    auto purge = [](QDir& d) {
        if (!d.exists()) return;
        QFileInfoList list = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const auto& fi : list) QFile::remove(fi.absoluteFilePath());
    };

    if (!inputDir.exists()) inputDir.mkpath(".");
    if (!outputDir.exists()) outputDir.mkpath(".");
    purge(inputDir);
    purge(outputDir);

    // Verify Python availability - cross-platform paths
    QString pythonExe = "python";
    
#if defined(Q_OS_MAC)
    // macOS: Check for bundled virtualenv in app bundle Resources
    QString bundledPython = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
    QString devPython = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#else
    // Windows: Check for bundled embeddable Python
    QString bundledPython = QCoreApplication::applicationDirPath() + "/python/python.exe";
    QString devPython = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#endif

    if (QFile::exists(bundledPython)) {
        pythonExe = bundledPython;
    } else if (QFile::exists(devPython)) {
        pythonExe = devPython;
    }

    QProcess pythonCheck;
    pythonCheck.start(pythonExe, QStringList() << "--version");
    if (!pythonCheck.waitForFinished(5000)) {
        if(errorMsg) *errorMsg = "Python not found! Please check PATH or TStar installation.";
        return false;
    }

    // Embed cosmic_bridge.py
    QString bridgeScriptPath = QCoreApplication::applicationDirPath() + "/scripts/cosmic_bridge.py";
    if (!QFile::exists(bridgeScriptPath)) {
        // Try Resources folder (macOS DMG bundle)
        bridgeScriptPath = QCoreApplication::applicationDirPath() + "/../Resources/scripts/cosmic_bridge.py";
    }
    if (!QFile::exists(bridgeScriptPath)) {
        bridgeScriptPath = QCoreApplication::applicationDirPath() + "/../src/scripts/cosmic_bridge.py";
    }
    
    if (!QFile::exists(bridgeScriptPath)) {
        if(errorMsg) *errorMsg = "Bridge script not found.";
        return false;
    }

    // Helper to run process with cancellation and timeout
    auto runProcess = [&](QProcess& p, int timeoutMs = 600000) -> bool {
        p.start();
        if (!p.waitForStarted()) {
             if (errorMsg) *errorMsg = "Failed to start process: " + p.program();
             return false;
        }
        
        // Wait loop
        int elapsed = 0;
        int interval = 100;
        while (!p.waitForFinished(interval)) {
             if (m_stop) {
                 p.kill();
                 p.waitForFinished();
                 if (errorMsg) *errorMsg = "Process cancelled by user.";
                 return false;
             }
             elapsed += interval;
             if (elapsed > timeoutMs) {
                 p.kill();
                 if (errorMsg) *errorMsg = "Process timed out.";
                 return false;
             }
        }
        
        if (p.exitCode() != 0) {
            QString errOut = p.readAllStandardError();
            if (errOut.isEmpty()) errOut = p.readAllStandardOutput();
            if (errorMsg) *errorMsg = "Process failed: " + errOut;
            return false;
        }
        return true;
    };

    // 1. Write Input to Raw Float
    QString rawInputFile = inputDir.filePath("input.raw");
    {
        QFile raw(rawInputFile);
        if(!raw.open(QIODevice::WriteOnly)) {
            if(errorMsg) *errorMsg = "Failed to write raw input.";
            return false;
        }
        raw.write((const char*)input.data().data(), input.data().size() * sizeof(float));
        raw.close();
    }

    // 2. Convert Raw to TIFF using Bridge
    QString savedTiff = inputDir.filePath("image.tif");
    {
        QStringList args;
        args << bridgeScriptPath << "save" << savedTiff 
             << QString::number(input.width()) << QString::number(input.height()) << QString::number(input.channels())
             << rawInputFile;
        
        QProcess p;
        p.setProgram(pythonExe);
        p.setArguments(args);
        
        if (!runProcess(p, 60000)) { // 60s timeout for IO
             return false;
        }
    }
    emit processOutput("Input staged via Python Bridge: " + savedTiff);

    // Determine Steps
    struct Step {
        CosmicClarityParams::Mode toolMode;
        QString suffix;
    };
    QVector<Step> steps;

    if (params.mode == CosmicClarityParams::Mode_SuperRes) {
        QString sf = params.scaleFactor;
        steps.append({CosmicClarityParams::Mode_SuperRes, "_upscaled" + sf.replace("x","")}); 
    } else if (params.mode == CosmicClarityParams::Mode_Both) {
        steps.append({CosmicClarityParams::Mode_Sharpen, "_sharpened"});
        steps.append({CosmicClarityParams::Mode_Denoise, "_denoised"});
    } else if (params.mode == CosmicClarityParams::Mode_Sharpen) {
        steps.append({CosmicClarityParams::Mode_Sharpen, "_sharpened"});
    } else if (params.mode == CosmicClarityParams::Mode_Denoise) {
        steps.append({CosmicClarityParams::Mode_Denoise, "_denoised"});
    }

    QString currentInputFile = savedTiff;
    QString baseName = "image";

    // Execute Steps
    for (int i = 0; i < steps.size(); ++i) {
        Step s = steps[i];
        QString exeName = getExecutableName(s.toolMode);
        QString exePath = rootDir.filePath(exeName);
        
        QStringList args;
        if (s.toolMode == CosmicClarityParams::Mode_Sharpen) {
            args << "--sharpening_mode" << params.sharpenMode;
            args << "--stellar_amount" << QString::number(params.stellarAmount, 'f', 2);
            args << "--nonstellar_strength" << QString::number(params.nonStellarPSF, 'f', 1);
            args << "--nonstellar_amount" << QString::number(params.nonStellarAmount, 'f', 2);
            if (params.separateChannelsSharpen) args << "--sharpen_channels_separately";
            if (params.autoPSF) args << "--auto_detect_psf";
        }
        else if (s.toolMode == CosmicClarityParams::Mode_Denoise) {
            args << "--denoise_strength" << QString::number(params.denoiseLum, 'f', 2);
            args << "--color_denoise_strength" << QString::number(params.denoiseColor, 'f', 2);
            args << "--denoise_mode" << params.denoiseMode;
            if (params.separateChannelsDenoise) args << "--separate_channels";
        }
        else if (s.toolMode == CosmicClarityParams::Mode_SuperRes) {
             int scale = 2;
             if (params.scaleFactor.contains("3")) scale = 3;
             if (params.scaleFactor.contains("4")) scale = 4;
             args << "--input" << currentInputFile;
             args << "--output_dir" << outputDir.absolutePath();
             args << "--scale" << QString::number(scale);
             args << "--model_dir" << root;
        }

        if (s.toolMode != CosmicClarityParams::Mode_SuperRes && !params.useGpu) {
            args << "--disable_gpu";
        }

        emit processOutput("Running: " + exeName);

        QProcess process;
        process.setProgram(exePath);
        process.setArguments(args);
        process.setWorkingDirectory(root);
        
        connect(&process, &QProcess::readyReadStandardOutput, [&process, this](){
            QString txt = process.readAllStandardOutput();
            if(!txt.isEmpty()) emit processOutput(txt.trimmed());
        });
        connect(&process, &QProcess::readyReadStandardError, [&process, this](){
            QString txt = process.readAllStandardError();
            if(!txt.isEmpty()) {
                QString trimmed = txt.trimmed();
                if (trimmed.contains("INFO") || trimmed.contains("Progress")) emit processOutput(trimmed);
                else emit processOutput("LOG: " + trimmed);
            }
        });

        if (!runProcess(process, 600000)) { // 10 min timeout
            purge(inputDir); purge(outputDir);
            return false;
        }

        // Determine Output
        QString expectedOutput;
        if (s.toolMode == CosmicClarityParams::Mode_SuperRes) {
             int scale = 2;
             if (params.scaleFactor.contains("3")) scale = 3;
             if (params.scaleFactor.contains("4")) scale = 4;
             QString suffix = QString("_upscaled%1").arg(scale);
             QStringList filters; filters << baseName + suffix + ".*";
             QFileInfoList found = outputDir.entryInfoList(filters, QDir::Files);
             if (found.isEmpty()) { if(errorMsg) *errorMsg = "SuperRes output not found."; return false; }
             expectedOutput = found.first().absoluteFilePath();
        } else {
             expectedOutput = outputDir.filePath(baseName + s.suffix + ".tif");
             if (!QFile::exists(expectedOutput)) {
                QStringList filters; filters << baseName + s.suffix + ".*";
                QFileInfoList found = outputDir.entryInfoList(filters, QDir::Files);
                if (!found.isEmpty()) expectedOutput = found.first().absoluteFilePath();
                else { if (errorMsg) *errorMsg = "Output file not found: " + baseName + s.suffix; return false; }
             }
        }

        if (i < steps.size() - 1) {
             // Chain: Rename output to input/image.tif
             QFile::remove(savedTiff); // remove old input
             QFile::rename(expectedOutput, savedTiff);
             emit processOutput("Chaining...");
        } else {
            // Load Results via Bridge
            QString rawResult = outputDir.filePath("result.raw");
            QStringList args;
            args << bridgeScriptPath << "load" << expectedOutput << rawResult;
            
            QProcess p;
            p.setProgram(pythonExe);
            p.setArguments(args);
            
            if (!runProcess(p, 60000)) {
                purge(inputDir); purge(outputDir);
                return false;
            }
            
            QString outData = p.readAllStandardOutput().trimmed();
            emit processOutput("Bridge Output:\n" + outData);

            if (outData.contains("Error")) {
                 if(errorMsg) *errorMsg = "Bridge error: " + outData;
                 return false;
            }

            // Find the line starting with RESULT:
            QString resultLine;
            QStringList lines = outData.split('\n');
            for (const QString& line : lines) {
                if (line.trimmed().startsWith("RESULT:")) {
                    resultLine = line.trimmed();
                    break;
                }
            }

            if (resultLine.isEmpty()) {
                 if(errorMsg) *errorMsg = "Bridge failed to provide result marker: " + outData;
                 return false;
            }

            QStringList parts = resultLine.mid(7).trimmed().split(QRegularExpression("\\s+"));
            if (parts.size() < 3) {
                 if(errorMsg) *errorMsg = "Bridge failed to parse dimensions: " + resultLine;
                 return false;
            }
            
            int w = parts[0].toInt();
            int h = parts[1].toInt();
            int c = parts[2].toInt();

            if (w <= 0 || h <= 0) {
                if(errorMsg) *errorMsg = "Bridge returned invalid dimensions: " + resultLine;
                return false;
            }
            
            QFile rawRes(rawResult);
            if(rawRes.open(QIODevice::ReadOnly)) {
                QByteArray blob = rawRes.readAll();
                std::vector<float> data(blob.size()/4);
                memcpy(data.data(), blob.constData(), blob.size());
                
                output.setData(w, h, c, data);
                emit processOutput(QString("Loaded via Bridge: %1x%2 %3ch").arg(w).arg(h).arg(c));
            } else {
                if(errorMsg) *errorMsg = "Failed to read raw result.";
                return false;
            }
             
             // Cleanup
             QFile::remove(expectedOutput);
             QFile::remove(rawResult);
        }
    }
    
    QFile::remove(rawInputFile);
    QFile::remove(savedTiff);
    purge(inputDir);
    purge(outputDir);

    output.setMetadata(input.metadata()); // Preserve WCS and other metadata
    return true;
}
