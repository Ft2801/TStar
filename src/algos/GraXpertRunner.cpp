#include "GraXpertRunner.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

// ============================================================================
// GraXpertWorker Implementation
// ============================================================================

GraXpertWorker::GraXpertWorker(QObject* parent) : QObject(parent) {}

QString GraXpertWorker::getExecutablePath() {
    QSettings settings;
    return settings.value("paths/graxpert").toString();
}

void GraXpertWorker::process(const ImageBuffer& input, const GraXpertParams& params) {
    QString errorMsg;
    ImageBuffer output;
    
    if (input.data().empty()) {
        emit finished(output, "Input buffer is empty");
        return;
    }
    
    QString exe = getExecutablePath();
    if (exe.isEmpty()) {
        emit finished(output, "GraXpert path not set.");
        return;
    }
    m_stop = false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        emit finished(output, "Failed to create temp dir.");
        return;
    }

    // 1. Ensure bridge script path
    QString scriptPath = QCoreApplication::applicationDirPath() + "/scripts/graxpert_bridge.py";
    if (!QFile::exists(scriptPath)) {
        scriptPath = QCoreApplication::applicationDirPath() + "/../Resources/scripts/graxpert_bridge.py";
    }
    if (!QFile::exists(scriptPath)) {
        scriptPath = QCoreApplication::applicationDirPath() + "/../src/scripts/graxpert_bridge.py";
    }

    if (!QFile::exists(scriptPath)) {
        emit finished(output, "Bridge script not found at: " + scriptPath);
        return;
    }

    // 2. Write Input to Raw Float
    QString rawInputFile = tempDir.filePath("input.raw");
    {
        QFile raw(rawInputFile);
        if (!raw.open(QIODevice::WriteOnly)) {
            emit finished(output, "Failed to write raw input.");
            return;
        }
        raw.write((const char*)input.data().data(), input.data().size() * sizeof(float));
        raw.close();
    }

    // Locate bundled Python interpreter once for all bridge calls.
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

    // Helper lambda: build QProcessEnvironment for every Python bridge subprocess.
    auto makePythonEnv = []() -> QProcessEnvironment {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONUNBUFFERED", "1");
#if defined(Q_OS_MAC)
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
#endif
        return env;
    };

    // 3. Convert Raw to TIFF using Bridge
    QString inputFile = tempDir.filePath("input.tiff");
    QString convLog;
    {
        QStringList args;
        args << scriptPath << "save" << inputFile 
             << QString::number(input.width()) << QString::number(input.height()) << QString::number(input.channels())
             << rawInputFile;
        
        QProcess p;
        p.setProcessEnvironment(makePythonEnv());
        p.setProcessChannelMode(QProcess::MergedChannels);
        
        connect(&p, &QProcess::readyReadStandardOutput, [&p, &convLog, this]() {
            QString txt = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if(!txt.isEmpty()){
                emit processOutput(txt);
                convLog.append(txt + "\n");
            }
        });
        
        p.start(pythonExe, args);
        if (!p.waitForFinished(60000)) {
            emit finished(output, "Bridge timeout saving TIFF.\n\nLog:\n" + convLog.trimmed().right(1000));
            return;
        }

        if (p.exitCode() != 0) {
            QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
            if (err.isEmpty() && !convLog.isEmpty()) err = convLog.trimmed().right(1000);
            emit finished(output, "Bridge failed to save TIFF:\n\n" + err);
            return;
        }
    }
    emit processOutput("Input staged via Python Bridge: " + inputFile);

    QString op = params.isDenoise ? "denoising" : "background-extraction";
    QStringList args;
    args << "-cmd" << op << inputFile << "-cli";
    args << "-gpu" << (params.useGpu ? "true" : "false");
    
    if (params.isDenoise) {
        args << "-strength" << QString::number(params.strength, 'f', 2);
        args << "-batch_size" << (params.useGpu ? "4" : "1");
        if (!params.aiVersion.isEmpty() && params.aiVersion != "Latest (auto)") {
            args << "-ai_version" << params.aiVersion;
        }
    } else {
        args << "-smoothing" << QString::number(params.smoothing, 'f', 2);
    }

    emit processOutput("Running GraXpert...");
    
    QProcess process;
    process.setProgram(exe);
    process.setArguments(args);
    
    QString graxpertLog;
    // Real-time output
    connect(&process, &QProcess::readyReadStandardOutput, [&process, &graxpertLog, this](){
        QString out = process.readAllStandardOutput();
        if (!out.isEmpty()) {
             emit processOutput(out.trimmed());
             graxpertLog.append(out + "\n");
        }
    });
    connect(&process, &QProcess::readyReadStandardError, [&process, &graxpertLog, this](){
        QString err = process.readAllStandardError();
        if (!err.isEmpty()) {
            graxpertLog.append(err + "\n");
            QString trimmed = err.trimmed();
            if (trimmed.contains("INFO") || trimmed.contains("Progress")) {
                emit processOutput(trimmed);
            } else {
                emit processOutput("LOG: " + trimmed);
            }
        }
    });

    process.start();
    
    // Wait with cancellation check (10 min timeout)
    int elapsed = 0;
    int interval = 100;
    while (process.state() != QProcess::NotRunning) {
        if (m_stop) {
            process.kill();
            process.waitForFinished();
            emit finished(output, "Process cancelled by user.");
            return;
        }
        QCoreApplication::processEvents();
        QThread::msleep(interval);
        elapsed += interval;
        if (elapsed > 600000) {  // 10 minutes
            process.kill();
            emit finished(output, "Process timed out.");
            return;
        }
    }

    if (process.exitCode() != 0) {
        QString tail = graxpertLog.trimmed().right(1000);
        errorMsg = QString("GraXpert failed (Exit Code %1): %2\n\nLog:\n%3")
                    .arg(process.exitCode())
                    .arg(process.errorString().isEmpty() ? "Unknown error" : process.errorString())
                    .arg(tail);
        emit finished(output, errorMsg);
        return;
    }

    // Output filename convention: filename_GraXpert.tiff
    QString outputFile = tempDir.filePath("input_GraXpert.tiff");
    
    if (!QFileInfo::exists(outputFile)) {
        // Fallback check
        QStringList filters; filters << "input_GraXpert.*";
        QFileInfoList found = QDir(tempDir.path()).entryInfoList(filters, QDir::Files);
        if (!found.isEmpty()) outputFile = found.first().absoluteFilePath();
        else {
            emit finished(output, "GraXpert output file not found.");
            return;
        }
    }

    // 4. Load Result via Bridge
    QString rawResult = tempDir.filePath("result.raw");
    {
        QStringList args;
        args << scriptPath << "load" << outputFile << rawResult;
        
        QProcess p;
        p.setProcessEnvironment(makePythonEnv());
        p.setProcessChannelMode(QProcess::MergedChannels);
        
        convLog.clear();
        connect(&p, &QProcess::readyReadStandardOutput, [&p, &convLog, this]() {
            QString txt = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if(!txt.isEmpty()){
                emit processOutput(txt);
                convLog.append(txt + "\n");
            }
        });
        
        p.start(pythonExe, args);
        if (!p.waitForFinished(60000)) {
            emit finished(output, "Bridge timeout loading result.\n\nLog:\n" + convLog.trimmed().right(1000));
            return;
        }
        
        QString outData = convLog.trimmed();

        if (p.exitCode() != 0 || outData.contains("Error")) {
            QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
            if (err.isEmpty() && !convLog.isEmpty()) err = convLog.trimmed().right(1000);
            emit finished(output, "Bridge error loading result:\n\n" + err);
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
            emit finished(output, "Bridge failed to provide result marker: " + outData);
            return;
        }

        QStringList parts = resultLine.mid(7).trimmed().split(QRegularExpression("\\s+"));
        if (parts.size() < 3) {
            emit finished(output, "Bridge failed to parse dimensions: " + resultLine);
            return;
        }
        
        int w = parts[0].toInt();
        int h = parts[1].toInt();
        int c = parts[2].toInt();

        if (w <= 0 || h <= 0) {
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
            emit finished(output, "Failed to read raw result.");
            return;
        }
    }
    
    emit finished(output, "");  // Success
}

