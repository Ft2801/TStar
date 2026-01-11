#ifndef STACKING_PROJECT_H
#define STACKING_PROJECT_H

#include <QString>
#include <QDir>
#include <QJsonObject>
#include <QVector>

namespace Stacking {

class StackingProject {
public:
    StackingProject();
    explicit StackingProject(const QString& rootPath);
    ~StackingProject() = default;
    
    // Project Creation
    bool create(const QString& rootPath);
    bool load(const QString& projectFile);
    bool save() const;
    
    // Directory Access
    QString rootDir() const { return m_rootDir; }
    QString biasDir() const { return m_biasDir; }
    QString darkDir() const { return m_darkDir; }
    QString flatDir() const { return m_flatDir; }
    QString lightDir() const { return m_lightDir; }
    QString processDir() const { return m_processDir; }
    QString outputDir() const { return m_outputDir; }
    
    // Project State
    bool isValid() const { return m_valid; }
    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    
    // Master Frames
    QString masterBias() const { return m_masterBias; }
    QString masterDark() const { return m_masterDark; }
    QString masterFlat() const { return m_masterFlat; }
    void setMasterBias(const QString& path) { m_masterBias = path; }
    void setMasterDark(const QString& path) { m_masterDark = path; }
    void setMasterFlat(const QString& path) { m_masterFlat = path; }
    
    // File Naming Helpers
    QString preprocessedName(const QString& originalName) const;
    QString registeredName(const QString& originalName) const;
    QString stackedName(const QString& suffix = QString()) const;
    
    // Symbolic Link / Copy Support
    static bool createLink(const QString& target, const QString& linkPath);
    static bool canCreateSymlinks();
    
    // Project File Discovery
    static QString findProjectFile(const QString& directory);
    static bool isProjectDirectory(const QString& directory);
    
    // Serialization
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj);
    
private:
    bool createDirectories();
    
    QString m_rootDir;
    QString m_biasDir;
    QString m_darkDir;
    QString m_flatDir;
    QString m_lightDir;
    QString m_processDir;
    QString m_outputDir;
    
    QString m_name;
    QString m_masterBias;
    QString m_masterDark;
    QString m_masterFlat;
    
    bool m_valid = false;
};

} // namespace Stacking

#endif // STACKING_PROJECT_H
