/**
 * @file StackingSequence.cpp
 * @brief Implementation of the ImageSequence class.
 *
 * Covers file loading, metadata extraction, selection / quality-based
 * filtering, quality metric computation (via star detection), reference
 * image selection, and comet-mode registration shift computation.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "StackingSequence.h"
#include "../io/FitsWrapper.h"
#include "../io/TiffIO.h"
#include "../io/FitsLoader.h"
#include "Statistics.h"
#include "Registration.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Stacking {

// ============================================================================
// Loading and initialisation
// ============================================================================

bool ImageSequence::loadFromFiles(const QStringList& files,
                                  ProgressCallback progressCallback)
{
    clear();

    if (files.isEmpty()) {
        return false;
    }

    m_images.reserve(files.size());

    /* Use the parent directory of the first file as the sequence directory. */
    QFileInfo firstFile(files.first());
    m_directory = firstFile.absolutePath();
    m_type      = Type::Regular;

    const int totalFiles = files.size();
    int processed = 0;

    for (const QString& filePath : files) {
        if (progressCallback) {
            progressCallback(
                QObject::tr("Loading %1...").arg(QFileInfo(filePath).fileName()),
                static_cast<double>(processed) / totalFiles);
        }

        SequenceImage img;
        img.filePath = filePath;
        img.index    = processed;
        img.selected = true;

        /* Attempt to read metadata; skip files that cannot be parsed. */
        if (!readImageMetadata(img)) {
            continue;
        }

        m_images.push_back(std::move(img));
        ++processed;
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

    /* Enumerate matching files in alphabetical order. */
    const QStringList files = dir.entryList(nameFilters, QDir::Files, QDir::Name);
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

void ImageSequence::clear()
{
    m_images.clear();
    m_directory.clear();
    m_type             = Type::Regular;
    m_width            = 0;
    m_height           = 0;
    m_channels         = 0;
    m_isVariable       = false;
    m_hasRegistration  = false;
    m_hasQualityMetrics = false;
    m_referenceImage   = 0;
}

void ImageSequence::removeImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) {
        return;
    }

    m_images.erase(m_images.begin() + index);

    /* Keep the reference index within valid bounds. */
    if (m_referenceImage >= static_cast<int>(m_images.size())) {
        m_referenceImage = std::max(0, static_cast<int>(m_images.size()) - 1);
    }
}

bool ImageSequence::validateSequence()
{
    if (m_images.empty()) {
        return false;
    }

    /* Adopt the dimensions of the first frame as the reference. */
    const auto& first = m_images.front();
    m_width      = first.width;
    m_height     = first.height;
    m_channels   = first.channels;
    m_isVariable = false;

    /* Scan for dimensional mismatches and mixed channel counts. */
    for (size_t i = 1; i < m_images.size(); ++i) {
        const auto& img = m_images[i];

        if (img.width != m_width || img.height != m_height) {
            m_isVariable = true;
        }
        if (img.channels != m_channels) {
            /* Sequences mixing mono and colour frames are not supported. */
            return false;
        }
    }

    /* Determine whether any frame carries registration data. */
    m_hasRegistration = std::any_of(
        m_images.begin(), m_images.end(),
        [](const SequenceImage& img) { return img.registration.hasRegistration; });

    /* Determine whether any frame carries quality metrics. */
    m_hasQualityMetrics = std::any_of(
        m_images.begin(), m_images.end(),
        [](const SequenceImage& img) { return img.quality.hasMetrics; });

    /* Default the reference to the middle frame of the sequence. */
    m_referenceImage = static_cast<int>(m_images.size()) / 2;

    return true;
}

bool ImageSequence::readImageMetadata(SequenceImage& img)
{
    QFileInfo fi(img.filePath);
    const QString ext = fi.suffix().toLower();

    /* ----- FITS ----- */
    if (ext == "fit" || ext == "fits" || ext == "fts") {
        ImageBuffer temp;
        if (!FitsLoader::loadMetadata(img.filePath, temp)) {
            return false;
        }

        img.width    = temp.width();
        img.height   = temp.height();
        img.channels = temp.channels();
        img.bitDepth = 16;      // Refined when the full data is read later.
        img.isFloat  = false;
        img.metadata = temp.metadata();
        img.exposure = temp.metadata().exposure;

        return (img.width > 0 && img.height > 0);
    }

    /* ----- TIFF ----- */
    if (ext == "tif" || ext == "tiff") {
        int w, h, ch, bits;
        if (!TiffIO::readInfo(img.filePath, w, h, ch, bits)) {
            return false;
        }

        img.width    = w;
        img.height   = h;
        img.channels = ch;
        img.bitDepth = bits;
        img.isFloat  = (bits == 32);

        return true;
    }

    /* Unsupported format. */
    return false;
}

