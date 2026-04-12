// =============================================================================
// ScriptConsoleWidget.cpp
//
// Full implementation of the scripting console for the right sidebar.
// =============================================================================

#include "ScriptConsoleWidget.h"
#include "JSSyntaxHighlighter.h"
#include "../scripting/JSRuntime.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QHeaderView>
#include <QDateTime>
#include <QScrollBar>
#include <QToolButton>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

// =============================================================================
// Construction
// =============================================================================

ScriptConsoleWidget::ScriptConsoleWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

// =============================================================================
// Runtime Connection
// =============================================================================

void ScriptConsoleWidget::setRuntime(Scripting::JSRuntime* runtime)
{
    // Disconnect previous runtime
    if (m_runtime) {
        disconnect(m_runtime, nullptr, this, nullptr);
    }

    m_runtime = runtime;

    if (m_runtime) {
        connect(m_runtime, &Scripting::JSRuntime::standardOutput,
                this,      &ScriptConsoleWidget::onStandardOutput);
        connect(m_runtime, &Scripting::JSRuntime::standardError,
                this,      &ScriptConsoleWidget::onStandardError);
        connect(m_runtime, &Scripting::JSRuntime::scriptStarted,
                this,      &ScriptConsoleWidget::onScriptStarted);
        connect(m_runtime, &Scripting::JSRuntime::scriptFinished,
                this,      &ScriptConsoleWidget::onScriptFinished);

        populateApiReference();
    }
}

// =============================================================================
// UI Setup
// =============================================================================

void ScriptConsoleWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Tab widget: Editor | API Reference
    auto* tabs = new QTabWidget(this);
    tabs->setTabPosition(QTabWidget::South);
    tabs->setStyleSheet(
        "QTabWidget::pane { border: none; background: #1e1e1e; }"
        "QTabBar::tab {"
        "  background: #2d2d2d; color: #888; border: none;"
        "  padding: 4px 10px; font-size: 10px; min-width: 50px;"
        "}"
        "QTabBar::tab:selected { background: #1e1e1e; color: #ddd; }"
        "QTabBar::tab:hover { color: #fff; }"
    );

    // -- Editor + Output tab --------------------------------------------------
    auto* editorTab = new QWidget();
    auto* editorLayout = new QVBoxLayout(editorTab);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    // Toolbar
    setupToolbar(editorTab, editorLayout);

    // Splitter: code editor (top) / output log (bottom)
    auto* splitter = new QSplitter(Qt::Vertical, editorTab);
    splitter->setStyleSheet(
        "QSplitter::handle { background: #333; height: 2px; }");

    setupEditor(splitter);
    setupOutputLog(splitter);

    splitter->setSizes({200, 100});
    editorLayout->addWidget(splitter);

    // Status bar
    m_statusLabel = new QLabel(tr("Ready"), editorTab);
    m_statusLabel->setStyleSheet(
        "color: #888; font-size: 10px; padding: 2px 6px;"
        "background: #252525; border-top: 1px solid #333;");
    editorLayout->addWidget(m_statusLabel);

    tabs->addTab(editorTab, tr("Console"));

    // -- API Reference tab ----------------------------------------------------
    setupApiReference(tabs);

    mainLayout->addWidget(tabs);
}

