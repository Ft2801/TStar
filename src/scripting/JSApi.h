// =============================================================================
// JSApi.h
//
// Core data wrappers for the TStar JavaScript scripting API.
//
// JSImage  - Wraps ImageBuffer for pixel-level access from JS.
// JSView   - Wraps an active image window (ImageViewer + buffer).
//
// These classes are exposed to QJSEngine via the factory/App objects
// and provide the data layer that Process objects operate on.
// =============================================================================

#ifndef JSAPI_H
#define JSAPI_H

#include <QObject>
#include <QVariant>
#include <QPointer>
#include <QJSValue>
#include <QJSEngine>
#include "ImageBuffer.h"

class ImageViewer;
class MainWindow;

namespace Scripting {

// =============================================================================
// JSImage
//
// Wraps an ImageBuffer for manipulation in JavaScript.
// Can own its own buffer (standalone) or reference an external one (from a view).
// =============================================================================

class JSImage : public QObject {
    Q_OBJECT
    Q_PROPERTY(int width    READ width    CONSTANT)
    Q_PROPERTY(int height   READ height   CONSTANT)
    Q_PROPERTY(int channels READ channels CONSTANT)
    Q_PROPERTY(bool isValid READ isValid  CONSTANT)

public:
    /** @brief Construct an empty (invalid) image. */
    explicit JSImage(QObject* parent = nullptr);

    /** @brief Construct from a copy of the given buffer. */
    explicit JSImage(const ImageBuffer& buffer, QObject* parent = nullptr);

    /**
     * @brief Construct referencing an external buffer (e.g. from an ImageViewer).
     *
     * The JSImage does NOT own the buffer. The caller must ensure the
     * buffer outlives this object. This mode is used when the script
     * accesses the active image — modifications go directly to the viewer.
     */
    JSImage(ImageBuffer* externalBuffer, QObject* parent = nullptr);

    /** @brief Construct referencing a viewer's buffer and the viewer itself for history. */
    JSImage(ImageBuffer* externalBuffer, ImageViewer* viewer, QObject* parent = nullptr);

    ~JSImage() override = default;

    // -- Internal API for processes -------------------------------------------

    Q_INVOKABLE void pushUndo(const QString& name);
    Q_INVOKABLE void refresh();

    /** @brief Set the entire image buffer. */
    Q_INVOKABLE void setBuffer(const ImageBuffer& buffer);


    // -- Property accessors ---------------------------------------------------

    int  width()    const;
    int  height()   const;
    int  channels() const;
    bool isValid()  const;

    // -- Pixel access ---------------------------------------------------------

    /** @brief Get a pixel value at (x, y, channel). */
    Q_INVOKABLE double getPixel(int x, int y, int channel = 0) const;

    /** @brief Set a pixel value at (x, y, channel). */
    Q_INVOKABLE void setPixel(int x, int y, int channel, double value);

    // -- File I/O -------------------------------------------------------------

    /** @brief Load an image from disk (FITS, TIFF, XISF). */
    Q_INVOKABLE bool load(const QString& filePath);

    /**
     * @brief Save the image to disk.
     * @param filePath  Output file path.
     * @param format    "fits", "tiff", "xisf", or "png" (default: "fits").
     * @param bitDepth  0=8bit, 1=16bit, 2=32int, 3=32float (default: 3).
     */
    Q_INVOKABLE bool save(const QString& filePath,
                          const QString& format = "fits",
                          int bitDepth = 3) const;

    // -- Image operations -----------------------------------------------------

    /** @brief Create a deep copy of this image. */
    Q_INVOKABLE QObject* clone() const;

    // -- Statistics -----------------------------------------------------------

    /** @brief Compute per-channel median values. Returns array of floats. */
    Q_INVOKABLE QVariantList medians() const;

    /**
     * @brief Compute per-channel histogram.
     * @param bins Number of bins (default 256).
     * @return Array of arrays (one per channel).
     */
    Q_INVOKABLE QVariantList computeHistogram(int bins = 256) const;

    /**
     * @brief Get a summary of image statistics.
     * @return Object with min, max, mean, median per channel.
     */
    Q_INVOKABLE QVariantMap getStatistics() const;

    // -- Metadata access ------------------------------------------------------

    /** @brief Get a FITS header value by keyword. */
    Q_INVOKABLE QString getHeaderValue(const QString& key) const;

    /** @brief Get the object name from metadata. */
    Q_INVOKABLE QString objectName() const;

    /** @brief Get the source file path. */
    Q_INVOKABLE QString filePath() const;

    // -- Direct C++ access (not exposed to JS) --------------------------------

    ImageBuffer& buffer();
    const ImageBuffer& buffer() const;

private:
    ImageBuffer  m_ownedBuffer;     ///< Buffer owned by this object (standalone mode).
    ImageBuffer* m_externalBuffer;  ///< Non-owning pointer (view reference mode).
    QPointer<ImageViewer> m_viewer; ///< Optional reference to viewer for undo/refresh.
    bool         m_ownsBuffer;      ///< True if we own the buffer.
};

// =============================================================================
// JSView
//
// Wraps an active image window in TStar.
// Provides access to the image data and window properties.
// =============================================================================

class JSView : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title    READ title    CONSTANT)
    Q_PROPERTY(QObject* image   READ image    CONSTANT)
    Q_PROPERTY(QString filePath READ filePath CONSTANT)

public:
    JSView(ImageViewer* viewer, MainWindow* mainWindow, QObject* parent = nullptr);
    ~JSView() override = default;

    QString  title()    const;
    QObject* image()    const;
    QString  filePath() const;

    /** @brief Refresh the viewer display after script modifications. */
    Q_INVOKABLE void refresh();

    /** @brief Add an entry to the image history (Undo stack). */
    Q_INVOKABLE void pushUndo(const QString& name = "Script Operation");

    /**
     * @brief Set a real-time preview LUT on the viewer.
     * @param luts A 2D array of floats [channel][65536] mapping input to output.
     */
    Q_INVOKABLE void setPreviewLUT(const QVariantList& luts);

    /** @brief Clear the preview LUT overlay. */
    Q_INVOKABLE void clearPreviewLUT();

    /** @brief Get the underlying ImageViewer (C++ only). */
    ImageViewer* viewer() const;

private:
    QPointer<ImageViewer> m_viewer;
    QPointer<MainWindow>  m_mainWindow;
    JSImage*              m_image = nullptr;  ///< Lazily created on first access.
};

} // namespace Scripting

#endif // JSAPI_H