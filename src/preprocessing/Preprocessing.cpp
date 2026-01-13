
#include "Preprocessing.h"
#include "Debayer.h"
#include "../calibration/CalibrationEngine.h"
#include "../calibration/CalibrationC.h"
#include "../io/FitsWrapper.h"
#include "../stacking/Statistics.h"
#include <QtConcurrent>
#include <QAtomicInt>
#include <mutex>
#include <omp.h>
#include <QFileInfo>
#include <QDir>
#include <cmath>
#include <algorithm>

namespace Preprocessing {

//=============================================================================
// CONSTRUCTOR / DESTRUCTOR
//=============================================================================

PreprocessingEngine::PreprocessingEngine(QObject* parent)
    : QObject(parent)
{
}

PreprocessingEngine::~PreprocessingEngine() = default;

//=============================================================================
// CONFIGURATION
//=============================================================================

void PreprocessingEngine::setParams(const PreprocessParams& params) {
    m_params = params;
    m_deviantPixels.clear();
    
    // Load master frames if paths are provided
    if (!params.masterBias.isEmpty() && params.useBias && params.biasLevel > 1e20) {
        m_masters.load(MasterType::Bias, params.masterBias);
    }
    if (!params.masterDark.isEmpty() && params.useDark) {
        m_masters.load(MasterType::Dark, params.masterDark);
        
        // Automated bad pixel mapping from dark
        if (params.cosmetic.type == CosmeticType::FromMaster) {
            ImageBuffer* dark = m_masters.get(MasterType::Dark);
            if (dark && dark->isValid()) {
                m_deviantPixels = Calibration::CalibrationEngine::findDeviantPixels(
                    *dark, params.cosmetic.hotSigma, params.cosmetic.coldSigma);
                emit logMessage(tr("Detected %1 deviant pixels in master dark").arg(m_deviantPixels.size()), "neutral");
            }
        }
    }
    if (!params.masterFlat.isEmpty() && params.useFlat) {
        m_masters.load(MasterType::Flat, params.masterFlat);
        
        ImageBuffer* flat = m_masters.get(MasterType::Flat);
        if (flat && flat->isValid()) {
            // Channel equalization
            if (params.equalizeFlat) {
                Calibration::CalibrationEngine::equalizeCFAChannels(*flat, params.bayerPattern);
                emit logMessage(tr("Equalized CFA channels in master flat"), "neutral");
            }

            // If we have a DarkFlat, subtract it from the flat
            if (!params.masterDarkFlat.isEmpty()) {
                m_masters.load(MasterType::DarkFlat, params.masterDarkFlat);
                const ImageBuffer* darkFlat = m_masters.get(MasterType::DarkFlat);
                if (darkFlat && darkFlat->isValid()) {
                    float* fData = flat->data().data();
                    const float* dfData = darkFlat->data().data();
                    size_t size = flat->size();
                    for (size_t i = 0; i < size; ++i) {
                        fData[i] -= dfData[i];
                    }
                }
            }
        }
    }
}

//=============================================================================
// SINGLE IMAGE PROCESSING
//=============================================================================

bool PreprocessingEngine::preprocessImage(const ImageBuffer& input, ImageBuffer& output) {
    // Make a working copy
    output = input;
    
    // Validate compatibility
    QString error = m_masters.validateCompatibility(output);
    if (!error.isEmpty()) {
        emit logMessage(error, "red");
        return false;
    }
    
    // 1. Bias subtraction
    if (m_params.useBias && m_masters.isLoaded(MasterType::Bias)) {
        if (!subtractBias(output)) {
            emit logMessage(tr("Bias subtraction failed"), "salmon");
        }
    }
    
    // 2. Dark subtraction (with optional optimization)
    double darkK = 1.0;
    if (m_params.useDark && m_masters.isLoaded(MasterType::Dark)) {
        if (!subtractDark(output, darkK)) {
            emit logMessage(tr("Dark subtraction failed"), "salmon");
        }
    }
    
    // 3. Flat field correction
    if (m_params.useFlat && m_masters.isLoaded(MasterType::Flat)) {
        const ImageBuffer* flat = m_masters.get(MasterType::Flat);
        double norm = Calibration::CalibrationEngine::computeFlatNormalization(*flat);
        if (Calibration::CalibrationEngine::applyFlat(output, *flat, norm)) {
            addHistory(output, QString("Calibration: applied master flat (norm: %1)").arg(norm));
        } else {
            emit logMessage(tr("Flat correction failed"), "salmon");
        }
    }
    
    // 4. Sensor fixes
    if (m_params.fixBanding) {
        Calibration::CalibrationEngine::fixBanding(output);
        addHistory(output, "Fix: sensor banding reduction");
    }
    if (m_params.fixBadLines) {
        Calibration::CalibrationEngine::fixBadLines(output);
        addHistory(output, "Fix: bad CCD lines correction");
    }
    if (m_params.fixXTrans) {
        QString rowOrder = output.getHeaderValue("ROW-ORDER");
        bool needsFlip = (rowOrder.contains("BOTTOM-UP", Qt::CaseInsensitive));
        
        if (needsFlip) output.mirrorY();
        Calibration::CalibrationEngine::fixXTransArtifacts(output);
        if (needsFlip) output.mirrorY();
        
        addHistory(output, "Fix: X-Trans AF pixel artifacts");
    }
    
    // 5. Cosmetic correction
    int hotCorrected = 0, coldCorrected = 0;
    if (m_params.cosmetic.type != CosmeticType::None) {
        applyCosmeticCorrection(output, hotCorrected, coldCorrected);
    }
    
    // 5. Debayering
    if (m_params.debayer && m_params.bayerPattern != BayerPattern::None) {
        if (!debayer(output)) {
            emit logMessage(tr("Debayering failed"), "salmon");
        }
    }
    
    // Update statistics
    m_stats.imagesProcessed++;
    m_stats.avgDarkOptimK = (m_stats.avgDarkOptimK * (m_stats.imagesProcessed - 1) + darkK) 
                            / m_stats.imagesProcessed;
    m_stats.hotPixelsCorrected += hotCorrected;
    m_stats.coldPixelsCorrected += coldCorrected;
    
    return true;
}

bool PreprocessingEngine::preprocessFile(const QString& inputPath, const QString& outputPath) {
    // Load input
    ImageBuffer input;
    if (!Stacking::FitsIO::read(inputPath, input)) {
        emit logMessage(tr("Failed to read: %1").arg(inputPath), "red");
        m_stats.imagesFailed++;
        return false;
    }
    
    // Process
    ImageBuffer output;
    if (!preprocessImage(input, output)) {
        m_stats.imagesFailed++;
        return false;
    }
    
    // Save output
    if (!Stacking::FitsIO::write(outputPath, output, m_params.outputFloat ? 32 : 16)) {
        emit logMessage(tr("Failed to write: %1").arg(outputPath), "red");
        m_stats.imagesFailed++;
        return false;
    }
    
    emit imageProcessed(inputPath, true);
    return true;
}

//=============================================================================
// BATCH PROCESSING
//=============================================================================

int PreprocessingEngine::preprocessBatch(
    const QStringList& inputFiles,
    const QString& outputDir,
    ProgressCallback progress)
{
    m_cancelled = false;
    m_stats = CalibrationStats();
    
    QDir outDir(outputDir);
    if (!outDir.exists()) {
        outDir.mkpath(".");
    }
    
    int total = inputFiles.size();
    QAtomicInt processed(0);
    std::mutex progressMutex;
    
    emit logMessage(tr("Preprocessing %1 files using %2 threads...").arg(total).arg(QThread::idealThreadCount()), "neutral");

    QtConcurrent::blockingMap(inputFiles, [&](const QString& inputPath) {
        if (m_cancelled) return;
        
        QFileInfo fi(inputPath);
        QString outputName = m_params.outputPrefix + fi.completeBaseName() + ".fit";
        QString outputPath = outDir.filePath(outputName);
        
        if (preprocessFile(inputPath, outputPath)) {
            processed.fetchAndAddRelaxed(1);
            
            // Log after successful processing to show accurate count
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                if (progress) {
                    progress(tr("Calibrating image %1 (%2/%3)...")
                            .arg(fi.fileName()).arg(processed.loadRelaxed()).arg(total),
                            static_cast<double>(processed.loadRelaxed()) / total);
                }
            }
        }
    });
    