void ScriptConsoleWidget::setupToolbar(QWidget* parent, QVBoxLayout* layout)
{
    auto* toolbar = new QWidget(parent);
    toolbar->setFixedHeight(30);
    toolbar->setStyleSheet(
        "background: #252525; border-bottom: 1px solid #333;");

    auto* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(4, 2, 4, 2);
    tbLayout->setSpacing(3);

    // Scripts selector
    m_scriptsCombo = new QComboBox(toolbar);
    m_scriptsCombo->setFixedHeight(22);
    m_scriptsCombo->setMinimumWidth(80);
    m_scriptsCombo->setStyleSheet(
        "QComboBox { background: #333; color: #4EC9B0; border: 1px solid #555;"
        "  border-radius: 2px; padding: 0 4px; font-size: 10px; font-weight: bold; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #2d2d2d; color: #ccc;"
        "  selection-background-color: #0055aa; font-size: 10px; }");
    setupScriptsMenu();
    connect(m_scriptsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScriptConsoleWidget::onScriptSelected);
    tbLayout->addWidget(m_scriptsCombo);

    // Refresh scripts button
    m_refreshScriptsBtn = new QPushButton(toolbar);
    m_refreshScriptsBtn->setFixedSize(22, 22);
    m_refreshScriptsBtn->setToolTip(tr("Refresh Scripts List"));
    m_refreshScriptsBtn->setStyleSheet(
        "QPushButton { background: #333; border: 1px solid #555; border-radius: 2px; padding: 2px; }"
        "QPushButton:hover { background: #444; border: 1px solid #4EC9B0; }");

    // Locate refresh icon
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    const QStringList iconPaths = {
        dir.filePath("images/refresh.svg"),
        QDir::cleanPath(dir.absoluteFilePath("../src/images/refresh.svg")),
        QDir::cleanPath(dir.absoluteFilePath("../../src/images/refresh.svg")),
        dir.filePath("refresh.svg")
    };
    for (const QString& path : iconPaths) {
        if (QFile::exists(path)) {
            m_refreshScriptsBtn->setIcon(QIcon(path));
            break;
        }
    }
    connect(m_refreshScriptsBtn, &QPushButton::clicked, this, &ScriptConsoleWidget::setupScriptsMenu);
    tbLayout->addWidget(m_refreshScriptsBtn);

    // Template selector
    m_templateCombo = new QComboBox(toolbar);
    m_templateCombo->setFixedHeight(22);
    m_templateCombo->setMinimumWidth(80);
    m_templateCombo->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555;"
        "  border-radius: 2px; padding: 0 4px; font-size: 10px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #2d2d2d; color: #ccc;"
        "  selection-background-color: #0055aa; font-size: 10px; }");
    setupTemplates();
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScriptConsoleWidget::onTemplateSelected);
    tbLayout->addWidget(m_templateCombo);

    tbLayout->addStretch();

    // Load button
    m_loadBtn = new QPushButton(tr("Open"), toolbar);
    m_loadBtn->setToolTip(tr("Load Script File"));
    m_loadBtn->setFixedHeight(22);
    m_loadBtn->setStyleSheet(
        "QPushButton { background: transparent; border: 1px solid #444; color: #ccc; padding: 0 8px; font-size: 10px; }"
        "QPushButton:hover { background: #444; }");
    connect(m_loadBtn, &QPushButton::clicked, this, &ScriptConsoleWidget::onLoadScript);
    tbLayout->addWidget(m_loadBtn);

    // Save button
    m_saveBtn = new QPushButton(tr("Save"), toolbar);
    m_saveBtn->setToolTip(tr("Save Script File"));
    m_saveBtn->setFixedHeight(22);
    m_saveBtn->setStyleSheet(m_loadBtn->styleSheet());
    connect(m_saveBtn, &QPushButton::clicked, this, &ScriptConsoleWidget::onSaveScript);
    tbLayout->addWidget(m_saveBtn);

    // Separator
    auto* sep = new QWidget(toolbar);
    sep->setFixedWidth(1);
    sep->setStyleSheet("background: #555;");
    tbLayout->addWidget(sep);

    // Clear button
    m_clearBtn = new QPushButton(tr("Clear"), toolbar);
    m_clearBtn->setToolTip(tr("Clear Output"));
    m_clearBtn->setFixedHeight(22);
    m_clearBtn->setStyleSheet(m_loadBtn->styleSheet());
    connect(m_clearBtn, &QPushButton::clicked, this, &ScriptConsoleWidget::onClearOutput);
    tbLayout->addWidget(m_clearBtn);

    // Stop button
    m_stopBtn = new QPushButton(tr("Stop"), toolbar);
    m_stopBtn->setToolTip(tr("Stop Script"));
    m_stopBtn->setFixedHeight(22);
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(
        "QPushButton { background: transparent; border: 1px solid #444; color: #888; padding: 0 8px; font-size: 10px; }"
        "QPushButton:enabled { color: #ff6666; border-color: #ff6666; }"
        "QPushButton:enabled:hover { background: #442222; }");
    connect(m_stopBtn, &QPushButton::clicked, this, &ScriptConsoleWidget::onStopScript);
    tbLayout->addWidget(m_stopBtn);

    // Run button
    m_runBtn = new QPushButton(tr("Run"), toolbar);
    m_runBtn->setToolTip(tr("Execute Script (Ctrl+Enter)"));
    m_runBtn->setFixedHeight(22);
    m_runBtn->setStyleSheet(
        "QPushButton { background: #0e6429; color: #fff; border: none;"
        "  border-radius: 2px; padding: 0 8px; font-size: 10px; font-weight: bold; }"
        "QPushButton:hover { background: #1a8038; }"
        "QPushButton:disabled { background: #333; color: #666; }");
    connect(m_runBtn, &QPushButton::clicked, this, &ScriptConsoleWidget::onRunScript);
    tbLayout->addWidget(m_runBtn);

    layout->addWidget(toolbar);
}

