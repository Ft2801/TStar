/**
 * @file StackingSequence.h
 * @brief Image sequence management for stacking operations
 * 
 * This file contains the ImageSequence class and related structures
 * for managing sequences of images to be stacked.
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef STACKING_SEQUENCE_H
#define STACKING_SEQUENCE_H

#include "StackingTypes.h"
#include "../ImageBuffer.h"
#include <QString>
#include <QStringList>
#include <vector>
#include <memory>
#include <QFileInfo>

namespace Stacking {

/**
 * @brief Represents a single image in a sequence
 */
struct SequenceImage {
    QString filePath;                ///< Full path to image file
    bool selected = true;            ///< Whether included in stacking
    int index = 0;                   ///< Original index in sequence
    
    // Dimensions (cached from file)
    int width = 0;
    int height = 0;
    int channels = 0;
    int bitDepth = 16;               ///< 8, 16, or 32
    bool isFloat = false;
    
    // Registration data
    RegistrationData registration;
    
    // Quality metrics
    ImageQuality quality;
    
    // Metadata (cached)
    ImageBuffer::Metadata metadata;
    
    // Stacking-specific data
    double exposure = 0.0;           ///< Exposure time in seconds
    int stackCount = 1;              ///< Prior stack count (for stacked masters)
    
    /**
     * @brief Get filename without path
     */
    QString fileName() const {
        return QFileInfo(filePath).fileName();
    }
    
    /**
     * @brief Get filename without extension
     */
    QString baseName() const {
        return QFileInfo(filePath).completeBaseName();
    }
};

/**
 * @brief Manages a sequence of images for stacking
 * 
 * The ImageSequence class handles loading, filtering, and accessing
 * images in a sequence. It supports various sequence types (FITS folder,
 * SER, etc.) and caches metadata for efficient access.
 */
class ImageSequence {
public:
    /**
     * @brief Sequence type enumeration
     */
    enum class Type {
        Regular,    ///< Individual FITS/TIFF files
        FitSeq,     ///< FITS sequence file
        SER         ///< SER video file
    };
    
    ImageSequence() = default;
    ~ImageSequence() = default;
    
    // Non-copyable but movable
    ImageSequence(const ImageSequence&) = delete;
    ImageSequence& operator=(const ImageSequence&) = delete;
    ImageSequence(ImageSequence&&) = default;
    ImageSequence& operator=(ImageSequence&&) = default;
    
    //=========================================================================
    // LOADING AND INITIALIZATION
    //=========================================================================
    
