// =============================================================================
// JSSyntaxHighlighter.h
//
// QSyntaxHighlighter subclass for JavaScript code in the Script Console.
// Highlights keywords, TStar globals, strings, comments, and numbers.
// =============================================================================

#ifndef JSSYNTAXHIGHLIGHTER_H
#define JSSYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

class JSSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit JSSyntaxHighlighter(QTextDocument* parent = nullptr);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };

    QVector<HighlightRule> m_rules;

    // Multi-line comment state
    QRegularExpression m_commentStart;
    QRegularExpression m_commentEnd;
    QTextCharFormat    m_multiLineCommentFormat;
};

#endif // JSSYNTAXHIGHLIGHTER_H