void ScriptConsoleWidget::setupEditor(QSplitter* splitter)
{
    m_editor = new QPlainTextEdit(splitter);
    m_editor->setPlaceholderText(
        tr("// Write your TStar script here...\n"
           "// Select a template from the dropdown to get started.\n\n"
           "var view = App.activeWindow();\n"
           "Console.log(\"Hello from TStar!\");"));

    m_editor->setStyleSheet(
        "QPlainTextEdit {"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 11px;"
        "  border: none;"
        "  selection-background-color: #264f78;"
        "  selection-color: #ffffff;"
        "}"
    );

    m_editor->setTabStopDistance(
        QFontMetricsF(m_editor->font()).horizontalAdvance(' ') * 4);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);

    // Attach syntax highlighter
    new JSSyntaxHighlighter(m_editor->document());

    splitter->addWidget(m_editor);
}

void ScriptConsoleWidget::setupOutputLog(QSplitter* splitter)
{
    m_outputLog = new QTextEdit(splitter);
    m_outputLog->setReadOnly(true);
    m_outputLog->setStyleSheet(
        "QTextEdit {"
        "  background-color: #1a1a1a;"
        "  color: #cccccc;"
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 10px;"
        "  border: none;"
        "  border-top: 1px solid #333;"
        "}"
    );

    // Show a welcome/help message
    m_outputLog->setHtml(
        "<span style='color:#569CD6'>TStar JavaScript Console</span><br>"
        "<span style='color:#666'>Type or paste a script above, then click ▶ Run.</span><br>"
        "<span style='color:#666'>See the API Reference tab for available commands.</span><br>"
    );

    splitter->addWidget(m_outputLog);
}

void ScriptConsoleWidget::setupApiReference(QTabWidget* tabs)
{
    auto* refPage = new QWidget();
    auto* refLayout = new QVBoxLayout(refPage);
    refLayout->setContentsMargins(0, 0, 0, 0);
    refLayout->setSpacing(0);

    // Header
    auto* header = new QLabel(tr("TStar Scripting API Reference"), refPage);
    header->setStyleSheet(
        "color: #569CD6; font-size: 11px; font-weight: bold;"
        "padding: 6px 8px; background: #252525;"
        "border-bottom: 1px solid #333;");
    refLayout->addWidget(header);

    // Tree widget for API entries
    m_apiTree = new QTreeWidget(refPage);
    m_apiTree->setHeaderHidden(true);
    m_apiTree->setAnimated(true);
    m_apiTree->setIndentation(16);
    m_apiTree->setStyleSheet(
        "QTreeWidget {"
        "  background: #1e1e1e; color: #cccccc; border: none;"
        "  font-size: 10px;"
        "}"
        "QTreeWidget::item {"
        "  padding: 2px 4px;"
        "}"
        "QTreeWidget::item:selected {"
        "  background: #264f78;"
        "}"
        "QTreeWidget::item:hover {"
        "  background: #2a2d2e;"
        "}"
        "QTreeWidget::branch {"
        "  background: #1e1e1e;"
        "}"
    );

    // Double-click on an example to insert it into the editor
    connect(m_apiTree, &QTreeWidget::itemDoubleClicked,
            [this](QTreeWidgetItem* item, int) {
        if (item->data(0, Qt::UserRole).toString() == "example") {
            QString code = item->data(0, Qt::UserRole + 1).toString();
            if (!code.isEmpty() && m_editor) {
                m_editor->appendPlainText("\n" + code + "\n");
            }
        }
    });

    refLayout->addWidget(m_apiTree);

    // Instruction footer
    auto* footer = new QLabel(
        tr("Double-click an example to insert it into the editor."),
        refPage);
    footer->setStyleSheet(
        "color: #888; font-size: 9px; padding: 4px 8px;"
        "background: #252525; border-top: 1px solid #333;");
    refLayout->addWidget(footer);

    tabs->addTab(refPage, tr("API Reference"));
}

