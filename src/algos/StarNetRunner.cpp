#include "StarNetRunner.h"
#include "SimpleTiffWriter.h"
#include "SimpleTiffReader.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <QSettings>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

StarNetRunner::StarNetRunner(QObject* parent) : QObject(parent) {}

QString StarNetRunner::getExecutablePath() {
    QSettings s;
    return s.value("paths/starnet", "").toString();
}

// ----------------------------------------------------------------------------
// Mathematical Helpers for Standard MTF
// ----------------------------------------------------------------------------

static float mtf_calc(float x, float m) {
    // Basic MTF: y = (m-1)x / ((2m-1)x - m)
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    float num = (m - 1.0f) * x;
    float den = (2.0f * m - 1.0f) * x - m;
    if (std::abs(den) < 1e-9f) return 0.5f;
    return std::max(0.0f, std::min(1.0f, num / den));
}

static float mtf_calc_inv(float y, float m) {
    // Inverse: same formula structure, but check coeffs
    // y = ((m-1)x) / ((2m-1)x - m)
    // y((2m-1)x - m) = (m-1)x
    // y(2m-1)x - ym = (m-1)x
    // x(y(2m-1) - (m-1)) = ym
    // x = ym / (y(2m-1) - m + 1)
    
    // Inverse MTF calculation (Shadows=0, Highlights=1)
    // Formula derived from: num = (m * yp), den = (2m - 1)*yp - m + 1
    
    if (y <= 0.0f) return 0.0f;
    if (y >= 1.0f) return 1.0f;
    
    float num = m * y;
    float den = (2.0f * m - 1.0f) * y - m + 1.0f;
    
    if (std::abs(den) < 1e-9f) return 0.0f; 
    return std::max(0.0f, std::min(1.0f, num / den));
}

