/**
 * @file ScriptParser.cpp
 * @brief Implementation of script parser
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "ScriptParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace Scripting {

//=============================================================================
// PARSING
//=============================================================================

bool ScriptParser::parseFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_errors.append(QObject::tr("Cannot open file: %1").arg(path));
        return false;
    }
    
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();
    
    return parseString(content, path);
}

bool ScriptParser::parseString(const QString& content, const QString& sourceName) {
    qDebug() << "ScriptParser::parseString called. Content length:" << content.length();
    clear();
    
    QStringList lines = content.split('\n');
    int lineNumber = 0;
    
    for (const QString& rawLine : lines) {
        lineNumber++;
        
        QString line = rawLine.trimmed();
        
        // Handle continuation
        if (!m_pendingLine.isEmpty()) {
            line = m_pendingLine + " " + line;
            m_pendingLine.clear();
        }
        
        // Check for continuation marker
        if (line.endsWith('\\')) {
            m_pendingLine = line.left(line.length() - 1);
            continue;
        }
        
        if (!parseLine(line, lineNumber)) {
            // Continue parsing even on errors
        }
    }
    
    // Check for unclosed continuation
    if (!m_pendingLine.isEmpty()) {
        m_errors.append(QObject::tr("%1: Unexpected end of file in continued line")
                       .arg(sourceName));
    }
    
    return m_errors.isEmpty();
}

void ScriptParser::clear() {
    m_commands.clear();
    m_errors.clear();
    m_pendingLine.clear();
}

//=============================================================================
// LINE PARSING
//=============================================================================

bool ScriptParser::parseLine(const QString& line, int lineNumber) {
    // Skip empty lines
    if (line.isEmpty()) {
        return true;
    }
    
    // Skip comments
    if (line.startsWith('#') || line.startsWith(';')) {
        return true;
    }
    
    // Check for variable assignment: set name value
    if (line.startsWith("set ", Qt::CaseInsensitive)) {
        QString rest = line.mid(4).trimmed();
        int spaceIdx = rest.indexOf(' ');
        if (spaceIdx > 0) {
            QString varName = rest.left(spaceIdx).trimmed();
            QString varValue = rest.mid(spaceIdx + 1).trimmed();
            varValue = substituteVariables(varValue);
            m_variables[varName] = varValue;
            return true;
        }
    }
    
    // Perform variable substitution
    QString processed = substituteVariables(line);
    
    // Tokenize
    QStringList tokens = tokenize(processed);
    if (tokens.isEmpty()) {
        return true;
    }
    
    // Parse into command
    ScriptCommand cmd;
    cmd.lineNumber = lineNumber;
    
    if (!parseTokens(tokens, cmd)) {
        return false;
    }
    
    m_commands.append(cmd);
    return true;
}

//=============================================================================
// VARIABLE SUBSTITUTION
//=============================================================================

void ScriptParser::setVariable(const QString& name, const QString& value) {
    m_variables[name] = value;
}

QString ScriptParser::substituteVariables(const QString& text) {
    QString result = text;
    
    // ${variable} syntax
    QRegularExpression bracePattern("\\$\\{([^}]+)\\}");
    QRegularExpressionMatchIterator it = bracePattern.globalMatch(result);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString varName = match.captured(1);
        if (m_variables.contains(varName)) {
            result.replace(match.captured(0), m_variables[varName]);
        }
    }
    
    // $variable syntax (word boundary)
    QRegularExpression simplePattern("\\$([A-Za-z_][A-Za-z0-9_]*)");
    it = simplePattern.globalMatch(result);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString varName = match.captured(1);
        if (m_variables.contains(varName)) {
            result.replace(match.captured(0), m_variables[varName]);
        }
    }
    
    return result;
}

//=============================================================================
// TOKENIZATION
//=============================================================================

QStringList ScriptParser::tokenize(const QString& line) {
    QStringList tokens;
    QString current;
    bool inQuote = false;
    QChar quoteChar;
    
    for (int i = 0; i < line.length(); ++i) {
        QChar c = line[i];
        
        if (inQuote) {
            if (c == quoteChar) {
                inQuote = false;
                if (!current.isEmpty()) {
                    tokens.append(current);
                    current.clear();
                }
            } else if (c == '\\' && i + 1 < line.length()) {
                // Escape sequence
                QChar next = line[++i];
                if (next == 'n') current += '\n';
                else if (next == 't') current += '\t';
                else current += next;
            } else {
                current += c;
            }
        } else {
            if (c == '"' || c == '\'') {
                inQuote = true;
                quoteChar = c;
            } else if (c.isSpace()) {
                if (!current.isEmpty()) {
                    tokens.append(current);
                    current.clear();
                }
            } else if (c == '#' || c == ';') {
                // Comment - stop parsing
                break;
            } else {
                current += c;
            }
        }
    }
    
    if (!current.isEmpty()) {
        tokens.append(current);
    }
    
    return tokens;
}

//=============================================================================
// TOKEN PARSING
//=============================================================================

bool ScriptParser::parseTokens(const QStringList& tokens, ScriptCommand& cmd) {
    if (tokens.isEmpty()) {
        return false;
    }
    
    cmd.name = tokens[0].toLower();
    
    for (int i = 1; i < tokens.size(); ++i) {
        const QString& token = tokens[i];
        
        if (token.startsWith("--")) {
            // Long option: --name=value or --name
            QString opt = token.mid(2);
            int eqIdx = opt.indexOf('=');
            if (eqIdx > 0) {
                cmd.options[opt.left(eqIdx)] = opt.mid(eqIdx + 1);
            } else {
                cmd.options[opt] = QString();
            }
        } else if (token.startsWith('-') && token.length() > 1) {
            // Short option: -n or -n=value
            QString opt = token.mid(1);
            int eqIdx = opt.indexOf('=');
            if (eqIdx > 0) {
                cmd.options[opt.left(eqIdx)] = opt.mid(eqIdx + 1);
            } else {
                cmd.options[opt] = QString();
            }
        } else {
            // Positional argument
            cmd.args.append(token);
        }
    }
    
    return true;
}

} // namespace Scripting
