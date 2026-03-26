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
#include <QGridLayout>
#include <QApplication>
#include "../astrometry/AnnotationEngine.h"
#include "../astrometry/WCSUtils.h"
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

    // --- Auto Annotations ---
    m_catGroup = new QGroupBox(tr("Catalogs && Grid"));
    auto* catLayout = new QGridLayout(m_catGroup);
    
    m_chkMessier = new QCheckBox(tr("Messier"));
    m_chkNGC = new QCheckBox(tr("NGC"));
    m_chkIC = new QCheckBox(tr("IC"));
    m_chkLdN = new QCheckBox(tr("LdN"));
    m_chkSh2 = new QCheckBox(tr("Sh2"));
    m_chkStars = new QCheckBox(tr("Stars"));
    m_chkConstellations = new QCheckBox(tr("Constellations"));
    m_chkWcsGrid = new QCheckBox(tr("WCS Grid (RA/Dec)"));
    m_chkCompass = new QCheckBox(tr("Compass"));
    
    m_chkMessier->setChecked(true);
    m_chkNGC->setChecked(true);
    m_chkIC->setChecked(true);
    m_chkLdN->setChecked(true);
    m_chkSh2->setChecked(true);
    m_chkStars->setChecked(true);
    m_chkConstellations->setChecked(false);
    m_chkWcsGrid->setChecked(true);
    m_chkCompass->setChecked(true);
    
    // Compass position dropdown
    m_cmbCompassPosition = new QComboBox();
    m_cmbCompassPosition->addItem(tr("Center"),       static_cast<int>(AnnotationOverlay::CompassPosition::Center));
    m_cmbCompassPosition->addItem(tr("Top Left"),     static_cast<int>(AnnotationOverlay::CompassPosition::TopLeft));
    m_cmbCompassPosition->addItem(tr("Top Right"),    static_cast<int>(AnnotationOverlay::CompassPosition::TopRight));
    m_cmbCompassPosition->addItem(tr("Bottom Left"),  static_cast<int>(AnnotationOverlay::CompassPosition::BottomLeft));
    m_cmbCompassPosition->addItem(tr("Bottom Right"), static_cast<int>(AnnotationOverlay::CompassPosition::BottomRight));
    m_cmbCompassPosition->setCurrentIndex(0); // Center is default
    m_cmbCompassPosition->setAccessibleName(tr("Compass position"));
    m_cmbCompassPosition->setToolTip(tr("Choose where the compass is placed on the image"));
    
    // Add checkboxes to grid
    catLayout->addWidget(m_chkMessier, 0, 0);
    catLayout->addWidget(m_chkNGC, 0, 1);
    catLayout->addWidget(m_chkIC, 0, 2);
    catLayout->addWidget(m_chkLdN, 1, 0);
    catLayout->addWidget(m_chkSh2, 1, 1);
    catLayout->addWidget(m_chkStars, 1, 2);
    catLayout->addWidget(m_chkConstellations, 2, 0, 1, 2);
    catLayout->addWidget(m_chkWcsGrid, 3, 0, 1, 1);
    catLayout->addWidget(m_chkCompass, 3, 1, 1, 1);
    catLayout->addWidget(m_cmbCompassPosition, 3, 2, 1, 1);
    
    m_btnAnnotate = new QPushButton(tr("Annotate Image"));
    catLayout->addWidget(m_btnAnnotate, 4, 0, 1, 3);
    
    mainLayout->addWidget(m_catGroup);
    
    connect(m_btnAnnotate, &QPushButton::clicked, this, &AnnotationToolDialog::refreshAutomaticAnnotations);
    connect(m_chkWcsGrid, &QCheckBox::toggled, this, &AnnotationToolDialog::refreshAutomaticAnnotations);
    connect(m_chkCompass, &QCheckBox::toggled, this, &AnnotationToolDialog::refreshAutomaticAnnotations);
    connect(m_cmbCompassPosition, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnnotationToolDialog::refreshAutomaticAnnotations);

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
    
    // Create overlay if needed, or if the current one belongs to a different viewer
    if (!m_overlay || m_overlay->parent() != viewer) {
        if (m_overlay) {
            logToFile("[setViewer] Overlay exists but belongs to a different viewer. Destroying it.");
            // Save current annotations before destroying
            m_savedAnnotations = m_overlay->annotations();
            delete m_overlay;
            m_overlay = nullptr;
        }
        
        logToFile("[setViewer] Creating new overlay");
        logToFile(QString("[setViewer] m_savedAnnotations size at creation: %1").arg(m_savedAnnotations.size()));
        
        m_overlay = new AnnotationOverlay(viewer);
        // Make overlay fill entire viewer area and auto-resize
        m_overlay->setGeometry(0, 0, viewer->width(), viewer->height());
        
        if (this->isVisible()) {
            m_overlay->show();
            m_overlay->setEnabled(true);
        } else {
            m_overlay->hide();
            m_overlay->setEnabled(false);
        }
        
        // ... (Restore logic remains same)
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
        } else if (!m_savedAnnotations.isEmpty()) {
            logToFile(QString("[setViewer] Restoring %1 saved annotations from m_savedAnnotations").arg(m_savedAnnotations.size()));
            m_overlay->setAnnotations(m_savedAnnotations);
        }
        
        // Resize overlay when viewer is resized
        connect(viewer, &ImageViewer::resized, this, [this]() {
            if (m_overlay && m_viewer) {
                m_overlay->setGeometry(0, 0, m_viewer->width(), m_viewer->height());
            }
        });
        
        // Connect signals
        connect(m_overlay, &AnnotationOverlay::aboutToAddAnnotation,
                this, &AnnotationToolDialog::onAboutToAddAnnotation);
        connect(m_overlay, &AnnotationOverlay::textInputRequested,
                this, &AnnotationToolDialog::onTextInputRequested);
    } else {
        // Overlay already exists and belongs to the correct viewer
        logToFile(QString("[setViewer] Overlay already exists for this viewer. Syncing visibility with dialog. Current annotations: %1").arg(m_overlay->annotations().size()));
        
        if (this->isVisible()) {
            m_overlay->show();
            m_overlay->setEnabled(true);
        } else {
            m_overlay->hide();
            m_overlay->setEnabled(false);
        }
    }
    
    // Sync the draw mode from last state
    syncOverlayDrawMode();
    
    // Auto-annotate if WCS is valid and no annotations exist yet
    if (m_overlay && m_overlay->annotations().isEmpty() && WCSUtils::hasValidWCS(m_viewer->getBuffer().metadata())) {
        refreshAutomaticAnnotations();
    }
    
    m_statusLabel->setText(tr("Ready to draw"));
}

