/**
 * @file StackingSequence.h
 * @brief Image sequence management for stacking operations.
 *
 * Defines the SequenceImage descriptor and the ImageSequence container
 * that handle loading, metadata caching, quality analysis, filtering,
 * selection, registration bookkeeping and comet-mode alignment for a
 * set of images destined for stacking.
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#ifndef STACKING_SEQUENCE_H
#define STACKING_SEQUENCE_H

#include "StackingTypes.h"
#include "../ImageBuffer.h"

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <vector>
#include <memory>

namespace Stacking {

// ============================================================================
// SequenceImage -- per-frame descriptor
// ============================================================================

/**
 * @brief Metadata and state for a single image within a stacking sequence.
 *
 * Caches file-level properties (dimensions, bit depth, exposure) together
 * with per-frame registration transforms and quality metrics so that the
 * stacking pipeline can operate without re-reading the files repeatedly.
 */
struct SequenceImage {

    QString filePath;               ///< Absolute path to the image file.
    bool    selected    = true;     ///< Whether this frame is included in stacking.
    int     index       = 0;        ///< Original (load-order) index in the sequence.

    // -- Cached image dimensions ---------------------------------------------
    int  width    = 0;
    int  height   = 0;
    int  channels = 0;
    int  bitDepth = 16;             ///< Bits per sample (8, 16, or 32).
    bool isFloat  = false;          ///< True when the on-disk format is 32-bit float.

    // -- Registration --------------------------------------------------------
    RegistrationData registration;

    // -- Quality metrics -----------------------------------------------------
    ImageQuality quality;

    // -- FITS / image metadata (cached) --------------------------------------
    ImageBuffer::Metadata metadata;

    // -- Stacking-specific ---------------------------------------------------
    double exposure   = 0.0;        ///< Exposure time in seconds.
    int    stackCount = 1;          ///< Prior stack depth (for master-of-masters).

    /**
     * @brief Return the filename component (no directory path).
     */
    QString fileName() const { return QFileInfo(filePath).fileName(); }

    /**
     * @brief Return the filename without its extension.
     */
    QString baseName() const { return QFileInfo(filePath).completeBaseName(); }
};

// ============================================================================
// ImageSequence -- sequence container
// ============================================================================

/**
 * @brief Container and manager for a sequence of images to be stacked.
 *
 * Handles loading from individual files or a directory, validates
 * dimensional consistency, provides selection / quality-based filtering,
 * and delegates I/O for individual frames to the appropriate file-format
 * readers.
 */
class ImageSequence {
public:
    /**
     * @brief Discriminates the on-disk organisation of the sequence.
     */
    enum class Type {
        Regular,    ///< Individual FITS / TIFF files in a directory.
        FitSeq,     ///< Single FITS-sequence container file.
        SER         ///< SER video capture file.
    };

    ImageSequence()  = default;
    ~ImageSequence() = default;

    /* Non-copyable but movable. */
    ImageSequence(const ImageSequence&)            = delete;
    ImageSequence& operator=(const ImageSequence&) = delete;
    ImageSequence(ImageSequence&&)                 = default;
    ImageSequence& operator=(ImageSequence&&)      = default;

    // ========================================================================
    // Loading and initialisation
    // ========================================================================

    /**
     * @brief Load a sequence from an explicit list of file paths.
     * @param files            List of absolute paths to image files.
     * @param progressCallback Optional callback for progress reporting.
     * @return true if at least one valid image was loaded.
     */
    bool loadFromFiles(const QStringList& files,
                       ProgressCallback progressCallback = nullptr);

    /**
     * @brief Scan a directory and load matching files as a sequence.
     * @param directory        Path to the directory to scan.
     * @param nameFilters      Glob patterns (e.g. {"*.fit", "*.fits"}).
     * @param progressCallback Optional callback for progress reporting.
     * @return true if at least one valid image was loaded.
     */
    bool loadFromDirectory(const QString& directory,
                           const QStringList& nameFilters,
                           ProgressCallback progressCallback = nullptr);

    /** @brief Discard all loaded data and reset the sequence to its initial state. */
    void clear();

    /**
     * @brief Remove a single image from the sequence by index.
     * @param index Zero-based position of the image to remove.
     */
    void removeImage(int index);

    /** @brief Test whether the sequence contains at least one valid image. */
    bool isValid() const { return !m_images.empty(); }

    // ========================================================================
    // Image access
    // ========================================================================

    /** @brief Total number of images in the sequence. */
    int count() const { return static_cast<int>(m_images.size()); }

    /** @brief Number of currently selected (checked) images. */
    int selectedCount() const;

    /** @brief Const access to the image descriptor at @p index. */
    const SequenceImage& image(int index) const { return m_images.at(index); }

    /** @brief Mutable access to the image descriptor at @p index. */
    SequenceImage& image(int index) { return m_images.at(index); }

    /** @brief Const access to the full image vector. */
    const std::vector<SequenceImage>& images() const { return m_images; }

    /** @brief Mutable access to the full image vector. */
    std::vector<SequenceImage>& images() { return m_images; }

    /**
     * @brief Read a complete image from disk into @p buffer.
     * @param index  Zero-based image index.
     * @param buffer Destination buffer.
     * @return true on success.
     */
    bool readImage(int index, ImageBuffer& buffer) const;

