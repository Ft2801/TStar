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

GraXpertRunner::GraXpertRunner(QObject* parent) : QObject(parent) {}

QString GraXpertRunner::getExecutablePath() {
    QSettings settings;
    return settings.value("paths/graxpert").toString();
}

bool GraXpertRunner::run(const ImageBuffer& input, ImageBuffer& output, const GraXpertParams& params, QString* errorMsg) {
    QString exe = getExecutablePath();
    if (exe.isEmpty()) {
        if (errorMsg) *errorMsg = "GraXpert path not set.";
        return false;
    }
    m_stop = false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorMsg) *errorMsg = "Failed to create temp dir.";
        return false;
    }

    // 1. Ensure bridge script path
    QString scriptPath = QCoreApplication::applicationDirPath() + "/scripts/graxpert_bridge.py";
    if (!QFile::exists(scriptPath)) {
        // Try Resources folder (macOS DMG bundle)
        scriptPath = QCoreApplication::applicationDirPath() + "/../Resources/scripts/graxpert_bridge.py";
    }
    if (!QFile::exists(scriptPath)) {
        // Fallback for dev environment
        scriptPath = QCoreApplication::applicationDirPath() + "/../src/scripts/graxpert_bridge.py";
    }

    if (!QFile::exists(scriptPath)) {
        if (errorMsg) *errorMsg = "Bridge script not found at: " + scriptPath;
        return false;
    }

    // 2. Write Input to Raw Float
    QString rawInputFile = tempDir.filePath("input.raw");
    {
        QFile raw(rawInputFile);
        if(!raw.open(QIODevice::WriteOnly)) {
            if(errorMsg) *errorMsg = "Failed to write raw input.";
            return false;
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
        
        QProcess p;
        // Check for bundled python - cross-platform
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
        
        p.start(pythonExe, args);
        p.waitForFinished();
        if (p.exitCode() != 0) {
            if(errorMsg) *errorMsg = "Bridge failed to save TIFF: " + p.readAllStandardOutput();
            return false;
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
        if(!out.isEmpty()) emit processOutput(out.trimmed());
    });
    connect(&process, &QProcess::readyReadStandardError, [&process, this](){
        QString err = process.readAllStandardError();
        if(!err.isEmpty()) {
            QString trimmed = err.trimmed();
            if (trimmed.contains("INFO") || trimmed.contains("Progress")) emit processOutput(trimmed);
            else emit processOutput("LOG: " + trimmed);
        }
    });

    process.start();
    
    // Blocking wait with cancellation check (for worker thread)
    while(process.state() != QProcess::NotRunning) {
        if (m_stop) {
            process.kill();
            process.waitForFinished();
            if(errorMsg) *errorMsg = "Process cancelled by user.";
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }

    if (process.exitCode() != 0) {
        if (errorMsg) *errorMsg = QString("GraXpert failed (Exit Code %1): %2")
                                    .arg(process.exitCode())
                                    .arg(process.errorString().isEmpty() ? "Unknown error" : process.errorString());
        return false;
    }

    // Output filename convention: filename_GraXpert.tiff
    QString outputFile = tempDir.filePath("input_GraXpert.tiff");
    
    if (!QFileInfo::exists(outputFile)) {
        // Fallback check
        QStringList filters; filters << "input_GraXpert.*";
        QFileInfoList found = QDir(tempDir.path()).entryInfoList(filters, QDir::Files);
        if(!found.isEmpty()) outputFile = found.first().absoluteFilePath();
        else {
            if (errorMsg) *errorMsg =  "GraXpert output file not found.";
            return false;
        }
    }

    // 4. Load Result via Bridge
    QString rawResult = tempDir.filePath("result.raw");
    {
        QStringList args;
        args << scriptPath << "load" << outputFile << rawResult;
        
        // Check for bundled python - cross-platform
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
        p.waitForFinished();
        
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
    }
    
    output.setMetadata(input.metadata()); // Preserve WCS and other metadata
    return true;
}
