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

    // 3. Convert Raw to TIFF using Bridge
    QString inputFile = tempDir.filePath("input.tiff");
    {
        QStringList args;
        args << scriptPath << "save" << inputFile 
             << QString::number(input.width()) << QString::number(input.height()) << QString::number(input.channels())
             << rawInputFile;
        
        QString pythonExe;
#if defined(Q_OS_MAC)
        QString bundledPython = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
        QString devPython = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#else
        QString bundledPython = QCoreApplication::applicationDirPath() + "/python/python.exe";
        QString devPython = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#endif
        QString foundPython = QStandardPaths::findExecutable("python3");
        if (QFile::exists(bundledPython)) pythonExe = bundledPython;
        else if (QFile::exists(devPython)) pythonExe = devPython;
        else if (!foundPython.isEmpty()) pythonExe = foundPython;
        else pythonExe = "python3";
        
        QProcess p;
        p.start(pythonExe, args);
        if (!p.waitForFinished(60000)) {
            emit finished(output, "Bridge timeout saving TIFF.");
            return;
        }
        
        if (p.exitCode() != 0) {
            emit finished(output, "Bridge failed to save TIFF: " + p.readAllStandardOutput());
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
    
    // Real-time output
    connect(&process, &QProcess::readyReadStandardOutput, [&process, this](){
        QString out = process.readAllStandardOutput();
        if (!out.isEmpty()) emit processOutput(out.trimmed());
    });
    connect(&process, &QProcess::readyReadStandardError, [&process, this](){
        QString err = process.readAllStandardError();
        if (!err.isEmpty()) {
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
        errorMsg = QString("GraXpert failed (Exit Code %1): %2")
                    .arg(process.exitCode())
                    .arg(process.errorString().isEmpty() ? "Unknown error" : process.errorString());
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
        
        QString pythonExe = "python";
#if defined(Q_OS_MAC)
        QString bundledPython = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
        QString devPython = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#else
        QString bundledPython = QCoreApplication::applicationDirPath() + "/python/python.exe";
        QString devPython = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#endif
        if (QFile::exists(bundledPython)) pythonExe = bundledPython;
        else if (QFile::exists(devPython)) pythonExe = devPython;

        QProcess p;
        p.start(pythonExe, args);
        if (!p.waitForFinished(60000)) {
            emit finished(output, "Bridge timeout loading result.");
            return;
        }
        
        QString outData = p.readAllStandardOutput().trimmed();
        emit processOutput("Bridge Output:\n" + outData);

        if (outData.contains("Error")) {
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
