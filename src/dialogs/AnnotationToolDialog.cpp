#include "AnnotationToolDialog.h"
#include "../widgets/AnnotationOverlay.h"
#include "../ImageViewer.h"
#include "../MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QCoreApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QDebug>
#include <QShortcut>
#include <QKeySequence>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <cmath>
#include <QRegularExpression>
#include <QSet>
#include <QFileInfo>
#include <QMap>

AnnotationToolDialog::AnnotationToolDialog(MainWindow* parent)
    : QDialog(parent)
    , m_mainWindow(parent)
{
    setWindowTitle(tr("Annotation Tool"));
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    setupUI();
    

}

AnnotationToolDialog::~AnnotationToolDialog() {
}

void AnnotationToolDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // Drawing Tools Group
    auto* toolsGroup = new QGroupBox(tr("Drawing Tools"));
    auto* toolsLayout = new QHBoxLayout(toolsGroup);
    
    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);
    
    // Use text buttons instead of emoji for consistent sizing
    m_selectBtn = new QToolButton();
    m_selectBtn->setText(tr("Select"));
    m_selectBtn->setToolTip(tr("Select/Pan (no drawing)"));
    m_selectBtn->setCheckable(true);
    m_selectBtn->setChecked(true);
    m_selectBtn->setMinimumWidth(50);
    m_toolGroup->addButton(m_selectBtn, 0);
    
    m_circleBtn = new QToolButton();
    m_circleBtn->setText(tr("Circle"));
    m_circleBtn->setToolTip(tr("Draw Circle"));
    m_circleBtn->setCheckable(true);
    m_circleBtn->setMinimumWidth(50);
    m_toolGroup->addButton(m_circleBtn, 1);
    
    m_rectBtn = new QToolButton();
    m_rectBtn->setText(tr("Rect"));
    m_rectBtn->setToolTip(tr("Draw Rectangle"));
    m_rectBtn->setCheckable(true);
    m_rectBtn->setMinimumWidth(50);
    m_toolGroup->addButton(m_rectBtn, 2);
    
    m_arrowBtn = new QToolButton();
    m_arrowBtn->setText(tr("Arrow"));
    m_arrowBtn->setToolTip(tr("Draw Arrow"));
    m_arrowBtn->setCheckable(true);
    m_arrowBtn->setMinimumWidth(50);
    m_toolGroup->addButton(m_arrowBtn, 3);
    
    m_textBtn = new QToolButton();
    m_textBtn->setText(tr("Text"));
    m_textBtn->setToolTip(tr("Add Text Label"));
    m_textBtn->setCheckable(true);
    m_textBtn->setMinimumWidth(50);
    m_toolGroup->addButton(m_textBtn, 4);
    
    toolsLayout->addWidget(m_selectBtn);
    toolsLayout->addWidget(m_circleBtn);
    toolsLayout->addWidget(m_rectBtn);
    toolsLayout->addWidget(m_arrowBtn);
    toolsLayout->addWidget(m_textBtn);
    toolsLayout->addStretch();
    
    connect(m_toolGroup, &QButtonGroup::idClicked,
            this, &AnnotationToolDialog::onToolSelected);
    
    mainLayout->addWidget(toolsGroup);

    // Color and Undo/Redo
    auto* optionsLayout = new QHBoxLayout();
    optionsLayout->addWidget(new QLabel(tr("Color:")));
    m_colorCombo = new QComboBox();
    m_colorCombo->addItem(tr("Yellow"), QColor(Qt::yellow));
    m_colorCombo->addItem(tr("Red"), QColor(Qt::red));
    m_colorCombo->addItem(tr("Green"), QColor(Qt::green));
    m_colorCombo->addItem(tr("Cyan"), QColor(Qt::cyan));
    m_colorCombo->addItem(tr("White"), QColor(Qt::white));
    connect(m_colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnnotationToolDialog::onColorChanged);
    optionsLayout->addWidget(m_colorCombo);
    
    // Undo/Redo buttons
    m_undoBtn = new QPushButton(tr("Undo"));
    m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QPushButton::clicked, this, &AnnotationToolDialog::onUndo);
    optionsLayout->addWidget(m_undoBtn);
    
    m_redoBtn = new QPushButton(tr("Redo"));
    m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QPushButton::clicked, this, &AnnotationToolDialog::onRedo);
    optionsLayout->addWidget(m_redoBtn);
    
    optionsLayout->addStretch();
    
    m_clearBtn = new QPushButton(tr("Clear All"));
    connect(m_clearBtn, &QPushButton::clicked,
            this, &AnnotationToolDialog::onClearAnnotations);
    optionsLayout->addWidget(m_clearBtn);
    
    mainLayout->addLayout(optionsLayout);


    // Status
    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    // Instruction for saving
    QLabel* tipLabel = new QLabel(tr("Note: Keep this tool OPEN to burn annotations into the saved image (File > Save)."));
    tipLabel->setStyleSheet("color: #AAAAAA; font-style: italic; border-top: 1px solid #444; padding-top: 5px;");
    tipLabel->setWordWrap(true);
    mainLayout->addWidget(tipLabel);
    


    mainLayout->addStretch();
    
    // Keyboard shortcuts (work even when dialog doesn't have focus)
    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    undoShortcut->setContext(Qt::ApplicationShortcut);
    connect(undoShortcut, &QShortcut::activated, this, &AnnotationToolDialog::onUndo);
    
    auto* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    redoShortcut->setContext(Qt::ApplicationShortcut);
    connect(redoShortcut, &QShortcut::activated, this, &AnnotationToolDialog::onRedo);
    
    setMinimumWidth(400);
    adjustSize();
}

