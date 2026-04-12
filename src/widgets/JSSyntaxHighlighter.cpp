// =============================================================================
// JSSyntaxHighlighter.cpp
//
// JavaScript syntax highlighter implementation.
// =============================================================================

#include "JSSyntaxHighlighter.h"

JSSyntaxHighlighter::JSSyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    HighlightRule rule;

    // -- JavaScript keywords --------------------------------------------------
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor("#C586C0"));  // Purple
    keywordFormat.setFontWeight(QFont::Bold);

    const QStringList keywords = {
        "\\bvar\\b", "\\blet\\b", "\\bconst\\b", "\\bfunction\\b",
        "\\breturn\\b", "\\bif\\b", "\\belse\\b", "\\bfor\\b",
        "\\bwhile\\b", "\\bdo\\b", "\\bswitch\\b", "\\bcase\\b",
        "\\bbreak\\b", "\\bcontinue\\b", "\\bnew\\b", "\\bthis\\b",
        "\\btypeof\\b", "\\binstanceof\\b", "\\bin\\b", "\\bof\\b",
        "\\btry\\b", "\\bcatch\\b", "\\bfinally\\b", "\\bthrow\\b",
        "\\bdelete\\b", "\\bvoid\\b", "\\bwith\\b", "\\byield\\b",
        "\\bclass\\b", "\\bextends\\b", "\\bsuper\\b",
        "\\bimport\\b", "\\bexport\\b", "\\bdefault\\b",
        "\\basync\\b", "\\bawait\\b"
    };

    for (const QString& pattern : keywords) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        m_rules.append(rule);
    }

    // -- Boolean / null / undefined literals -----------------------------------
    QTextCharFormat literalFormat;
    literalFormat.setForeground(QColor("#569CD6"));  // Blue

    const QStringList literals = {
        "\\btrue\\b", "\\bfalse\\b", "\\bnull\\b", "\\bundefined\\b",
        "\\bNaN\\b", "\\bInfinity\\b"
    };

    for (const QString& pattern : literals) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = literalFormat;
        m_rules.append(rule);
    }

    // -- TStar global objects -------------------------------------------------
    QTextCharFormat tstarFormat;
    tstarFormat.setForeground(QColor("#4EC9B0"));  // Teal
    tstarFormat.setFontWeight(QFont::Bold);

    const QStringList tstarGlobals = {
        "\\bApp\\b", "\\bConsole\\b", "\\bconsole\\b",
        "\\bCurves\\b", "\\bSaturation\\b", "\\bSCNR\\b",
        "\\bGHS\\b", "\\bStretch\\b", "\\bImage\\b"
    };

    for (const QString& pattern : tstarGlobals) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = tstarFormat;
        m_rules.append(rule);
    }

    // -- Method/property names after dot --------------------------------------
    QTextCharFormat methodFormat;
    methodFormat.setForeground(QColor("#DCDCAA"));  // Yellow
    rule.pattern = QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()");
    rule.format = methodFormat;
    m_rules.append(rule);

    // -- Numbers --------------------------------------------------------------
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor("#B5CEA8"));  // Light green
    rule.pattern = QRegularExpression("\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?\\b");
    rule.format = numberFormat;
    m_rules.append(rule);

    // -- Strings (double-quoted) ----------------------------------------------
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor("#CE9178"));  // Orange
    rule.pattern = QRegularExpression("\"[^\"]*\"");
    rule.format = stringFormat;
    m_rules.append(rule);

    // -- Strings (single-quoted) ----------------------------------------------
    rule.pattern = QRegularExpression("'[^']*'");
    rule.format = stringFormat;
    m_rules.append(rule);

    // -- Strings (template literals) ------------------------------------------
    rule.pattern = QRegularExpression("`[^`]*`");
    rule.format = stringFormat;
    m_rules.append(rule);

    // -- Single-line comments -------------------------------------------------
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor("#6A9955"));  // Green
    commentFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("//[^\n]*");
    rule.format = commentFormat;
    m_rules.append(rule);

    // -- Multi-line comment setup ---------------------------------------------
    m_multiLineCommentFormat = commentFormat;
    m_commentStart = QRegularExpression("/\\*");
    m_commentEnd   = QRegularExpression("\\*/");
}

void JSSyntaxHighlighter::highlightBlock(const QString& text)
{
    // Apply single-line rules
    for (const HighlightRule& rule : m_rules) {
        QRegularExpressionMatchIterator matchIt = rule.pattern.globalMatch(text);
        while (matchIt.hasNext()) {
            QRegularExpressionMatch match = matchIt.next();
            setFormat(match.capturedStart(),
                      match.capturedLength(),
                      rule.format);
        }
    }

    // Multi-line comment handling
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1) {
        startIndex = text.indexOf(m_commentStart);
    }

    while (startIndex >= 0) {
        QRegularExpressionMatch endMatch;
        int endIndex = text.indexOf(m_commentEnd, startIndex, &endMatch);
        int commentLength;

        if (endIndex == -1) {
            // Comment extends to the end of this block
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + endMatch.capturedLength();
        }

        setFormat(startIndex, commentLength, m_multiLineCommentFormat);
        startIndex = text.indexOf(m_commentStart, startIndex + commentLength);
    }
}
