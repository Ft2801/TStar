#pragma once

#include <QString>

class ImageBuffer;

/**
 * @brief Loads camera RAW files (CR2, NEF, ARW, DNG, ORF, RAF, RW2, PEF, etc.)
 *        into an ImageBuffer as a single-channel CFA (Bayer pattern) float image.
 *
 *  The raw sensor data is black-level corrected and normalized to [0,1].
 *  Bayer pattern metadata is stored in ImageBuffer::Metadata so the image can
 *  later be debayered by DebayerDialog or the preprocessing pipeline.
 *
 *  Requires HAVE_LIBRAW to be defined (set automatically by CMakeLists.txt
 *  when LibRaw is found).
 */
namespace RawLoader {

/**
 * @brief Load a RAW file into an ImageBuffer.
 * @param filePath  Absolute path to the RAW file.
 * @param buf       Output ImageBuffer (will be overwritten).
 * @param errorMsg  Optional pointer to receive an error description on failure.
 * @return true on success, false on failure.
 */
bool load(const QString& filePath, ImageBuffer& buf, QString* errorMsg = nullptr);

/**
 * @brief Returns true if the given file extension is a supported RAW format.
 */
bool isSupportedExtension(const QString& ext);

/**
 * @brief Returns the Qt file-dialog filter string for all supported RAW formats.
 *        e.g. "RAW Files (*.cr2 *.cr3 *.nef ...)"
 */
QString filterString();

} // namespace RawLoader