void ScriptConsoleWidget::populateApiReference()
{
    if (!m_apiTree) return;
    m_apiTree->clear();

    QVariantList apiRef = Scripting::JSRuntime::buildApiReference();

    for (const QVariant& entry : apiRef) {
        QVariantMap obj = entry.toMap();

        // Top-level item: object name
        auto* objItem = new QTreeWidgetItem(m_apiTree);
        QString typeTag = obj["type"].toString();
        QString prefix;
        if (typeTag == "global")  prefix = "[Global] ";
        else if (typeTag == "process") prefix = "[Process] ";
        else if (typeTag == "class")   prefix = "[Class] ";

        objItem->setText(0, prefix + obj["name"].toString());
        objItem->setForeground(0, QColor("#4EC9B0"));
        QFont boldFont = objItem->font(0);
        boldFont.setBold(true);
        boldFont.setPointSize(boldFont.pointSize() + 1);
        objItem->setFont(0, boldFont);

        // Description
        if (obj.contains("description")) {
            auto* descItem = new QTreeWidgetItem(objItem);
            descItem->setText(0, obj["description"].toString());
            descItem->setForeground(0, QColor("#888888"));
            QFont italicFont = descItem->font(0);
            italicFont.setItalic(true);
            descItem->setFont(0, italicFont);
        }

        // Properties
        QVariantList props = obj["properties"].toList();
        if (!props.isEmpty()) {
            auto* propsHeader = new QTreeWidgetItem(objItem);
            propsHeader->setText(0, "Properties");
            propsHeader->setForeground(0, QColor("#CE9178"));
            QFont headerFont = propsHeader->font(0);
            headerFont.setBold(true);
            propsHeader->setFont(0, headerFont);

            for (const QVariant& prop : props) {
                QVariantMap p = prop.toMap();
                auto* propItem = new QTreeWidgetItem(propsHeader);
                propItem->setText(0, QString("%1  —  %2")
                    .arg(p["name"].toString(), p["desc"].toString()));
                propItem->setForeground(0, QColor("#9CDCFE"));
            }
        }

        // Methods
        QVariantList methods = obj["methods"].toList();
        if (!methods.isEmpty()) {
            auto* methodsHeader = new QTreeWidgetItem(objItem);
            methodsHeader->setText(0, "Methods");
            methodsHeader->setForeground(0, QColor("#CE9178"));
            QFont headerFont = methodsHeader->font(0);
            headerFont.setBold(true);
            methodsHeader->setFont(0, headerFont);

            for (const QVariant& method : methods) {
                QVariantMap m = method.toMap();
                auto* methItem = new QTreeWidgetItem(methodsHeader);
                methItem->setText(0, QString("%1  —  %2")
                    .arg(m["name"].toString(), m["desc"].toString()));
                methItem->setForeground(0, QColor("#DCDCAA"));
            }
        }

        // Example (double-click to insert)
        if (obj.contains("example")) {
            auto* exItem = new QTreeWidgetItem(objItem);
            exItem->setText(0, "Example (double-click to insert)");
            exItem->setForeground(0, QColor("#6A9955"));
            exItem->setData(0, Qt::UserRole, "example");
            exItem->setData(0, Qt::UserRole + 1, obj["example"].toString());
            QFont exFont = exItem->font(0);
            exFont.setItalic(true);
            exItem->setFont(0, exFont);

            // Show the code preview as a child
            auto* codeItem = new QTreeWidgetItem(exItem);
            codeItem->setText(0, obj["example"].toString()
                .replace("\n", "  ↵  "));
            codeItem->setForeground(0, QColor("#808080"));
        }
    }

    m_apiTree->expandAll();
}

