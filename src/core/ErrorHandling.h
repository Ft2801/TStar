#ifndef ERRORHANDLING_H
#define ERRORHANDLING_H

#include <QString>
#include <functional>
#include <optional>
#include <stdexcept>

/**
 * @brief Consistent error handling utilities for TStar
 * 
 * TStar uses error codes (Qt-style) rather than exceptions for most operations.
 * This header provides utilities to standardize error handling across I/O,
 * processing, and networking operations.
 * 
 * Standard patterns:
 * 1. Bool return + optional QString* error output
 *    bool load(const QString& path, ImageBuffer& buf, QString* err = nullptr)
 * 
 * 2. Q_ASSERT guards for preconditions
 *    Q_ASSERT(!path.isEmpty());
 *    Q_ASSERT(buffer.isValid());
 * 
 * 3. RAII cleanup for resource management
 * 
 * 4. Signal/slot for async operations with error reporting
 */

// ============================================================================
// Error Message Formatting
// ============================================================================

/**
 * @brief Format error message with context
 * 
 * Usage:
 *   QString errMsg = formatError("Failed to load file", filePath, status);
 *   // Output: "Failed to load file: /path/to/file.fits (error 123)"
 */
inline QString formatError(const QString& operation, const QString& context, int errorCode) {
    return QString("%1: %2 (error %3)").arg(operation, context, QString::number(errorCode));
}

inline QString formatError(const QString& operation, const QString& context, const QString& reason) {
    return QString("%1: %2 - %3").arg(operation, context, reason);
}

/**
 * @brief Assert with error message
 * 
 * Usage:
 *   Q_ASSERT_X(!path.isEmpty(), "loadFile", "File path cannot be empty");
 */
#define TSTAR_ASSERT(cond, msg) Q_ASSERT_X(cond, __FUNCTION__, msg)

// ============================================================================
// Error Result Type (Optional-like, but more semantic)
// ============================================================================

template<typename T>
class Result {
public:
    // Success constructor
    explicit Result(const T& value) : m_value(value), m_hasError(false), m_error("") {}
    explicit Result(T&& value) : m_value(std::move(value)), m_hasError(false), m_error("") {}
    
    // Error constructor
    explicit Result(const QString& error) : m_hasError(true), m_error(error) {}
    
    // Status checks
    bool isSuccess() const { return !m_hasError; }
    bool isError() const { return m_hasError; }
    
    // Get value (asserts if error)
    const T& value() const {
        Q_ASSERT(!m_hasError);
        return m_value;
    }
    
    T& mutable_value() {
        Q_ASSERT(!m_hasError);
        return m_value;
    }
    
    // Get error message
    const QString& error() const {
        Q_ASSERT(m_hasError);
        return m_error;
    }
    
    // Operator overloads for convenience
    explicit operator bool() const { return isSuccess(); }
    bool operator!() const { return isError(); }

private:
    T m_value;
    bool m_hasError;
    QString m_error;
};

// ============================================================================
// RAII Wrappers for Resource Cleanup
// ============================================================================

/**
 * @brief RAII wrapper for cleanup functions
 * 
 * Usage:
 *   void* resource = acquire();
 *   ScopeGuard cleanup([resource] { release(resource); });
 *   // resource is automatically released on scope exit
 */
template<typename CleanupFunc>
class ScopeGuard {
public:
    explicit ScopeGuard(CleanupFunc func) : m_cleanup(func), m_enabled(true) {}
    
    ~ScopeGuard() {
        if (m_enabled) {
            m_cleanup();
        }
    }
    
    // Disable cleanup (e.g., if error recovery succeeded)
    void dismiss() { m_enabled = false; }
    
private:
    CleanupFunc m_cleanup;
    bool m_enabled;
};

// ============================================================================
// Error Logging & Reporting
// ============================================================================

/**
 * @brief Log and forward error to user
 * 
 * Used when operation fails but should notify user
 * 
 * Usage:
 *   if (!loadFile(path, buffer, &errorMsg)) {
 *       reportUserError("File Load Failed", errorMsg);
 *       return;
 *   }
 */
void reportUserError(const QString& title, const QString& message);
void reportWarning(const QString& title, const QString& message);
void reportInfo(const QString& title, const QString& message);

// ============================================================================
// Validation Helpers
// ============================================================================

/**
 * @brief Validate file exists and is readable
 */
bool validateFileExists(const QString& path, QString* error = nullptr);

/**
 * @brief Validate ImageBuffer has valid dimensions
 */
bool validateBuffer(const class ImageBuffer& buffer, QString* error = nullptr);

/**
 * @brief Validate file format matches expected
 */
bool validateFileFormat(const QString& path, const QString& expectedMagic, QString* error = nullptr);

#endif // ERRORHANDLING_H
