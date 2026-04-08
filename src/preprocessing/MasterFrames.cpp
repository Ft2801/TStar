/**
 * @file MasterFrames.cpp
 * @brief Implementation of master calibration frame management.
 *
 * Covers loading / unloading of master frames, computation of per-frame
 * statistics, creation of master bias / dark / flat via integrated
 * stacking, and dimensional / channel compatibility validation.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "MasterFrames.h"
#include "Preprocessing.h"
#include "../io/FitsWrapper.h"
#include "../io/FitsLoader.h"
#include "../stacking/StackingEngine.h"
#include "../stacking/StackingSequence.h"
#include "../stacking/Statistics.h"

#include <QDateTime>
#include <QDir>

#include <cstring>

// ============================================================================
//  Anonymous-Namespace Helper
// ============================================================================

namespace {

/**
 * @brief Pre-calibrate a set of source files (e.g. subtract bias from darks)
 *        and write the calibrated versions to a working directory.
 *
 * @param sourceFiles       List of raw sub-frame paths.
 * @param params            Preprocessing parameters to apply.
 * @param workingDirectory  Temporary directory for calibrated outputs.
 * @param calibratedFiles   [out] Populated with the paths of calibrated files.
 * @param progress          Optional progress callback.
 * @param stageLabel        Human-readable label for progress messages.
 * @return true if all files were calibrated successfully.
 */
bool calibrateFramesForMaster(const QStringList& sourceFiles,
                              const Preprocessing::PreprocessParams& params,
                              const QString& workingDirectory,
                              QStringList& calibratedFiles,
                              Preprocessing::ProgressCallback progress,
                              const QString& stageLabel)
{
    QDir dir(workingDirectory);
    if (!dir.exists() && !dir.mkpath(".")) {
        return false;
    }

    Preprocessing::PreprocessingEngine engine;
    engine.setParams(params);

    calibratedFiles.clear();

    for (int i = 0; i < sourceFiles.size(); ++i) {
        if (progress) {
            progress(QObject::tr("%1 %2/%3...")
                         .arg(stageLabel)
                         .arg(i + 1)
                         .arg(sourceFiles.size()),
                     static_cast<double>(i) / std::max(1LL, static_cast<long long>(sourceFiles.size())));
        }

        const QString outPath = dir.filePath(QString("cal_%1.fit").arg(i, 4, 10, QChar('0')));

        if (!engine.preprocessFile(sourceFiles[i], outPath)) {
            calibratedFiles.clear();
            return false;
        }

        calibratedFiles.append(outPath);
    }

    if (progress) {
        progress(QObject::tr("%1 complete").arg(stageLabel), 1.0);
    }

    return !calibratedFiles.isEmpty();
}

} // anonymous namespace

// ============================================================================
//  Preprocessing::MasterFrames Implementation
// ============================================================================

namespace Preprocessing {

// ----------------------------------------------------------------------------
//  Loading and Access
// ----------------------------------------------------------------------------

bool MasterFrames::load(MasterType type, const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }

    const int idx = typeIndex(type);

    MasterData data;
    data.buffer = std::make_unique<ImageBuffer>();
    data.path   = path;

    if (!Stacking::FitsIO::read(path, *data.buffer)) {
        return false;
    }

    m_masters[idx] = std::move(data);
    computeStats(type);

    return true;
}

bool MasterFrames::isLoaded(MasterType type) const
{
    const int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    return (it != m_masters.end()) && it->second.buffer && it->second.buffer->isValid();
}

const ImageBuffer* MasterFrames::get(MasterType type) const
{
    const int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    return (it != m_masters.end()) ? it->second.buffer.get() : nullptr;
}

ImageBuffer* MasterFrames::get(MasterType type)
{
    const int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    return (it != m_masters.end()) ? it->second.buffer.get() : nullptr;
}

const MasterStats& MasterFrames::stats(MasterType type) const
{
    static MasterStats empty;
    const int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    return (it != m_masters.end()) ? it->second.stats : empty;
}

void MasterFrames::unload(MasterType type)
{
    m_masters.erase(typeIndex(type));
}

