#include "PixelMathDialog.h"
#include "../MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>
#include <stack>
#include <omp.h>

// ============================================================================
// Expression Parser AST
// ============================================================================
struct ASTNode {
    enum Type { 
        ADD, SUB, MUL, DIV, POW, 
        VAR_R, VAR_G, VAR_B, CONST,
        FUNC_MTF, FUNC_GAMMA, FUNC_CLAMP, FUNC_MIN, FUNC_MAX, FUNC_IFF,
        FUNC_ABS, FUNC_SQRT, FUNC_LOG, FUNC_SIN, FUNC_COS
    } type;
    
    float val = 0;
    std::vector<ASTNode*> children;

    ASTNode(Type t) : type(t) {}
    ASTNode(float v) : type(CONST), val(v) {}
    ~ASTNode() { for (auto c : children) delete c; }

    float eval(float r, float g, float b) const {
        switch (type) {
            case VAR_R: return r;
            case VAR_G: return g;
            case VAR_B: return b;
            case CONST: return val;
            case ADD: return children[0]->eval(r,g,b) + children[1]->eval(r,g,b);
            case SUB: return children[0]->eval(r,g,b) - children[1]->eval(r,g,b);
            case MUL: return children[0]->eval(r,g,b) * children[1]->eval(r,g,b);
            case DIV: {
                float d = children[1]->eval(r,g,b);
                return (std::abs(d) < 1e-10f) ? 0.0f : children[0]->eval(r,g,b) / d;
            }
            case POW: return std::pow(std::max(0.0f, children[0]->eval(r,g,b)), children[1]->eval(r,g,b));
            case FUNC_ABS:  return std::abs(children[0]->eval(r,g,b));
            case FUNC_SQRT: return std::sqrt(std::max(0.0f, children[0]->eval(r,g,b)));
            case FUNC_LOG:  return std::log10(std::max(1e-10f, children[0]->eval(r,g,b)));
            case FUNC_SIN:  return std::sin(children[0]->eval(r,g,b));
            case FUNC_COS:  return std::cos(children[0]->eval(r,g,b));
            case FUNC_MIN:  return std::min(children[0]->eval(r,g,b), children[1]->eval(r,g,b));
            case FUNC_MAX:  return std::max(children[0]->eval(r,g,b), children[1]->eval(r,g,b));
            case FUNC_MTF: {
                float x = children[0]->eval(r,g,b);
                float m = children[1]->eval(r,g,b);
                if (x <= 0) return 0;
                if (x >= 1) return 1;
                float den = ((2.0f * m - 1.0f) * x) - m;
                return (std::abs(den) < 1e-10f) ? x : ((m - 1.0f) * x) / den;
            }
            case FUNC_GAMMA: return std::pow(std::max(0.0f, children[0]->eval(r,g,b)), children[1]->eval(r,g,b));
            case FUNC_CLAMP: return std::clamp(children[0]->eval(r,g,b), children[1]->eval(r,g,b), children[2]->eval(r,g,b));
            case FUNC_IFF:   return (children[0]->eval(r,g,b) > 0.5f) ? children[1]->eval(r,g,b) : children[2]->eval(r,g,b);
            default: return 0;
        }
    }
};

// ============================================================================
// Recursive Descent Parser
// ============================================================================
class PMParser {
public:
    PMParser(const QString& s) : m_str(s), m_pos(0) {}

    ASTNode* parse() {
        return parseExpression();
    }

