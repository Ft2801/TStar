#ifndef DIALOGBASE_H
#define DIALOGBASE_H

#include <QDialog>
#include <QIcon>
#include <QString>

/**
 * @brief Base class for all TStar dialogs with common functionality
 * 
 * Consolidates:
 * - Window title/icon setup (eliminates 30+ copy-paste setups)
 * - Standard size initialization
 * - Delete on close attribute
 * - Help/documentation integration
 * 
 * Usage:
 * @code
 * class MyDialog : public DialogBase {
 * public:
 *     MyDialog(QWidget* parent = nullptr)
 *         : DialogBase(parent, "My Dialog Title", 400, 300) {}
 * };
 * @endcode
 * 
 * This consolidates common pattern that appears in 40+ dialog files:
 * - setWindowTitle(tr("..."))
 * - setWindowIcon(QIcon(":/images/Logo.png"))
 * - resize(width, height)
 * - setAttribute(Qt::WA_DeleteOnClose)
 */
class DialogBase : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Construct a standard TStar dialog
     * @param parent Parent widget
     * @param title Window title (will be translated)
     * @param defaultWidth Default window width (use 0 for auto)
     * @param defaultHeight Default window height (use 0 for auto)
     * @param deleteOnClose Delete dialog when closed (default: true for modeless dialogs)
     * @param showIcon Show TStar logo icon (default: true)
     */
    explicit DialogBase(QWidget* parent = nullptr,
                       const QString& title = QString(),
                       int defaultWidth = 0,
                       int defaultHeight = 0,
                       bool deleteOnClose = false,
                       bool showIcon = true);
    
    virtual ~DialogBase() = default;
    
    /**
     * @brief Set window properties with sensible defaults
     * @param title Window title
     * @param width Window width (0 = don't resize)
     * @param height Window height (0 = don't resize)
     */
    void setWindowProperties(const QString& title, int width = 0, int height = 0);
    
    /**
     * @brief Enable/disable standard delete-on-close behavior
     * Useful for tool windows that should persist in memory
     */
    void setDeleteOnClose(bool enabled);
    
    /**
     * @brief Get standard icon for all dialogs
     * @return Logo icon from resources
     */
    static QIcon getStandardIcon();
    
    /**
     * @brief Find and return the MainWindowCallbacks interface by traversing parents
     * @return Pointer to the interface, or nullptr if not found
     */
    class MainWindowCallbacks* getCallbacks();
    
protected:
    /**
     * @brief Called after window setup to allow subclasses to configure
     * Override this instead of manual setWindowTitle/resize
     */
    virtual void setupDialogUI() {}
    
    /**
     * @brief Restore previous window geometry from settings
     * @param settingsKey Key to use in QSettings (default: class name)
     */
    void restoreWindowGeometry(const QString& settingsKey = QString());
    
    /**
     * @brief Save window geometry to settings for next session
     * @param settingsKey Key to use in QSettings (default: class name)
     */
    void saveWindowGeometry(const QString& settingsKey = QString());

private:
    void initialize(const QString& title, int width, int height, bool deleteOnClose, bool showIcon);
};

#endif // DIALOGBASE_H