// ============================================================================
// Image access
// ============================================================================

int ImageSequence::selectedCount() const
{
    return static_cast<int>(
        std::count_if(m_images.begin(), m_images.end(),
                      [](const SequenceImage& img) { return img.selected; }));
}

bool ImageSequence::readImage(int index, ImageBuffer& buffer) const
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) {
        return false;
    }

    const auto& img = m_images[index];
    QFileInfo fi(img.filePath);
    const QString ext = fi.suffix().toLower();

    if (ext == "fit" || ext == "fits" || ext == "fts") {
        return FitsLoader::load(img.filePath, buffer);
    }
    if (ext == "tif" || ext == "tiff") {
        return TiffIO::read(img.filePath, buffer);
    }

    return false;
}

bool ImageSequence::readImageRegion(int index, ImageBuffer& buffer,
                                    int x, int y, int width, int height,
                                    int channel) const
{
    Q_UNUSED(channel);

    if (index < 0 || index >= static_cast<int>(m_images.size())) {
        return false;
    }

    const auto& img = m_images[index];
    QFileInfo fi(img.filePath);
    const QString ext = fi.suffix().toLower();

    /* FITS supports native region reads. */
    if (ext == "fit" || ext == "fits" || ext == "fts") {
        return FitsLoader::loadRegion(img.filePath, buffer, x, y, width, height);
    }

    /* For other formats, load the full image and crop in memory. */
    if (!readImage(index, buffer)) {
        return false;
    }

    buffer.crop(x, y, width, height);
    return buffer.isValid();
}

// ============================================================================
// Selection and filtering
// ============================================================================

void ImageSequence::setSelected(int index, bool selected)
{
    if (index >= 0 && index < static_cast<int>(m_images.size())) {
        m_images[index].selected = selected;
    }
}

void ImageSequence::selectAll()
{
    for (auto& img : m_images) {
        img.selected = true;
    }
}

void ImageSequence::deselectAll()
{
    for (auto& img : m_images) {
        img.selected = false;
    }
}

void ImageSequence::toggleSelection(int index)
{
    if (index >= 0 && index < static_cast<int>(m_images.size())) {
        m_images[index].selected = !m_images[index].selected;
    }
}

int ImageSequence::applyFilter(ImageFilter filter, FilterMode mode, double parameter)
{
    /* "All" resets every frame to selected. */
    if (filter == ImageFilter::All) {
        selectAll();
        return count();
    }

    /* "Selected" preserves the manual selection state managed elsewhere. */
    if (filter == ImageFilter::Selected) {
        return selectedCount();
    }

    /* Quality-based filters require pre-computed metrics. */
    if (!m_hasQualityMetrics) {
        return selectedCount();
    }

    const double threshold = computeFilterThreshold(filter, mode, parameter);

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
        if (passes) {
            ++passed;
        }
    }

    return passed;
}

std::vector<int> ImageSequence::getFilteredIndices() const
{
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
                                             double parameter) const
{
    if (!m_hasQualityMetrics) {
        return 0.0;
    }

    /* Collect the relevant metric from every image. */
    std::vector<double> values;
    values.reserve(m_images.size());

    for (const auto& img : m_images) {
        double val = 0.0;
        switch (filter) {
        case ImageFilter::BestFWHM:         val = img.quality.fwhm;                          break;
        case ImageFilter::BestWeightedFWHM: val = img.quality.weightedFwhm;                  break;
        case ImageFilter::BestRoundness:    val = img.quality.roundness;                     break;
        case ImageFilter::BestBackground:   val = img.quality.background;                    break;
        case ImageFilter::BestStarCount:    val = static_cast<double>(img.quality.starCount); break;
        case ImageFilter::BestQuality:      val = img.quality.quality;                       break;
        default:                            continue;
        }

        if (val > 0 || filter == ImageFilter::BestRoundness) {
            values.push_back(val);
        }
    }

    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());

    /* ---- Percentage-based threshold ---- */
    if (mode == FilterMode::Percentage) {
        int idx = 0;

        switch (filter) {
        case ImageFilter::BestFWHM:
        case ImageFilter::BestWeightedFWHM:
        case ImageFilter::BestBackground:
            /* Lower values are better -- accept the lowest N%. */
            idx = static_cast<int>(values.size() * parameter / 100.0);
            idx = std::min(idx, static_cast<int>(values.size()) - 1);
            return values[idx];

        case ImageFilter::BestRoundness:
        case ImageFilter::BestStarCount:
        case ImageFilter::BestQuality:
            /* Higher values are better -- accept the highest N%. */
            idx = static_cast<int>(values.size() * (100.0 - parameter) / 100.0);
            idx = std::max(idx, 0);
            return values[idx];

        default:
            return values.back();
        }
    }

    /* ---- K-sigma clipping threshold ---- */
    const double sum  = std::accumulate(values.begin(), values.end(), 0.0);
    const double mean = sum / values.size();

    double sqSum = 0.0;
    for (double v : values) {
        sqSum += (v - mean) * (v - mean);
    }
    const double sigma = std::sqrt(sqSum / values.size());

    switch (filter) {
    case ImageFilter::BestFWHM:
    case ImageFilter::BestWeightedFWHM:
    case ImageFilter::BestBackground:
        /* Lower is better -- reject above mean + k*sigma. */
        return mean + parameter * sigma;

    case ImageFilter::BestRoundness:
    case ImageFilter::BestStarCount:
    case ImageFilter::BestQuality:
        /* Higher is better -- reject below mean - k*sigma. */
        return mean - parameter * sigma;

    default:
        return mean;
    }
}