void AnnotationToolDialog::setViewer(ImageViewer* viewer) {
    m_viewer = viewer;
    
    if (!viewer) {
        m_statusLabel->setText(tr("No image loaded"));
        return;
    }
    
    // Create overlay if needed
    if (!m_overlay) {
        m_overlay = new AnnotationOverlay(viewer);
        m_overlay->setGeometry(viewer->rect());
        m_overlay->show();
        
        // Connect signal that fires BEFORE annotation is added (for proper undo)
        connect(m_overlay, &AnnotationOverlay::aboutToAddAnnotation,
                this, &AnnotationToolDialog::onAboutToAddAnnotation);
        // Connect for text input request
        connect(m_overlay, &AnnotationOverlay::textInputRequested,
                this, &AnnotationToolDialog::onTextInputRequested);
    }
    

    
    m_statusLabel->setText(tr("Ready to draw"));
}



void AnnotationToolDialog::onToolSelected(int toolId) {
    if (!m_overlay) return;
    
    switch (toolId) {
        case 0: 
            m_overlay->setDrawMode(AnnotationOverlay::DrawMode::None); 
            m_statusLabel->setText(tr("Select mode"));
            break;
        case 1: 
            m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Circle); 
            m_statusLabel->setText(tr("Click and drag to draw circle"));
            break;
        case 2: 
            m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Rectangle); 
            m_statusLabel->setText(tr("Click and drag to draw rectangle"));
            break;
        case 3: 
            m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Arrow); 
            m_statusLabel->setText(tr("Click and drag to draw arrow"));
            break;
        case 4: 
            m_overlay->setDrawMode(AnnotationOverlay::DrawMode::Text);
            // DON'T open dialog here - wait for click, then ask for text
            m_statusLabel->setText(tr("Click on image to add text label"));
            break;
    }
}

void AnnotationToolDialog::onTextInputRequested(const QPointF& imagePos) {
    // Called when user clicks on image in text mode
    bool ok;
    QString text = QInputDialog::getText(this, tr("Text Label"),
                                         tr("Enter text:"), QLineEdit::Normal,
                                         m_pendingText, &ok);
    if (ok && !text.isEmpty()) {
        m_pendingText = text;
        // Save undo state BEFORE adding annotation
        pushUndoState();
        // Tell overlay to place the text at the clicked position
        if (m_overlay) {
            m_overlay->placeTextAt(imagePos, text, m_colorCombo->itemData(m_colorCombo->currentIndex()).value<QColor>());
        }
        m_statusLabel->setText(tr("Text added. Click again to add more."));
    }
}

void AnnotationToolDialog::renderAnnotations(QPainter& painter, const QRectF& imageRect) {
    if (m_overlay) {
        m_overlay->renderToPainter(painter, imageRect);
    }
}

void AnnotationToolDialog::onClearAnnotations() {
    if (m_overlay) {
        // Save current state for undo
        pushUndoState();
        m_overlay->clearManualAnnotations();
    }
}

void AnnotationToolDialog::onColorChanged(int index) {
    if (m_overlay) {
        QColor color = m_colorCombo->itemData(index).value<QColor>();
        m_overlay->setDrawColor(color);
    }
}

void AnnotationToolDialog::onAboutToAddAnnotation() {
    // Called BEFORE annotation is added - save state for undo
    pushUndoState();
}

void AnnotationToolDialog::onUndo() {
    if (m_undoStack.isEmpty() || !m_overlay) return;
    
    // Save current state to redo stack
    m_redoStack.push(m_overlay->annotations());
    
    // Restore previous state
    QVector<Annotation> prevState = m_undoStack.pop();
    m_overlay->setAnnotations(prevState);
    
    updateUndoRedoButtons();
}

void AnnotationToolDialog::onRedo() {
    if (m_redoStack.isEmpty() || !m_overlay) return;
    
    // Save current state to undo stack
    m_undoStack.push(m_overlay->annotations());
    
    // Restore redo state
    QVector<Annotation> redoState = m_redoStack.pop();
    m_overlay->setAnnotations(redoState);
    
    updateUndoRedoButtons();
}

void AnnotationToolDialog::pushUndoState() {
    if (!m_overlay) return;
    
    m_undoStack.push(m_overlay->annotations());
    m_redoStack.clear();  // Clear redo on new action
    
    // Limit stack size
    while (m_undoStack.size() > 20) {
        m_undoStack.removeFirst();
    }
    
    updateUndoRedoButtons();
}

void AnnotationToolDialog::updateUndoRedoButtons() {
    m_undoBtn->setEnabled(!m_undoStack.isEmpty());
    m_redoBtn->setEnabled(!m_redoStack.isEmpty());
}