    QString error() const { return m_error; }

private:
    ASTNode* parseExpression() {
        ASTNode* node = parseTerm();
        while (m_pos < m_str.length()) {
            skipWS();
            if (m_pos >= m_str.length()) break;
            if (m_str[m_pos] == '+') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::ADD);
                next->children.push_back(node);
                next->children.push_back(parseTerm());
                node = next;
            } else if (m_str[m_pos] == '-') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::SUB);
                next->children.push_back(node);
                next->children.push_back(parseTerm());
                node = next;
            } else break;
        }
        return node;
    }

    ASTNode* parseTerm() {
        ASTNode* node = parseFactor();
        while (m_pos < m_str.length()) {
            skipWS();
            if (m_pos >= m_str.length()) break;
            if (m_str[m_pos] == '*') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::MUL);
                next->children.push_back(node);
                next->children.push_back(parseFactor());
                node = next;
            } else if (m_str[m_pos] == '/') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::DIV);
                next->children.push_back(node);
                next->children.push_back(parseFactor());
                node = next;
            } else if (m_str[m_pos] == '^') {
                m_pos++;
                ASTNode* next = new ASTNode(ASTNode::POW);
                next->children.push_back(node);
                next->children.push_back(parseFactor());
                node = next;
            } else break;
        }
        return node;
    }

    ASTNode* parseFactor() {
        skipWS();
        if (m_pos >= m_str.length()) return new ASTNode(0.0f);

        if (m_str[m_pos] == '~') {
            m_pos++;
            ASTNode* next = new ASTNode(ASTNode::SUB);
            next->children.push_back(new ASTNode(1.0f));
            next->children.push_back(parseFactor());
            return next;
        }

        if (m_str[m_pos] == '-') {
            m_pos++;
            ASTNode* next = new ASTNode(ASTNode::SUB);
            next->children.push_back(new ASTNode(0.0f));
            next->children.push_back(parseFactor());
            return next;
        }

        if (m_str[m_pos] == '(') {
            m_pos++;
            ASTNode* node = parseExpression();
            skipWS();
            if (m_pos >= m_str.length()) {
                m_error = QCoreApplication::translate("PixelMathDialog", "Missing closing parenthesis");
                return node;
            }
            if (m_str[m_pos] == ')') m_pos++;
            else m_error = QCoreApplication::translate("PixelMathDialog", "Expected ')' but found '%1'").arg(m_str[m_pos]);
            return node;
        }

        if (m_str[m_pos].isDigit() || m_str[m_pos] == '.') {
            int start = m_pos;
            while (m_pos < m_str.length() && (m_str[m_pos].isDigit() || m_str[m_pos] == '.')) m_pos++;
            return new ASTNode(m_str.mid(start, m_pos - start).toFloat());
        }

        if (m_str[m_pos].isLetter()) {
            int start = m_pos;
            while (m_pos < m_str.length() && (m_str[m_pos].isLetterOrNumber() || m_str[m_pos] == '_')) m_pos++;
            QString name = m_str.mid(start, m_pos - start).toLower();

            if (name == "r") return new ASTNode(ASTNode::VAR_R);
            if (name == "g") return new ASTNode(ASTNode::VAR_G);
            if (name == "b") return new ASTNode(ASTNode::VAR_B);

            // Functions
            skipWS();
            if (m_pos < m_str.length() && m_str[m_pos] == '(') {
                m_pos++;
                std::vector<ASTNode*> args;
                while (m_pos < m_str.length() && m_str[m_pos] != ')') {
                    args.push_back(parseExpression());
                    skipWS();
                    if (m_pos < m_str.length() && m_str[m_pos] == ',') {
                        m_pos++;
                        skipWS();
                    }
                }
                if (m_pos >= m_str.length()) {
                    m_error = QCoreApplication::translate("PixelMathDialog", "Unclosed function call: %1").arg(name);
                    for (auto a : args) delete a;
                    return new ASTNode(0.0f);
                }
                m_pos++;  // Skip ')'

                ASTNode* func = nullptr;
                if (name == "mtf" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_MTF);
                else if (name == "gamma" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_GAMMA);
                else if (name == "clamp" && args.size() == 3) func = new ASTNode(ASTNode::FUNC_CLAMP);
                else if (name == "min" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_MIN);
                else if (name == "max" && args.size() == 2) func = new ASTNode(ASTNode::FUNC_MAX);
                else if (name == "iff" && args.size() == 3) func = new ASTNode(ASTNode::FUNC_IFF);
                else if (name == "abs" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_ABS);
                else if (name == "sqrt" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_SQRT);
                else if (name == "log" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_LOG);
                else if (name == "sin" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_SIN);
                else if (name == "cos" && args.size() == 1) func = new ASTNode(ASTNode::FUNC_COS);
                else {
                    // Check if this is an unknown function or a typo
                    for (auto a : args) delete a;
                    if (args.size() > 0) {
                        m_error = QCoreApplication::translate("PixelMathDialog", "Unknown function '%1' with %2 arguments").arg(name).arg(args.size());
                    } else {
                        m_error = QCoreApplication::translate("PixelMathDialog", "Unknown variable or function: '%1' (only r, g, b are allowed)").arg(name);
                    }
                    return new ASTNode(0.0f);
                }

                if (func) {
                    func->children = args;
                    return func;
                } else {
                    for (auto a : args) delete a;
                    m_error = QCoreApplication::translate("PixelMathDialog", "Unknown function or wrong arg count: %1").arg(name);
                    return new ASTNode(0.0f);
                }
            }
        }

        return new ASTNode(0.0f);
    }

    void skipWS() {
        while (m_pos < m_str.length() && m_str[m_pos].isSpace()) m_pos++;
    }

    QString m_str;
    int m_pos;
    QString m_error;
};

// ============================================================================
// PixelMathDialog Implementation
// ============================================================================