void MasterFrames::clear()
{
    m_masters.clear();
}

// ----------------------------------------------------------------------------
//  Statistics
// ----------------------------------------------------------------------------

void MasterFrames::computeStats(MasterType type)
{
    const int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    if (it == m_masters.end() || !it->second.buffer) {
        return;
    }

    ImageBuffer* buf   = it->second.buffer.get();
    MasterStats& stats = it->second.stats;

    stats.width    = buf->width();
    stats.height   = buf->height();
    stats.channels = buf->channels();

    const size_t size = buf->data().size();

    // Min / max.
    float minVal, maxVal;
    Stacking::Statistics::minMax(buf->data().data(), size, minVal, maxVal);
    std::memcpy(&stats.min, &minVal, sizeof(float));
    std::memcpy(&stats.max, &maxVal, sizeof(float));

    // Median (destructive -- operates on a copy).
    std::vector<float> copy(buf->data().begin(), buf->data().begin() + size);
    stats.median = Stacking::Statistics::quickMedian(copy);

    // Mean and standard deviation.
    stats.mean  = Stacking::Statistics::mean(buf->data().data(), size);
    stats.sigma = Stacking::Statistics::stdDev(buf->data().data(), size, &stats.mean);

    // Exposure and temperature from file metadata.
    const auto& meta   = buf->metadata();
    stats.exposure     = meta.exposure;
    stats.temperature  = meta.ccdTemp;
}

// ----------------------------------------------------------------------------
//  Master Frame Creation -- Common Stacking Configuration
// ----------------------------------------------------------------------------

/**
 * @brief Populate a StackingArgs with settings common to all master-frame
 *        creation workflows (bias, dark, flat).
 */
static void configureStackingArgs(Stacking::StackingArgs& args,
                                  Stacking::ImageSequence* sequence,
                                  const QString& output,
                                  Stacking::Method method,
                                  Stacking::Rejection rejection,
                                  float sigmaLow,
                                  float sigmaHigh,
                                  Stacking::NormalizationMethod norm,
                                  ProgressCallback progress)
{
    args.sequence          = sequence;
    args.progressCallback  = progress;
    args.params.outputFilename    = output;
    args.params.force32Bit        = true;

    args.params.method     = method;
    args.params.rejection  = rejection;
    args.params.sigmaLow   = sigmaLow;
    args.params.sigmaHigh  = sigmaHigh;

    // Calibration frames must never be normalised (except flats),
    // weighted, reframed, upscaled, or drizzled.
    args.params.normalization      = norm;
    args.params.weighting          = Stacking::WeightingType::None;
    args.params.framingMode        = Stacking::FramingMode::Reference;
    args.params.upscaleAtStacking  = false;
    args.params.drizzle            = false;
}

// ----------------------------------------------------------------------------
//  Master Bias Creation
// ----------------------------------------------------------------------------

bool MasterFrames::createMasterBias(const QStringList& files,
                                    const QString& output,
                                    Stacking::Method method,
                                    Stacking::Rejection rejection,
                                    float sigmaLow,
                                    float sigmaHigh,
                                    ProgressCallback progress)
{
    if (files.isEmpty()) {
        return false;
    }

    Stacking::ImageSequence sequence;
    if (!sequence.loadFromFiles(files, progress)) {
        return false;
    }

    Stacking::StackingArgs args;
    configureStackingArgs(args, &sequence, output, method, rejection,
                          sigmaLow, sigmaHigh,
                          Stacking::NormalizationMethod::None, progress);

    Stacking::StackingEngine engine;
    if (engine.execute(args) != Stacking::StackResult::OK) {
        return false;
    }

    return Stacking::FitsIO::write(output, args.result, 32);
}

// ----------------------------------------------------------------------------
//  Master Dark Creation
// ----------------------------------------------------------------------------

