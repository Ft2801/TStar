#ifndef SEQUENCE_FILE_H
#define SEQUENCE_FILE_H

#include <QString>
#include <QVector>
#include <QJsonObject>
#include "StackingTypes.h"

namespace Stacking {

/**
 * @brief Persistent sequence file format (.tseq)
 * 
 * Stores sequence metadata, image list, calibration/registration status,
 * and quality metrics for each frame.
 */
class SequenceFile {
public:
    /**
     * @brief Per-image entry in sequence
     */
    struct ImageEntry {
        QString filename;
        bool selected = true;
        bool calibrated = false;
        bool registered = false;
        
        // Quality metrics
        double fwhm = 0.0;
        double roundness = 0.0;
        double background = 0.0;
        int starsDetected = 0;
        
        // Registration data (if registered)
        RegistrationData regData;
        
        QJsonObject toJson() const;
        static ImageEntry fromJson(const QJsonObject& obj);
    };
    
    enum class SequenceType {
        Lights,
        Biases,
        Darks,
        Flats
    };
    
    SequenceFile();
    ~SequenceFile() = default;
    
    // File I/O
    bool load(const QString& path);
    bool save(const QString& path) const;
    bool save() const;  // Save to loaded path
    
    // Image Management
    void addImage(const QString& filename);
    void addImages(const QStringList& filenames);
    void removeImage(int index);
    void clear();
    
    int count() const { return m_images.size(); }
    int selectedCount() const;
    
    ImageEntry& image(int index) { return m_images[index]; }
    const ImageEntry& image(int index) const { return m_images[index]; }
    QVector<ImageEntry>& images() { return m_images; }
    const QVector<ImageEntry>& images() const { return m_images; }
    
    // Selection
    void selectAll();
    void selectNone();
    void toggleSelection(int index);
    void setSelected(int index, bool selected);
    
    // Reference Image
    int referenceIndex() const { return m_referenceIndex; }
    void setReferenceIndex(int index);
    
    // Sequence Properties
    SequenceType type() const { return m_type; }
    void setType(SequenceType type) { m_type = type; }
    
    QString basePath() const { return m_basePath; }
    void setBasePath(const QString& path) { m_basePath = path; }
    
    // Registration Settings
    bool useDrizzle() const { return m_useDrizzle; }
    void setUseDrizzle(bool use) { m_useDrizzle = use; }
    
    // Status
    bool isValid() const { return !m_images.isEmpty(); }
    QString filePath() const { return m_filePath; }
    
    // Quality Filtering
    void filterByFWHM(double maxFWHM);
    void filterByRoundness(double minRoundness);
    void filterByStars(int minStars);
    void filterBestPercent(double percent);
    
    // Serialization
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj);
    
    // Static helpers
    static QString typeToString(SequenceType type);
    static SequenceType typeFromString(const QString& str);
    
private:
    QVector<ImageEntry> m_images;
    SequenceType m_type = SequenceType::Lights;
    QString m_basePath;
    QString m_filePath;
    int m_referenceIndex = 0;
    bool m_useDrizzle = false;
};

} // namespace Stacking

#endif // SEQUENCE_FILE_H