PixelMathDialog::PixelMathDialog(MainWindow* parent, ImageViewer* viewer)
    : QDialog(parent), m_mainWin(parent), m_viewer(viewer)
{
    setWindowTitle(tr("Pixel Math (Pro)"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    resize(700, 450);
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    
    setupUI();

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

PixelMathDialog::~PixelMathDialog() {}

void PixelMathDialog::setViewer(ImageViewer* viewer) {
    m_viewer = viewer;
}

void PixelMathDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);
    
    // Help Label
    QLabel* helpLabel = new QLabel(tr(
        "<b>Variables:</b> r, g, b<br>"
        "<b>Targets:</b> R = ...; G = ...; B = ...;<br>"
        "<b>Operators:</b> +, -, *, /, ^, ( ), ~ (invert)<br>"
        "<b>Functions:</b> mtf(x, mid), gamma(x, g), clamp(x, lo, hi), iff(cond, t, f), min(a, b), max(a, b), sqrt, abs, log, sin, cos<br>"
        "<b>Example:</b> R = ~r; G = (g * 1.5) - 0.1; B = mtf(b, 0.5);")
    );
    helpLabel->setWordWrap(true);
    mainLayout->addWidget(helpLabel);
    
    // Expression Editor
    QGroupBox* exprGroup = new QGroupBox(tr("Channel Expressions"), this);
    QVBoxLayout* exprLayout = new QVBoxLayout(exprGroup);
    m_exprEdit = new QPlainTextEdit(this);
    m_exprEdit->setPlaceholderText(tr("R = r; G = g; B = b;"));
    m_exprEdit->setFont(QFont("Consolas", 11));
    exprLayout->addWidget(m_exprEdit);
    mainLayout->addWidget(exprGroup);

    // Options
    m_checkRescale = new QCheckBox(tr("Rescale result (maps min-max to 0-1)"), this);
    mainLayout->addWidget(m_checkRescale);
    
    // Status
    m_statusLabel = new QLabel(tr("Ready"), this);
    mainLayout->addWidget(m_statusLabel);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnApply = new QPushButton(tr("Apply"), this);
    m_btnCancel = new QPushButton(tr("Cancel"), this);
    
    connect(m_btnApply, &QPushButton::clicked, this, &PixelMathDialog::onApply);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnCancel);
    btnLayout->addWidget(m_btnApply);
    
    mainLayout->addLayout(btnLayout);
}

bool PixelMathDialog::evaluateExpression(const QString& expr, ImageBuffer& buf, bool rescale, QString* errorMsg) {
    if (buf.channels() != 3) {
        if (errorMsg) *errorMsg = tr("Pixel Math requires RGB image.");
        return false;
    }
    
    int w = buf.width();
    int h = buf.height();
    size_t totalPixels = (size_t)w * h;
    std::vector<float>& data = buf.data();
    std::vector<float> src = data; // Reference copy
    
    // Parse target assignments
    ASTNode* asts[3] = { nullptr, nullptr, nullptr };
    
    QRegularExpression reAssign(R"(([RGB])\s*=\s*([^;]+);?)");
    auto matches = reAssign.globalMatch(expr);
    
    while (matches.hasNext()) {
        auto m = matches.next();
        char target = m.captured(1).toUpper().at(0).toLatin1();
        QString exprPart = m.captured(2);
        
        PMParser p(exprPart);
        ASTNode* node = p.parse();
        if (!p.error().isEmpty()) {
            if (errorMsg) *errorMsg = tr("Parse error: %1").arg(p.error());
            for (int i=0; i<3; ++i) delete asts[i];
            delete node;
            return false;
        }
        
        int idx = (target == 'R') ? 0 : (target == 'G' ? 1 : 2);
        if (asts[idx]) delete asts[idx];
        asts[idx] = node;
    }
    
    // Fallback to identity for missing targets
    if (!asts[0]) asts[0] = new ASTNode(ASTNode::VAR_R);
    if (!asts[1]) asts[1] = new ASTNode(ASTNode::VAR_G);
    if (!asts[2]) asts[2] = new ASTNode(ASTNode::VAR_B);

    float globalMin = 1e30f;
    float globalMax = -1e30f;

    // Apply loop
    #pragma omp parallel
    {
        float localMin = 1e30f;
        float localMax = -1e30f;
        
        #pragma omp for
        for (long long i = 0; i < (long long)totalPixels; ++i) {
            size_t idx = i * 3;
            float r = src[idx];
            float g = src[idx + 1];
            float b = src[idx + 2];
            
            for (int c = 0; c < 3; ++c) {
                float res = asts[c]->eval(r, g, b);
                if (rescale) {
                    if (res < localMin) localMin = res;
                    if (res > localMax) localMax = res;
                    data[idx + c] = res;  // Keep original for rescale pass
                } else {
                    // Always clamp to [0, 1] when not rescaling
                    data[idx + c] = std::clamp(res, 0.0f, 1.0f);
                }
            }
        }
        
        if (rescale) {
            #pragma omp critical
            {
                if (localMin < globalMin) globalMin = localMin;
                if (localMax > globalMax) globalMax = localMax;
            }
        }
    }

    // Cleanup AST
    for (int i=0; i<3; ++i) delete asts[i];

    // Rescale pass
    if (rescale && globalMax > globalMin) {
        float den = globalMax - globalMin;
        #pragma omp parallel for
        for (long long i = 0; i < (long long)data.size(); ++i) {
            data[i] = (data[i] - globalMin) / den;
        }
    }
    
    return true;
}

void PixelMathDialog::onApply() {
    QString expr = m_exprEdit->toPlainText().trimmed();
    if (expr.isEmpty()) {
        QMessageBox::warning(this, tr("Empty Expression"), tr("Please enter an expression."));
        return;
    }
    emit apply(expr, m_checkRescale->isChecked());
}