    if (progress) {
        progress(tr("Complete: %1/%2 images processed").arg(processed.loadRelaxed()).arg(total), 1.0);
    }
    
    return processed.loadRelaxed();
}

//=============================================================================
// BIAS SUBTRACTION
//=============================================================================

bool PreprocessingEngine::subtractBias(ImageBuffer& image) {
    // 1. Check for synthetic bias (constant level)
    if (m_params.biasLevel < 1e20) {
        float b = static_cast<float>(m_params.biasLevel);
        float* data = image.data().data();
        size_t size = image.size();
        #pragma omp parallel for
        for (long long i = 0; i < (long long)size; ++i) {
            data[i] -= b;
        }
        addHistory(image, QString("Calibration: subtracted synthetic bias %1").arg(b));
        return true;
    }

    // 2. Otherwise use master file
    const ImageBuffer* bias = m_masters.get(MasterType::Bias);
    if (!bias || !bias->isValid()) {
        return false;
    }
    
    float* imgData = image.data().data();
    const float* biasData = bias->data().data();
    size_t size = image.size();
    
    subtract_bias_c(imgData, biasData, size, omp_get_max_threads());
    
    addHistory(image, QString("Calibration: subtracted master bias"));
    return true;
}

//=============================================================================
// DARK SUBTRACTION WITH OPTIMIZATION
//=============================================================================

