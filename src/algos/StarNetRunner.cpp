#include "StarNetRunner.h"
#include "io/SimpleTiffWriter.h"
#include "io/SimpleTiffReader.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <QSettings>
#include <QDebug>
#include <QStandardPaths>
#include <QEventLoop>
#include <QTimer>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

// ============================================================================
// Math Helpers
// ============================================================================

static float mtf_calc(float x, float m) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    float num = (m - 1.0f) * x;
    float den = (2.0f * m - 1.0f) * x - m;
    if (std::abs(den) < 1e-9f) return 0.5f;
    return std::max(0.0f, std::min(1.0f, num / den));
}

static float mtf_calc_inv(float y, float m) {
    if (y <= 0.0f) return 0.0f;
    if (y >= 1.0f) return 1.0f;
    
    float num = m * y;
    float den = (2.0f * m - 1.0f) * y - m + 1.0f;
    
    if (std::abs(den) < 1e-9f) return 0.0f; 
    return std::max(0.0f, std::min(1.0f, num / den));
}

static void get_median_mad(const std::vector<float>& data, int step, size_t offset, float& median, float& mad) {
    if (data.empty()) { median=0; mad=0; return; }
    
    std::vector<float> samples;
    samples.reserve(data.size() / step / 10 + 1000);
    
    size_t total = data.size() / step;
    size_t stride = 1;
    if (total > 200000) stride = total / 100000;
    
    for (size_t i = 0; i < total; i += stride) {
        samples.push_back(data[i * step + offset]);
    }
    
    if (samples.empty()) { median=0; mad=0; return; }
    
    size_t n = samples.size();
    auto mid = samples.begin() + n/2;
    std::nth_element(samples.begin(), mid, samples.end());
    median = *mid;
    
    std::vector<float> absdevs;
    absdevs.reserve(n);
    for (float v : samples) absdevs.push_back(std::abs(v - median));
    
    mid = absdevs.begin() + n/2;
    std::nth_element(absdevs.begin(), mid, absdevs.end());
    float mad_raw = *mid;
    
    mad = mad_raw * 1.4826f;
}

// ============================================================================
// StarNetWorker Implementation
// ============================================================================

StarNetWorker::StarNetWorker(QObject* parent) : QObject(parent) {}

QString StarNetWorker::getExecutablePath() {
    QSettings s;
    return s.value("paths/starnet", "").toString();
}

StarNetWorker::MTFParams StarNetWorker::computeMtfParams(const ImageBuffer& img) {
    MTFParams p;
    float shadows_clipping = -2.8f;
    float targetbg = 0.25f;
    
    int c_in = img.channels();
    const auto& data = img.data();
    
    for (int c = 0; c < 3; ++c) {
        int ch_idx = (c_in == 1) ? 0 : c;
        if (ch_idx >= c_in) {
            p.s[c] = p.s[0]; p.m[c] = p.m[0]; p.h[c] = p.h[0];
            continue;
        }

        float med, mad;
        get_median_mad(data, c_in, ch_idx, med, mad);
        if (mad == 0) mad = 0.001f;

        bool is_inv = (med > 0.5f);
        
        float s_val, m_val, h_val;
        
        if (!is_inv) {
            float c0 = std::max(0.0f, med + shadows_clipping * mad);
            float m2 = med - c0;
            s_val = c0;
            h_val = 1.0f;
            m_val = mtf_calc(m2, targetbg);
        } else {
            float c1 = std::min(1.0f, med - shadows_clipping * mad);
            float m2 = c1 - med;
            s_val = 0.0f;
            h_val = c1;
            m_val = 1.0f - mtf_calc(m2, targetbg);
        }
        
        p.s[c] = s_val;
        p.m[c] = m_val;
        p.h[c] = h_val;
    }
    
    return p;
}

