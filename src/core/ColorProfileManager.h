#pragma once

#include <QString>
#include <QByteArray>
#include <QObject>
#include <QMutex>
#include <memory>

// Forward declarations
class ImageBuffer;

namespace core {

enum class StandardProfile {
    sRGB,
    AdobeRGB,
    ProPhotoRGB,
    LinearRGB,
    Custom
};

enum class AutoConversionMode {
    Never,      // Never ask, don't convert
    Ask,        // Ask user for each mismatch
    Always      // Auto-convert without asking
};

/**
 * @brief Represents a Color Profile (ICC) for images or workspace.
 */
class ColorProfile {
public:
    ColorProfile();
    explicit ColorProfile(StandardProfile type);
    explicit ColorProfile(const QString& iccFilePath);
    explicit ColorProfile(const QByteArray& iccData);

    bool isValid() const;
    StandardProfile type() const;
    QString name() const;
    QByteArray iccData() const;

    bool operator==(const ColorProfile& other) const;
    bool operator!=(const ColorProfile& other) const;

private:
    StandardProfile m_type;
    QString m_name;
    QByteArray m_iccData;
    bool m_valid;
    
    void loadStandardProfile(StandardProfile type);
};

/**
 * @brief Manages Application Workspace color profiles and conversions.
 * Implements the Singleton pattern.
 */
class ColorProfileManager : public QObject {
    Q_OBJECT

public:
    static ColorProfileManager& instance();

    // Workspace management
    void setWorkspaceProfile(const ColorProfile& profile);
    ColorProfile workspaceProfile() const;
    
    // Auto-conversion preference  
    void setAutoConversionMode(AutoConversionMode mode);
    AutoConversionMode autoConversionMode() const;

    // Reload settings from QSettings
    void syncSettings();

    // Image operations & Validation
    bool isMismatch(const ColorProfile& imageProfile) const;
    bool convertToWorkspace(ImageBuffer& buffer, const ColorProfile& sourceProfile);
    bool convertProfile(ImageBuffer& buffer, const ColorProfile& sourceProfile, const ColorProfile& targetProfile);

    /**
     * @brief Asynchronous version of convertProfile. Returns a Task that can be submitted to TaskManager.
     */
    void convertProfileAsync(ImageBuffer& buffer, const ColorProfile& sourceProfile, const ColorProfile& targetProfile);

signals:
    void workspaceProfileChanged(const ColorProfile& newProfile);
    void conversionStarted(quint64 taskId);
    void conversionFinished(quint64 taskId);
    void conversionFailed(quint64 taskId, const QString& error);

private:
    ColorProfileManager();
    ~ColorProfileManager();
    Q_DISABLE_COPY(ColorProfileManager)

    ColorProfile m_workspaceProfile;
    AutoConversionMode m_autoConversionMode;
    mutable QMutex m_mutex;
};

} // namespace core