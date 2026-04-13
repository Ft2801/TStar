#ifndef SEQUENCE_FILE_H
#define SEQUENCE_FILE_H

/**
 * @file SequenceFile.h
 * @brief Persistent sequence file format (.tseq) for image stacking.
 *
 * A SequenceFile stores an ordered list of images together with per-frame
 * metadata (selection state, calibration/registration status, quality metrics,
 * and homography registration data).  The file is serialised as JSON.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include <QString>
#include <QVector>
#include <QJsonObject>

#include "StackingTypes.h"

namespace Stacking {

/**
 * @brief Manages a sequence of images for calibration or stacking.
 *
 * Provides methods for loading/saving from JSON, filtering by quality,
 * and managing per-image selection and registration state.
 */
class SequenceFile {
public:
    // ---- Per-image entry ---------------------------------------------------

    /**
     * @brief Metadata for a single image within the sequence.
     */
    struct ImageEntry {
        QString filename;
        bool    selected   = true;
        bool    calibrated = false;
        bool    registered = false;

        // Quality metrics
        double fwhm          = 0.0;
        double roundness      = 0.0;
        double background     = 0.0;
        int    starsDetected  = 0;

        // Registration data (populated after alignment)
        RegistrationData regData;

        /** @brief Serialise this entry to a JSON object. */
        QJsonObject toJson() const;

        /** @brief Deserialise an entry from a JSON object. */
        static ImageEntry fromJson(const QJsonObject& obj);
    };

    // ---- Sequence type -----------------------------------------------------

    /**
     * @brief Classification of the frame type contained in this sequence.
     */
    enum class SequenceType {
        Lights,
        Biases,
        Darks,
        Flats
    };

    // ---- Construction ------------------------------------------------------

    SequenceFile();
    ~SequenceFile() = default;

    // ---- File I/O ----------------------------------------------------------

    /** @brief Load a sequence from a .tseq JSON file. */
    bool load(const QString& path);

    /** @brief Save the sequence to the specified path. */
    bool save(const QString& path) const;

    /** @brief Save to the path from which the sequence was last loaded. */
    bool save() const;

    // ---- Image management --------------------------------------------------

    void addImage(const QString& filename);
    void addImages(const QStringList& filenames);
    void removeImage(int index);
    void clear();

    int count()         const { return m_images.size(); }
    int selectedCount() const;

    ImageEntry&       image(int index)       { return m_images[index]; }
    const ImageEntry& image(int index) const { return m_images[index]; }

    QVector<ImageEntry>&       images()       { return m_images; }
    const QVector<ImageEntry>& images() const { return m_images; }

    // ---- Selection ---------------------------------------------------------

    void selectAll();
    void selectNone();
    void toggleSelection(int index);
    void setSelected(int index, bool selected);

    // ---- Reference image ---------------------------------------------------

    int  referenceIndex() const { return m_referenceIndex; }
    void setReferenceIndex(int index);

    // ---- Sequence properties -----------------------------------------------

    SequenceType type() const             { return m_type; }
    void         setType(SequenceType t)  { m_type = t; }

    QString basePath() const                     { return m_basePath; }
    void    setBasePath(const QString& path)     { m_basePath = path; }

    bool useDrizzle() const        { return m_useDrizzle; }
    void setUseDrizzle(bool use)   { m_useDrizzle = use; }

    // ---- Status ------------------------------------------------------------

    bool    isValid()  const { return !m_images.isEmpty(); }
    QString filePath() const { return m_filePath; }

    // ---- Quality filtering -------------------------------------------------

    void filterByFWHM(double maxFWHM);
    void filterByRoundness(double minRoundness);
    void filterByStars(int minStars);
    void filterBestPercent(double percent);

    // ---- Serialisation helpers ----------------------------------------------

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj);

    static QString      typeToString(SequenceType type);
    static SequenceType typeFromString(const QString& str);

private:
    QVector<ImageEntry> m_images;
    SequenceType        m_type           = SequenceType::Lights;
    QString             m_basePath;
    QString             m_filePath;
    int                 m_referenceIndex = 0;
    bool                m_useDrizzle     = false;
};

} // namespace Stacking

#endif // SEQUENCE_FILE_H