void ScriptConsoleWidget::setupTemplates()
{
    m_templateCombo->addItem(tr("Templates..."), QString());

    // Template 1: Basic Processing
    m_templateCombo->addItem(tr("Basic Processing"), QString(
        "// Basic Image Processing Pipeline\n"
        "// Applies stretch + saturation + SCNR to the active image.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n"
        "    Console.log(\"Processing: \" + view.title);\n"
        "    Console.log(\"Size: \" + img.width + \"x\" + img.height);\n\n"
        "    // Auto-stretch\n"
        "    var stretch = new Stretch();\n"
        "    stretch.targetMedian = 0.25;\n"
        "    stretch.linked = true;\n"
        "    stretch.executeOn(img);\n"
        "    Console.log(\"Stretch applied.\");\n\n"
        "    // Boost saturation\n"
        "    var sat = new Saturation();\n"
        "    sat.amount = 1.3;\n"
        "    sat.executeOn(img);\n"
        "    Console.log(\"Saturation boosted.\");\n\n"
        "    // Remove green cast\n"
        "    var scnr = new SCNR();\n"
        "    scnr.amount = 0.5;\n"
        "    scnr.executeOn(img);\n"
        "    Console.log(\"SCNR applied.\");\n\n"
        "    view.refresh();\n"
        "    Console.log(\"Done!\");\n"
        "}\n"
    ));

    // Template 2: Curves Adjustment
    m_templateCombo->addItem(tr("Curves Adjustment"), QString(
        "// Curves Adjustment\n"
        "// Apply a custom tone curve to the active image.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n\n"
        "    var curves = new Curves();\n"
        "    curves.points = [\n"
        "        [0.0, 0.0],    // Black point\n"
        "        [0.15, 0.10],  // Shadows (darken slightly)\n"
        "        [0.50, 0.55],  // Midtones (brighten slightly)\n"
        "        [0.85, 0.92],  // Highlights (boost)\n"
        "        [1.0, 1.0]     // White point\n"
        "    ];\n"
        "    curves.red = true;\n"
        "    curves.green = true;\n"
        "    curves.blue = true;\n\n"
        "    curves.executeOn(img);\n"
        "    view.refresh();\n"
        "    Console.log(\"Curves applied!\");\n"
        "}\n"
    ));

    // Template 3: GHS Stretch
    m_templateCombo->addItem(tr("GHS Stretch"), QString(
        "// Generalized Hyperbolic Stretch\n"
        "// Non-linear stretch with fine control over shadows/highlights.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n\n"
        "    var ghs = new GHS();\n"
        "    ghs.D = 5.0;    // Stretch factor\n"
        "    ghs.B = 0.25;   // Local intensity\n"
        "    ghs.SP = 0.15;  // Symmetry point\n"
        "    ghs.LP = 0.0;   // Shadow protection\n"
        "    ghs.HP = 1.0;   // Highlight protection\n"
        "    ghs.mode = 0;   // Generalized Hyperbolic\n\n"
        "    ghs.executeOn(img);\n"
        "    view.refresh();\n"
        "    Console.log(\"GHS stretch applied!\");\n"
        "}\n"
    ));

    // Template 4: PixelMath (Channel Arithmetic)
    m_templateCombo->addItem(tr("PixelMath: Channel Mix"), QString(
        "// PixelMath - Channel Arithmetic\n"
        "// Combine channels or apply math expressions to pixels.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n"
        "    var pm = new PixelMath();\n\n"
        "    // Example: Average of RGB channels (Grayscale)\n"
        "    pm.expression = \"(r + g + b) / 3\";\n"
        "    pm.rescale = true;\n\n"
        "    pm.executeOn(img);\n"
        "    view.refresh();\n"
        "    Console.log(\"PixelMath applied!\");\n"
        "}\n"
    ));

    // Template 5: PixelMath (Multi-Image)
    m_templateCombo->addItem(tr("PixelMath: Image Reference"), QString(
        "// PixelMath - Cross-Image Operation\n"
        "// Reference another image window using variable IDs.\n\n"
        "var views = App.windows();\n"
        "if (views.length < 2) {\n"
        "    Console.error(\"This script requires at least two open images.\");\n"
        "} else {\n"
        "    var target = views[0]; // First window\n"
        "    var source = views[1]; // Second window\n\n"
        "    var pm = new PixelMath();\n"
        "    pm.setReference(\"I1\", source.image);\n"
        "    pm.expression = \"(r + I1.r) / 2\"; // Average target with source\n\n"
        "    pm.executeOn(target.image);\n"
        "    target.refresh();\n"
        "    Console.log(\"Merged \" + source.title + \" into \" + target.title);\n"
        "}\n"
    ));

    // Template 6: Arcsinh Stretch
    m_templateCombo->addItem(tr("Arcsinh Stretch"), QString(
        "// Color-Preserving Stretch\n"
        "// Uses Arcsinh to boost signal without saturating colors.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n"
        "    var as = new ArcsinhStretch();\n\n"
        "    as.stretchFactor = 100.0;\n"
        "    as.blackPoint = 0.005;\n"
        "    as.humanLuminance = true;\n\n"
        "    as.executeOn(img);\n"
        "    view.refresh();\n"
        "    Console.log(\"Arcsinh stretch applied!\");\n"
        "}\n"
    ));

    // Template 7: Histogram Transformation
    m_templateCombo->addItem(tr("Histogram Stretch"), QString(
        "// Manual Histogram Transformation\n"
        "// Apply MTF-based stretching with shadows/midtones control.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n"
        "    var hs = new HistogramStretch();\n\n"
        "    hs.midtones = 0.05;  // Strong stretch\n"
        "    hs.shadows  = 0.001; // Slight black clipping\n\n"
        "    hs.executeOn(img);\n"
        "    view.refresh();\n"
        "    Console.log(\"Histogram stretch applied!\");\n"
        "}\n"
    ));

    // Template 8: Star Processing
    m_templateCombo->addItem(tr("Star Expansion"), QString(
        "// Dedicated Star Tool\n"
        "// Enhance star presence and color.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n"
        "    var ss = new StarStretch();\n\n"
        "    ss.stretchAmount = 3.5;\n"
        "    ss.colorBoost    = 1.15;\n"
        "    ss.scnr          = true; // Remove green halos\n\n"
        "    ss.executeOn(img);\n"
        "    view.refresh();\n"
        "    Console.log(\"Star expansion complete!\");\n"
        "}\n"
    ));

    // Template 9: Utility Tools (Correction)
    m_templateCombo->addItem(tr("Fix Highlights & Color"), QString(
        "// Magenta Halo Removal & WB Correction\n"
        "// Clean up artifacts and adjust temperature.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n\n"
        "    // 1. Remove magenta stars\n"
        "    var mc = new MagentaCorrection();\n"
        "    mc.amount = 0.8;\n"
        "    mc.executeOn(img);\n\n"
        "    // 2. Adjust Temperature (Warm it up)\n"
        "    var tt = new TemperatureTint();\n"
        "    tt.temperature = 15;\n"
        "    tt.executeOn(img);\n\n"
        "    view.refresh();\n"
        "    Console.log(\"Corrections applied!\");\n"
        "}\n"
    ));

    // Template 10: Image Info
    m_templateCombo->addItem(tr("Image Info"), QString(
        "// Image Information\n"
        "// Display statistics about the active image.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) {\n"
        "    Console.error(\"No image open!\");\n"
        "} else {\n"
        "    var img = view.image;\n"
        "    Console.log(\"=== Image Info ===\");\n"
        "    Console.log(\"Title:    \" + view.title);\n"
        "    Console.log(\"Size:     \" + img.width + \" x \" + img.height);\n"
        "    Console.log(\"Channels: \" + img.channels);\n"
        "    Console.log(\"File:     \" + img.filePath());\n"
        "    Console.log(\"Object:   \" + img.objectName());\n\n"
        "    var stats = img.getStatistics();\n"
        "    Console.log(\"\\n=== Statistics ===\");\n"
        "    Console.log(\"Median: \" + JSON.stringify(stats.median));\n"
        "    Console.log(\"Min:    \" + JSON.stringify(stats.min));\n"
        "    Console.log(\"Max:    \" + JSON.stringify(stats.max));\n"
        "    Console.log(\"Mean:   \" + JSON.stringify(stats.mean));\n\n"
        "    var header = img.getHeaderValue(\"OBJECT\");\n"
        "    if (header) Console.log(\"FITS OBJECT: \" + header);\n"
        "}\n"
    ));

    // Template 5: Batch Processing
    m_templateCombo->addItem(tr("Batch All Windows"), QString(
        "// Batch Processing\n"
        "// Apply the same process to all open image windows.\n\n"
        "var views = App.windows();\n"
        "Console.log(\"Processing \" + views.length + \" images...\");\n\n"
        "for (var i = 0; i < views.length; i++) {\n"
        "    var view = views[i];\n"
        "    var img = view.image;\n\n"
        "    Console.log(\"[\" + (i+1) + \"/\" + views.length + \"] \" + view.title);\n\n"
        "    // Apply your processing here:\n"
        "    var sat = new Saturation();\n"
        "    sat.amount = 1.2;\n"
        "    sat.executeOn(img);\n\n"
        "    view.refresh();\n"
        "}\n\n"
        "Console.log(\"Batch processing complete!\");\n"
    ));

    // Template: AI Toolkit
    m_templateCombo->addItem(tr("AI: Star Removal & Denoise"), QString(
        "// AI Image Processing\n"
        "// Extracts background, removes noise, and separates stars.\n\n"
        "var view = App.activeWindow();\n"
        "if (!view) { Console.error(\"No image open!\"); }\n"
        "else {\n"
        "    var img = view.image;\n\n"
        "    // 1. Denoise\n"
        "    Console.log(\"Running Cosmic Clarity...\");\n"
        "    var cc = new CosmicClarity();\n"
        "    cc.mode = 1; // Denoise\n"
        "    cc.executeOn(img);\n"
        "    view.refresh();\n\n"
        "    // 2. Extract Background\n"
        "    Console.log(\"Running GraXpert...\");\n"
        "    var gx = new GraXpert();\n"
        "    gx.operation = 0; // Extraction\n"
        "    gx.tolerance = 1.5;\n"
        "    gx.executeOn(img);\n"
        "    view.refresh();\n\n"
        "    // 3. Remove Stars\n"
        "    Console.log(\"Running StarNet...\");\n"
        "    var sn = new StarNet();\n"
        "    sn.executeOn(img);\n"
        "    view.refresh();\n\n"
        "    Console.log(\"AI processing completed!\");\n"
        "}\n"
    ));
}

