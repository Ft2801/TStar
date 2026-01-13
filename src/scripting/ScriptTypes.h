
#ifndef SCRIPT_TYPES_H
#define SCRIPT_TYPES_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QMap>
#include <functional>

namespace Scripting {

/**
 * @brief Script command structure
 */
struct ScriptCommand {
    QString name;                      ///< Command name
    QStringList args;                  ///< Positional arguments
    QMap<QString, QString> options;    ///< Named options (--key=value)
    int lineNumber = 0;                ///< Source line number
    
    /**
     * @brief Check if an option is set
     */
    bool hasOption(const QString& key) const {
        return options.contains(key);
    }
    
    /**
     * @brief Get option value with default
     */
    QString option(const QString& key, const QString& defaultValue = QString()) const {
        return options.value(key, defaultValue);
    }
    
    /**
     * @brief Get option as integer
     */
    int optionInt(const QString& key, int defaultValue = 0) const {
        bool ok;
        int val = options.value(key).toInt(&ok);
        return ok ? val : defaultValue;
    }
    
    /**
     * @brief Get option as double
     */
    double optionDouble(const QString& key, double defaultValue = 0.0) const {
        bool ok;
        double val = options.value(key).toDouble(&ok);
        return ok ? val : defaultValue;
    }
    
    /**
     * @brief Get option as bool (presence = true)
     */
    bool optionBool(const QString& key) const {
        if (!options.contains(key)) return false;
        QString val = options[key].toLower();
        return val.isEmpty() || val == "true" || val == "1" || val == "yes";
    }
};

/**
 * @brief Script execution result
 */
enum class ScriptResult {
    OK = 0,
    SyntaxError,
    CommandError,
    FileError,
    Cancelled,
    UnknownCommand
};

/**
 * @brief Command handler function type
 * 
 * Returns true on success, false on failure
 */
using CommandHandler = std::function<bool(const ScriptCommand&)>;

/**
 * @brief Command definition
 */
struct CommandDef {
    QString name;
    int minArgs = 0;
    int maxArgs = -1;  // -1 = unlimited
    QString usage;
    QString description;
    CommandHandler handler;
    bool scriptable = true;  // Can be used in scripts
    
    CommandDef() = default;
    CommandDef(const QString& n, int min, int max, 
               const QString& u, const QString& d,
               CommandHandler h, bool s = true)
        : name(n), minArgs(min), maxArgs(max)
        , usage(u), description(d), handler(h), scriptable(s) {}
};

/**
 * @brief Script variable (for variable substitution)
 */
struct ScriptVariable {
    QString name;
    QVariant value;
};

/**
 * @brief Progress callback for script execution
 */
using ScriptProgressCallback = std::function<void(const QString&, double)>;

/**
 * @brief Log callback for script execution  
 */
using ScriptLogCallback = std::function<void(const QString&, const QString&)>;

} // namespace Scripting

#endif // SCRIPT_TYPES_H