    /**
     * @brief Read a rectangular sub-region of an image from disk.
     * @param index   Zero-based image index.
     * @param buffer  Destination buffer.
     * @param x       Left column of the region.
     * @param y       Top row of the region.
     * @param width   Width of the region in pixels.
     * @param height  Height of the region in pixels.
     * @param channel Channel to read (-1 for all channels).
     * @return true on success.
     */
    bool readImageRegion(int index, ImageBuffer& buffer,
                         int x, int y, int width, int height,
                         int channel = -1) const;

    // ========================================================================
    // Selection and filtering
    // ========================================================================

    /** @brief Set the selection state of a single image. */
    void setSelected(int index, bool selected);

    /** @brief Select every image in the sequence. */
    void selectAll();

    /** @brief Deselect every image in the sequence. */
    void deselectAll();

    /** @brief Invert the selection state of a single image. */
    void toggleSelection(int index);

    /**
     * @brief Apply a quality-based filter to the selection state.
     * @param filter    Quality criterion to filter on.
     * @param mode      Whether @p parameter is a percentage or a k-sigma value.
     * @param parameter Numeric threshold (percentage 0-100 or sigma multiplier).
     * @return Number of images that passed the filter.
     */
    int applyFilter(ImageFilter filter, FilterMode mode, double parameter);

    /**
     * @brief Collect the indices of all currently selected images.
     * @return Sorted vector of zero-based indices.
     */
    std::vector<int> getFilteredIndices() const;

    // ========================================================================
    // Reference image
    // ========================================================================

    /** @brief Designate a new reference image by index. */
    void setReferenceImage(int index) { m_referenceImage = index; }

    /** @brief Index of the current reference image. */
    int referenceImage() const { return m_referenceImage; }

    /**
     * @brief Automatically select the best reference image based on quality.
     * @return Index of the highest-quality selected image, or 0 if no metrics.
     */
    int findBestReference() const;

    // ========================================================================
    // Sequence properties
    // ========================================================================

    /** @brief Directory from which the sequence was loaded. */
    QString directory() const { return m_directory; }

    /** @brief On-disk organisation type of the sequence. */
    Type type() const { return m_type; }

    /** @brief Common image width, or 0 if dimensions vary across frames. */
    int width()    const { return m_width;    }

    /** @brief Common image height, or 0 if dimensions vary across frames. */
    int height()   const { return m_height;   }

    /** @brief Number of channels (1 = mono, 3 = RGB). */
    int channels() const { return m_channels; }

    /** @brief True if frame dimensions are not uniform across the sequence. */
    bool isVariable() const { return m_isVariable; }

    /** @brief True if at least one frame carries registration data. */
    bool hasRegistration() const;

    /** @brief True if per-frame quality metrics have been computed. */
    bool hasQualityMetrics() const { return m_hasQualityMetrics; }

    /** @brief Sum of exposure times for all selected frames (seconds). */
    double totalExposure() const;

    // ========================================================================
    // Registration
    // ========================================================================

    /**
     * @brief Load registration transforms from an external file.
     * @param regFile Path to the registration data file.
     * @return true on success (currently unimplemented -- returns false).
     */
    bool loadRegistration(const QString& regFile);

    /** @brief Clear all per-frame registration data. */
    void clearRegistration();

    /** @brief True if every registered frame uses a pure translation (no rotation/scale). */
    bool isShiftOnlyRegistration() const;

    // ========================================================================
    // Quality metrics
    // ========================================================================

    /**
     * @brief Compute quality metrics (FWHM, roundness, noise, etc.) for every frame.
     * @param progressCallback Optional callback for progress reporting.
     * @return true on success.
     */
    bool computeQualityMetrics(ProgressCallback progressCallback = nullptr);

    /**
     * @brief Compute the acceptance threshold for a given quality filter.
     * @param filter    Quality criterion.
     * @param mode      Percentage or k-sigma interpretation.
     * @param parameter Numeric threshold value.
     * @return Computed threshold value.
     */
    double computeFilterThreshold(ImageFilter filter, FilterMode mode,
                                  double parameter) const;

    // ========================================================================
    // Comet registration
    // ========================================================================

    /**
     * @brief Compute differential shifts to freeze a comet in the stacked result.
     *
     * Given comet positions marked in a reference frame and a target frame,
     * the method derives the comet's apparent velocity and adjusts every
     * frame's registration shift so that the comet remains stationary
     * while stars trail.
     *
     * @param refIndex    Index of the reference frame (first comet position).
     * @param targetIndex Index of the target frame (second comet position).
     * @return true on success.
     */
    bool computeCometShifts(int refIndex, int targetIndex);

private:
    /** @brief Validate dimensional consistency after loading and set sequence-wide properties. */
    bool validateSequence();

    /** @brief Read and cache metadata (dimensions, exposure, etc.) from a single file. */
    bool readImageMetadata(SequenceImage& img);

    // -- Data members --------------------------------------------------------
    std::vector<SequenceImage> m_images;
    QString m_directory;
    Type    m_type = Type::Regular;

    int  m_width              = 0;
    int  m_height             = 0;
    int  m_channels           = 0;
    bool m_isVariable         = false;
    bool m_hasRegistration    = false;
    bool m_hasQualityMetrics  = false;

    int  m_referenceImage     = 0;
};

} // namespace Stacking

#endif // STACKING_SEQUENCE_H