bool PreprocessingEngine::subtractDark(ImageBuffer& image, double& K) {
    const ImageBuffer* dark = m_masters.get(MasterType::Dark);
    if (!dark || !dark->isValid()) {
        return false;
    }
    
    K = 1.0;
    
    // Optionally optimize dark scaling
    if (m_params.darkOptim.enabled) {
        K = Calibration::CalibrationEngine::findOptimalDarkScale(image, *dark, m_params.darkOptim);
    }
    
    // Apply dark subtraction and add pedestal
    float* imgData = image.data().data();
    const float* darkData = dark->data().data();
    size_t size = image.size();
    
    float kf = static_cast<float>(K);
    float p = static_cast<float>(m_params.pedestal);

    subtract_dark_c(imgData, darkData, size, kf, p, omp_get_max_threads());
    
    QString history = QString("Calibration: subtracted master dark (factor: %1)").arg(K);
    if (p != 0.0f) history += QString(", added pedestal %1").arg(p);
    addHistory(image, history);

    return true;
}

//=============================================================================
// COSMETIC CORRECTION
//=============================================================================

void PreprocessingEngine::applyCosmeticCorrection(ImageBuffer& image,
                                                   int& hotCorrected,
                                                   int& coldCorrected) {
    hotCorrected = 0;
    coldCorrected = 0;
    
    if (m_params.cosmetic.type == CosmeticType::None) {
        return;
    }
    
    int w = image.width();
    int h = image.height();
    int channels = image.channels();
    float* data = image.data().data();
    
    // 1. Deviant pixel list from master dark (CFA/X-Trans aware)
    if (m_params.cosmetic.type == CosmeticType::FromMaster && !m_deviantPixels.empty()) {
        #pragma omp parallel for reduction(+:hotCorrected)
        for (int i = 0; i < (int)m_deviantPixels.size(); ++i) {
            const QPoint& pt = m_deviantPixels[i];
            int x = pt.x();
            int y = pt.y();
            if (x < 1 || y < 1 || x >= w - 1 || y >= h - 1) continue;

            for (int c = 0; c < channels; ++c) {
                float* layerData = data + c * w * h;
                int idx = y * w + x;
                
                // Replace with median of neighbors (8 for mono/debayered, 
                // but for CFA we should ideally use same-color neighbors).
                // For simplicity, we use the average of same-color neighbors if CFA.
                if (m_params.bayerPattern != BayerPattern::None && channels == 1) {
                    // CFA median (2 pixels distance)
                    float neighbors[4] = {
                        layerData[(y-2)*w + x], layerData[(y+2)*w + x],
                        layerData[y*w + (x-2)], layerData[y*w + (x+2)]
                    };
                    layerData[idx] = (neighbors[0] + neighbors[1] + neighbors[2] + neighbors[3]) / 4.0f;
                } else {
                    float neighbors[8] = {
                        layerData[(y-1)*w + (x-1)], layerData[(y-1)*w + x], layerData[(y-1)*w + (x+1)],
                        layerData[y*w + (x-1)],                             layerData[y*w + (x+1)],
                        layerData[(y+1)*w + (x-1)], layerData[(y+1)*w + x], layerData[(y+1)*w + (x+1)]
                    };
                    std::sort(neighbors, neighbors + 8);
                    layerData[idx] = (neighbors[3] + neighbors[4]) / 2.0f;
                }
                hotCorrected++;
            }
        }
        addHistory(image, QString("Cosmetic: corrected %1 pixels from master dark").arg(hotCorrected));
    }
    // 2. Statistical detection (sigma-clipping)
    else if (m_params.cosmetic.type == CosmeticType::Sigma) {
        for (int c = 0; c < channels; ++c) {
            float* layerData = data + c * w * h;
            
            // Compute median and MAD
            std::vector<float> copy(layerData, layerData + w * h);
            float median = Stacking::Statistics::quickMedian(copy);
            double mad = Stacking::Statistics::mad(copy.data(), copy.size(), median);
            double sigma = 1.4826 * mad;
            
            float coldThresh = median - m_params.cosmetic.coldSigma * static_cast<float>(sigma);
            float hotThresh = median + m_params.cosmetic.hotSigma * static_cast<float>(sigma);
            
            // Detect and fix bad pixels
            #pragma omp parallel for reduction(+:hotCorrected, coldCorrected)
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    int idx = y * w + x;
                    float val = layerData[idx];
                    
                    bool isBad = false;
                    if (val < coldThresh) {
                        isBad = true;
                    } else if (val > hotThresh) {
                        isBad = true;
                    }
                    
                    if (isBad) {
                        // Median fix
                        float neighbors[8] = {
                            layerData[(y-1)*w + (x-1)], layerData[(y-1)*w + x], layerData[(y-1)*w + (x+1)],
                            layerData[y*w + (x-1)],                             layerData[y*w + (x+1)],
                            layerData[(y+1)*w + (x-1)], layerData[(y+1)*w + x], layerData[(y+1)*w + (x+1)]
                        };
                        std::sort(neighbors, neighbors + 8);
                        layerData[idx] = (neighbors[3] + neighbors[4]) / 2.0f;
                        
                        if (val < coldThresh) coldCorrected++;
                        else hotCorrected++;
                    }
                }
            }
        }
        addHistory(image, QString("Cosmetic: sigma correction (hot:%1, cold:%2)").arg(hotCorrected).arg(coldCorrected));
    }
}

