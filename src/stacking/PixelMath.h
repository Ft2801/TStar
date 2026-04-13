#ifndef PIXELMATH_H
#define PIXELMATH_H

/**
 * @file PixelMath.h
 * @brief Expression-based pixel-level mathematical operations on images.
 *
 * Parses and evaluates arithmetic expressions that reference image
 * buffers by name, producing a new image as output. Supports the
 * standard operators (+, -, *, /), unary inversion (~), and built-in
 * functions such as mtf() (midtones transfer function).
 *
 * Copyright (C) 2024-2026 Siril Team
 */

#include "../ImageBuffer.h"
#include <QString>
#include <QMap>
#include <vector>
#include <memory>

namespace Stacking {

class PixelMath {
public:

    PixelMath();

    /**
     * @brief Register a named variable (image reference).
     * @param name   Variable name used in expressions (e.g. "$T", "img1").
     * @param image  Pointer to the image buffer (caller retains ownership).
     */
    void setVariable(const QString& name, ImageBuffer* image);

    /**
     * @brief Evaluate a mathematical expression and write the result.
     *
     * The expression is tokenized, converted to reverse Polish notation
     * via the shunting-yard algorithm, then executed per-pixel in parallel.
     *
     * @param expression  Mathematical expression string.
     * @param output      Output image (resized to match the largest input).
     * @return true if evaluation succeeded, false on parse or runtime error.
     */
    bool evaluate(const QString& expression, ImageBuffer& output);

    /** @brief Retrieve the last error message (empty on success). */
    QString lastError() const { return m_lastError; }

private:

    // -------------------------------------------------------------------------
    // Token representation for the expression parser.
    // -------------------------------------------------------------------------

    struct Token {
        enum Type { Number, Variable, Operator, Function, LParen, RParen, Comma };
        Type    type;
        QString value;
        double  numValue = 0.0;
    };

    // Parsing stages.
    std::vector<Token> tokenize(const QString& expr);
    std::vector<Token> shuntingYard(const std::vector<Token>& tokens);

    // RPN evaluation.
    bool executeRPN(const std::vector<Token>& rpn, ImageBuffer& output);

    // Built-in math helpers.
    static float mtf(float mid, float val);

    QMap<QString, ImageBuffer*> m_variables;
    QString                    m_lastError;
};

} // namespace Stacking

#endif // PIXELMATH_H