void ScriptConsoleWidget::setupScriptsMenu()
{
    if (!m_scriptsCombo) return;

    m_scriptsCombo->clear();
    m_scriptsCombo->addItem(tr("Scripts..."), QString());

    // Locate JS-Scripts directory
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);

    const QStringList searchPaths = {
        dir.filePath("JS-Scripts"),
        QDir::cleanPath(dir.absoluteFilePath("../Resources/JS-Scripts")),
        QDir::cleanPath(dir.absoluteFilePath("../JS-Scripts")), // Development environment
        QDir::cleanPath(dir.absoluteFilePath("../../JS-Scripts"))
    };

    QString scriptsPath;
    for (const QString& path : searchPaths) {
        if (QDir(path).exists()) {
            scriptsPath = path;
            break;
        }
    }

    if (scriptsPath.isEmpty()) {
        qDebug() << "ScriptConsole: JS-Scripts folder not found in" << searchPaths;
        return;
    }

    QDir scriptsDir(scriptsPath);
    scriptsDir.setNameFilters({"*.js"});
    scriptsDir.setSorting(QDir::Name);

    QFileInfoList entries = scriptsDir.entryInfoList();
    for (const QFileInfo& fi : entries) {
        m_scriptsCombo->addItem(fi.fileName(), fi.absoluteFilePath());
    }
}