void AnnotationToolDialog::refreshAutomaticAnnotations() {
    if (!m_viewer || !m_overlay) return;

    auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) {
        QMessageBox::warning(this, tr("WCS Error"), tr("Image must be plate solved first."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Find data path
    QString dataPath;
    QStringList paths = {
        QCoreApplication::applicationDirPath() + "/data",
        QCoreApplication::applicationDirPath() + "/../Resources/data",
        QCoreApplication::applicationDirPath() + "/../data",
        QCoreApplication::applicationDirPath() + "/../../data",
        QDir::currentPath() + "/data"
    };

    for (const QString& p : paths) {
        if (QDir(p).exists("messier.csv")) {
            dataPath = p;
            break;
        }
    }

    if (dataPath.isEmpty()) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("Data Error"), tr("Could not find catalog data directory."));
        return;
    }

    QVector<CatalogObject> objects;
    int w = m_viewer->getBuffer().width();
    int h = m_viewer->getBuffer().height();

    auto deduplicate = [](QVector<CatalogObject>& current, const QVector<CatalogObject>& news) {
        const double EPSILON = 1.0/1800;
        for (const auto& n : news) {
            bool duplicate = false;
            for (const auto& c : current) {
                if (std::abs(c.ra - n.ra) < EPSILON && std::abs(c.dec - n.dec) < EPSILON) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                current.append(n);
            }
        }
    };

    if (m_chkMessier->isChecked()) {
        deduplicate(objects, AnnotationEngine::loadCatalog(dataPath + "/messier.csv", meta, "Messier", w, h));
    }
    if (m_chkNGC->isChecked()) {
        deduplicate(objects, AnnotationEngine::loadCatalog(dataPath + "/ngc.csv", meta, "NGC", w, h));
    }
    if (m_chkIC->isChecked()) {
        deduplicate(objects, AnnotationEngine::loadCatalog(dataPath + "/ic.csv", meta, "IC", w, h));
    }
    if (m_chkLdN->isChecked()) {
        deduplicate(objects, AnnotationEngine::loadCatalog(dataPath + "/ldn.csv", meta, "LdN", w, h));
    }
    if (m_chkSh2->isChecked()) {
        deduplicate(objects, AnnotationEngine::loadCatalog(dataPath + "/sh2.csv", meta, "Sh2", w, h));
    }
    if (m_chkStars->isChecked()) {
        deduplicate(objects, AnnotationEngine::loadCatalog(dataPath + "/stars.csv", meta, "Star", w, h));
    }
    if (m_chkConstellations->isChecked()) {
        objects.append(AnnotationEngine::loadConstellationLines(dataPath + "/constellations.csv", meta, w, h));
        // Add constellation names
        deduplicate(objects, AnnotationEngine::loadCatalog(dataPath + "/constellationsnames.csv", meta, "ConstellationName", w, h));
    }

    // Pass WCS Grid check state to overlay
    m_overlay->setWCSGridVisible(m_chkWcsGrid->isChecked());
    
    // Pass Compass visibility and position to overlay
    m_overlay->setCompassVisible(m_chkCompass->isChecked());
    {
        int idx = m_cmbCompassPosition->currentData().toInt();
        m_overlay->setCompassPosition(static_cast<AnnotationOverlay::CompassPosition>(idx));
    }

    m_overlay->setWCSObjects(objects);
    m_overlay->setWCSObjectsVisible(true);
    
    QApplication::restoreOverrideCursor();
    m_statusLabel->setText(tr("Annotations updated. Found %1 unique objects.").arg(objects.size()));
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
    if (!m_overlay) return; // Added check
    // Save current state for undo
    pushUndoState();
    m_overlay->clearManualAnnotations();
}

void AnnotationToolDialog::onColorChanged(int index) {
    if (!m_overlay) return; // Added check
    QColor color = m_colorCombo->itemData(index).value<QColor>();
    m_overlay->setDrawColor(color);
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
    logToFile(QString("[onUndo] Overlay now has %1 annotations").arg(m_overlay ? m_overlay->annotations().size() : 0));
    
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
            logToFile(QString("[hideEvent] MainWindow::m_persistedAnnotations now has %1 annotations").arg(mainWin ? mainWin->m_persistedAnnotations.size() : 0));
            logToFile(QString("[hideEvent] MainWindow::m_persistedUndoStack now has %1 states").arg(mainWin ? mainWin->m_persistedUndoStack.size() : 0));
            logToFile(QString("[hideEvent] MainWindow::m_persistedRedoStack now has %1 states").arg(mainWin ? mainWin->m_persistedRedoStack.size() : 0));
        } else {
            logToFile("[hideEvent] WARNING: Could not find MainWindow in parent hierarchy!");
        }
    }
    
    logToFile(QString("[hideEvent] SavedUndo size: %1").arg(m_savedUndoStack.size()));
    
    DialogBase::hideEvent(event);
}