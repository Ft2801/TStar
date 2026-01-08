#include "AnnotationToolDialog.h"
#include "../widgets/AnnotationOverlay.h"
#include "../ImageViewer.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
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
#include <QShowEvent>
#include <QHideEvent>
#include <cmath>
#include <QRegularExpression>
#include <QSet>
#include <QFileInfo>
#include <QMap>
#include <QDateTime>

// Helper function to log messages to file
static void logToFile(const QString& msg) {
    QFile logFile(QDir::homePath() + "/TStar_annotation_debug.log");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " | " << msg << "\n";
        logFile.close();
    }
}

AnnotationToolDialog::AnnotationToolDialog(QWidget* parent)
    : DialogBase(parent, tr("Annotation Tool"))
{
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
    QLabel* tipLabel = new QLabel(tr("Tip: Annotations are saved as overlay. If you close the tool, annotations will disappear, and then reappear when you open this tool again. Open this tool to continue editing with full undo/redo support. To burn annotations into the image, use File > Save while the tool is open."));
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
    
    logToFile("[AnnotationToolDialog::CONSTRUCTOR] Dialog created");

}

AnnotationToolDialog::~AnnotationToolDialog() {
    logToFile(QString("[AnnotationToolDialog::DESTRUCTOR] Dialog destroyed. SavedAnnotations size: %1").arg(m_savedAnnotations.size()));
    // Cleanup happens in hideEvent when dialog is hidden
    // No need to save state here since hideEvent already saved it
}

void AnnotationToolDialog::setViewer(ImageViewer* viewer) {
    logToFile(QString("[setViewer] Called with viewer=%1, current m_overlay=%2").arg(viewer ? "valid" : "null").arg(m_overlay ? "exists" : "null"));
    
    m_viewer = viewer;
    
    if (!viewer) {
        m_statusLabel->setText(tr("No image loaded"));
        return;
    }
    
    // CRITICAL: Check if there's an old overlay already attached to viewer from previous dialog instance
    // Since AnnotationOverlay is a child of ImageViewer (not the dialog), it survives dialog destruction!
    AnnotationOverlay* oldOverlay = nullptr;
    for (QObject* child : viewer->children()) {
        oldOverlay = qobject_cast<AnnotationOverlay*>(child);
        if (oldOverlay) {
            logToFile("[setViewer] Found existing overlay from previous dialog instance. Destroying it.");
            // Save its annotations before deleting
            m_savedAnnotations = oldOverlay->annotations();
            delete oldOverlay;
            oldOverlay = nullptr;
            break;
        }
    }
    
    // Create overlay if needed
    if (!m_overlay) {
        logToFile("[setViewer] Creating new overlay");
        logToFile(QString("[setViewer] m_savedAnnotations size at creation: %1").arg(m_savedAnnotations.size()));
        
        m_overlay = new AnnotationOverlay(viewer);
        // Make overlay fill entire viewer area and auto-resize
        m_overlay->setGeometry(0, 0, viewer->width(), viewer->height());
        m_overlay->show();
        
        // CRITICAL: Restore saved annotations from MainWindow if they exist
        // Find MainWindow by walking up the parent hierarchy
        QWidget* w = parentWidget();
        MainWindow* mainWin = nullptr;
        int depth = 0;
        while (w && !mainWin && depth < 10) {
            mainWin = qobject_cast<MainWindow*>(w);
            if (!mainWin) {
                w = w->parentWidget();
            }
            depth++;
        }
        
        if (mainWin && !mainWin->m_persistedAnnotations.isEmpty()) {
            logToFile(QString("[setViewer] Restoring %1 annotations from MainWindow::m_persistedAnnotations").arg(mainWin->m_persistedAnnotations.size()));
            m_overlay->setAnnotations(mainWin->m_persistedAnnotations);
            m_savedAnnotations = mainWin->m_persistedAnnotations;
            m_savedUndoStack = mainWin->m_persistedUndoStack;
            m_savedRedoStack = mainWin->m_persistedRedoStack;
            logToFile(QString("[setViewer] Restored undo stack size: %1").arg(m_savedUndoStack.size()));
            logToFile(QString("[setViewer] Restored redo stack size: %1").arg(m_savedRedoStack.size()));
        } else if (!m_savedAnnotations.isEmpty()) {
            logToFile(QString("[setViewer] Restoring %1 saved annotations from m_savedAnnotations").arg(m_savedAnnotations.size()));
            m_overlay->setAnnotations(m_savedAnnotations);
        } else {
            logToFile("[setViewer] No annotations to restore");
        }
        
        // Resize overlay when viewer is resized
        connect(viewer, &ImageViewer::resized, this, [this]() {
            if (m_overlay && m_viewer) {
                m_overlay->setGeometry(0, 0, m_viewer->width(), m_viewer->height());
            }
        });
        
        // Connect signal that fires BEFORE annotation is added (for proper undo)
        connect(m_overlay, &AnnotationOverlay::aboutToAddAnnotation,
                this, &AnnotationToolDialog::onAboutToAddAnnotation);
        // Connect for text input request
        connect(m_overlay, &AnnotationOverlay::textInputRequested,
                this, &AnnotationToolDialog::onTextInputRequested);
    } else {
        // Overlay already exists - just show it
        logToFile(QString("[setViewer] Overlay already exists. Showing it. Current annotations: %1").arg(m_overlay->annotations().size()));
        m_overlay->show();
    }
    
    // Sync the draw mode from last state
    syncOverlayDrawMode();
    
    m_statusLabel->setText(tr("Ready to draw"));
}

