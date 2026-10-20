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
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
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