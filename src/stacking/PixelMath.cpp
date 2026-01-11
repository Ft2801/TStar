/**
 * @file PixelMath.cpp
 * @brief Implementation of PixelMath engine
 * 
 * Copyright (C) 2024-2026 TStar Team
 */

#include "PixelMath.h"
#include <QStack>
#include <cmath>
#include <QDebug>

namespace Stacking {

PixelMath::PixelMath() {}

void PixelMath::setVariable(const QString& name, ImageBuffer* image) {
    m_variables[name] = image;
}

float PixelMath::mtf(float mid, float val) {
    // MTF (Midtones Transfer Function)
    // Maps 0->0, 1->1, mid->0.5
    // Formula: (m-1)x / ((2m-1)x - m)
    if (val <= 0.0f) return 0.0f;
    if (val >= 1.0f) return 1.0f;
    if (mid <= 0.0f || mid >= 1.0f) return val; // Identity if invalid mid
    
    double m = static_cast<double>(mid);
    double x = static_cast<double>(val);
    
    return static_cast<float>( (m - 1.0) * x / ((2.0 * m - 1.0) * x - m) );
}

bool PixelMath::evaluate(const QString& expression, ImageBuffer& output) {
    auto tokens = tokenize(expression);
    if (tokens.empty()) {
        m_lastError = "Empty expression";
        return false;
    }
    
    auto rpn = shuntingYard(tokens);
    if (rpn.empty()) {
        return false; // Error set in shuntingYard usually, or truly empty
    }
    
    return executeRPN(rpn, output);
}

// Tokenizer
std::vector<PixelMath::Token> PixelMath::tokenize(const QString& expr) {
    std::vector<Token> tokens;
    int pos = 0;
    int len = expr.length();
    
    while (pos < len) {
        QChar c = expr[pos];
        
        if (c.isSpace()) {
            pos++;
            continue;
        }
        
        if (c.isDigit() || c == '.') {
            // Number
            QString numStr;
            while (pos < len && (expr[pos].isDigit() || expr[pos] == '.')) {
                numStr += expr[pos++];
            }
            Token t; t.type = Token::Number; t.numValue = numStr.toDouble();
            tokens.push_back(t);
        }
        else if (c.isLetter() || c == '$') {
            // Variable or Function
            QString name;
            while (pos < len && (expr[pos].isLetterOrNumber() || expr[pos] == '_' || expr[pos] == '$')) {
                name += expr[pos++];
            }
            Token t; 
            t.value = name;
            // Identifying if function or var?
            // If next char is '(', it's likely function (except if it's a variable multiplied implicit? No support for implicit mul)
            // But '$' logic suggests variable.
            // Check known functions
            if (name == "mtf" || name == "min" || name == "max" || name == "median") 
                 t.type = Token::Function;
            else 
                 t.type = Token::Variable;
            
            tokens.push_back(t);
        }
        else {
            // Operators
            Token t; t.type = Token::Operator; t.value = c;
            if (c == '(') t.type = Token::LParen;
            else if (c == ')') t.type = Token::RParen;
            else if (c == ',') t.type = Token::Comma;
            else if (c == '~') t.type = Token::Function; // Treat Unary ~ as function "invert" internally? Or Operator
            
            // ~ is unary prefix.
            if (c == '~') {
               t.type = Token::Operator; // Unary operator priority
            }
            
            tokens.push_back(t);
            pos++;
        }
    }
    return tokens;
}

// Shunting Yard
std::vector<PixelMath::Token> PixelMath::shuntingYard(const std::vector<Token>& tokens) {
    std::vector<Token> outputQueue;
    QStack<Token> operatorStack;
    
    // Operator precedence
    auto precedence = [](const QString& op) {
        if (op == "~") return 4; // Unary High
        if (op == "*" || op == "/") return 3;
        if (op == "+" || op == "-") return 2;
        return 0;
    };
    
    for (const auto& token : tokens) {
        if (token.type == Token::Number || token.type == Token::Variable) {
            outputQueue.push_back(token);
        }
        else if (token.type == Token::Function) {
            operatorStack.push(token);
        }
        else if (token.type == Token::Comma) {
            while (!operatorStack.empty() && operatorStack.top().type != Token::LParen) {
                outputQueue.push_back(operatorStack.pop());
            }
        }
        else if (token.type == Token::Operator) {
            while (!operatorStack.empty() && operatorStack.top().type == Token::Operator &&
                   precedence(operatorStack.top().value) >= precedence(token.value)) {
                outputQueue.push_back(operatorStack.pop());
            }
            operatorStack.push(token);
        }
        else if (token.type == Token::LParen) {
            operatorStack.push(token);
        }
        else if (token.type == Token::RParen) {
            while (!operatorStack.empty() && operatorStack.top().type != Token::LParen) {
                outputQueue.push_back(operatorStack.pop());
            }
            if (!operatorStack.empty() && operatorStack.top().type == Token::LParen) {
                operatorStack.pop(); // Pop (
                if (!operatorStack.empty() && operatorStack.top().type == Token::Function) {
                    outputQueue.push_back(operatorStack.pop()); // Pop Function
                }
            }
        }
    }
    
    while (!operatorStack.empty()) {
        outputQueue.push_back(operatorStack.pop());
    }
    
    return outputQueue;
}

// Helper to get pixel from operand (either scalar or image)
struct Operand {
    ImageBuffer* img = nullptr;
    double scalar = 0.0;
    bool isImage = false;
    
