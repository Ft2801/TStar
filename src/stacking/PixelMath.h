/**
 * @file PixelMath.h
 * @brief PixelMath engine for mathematical operations on images
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#ifndef PIXELMATH_H
#define PIXELMATH_H

#include "../ImageBuffer.h"
#include <QString>
#include <QMap>
#include <vector>
#include <memory>

namespace Stacking {

/**
 * @brief PixelMath Engine
 * 
 * Evaluates mathematical expressions on image buffers.
 * Supports:
 * - Arithmetic: +, -, *, /
 * - Functions: mtf, med, mad, min, max, avg
 * - Unary: ~ (Invert: 1-x), - (Negate)
 * - Variables: References to images by name or ID
 */
class PixelMath {
public:
    PixelMath();
    
    /**
     * @brief Set a variable (image reference)
     */
    void setVariable(const QString& name, ImageBuffer* image);
    
    /**
     * @brief Evaluate expression
     * 
     * @param expression Mathematics expression
     * @param output Output image (resized to match inputs)
     * @return true if successful
     */
    bool evaluate(const QString& expression, ImageBuffer& output);
    
    /**
     * @brief Get last error message
     */
    QString lastError() const { return m_lastError; }

private:
    struct Token {
        enum Type { Number, Variable, Operator, Function, LParen, RParen, Comma };
        Type type;
        QString value;
        double numValue = 0.0;
    };
    
    // RPN Execution
    bool executeRPN(const std::vector<Token>& rpn, ImageBuffer& output);
    
    // Parsing
    std::vector<Token> tokenize(const QString& expr);
    std::vector<Token> shuntingYard(const std::vector<Token>& tokens);
    
    // Math helpers
    static float mtf(float mid, float val);
    
    QMap<QString, ImageBuffer*> m_variables;
    QString m_lastError;
};

} // namespace Stacking

#endif // PIXELMATH_H