// =============================================================================
// Slot Implementations
// =============================================================================

void ScriptConsoleWidget::onRunScript()
{
    if (!m_runtime) {
        appendLog("Error: Script runtime not initialized.", "#ff6666");
        return;
    }

    QString script = m_editor->toPlainText().trimmed();
    if (script.isEmpty()) {
        appendLog("No script to run.", "#ffaa00");
        return;
    }

    // Clear previous output
    m_outputLog->clear();
    appendLog(QString("Running script... [%1]")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss")),
        "#569CD6");

    m_runtime->evaluateAsync(script, m_currentFile.isEmpty() ? "console" : m_currentFile);
}

void ScriptConsoleWidget::onStopScript()
{
    if (m_runtime) {
        m_runtime->cancelExecution();
        appendLog("Cancellation requested.", "#ffaa00");
    }
}

void ScriptConsoleWidget::onClearOutput()
{
    if (m_outputLog) {
        m_outputLog->clear();
    }
}

void ScriptConsoleWidget::onLoadScript()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("Load JavaScript"),
        QString(),
        tr("JavaScript Files (*.js);;All Files (*)"));

    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLog("Failed to open: " + path, "#ff6666");
        return;
    }

    QTextStream in(&file);
    m_editor->setPlainText(in.readAll());
    file.close();

    m_currentFile = path;
    m_statusLabel->setText(QFileInfo(path).fileName());
    appendLog("Loaded: " + path, "#569CD6");
}

