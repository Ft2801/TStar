
#ifndef SCRIPT_PARSER_H
#define SCRIPT_PARSER_H

#include "ScriptTypes.h"
#include <QVector>

namespace Scripting {

class ScriptParser {
public:
    ScriptParser() = default;
    
    /**
     * @brief Parse script from file
     * @param path Path to script file
     * @return true if parsing successful
     */
    bool parseFile(const QString& path);
    
    /**
     * @brief Parse script from string
     * @param content Script content
     * @param sourceName Name for error messages
     * @return true if parsing successful
     */
    bool parseString(const QString& content, const QString& sourceName = "script");
    
    /**
     * @brief Get parsed commands
     */
    const QVector<ScriptCommand>& commands() const { return m_commands; }
    
    /**
     * @brief Get parse errors
     */
    const QStringList& errors() const { return m_errors; }
    
    /**
     * @brief Clear parsed content
     */
    void clear();
    
    /**
     * @brief Set a variable for substitution
     */
    void setVariable(const QString& name, const QString& value);
    
    /**
     * @brief Get current variables
     */
    const QMap<QString, QString>& variables() const { return m_variables; }
    
private:
    /**
     * @brief Parse a single line
     */
    bool parseLine(const QString& line, int lineNumber);
    
    /**
     * @brief Perform variable substitution
     */
    QString substituteVariables(const QString& text);
    
    /**
     * @brief Tokenize a command line
     */
    QStringList tokenize(const QString& line);
    
    /**
     * @brief Parse tokens into command
     */
    bool parseTokens(const QStringList& tokens, ScriptCommand& cmd);
    
    QVector<ScriptCommand> m_commands;
    QMap<QString, QString> m_variables;
    QStringList m_errors;
    QString m_pendingLine;  // For continuation lines
};

} // namespace Scripting

#endif // SCRIPT_PARSER_H