bool MasterFrames::createMasterDark(const QStringList& files,
                                    const QString& output,
                                    const QString& masterBias,
                                    Stacking::Method method,
                                    Stacking::Rejection rejection,
                                    float sigmaLow,
                                    float sigmaHigh,
                                    ProgressCallback progress)
{
    if (files.isEmpty()) {
        return false;
    }

    // Optionally pre-calibrate dark sub-frames with a master bias.
    QStringList stackInputFiles = files;
    QString     tempDirPath;

    if (!masterBias.isEmpty()) {
        tempDirPath = QDir::temp().filePath(
            QString("tstar_masterdark_%1").arg(QDateTime::currentMSecsSinceEpoch()));

        PreprocessParams params;
        params.masterBias = masterBias;
        params.useBias    = true;
        params.useDark    = false;
        params.useFlat    = false;
        params.outputFloat = true;

        if (!calibrateFramesForMaster(files, params, tempDirPath,
                                      stackInputFiles, progress,
                                      QObject::tr("Calibrating dark frame")))
        {
            QDir(tempDirPath).removeRecursively();
            return false;
        }
    }

    // Preserve single-frame metadata (exposure, temperature) before stacking,
    // because the stacking engine sums exposure times.
    double firstExposure = 0.0;
    double firstTemp     = 0.0;
    {
        ImageBuffer firstBuf;
        if (FitsLoader::loadMetadata(files.first(), firstBuf)) {
            firstExposure = firstBuf.metadata().exposure;
            firstTemp     = firstBuf.metadata().ccdTemp;
        }
    }

    // Stack the (optionally calibrated) dark sub-frames.
    Stacking::ImageSequence sequence;
    if (!sequence.loadFromFiles(stackInputFiles, progress)) {
        if (!tempDirPath.isEmpty()) QDir(tempDirPath).removeRecursively();
        return false;
    }

    Stacking::StackingArgs args;
    configureStackingArgs(args, &sequence, output, method, rejection,
                          sigmaLow, sigmaHigh,
                          Stacking::NormalizationMethod::None, progress);

    Stacking::StackingEngine engine;
    if (engine.execute(args) != Stacking::StackResult::OK) {
        if (!tempDirPath.isEmpty()) QDir(tempDirPath).removeRecursively();
        return false;
    }

    if (!tempDirPath.isEmpty()) {
        QDir(tempDirPath).removeRecursively();
    }

    // Restore single-frame exposure and temperature metadata.
    args.result.metadata().exposure = firstExposure;
    args.result.metadata().ccdTemp  = firstTemp;

    return Stacking::FitsIO::write(output, args.result, 32);
}

// ----------------------------------------------------------------------------
//  Master Flat Creation
// ----------------------------------------------------------------------------