void ScriptConsoleWidget::onSaveScript()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save JavaScript"),
        m_currentFile.isEmpty() ? "script.js" : m_currentFile,
        tr("JavaScript Files (*.js)"));

    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendLog("Failed to save: " + path, "#ff6666");
        return;
    }

    QTextStream out(&file);
    out << m_editor->toPlainText();
    file.close();

    m_currentFile = path;
    m_statusLabel->setText(QFileInfo(path).fileName());
    appendLog("Saved: " + path, "#569CD6");
}

void ScriptConsoleWidget::onTemplateSelected(int index)
{
    if (index <= 0) return; // Skip the "Templates..." placeholder

    QString code = m_templateCombo->itemData(index).toString();
    if (!code.isEmpty()) {
        m_editor->setPlainText(code);
    }

    // Reset to placeholder
    m_templateCombo->setCurrentIndex(0);
}

void ScriptConsoleWidget::onScriptSelected(int index)
{
    if (index <= 0) return;

    QString path = m_scriptsCombo->itemData(index).toString();
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLog("Failed to open script: " + path, "#ff6666");
        return;
    }

    QTextStream in(&file);
    m_editor->setPlainText(in.readAll());
    file.close();

    m_currentFile = path;
    m_statusLabel->setText(QFileInfo(path).fileName());
    appendLog("Loaded script: " + path, "#569CD6");

    // Reset to placeholder
    m_scriptsCombo->setCurrentIndex(0);
}

void ScriptConsoleWidget::onStandardOutput(const QString& message)
{
    appendLog(message, "#cccccc");
}

void ScriptConsoleWidget::onStandardError(const QString& message)
{
    appendLog(message, "#ff6666");
}

void ScriptConsoleWidget::onScriptStarted()
{
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_statusLabel->setText(tr("Running..."));
    m_statusLabel->setStyleSheet(
        "color: #4EC9B0; font-size: 10px; padding: 2px 6px;"
        "background: #252525; border-top: 1px solid #333;");
}

void ScriptConsoleWidget::onScriptFinished(bool success)
{
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);

    if (success) {
        appendLog(QString("Script finished successfully. [%1]")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss")),
            "#4EC9B0");
        m_statusLabel->setText(tr("Completed"));
        m_statusLabel->setStyleSheet(
            "color: #4EC9B0; font-size: 10px; padding: 2px 6px;"
            "background: #252525; border-top: 1px solid #333;");
    } else {
        appendLog(QString("Script failed. [%1]")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss")),
            "#ff6666");
        m_statusLabel->setText(tr("Failed"));
        m_statusLabel->setStyleSheet(
            "color: #ff6666; font-size: 10px; padding: 2px 6px;"
            "background: #252525; border-top: 1px solid #333;");
    }
}

// =============================================================================
// Helpers
// =============================================================================

void ScriptConsoleWidget::appendLog(const QString& message, const QString& color)
{
    if (!m_outputLog) return;

    m_outputLog->append(
        QString("<span style='color:%1; font-family: Consolas, monospace; font-size: 10px;'>%2</span>")
        .arg(color, message.toHtmlEscaped()));

    // Auto-scroll to bottom
    QScrollBar* sb = m_outputLog->verticalScrollBar();
    sb->setValue(sb->maximum());
}