    float getValue(int x, int y, int c) const {
        if (isImage && img) {
            // Handle bounds? Assuming aligned.
            if (c >= img->channels()) c = 0; // Mono fallback
            return img->value(x, y, c);
        }
        return static_cast<float>(scalar);
    }
};

// Execution
bool PixelMath::executeRPN(const std::vector<Token>& rpn, ImageBuffer& output) {
    // 1. Determine output dimensions (Max of inputs)
    int w = 0, h = 0, c = 0;
    for (const auto& t : rpn) {
        if (t.type == Token::Variable) {
            if (m_variables.contains(t.value)) {
                auto* img = m_variables[t.value];
                if (img) {
                    w = std::max(w, img->width());
                    h = std::max(h, img->height());
                    c = std::max(c, img->channels());
                }
            } else {
                m_lastError = "Variable not found: " + t.value;
                return false;
            }
        }
    }
    
    if (w == 0) { // All scalars? Default 1x1
        w=1; h=1; c=1;
    }
    
    output = ImageBuffer(w, h, c);
    
    // 2. Iterate pixels
    #pragma omp parallel for collapse(2)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                // Use a local stack for each pixel to ensure thread safety
                std::vector<Operand> stack;
                stack.reserve(rpn.size());
                
                bool error = false;
                for (const auto& token : rpn) {
                    if (token.type == Token::Number) {
                        Operand op; op.scalar = token.numValue;
                        stack.push_back(op);
                    }
                    else if (token.type == Token::Variable) {
                        Operand op; op.isImage = true; op.img = m_variables[token.value];
                        stack.push_back(op);
                    }
                    else if (token.type == Token::Operator || token.type == Token::Function) {
                        if (token.value == "~") {
                            // Unary Invert
                            if (stack.empty()) { error = true; break; }
                            Operand a = stack.back(); stack.pop_back();
                            float val = a.getValue(x, y, ch);
                            Operand res; res.scalar = 1.0f - val; // assuming range 0-1
                            stack.push_back(res);
                        } 
                        else if (token.value == "mtf") {
                             if (stack.size() < 2) { error = true; break; }
                             Operand valOp = stack.back(); stack.pop_back();
                             Operand midOp = stack.back(); stack.pop_back();
                             float val = valOp.getValue(x, y, ch);
                             float mid = midOp.getValue(x, y, ch);
                             Operand res; res.scalar = mtf(mid, val);
                             stack.push_back(res);
                        }
                        else {
                            // Binary ops
                            if (stack.size() < 2) { error = true; break; }
                            Operand b = stack.back(); stack.pop_back();
                            Operand a = stack.back(); stack.pop_back();
                            
                            float valA = a.getValue(x, y, ch);
                            float valB = b.getValue(x, y, ch);
                            float resVal = 0.0f;
                            
                            if (token.value == "+") resVal = valA + valB;
                            else if (token.value == "-") resVal = valA - valB;
                            else if (token.value == "*") resVal = valA * valB;
                            else if (token.value == "/") resVal = (valB != 0.0f) ? valA / valB : 0.0f;
                            
                            Operand res; res.scalar = resVal;
                            stack.push_back(res);
                        }
                    }
                }
                
                if (error) continue;
                
                if (!stack.empty()) {
                    output.value(x, y, ch) = static_cast<float>(stack.back().scalar);
                }
            }
        }
    }
    
    return true;
}

} // namespace Stacking
