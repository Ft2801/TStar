/**
 * @file SequenceFile.cpp
 * @brief Implementation of the persistent sequence file (.tseq) format.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "SequenceFile.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <algorithm>

namespace Stacking {

// ============================================================================
// ImageEntry Serialisation
// ============================================================================

QJsonObject SequenceFile::ImageEntry::toJson() const
{
    QJsonObject obj;
    obj["filename"]   = filename;
    obj["selected"]   = selected;
    obj["calibrated"] = calibrated;
    obj["registered"] = registered;

    // Quality metrics (only written when populated).
    if (fwhm > 0)          obj["fwhm"]       = fwhm;
    if (roundness > 0)     obj["roundness"]   = roundness;
    if (background > 0)    obj["background"]  = background;
    if (starsDetected > 0) obj["stars"]       = starsDetected;

    // Registration data (homography + basic transform parameters).
    if (regData.hasRegistration) {
        QJsonObject reg;
        reg["shiftX"]   = regData.shiftX;
        reg["shiftY"]   = regData.shiftY;
        reg["rotation"] = regData.rotation;
        reg["scaleX"]   = regData.scaleX;
        reg["scaleY"]   = regData.scaleY;

        // Flatten the 3x3 homography matrix into a JSON array of 9 elements.
        QJsonArray h;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                h.append(regData.H[i][j]);
        reg["homography"] = h;

        obj["registration"] = reg;
    }

    return obj;
}

SequenceFile::ImageEntry SequenceFile::ImageEntry::fromJson(const QJsonObject& obj)
{
    ImageEntry entry;
    entry.filename   = obj["filename"].toString();
    entry.selected   = obj["selected"].toBool(true);
    entry.calibrated = obj["calibrated"].toBool(false);
    entry.registered = obj["registered"].toBool(false);

    entry.fwhm          = obj["fwhm"].toDouble(0);
    entry.roundness     = obj["roundness"].toDouble(0);
    entry.background    = obj["background"].toDouble(0);
    entry.starsDetected = obj["stars"].toInt(0);

    if (obj.contains("registration")) {
        QJsonObject reg = obj["registration"].toObject();
        entry.regData.hasRegistration = true;
        entry.regData.shiftX   = reg["shiftX"].toDouble();
        entry.regData.shiftY   = reg["shiftY"].toDouble();
        entry.regData.rotation = reg["rotation"].toDouble();
        entry.regData.scaleX   = reg["scaleX"].toDouble(1.0);
        entry.regData.scaleY   = reg["scaleY"].toDouble(1.0);

        QJsonArray h = reg["homography"].toArray();
        if (h.size() == 9) {
            int idx = 0;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    entry.regData.H[i][j] = h[idx++].toDouble();
        }
    }

    return entry;
}

// ============================================================================
// Construction
// ============================================================================

SequenceFile::SequenceFile() = default;

// ============================================================================
// File I/O
// ============================================================================

bool SequenceFile::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
        return false;

    m_filePath = path;
    m_basePath = QFileInfo(path).absolutePath();

    return fromJson(doc.object());
}

bool SequenceFile::save(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

bool SequenceFile::save() const
{
    if (m_filePath.isEmpty())
        return false;
    return save(m_filePath);
}

// ============================================================================
// Image Management
// ============================================================================

void SequenceFile::addImage(const QString& filename)
{
    ImageEntry entry;
    entry.filename = filename;
    m_images.append(entry);
}

void SequenceFile::addImages(const QStringList& filenames)
{
    for (const QString& f : filenames)
        addImage(f);
}

void SequenceFile::removeImage(int index)
{
    if (index >= 0 && index < m_images.size()) {
        m_images.remove(index);
        if (m_referenceIndex >= m_images.size())
            m_referenceIndex = m_images.isEmpty() ? 0 : m_images.size() - 1;
    }
}

void SequenceFile::clear()
{
    m_images.clear();
    m_referenceIndex = 0;
}

int SequenceFile::selectedCount() const
{
    int count = 0;
    for (const auto& img : m_images) {
        if (img.selected) ++count;
    }
    return count;
}

// ============================================================================
// Selection
// ============================================================================

void SequenceFile::selectAll()
{
    for (auto& img : m_images)
        img.selected = true;
}

void SequenceFile::selectNone()
{
    for (auto& img : m_images)
        img.selected = false;
}

void SequenceFile::toggleSelection(int index)
{
    if (index >= 0 && index < m_images.size())
        m_images[index].selected = !m_images[index].selected;
}

void SequenceFile::setSelected(int index, bool selected)
{
    if (index >= 0 && index < m_images.size())
        m_images[index].selected = selected;
}

void SequenceFile::setReferenceIndex(int index)
{
    if (index >= 0 && index < m_images.size())
        m_referenceIndex = index;
}

// ============================================================================
// Quality Filtering
// ============================================================================

void SequenceFile::filterByFWHM(double maxFWHM)
{
    for (auto& img : m_images) {
        if (img.fwhm > 0 && img.fwhm > maxFWHM)
            img.selected = false;
    }
}

void SequenceFile::filterByRoundness(double minRoundness)
{
    for (auto& img : m_images) {
        if (img.roundness > 0 && img.roundness < minRoundness)
            img.selected = false;
    }
}

void SequenceFile::filterByStars(int minStars)
{
    for (auto& img : m_images) {
        if (img.starsDetected > 0 && img.starsDetected < minStars)
            img.selected = false;
    }
}

void SequenceFile::filterBestPercent(double percent)
{
    if (m_images.isEmpty()) return;

    // Rank images by FWHM (lower is better) and keep the top N percent.
    QVector<int> indices(m_images.size());
    for (int i = 0; i < m_images.size(); ++i)
        indices[i] = i;

    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return m_images[a].fwhm < m_images[b].fwhm;
    });

    int keepCount = static_cast<int>(m_images.size() * percent / 100.0);
    keepCount = std::max(1, keepCount);

    for (int i = 0; i < m_images.size(); ++i)
        m_images[indices[i]].selected = (i < keepCount);
}

// ============================================================================
// Serialisation
// ============================================================================

QJsonObject SequenceFile::toJson() const
{
    QJsonObject obj;
    obj["version"]   = 1;
    obj["type"]      = typeToString(m_type);
    obj["reference"] = m_referenceIndex;
    obj["drizzle"]   = m_useDrizzle;

    QJsonArray imagesArray;
    for (const auto& img : m_images)
        imagesArray.append(img.toJson());
    obj["images"] = imagesArray;

    return obj;
}

bool SequenceFile::fromJson(const QJsonObject& obj)
{
    if (!obj.contains("version") || !obj.contains("images"))
        return false;

    m_type           = typeFromString(obj["type"].toString("lights"));
    m_referenceIndex = obj["reference"].toInt(0);
    m_useDrizzle     = obj["drizzle"].toBool(false);

    m_images.clear();
    QJsonArray imagesArray = obj["images"].toArray();
    for (const auto& val : imagesArray)
        m_images.append(ImageEntry::fromJson(val.toObject()));

    // Clamp the reference index to valid range.
    if (m_referenceIndex >= m_images.size())
        m_referenceIndex = 0;

    return true;
}

// ============================================================================
// Type Conversion Helpers
// ============================================================================

QString SequenceFile::typeToString(SequenceType type)
{
    switch (type) {
    case SequenceType::Lights: return "lights";
    case SequenceType::Biases: return "biases";
    case SequenceType::Darks:  return "darks";
    case SequenceType::Flats:  return "flats";
    }
    return "lights";
}

SequenceFile::SequenceType SequenceFile::typeFromString(const QString& str)
{
    const QString lower = str.toLower();
    if (lower == "biases" || lower == "bias") return SequenceType::Biases;
    if (lower == "darks"  || lower == "dark") return SequenceType::Darks;
    if (lower == "flats"  || lower == "flat") return SequenceType::Flats;
    return SequenceType::Lights;
}

} // namespace Stacking