void StarNetWorker::applyMtf(ImageBuffer& img, const MTFParams& p) {
    int channels = img.channels();
    auto& data = img.data();
    size_t total = data.size() / channels;
    
    for (size_t i = 0; i < total; ++i) {
        for (int c = 0; c < channels; ++c) {
            float v = data[i * channels + c];
            float s = p.s[c >= 3 ? 0 : c];
            float m = p.m[c >= 3 ? 0 : c];
            float h = p.h[c >= 3 ? 0 : c];
            
            float range = h - s;
            if (range < 1e-9f) range = 1e-9f;
            
            float vn = (v - s) / range;
            vn = std::max(0.0f, std::min(1.0f, vn));
            
            float y = mtf_calc(vn, m);
            data[i * channels + c] = y;
        }
    }
}

void StarNetWorker::invertMtf(ImageBuffer& img, const MTFParams& p) {
    int channels = img.channels();
    auto& data = img.data();
    size_t total = data.size() / channels;
    
    for (size_t i = 0; i < total; ++i) {
        for (int c = 0; c < channels; ++c) {
            float y = data[i * channels + c];
            float s = p.s[c >= 3 ? 0 : c];
            float m = p.m[c >= 3 ? 0 : c];
            float h = p.h[c >= 3 ? 0 : c];
            
            float vn = mtf_calc_inv(y, m);
            
            float range = h - s;
            float v = vn * range + s;
            
            data[i * channels + c] = std::max(0.0f, std::min(1.0f, v));
        }
    }
}

void StarNetWorker::process(const ImageBuffer& input, const StarNetParams& params) {
    QString errorMsg;
    ImageBuffer output;
    
    if (input.data().empty()) {
        emit finished(output, "Input buffer is empty");
        return;
    }
    
    QString exe = getExecutablePath();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        emit finished(output, "StarNet executable not configured. Please set it in settings.");
        return;
    }
    m_stop = false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        emit finished(output, "Failed to create temporary directory.");
        return;
    }

    // Work on a copy
    ImageBuffer working = input;

    // Pad if necessary
    int origW = working.width();
    int origH = working.height();
    int padW = std::max(origW, 512); 
    int padH = std::max(origH, 512); 
    if (padW % 32 != 0) padW += (32 - (padW % 32));
    if (padH % 32 != 0) padH += (32 - (padH % 32));

    bool didPad = (padW != origW || padH != origH);
    if (didPad) {
        emit processOutput(QString("Padding image to %1x%2 for StarNet stability...").arg(padW).arg(padH));
        std::vector<float> pData(padW * padH * working.channels(), 0.0f);
        const auto& sData = working.data();
        int ch = working.channels();
        for (int y = 0; y < padH; ++y) {
            for (int x = 0; x < padW; ++x) {
                int sx = std::min(x, origW - 1);
                int sy = std::min(y, origH - 1);
                for (int c = 0; c < ch; ++c) {
                    pData[(y * padW + x) * ch + c] = sData[(sy * origW + sx) * ch + c];
                }
            }
        }
        working.setData(padW, padH, ch, pData);
    }
    
    MTFParams mtfP;
    bool didStretch = false;
    if (params.isLinear) {
        emit processOutput("Linear data detected. Calculating Auto-Stretch parameters...");
        mtfP = computeMtfParams(working);
        applyMtf(working, mtfP);
        didStretch = true;
    }

    QString inputFile = tempDir.filePath("starnet_input.tif");
    QString outputFile = tempDir.filePath("starnet_output.tif");
    
    int w = working.width();
    int h = working.height();
    int c = working.channels();
    
    std::vector<float> exportData;
    int exportChannels = 3; 
    
    if (c == 1) {
        exportData.resize(w * h * 3);
        const auto& d = working.data();
        for (size_t i = 0; i < d.size(); ++i) {
            exportData[i*3+0] = d[i];
            exportData[i*3+1] = d[i];
            exportData[i*3+2] = d[i]; 
        }
    } else if (c == 3) {
        exportData = working.data();
    } else {
        emit finished(output, "Unsupported channel count for StarNet.");
        return;
    }

    emit processOutput("Saving temporary input TIFF (16-bit)...");
    if (!SimpleTiffWriter::write(inputFile, w, h, exportChannels, SimpleTiffWriter::Format_uint16, exportData, &errorMsg)) {
        emit finished(output, errorMsg);
        return;
    }

    emit processOutput("Running StarNet++...");
    QStringList args;