bool MasterFrames::createMasterFlat(const QStringList& files,
                                    const QString& output,
                                    const QString& masterBias,
                                    const QString& masterDark,
                                    Stacking::Method method,
                                    Stacking::Rejection rejection,
                                    float sigmaLow,
                                    float sigmaHigh,
                                    ProgressCallback progress)
{
    if (files.isEmpty()) {
        return false;
    }

    // Optionally pre-calibrate flat sub-frames with bias and/or dark.
    QStringList stackInputFiles = files;
    QString     tempDirPath;

    if (!masterBias.isEmpty() || !masterDark.isEmpty()) {
        tempDirPath = QDir::temp().filePath(
            QString("tstar_masterflat_%1").arg(QDateTime::currentMSecsSinceEpoch()));

        PreprocessParams params;
        params.masterBias = masterBias;
        params.masterDark = masterDark;
        params.useBias    = !masterBias.isEmpty();
        params.useDark    = !masterDark.isEmpty();
        params.useFlat    = false;
        params.outputFloat = true;

        if (!calibrateFramesForMaster(files, params, tempDirPath,
                                      stackInputFiles, progress,
                                      QObject::tr("Calibrating flat frame")))
        {
            QDir(tempDirPath).removeRecursively();
            return false;
        }
    }

    // Preserve single-frame metadata.
    double firstExposure = 0.0;
    double firstTemp     = 0.0;
    {
        ImageBuffer firstBuf;
        if (FitsLoader::loadMetadata(files.first(), firstBuf)) {
            firstExposure = firstBuf.metadata().exposure;
            firstTemp     = firstBuf.metadata().ccdTemp;
        }
    }

    // Stack with multiplicative normalisation (standard for flats).
    Stacking::ImageSequence sequence;
    if (!sequence.loadFromFiles(stackInputFiles, progress)) {
        if (!tempDirPath.isEmpty()) QDir(tempDirPath).removeRecursively();
        return false;
    }

    Stacking::StackingArgs args;
    configureStackingArgs(args, &sequence, output, method, rejection,
                          sigmaLow, sigmaHigh,
                          Stacking::NormalizationMethod::Multiplicative, progress);

    Stacking::StackingEngine engine;
    if (engine.execute(args) != Stacking::StackResult::OK) {
        if (!tempDirPath.isEmpty()) QDir(tempDirPath).removeRecursively();
        return false;
    }

    if (!tempDirPath.isEmpty()) {
        QDir(tempDirPath).removeRecursively();
    }

    args.result.metadata().exposure = firstExposure;
    args.result.metadata().ccdTemp  = firstTemp;

    // Normalise each channel so its median equals 1.0.
    float*    flatData  = args.result.data().data();
    const int channels  = args.result.channels();
    const int layerSize = args.result.width() * args.result.height();

    for (int c = 0; c < channels; ++c) {
        float* layerData = flatData + c * layerSize;

        std::vector<float> copy(layerData, layerData + layerSize);
        const float median = Stacking::Statistics::quickMedian(copy);

        if (median > 0.0f) {
            const float invMedian = 1.0f / median;
            for (int i = 0; i < layerSize; ++i) {
                layerData[i] *= invMedian;
            }
        }
    }

    return Stacking::FitsIO::write(output, args.result, 32);
}

// ----------------------------------------------------------------------------
//  Validation
// ----------------------------------------------------------------------------

QString MasterFrames::validateCompatibility(const ImageBuffer& target) const
{
    const int targetWidth    = target.width();
    const int targetHeight   = target.height();
    const int targetChannels = target.channels();

    for (const auto& [idx, data] : m_masters) {
        if (!data.buffer) continue;

        // Derive a readable name for diagnostic messages.
        const MasterType type = static_cast<MasterType>(idx);
        QString typeName;
        switch (type) {
            case MasterType::Bias:     typeName = "Bias";      break;
            case MasterType::Dark:     typeName = "Dark";      break;
            case MasterType::Flat:     typeName = "Flat";      break;
            case MasterType::DarkFlat: typeName = "Dark Flat"; break;
        }

        // Dimension check.
        if (data.stats.width != targetWidth || data.stats.height != targetHeight) {
            return QObject::tr("Master %1 dimensions (%2x%3) don't match target (%4x%5)")
                .arg(typeName)
                .arg(data.stats.width).arg(data.stats.height)
                .arg(targetWidth).arg(targetHeight);
        }

        // Channel count check (e.g. mono CFA master vs. debayered light).
        const int masterChannels = data.buffer->channels();
        if (masterChannels != targetChannels) {
            return QObject::tr("Master %1 channels (%2) don't match target (%3). "
                               "Ensure masters and lights have the same format.")
                .arg(typeName)
                .arg(masterChannels).arg(targetChannels);
        }
    }

    return QString(); // All compatible.
}

bool MasterFrames::checkDarkTemperature(double targetTemp, double tolerance) const
{
    const auto& darkStats = stats(MasterType::Dark);

    // If temperature metadata is absent, assume compatibility.
    if (darkStats.temperature == 0.0) {
        return true;
    }

    return std::abs(darkStats.temperature - targetTemp) <= tolerance;
}

bool MasterFrames::checkDarkExposure(double targetExposure, double tolerance) const
{
    const auto& darkStats = stats(MasterType::Dark);

    // If either exposure value is unavailable, assume compatibility.
    if (darkStats.exposure <= 0.0 || targetExposure <= 0.0) {
        return true;
    }

    const double ratio = targetExposure / darkStats.exposure;
    return std::abs(ratio - 1.0) <= tolerance;
}

} // namespace Preprocessing