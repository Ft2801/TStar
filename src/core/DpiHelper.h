#ifndef DPIHELPER_H
#define DPIHELPER_H

#include <QWidget>
#include <QScreen>
#include <QApplication>
#include <QGuiApplication>

class DpiHelper {
public:
    static qreal dpr(QWidget* widget = nullptr) {
        if (widget && widget->screen())
            return widget->screen()->devicePixelRatio();
        if (QGuiApplication::primaryScreen())
            return QGuiApplication::primaryScreen()->devicePixelRatio();
        return 1.0;
    }
    
    static int scale(int px, QWidget* widget = nullptr) {
        return qRound(px * dpr(widget));
    }
    
    static qreal scaleF(qreal px, QWidget* widget = nullptr) {
        return px * dpr(widget);
    }
    
    // ========== Standard UI Element Sizes ==========
    
    /// Standard sidebar width (24px at 100%)
    static int sidebarWidth(QWidget* w = nullptr) { return scale(24, w); }
    
    /// Standard title bar height (28px at 100%)
    static int titleBarHeight(QWidget* w = nullptr) { return scale(28, w); }
    
    /// Standard button size (22px at 100%)
    static int buttonSize(QWidget* w = nullptr) { return scale(22, w); }
    
    /// Icon size within buttons (14px at 100%)
    static int iconSize(QWidget* w = nullptr) { return scale(14, w); }
    
    /// Resize margin/edge detection area (8px at 100%)
    static int resizeMargin(QWidget* w = nullptr) { return scale(8, w); }
    
    /// Border width (2px at 100%)
    static int borderWidth(QWidget* w = nullptr) { return scale(2, w); }
    
    // ========== Minimum Window Dimensions ==========
    
    /// Minimum window width (150px at 100%)
    static int minWindowWidth(QWidget* w = nullptr) { return scale(150, w); }
    
    /// Minimum window height (80px at 100%)
    static int minWindowHeight(QWidget* w = nullptr) { return scale(80, w); }
    
    /// Minimum shaded window width (200px at 100%)
    static int minShadedWidth(QWidget* w = nullptr) { return scale(200, w); }
    
    // ========== Icon Rendering ==========
    
    /// Standard icon pixmap size (64px at 100%)
    static int iconPixmapSize(QWidget* w = nullptr) { return scale(64, w); }
    
    /// Drag indicator pixmap size (20px at 100%)
    static int dragPixmapSize(QWidget* w = nullptr) { return scale(20, w); }
};

#endif // DPIHELPER_H
