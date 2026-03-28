// =============================================================================
// MainWindow Callback Interface
// =============================================================================
// This header breaks circular dependencies by using an interface
// instead of including MainWindow.h in every dialog
// =============================================================================

#ifndef MAINWINDOW_CALLBACKS_H
#define MAINWINDOW_CALLBACKS_H

#include <QString>
#include "ImageBuffer.h"

class MainWindowCallbacks {
public:
    virtual ~MainWindowCallbacks() = default;
    
    // Get current ImageBuffer safely
    // Returns nullptr if no image is loaded
    virtual ImageBuffer* getCurrentImageBuffer() = 0;
    
    // Get current ImageViewer safely
    // Returns nullptr if no viewer is active
    virtual class ImageViewer* getCurrentViewer() = 0;
    
    // Create new image window with result
    virtual void createResultWindow(const ImageBuffer& buffer, const QString& title, int mode = -1, float median = 0.25f, bool linked = true) = 0;
    
    // Log message to main window
    virtual void logMessage(const QString& message, int severity, bool showPopup = false) = 0;
    
    // Update display/refresh main window
    virtual void updateDisplay() = 0;
    
    // Start long operation (show progress bar, disable UI)
    virtual void startLongProcess() = 0;
    
    // End long operation (hide progress bar, enable UI)
    virtual void endLongProcess() = 0;
    
    // Check if the viewer is currently actively retained by a tool dialog
    virtual bool isViewerInUse(class ImageViewer* vi, QString* toolName = nullptr) const { 
        (void)vi; (void)toolName; 
        return false; 
    }

    // Refresh the FITS header panel to reflect the current active viewer.
    // Call this after batch operations that modify non-active windows.
    virtual void refreshHeaderPanel() {}

    // ── Utility helpers ───────────────────────────────────────────────────────

    /// Build a child window title by appending @p suffix to @p parentTitle,
    /// stripping any trailing '*' and known image-file extensions first.
    /// Dialogs should call this BEFORE starting background work (capturing the
    /// viewer title while it is still valid) rather than relying on the current
    /// viewer at the time the result window is created.
    static QString buildChildTitle(const QString& parentTitle, const QString& suffix) {
        QString t = parentTitle;
        if (t.endsWith(QLatin1Char('*'))) t.chop(1);
        // Strip known image-file extensions so they don't end up in the title.
        static const char* const kExts[] = {
            "fits","fit","tif","tiff","png","jpg","jpeg","xisf","bmp", nullptr
        };
        const int dot = t.lastIndexOf(QLatin1Char('.'));
        if (dot >= 0) {
            const QString ext = t.mid(dot + 1).toLower();
            for (int i = 0; kExts[i]; ++i) {
                if (ext == QLatin1String(kExts[i])) {
                    t = t.left(dot);
                    break;
                }
            }
        }
        return t.trimmed() + suffix;
    }
};

#endif // MAINWINDOW_CALLBACKS_H