// Robust stats
static void get_median_mad(const std::vector<float>& data, int step, size_t offset, float& median, float& mad) {
    if (data.empty()) { median=0; mad=0; return; }
    
    // Sample a subset for speed if large
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

StarNetRunner::MTFParams StarNetRunner::computeMtfParams(const ImageBuffer& img) {
    MTFParams p;
    // shadows_clipping = -2.8, targetbg = 0.25
    float shadows_clipping = -2.8f;
    float targetbg = 0.25f;
    
    int c_in = img.channels();
    const auto& data = img.data();
    
    for (int c = 0; c < 3; ++c) {
        // If mono, reuse ch 0
        int ch_idx = (c_in == 1) ? 0 : c;
        if (ch_idx >= c_in) {
            // copy from 0 or default
            p.s[c] = p.s[0]; p.m[c] = p.m[0]; p.h[c] = p.h[0];
            continue;
        }

        float med, mad;
        get_median_mad(data, c_in, ch_idx, med, mad);
        if (mad == 0) mad = 0.001f;

        // Inverted logic: (med > 0.5)
        bool is_inv = (med > 0.5f);
        
        float s_val, m_val, h_val;
        
        if (!is_inv) {
             float c0 = std::max(0.0f, med + shadows_clipping * mad);
             float m2 = med - c0;
             s_val = c0;
             h_val = 1.0f;
             m_val = mtf_calc(m2, targetbg);
        } else {
             // Inverted case
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

void StarNetRunner::applyMtf(ImageBuffer& img, const MTFParams& p) {
    int channels = img.channels();
    auto& data = img.data();
    size_t total = data.size() / channels;
    
    for (size_t i = 0; i < total; ++i) {
        for (int c = 0; c < channels; ++c) {
            float v = data[i * channels + c];
            float s = p.s[c >= 3 ? 0 : c];
            float m = p.m[c >= 3 ? 0 : c];
            float h = p.h[c >= 3 ? 0 : c];
            
            // Normalize to [s, h] domain being [0, 1]
            float range = h - s;
            if (range < 1e-9f) range = 1e-9f;
            
            float vn = (v - s) / range;
            vn = std::max(0.0f, std::min(1.0f, vn));
            
            float y = mtf_calc(vn, m);
            data[i * channels + c] = y; // Output in [0,1]
        }
    }
}

void StarNetRunner::invertMtf(ImageBuffer& img, const MTFParams& p) {
    int channels = img.channels();
    auto& data = img.data();
    size_t total = data.size() / channels;
    
    for (size_t i = 0; i < total; ++i) {
        for (int c = 0; c < channels; ++c) {
            float y = data[i * channels + c]; // Starless result in [0,1]
            float s = p.s[c >= 3 ? 0 : c];
            float m = p.m[c >= 3 ? 0 : c];
            float h = p.h[c >= 3 ? 0 : c];
            
            // Inverse MTF
            float vn = mtf_calc_inv(y, m);
            
            // Denormalize: v = vn * range + s
            float range = h - s;
            float v = vn * range + s;
            
            data[i * channels + c] = std::max(0.0f, std::min(1.0f, v));
        }
    }
}

bool StarNetRunner::run(const ImageBuffer& input, ImageBuffer& output, const StarNetParams& params, QString* errorMsg) {
    QString exe = getExecutablePath();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        if (errorMsg) *errorMsg = "StarNet executable paths not configured. Please set it in settings.";
        return false;
    }
    m_stop = false;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
         if (errorMsg) *errorMsg = "Failed to create temporary directory.";
         return false;
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
        if (errorMsg) *errorMsg = "Unsupported channel count for StarNet.";
        return false;
    }

    emit processOutput("Saving temporary input TIFF (16-bit)...");
    if (!SimpleTiffWriter::write(inputFile, w, h, exportChannels, SimpleTiffWriter::Format_uint16, exportData, errorMsg)) {
        return false;
    }

    emit processOutput("Running StarNet++...");
    QStringList args;
    if (exe.contains("starnet2", Qt::CaseInsensitive)) {
        args << inputFile << outputFile << QString::number(params.stride);
    } else {
        args << inputFile << outputFile << QString::number(params.stride);
    }

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels); 
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());
    
    connect(&p, &QProcess::readyReadStandardOutput, [&p, this](){
        QString txt = p.readAllStandardOutput().trimmed();
        if (!txt.isEmpty()) emit processOutput(txt);
    });

    if (m_stop) return false;
    p.start(exe, args);
    
    // Thread-safe wait loop with cancellation
    while(p.state() != QProcess::NotRunning) {
        if (m_stop) {
            p.kill();
            p.waitForFinished();
            if (errorMsg) *errorMsg = "StarNet process cancelled by user.";
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }
    
    if (p.exitCode() != 0) {
        if (errorMsg) *errorMsg = "StarNet process failed.";
        return false;
    }
    
    if (!QFile::exists(outputFile)) {
        if (errorMsg) *errorMsg = "StarNet did not produce an output file.";
        return false;
    }

    emit processOutput("Converting StarNet output (via Python Bridge)...");
    
    // We use a small python script to convert the potentially compressed TIFF to RAW
    // This avoids implementing LZW/Deflate in C++ and uses the installed tifffile env.
    // We use a small python script to convert the potentially compressed TIFF to RAW
    // This avoids implementing LZW/Deflate in C++ and uses the installed tifffile env.
    QString converterScript = QCoreApplication::applicationDirPath() + "/scripts/starnet_converter.py";
    if (!QFile::exists(converterScript)) {
        converterScript = QCoreApplication::applicationDirPath() + "/../src/scripts/starnet_converter.py";
    }

    if (!QFile::exists(converterScript)) {
         if (errorMsg) *errorMsg = "Converter script not found.";
         return false;
    }
    
    QString rawOutput = tempDir.filePath("starnet_output.raw");
    QStringList convArgs;
    convArgs << converterScript << outputFile << rawOutput;
    
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

    QProcess conv;
    conv.start(pythonExe, convArgs);
    conv.waitForFinished();
    
    if (conv.exitCode() != 0) {
        if(errorMsg) *errorMsg = "Output conversion failed: " + conv.readAllStandardOutput() + conv.readAllStandardError();
        return false;
    }
    
    emit processOutput("Loading RAW result...");
    QFile rawFile(rawOutput);
    if (!rawFile.open(QIODevice::ReadOnly)) {
         if(errorMsg) *errorMsg = "Failed to open Converted RAW file.";
         return false;
    }
    QByteArray blob = rawFile.readAll();
    rawFile.close();
    
    // Validate size
    // We expect size to match padded W * H * C
    // Note: 'w', 'h', 'c' are already defined from input setup.
    // Ensure we use the current 'working' dimensions.
    
    // StarNet sometimes outputs 3 channels even for mono input? 
    // If we duplicated mono to RGB for input, Output will be RGB.
    // So 'working' has C=3 if we did that export logic earlier.
    
    // Check if 'c' needs update if we modified working?
    // 'c' was set from working.channels() earlier.
    
    
    // Verify size against original input intent.
    
    size_t expectedBytes = (size_t)w * h * 3 * sizeof(float);
    // if (c==1) exportData.resize(w*h*3)...
    // So if input was 1, we made it 3.
    // So output should be 3.
    
    if ((size_t)blob.size() != expectedBytes) {
        // Fallback: Maybe StarNet preserved mono?
        size_t expectedMono = (size_t)w * h * 1 * sizeof(float);
        if ((size_t)blob.size() == expectedMono) {
             // It's mono
             c = 1;
        } else {
             if(errorMsg) *errorMsg = QString("Size mismatch. Exp %1, Got %2").arg(expectedBytes).arg(blob.size());
             return false;
        }
    } else {
        c = 3; // Confirmed RGB
    }
    
    std::vector<float> outData(blob.size() / sizeof(float));
    std::cerr << "Memcpy RAW data: " << outData.size() << " floats" << std::endl;
    emit processOutput(QString("Memcpy RAW data: %1 floats").arg(outData.size()));
    memcpy(outData.data(), blob.constData(), blob.size());
    
    // Normalize if needed (StarNet output likely matches input scale 0..65535)
    float dMax = -1e9f;
    for(float v : outData) if(v > dMax) dMax = v;
    std::cerr << "Output Max Value: " << dMax << std::endl;
    emit processOutput(QString("Output Max Value: %1").arg(dMax));
    
    if (dMax > 1.0f) {
        emit processOutput("Normalizing output (assuming 16-bit range)...");
        std::cerr << "Normalizing output..." << std::endl;
        float scale = 1.0f / 65535.0f; 
        for(float& v : outData) v *= scale;
    }

    ImageBuffer starlessLocal;
    std::cerr << "Setting data: " << w << "x" << h << " ch=" << c << std::endl;
    emit processOutput(QString("Setting data: %1x%2 ch=%3").arg(w).arg(h).arg(c));
    starlessLocal.setData(w, h, c, outData);
    // REMOVED dangerous processEvents()

    std::cerr << "Checking mono logic. Input channels: " << input.channels() << std::endl;

    // If source was mono, revert to mono
    // Use input.channels() because 'working' might have been set to 3 for RGB output
    if (input.channels() == 1 && c == 3) {
        std::cerr << "Reverting to mono..." << std::endl;
        emit processOutput("Reverting to mono...");
        std::vector<float> monoData(w * h);
        for (int i = 0; i < w*h; ++i) {
             // Simple average
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
        
        // If we generated mask, we need to handle it carefully.
        // Mask = Original - Starless.
        // Should we compute mask on linear or stretched?
        // Usually mask is computed on the stretched, then unstretched? 
        // No, typically Mask = LinearOriginal - LinearStarless
    }
    // Crop back if padded
    if (didPad) {
        std::cerr << "Cropping padding to " << origW << "x" << origH << std::endl;
        emit processOutput("Cropping padding...");
        starlessLocal.crop(0, 0, origW, origH);
    }

    std::cerr << "Assigning to output/result..." << std::endl;
    output = starlessLocal;
    output.setMetadata(input.metadata()); // Preserve WCS and other metadata
    std::cerr << "Run completed successfully." << std::endl;

    // Note: Star Mask generation is handled by the caller (StarNetDialog)
    // because it has access to both original and starless.

    return true;
}