#if defined(Q_OS_MAC)
    args << "-i" << inputFile << "-o" << outputFile << "-s" << QString::number(params.stride);
#else
    args << inputFile << outputFile << QString::number(params.stride);
#endif

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels); 
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());
    
    connect(&p, &QProcess::readyReadStandardOutput, [&p, this](){
        QString txt = p.readAllStandardOutput().trimmed();
        if (!txt.isEmpty()) emit processOutput(txt);
    });

    p.start(exe, args);
    
    // Wait loop with cancellation (10 min timeout)
    int elapsed = 0;
    int interval = 100;
    while (p.state() != QProcess::NotRunning) {
        if (m_stop) {
            p.kill();
            p.waitForFinished();
            emit finished(output, "StarNet process cancelled by user.");
            return;
        }
        QCoreApplication::processEvents();
        QThread::msleep(interval);
        elapsed += interval;
        if (elapsed > 600000) {
            p.kill();
            emit finished(output, "StarNet process timed out.");
            return;
        }
    }
    
    if (p.exitCode() != 0) {
        errorMsg = QString("StarNet process failed (Code %1): %2")
                    .arg(p.exitCode())
                    .arg(p.errorString().isEmpty() ? "Unknown error" : p.errorString());
        emit finished(output, errorMsg);
        return;
    }
    
    if (!QFile::exists(outputFile)) {
        emit finished(output, "StarNet did not produce an output file.");
        return;
    }

    emit processOutput("Converting StarNet output (via Python Bridge)...");
    
    QString converterScript = QCoreApplication::applicationDirPath() + "/scripts/starnet_converter.py";
    if (!QFile::exists(converterScript)) {
        converterScript = QCoreApplication::applicationDirPath() + "/../Resources/scripts/starnet_converter.py";
    }
    if (!QFile::exists(converterScript)) {
        converterScript = QCoreApplication::applicationDirPath() + "/../src/scripts/starnet_converter.py";
    }

    if (!QFile::exists(converterScript)) {
        emit finished(output, "Converter script not found.");
        return;
    }
    
    QString rawOutput = tempDir.filePath("starnet_output.raw");
    QStringList convArgs;
    convArgs << "-I" << converterScript << outputFile << rawOutput;
    
    // Verify the python binary actually loads before committing to it.
    // On macOS, dyld may hard-crash the venv python3 (exit 6) if Python.framework
    // is not at the Homebrew Cellar path baked in at venv creation time.
    auto pythonWorks = [](const QString& exe) -> bool {
        if (!QFile::exists(exe)) return false;
        QProcess test;
        test.start(exe, QStringList() << "-c" << "import sys; sys.exit(0)");
        return test.waitForFinished(5000) && test.exitCode() == 0;
    };

    QString pythonExe;
#if defined(Q_OS_MAC)
    QString bundledPython = QCoreApplication::applicationDirPath() + "/../Resources/python_venv/bin/python3";
    QString devPython = QCoreApplication::applicationDirPath() + "/../../deps/python_venv/bin/python3";
#else
    QString bundledPython = QCoreApplication::applicationDirPath() + "/python/python.exe";
    QString devPython = QCoreApplication::applicationDirPath() + "/../deps/python/python.exe";
