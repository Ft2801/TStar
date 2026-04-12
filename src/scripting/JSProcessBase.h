// =============================================================================
// JSProcessBase.h
//
// Abstract base class for all scriptable image processing algorithms.
//
// Each Process subclass exposes its parameters as Q_PROPERTYs and implements
// executeOn() to delegate to the underlying ImageBuffer algorithm.
// =============================================================================

#ifndef JSPROCESSBASE_H
#define JSPROCESSBASE_H

#include <QObject>
#include <QVariant>

namespace Scripting {

class JSProcessBase : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    explicit JSProcessBase(QObject* parent = nullptr) : QObject(parent) {}
    ~JSProcessBase() override = default;

    /** @brief Human-readable process name (e.g. "Curves", "Saturation"). */
    virtual QString name() const = 0;

    /**
     * @brief Execute this process on a target image.
     *
     * @param target  Must be a JSImage* (passed as QObject* for JS interop).
     * @return true on success, false on failure.
     *
     * The method validates the target, extracts the ImageBuffer, applies
     * the algorithm with the current property values, and returns.
     */
    Q_INVOKABLE virtual bool executeOn(QObject* target) = 0;

    /**
     * @brief Return a map of all current parameters and their values.
     *
     * Useful for serialization, logging, and the API reference panel.
     */
    Q_INVOKABLE virtual QVariantMap parameters() const = 0;
};

} // namespace Scripting

#endif // JSPROCESSBASE_H
