#include "CosmicClarityRunner.h"
#include "ImageBuffer.h"
#include "network/ModelDownloader.h"
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
#include <QJsonObject>
#include <QJsonDocument>
#include <QOverload>

// ============================================================================
// CosmicClarityWorker Implementation
// ============================================================================

CosmicClarityWorker::CosmicClarityWorker(QObject* parent) 
    : QObject(parent) {}

void CosmicClarityWorker::process(const ImageBuffer& input, const CosmicClarityParams& params) {
    QString errorMsg;
    ImageBuffer output;

    // Validate input
    if (input.data().empty()) {
        emit finished(output, "Input buffer is empty");
        return;
    }

    // Use downloaded models directory instead of external executable folder
    QString modelsRoot = ModelDownloader::cosmicClarityRoot();
    if (modelsRoot.isEmpty() || !QDir(modelsRoot).exists()) {
        emit finished(output, "Cosmic Clarity models not found. Please download them in Settings.");
        return;
    }
    m_stop = false;

    QDir modelsDir(modelsRoot);
    QDir tempDir(modelsDir.filePath("temp_io"));
    if (!tempDir.exists()) tempDir.mkpath(".");

    auto purge = [](QDir& d) {
        if (!d.exists()) return;
        for (const auto& fi : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
            QFile::remove(fi.absoluteFilePath());
    };
    purge(tempDir);

    // Locate bundled Python interpreter.
    // No test-run: DYLD_FRAMEWORK_PATH (set below) lets dyld find Python.framework
    // inside the bundle even when the baked-in Homebrew Cellar path is stale.
    QString pythonExe;
#if defined(Q_OS_MAC)
    pythonExe = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
    if (!QFile::exists(pythonExe))
        pythonExe = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#else
    pythonExe = QCoreApplication::applicationDirPath() + "/python/python.exe";
    if (!QFile::exists(pythonExe))
        pythonExe = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#endif

    if (!QFile::exists(pythonExe)) {
        emit finished(output, "Bundled Python interpreter not found.\nExpected path: " + pythonExe);
        return;
    }

    // Find bridge script
    QString bridge = QCoreApplication::applicationDirPath() + "/scripts/cosmic_bridge.py";
    if (!QFile::exists(bridge))
        bridge = QCoreApplication::applicationDirPath() + "/../Resources/scripts/cosmic_bridge.py";
    if (!QFile::exists(bridge))
        bridge = QCoreApplication::applicationDirPath() + "/../src/scripts/cosmic_bridge.py";
    if (!QFile::exists(bridge)) {
        emit finished(output, "Bridge script (cosmic_bridge.py) not found.");
        return;
    }

    // ---- Build parameters JSON ----
    QJsonObject json;
    if (params.mode == CosmicClarityParams::Mode_Sharpen)   json["mode"] = "sharpen";
    else if (params.mode == CosmicClarityParams::Mode_Denoise)  json["mode"] = "denoise";
    else if (params.mode == CosmicClarityParams::Mode_Both)     json["mode"] = "both";
    else if (params.mode == CosmicClarityParams::Mode_SuperRes) json["mode"] = "superres";

    json["use_gpu"]         = params.useGpu;
    json["sharpen_mode"]    = params.sharpenMode;
    json["stellar_amount"]  = params.stellarAmount;
    json["nonstellar_amount"] = params.nonStellarAmount;
    json["nonstellar_psf"]  = params.nonStellarPSF;
    json["auto_psf"]        = params.autoPSF;
    json["separate_channels_sharpen"] = params.separateChannelsSharpen;

    json["denoise_luma"]    = params.denoiseLum;
    json["denoise_color"]   = params.denoiseColor;
    json["denoise_mode"]    = params.denoiseMode;
    json["separate_channels_denoise"] = params.separateChannelsDenoise;

    int scale = 2;
    if (params.scaleFactor.contains("3")) scale = 3;
    if (params.scaleFactor.contains("4")) scale = 4;
    json["scale"]    = scale;
    json["width"]    = input.width();
    json["height"]   = input.height();
    json["channels"] = input.channels();
    json["models_dir"] = modelsRoot;

    QString paramsFile = tempDir.filePath("params.json");
    {
        QFile f(paramsFile);
        if (!f.open(QIODevice::WriteOnly)) {
            emit finished(output, "Failed to write params.json");
            return;
        }
        f.write(QJsonDocument(json).toJson());
        f.close();
    }

    // ---- Write raw input ----
    QString rawIn = tempDir.filePath("input.raw");
    {
        QFile f(rawIn);
        if (!f.open(QIODevice::WriteOnly)) {
            emit finished(output, "Failed to write input.raw");
            return;
        }
        f.write(reinterpret_cast<const char*>(input.data().data()),
                input.data().size() * sizeof(float));
        f.close();
    }

    QString rawOut = tempDir.filePath("output.raw");

    // ---- Launch bridge: process ----
    emit processOutput("Launching Cosmic Clarity (ONNX)...");

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
#if defined(Q_OS_MAC)
    // Make Python.framework findable regardless of the path baked into the binary.
    {
        const QString frameworksDir = QCoreApplication::applicationDirPath() + "/../Frameworks";
        if (QDir(frameworksDir).exists()) {
            const QString curFw = env.value("DYLD_FRAMEWORK_PATH");
            env.insert("DYLD_FRAMEWORK_PATH", curFw.isEmpty() ? frameworksDir : frameworksDir + ":" + curFw);
            const QString curLib = env.value("DYLD_LIBRARY_PATH");
            env.insert("DYLD_LIBRARY_PATH", curLib.isEmpty() ? frameworksDir : frameworksDir + ":" + curLib);
        }
        const QStringList venvBases = {
            QCoreApplication::applicationDirPath() + "/../Resources/python_venv/lib",
            QCoreApplication::applicationDirPath() + "/../../deps/python_venv/lib"
        };
        for (const QString& venvLib : venvBases) {
            if (QDir(venvLib).exists()) {
                const QStringList pyDirs = QDir(venvLib).entryList({"python3*"}, QDir::Dirs | QDir::NoDotAndDotDot);
                if (!pyDirs.isEmpty()) {
                    const QString sitePkgs = venvLib + "/" + pyDirs.first() + "/site-packages";
                    if (QDir(sitePkgs).exists()) {
                        const QString cur = env.value("PYTHONPATH");
                        env.insert("PYTHONPATH", cur.isEmpty() ? sitePkgs : sitePkgs + ":" + cur);
                        break;
                    }
                }
            }
        }
    }
#endif

    proc.setProcessEnvironment(env);
    
    proc.setProgram(pythonExe);
    proc.setArguments(QStringList() << bridge << "process" << paramsFile << rawIn << rawOut);
    proc.setProcessChannelMode(QProcess::MergedChannels);

    QString aiLog;
    // Real-time stdout relay
    connect(&proc, &QProcess::readyReadStandardOutput, [&proc, &aiLog, this]() {
        QString txt = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!txt.isEmpty()) {
            emit processOutput(txt);
            aiLog.append(txt + "\n");
        }
    });

    proc.start();
    if (!proc.waitForStarted(10000)) {
        emit finished(output, "Failed to start Python process.");
        return;
    }

    // Poll for completion with cancel + timeout support
    int elapsed = 0;
    const int interval = 200;
    const int timeoutMs = 20 * 60 * 1000;  // 20 min
    while (!proc.waitForFinished(interval)) {
        if (m_stop) {
            proc.kill();
            proc.waitForFinished(3000);
            emit finished(output, "Cancelled.");
            return;
        }
        elapsed += interval;
        if (elapsed > timeoutMs) {
            proc.kill();
            proc.waitForFinished(3000);
            emit finished(output, "Timed out.");
            return;
        }
    }

    // Grab any remaining output
    {
        QString tail = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!tail.isEmpty()) {
             emit processOutput(tail);
             aiLog.append(tail + "\n");
        }
    }

    if (proc.exitCode() != 0) {
        QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = "bridge exited with code " + QString::number(proc.exitCode());
        emit finished(output, "Python error: " + err + "\n\nLog:\n" + aiLog.trimmed().right(1000));
        return;
    }

    // ---- Read result raw ----
    if (!QFile::exists(rawOut)) {
        emit finished(output, "Result file not found.");
        return;
    }

    int outW = input.width();
    int outH = input.height();
    int outC = input.channels();
    if (params.mode == CosmicClarityParams::Mode_SuperRes) {
        outW *= scale;
        outH *= scale;
    }

    QFile rawRes(rawOut);
    if (!rawRes.open(QIODevice::ReadOnly)) {
        emit finished(output, "Failed to read output.raw");
        return;
    }
    qint64 expected = (qint64)outW * outH * outC * sizeof(float);
    if (rawRes.size() != expected) {
        emit finished(output, QString("Size mismatch: expected %1 got %2").arg(expected).arg(rawRes.size()));
        return;
    }
    QByteArray blob = rawRes.readAll();
    rawRes.close();

    std::vector<float> data(blob.size() / sizeof(float));
    memcpy(data.data(), blob.constData(), blob.size());
    output.setData(outW, outH, outC, data);
    output.setMetadata(input.metadata());

    // Cleanup temp
    purge(tempDir);

    emit processOutput(QString("Done: %1x%2 %3ch").arg(outW).arg(outH).arg(outC));
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
