#include "DialogBase.h"
#include "MainWindowCallbacks.h"
#include <QIcon>
#include <QSettings>
#include <QRect>
#include <QApplication>
#include <QScreen>

DialogBase::DialogBase(QWidget* parent,
                       const QString& title,
                       int defaultWidth,
                       int defaultHeight,
                       bool deleteOnClose,
                       bool showIcon)
    : QDialog(parent) {
    initialize(title, defaultWidth, defaultHeight, deleteOnClose, showIcon);
}

void DialogBase::initialize(const QString& title,
                           int width,
                           int height,
                           bool deleteOnClose,
                           bool showIcon) {
    // Set up standard properties
    if (showIcon) {
        setWindowIcon(getStandardIcon());
    }
    
    // Center on parent or screen
    if (parent() && parent()->isWidgetType()) {
        move(static_cast<QWidget*>(parent())->frameGeometry().topLeft() + QPoint(40, 40));
    } else {
        QRect screenGeometry = QApplication::primaryScreen()->geometry();
        int x = (screenGeometry.width() / 2) - (width / 2);
        int y = (screenGeometry.height() / 2) - (height / 2);
        move(x, y);
    }
    
    // Set delete on close for transient dialogs
    if (deleteOnClose) {
        setAttribute(Qt::WA_DeleteOnClose);
    }
    
    // Set initial properties
    setWindowProperties(title, width, height);
    
    // Allow subclass to configure UI
    setupDialogUI();
    
    // Try to restore saved geometry
    QString settingsKey = parent() ? parent()->objectName() : metaObject()->className();
    if (!settingsKey.isEmpty()) {
        restoreWindowGeometry(settingsKey);
    }
}

void DialogBase::setWindowProperties(const QString& title, int width, int height) {
    if (!title.isEmpty()) {
        setWindowTitle(title);
    }
    
    if (width > 0 && height > 0) {
        resize(width, height);
    }
}

void DialogBase::setDeleteOnClose(bool enabled) {
    setAttribute(Qt::WA_DeleteOnClose, enabled);
}

QIcon DialogBase::getStandardIcon() {
    static QIcon icon;
    if (icon.isNull()) {
        icon = QIcon(":/images/Logo.png");
    }
    return icon;
}

void DialogBase::restoreWindowGeometry(const QString& settingsKey) {
    if (settingsKey.isEmpty()) {
        return;
    }
    
    QSettings settings("TStar", "TStar");
    QString geometryKey = settingsKey + "/geometry";
    
    if (settings.contains(geometryKey)) {
        restoreGeometry(settings.value(geometryKey).toByteArray());
    }
}

void DialogBase::saveWindowGeometry(const QString& settingsKey) {
    if (settingsKey.isEmpty()) {
        return;
    }
    
    QSettings settings("TStar", "TStar");
    QString geometryKey = settingsKey + "/geometry";
    settings.setValue(geometryKey, saveGeometry());
}

MainWindowCallbacks* DialogBase::getCallbacks() {
    QWidget* w = this;
    while (w) {
        if (MainWindowCallbacks* mw = dynamic_cast<MainWindowCallbacks*>(w)) return mw;
        w = w->parentWidget();
    }
    return nullptr;
}