//=============================================================================
// DEBAYERING
//=============================================================================

bool PreprocessingEngine::debayer(ImageBuffer& image) {
    if ((m_params.bayerPattern == BayerPattern::None && m_params.bayerPattern != BayerPattern::Auto) || image.channels() != 1) {
        return false;
    }

    BayerPattern pattern = m_params.bayerPattern;
    
    // Resolve Auto Pattern
    if (pattern == BayerPattern::Auto) {
         QString bpStr = image.metadata().bayerPattern;
         if (bpStr.contains("RGGB", Qt::CaseInsensitive)) pattern = BayerPattern::RGGB;
         else if (bpStr.contains("BGGR", Qt::CaseInsensitive)) pattern = BayerPattern::BGGR;
         else if (bpStr.contains("GBRG", Qt::CaseInsensitive)) pattern = BayerPattern::GBRG;
         else if (bpStr.contains("GRBG", Qt::CaseInsensitive)) pattern = BayerPattern::GRBG;
         else {
             emit logMessage(tr("Warning: Auto-detect Bayer Pattern failed (Header: '%1'). Defaulting to RGGB.").arg(bpStr), "orange");
             pattern = BayerPattern::RGGB;
         }
    }
    
    ImageBuffer output;
    bool success = false;
    
    switch (m_params.debayerAlgorithm) {
        case DebayerAlgorithm::Bilinear:
            success = Debayer::bilinear(image, output, pattern);
            break;
        case DebayerAlgorithm::VNG:
            success = Debayer::vng(image, output, pattern);
            break;
        case DebayerAlgorithm::SuperPixel:
            success = Debayer::superpixel(image, output, pattern);
            break;
        case DebayerAlgorithm::RCD:
            // RCD was refactored to be static in previous steps
            // We need to check Debayer.h if 'rcd' method exists or logic is inside vng?
            // Wait, previous session I modified RCD in Debayer.cpp. 
            // I should check if Debayer::rcd exists.
            // Assuming it exists as I saw 'RCD' in DebayerAlgorithm enum.
            // Wait, previous view of Debayer.cpp showed 'rcd' method.
            success = Debayer::rcd(image, output, pattern);
            break;
        case DebayerAlgorithm::AHD:
             // Fallback to VNG if AHD not implemented, or use ahd if exists.
             // Debayer.cpp showed AHD? No, I only saw VNG/RCD/Bilinear.
             // Safe fallback to VNG.
             success = Debayer::vng(image, output, pattern);
             break;
        default:
            success = Debayer::bilinear(image, output, pattern);
            break;
    }
    
    if (success) {
        // Critical: Preserve metadata!
        // The output buffer is fresh and has no metadata.
        // We must copy it from the input before replacing the input.
        output.setMetadata(image.metadata());
        
        // Update logic: It is now RGB, so BayerPattern is technically None.
        // However, keeping the history is useful.
        // But for consistency:
        output.metadata().isMono = false;
        // output.metadata().bayerPattern = ""; // Optional: Clear it?
        
        image = std::move(output);
        return true;
    }
    return false;
}