// ============================================================================
// GraXpertRunner Implementation
// ============================================================================

GraXpertRunner::GraXpertRunner(QObject* parent) 
    : QObject(parent) {}

GraXpertRunner::~GraXpertRunner() {
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

bool GraXpertRunner::run(const ImageBuffer& input, ImageBuffer& output, 
                         const GraXpertParams& params, QString* errorMsg) {
    // Create thread and worker if not already created
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new GraXpertWorker();
        m_worker->moveToThread(m_thread);
        
        // Connect signals
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &GraXpertWorker::finished, this, &GraXpertRunner::onWorkerFinished);
        connect(m_worker, &GraXpertWorker::processOutput, this, &GraXpertRunner::processOutput);
        
        m_thread->start();
    }
    
    // Reset synchronization flag
    m_finished = false;
    
    // Emit process signal (queued call in worker thread)
    QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                              Q_ARG(const ImageBuffer&, input),
                              Q_ARG(const GraXpertParams&, params));
    
    // Wait for completion with event loop
    QEventLoop loop;
    connect(this, &GraXpertRunner::finished, &loop, &QEventLoop::quit);
    
    // Timeout: 15 minutes
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(15 * 60 * 1000);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
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

void GraXpertRunner::onWorkerFinished(const ImageBuffer& output, const QString& errorMsg) {
    m_output = output;
    m_errorMsg = errorMsg;
    m_finished = true;
    emit finished();
}
