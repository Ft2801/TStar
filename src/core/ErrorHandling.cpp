#include "ErrorHandling.h"
#include <QFile>
#include <QMessageBox>
#include "ImageBuffer.h"
#include <QCoreApplication>

// ============================================================================
// Error Reporting to User
// ============================================================================

void reportUserError(const QString& title, const QString& message) {
    qWarning() << "[ERROR]" << title << "-" << message;
    // Note: In GUI context, could emit signal to MainWindow for dialog
    // For now, just log - dialogs should handle their own error reporting
}

void reportWarning(const QString& title, const QString& message) {
    qWarning() << "[WARNING]" << title << "-" << message;
}

void reportInfo(const QString& title, const QString& message) {
    qInfo() << "[INFO]" << title << "-" << message;
}

// ============================================================================
// Validation Helpers
// ============================================================================

bool validateFileExists(const QString& path, QString* error) {
    if (path.isEmpty()) {
        if (error) *error = "File path cannot be empty";
        return false;
    }
    
    QFile file(path);
    if (!file.exists()) {
        if (error) *error = formatError("File not found", path, "");
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = formatError("Cannot open file", path, file.errorString());
        return false;
    }
    file.close();
    
    return true;
}

bool validateBuffer(const ImageBuffer& buffer, QString* error) {
    if (!buffer.isValid()) {
        if (error) *error = "ImageBuffer is invalid or empty";
        return false;
    }
    
    if (buffer.width() <= 0 || buffer.height() <= 0 || buffer.channels() <= 0) {
        if (error) *error = QString("Invalid buffer dimensions: %1x%2 channels=%3")
            .arg(buffer.width())
            .arg(buffer.height())
            .arg(buffer.channels());
        return false;
    }
    
    if (buffer.data().empty()) {
        if (error) *error = "ImageBuffer has no pixel data";
        return false;
    }
    
    return true;
}

bool validateFileFormat(const QString& path, const QString& expectedMagic, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = formatError("Cannot open file", path, file.errorString());
        return false;
    }
    
    // Read magic bytes
    QByteArray magic = file.read(expectedMagic.length());
    file.close();
    
    if (magic != expectedMagic.toUtf8()) {
        if (error) *error = formatError("File format mismatch", path,
            QString("Expected %1, got %2").arg(expectedMagic, QString(magic)));
        return false;
    }
    
    return true;
}
