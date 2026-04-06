/**
 * @file Preprocessing.cpp
 * @brief Implementation of the image calibration pipeline engine.
 *
 * Implements single-image and batch calibration including bias/dark/flat
 * subtraction, sensor artefact fixes, cosmetic correction, and CFA
 * demosaicing.  Also implements the PreprocessingWorker thread wrapper.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "Preprocessing.h"
#include "Debayer.h"
#include "../calibration/CalibrationEngine.h"
#include "../calibration/CalibrationC.h"
#include "../io/FitsWrapper.h"
#include "../stacking/Statistics.h"
#include "../core/ResourceManager.h"

#include <QtConcurrent>
#include <QAtomicInt>
#include <QFileInfo>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <omp.h>

namespace Preprocessing {

// ============================================================================
//  Construction / Destruction
// ============================================================================

PreprocessingEngine::PreprocessingEngine(QObject* parent)
    : QObject(parent)
{
}

PreprocessingEngine::~PreprocessingEngine() = default;

// ============================================================================
//  Configuration
// ============================================================================

void PreprocessingEngine::setParams(const PreprocessParams& params)
{
    m_params = params;
    m_deviantPixels.clear();

    // --- Load master bias ---
    // A synthetic bias (constant ADU value) bypasses the master file entirely,
    // so only load a file when biasLevel is at its sentinel default (> 1e20).
    if (!params.masterBias.isEmpty() && params.useBias && params.biasLevel > 1e20) {
        m_masters.load(MasterType::Bias, params.masterBias);
    }

    // --- Load master dark and (optionally) build a defect map ---
    if (!params.masterDark.isEmpty() && params.useDark) {
        m_masters.load(MasterType::Dark, params.masterDark);

        if (params.cosmetic.type == CosmeticType::FromMaster) {
            ImageBuffer* dark = m_masters.get(MasterType::Dark);
            if (dark && dark->isValid()) {
                m_deviantPixels = Calibration::CalibrationEngine::findDeviantPixels(
                    *dark, params.cosmetic.hotSigma, params.cosmetic.coldSigma);
                emit logMessage(
                    tr("Detected %1 deviant pixels in master dark").arg(m_deviantPixels.size()),
                    "neutral");
            }
        }
    }

    // --- Load master flat and apply optional pre-calibrations ---
    if (!params.masterFlat.isEmpty() && params.useFlat) {
        m_masters.load(MasterType::Flat, params.masterFlat);

        ImageBuffer* flat = m_masters.get(MasterType::Flat);
        if (flat && flat->isValid()) {

            // Per-channel CFA equalisation (optional).
            if (params.equalizeFlat) {
                Calibration::CalibrationEngine::equalizeCFAChannels(*flat, params.bayerPattern);
                emit logMessage(tr("Equalized CFA channels in master flat"), "neutral");
            }

            // Subtract a dark-for-flat if one was provided...
            if (!params.masterDarkFlat.isEmpty()) {
                m_masters.load(MasterType::DarkFlat, params.masterDarkFlat);
                const ImageBuffer* darkFlat = m_masters.get(MasterType::DarkFlat);
                if (darkFlat && darkFlat->isValid()) {
                    float*       fData  = flat->data().data();
                    const float* dfData = darkFlat->data().data();
                    const size_t size   = flat->size();
                    for (size_t i = 0; i < size; ++i) {
                        fData[i] -= dfData[i];
                    }
                }
            } else {
                // ...otherwise calibrate the flat with bias (synthetic or master).
                if (params.useBias && params.biasLevel < 1e20) {
                    const float b     = static_cast<float>(params.biasLevel);
                    float*      fData = flat->data().data();
                    const size_t size = flat->size();
                    for (size_t i = 0; i < size; ++i) {
                        fData[i] -= b;
                    }
                    emit logMessage(
                        tr("Calibrated Master Flat using synthetic bias %1").arg(b), "neutral");

                } else if (params.useBias && m_masters.isLoaded(MasterType::Bias)) {
                    const ImageBuffer* bias = m_masters.get(MasterType::Bias);
                    if (bias && bias->isValid()) {
                        float*       fData = flat->data().data();
                        const float* bData = bias->data().data();
                        const size_t size  = flat->size();
                        for (size_t i = 0; i < size; ++i) {
                            fData[i] -= bData[i];
                        }
                        emit logMessage(
                            tr("Calibrated Master Flat using Master Bias"), "neutral");
                    }
                }
            }
        }
    }

    // --- Cache flat normalisation factor ---
    m_flatNormalization = 1.0;
    if (m_params.useFlat && m_masters.isLoaded(MasterType::Flat)) {
        const ImageBuffer* flat = m_masters.get(MasterType::Flat);
        m_flatNormalization = Calibration::CalibrationEngine::computeFlatNormalization(*flat);

        QString patternStr = "None/Mono";
        if (m_params.bayerPattern != BayerPattern::None) {
            switch (m_params.bayerPattern) {
                case BayerPattern::RGGB: patternStr = "RGGB"; break;
                case BayerPattern::BGGR: patternStr = "BGGR"; break;
                case BayerPattern::GBRG: patternStr = "GBRG"; break;
                case BayerPattern::GRBG: patternStr = "GRBG"; break;
                default:                 patternStr = "Auto/Unknown"; break;
            }
        }
        emit logMessage(
            tr("Master Flat loaded (Norm: %1, Pattern: %2)")
                .arg(m_flatNormalization, 0, 'f', 4)
                .arg(patternStr),
            "neutral");
    }
}

// ============================================================================
//  Single-Image Processing
// ============================================================================

bool PreprocessingEngine::preprocessImage(const ImageBuffer& input,
                                          ImageBuffer& output)
{
    // Start with a working copy of the input.
    output = input;

    // Validate dimensional compatibility with loaded master frames.
    const QString error = m_masters.validateCompatibility(output);
    if (!error.isEmpty()) {
        emit logMessage(error, "red");
        return false;
    }

    // ---- Step 1: Bias subtraction ----
    if (isCancelled()) return false;
    if (m_params.useBias && m_masters.isLoaded(MasterType::Bias)) {
        if (!subtractBias(output)) {
            emit logMessage(tr("Bias subtraction failed"), "salmon");
        }
    }

    // ---- Step 2: Dark subtraction (with optional scaling optimisation) ----
    if (isCancelled()) return false;
    double darkK = 1.0;
    if (m_params.useDark && m_masters.isLoaded(MasterType::Dark)) {
        if (!subtractDark(output, darkK)) {
            emit logMessage(tr("Dark subtraction failed"), "salmon");
        }
    }

    // ---- Step 3: Flat-field correction ----
    if (isCancelled()) return false;
    if (m_params.useFlat && m_masters.isLoaded(MasterType::Flat)) {
        const ImageBuffer* flat = m_masters.get(MasterType::Flat);
        if (Calibration::CalibrationEngine::applyFlat(output, *flat, m_flatNormalization)) {
            addHistory(output, QString("Calibration: applied master flat (norm: %1)")
                                   .arg(m_flatNormalization));
        } else {
            emit logMessage(tr("Flat correction failed"), "salmon");
        }
    }

    // ---- Step 4: Sensor-specific artefact fixes ----
    if (m_params.fixBanding) {
        Calibration::CalibrationEngine::fixBanding(output);
        addHistory(output, "Fix: sensor banding reduction");
    }
    if (m_params.fixBadLines) {
        Calibration::CalibrationEngine::fixBadLines(output);
        addHistory(output, "Fix: bad CCD lines correction");
    }
    if (m_params.fixXTrans) {
        // X-Trans AF correction requires top-down row order.
        const QString rowOrder = output.getHeaderValue("ROW-ORDER");
        const bool needsFlip = rowOrder.contains("BOTTOM-UP", Qt::CaseInsensitive);

        if (needsFlip) output.mirrorY();
        Calibration::CalibrationEngine::fixXTransArtifacts(output);
        if (needsFlip) output.mirrorY();

        addHistory(output, "Fix: X-Trans AF pixel artifacts");
    }

    // ---- Step 5: Cosmetic (hot / cold pixel) correction ----
    int hotCorrected = 0, coldCorrected = 0;
    if (m_params.cosmetic.type != CosmeticType::None) {
        applyCosmeticCorrection(output, hotCorrected, coldCorrected);
    }

    // ---- Step 6: CFA demosaicing ----
    if (m_params.debayer && m_params.bayerPattern != BayerPattern::None) {
        if (!debayer(output)) {
            emit logMessage(tr("Debayering failed"), "salmon");
        }
    }

    // ---- Update cumulative run statistics ----
    m_stats.imagesProcessed++;
    m_stats.avgDarkOptimK =
        (m_stats.avgDarkOptimK * (m_stats.imagesProcessed - 1) + darkK)
        / m_stats.imagesProcessed;
    m_stats.hotPixelsCorrected  += hotCorrected;
    m_stats.coldPixelsCorrected += coldCorrected;

    return true;
}

bool PreprocessingEngine::preprocessFile(const QString& inputPath,
                                         const QString& outputPath)
{
    // Load the source FITS file.
    ImageBuffer input;
    if (!Stacking::FitsIO::read(inputPath, input)) {
        emit logMessage(tr("Failed to read: %1").arg(inputPath), "red");
        m_stats.imagesFailed++;
        return false;
    }

    // Run the calibration pipeline.
    ImageBuffer output;
    if (!preprocessImage(input, output)) {
        m_stats.imagesFailed++;
        return false;
    }

    // Write the calibrated output.
    const int bitDepth = m_params.outputFloat ? 32 : 16;
    if (!Stacking::FitsIO::write(outputPath, output, bitDepth)) {
        emit logMessage(tr("Failed to write: %1").arg(outputPath), "red");
        m_stats.imagesFailed++;
        return false;
    }

    emit imageProcessed(inputPath, true);
    return true;
}

// ============================================================================
//  Batch Processing
// ============================================================================

int PreprocessingEngine::preprocessBatch(const QStringList& inputFiles,
                                         const QString& outputDir,
                                         ProgressCallback progress)
{
    m_cancelled = false;
    m_stats     = CalibrationStats();

    QDir outDir(outputDir);
    if (!outDir.exists()) {
        outDir.mkpath(".");
    }

    const int total = inputFiles.size();
    QAtomicInt processed(0);
    std::mutex progressMutex;

    emit logMessage(
        tr("Preprocessing %1 files using %2 threads...")
            .arg(total)
            .arg(ResourceManager::instance().maxThreads()),
        "neutral");

    QtConcurrent::blockingMap(inputFiles, [&](const QString& inputPath) {
        if (m_cancelled) return;

        QFileInfo fi(inputPath);

        // Conservative memory safety check before allocating buffers.
        const size_t estimatedMem = fi.size() * 20;
        if (!ResourceManager::instance().isMemorySafe(estimatedMem)) {
            emit logMessage(
                tr("Skipped %1: Insufficient memory (current usage > 90%)")
                    .arg(fi.fileName()),
                "red");
            return;
        }

        const QString outputName = m_params.outputPrefix + fi.completeBaseName() + ".fit";
        const QString outputPath = outDir.filePath(outputName);

        if (preprocessFile(inputPath, outputPath)) {
            processed.fetchAndAddRelaxed(1);

            std::lock_guard<std::mutex> lock(progressMutex);
            if (progress) {
                progress(tr("Calibrating image %1 (%2/%3)...")
                             .arg(fi.fileName())
                             .arg(processed.loadRelaxed())
                             .arg(total),
                         static_cast<double>(processed.loadRelaxed()) / total);
            }
        }
    });

    if (progress) {
        progress(tr("Complete: %1/%2 images processed")
                     .arg(processed.loadRelaxed())
                     .arg(total),
                 1.0);
    }

    return processed.loadRelaxed();
}

// ============================================================================
//  Bias Subtraction
// ============================================================================

bool PreprocessingEngine::subtractBias(ImageBuffer& image)
{
    // Option 1: Synthetic (constant-value) bias.
    if (m_params.biasLevel < 1e20) {
        const float  b    = static_cast<float>(m_params.biasLevel);
        float*       data = image.data().data();
        const size_t size = image.size();

        #pragma omp parallel for
        for (long long i = 0; i < static_cast<long long>(size); ++i) {
            data[i] -= b;
        }

        addHistory(image, QString("Calibration: subtracted synthetic bias %1").arg(b));
        return true;
    }

    // Option 2: Master bias frame.
    const ImageBuffer* bias = m_masters.get(MasterType::Bias);
    if (!bias || !bias->isValid()) {
        return false;
    }

    float*       imgData  = image.data().data();
    const float* biasData = bias->data().data();
    const size_t size     = image.size();

    subtract_bias_c(imgData, biasData, size, omp_get_max_threads());

    addHistory(image, QString("Calibration: subtracted master bias"));
    return true;
}

// ============================================================================
//  Dark Subtraction with Optional Optimisation
// ============================================================================

bool PreprocessingEngine::subtractDark(ImageBuffer& image, double& K)
{
    const ImageBuffer* dark = m_masters.get(MasterType::Dark);
    if (!dark || !dark->isValid()) {
        return false;
    }

    K = 1.0;

    // Compute the exposure-time ratio if both values are available.
    const double lightExptime = image.metadata().exposure;
    const double darkExptime  = m_masters.stats(MasterType::Dark).exposure;

    if (lightExptime > 0.0 && darkExptime > 0.0) {
        K = lightExptime / darkExptime;
    } else if (!m_params.darkOptim.enabled) {
        emit logMessage(
            tr("Warning: Missing exposure time metadata. Using scaling factor 1.0."),
            "orange");
    }

    // Numerical optimisation overrides the simple ratio.
    if (m_params.darkOptim.enabled) {
        K = Calibration::CalibrationEngine::findOptimalDarkScale(
                image, *dark, m_params.darkOptim);
        emit logMessage(
            tr("Numerical dark optimization found factor K = %1").arg(K, 0, 'f', 4),
            "neutral");
    } else if (lightExptime > 0.0 && darkExptime > 0.0) {
        emit logMessage(
            tr("Scaling dark by exposure ratio K = %1 (%2s / %3s)")
                .arg(K, 0, 'f', 4)
                .arg(lightExptime)
                .arg(darkExptime),
            "neutral");
    }

    // Apply: image = image - K * dark + pedestal
    float*       imgData  = image.data().data();
    const float* darkData = dark->data().data();
    const size_t size     = image.size();

    const float kf = static_cast<float>(K);
    const float p  = static_cast<float>(m_params.pedestal);

    subtract_dark_c(imgData, darkData, size, kf, p, omp_get_max_threads());

    QString history = QString("Calibration: subtracted master dark (factor: %1)").arg(K);
    if (p != 0.0f) {
        history += QString(", added pedestal %1").arg(p);
    }
    addHistory(image, history);

    return true;
}

// ============================================================================
//  Cosmetic Correction
// ============================================================================

void PreprocessingEngine::applyCosmeticCorrection(ImageBuffer& image,
                                                  int& hotCorrected,
                                                  int& coldCorrected)
{
    hotCorrected  = 0;
    coldCorrected = 0;

    if (m_params.cosmetic.type == CosmeticType::None) {
        return;
    }

    const int w        = image.width();
    const int h        = image.height();
    const int channels = image.channels();
    float*    data     = image.data().data();

    // ---- Strategy 1: Defect map from master dark ----
    if (m_params.cosmetic.type == CosmeticType::FromMaster && !m_deviantPixels.empty()) {

        #pragma omp parallel for reduction(+:hotCorrected)
        for (int i = 0; i < static_cast<int>(m_deviantPixels.size()); ++i) {
            const QPoint& pt = m_deviantPixels[i];
            const int x = pt.x();
            const int y = pt.y();

            // CFA-aware replacement uses +/-2 offsets, so guard a 2-pixel border.
            if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2) continue;

            for (int c = 0; c < channels; ++c) {
                float*    layerData = data + c * w * h;
                const int idx       = y * w + x;

                if (m_params.bayerPattern != BayerPattern::None && channels == 1) {
                    // CFA data: average of 4 same-colour neighbours at +/-2 distance.
                    const float n0 = layerData[(y - 2) * w + x];
                    const float n1 = layerData[(y + 2) * w + x];
                    const float n2 = layerData[y * w + (x - 2)];
                    const float n3 = layerData[y * w + (x + 2)];
                    layerData[idx] = (n0 + n1 + n2 + n3) * 0.25f;
                } else {
                    // Mono / debayered data: median of 8-connected neighbours.
                    float neighbours[8] = {
                        layerData[(y-1)*w + (x-1)], layerData[(y-1)*w + x], layerData[(y-1)*w + (x+1)],
                        layerData[ y   *w + (x-1)],                         layerData[ y   *w + (x+1)],
                        layerData[(y+1)*w + (x-1)], layerData[(y+1)*w + x], layerData[(y+1)*w + (x+1)]
                    };
                    std::sort(neighbours, neighbours + 8);
                    layerData[idx] = (neighbours[3] + neighbours[4]) * 0.5f;
                }
                hotCorrected++;
            }
        }

        addHistory(image,
                   QString("Cosmetic: corrected %1 pixels from master dark").arg(hotCorrected));
    }

    // ---- Strategy 2: Statistical sigma-clipping detection ----
    else if (m_params.cosmetic.type == CosmeticType::Sigma) {

        for (int c = 0; c < channels; ++c) {
            float* layerData = data + c * w * h;

            // Compute robust statistics (median and MAD).
            std::vector<float> copy(layerData, layerData + w * h);
            const float  median = Stacking::Statistics::quickMedian(copy);
            const double mad    = Stacking::Statistics::mad(copy.data(), copy.size(), median);
            const double sigma  = 1.4826 * mad;

            const float coldThresh = median - m_params.cosmetic.coldSigma * static_cast<float>(sigma);
            const float hotThresh  = median + m_params.cosmetic.hotSigma  * static_cast<float>(sigma);

            #pragma omp parallel for reduction(+:hotCorrected, coldCorrected)
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {

                    const int   idx = y * w + x;
                    const float val = layerData[idx];

                    const bool isCold = (val < coldThresh);
                    const bool isHot  = (val > hotThresh);

                    if (isCold || isHot) {
                        // Replace with median of 8-connected neighbours.
                        float neighbours[8] = {
                            layerData[(y-1)*w + (x-1)], layerData[(y-1)*w + x], layerData[(y-1)*w + (x+1)],
                            layerData[ y   *w + (x-1)],                         layerData[ y   *w + (x+1)],
                            layerData[(y+1)*w + (x-1)], layerData[(y+1)*w + x], layerData[(y+1)*w + (x+1)]
                        };
                        std::sort(neighbours, neighbours + 8);
                        layerData[idx] = (neighbours[3] + neighbours[4]) * 0.5f;

                        if (isCold) coldCorrected++;
                        else        hotCorrected++;
                    }
                }
            }
        }

        addHistory(image,
                   QString("Cosmetic: sigma correction (hot:%1, cold:%2)")
                       .arg(hotCorrected)
                       .arg(coldCorrected));
    }
}

// ============================================================================
//  Demosaicing (Debayering)
// ============================================================================

bool PreprocessingEngine::debayer(ImageBuffer& image)
{
    // Must be a single-channel CFA image with a valid Bayer pattern.
    if ((m_params.bayerPattern == BayerPattern::None &&
         m_params.bayerPattern != BayerPattern::Auto) ||
        image.channels() != 1)
    {
        return false;
    }

    BayerPattern pattern = m_params.bayerPattern;

    // Resolve Auto pattern from the FITS header.
    if (pattern == BayerPattern::Auto) {
        const QString bpStr = image.metadata().bayerPattern;
        if      (bpStr.contains("RGGB", Qt::CaseInsensitive)) pattern = BayerPattern::RGGB;
        else if (bpStr.contains("BGGR", Qt::CaseInsensitive)) pattern = BayerPattern::BGGR;
        else if (bpStr.contains("GBRG", Qt::CaseInsensitive)) pattern = BayerPattern::GBRG;
        else if (bpStr.contains("GRBG", Qt::CaseInsensitive)) pattern = BayerPattern::GRBG;
        else {
            emit logMessage(
                tr("Warning: Auto-detect Bayer Pattern failed (Header: '%1'). "
                   "Defaulting to RGGB.").arg(bpStr),
                "orange");
            pattern = BayerPattern::RGGB;
        }
    }

    // Dispatch to the selected algorithm.
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
            success = Debayer::rcd(image, output, pattern);
            break;
        case DebayerAlgorithm::AHD:
            success = Debayer::ahd(image, output, pattern);
            break;
        case DebayerAlgorithm::EdgeAware:
            success = Debayer::edgeAware(image, output, pattern);
            break;
        default:
            success = Debayer::bilinear(image, output, pattern);
            break;
    }

    if (success) {
        // Preserve all metadata from the original CFA frame.
        output.setMetadata(image.metadata());

        // Clear CFA-specific metadata so downstream code treats the
        // image as a fully-interpolated RGB frame.
        output.metadata().isMono = false;
        output.metadata().bayerPattern.clear();
        output.metadata().xisfProperties.remove("BayerPattern");

        auto& headers = output.metadata().rawHeaders;
        headers.erase(
            std::remove_if(headers.begin(), headers.end(),
                           [](const auto& h) { return h.key == "BAYERPAT"; }),
            headers.end());

        image = std::move(output);
        return true;
    }

    return false;
}

// ============================================================================
//  Debayering Algorithm Wrappers
// ============================================================================

int PreprocessingEngine::getBayerColor(int x, int y) const
{
    const int xEven = (x % 2 == 0);
    const int yEven = (y % 2 == 0);

    switch (m_params.bayerPattern) {
        case BayerPattern::RGGB:
            return yEven ? (xEven ? 0 : 1)    // R G
                         : (xEven ? 1 : 2);   // G B
        case BayerPattern::BGGR:
            return yEven ? (xEven ? 2 : 1)    // B G
                         : (xEven ? 1 : 0);   // G R
        case BayerPattern::GBRG:
            return yEven ? (xEven ? 1 : 2)    // G B
                         : (xEven ? 0 : 1);   // R G
        case BayerPattern::GRBG:
            return yEven ? (xEven ? 1 : 0)    // G R
                         : (xEven ? 2 : 1);   // B G
        default:
            return 1; // Assume green for unknown patterns.
    }
}

bool PreprocessingEngine::debayerBilinear(ImageBuffer& image)
{
    ImageBuffer output;
    if (Debayer::bilinear(image, output, m_params.bayerPattern)) {
        image = std::move(output);
        return true;
    }
    return false;
}

bool PreprocessingEngine::debayerVNG(ImageBuffer& image)
{
    ImageBuffer output;
    if (Debayer::vng(image, output, m_params.bayerPattern)) {
        image = std::move(output);
        return true;
    }
    return false;
}

bool PreprocessingEngine::debayerSuperpixel(ImageBuffer& image)
{
    ImageBuffer output;
    if (Debayer::superpixel(image, output, m_params.bayerPattern)) {
        image = std::move(output);
        return true;
    }
    return false;
}

// ============================================================================
//  FITS Header History Helper
// ============================================================================

void PreprocessingEngine::addHistory(ImageBuffer& image, const QString& message)
{
    image.metadata().rawHeaders.push_back({"HISTORY", message, ""});
}

// ============================================================================
//  Worker Thread Implementation
// ============================================================================

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
            this,      &PreprocessingWorker::progressChanged);
    connect(&m_engine, &PreprocessingEngine::logMessage,
            this,      &PreprocessingWorker::logMessage);
    connect(&m_engine, &PreprocessingEngine::imageProcessed,
            this,      &PreprocessingWorker::imageProcessed);
}

void PreprocessingWorker::run()
{
    const int processed = m_engine.preprocessBatch(
        m_files, m_outputDir,
        [this](const QString& msg, double pct) {
            emit progressChanged(msg, pct);
        });

    emit finished(processed == m_files.size());
}

void PreprocessingWorker::requestCancel()
{
    m_engine.requestCancel();
}

} // namespace Preprocessing