/**
 * @file MasterFrames.cpp
 * @brief Implementation of master calibration frame management
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "MasterFrames.h"
#include "../io/FitsWrapper.h"
#include "../stacking/StackingEngine.h"
#include "../stacking/StackingSequence.h"
#include "../stacking/Statistics.h"

namespace Preprocessing {

//=============================================================================
// LOADING
//=============================================================================

bool MasterFrames::load(MasterType type, const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    
    int idx = typeIndex(type);
    
    // Create new master data
    MasterData data;
    data.buffer = std::make_unique<ImageBuffer>();
    data.path = path;
    
    // Load the file
    if (!Stacking::FitsIO::read(path, *data.buffer)) {
        return false;
    }
    
    // Store and compute stats
    m_masters[idx] = std::move(data);
    computeStats(type);
    
    return true;
}

bool MasterFrames::isLoaded(MasterType type) const {
    int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    return it != m_masters.end() && it->second.buffer && it->second.buffer->isValid();
}

const ImageBuffer* MasterFrames::get(MasterType type) const {
    int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    if (it != m_masters.end()) {
        return it->second.buffer.get();
    }
    return nullptr;
}

ImageBuffer* MasterFrames::get(MasterType type) {
    int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    if (it != m_masters.end()) {
        return it->second.buffer.get();
    }
    return nullptr;
}

const MasterStats& MasterFrames::stats(MasterType type) const {
    static MasterStats empty;
    int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    if (it != m_masters.end()) {
        return it->second.stats;
    }
    return empty;
}

void MasterFrames::unload(MasterType type) {
    int idx = typeIndex(type);
    m_masters.erase(idx);
}

void MasterFrames::clear() {
    m_masters.clear();
}

//=============================================================================
// STATISTICS
//=============================================================================

void MasterFrames::computeStats(MasterType type) {
    int idx = typeIndex(type);
    auto it = m_masters.find(idx);
    if (it == m_masters.end() || !it->second.buffer) {
        return;
    }
    
    ImageBuffer* buf = it->second.buffer.get();
    MasterStats& stats = it->second.stats;
    
    stats.width = buf->width();
    stats.height = buf->height();
    stats.channels = buf->channels();
    
    size_t size = buf->data().size();
    
    // Compute statistics on first channel
    // Use statistics from stacking module
    float minVal, maxVal;
    Stacking::Statistics::minMax(buf->data().data(), size, minVal, maxVal);
    std::memcpy(&stats.min, &minVal, sizeof(float));
    std::memcpy(&stats.max, &maxVal, sizeof(float));
    
    std::vector<float> copy(buf->data().begin(), buf->data().begin() + size);
    stats.median = Stacking::Statistics::quickMedian(copy);
    stats.mean = Stacking::Statistics::mean(buf->data().data(), size);
    stats.sigma = Stacking::Statistics::stdDev(buf->data().data(), size, &stats.mean);
    
    // Get metadata from buffer
    const auto& meta = buf->metadata();
    stats.exposure = meta.exposure;
    stats.temperature = meta.ccdTemp;
}

//=============================================================================
// CREATION
//=============================================================================

bool MasterFrames::createMasterBias(
    const QStringList& files,
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
    
    // Create stacking sequence
    Stacking::ImageSequence sequence;
    if (!sequence.loadFromFiles(files, progress)) {
        return false;
    }
    
    // Configure stacking
    Stacking::StackingParams params;
    params.method = method;
    params.rejection = rejection;
    params.sigmaLow = sigmaLow;
    params.sigmaHigh = sigmaHigh;
    params.normalization = Stacking::NormalizationMethod::None;  // No normalization for bias
    params.outputFilename = output;
    params.force32Bit = true;
    
    // Stack
    Stacking::StackingArgs args;
    args.params = params;
    args.sequence = &sequence;
    args.progressCallback = progress;
    
    Stacking::StackingEngine engine;
    auto result = engine.execute(args);
    
    if (result != Stacking::StackResult::OK) {
        return false;
    }
    
    // Save result
    return Stacking::FitsIO::write(output, args.result, 32);
}

bool MasterFrames::createMasterDark(
    const QStringList& files,
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
    
    // Load master bias if provided
    std::unique_ptr<ImageBuffer> bias;
    if (!masterBias.isEmpty()) {
        bias = std::make_unique<ImageBuffer>();
        if (!Stacking::FitsIO::read(masterBias, *bias)) {
            bias.reset();
        }
    }
    
    // Create stacking sequence
    Stacking::ImageSequence sequence;
    if (!sequence.loadFromFiles(files, progress)) {
        return false;
    }
    
    // Configure stacking
    Stacking::StackingParams params;
    params.method = method;
    params.rejection = rejection;
    params.sigmaLow = sigmaLow;
    params.sigmaHigh = sigmaHigh;
    params.normalization = Stacking::NormalizationMethod::None;  // No normalization for dark
    params.outputFilename = output;
    params.force32Bit = true;
    
    // Stack
    Stacking::StackingArgs args;
    args.params = params;
    args.sequence = &sequence;
    args.progressCallback = progress;
    
    Stacking::StackingEngine engine;
    auto result = engine.execute(args);
    
    if (result != Stacking::StackResult::OK) {
        return false;
    }
    
    // Subtract bias from result if provided
    if (bias && bias->isValid()) {
        float* darkData = args.result.data().data();
        const float* biasData = bias->data().data();
        size_t size = static_cast<size_t>(args.result.width()) * 
                      args.result.height() * args.result.channels();
        
        for (size_t i = 0; i < size; ++i) {
            darkData[i] -= biasData[i];
        }
    }
    
    // Save result
    return Stacking::FitsIO::write(output, args.result, 32);
}

bool MasterFrames::createMasterFlat(
    const QStringList& files,
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
    
    // Load master bias if provided
    std::unique_ptr<ImageBuffer> bias;
    if (!masterBias.isEmpty()) {
        bias = std::make_unique<ImageBuffer>();
        if (!Stacking::FitsIO::read(masterBias, *bias)) {
            bias.reset();
        }
    }
    
    // Load master dark if provided
    std::unique_ptr<ImageBuffer> dark;
    if (!masterDark.isEmpty()) {
        dark = std::make_unique<ImageBuffer>();
        if (!Stacking::FitsIO::read(masterDark, *dark)) {
            dark.reset();
        }
    }
    
    // Create stacking sequence
    Stacking::ImageSequence sequence;
    if (!sequence.loadFromFiles(files, progress)) {
        return false;
    }
    
    // Configure stacking - flats use multiplicative normalization
    Stacking::StackingParams params;
    params.method = method;
    params.rejection = rejection;
    params.sigmaLow = sigmaLow;
    params.sigmaHigh = sigmaHigh;
    params.normalization = Stacking::NormalizationMethod::Multiplicative;
    params.outputFilename = output;
    params.force32Bit = true;
    
    // Stack
    Stacking::StackingArgs args;
    args.params = params;
    args.sequence = &sequence;
    args.progressCallback = progress;
    
    Stacking::StackingEngine engine;
    auto result = engine.execute(args);
    
    if (result != Stacking::StackResult::OK) {
        return false;
    }
    
    // Calibrate result: subtract bias and dark
    float* flatData = args.result.data().data();
    size_t size = static_cast<size_t>(args.result.width()) * 
                  args.result.height() * args.result.channels();
    
    if (bias && bias->isValid()) {
        const float* biasData = bias->data().data();
        for (size_t i = 0; i < size; ++i) {
            flatData[i] -= biasData[i];
        }
    }
    
    if (dark && dark->isValid()) {
        const float* darkData = dark->data().data();
        for (size_t i = 0; i < size; ++i) {
            flatData[i] -= darkData[i];
        }
    }
    
    // Normalize flat so median = 1.0
    // Per channel if color
    int channels = args.result.channels();
    int layerSize = args.result.width() * args.result.height();
    
    for (int c = 0; c < channels; ++c) {
        float* layerData = flatData + c * layerSize;
        
        // Compute median
        std::vector<float> copy(layerData, layerData + layerSize);
        float median = Stacking::Statistics::quickMedian(copy);
        
        if (median > 0.0f) {
            float invMedian = 1.0f / median;
            for (int i = 0; i < layerSize; ++i) {
                layerData[i] *= invMedian;
            }
        }
    }
    
    // Save result
    return Stacking::FitsIO::write(output, args.result, 32);
}

//=============================================================================
// VALIDATION
//=============================================================================

QString MasterFrames::validateCompatibility(const ImageBuffer& target) const {
    int targetWidth = target.width();
    int targetHeight = target.height();
    
    // Check each loaded master
    for (const auto& [idx, data] : m_masters) {
        if (!data.buffer) continue;
        
        MasterType type = static_cast<MasterType>(idx);
        QString typeName;
        switch (type) {
            case MasterType::Bias: typeName = "Bias"; break;
            case MasterType::Dark: typeName = "Dark"; break;
            case MasterType::Flat: typeName = "Flat"; break;
            case MasterType::DarkFlat: typeName = "Dark Flat"; break;
        }
        
        if (data.stats.width != targetWidth || data.stats.height != targetHeight) {
            return QObject::tr("Master %1 dimensions (%2x%3) don't match target (%4x%5)")
                   .arg(typeName)
                   .arg(data.stats.width).arg(data.stats.height)
                   .arg(targetWidth).arg(targetHeight);
        }
    }
    
    return QString();  // Compatible
}

bool MasterFrames::checkDarkTemperature(double targetTemp, double tolerance) const {
    const auto& darkStats = stats(MasterType::Dark);
    if (darkStats.temperature == 0.0) {
        return true;  // No temperature info, assume compatible
    }
    
    return std::abs(darkStats.temperature - targetTemp) <= tolerance;
}

bool MasterFrames::checkDarkExposure(double targetExposure, double tolerance) const {
    const auto& darkStats = stats(MasterType::Dark);
    if (darkStats.exposure <= 0.0 || targetExposure <= 0.0) {
        return true;  // No exposure info, assume compatible
    }
    
    double ratio = targetExposure / darkStats.exposure;
    return std::abs(ratio - 1.0) <= tolerance;
}

} // namespace Preprocessing
