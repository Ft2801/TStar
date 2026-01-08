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
#include <QTimer>
#include <QStandardPaths>
#include <QWaitCondition>
#include <QMutex>

// ============================================================================
// CosmicClarityWorker Implementation
// ============================================================================

CosmicClarityWorker::CosmicClarityWorker(QObject* parent) 
    : QObject(parent) {}

QString CosmicClarityWorker::getCosmicFolder() {
    QSettings s;
    return s.value("paths/cosmic_clarity").toString();
}

QString CosmicClarityWorker::getExecutableName(CosmicClarityParams::Mode mode) {
    if (mode == CosmicClarityParams::Mode_SuperRes) {
#ifdef Q_OS_WIN
        return "setiastrocosmicclarity_superres.exe";
#else
        return "setiastrocosmicclarity_superres";
#endif
    }
    
    bool isWin = false;
#ifdef Q_OS_WIN
    isWin = true;
#endif

    if (mode == CosmicClarityParams::Mode_Denoise) {
        return isWin ? "SetiAstroCosmicClarity_denoise.exe" : "SetiAstroCosmicClarity_denoise";
    }
    
    // Sharpen / Both modes use main executable
    return isWin ? "SetiAstroCosmicClarity.exe" : "SetiAstroCosmicClarity";
}

void CosmicClarityWorker::process(const ImageBuffer& input, const CosmicClarityParams& params) {
    QString errorMsg;
    ImageBuffer output;
    
    // Validate input
    if (input.data().empty()) {
        emit finished(output, "Input buffer is empty");
        return;
    }
    
    QString root = getCosmicFolder();
    if (root.isEmpty() || !QDir(root).exists()) {
        emit finished(output, "Cosmic Clarity folder not configured or not found. Please set it in Settings.");
        return;
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

    // Verify Python availability
    QString pythonExe;
    
#if defined(Q_OS_MAC)
    QString bundledPython = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
    QString devPython = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#else
    QString bundledPython = QCoreApplication::applicationDirPath() + "/python/python.exe";
    QString devPython = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#endif
    QString foundPython = QStandardPaths::findExecutable("python3");
    if (QFile::exists(bundledPython)) {
        pythonExe = bundledPython;
    } else if (QFile::exists(devPython)) {
        pythonExe = devPython;
    } else if (!foundPython.isEmpty()) {
        pythonExe = foundPython;
    } else {
        pythonExe = "python3";
    }

    QProcess pythonCheck;
    pythonCheck.start(pythonExe, QStringList() << "--version");
    if (!pythonCheck.waitForFinished(5000)) {
        emit finished(output, "Python version check timed out.");
        return;
    }
    
    if (pythonCheck.exitCode() != 0) {
        emit finished(output, "Python interpreter failed to run (bundled python might be broken).");
        return;
    }

    // Find bridge script
    QString bridgeScriptPath = QCoreApplication::applicationDirPath() + "/scripts/cosmic_bridge.py";
    if (!QFile::exists(bridgeScriptPath)) {
        bridgeScriptPath = QCoreApplication::applicationDirPath() + "/../Resources/scripts/cosmic_bridge.py";
    }
    if (!QFile::exists(bridgeScriptPath)) {
        bridgeScriptPath = QCoreApplication::applicationDirPath() + "/../src/scripts/cosmic_bridge.py";
    }
    
    if (!QFile::exists(bridgeScriptPath)) {
        emit finished(output, "Bridge script not found.");
        return;
    }

    // Helper to run process with cancellation and timeout
    auto runProcess = [&](QProcess& p, int timeoutMs = 600000) -> bool {
        p.start();
        if (!p.waitForStarted()) {
            errorMsg = "Failed to start process: " + p.program();
            return false;
        }
        
        // Wait loop with cancellation support
        int elapsed = 0;
        int interval = 100;
        while (!p.waitForFinished(interval)) {
            if (m_stop) {
                p.kill();
                p.waitForFinished();
                errorMsg = "Process cancelled by user.";
                return false;
            }
            elapsed += interval;
            if (elapsed > timeoutMs) {
                p.kill();
                errorMsg = "Process timed out.";
                return false;
            }
        }
        
        if (p.exitCode() != 0) {
            QString errOut = p.readAllStandardError();
            if (errOut.isEmpty()) errOut = p.readAllStandardOutput();
            errorMsg = "Process failed: " + errOut;
            return false;
        }
        return true;
    };

    // 1. Write Input to Raw Float
    QString rawInputFile = inputDir.filePath("input.raw");
    {
        QFile raw(rawInputFile);
        if (!raw.open(QIODevice::WriteOnly)) {
            emit finished(output, "Failed to write raw input.");
            return;
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
        
        if (!runProcess(p, 60000)) {
            emit finished(output, errorMsg);
            return;
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
            if (!txt.isEmpty()) emit processOutput(txt.trimmed());
        });
        connect(&process, &QProcess::readyReadStandardError, [&process, this](){
            QString txt = process.readAllStandardError();
            if (!txt.isEmpty()) {
                QString trimmed = txt.trimmed();
                if (trimmed.contains("INFO") || trimmed.contains("Progress")) {
                    emit processOutput(trimmed);
                } else {
                    emit processOutput("LOG: " + trimmed);
                }
            }
        });

        if (!runProcess(process, 600000)) {
            purge(inputDir); 
            purge(outputDir);
            emit finished(output, errorMsg);
            return;
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
            if (found.isEmpty()) { 
                purge(inputDir); purge(outputDir);
                emit finished(output, "SuperRes output not found.");
                return;
            }
            expectedOutput = found.first().absoluteFilePath();
        } else {
            expectedOutput = outputDir.filePath(baseName + s.suffix + ".tif");
            if (!QFile::exists(expectedOutput)) {
                QStringList filters; filters << baseName + s.suffix + ".*";
                QFileInfoList found = outputDir.entryInfoList(filters, QDir::Files);
                if (!found.isEmpty()) expectedOutput = found.first().absoluteFilePath();
                else { 
                    purge(inputDir); purge(outputDir);
                    emit finished(output, "Output file not found: " + baseName + s.suffix);
                    return;
                }
            }
        }

        if (i < steps.size() - 1) {
            // Chain: Rename output to input/image.tif
            QFile::remove(savedTiff);
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
                emit finished(output, errorMsg);
                return;
            }
            
            QString outData = p.readAllStandardOutput().trimmed();
            emit processOutput("Bridge Output:\n" + outData);

            if (outData.contains("Error")) {
                purge(inputDir); purge(outputDir);
                emit finished(output, "Bridge error: " + outData);
                return;
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
                purge(inputDir); purge(outputDir);
                emit finished(output, "Bridge failed to provide result marker: " + outData);
                return;
            }

            QStringList parts = resultLine.mid(7).trimmed().split(QRegularExpression("\\s+"));
            if (parts.size() < 3) {
                purge(inputDir); purge(outputDir);
                emit finished(output, "Bridge failed to parse dimensions: " + resultLine);
                return;
            }
            
            int w = parts[0].toInt();
            int h = parts[1].toInt();
            int c = parts[2].toInt();

            if (w <= 0 || h <= 0) {
                purge(inputDir); purge(outputDir);
                emit finished(output, "Bridge returned invalid dimensions: " + resultLine);
                return;
            }
            
            QFile rawRes(rawResult);
            if (rawRes.open(QIODevice::ReadOnly)) {
                QByteArray blob = rawRes.readAll();
                std::vector<float> data(blob.size()/4);
                memcpy(data.data(), blob.constData(), blob.size());
                
                output.setData(w, h, c, data);
                output.setMetadata(input.metadata());
                emit processOutput(QString("Loaded via Bridge: %1x%2 %3ch").arg(w).arg(h).arg(c));
            } else {
                purge(inputDir); purge(outputDir);
                emit finished(output, "Failed to read raw result.");
                return;
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

    emit finished(output, "");  // Success
}

// ============================================================================
// CosmicClarityRunner Implementation
// ============================================================================

CosmicClarityRunner::CosmicClarityRunner(QObject* parent) 
    : QObject(parent) {}

CosmicClarityRunner::~CosmicClarityRunner() {
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

bool CosmicClarityRunner::run(const ImageBuffer& input, ImageBuffer& output, 
                              const CosmicClarityParams& params, QString* errorMsg) {
    // Create thread and worker if not already created
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new CosmicClarityWorker();
        m_worker->moveToThread(m_thread);
        
        // Connect signals
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &CosmicClarityWorker::finished, this, &CosmicClarityRunner::onWorkerFinished);
        connect(m_worker, &CosmicClarityWorker::processOutput, this, &CosmicClarityRunner::processOutput);
        
        m_thread->start();
    }
    
    // Reset synchronization flag
    m_finished = false;
    
    // Emit process signal (queued call in worker thread)
    QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                              Q_ARG(const ImageBuffer&, input),
                              Q_ARG(const CosmicClarityParams&, params));
    
    // Wait for completion with event loop
    QEventLoop loop;
    // Connect to worker's finished signal instead of non-existent runner signal
    QObject::connect(m_worker, QOverload<const ImageBuffer&, const QString&>::of(&CosmicClarityWorker::finished), 
                    this, &CosmicClarityRunner::onWorkerFinished);
    
    // Connect onWorkerFinished to exit the loop
    QObject::connect(this, &CosmicClarityRunner::workerDone, &loop, &QEventLoop::quit);
    
    // Timeout: 15 minutes
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(15 * 60 * 1000);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
    loop.exec();
    timer.stop();
    
    if (!m_finished) {
        if (errorMsg) *errorMsg = "Operation timed out";
        m_errorMsg = "Operation timed out";
        return false;
    }
    
    if (!m_errorMsg.isEmpty()) {
        if (errorMsg) *errorMsg = m_errorMsg;
        return false;
    }
    
    output = m_output;
    return true;
}

void CosmicClarityRunner::onWorkerFinished(const ImageBuffer& output, const QString& errorMsg) {
    m_output = output;
    m_errorMsg = errorMsg;
    m_finished = true;
    emit workerDone();
}
