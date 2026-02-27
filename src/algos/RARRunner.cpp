#include "RARRunner.h"
#include "SimpleTiffWriter.h"
#include "SimpleTiffReader.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <iostream>

RARRunner::RARRunner(QObject* parent) : QObject(parent) {}

bool RARRunner::run(const ImageBuffer& input, ImageBuffer& output, const RARParams& params, QString* errorMsg) {
    if (!QFileInfo::exists(params.modelPath)) {
        if(errorMsg) *errorMsg = "Model file not found: " + params.modelPath;
        return false;
    }
    m_stop = false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if(errorMsg) *errorMsg = "Failed to create temp dir.";
        return false;
    }

    QString inputFile = tempDir.filePath("rar_input.tif");
    // Use .raw for output to avoid TIFF parsing issues in C++ (float32 exchange)
    QString outputFile = tempDir.filePath("rar_output.raw");
    QString scriptPath = QCoreApplication::applicationDirPath() + "/scripts/rar_worker.py";

    // Adjust script path if running from build dir or installed
    if (!QFile::exists(scriptPath)) {
        // Try Resources folder (macOS DMG bundle)
        scriptPath = QCoreApplication::applicationDirPath() + "/../Resources/scripts/rar_worker.py";
    }
    if (!QFile::exists(scriptPath)) {
        // Fallback for dev environment
        scriptPath = QCoreApplication::applicationDirPath() + "/../src/scripts/rar_worker.py";
    }

    // Save Input (Float32 for precision)
    emit processOutput("Saving temp input...");
    if (!SimpleTiffWriter::write(inputFile, input.width(), input.height(), input.channels(), 
                                 SimpleTiffWriter::Format_float32, input.data(), errorMsg)) {
        return false;
    }

    QStringList args;
    args << "-I" << scriptPath 
         << "--input" << inputFile 
         << "--output" << outputFile 
         << "--model" << params.modelPath
         << "--patch" << QString::number(params.patchSize)
         << "--overlap" << QString::number(params.overlap)
         << "--provider" << params.provider;

    emit processOutput("Starting Aberration AI worker...");
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    
    // Connect output
    connect(&p, &QProcess::readyReadStandardOutput, [this, &p](){
        QByteArray data = p.readAllStandardOutput();
        QString txt = QString::fromUtf8(data).trimmed();
        if(!txt.isEmpty()){
            // Log logic
            if(txt.contains("Progress:")) {
                // Console output for CLI users (updates same line)
                std::cout << "\r" << txt.toStdString() << "    " << std::flush;
                emit processOutput(txt);
            }
            else if(txt.startsWith("RESULT")) {
                std::cout << "\n[AI] " << txt.toStdString() << std::endl;
                emit processOutput(txt);
            }
            else {
                if(txt.contains("INFO") || txt.contains("DEBUG")) {
                     std::cout << "\n[AI] " << txt.toStdString() << std::endl;
                }
                emit processOutput(txt);
            }
        }
    });

    QString pythonExe;
#if defined(Q_OS_MAC)
    // macOS: Check for bundled virtualenv in app bundle Resources
    QString bundledPython = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
    QString devPython = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#else
    // Windows: Check for bundled embeddable Python
    QString bundledPython = QCoreApplication::applicationDirPath() + "/python/python.exe";
    QString devPython = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#endif
    QString foundPython = QStandardPaths::findExecutable("python3");
    if (QFile::exists(bundledPython)) pythonExe = bundledPython;
    else if (QFile::exists(devPython)) pythonExe = devPython;
    else if (!foundPython.isEmpty()) pythonExe = foundPython;
    else pythonExe = "python3";

    p.start(pythonExe, args);
    
    // Check if it started successfully
    if (!p.waitForStarted(3000)) {
        if(errorMsg) *errorMsg = "Failed to start AI worker: " + p.errorString();
        return false;
    }
    
    // Blocking loop with cancellation
    while(p.state() != QProcess::NotRunning) {
        if (m_stop) {
            p.kill();
            p.waitForFinished();
            if(errorMsg) *errorMsg = "Aborted by user.";
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }

    if (p.exitCode() != 0) {
        if(errorMsg) *errorMsg = QString("Worker process failed (Code %1): %2")
                                    .arg(p.exitCode())
                                    .arg(p.errorString().isEmpty() ? "Unknown error" : p.errorString());
        return false;
    }
    
    if (!QFile::exists(outputFile)) {
         if(errorMsg) *errorMsg = "Output file not generated.";
         return false;
    }

    emit processOutput("Loading result...");
    
    QFile resFile(outputFile);
    if (!resFile.open(QIODevice::ReadOnly)) {
         if(errorMsg) *errorMsg = "Failed to open result file.";
         return false;
    }
    
    QByteArray blob = resFile.readAll();
    resFile.close();
    
    // Size check
    // Expected size: w * h * c * 4
    size_t expectedBytes = (size_t)input.width() * input.height() * input.channels() * sizeof(float);
    if ((size_t)blob.size() != expectedBytes) {
        if(errorMsg) *errorMsg = QString("Result size mismatch. Expected %1 bytes, got %2").arg(expectedBytes).arg(blob.size());
        return false;
    }
    
    std::vector<float> data(blob.size() / sizeof(float));
    memcpy(data.data(), blob.constData(), blob.size());
    
    // No Min/Max check needed here really, it's raw memory dump.
    emit processOutput(QString("Loaded RAW: %1 bytes").arg(blob.size()));

    output.setData(input.width(), input.height(), input.channels(), data);
    output.setMetadata(input.metadata()); // Preserve WCS and other metadata
    return true;
}