// ============================================================================
// Reference image
// ============================================================================

int ImageSequence::findBestReference() const
{
    if (!m_hasQualityMetrics || m_images.empty()) {
        return 0;
    }

    int    bestIdx     = 0;
    double bestQuality = -1.0;

    for (size_t i = 0; i < m_images.size(); ++i) {
        const auto& img = m_images[i];

        if (!img.selected || !img.quality.hasMetrics) {
            continue;
        }

        double q = img.quality.quality;

        /* Synthesise a simple quality score when the composite metric is absent. */
        if (q <= 0.0 && img.quality.fwhm > 0 && img.quality.roundness > 0) {
            q = img.quality.roundness / img.quality.fwhm;
        }

        if (q > bestQuality) {
            bestQuality = q;
            bestIdx     = static_cast<int>(i);
        }
    }

    return bestIdx;
}

// ============================================================================
// Sequence properties
// ============================================================================

double ImageSequence::totalExposure() const
{
    double total = 0.0;
    for (const auto& img : m_images) {
        if (img.selected) {
            total += img.exposure;
        }
    }
    return total;
}

bool ImageSequence::hasRegistration() const
{
    if (m_hasRegistration) {
        return true;
    }
    return std::any_of(m_images.begin(), m_images.end(),
                       [](const SequenceImage& img) {
                           return img.registration.hasRegistration;
                       });
}

// ============================================================================
// Registration
// ============================================================================

bool ImageSequence::loadRegistration(const QString& regFile)
{
    Q_UNUSED(regFile);
    /* Not yet implemented. */
    return false;
}

void ImageSequence::clearRegistration()
{
    for (auto& img : m_images) {
        img.registration = RegistrationData();
    }
    m_hasRegistration = false;
}