#endif
    QString foundPython = QStandardPaths::findExecutable("python3");
    if (pythonWorks(bundledPython)) pythonExe = bundledPython;
    else if (pythonWorks(devPython)) pythonExe = devPython;
    else if (!foundPython.isEmpty()) pythonExe = foundPython;
    else pythonExe = "python3";

    QProcess conv;
    // Inject bundled venv site-packages into PYTHONPATH (macOS: venv symlink may be broken after packaging)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#if defined(Q_OS_MAC)
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
        conv.setProcessEnvironment(env);
    }
    conv.start(pythonExe, convArgs);
    if (!conv.waitForFinished(60000)) {
        emit finished(output, "Output conversion timed out.");
        return;
    }
    
    if (conv.exitCode() != 0) {
        emit finished(output, "Output conversion failed: " + conv.readAllStandardOutput() + conv.readAllStandardError());
        return;
    }
    
    emit processOutput("Loading RAW result...");
    QFile rawFile(rawOutput);
    if (!rawFile.open(QIODevice::ReadOnly)) {
        emit finished(output, "Failed to open converted RAW file.");
        return;
    }
    QByteArray blob = rawFile.readAll();
    rawFile.close();
    
    size_t expectedBytes = (size_t)w * h * 3 * sizeof(float);
    
    if ((size_t)blob.size() != expectedBytes) {
        size_t expectedMono = (size_t)w * h * 1 * sizeof(float);
        if ((size_t)blob.size() == expectedMono) {
            c = 1;
        } else {
            emit finished(output, QString("Size mismatch. Exp %1, Got %2").arg(expectedBytes).arg(blob.size()));
            return;
        }
    } else {
        c = 3;
    }
    
    std::vector<float> outData(blob.size() / sizeof(float));
    emit processOutput(QString("Memcpy RAW data: %1 floats").arg(outData.size()));
    memcpy(outData.data(), blob.constData(), blob.size());
    
    // Normalize if needed
    float dMax = -1e9f;
    for (float v : outData) if (v > dMax) dMax = v;
    emit processOutput(QString("Output Max Value: %1").arg(dMax));
    
    if (dMax > 1.0f) {
        emit processOutput("Normalizing output (assuming 16-bit range)...");
        float scale = 1.0f / 65535.0f; 
        for (float& v : outData) v *= scale;
    }

    ImageBuffer starlessLocal;
    emit processOutput(QString("Setting data: %1x%2 ch=%3").arg(w).arg(h).arg(c));
    starlessLocal.setData(w, h, c, outData);

    // If source was mono, revert to mono
    if (input.channels() == 1 && c == 3) {
        emit processOutput("Reverting to mono...");
        std::vector<float> monoData(w * h);
        for (int i = 0; i < w*h; ++i) {
            monoData[i] = (outData[i*3] + outData[i*3+1] + outData[i*3+2]) / 3.0f;
        }
        ImageBuffer mono;
        mono.setData(w, h, 1, monoData);
        starlessLocal = mono;
    }

    // Invert Stretch if needed
    if (didStretch) {
        emit processOutput("Inverting Auto-Stretch...");
        invertMtf(starlessLocal, mtfP);
    }
    
    // Crop back if padded
    if (didPad) {
        emit processOutput("Cropping padding...");
        starlessLocal.crop(0, 0, origW, origH);
    }

    output = starlessLocal;
    output.setMetadata(input.metadata());

    emit finished(output, "");  // Success
}

// ============================================================================
// StarNetRunner Implementation
// ============================================================================

StarNetRunner::StarNetRunner(QObject* parent) 
    : QObject(parent) {}

StarNetRunner::~StarNetRunner() {
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

bool StarNetRunner::run(const ImageBuffer& input, ImageBuffer& output, 
                        const StarNetParams& params, QString* errorMsg) {
    // Create thread and worker if not already created
    if (!m_thread) {
        m_thread = new QThread(this);
        m_worker = new StarNetWorker();
        m_worker->moveToThread(m_thread);
        
        // Connect signals
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &StarNetWorker::finished, this, &StarNetRunner::onWorkerFinished);
        connect(m_worker, &StarNetWorker::processOutput, this, &StarNetRunner::processOutput);
        
        m_thread->start();
    }
    
    // Reset synchronization flag
    m_finished = false;
    
    // Emit process signal (queued call in worker thread)
    QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                              Q_ARG(const ImageBuffer&, input),
                              Q_ARG(const StarNetParams&, params));
    
    // Wait for completion with event loop
    QEventLoop loop;
    connect(this, &StarNetRunner::finished, &loop, &QEventLoop::quit);
    
    // Timeout: 15 minutes (allow StarNet longer for large images)
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

void StarNetRunner::onWorkerFinished(const ImageBuffer& output, const QString& errorMsg) {
    m_output = output;
    m_errorMsg = errorMsg;
    m_finished = true;
    emit finished();
}