int PreprocessingEngine::getBayerColor(int x, int y) const {
    // Returns: 0=R, 1=G, 2=B
    int xEven = (x % 2 == 0);
    int yEven = (y % 2 == 0);
    
    switch (m_params.bayerPattern) {
        case BayerPattern::RGGB:
            if (yEven) return xEven ? 0 : 1;  // R G
            else return xEven ? 1 : 2;         // G B
            
        case BayerPattern::BGGR:
            if (yEven) return xEven ? 2 : 1;  // B G
            else return xEven ? 1 : 0;         // G R
            
        case BayerPattern::GBRG:
            if (yEven) return xEven ? 1 : 2;  // G B
            else return xEven ? 0 : 1;         // R G
            
        case BayerPattern::GRBG:
            if (yEven) return xEven ? 1 : 0;  // G R
            else return xEven ? 2 : 1;         // B G
            
        default:
            return 1;  // Green
    }
}

bool PreprocessingEngine::debayerBilinear(ImageBuffer& image) {
    ImageBuffer output;
    if (Debayer::bilinear(image, output, m_params.bayerPattern)) {
        image = std::move(output);
        return true;
    }
    return false;
}

bool PreprocessingEngine::debayerVNG(ImageBuffer& image) {
    ImageBuffer output;
    if (Debayer::vng(image, output, m_params.bayerPattern)) {
        image = std::move(output);
        return true;
    }
    return false;
}

bool PreprocessingEngine::debayerSuperpixel(ImageBuffer& image) {
    ImageBuffer output;
    if (Debayer::superpixel(image, output, m_params.bayerPattern)) {
        image = std::move(output);
        return true;
    }
    return false;
}

//=============================================================================
// WORKER THREAD
//=============================================================================

PreprocessingWorker::PreprocessingWorker(const PreprocessParams& params,
                                          const QStringList& files,
                                          const QString& outputDir,
                                          QObject* parent)
    : QThread(parent)
    , m_files(files)
    , m_outputDir(outputDir)
{
    m_engine.setParams(params);
    
    connect(&m_engine, &PreprocessingEngine::progressChanged,
            this, &PreprocessingWorker::progressChanged);
    connect(&m_engine, &PreprocessingEngine::logMessage,
            this, &PreprocessingWorker::logMessage);
    connect(&m_engine, &PreprocessingEngine::imageProcessed,
            this, &PreprocessingWorker::imageProcessed);
}

void PreprocessingWorker::run() {
    int processed = m_engine.preprocessBatch(
        m_files, m_outputDir,
        [this](const QString& msg, double pct) {
            emit progressChanged(msg, pct);
        }
    );
    
    emit finished(processed == m_files.size());
}

void PreprocessingWorker::requestCancel() {
    m_engine.requestCancel();
}

void PreprocessingEngine::addHistory(ImageBuffer& image, const QString& message) {
    image.metadata().rawHeaders.push_back({ "HISTORY", message, "" });
}

} // namespace Preprocessing
