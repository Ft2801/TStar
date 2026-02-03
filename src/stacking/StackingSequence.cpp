/**
 * @file StackingSequence.cpp
 * @brief Implementation of ImageSequence class
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "StackingSequence.h"
#include "../io/FitsWrapper.h"
#include "../io/TiffIO.h"
#include "../io/FitsLoader.h"
#include "Statistics.h"
#include "Registration.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <QDateTime>

namespace Stacking {

//=============================================================================
// LOADING AND INITIALIZATION
//=============================================================================

bool ImageSequence::loadFromFiles(const QStringList& files, 
                                   ProgressCallback progressCallback) {
    clear();
    
    if (files.isEmpty()) {
        return false;
    }
    
    m_images.reserve(files.size());
    
    // Determine common directory
    QFileInfo firstFile(files.first());
    m_directory = firstFile.absolutePath();
    m_type = Type::Regular;
    
    int totalFiles = files.size();
    int processed = 0;
    
    for (const QString& filePath : files) {
        if (progressCallback) {
            progressCallback(QObject::tr("Loading %1...").arg(QFileInfo(filePath).fileName()),
                           static_cast<double>(processed) / totalFiles);
        }
        
        SequenceImage img;
        img.filePath = filePath;
        img.index = processed;
        img.selected = true;
        
        // Read metadata
        if (!readImageMetadata(img)) {
            // Skip invalid files but continue
            continue;
        }
        
        m_images.push_back(std::move(img));
        processed++;
    }
    
    if (progressCallback) {
        progressCallback(QObject::tr("Validating sequence..."), -1);
    }
    
    return validateSequence();
}

bool ImageSequence::loadFromDirectory(const QString& directory,
                                      const QStringList& nameFilters,
                                      ProgressCallback progressCallback)
{
    m_directory = directory;
    m_images.clear();
    
    QDir dir(directory);
    if (!dir.exists()) {
        return false;
    }
    
    // Use QDir to list files with pattern
    QStringList files = dir.entryList(nameFilters, QDir::Files, QDir::Name);
    
    if (files.empty()) {
        return false;
    }
    QStringList fullPaths;
    fullPaths.reserve(files.size());
    for (const QString& file : files) {
        fullPaths.append(dir.absoluteFilePath(file));
    }
    
    return loadFromFiles(fullPaths, progressCallback);
}

void ImageSequence::clear() {
    m_images.clear();
    m_directory.clear();
    m_type = Type::Regular;
    m_width = 0;
    m_height = 0;
    m_channels = 0;
    m_isVariable = false;
    m_hasRegistration = false;
    m_hasQualityMetrics = false;
    m_referenceImage = 0;
}

void ImageSequence::removeImage(int index) {
    if (index < 0 || index >= static_cast<int>(m_images.size())) {
        return;  // Invalid index
    }
    m_images.erase(m_images.begin() + index);
    
    // Adjust reference image if needed
    if (m_referenceImage >= static_cast<int>(m_images.size())) {
        m_referenceImage = std::max(0, static_cast<int>(m_images.size()) - 1);
    }
}

bool ImageSequence::validateSequence() {
    if (m_images.empty()) {
        return false;
    }
    
    // Use first image as reference for dimensions
    const auto& first = m_images.front();
    m_width = first.width;
    m_height = first.height;
    m_channels = first.channels;
    m_isVariable = false;
    
    // Check for variable dimensions
    for (size_t i = 1; i < m_images.size(); ++i) {
        const auto& img = m_images[i];
        if (img.width != m_width || img.height != m_height) {
            m_isVariable = true;
        }
        if (img.channels != m_channels) {
            // Mixed mono/color sequences not supported
            return false;
        }
    }
    
    // Check for registration data
    m_hasRegistration = false;
    for (const auto& img : m_images) {
        if (img.registration.hasRegistration) {
            m_hasRegistration = true;
            break;
        }
    }
    
    // Check for quality metrics
    m_hasQualityMetrics = false;
    for (const auto& img : m_images) {
        if (img.quality.hasMetrics) {
            m_hasQualityMetrics = true;
            break;
        }
    }
    
    // Set reference to middle image by default
    m_referenceImage = static_cast<int>(m_images.size()) / 2;
    
    return true;
}

bool ImageSequence::readImageMetadata(SequenceImage& img) {
    QFileInfo fi(img.filePath);
    QString ext = fi.suffix().toLower();
    
    // Try to read basic dimensions and metadata
    if (ext == "fit" || ext == "fits" || ext == "fts") {
        // Read FITS metadata
        ImageBuffer temp;
        if (!FitsLoader::loadMetadata(img.filePath, temp)) {
            return false;
        }
        
        img.width = temp.width();
        img.height = temp.height();
        img.channels = temp.channels();
        img.bitDepth = 16;  // Will be updated when reading
        img.isFloat = false;
        img.metadata = temp.metadata();
        
        // Exposure time is now robustly populated by FitsLoader
        img.exposure = temp.metadata().exposure;
        
        return img.width > 0 && img.height > 0;
    }
    else if (ext == "tif" || ext == "tiff") {
        // Read TIFF metadata
        int w, h, ch, bits;
        if (!TiffIO::readInfo(img.filePath, w, h, ch, bits)) {
            return false;
        }
        
        img.width = w;
        img.height = h;
        img.channels = ch;
        img.bitDepth = bits;
        img.isFloat = (bits == 32);
        
        return true;
    }
    
    // Unsupported format
    return false;
}

//=============================================================================
// IMAGE ACCESS
//=============================================================================

int ImageSequence::selectedCount() const {
    return static_cast<int>(std::count_if(m_images.begin(), m_images.end(),
        [](const SequenceImage& img) { return img.selected; }));
}

bool ImageSequence::readImage(int index, ImageBuffer& buffer) const {
    if (index < 0 || index >= static_cast<int>(m_images.size())) {
        return false;
    }
    
    const auto& img = m_images[index];
    QFileInfo fi(img.filePath);
    QString ext = fi.suffix().toLower();
    
    if (ext == "fit" || ext == "fits" || ext == "fts") {
        return FitsLoader::load(img.filePath, buffer);
    } else if (ext == "tif" || ext == "tiff") {
        return TiffIO::read(img.filePath, buffer);
    }
    
    return false;
}

bool ImageSequence::readImageRegion(int index, ImageBuffer& buffer,
                                     int x, int y, int width, int height,
                                     int channel) const {
    Q_UNUSED(channel);
    if (index < 0 || index >= static_cast<int>(m_images.size())) {
        return false;
    }
    
    const auto& img = m_images[index];
    QFileInfo fi(img.filePath);
    QString ext = fi.suffix().toLower();
    
    if (ext == "fit" || ext == "fits" || ext == "fts") {
        return FitsLoader::loadRegion(img.filePath, buffer, x, y, width, height);
    }
    
    // For other formats, read full image and crop
    if (!readImage(index, buffer)) {
        return false;
    }
    
    buffer.crop(x, y, width, height);
    return buffer.isValid();
}

//=============================================================================
// SELECTION AND FILTERING
//=============================================================================

void ImageSequence::setSelected(int index, bool selected) {
    if (index >= 0 && index < static_cast<int>(m_images.size())) {
        m_images[index].selected = selected;
    }
}

void ImageSequence::selectAll() {
    for (auto& img : m_images) {
        img.selected = true;
    }
}

void ImageSequence::deselectAll() {
    for (auto& img : m_images) {
        img.selected = false;
    }
}

void ImageSequence::toggleSelection(int index) {
    if (index >= 0 && index < static_cast<int>(m_images.size())) {
        m_images[index].selected = !m_images[index].selected;
    }
}

int ImageSequence::applyFilter(ImageFilter filter, FilterMode mode, double parameter) {
    // First select all
    selectAll();
    
    if (filter == ImageFilter::All) {
        return count();
    }
    
    if (filter == ImageFilter::Selected) {
        // Keep current selection (already set)
        return selectedCount();
    }
    
    // For quality-based filters, we need metrics
    if (!m_hasQualityMetrics) {
        // Can't filter without metrics
        return count();
    }
    
    double threshold = computeFilterThreshold(filter, mode, parameter);
    
    int passed = 0;
    for (auto& img : m_images) {
        bool passes = false;
        
        switch (filter) {
            case ImageFilter::BestFWHM:
                passes = img.quality.fwhm <= threshold && img.quality.fwhm > 0;
                break;
            case ImageFilter::BestWeightedFWHM:
                passes = img.quality.weightedFwhm <= threshold && img.quality.weightedFwhm > 0;
                break;
            case ImageFilter::BestRoundness:
                passes = img.quality.roundness >= threshold;
                break;
            case ImageFilter::BestBackground:
                passes = img.quality.background <= threshold;
                break;
            case ImageFilter::BestStarCount:
                passes = img.quality.starCount >= static_cast<int>(threshold);
                break;
            case ImageFilter::BestQuality:
                passes = img.quality.quality >= threshold;
                break;
            default:
                passes = true;
        }
        
        img.selected = passes;
        if (passes) passed++;
    }
    
    return passed;
}

std::vector<int> ImageSequence::getFilteredIndices() const {
    std::vector<int> indices;
    indices.reserve(m_images.size());
    
    for (size_t i = 0; i < m_images.size(); ++i) {
        if (m_images[i].selected) {
            indices.push_back(static_cast<int>(i));
        }
    }
    
    return indices;
}

double ImageSequence::computeFilterThreshold(ImageFilter filter, FilterMode mode,
                                              double parameter) const {
    if (!m_hasQualityMetrics) {
        return 0.0;
    }
    
    // Collect values based on filter type
    std::vector<double> values;
    values.reserve(m_images.size());
    
    for (const auto& img : m_images) {
        double val = 0.0;
        switch (filter) {
            case ImageFilter::BestFWHM:
                val = img.quality.fwhm;
                break;
            case ImageFilter::BestWeightedFWHM:
                val = img.quality.weightedFwhm;
                break;
            case ImageFilter::BestRoundness:
                val = img.quality.roundness;
                break;
            case ImageFilter::BestBackground:
                val = img.quality.background;
                break;
            case ImageFilter::BestStarCount:
                val = static_cast<double>(img.quality.starCount);
                break;
            case ImageFilter::BestQuality:
                val = img.quality.quality;
                break;
            default:
                continue;
        }
        
        if (val > 0 || filter == ImageFilter::BestRoundness) {
            values.push_back(val);
        }
    }
    
    if (values.empty()) {
        return 0.0;
    }
    
    // Sort values
    std::sort(values.begin(), values.end());
    
    if (mode == FilterMode::Percentage) {
        // Find percentile threshold
        int idx = 0;
        
        // For "best" filters, lower is better for FWHM/background, higher for others
        switch (filter) {
            case ImageFilter::BestFWHM:
            case ImageFilter::BestWeightedFWHM:
            case ImageFilter::BestBackground:
                // Lower is better - take lowest N%
                idx = static_cast<int>(values.size() * parameter / 100.0);
                idx = std::min(idx, static_cast<int>(values.size()) - 1);
                return values[idx];
                
            case ImageFilter::BestRoundness:
            case ImageFilter::BestStarCount:
            case ImageFilter::BestQuality:
                // Higher is better - take highest N%
                idx = static_cast<int>(values.size() * (100.0 - parameter) / 100.0);
                idx = std::max(idx, 0);
                return values[idx];
                
            default:
                return values.back();
        }
    }
    else {
        // K-sigma clipping
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / values.size();
        
        double sqSum = 0.0;
        for (double v : values) {
            sqSum += (v - mean) * (v - mean);
        }
        double sigma = std::sqrt(sqSum / values.size());
        
        switch (filter) {
            case ImageFilter::BestFWHM:
            case ImageFilter::BestWeightedFWHM:
            case ImageFilter::BestBackground:
                // Lower is better
                return mean + parameter * sigma;
                
            case ImageFilter::BestRoundness:
            case ImageFilter::BestStarCount:
            case ImageFilter::BestQuality:
                // Higher is better
                return mean - parameter * sigma;
                
            default:
                return mean;
        }
    }
}

//=============================================================================
// REFERENCE IMAGE
//=============================================================================

int ImageSequence::findBestReference() const {
    if (!m_hasQualityMetrics || m_images.empty()) {
        return 0;
    }
    
    int bestIdx = 0;
    double bestQuality = -1.0;
    
    for (size_t i = 0; i < m_images.size(); ++i) {
        const auto& img = m_images[i];
        if (img.selected && img.quality.hasMetrics) {
            // Combine metrics into quality score
            double q = img.quality.quality;
            if (q <= 0) {
                // Compute simple quality from available metrics
                if (img.quality.fwhm > 0 && img.quality.roundness > 0) {
                    q = img.quality.roundness / img.quality.fwhm;
                }
            }
            
            if (q > bestQuality) {
                bestQuality = q;
                bestIdx = static_cast<int>(i);
            }
        }
    }
    
    return bestIdx;
}

//=============================================================================
// SEQUENCE PROPERTIES
//=============================================================================

double ImageSequence::totalExposure() const {
    double total = 0.0;
    for (const auto& img : m_images) {
        if (img.selected) {
            total += img.exposure;
        }
    }
    return total;
}

//=============================================================================
// REGISTRATION
//=============================================================================

bool ImageSequence::loadRegistration(const QString& regFile) {
    Q_UNUSED(regFile);
    return false;
}

void ImageSequence::clearRegistration() {
    for (auto& img : m_images) {
        img.registration = RegistrationData();
    }
    m_hasRegistration = false;
}

bool ImageSequence::isShiftOnlyRegistration() const {
    for (const auto& img : m_images) {
        if (img.registration.hasRegistration && !img.registration.isShiftOnly()) {
            return false;
        }
    }
    return true;
}

//=============================================================================
// QUALITY METRICS
//=============================================================================

bool ImageSequence::computeQualityMetrics(ProgressCallback progressCallback) {
    if (progressCallback) {
        progressCallback(QObject::tr("Computing quality metrics..."), -1);
    }
    
    RegistrationEngine engine;
    // Connect logging for registration progress
    QObject::connect(&engine, &RegistrationEngine::logMessage, [](const QString&, const QString&) {
        // Registration logging enabled
    });
    
    for (size_t i = 0; i < m_images.size(); ++i) {
        if (progressCallback) {
            progressCallback(QObject::tr("Analyzing image %1/%2...")
                           .arg(i + 1).arg(m_images.size()),
                           static_cast<double>(i) / m_images.size());
        }
        
        // Load image data
        ImageBuffer buffer;
        bool loaded = false;
        QString p = m_images[i].filePath;
        if (p.endsWith(".fits", Qt::CaseInsensitive) || p.endsWith(".fit", Qt::CaseInsensitive)) {
            loaded = FitsLoader::load(p, buffer);
        } else if (p.endsWith(".tiff", Qt::CaseInsensitive) || p.endsWith(".tif", Qt::CaseInsensitive)) {
            loaded = TiffIO::read(p, buffer);
        }
        
        if (!loaded) continue;

        // Measure background (using median of first channel)
        m_images[i].quality.background = buffer.getRobustMedian(0, 0, 1.0f);

        // Detect stars and compute metrics
        auto stars = engine.detectStars(buffer);
        
        if (!stars.empty()) {
            // Sort by flux (handled by DetectedStar::operator<)
            std::sort(stars.begin(), stars.end());
            
            // Use top 50 stars for robust average
            int count = std::min((int)stars.size(), 50);
            float sumFWHM = 0, sumRoundness = 0;
            int validCount = 0;
            
            for (int k = 0; k < count; ++k) {
                if (stars[k].fwhm > 0.5f && stars[k].fwhm < 20.0f) {
                    sumFWHM += stars[k].fwhm;
                    sumRoundness += stars[k].roundness;
                    validCount++;
                }
            }
            
            m_images[i].quality.hasMetrics = true;
            m_images[i].quality.starCount = static_cast<int>(stars.size());
            
            if (validCount > 0) {
                float avgFWHM = sumFWHM / validCount;
                m_images[i].quality.fwhm = avgFWHM;
                m_images[i].quality.roundness = sumRoundness / validCount;
                
                // Compute quality score: combination of star count and inverse FWHM
                // Quality = (StarCount / 500) * (1.0 / FWHM^2)
                // Normalized roughly to 0-100 range for typical images
                m_images[i].quality.quality = (m_images[i].quality.starCount / 50.0) * (2.0 / (avgFWHM * avgFWHM)) * 100.0;
            } else {
                m_images[i].quality.fwhm = 99.0f;
                m_images[i].quality.roundness = 0.0f;
                m_images[i].quality.quality = 0.0;
            }
        } else {
            m_images[i].quality.hasMetrics = true;
            m_images[i].quality.starCount = 0;
            m_images[i].quality.fwhm = 99.0f;
            m_images[i].quality.roundness = 0.0f;
            m_images[i].quality.quality = 0.0;
        }
    }
    
    m_hasQualityMetrics = true;
    return true;
}

//=============================================================================
// COMET REGISTRATION
//=============================================================================

bool ImageSequence::computeCometShifts(int refIndex, int targetIndex) {
    if (refIndex < 0 || refIndex >= count() || 
        targetIndex < 0 || targetIndex >= count()) {
        return false;
    }
    
    // Get time and position info
    const auto& refImg = m_images[refIndex];
    const auto& targetImg = m_images[targetIndex];
    
    QDateTime refTime = QDateTime::fromString(refImg.metadata.dateObs, Qt::ISODate);
    QDateTime targetTime = QDateTime::fromString(targetImg.metadata.dateObs, Qt::ISODate);
    
    if (!refTime.isValid() || !targetTime.isValid()) {
        // Try alternate formats or assume failure
        // Common FITS format: YYYY-MM-DDTHH:MM:SS[.sss]
        // Sometimes spaces instead of T
        return false;
    }
    
    double dtTotal = refTime.msecsTo(targetTime) / 1000.0;
    if (std::abs(dtTotal) < 0.1) return false; // Too close in time
    
    // Comet positions (must be set externally before calling)
    double cx1 = refImg.registration.cometX;
    double cy1 = refImg.registration.cometY;
    double cx2 = targetImg.registration.cometX;
    double cy2 = targetImg.registration.cometY;
    
    // These positions are usually in the ALIGNED reference frame if user clicked after registration
    // OR in raw frame.
    // Case 1: Stars are registered. User sees aligned images. User clicks comet in #1 and #N.
    // The clicked coordinates are in the Aligned Space.
    // So cx1, cx2 are aligned coordinates.
    // Velocity V = (Pos2_aligned - Pos1_aligned) / dt.
    // Shift_Comet(t) = - V * dt.
    // This shift is ADDED to the existing star alignment.
    
    double vx = (cx2 - cx1) / dtTotal;
    double vy = (cy2 - cy1) / dtTotal;
    
    for (auto& img : m_images) {
        QDateTime t = QDateTime::fromString(img.metadata.dateObs, Qt::ISODate);
        if (!t.isValid()) continue;
        
        double dt = refTime.msecsTo(t) / 1000.0;
        
        // Additional shift to freeze comet
        double shiftCometX = -(vx * dt);
        double shiftCometY = -(vy * dt);
        
        // Update registration
        img.registration.shiftX += shiftCometX;
        img.registration.shiftY += shiftCometY;
        
        // We might want to flag that this is now comet-aligned
    }
    
    return true;
}

} // namespace Stacking