bool ImageSequence::isShiftOnlyRegistration() const
{
    for (const auto& img : m_images) {
        if (img.registration.hasRegistration && !img.registration.isShiftOnly()) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Quality metrics computation
// ============================================================================

bool ImageSequence::computeQualityMetrics(ProgressCallback progressCallback)
{
    if (progressCallback) {
        progressCallback(QObject::tr("Computing quality metrics..."), -1);
    }

    RegistrationEngine engine;

    /* Suppress unused-message warnings from the registration logger. */
    QObject::connect(&engine, &RegistrationEngine::logMessage,
                     [](const QString&, const QString&) {});

    for (size_t i = 0; i < m_images.size(); ++i) {
        if (progressCallback) {
            progressCallback(
                QObject::tr("Analyzing image %1/%2...")
                    .arg(i + 1).arg(m_images.size()),
                static_cast<double>(i) / m_images.size());
        }

        /* ---- Load frame ---- */
        ImageBuffer buffer;
        bool loaded = false;
        const QString& p = m_images[i].filePath;

        if (p.endsWith(".fits", Qt::CaseInsensitive) ||
            p.endsWith(".fit",  Qt::CaseInsensitive) ||
            p.endsWith(".fts",  Qt::CaseInsensitive)) {
            loaded = FitsLoader::load(p, buffer);
        } else if (p.endsWith(".tiff", Qt::CaseInsensitive) ||
                   p.endsWith(".tif",  Qt::CaseInsensitive)) {
            loaded = TiffIO::read(p, buffer);
        }

        if (!loaded) {
            continue;
        }

        const int    width    = buffer.width();
        const int    height   = buffer.height();
        const int    channels = buffer.channels();
        const float* data     = buffer.data().data();

        /* ---- Build luminance channel ---- */
        const size_t pixelCount = static_cast<size_t>(width) * height;
        std::vector<float> luminance(pixelCount);

        if (channels == 1) {
            std::copy(data, data + pixelCount, luminance.begin());
        } else {
            for (size_t px = 0; px < pixelCount; ++px) {
                const size_t base = px * channels;
                luminance[px] = 0.2126f * data[base]
                              + 0.7152f * data[base + 1]
                              + 0.0722f * data[base + 2];
            }
        }

        /* ---- Background and noise ---- */
        m_images[i].quality.background = Statistics::median(luminance);
        m_images[i].quality.noise      = Statistics::computeNoise(luminance.data(), width, height);

        /* ---- Star detection ---- */
        auto stars = engine.detectStars(buffer);

        if (!stars.empty()) {
            /* Sort by flux (using DetectedStar::operator<). */
            std::sort(stars.begin(), stars.end());

            /* Average FWHM and roundness over the brightest stars (max 50). */
            const int maxStars  = std::min(static_cast<int>(stars.size()), 50);
            float     sumFWHM      = 0.0f;
            float     sumRoundness = 0.0f;
            int       validCount   = 0;

            for (int k = 0; k < maxStars; ++k) {
                if (stars[k].fwhm > 0.5f && stars[k].fwhm < 20.0f) {
                    sumFWHM      += stars[k].fwhm;
                    sumRoundness += stars[k].roundness;
                    ++validCount;
                }
            }

            m_images[i].quality.hasMetrics = true;
            m_images[i].quality.starCount  = static_cast<int>(stars.size());

            if (validCount > 0) {
                const float avgFWHM = sumFWHM / validCount;

                m_images[i].quality.fwhm         = avgFWHM;
                m_images[i].quality.roundness     = sumRoundness / validCount;
                m_images[i].quality.weightedFwhm  = avgFWHM /
                    std::max(0.05, static_cast<double>(m_images[i].quality.roundness));

                /* Composite quality: more stars and sharper/rounder is better. */
                const double noiseTerm     = std::max(0.01,  m_images[i].quality.noise);
                const double sharpnessTerm = std::max(0.05,  static_cast<double>(m_images[i].quality.weightedFwhm));
                const double starTerm      = std::max(1.0,   static_cast<double>(m_images[i].quality.starCount));

                m_images[i].quality.quality = starTerm / (sharpnessTerm * noiseTerm);
            } else {
                /* Valid stars detected but none passed the FWHM sanity range. */
                m_images[i].quality.fwhm         = 99.0f;
                m_images[i].quality.weightedFwhm = 99.0f;
                m_images[i].quality.roundness     = 0.0f;
                m_images[i].quality.quality        = 0.0;
            }
        } else {
            /* No stars detected at all. */
            m_images[i].quality.hasMetrics   = true;
            m_images[i].quality.starCount    = 0;
            m_images[i].quality.fwhm         = 99.0f;
            m_images[i].quality.weightedFwhm = 99.0f;
            m_images[i].quality.roundness     = 0.0f;
            m_images[i].quality.quality        = 0.0;
        }
    }

    m_hasQualityMetrics = true;
    return true;
}

// ============================================================================
// Comet registration
// ============================================================================

bool ImageSequence::computeCometShifts(int refIndex, int targetIndex)
{
    if (refIndex < 0    || refIndex >= count() ||
        targetIndex < 0 || targetIndex >= count()) {
        return false;
    }

    const auto& refImg    = m_images[refIndex];
    const auto& targetImg = m_images[targetIndex];

    /* Parse observation timestamps from FITS DATE-OBS. */
    const QDateTime refTime    = QDateTime::fromString(refImg.metadata.dateObs,    Qt::ISODate);
    const QDateTime targetTime = QDateTime::fromString(targetImg.metadata.dateObs, Qt::ISODate);

    if (!refTime.isValid() || !targetTime.isValid()) {
        return false;
    }

    const double dtTotal = refTime.msecsTo(targetTime) / 1000.0;
    if (std::abs(dtTotal) < 0.1) {
        return false;   // Timestamps are too close to derive motion.
    }

    /*
     * Comet positions (in the aligned reference frame) must have been
     * set externally before this call.
     *
     * Velocity = (position_target - position_ref) / dt.
     * The compensating shift applied to each frame is -V * dt(frame).
     */
    const double cx1 = refImg.registration.cometX;
    const double cy1 = refImg.registration.cometY;
    const double cx2 = targetImg.registration.cometX;
    const double cy2 = targetImg.registration.cometY;

    const double vx = (cx2 - cx1) / dtTotal;
    const double vy = (cy2 - cy1) / dtTotal;

    for (auto& img : m_images) {
        const QDateTime t = QDateTime::fromString(img.metadata.dateObs, Qt::ISODate);
        if (!t.isValid()) {
            continue;
        }

        const double dt = refTime.msecsTo(t) / 1000.0;

        /* Additional shift that freezes the comet while stars trail. */
        img.registration.shiftX += -(vx * dt);
        img.registration.shiftY += -(vy * dt);
    }

    return true;
}

} // namespace Stacking