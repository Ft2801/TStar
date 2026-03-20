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
            // Defer substitution to runner
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
    // We now defer most substitution to execution time in ScriptRunner.
    // But we still handle simple 'set' expansion here for immediate variables
    // if needed. For now, we return text as is to let ScriptRunner handle it.
    return text;
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
        QString token = tokens[i];
        
        if (token.startsWith('-') && token.length() > 1) {
            int prefixLen = token.startsWith("--") ? 2 : 1;
            QString opt = token.mid(prefixLen);
            int eqIdx = opt.indexOf('=');
            
            if (eqIdx > 0) {
                // --name=value or -n=value
                cmd.options[opt.left(eqIdx)] = opt.mid(eqIdx + 1);
            } else {
                // Check if next token is '=' or a value
                if (i + 1 < tokens.size()) {
                    QString next = tokens[i+1];
                    if (next == "=") {
                        if (i + 2 < tokens.size()) {
                            cmd.options[opt] = tokens[i+2];
                            i += 2;
                        } else {
                            cmd.options[opt] = "";
                            i += 1;
                        }
                    } else if (!next.startsWith('-')) {
                        cmd.options[opt] = next;
                        i++;
                    } else {
                        cmd.options[opt] = "";
                    }
                } else {
                    cmd.options[opt] = "";
                }
            }
        } else if (token == "=") {
            // Skip stray equals
            continue;
        } else {
            // Positional argument
            cmd.args.append(token);
        }
    }
    
    return true;
}

} // namespace Scripting
