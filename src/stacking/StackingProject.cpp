#include "StackingProject.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Stacking {

//=============================================================================
// CONSTRUCTOR
//=============================================================================

StackingProject::StackingProject() = default;

StackingProject::StackingProject(const QString& rootPath) {
    create(rootPath);
}

//=============================================================================
// PROJECT CREATION
//=============================================================================

bool StackingProject::create(const QString& rootPath) {
    m_rootDir = QDir(rootPath).absolutePath();
    m_biasDir = m_rootDir + "/biases";
    m_darkDir = m_rootDir + "/darks";
    m_flatDir = m_rootDir + "/flats";
    m_lightDir = m_rootDir + "/lights";
    m_processDir = m_rootDir + "/process";
    m_outputDir = m_rootDir + "/output";
    
    // Extract project name from directory
    m_name = QFileInfo(m_rootDir).fileName();
    
    if (!createDirectories()) {
        m_valid = false;
        return false;
    }
    
    m_valid = true;
    return save();
}

bool StackingProject::createDirectories() {
    QDir root(m_rootDir);
    
    if (!root.exists() && !root.mkpath(".")) {
        return false;
    }
    
    QStringList dirs = {m_biasDir, m_darkDir, m_flatDir, m_lightDir, m_processDir, m_outputDir};
    
    for (const QString& dir : dirs) {
        QDir d(dir);
        if (!d.exists() && !d.mkpath(".")) {
            return false;
        }
    }
    
    return true;
}

//=============================================================================
// LOAD / SAVE
//=============================================================================

bool StackingProject::load(const QString& projectFile) {
    QFile file(projectFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (!doc.isObject()) {
        return false;
    }
    
    // Set root directory from project file location
    m_rootDir = QFileInfo(projectFile).absolutePath();
    
    if (!fromJson(doc.object())) {
        return false;
    }
    
    // Reconstruct directory paths
    m_biasDir = m_rootDir + "/biases";
    m_darkDir = m_rootDir + "/darks";
    m_flatDir = m_rootDir + "/flats";
    m_lightDir = m_rootDir + "/lights";
    m_processDir = m_rootDir + "/process";
    m_outputDir = m_rootDir + "/output";
    
    m_valid = true;
    return true;
}

bool StackingProject::save() const {
    QString projectFile = m_rootDir + "/.tstar_project.json";
    
    QFile file(projectFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    return true;
}

//=============================================================================
// SERIALIZATION
//=============================================================================

QJsonObject StackingProject::toJson() const {
    QJsonObject obj;
    obj["version"] = 1;
    obj["name"] = m_name;
    obj["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // Master frames (relative paths)
    QJsonObject masters;
    if (!m_masterBias.isEmpty()) {
        masters["bias"] = QDir(m_rootDir).relativeFilePath(m_masterBias);
    }
    if (!m_masterDark.isEmpty()) {
        masters["dark"] = QDir(m_rootDir).relativeFilePath(m_masterDark);
    }
    if (!m_masterFlat.isEmpty()) {
        masters["flat"] = QDir(m_rootDir).relativeFilePath(m_masterFlat);
    }
    obj["masters"] = masters;
    
    return obj;
}

bool StackingProject::fromJson(const QJsonObject& obj) {
    if (!obj.contains("version")) {
        return false;
    }
    
    m_name = obj["name"].toString();
    
    // Load master frames
    QJsonObject masters = obj["masters"].toObject();
    if (masters.contains("bias")) {
        m_masterBias = m_rootDir + "/" + masters["bias"].toString();
    }
    if (masters.contains("dark")) {
        m_masterDark = m_rootDir + "/" + masters["dark"].toString();
    }
    if (masters.contains("flat")) {
        m_masterFlat = m_rootDir + "/" + masters["flat"].toString();
    }
    
    return true;
}

//=============================================================================
// FILE NAMING
//=============================================================================

QString StackingProject::preprocessedName(const QString& originalName) const {
    QString baseName = QFileInfo(originalName).completeBaseName();
    return m_processDir + "/pp_" + baseName + ".fit";
}

QString StackingProject::registeredName(const QString& originalName) const {
    QString baseName = QFileInfo(originalName).completeBaseName();
    // If already preprocessed, just add r_ prefix
    if (baseName.startsWith("pp_")) {
        return m_processDir + "/r_" + baseName + ".fit";
    }
    return m_processDir + "/r_pp_" + baseName + ".fit";
}

QString StackingProject::stackedName(const QString& suffix) const {
    QString name = m_name.isEmpty() ? "result" : m_name;
    if (!suffix.isEmpty()) {
        name += "_" + suffix;
    }
    return m_outputDir + "/" + name + "_stacked.fit";
}

//=============================================================================
// SYMBOLIC LINKS
//=============================================================================

bool StackingProject::canCreateSymlinks() {
#ifdef Q_OS_WIN
    // Test if we can create symlinks (requires admin or developer mode)
    QString testTarget = QDir::tempPath() + "/tstar_symlink_test_target.tmp";
    QString testLink = QDir::tempPath() + "/tstar_symlink_test_link.tmp";
    
    // Create test target
    QFile target(testTarget);
    if (!target.open(QIODevice::WriteOnly)) {
        return false;
    }
    target.write("test");
    target.close();
    
    // Try to create symlink
    bool success = CreateSymbolicLinkW(
        testLink.toStdWString().c_str(),
        testTarget.toStdWString().c_str(),
        0  // 0 for file, SYMBOLIC_LINK_FLAG_DIRECTORY for directory
    );
    
    // Cleanup
    QFile::remove(testLink);
    QFile::remove(testTarget);
    
    return success;
#else
    // Unix-like systems generally support symlinks
    return true;
#endif
}

bool StackingProject::createLink(const QString& target, const QString& linkPath) {
    // Remove existing link/file
    if (QFile::exists(linkPath)) {
        QFile::remove(linkPath);
    }
    
#ifdef Q_OS_WIN
    // Windows symlink
    DWORD flags = 0;
    if (QFileInfo(target).isDir()) {
        flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    }
    
    if (CreateSymbolicLinkW(
            linkPath.toStdWString().c_str(),
            target.toStdWString().c_str(),
            flags)) {
        return true;
    }
    
    // Fallback to copy if symlink fails
    return QFile::copy(target, linkPath);
#else
    // Unix symlink
    if (QFile::link(target, linkPath)) {
        return true;
    }
    // Fallback to copy
    return QFile::copy(target, linkPath);
#endif
}

//=============================================================================
// PROJECT DISCOVERY
//=============================================================================

QString StackingProject::findProjectFile(const QString& directory) {
    QString projectFile = directory + "/.tstar_project.json";
    if (QFile::exists(projectFile)) {
        return projectFile;
    }
    return QString();
}

bool StackingProject::isProjectDirectory(const QString& directory) {
    return !findProjectFile(directory).isEmpty();
}

} // namespace Stacking
