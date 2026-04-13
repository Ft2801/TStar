/**
 * @file StackingProject.h
 * @brief Project-level management for astrophotography stacking workflows.
 *
 * Encapsulates the directory layout, master calibration frame paths,
 * serialisation to/from JSON, and convenience helpers for file naming
 * and symbolic link creation.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#ifndef STACKING_PROJECT_H
#define STACKING_PROJECT_H

#include <QString>
#include <QDir>
#include <QJsonObject>
#include <QVector>

namespace Stacking {

/**
 * @brief Represents a stacking project on disk.
 *
 * A project owns a root directory that contains standardised sub-folders
 * for calibration frames (biases, darks, flats), light frames, intermediate
 * processing results, and final output images.  The project state is
 * persisted as a small JSON file.
 */
class StackingProject {
public:
    // -- Construction --------------------------------------------------------

    StackingProject();

    /**
     * @brief Construct and immediately create a project rooted at @p rootPath.
     * @param rootPath Filesystem path for the project root directory.
     */
    explicit StackingProject(const QString& rootPath);

    ~StackingProject() = default;

    // -- Project lifecycle ---------------------------------------------------

    /**
     * @brief Create a new project at the given root path.
     *
     * Builds the standard sub-directory tree and writes the initial
     * project file.
     *
     * @param rootPath Filesystem path for the project root directory.
     * @return true on success.
     */
    bool create(const QString& rootPath);

    /**
     * @brief Load an existing project from a JSON project file.
     * @param projectFile Path to the .tstar_project.json file.
     * @return true on success.
     */
    bool load(const QString& projectFile);

    /**
     * @brief Persist the current project state to disk.
     * @return true on success.
     */
    bool save() const;

    // -- Directory accessors -------------------------------------------------

    QString rootDir()    const { return m_rootDir;    }
    QString biasDir()    const { return m_biasDir;    }
    QString darkDir()    const { return m_darkDir;    }
    QString flatDir()    const { return m_flatDir;    }
    QString lightDir()   const { return m_lightDir;   }
    QString processDir() const { return m_processDir; }
    QString outputDir()  const { return m_outputDir;  }

    // -- Project state -------------------------------------------------------

    bool    isValid() const { return m_valid; }
    QString name()    const { return m_name;  }

    void setName(const QString& name) { m_name = name; }

    // -- Master calibration frames -------------------------------------------

    QString masterBias() const { return m_masterBias; }
    QString masterDark() const { return m_masterDark; }
    QString masterFlat() const { return m_masterFlat; }

    void setMasterBias(const QString& path) { m_masterBias = path; }
    void setMasterDark(const QString& path) { m_masterDark = path; }
    void setMasterFlat(const QString& path) { m_masterFlat = path; }

    // -- File naming helpers -------------------------------------------------

    /**
     * @brief Generate the preprocessed output path for an original frame.
     * @param originalName Original filename (may include path).
     * @return Full path to the preprocessed file in the process directory.
     */
    QString preprocessedName(const QString& originalName) const;

    /**
     * @brief Generate the registered output path for an original frame.
     * @param originalName Original filename (may include path).
     * @return Full path to the registered file in the process directory.
     */
    QString registeredName(const QString& originalName) const;

    /**
     * @brief Generate the final stacked output path.
     * @param suffix Optional descriptive suffix appended to the project name.
     * @return Full path to the stacked result in the output directory.
     */
    QString stackedName(const QString& suffix = QString()) const;

    // -- Symbolic link / copy support ----------------------------------------

    /**
     * @brief Create a symbolic link (or fall back to file copy).
     * @param target   Path to the existing file or directory.
     * @param linkPath Desired path for the new link.
     * @return true on success.
     */
    static bool createLink(const QString& target, const QString& linkPath);

    /**
     * @brief Test whether the current platform/permissions allow symlink creation.
     * @return true if symbolic links can be created.
     */
    static bool canCreateSymlinks();

    // -- Project file discovery ----------------------------------------------

    /**
     * @brief Locate the project file associated with a directory.
     * @param directory Directory to search.
     * @return Path to the project file, or an empty string if none found.
     */
    static QString findProjectFile(const QString& directory);

    /**
     * @brief Check whether a directory contains (or is associated with) a project.
     * @param directory Directory to test.
     * @return true if a project file was found.
     */
    static bool isProjectDirectory(const QString& directory);

    // -- Serialisation -------------------------------------------------------

    /**
     * @brief Serialise project state into a JSON object.
     * @return QJsonObject representing the project.
     */
    QJsonObject toJson() const;

    /**
     * @brief Deserialise project state from a JSON object.
     * @param obj JSON object previously produced by toJson().
     * @return true on success.
     */
    bool fromJson(const QJsonObject& obj);

private:
    /** @brief Ensure all required sub-directories exist. */
    bool createDirectories();

    /** @brief Resolve the on-disk path for the project JSON file. */
    QString resolveProjectFilePath() const;

    // Directory paths
    QString m_rootDir;
    QString m_biasDir;
    QString m_darkDir;
    QString m_flatDir;
    QString m_lightDir;
    QString m_processDir;
    QString m_outputDir;

    // Project metadata
    QString m_name;
    QString m_masterBias;
    QString m_masterDark;
    QString m_masterFlat;

    bool m_valid = false;
};

} // namespace Stacking

#endif // STACKING_PROJECT_H