QVector<Annotation> AnnotationToolDialog::saveAnnotations() const {
    if (m_overlay) {
        return m_overlay->annotations();
    }
    return QVector<Annotation>();
}

void AnnotationToolDialog::restoreAnnotations(const QVector<Annotation>& annotations) {
    if (m_overlay) {
        m_overlay->setAnnotations(annotations);
    }
}

void AnnotationToolDialog::syncOverlayDrawMode() {
    if (!m_overlay || !m_toolGroup) return;
    
    int checkedId = m_toolGroup->checkedId();
    if (checkedId == -1) {
        // If no button is checked, check the Select button (id=0)
        m_selectBtn->setChecked(true);
        checkedId = 0;
    }
    
    // Apply the checked button's mode to overlay
    onToolSelected(checkedId);
}

void AnnotationToolDialog::saveUndoRedoState() {
    // Save the current undo/redo stacks for later restoration
    m_savedUndoStack = m_undoStack;
    m_savedRedoStack = m_redoStack;
}

void AnnotationToolDialog::restoreUndoRedoState() {
    // Restore the saved undo/redo stacks
    m_undoStack = m_savedUndoStack;
    m_redoStack = m_savedRedoStack;
    updateUndoRedoButtons();
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
    
    logToFile(QString("[onUndo] Current overlay has %1 annotations").arg(m_overlay->annotations().size()));
    logToFile(QString("[onUndo] UndoStack size before pop: %1").arg(m_undoStack.size()));
    logToFile("[onUndo] UndoStack contents (top to bottom):");
    for (int i = m_undoStack.size() - 1; i >= 0; --i) {
        logToFile(QString("  [%1]: %2 annotations").arg(i).arg(m_undoStack[i].size()));
    }
    
    // Save current state to redo stack
    m_redoStack.push(m_overlay->annotations());
    logToFile(QString("[onUndo] Pushed current state to redo: %1 annotations").arg(m_overlay->annotations().size()));
    
    // Restore previous state
    QVector<Annotation> prevState = m_undoStack.pop();
    logToFile(QString("[onUndo] Popped from undo: %1 annotations").arg(prevState.size()));
    
    m_overlay->setAnnotations(prevState);
    logToFile(QString("[onUndo] Overlay now has %1 annotations").arg(m_overlay->annotations().size()));
    
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
    
    QVector<Annotation> currentState = m_overlay->annotations();
    logToFile(QString("[pushUndoState] Pushing undo state with %1 annotations. Stack size before: %2").arg(currentState.size()).arg(m_undoStack.size()));
    
    m_undoStack.push(currentState);
    m_redoStack.clear();  // Clear redo on new action
    
    logToFile(QString("[pushUndoState] Stack size after: %1. Redo cleared.").arg(m_undoStack.size()));
    
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
void AnnotationToolDialog::showEvent(QShowEvent* event) {
    DialogBase::showEvent(event);
    
    logToFile("[showEvent] DIALOG OPENING");
    logToFile(QString("[showEvent] Overlay annotations: %1").arg(m_overlay ? m_overlay->annotations().size() : 0));
    logToFile(QString("[showEvent] Before restore - Undo size: %1 Redo size: %2").arg(m_undoStack.size()).arg(m_redoStack.size()));
    logToFile(QString("[showEvent] Saved stacks - SavedUndo size: %1 SavedRedo size: %2").arg(m_savedUndoStack.size()).arg(m_savedRedoStack.size()));
    
    // Restore the saved undo/redo stacks from before dialog was hidden
    restoreUndoRedoState();
    
    logToFile(QString("[showEvent] After restore - Undo size: %1").arg(m_undoStack.size()));
    logToFile("[showEvent] UndoStack contents (after restore):");
    for (int i = m_undoStack.size() - 1; i >= 0; --i) {
        logToFile(QString("  [%1]: %2 annotations").arg(i).arg(m_undoStack[i].size()));
    }
    
    // If we have a restored stack from a previous session, DON'T add another entry
    // The stack should already contain all the history we need
    if (m_undoStack.isEmpty() && m_overlay && !m_overlay->annotations().isEmpty()) {
        // Only push the current state if we have NO history at all
        QVector<Annotation> currentState = m_overlay->annotations();
        logToFile(QString("[showEvent] Adding initial overlay state to undo stack. Current size: %1").arg(currentState.size()));
        m_undoStack.push(currentState);
        logToFile(QString("[showEvent] UndoStack size after push: %1").arg(m_undoStack.size()));
    }
    
    // Re-enable overlay when dialog opens
    if (m_overlay) {
        m_overlay->show();
        m_overlay->setEnabled(true);
        logToFile("[showEvent] Overlay shown and enabled");
    }
    
    updateUndoRedoButtons();
}

void AnnotationToolDialog::hideEvent(QHideEvent* event) {
    // Save undo/redo state when dialog is hidden
    logToFile("[hideEvent] DIALOG CLOSING");
    logToFile(QString("[hideEvent] Overlay annotations: %1").arg(m_overlay ? m_overlay->annotations().size() : 0));
    logToFile(QString("[hideEvent] Saving undo/redo state. Undo size: %1 Redo size: %2").arg(m_undoStack.size()).arg(m_redoStack.size()));
    logToFile("[hideEvent] UndoStack contents (before save):");
    for (int i = m_undoStack.size() - 1; i >= 0; --i) {
        logToFile(QString("  [%1]: %2 annotations").arg(i).arg(m_undoStack[i].size()));
    }
    
    saveUndoRedoState();
    
    // CRITICAL: Save overlay annotations to MainWindow for persistence across dialog destruction
    if (m_overlay) {
        m_savedAnnotations = m_overlay->annotations();
        logToFile(QString("[hideEvent] SAVED %1 overlay annotations for persistence").arg(m_savedAnnotations.size()));
        
        // Hide overlay when dialog closes - user shouldn't be able to draw while dialog is closed
        m_overlay->hide();
        m_overlay->setEnabled(false);
        logToFile("[hideEvent] Overlay hidden and disabled");
        
        // Find MainWindow by walking up the parent hierarchy
        QWidget* w = parentWidget();
        MainWindow* mainWin = nullptr;
        int depth = 0;
        while (w && !mainWin && depth < 10) {
            logToFile(QString("[hideEvent] Checking parent[%1] = %2").arg(depth).arg(w->metaObject()->className()));
            mainWin = qobject_cast<MainWindow*>(w);
            if (!mainWin) {
                w = w->parentWidget();
            }
            depth++;
        }
        
        if (mainWin) {
            logToFile(QString("[hideEvent] Found MainWindow after %1 levels. Saving %2 annotations").arg(depth).arg(m_savedAnnotations.size()));
            mainWin->m_persistedAnnotations = m_savedAnnotations;
            mainWin->m_persistedUndoStack = m_savedUndoStack;
            mainWin->m_persistedRedoStack = m_savedRedoStack;
            logToFile(QString("[hideEvent] MainWindow::m_persistedAnnotations now has %1 annotations").arg(mainWin->m_persistedAnnotations.size()));
            logToFile(QString("[hideEvent] MainWindow::m_persistedUndoStack now has %1 states").arg(mainWin->m_persistedUndoStack.size()));
            logToFile(QString("[hideEvent] MainWindow::m_persistedRedoStack now has %1 states").arg(mainWin->m_persistedRedoStack.size()));
        } else {
            logToFile("[hideEvent] WARNING: Could not find MainWindow in parent hierarchy!");
        }
    }
    
    logToFile(QString("[hideEvent] SavedUndo size: %1").arg(m_savedUndoStack.size()));
    
    DialogBase::hideEvent(event);
}