    /**
     * @brief Load sequence from a list of files
     * @param files List of image file paths
     * @param progressCallback Optional progress callback
     * @return true if successful
     */
    bool loadFromFiles(const QStringList& files, 
                       ProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Load sequence from a directory
     * @param directory Path to directory containing images
     * @param pattern File pattern (e.g., "*.fit")
     * @param progressCallback Optional progress callback
     * @return true if successful
     */
    bool loadFromDirectory(const QString& directory,
                          const QString& pattern = "*.fit",
                          ProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Clear all sequence data
     */
    void clear();
    
    /**
     * @brief Remove image at given index
     * @param index Index of image to remove
     */
    void removeImage(int index);
    
    /**
     * @brief Check if sequence is loaded and valid
     */
    bool isValid() const { return !m_images.empty(); }
    
    //=========================================================================
    // IMAGE ACCESS
    //=========================================================================
    
    /**
     * @brief Get number of images in sequence
     */
    int count() const { return static_cast<int>(m_images.size()); }
    
    /**
     * @brief Get number of selected images
     */
    int selectedCount() const;
    
    /**
     * @brief Get image info by index
     */
    const SequenceImage& image(int index) const { return m_images.at(index); }
    SequenceImage& image(int index) { return m_images.at(index); }
    
    /**
     * @brief Get all images
     */
    const std::vector<SequenceImage>& images() const { return m_images; }
    std::vector<SequenceImage>& images() { return m_images; }
    
    /**
     * @brief Read an image from the sequence
     * @param index Image index
     * @param buffer Output buffer
     * @return true if successful
     */
    bool readImage(int index, ImageBuffer& buffer) const;
    
    /**
     * @brief Read a region from an image
     * @param index Image index
     * @param buffer Output buffer
     * @param x, y, width, height Region to read
     * @param channel Channel to read (-1 for all)
     * @return true if successful
     */
    bool readImageRegion(int index, ImageBuffer& buffer,
                        int x, int y, int width, int height,
                        int channel = -1) const;
    
    //=========================================================================
    // SELECTION AND FILTERING
    //=========================================================================
    
    /**
     * @brief Select/deselect an image
     */
    void setSelected(int index, bool selected);
    
    /**
     * @brief Select all images
     */
    void selectAll();
    
    /**
     * @brief Deselect all images
     */
    void deselectAll();
    
    /**
     * @brief Toggle selection state
     */
    void toggleSelection(int index);
    
    /**
     * @brief Apply filter to select images based on criteria
     * @param filter Filter type
     * @param mode Percentage or k-sigma
     * @param parameter Filter parameter value
     * @return Number of images passing filter
     */
    int applyFilter(ImageFilter filter, FilterMode mode, double parameter);
    
    /**
     * @brief Get indices of filtered (selected) images
     */
    std::vector<int> getFilteredIndices() const;
    
    //=========================================================================
    // REFERENCE IMAGE
    //=========================================================================
    
    /**
     * @brief Set reference image index
     */
    void setReferenceImage(int index) { m_referenceImage = index; }
    
    /**
     * @brief Get reference image index
     */
    int referenceImage() const { return m_referenceImage; }
    
    /**
     * @brief Find best reference image based on quality
     * @return Index of best image, or 0 if no quality data
     */
    int findBestReference() const;
    
    //=========================================================================
    // SEQUENCE PROPERTIES
    //=========================================================================
    
    /**
     * @brief Get sequence directory
     */
    QString directory() const { return m_directory; }
    
    /**
     * @brief Get sequence type
     */
    Type type() const { return m_type; }
    
    /**
     * @brief Get common image width (0 if variable)
     */
    int width() const { return m_width; }
    
    /**
     * @brief Get common image height (0 if variable)
     */
    int height() const { return m_height; }
    
    /**
     * @brief Get number of channels
     */
    int channels() const { return m_channels; }
    
    /**
     * @brief Check if sequence has variable dimensions
     */
    bool isVariable() const { return m_isVariable; }
    
    /**
     * @brief Check if any image has registration data
     */
    bool hasRegistration() const { return m_hasRegistration; }
    
    /**
     * @brief Check if quality metrics are available
     */
    bool hasQualityMetrics() const { return m_hasQualityMetrics; }
    
    /**
     * @brief Get total exposure time of selected images
     */
    double totalExposure() const;
    
    //=========================================================================
    // REGISTRATION
    //=========================================================================
    
    /**
     * @brief Load registration data from file
     * @param regFile Path to registration file
     * @return true if successful
     */
    bool loadRegistration(const QString& regFile);
    
    /**
     * @brief Clear registration data
     */
    void clearRegistration();
    
    /**
     * @brief Check if registration is shift-only
     */
    bool isShiftOnlyRegistration() const;
    
    //=========================================================================
    // QUALITY METRICS
    //=========================================================================
    
    /**
     * @brief Compute quality metrics for all images
     * @param progressCallback Progress callback
     * @return true if successful
     */
    bool computeQualityMetrics(ProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Compute filter threshold for given criteria
     */
    double computeFilterThreshold(ImageFilter filter, FilterMode mode, 
                                  double parameter) const;
                                  
    /**
     * @brief Compute registration shifts for comet alignment
     * 
     * Uses comet positions in reference and target images to interpolate 
     * shifts for all images, combining with star registration.
     * 
     * @param refIndex Index of reference image (first comet position)
     * @param targetIndex Index of target image (second comet position)
     * @return true if successful
     */
    bool computeCometShifts(int refIndex, int targetIndex);
    
private:
    /**
     * @brief Validate and initialize sequence after loading
     */
    bool validateSequence();
    
    /**
     * @brief Read metadata from a single image file
     */
    bool readImageMetadata(SequenceImage& img);
    
    std::vector<SequenceImage> m_images;
    QString m_directory;
    Type m_type = Type::Regular;
    
    int m_width = 0;
    int m_height = 0;
    int m_channels = 0;
    bool m_isVariable = false;
    bool m_hasRegistration = false;
    bool m_hasQualityMetrics = false;
    
    int m_referenceImage = 0;
};

} // namespace Stacking

#endif // STACKING_SEQUENCE_H
