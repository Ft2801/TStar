/**
 * @file StackingProject.cpp
 * @brief Implementation of StackingProject -- project directory management
 *        and JSON persistence.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "StackingProject.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDateTime>
#include <QSettings>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Stacking {

// ============================================================================
// Construction
// ============================================================================

StackingProject::StackingProject() = default;

StackingProject::StackingProject(const QString& rootPath)
{
    create(rootPath);
}

// ============================================================================
// Project creation
// ============================================================================

bool StackingProject::create(const QString& rootPath)
{
    /* Resolve and store the canonical root path. */
    m_rootDir    = QDir(rootPath).absolutePath();
    m_biasDir    = m_rootDir + "/biases";
    m_darkDir    = m_rootDir + "/darks";
    m_flatDir    = m_rootDir + "/flats";
    m_lightDir   = m_rootDir + "/lights";
    m_processDir = m_rootDir + "/process";
    m_outputDir  = m_rootDir + "/output";

    /* Derive the project name from the directory basename. */
    m_name = QFileInfo(m_rootDir).fileName();

    if (!createDirectories()) {
        m_valid = false;
        return false;
    }

    m_valid = true;
    return save();
}

bool StackingProject::createDirectories()
{
    QDir root(m_rootDir);
    if (!root.exists() && !root.mkpath(".")) {
        return false;
    }

    const QStringList dirs = {
        m_biasDir, m_darkDir, m_flatDir,
        m_lightDir, m_processDir, m_outputDir
    };

    for (const QString& dir : dirs) {
        QDir d(dir);
        if (!d.exists() && !d.mkpath(".")) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// Load / Save
// ============================================================================

bool StackingProject::load(const QString& projectFile)
{
    QFile file(projectFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return false;
    }

    /* The root directory is the parent of the project file. */
    m_rootDir = QFileInfo(projectFile).absolutePath();

    if (!fromJson(doc.object())) {
        return false;
    }

    /* Rebuild sub-directory paths relative to the root. */
    m_biasDir    = m_rootDir + "/biases";
    m_darkDir    = m_rootDir + "/darks";
    m_flatDir    = m_rootDir + "/flats";
    m_lightDir   = m_rootDir + "/lights";
    m_processDir = m_rootDir + "/process";
    m_outputDir  = m_rootDir + "/output";

    m_valid = true;
    return true;
}

bool StackingProject::save() const
{
    const QString projectFile = resolveProjectFilePath();
    QFileInfo fi(projectFile);
    QDir().mkpath(fi.absolutePath());

    QFile file(projectFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QString StackingProject::resolveProjectFilePath() const
{
    /*
     * If the user has configured a custom project-root directory in
     * application settings, try to place the project file there.
     * Otherwise fall back to a hidden file inside the project root.
     */
    QSettings settings;
    const QString userRoot = settings.value("paths/project_root").toString().trimmed();

    if (!userRoot.isEmpty()) {
        QDir ud(userRoot);
        if (ud.exists() || ud.mkpath(".")) {
            return ud.absoluteFilePath(m_name + ".tstar_project.json");
        }
    }

    return m_rootDir + "/.tstar_project.json";
}

// ============================================================================
// Serialisation
// ============================================================================

QJsonObject StackingProject::toJson() const
{
    QJsonObject obj;
    obj["version"] = 1;
    obj["name"]    = m_name;
    obj["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    /* Master calibration frame paths are stored relative to the project root. */
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

bool StackingProject::fromJson(const QJsonObject& obj)
{
    if (!obj.contains("version")) {
        return false;
    }

    m_name = obj["name"].toString();

    /* Restore master frame paths, converting from relative to absolute. */
    const QJsonObject masters = obj["masters"].toObject();
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

// ============================================================================
// File naming helpers
// ============================================================================

QString StackingProject::preprocessedName(const QString& originalName) const
{
    const QString baseName = QFileInfo(originalName).completeBaseName();
    return m_processDir + "/pp_" + baseName + ".fit";
}

QString StackingProject::registeredName(const QString& originalName) const
{
    const QString baseName = QFileInfo(originalName).completeBaseName();

    /* If the file was already preprocessed, just prepend the registration prefix. */
    if (baseName.startsWith("pp_")) {
        return m_processDir + "/r_" + baseName + ".fit";
    }

    return m_processDir + "/r_pp_" + baseName + ".fit";
}

QString StackingProject::stackedName(const QString& suffix) const
{
    QString name = m_name.isEmpty() ? "result" : m_name;
    if (!suffix.isEmpty()) {
        name += "_" + suffix;
    }
    return m_outputDir + "/" + name + "_stacked.fit";
}

// ============================================================================
// Symbolic link / copy support
// ============================================================================

bool StackingProject::canCreateSymlinks()
{
#ifdef Q_OS_WIN
    /*
     * On Windows, symbolic links require either administrator privileges
     * or Developer Mode.  We perform a quick test to determine availability.
     */
    const QString testTarget = QDir::tempPath() + "/tstar_symlink_test_target.tmp";
    const QString testLink   = QDir::tempPath() + "/tstar_symlink_test_link.tmp";

    /* Create a temporary target file. */
    QFile target(testTarget);
    if (!target.open(QIODevice::WriteOnly)) {
        return false;
    }
    target.write("test");
    target.close();

    /* Attempt to create a symbolic link pointing at the target. */
    const bool success = CreateSymbolicLinkW(
        testLink.toStdWString().c_str(),
        testTarget.toStdWString().c_str(),
        0   // 0 = file link; SYMBOLIC_LINK_FLAG_DIRECTORY for directories
    );

    /* Clean up temporary files regardless of outcome. */
    QFile::remove(testLink);
    QFile::remove(testTarget);

    return success;
#else
    /* POSIX systems generally support symlinks without special permissions. */
    return true;
#endif
}

bool StackingProject::createLink(const QString& target, const QString& linkPath)
{
    /* Remove any pre-existing file or link at the destination. */
    if (QFile::exists(linkPath)) {
        QFile::remove(linkPath);
    }

#ifdef Q_OS_WIN
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

    /* Fall back to a full copy if symlink creation is not permitted. */
    return QFile::copy(target, linkPath);
#else
    if (QFile::link(target, linkPath)) {
        return true;
    }

    /* Fall back to a full copy if symlink creation fails. */
    return QFile::copy(target, linkPath);
#endif
}

// ============================================================================
// Project discovery
// ============================================================================

QString StackingProject::findProjectFile(const QString& directory)
{
    /* Check for the standard hidden project file inside the directory. */
    const QString projectFile = directory + "/.tstar_project.json";
    if (QFile::exists(projectFile)) {
        return projectFile;
    }

    /* Check the user-configured project root for a matching project file. */
    QSettings settings;
    const QString userRoot = settings.value("paths/project_root").toString().trimmed();
    if (!userRoot.isEmpty()) {
        QDir ud(userRoot);
        const QString name      = QFileInfo(directory).fileName();
        const QString candidate = ud.absoluteFilePath(name + ".tstar_project.json");
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }

    return QString();
}

bool StackingProject::isProjectDirectory(const QString& directory)
{
    return !findProjectFile(directory).isEmpty();
}

} // namespace Stacking