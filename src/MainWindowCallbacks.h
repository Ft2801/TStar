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
    virtual void createResultWindow(const ImageBuffer& buffer, const QString& title) = 0;
    
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
};

#endif // MAINWINDOW_CALLBACKS_H
