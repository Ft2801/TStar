#include "MainWindow.h"
#include "core/Version.h"
#include "core/Logger.h"
#include "widgets/CustomMdiSubWindow.h"
#include "dialogs/GHSDialog.h"
#include "Icons.h"
#include <QSvgRenderer>
#include <QTimer>
#include <QThread>
#include <QProgressDialog>
#include <QPainter>
#include <QFile>
#include <QMenuBar>
#include <QMenu>
#include <QToolButton>
#include <QToolBar>
#include <cstdlib>
#include <QMdiSubWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QComboBox>
#include <QDialog>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <cmath> // for std::abs
#include <QSet>
#include <exception>

#include <QFormLayout>
#include <QDialogButtonBox>

#include <QApplication>
#include <QTime>
#include <QEventLoop>
#include <QCoreApplication>
#include "dialogs/CropRotateDialog.h"
#include "dialogs/CurvesDialog.h"
#include "io/FitsLoader.h"
#include "io/SimpleTiffReader.h"
#include "dialogs/StretchDialog.h"
#include "dialogs/ABEDialog.h"
#include "dialogs/SCNRDialog.h"
#include "dialogs/SaturationDialog.h"
#include "dialogs/ChannelCombinationDialog.h"
#include "algos/ChannelOps.h"
#include "dialogs/SettingsDialog.h"
#include "dialogs/GraXpertDialog.h"
#include "dialogs/CosmicClarityDialog.h"
#include "dialogs/StarNetDialog.h"
#include "dialogs/RARDialog.h"
#include "dialogs/StarStretchDialog.h"
#include "dialogs/StarRecompositionDialog.h"
#include "dialogs/PerfectPaletteDialog.h"
#include "dialogs/PlateSolvingDialog.h"
#include "dialogs/PCCDialog.h"
#include "dialogs/BackgroundNeutralizationDialog.h"
#include "dialogs/PixelMathDialog.h"

#include "dialogs/UpdateDialog.h" // [NEW] Auto-updater dialog
#include "network/UpdateChecker.h" // [NEW] Auto-updater checker
#include "dialogs/HeaderViewerDialog.h"
#include "dialogs/StackingDialog.h"
#include "dialogs/RegistrationDialog.h"
#include "dialogs/PreprocessingDialog.h"
#include "dialogs/NewProjectDialog.h"
#include "dialogs/ScriptDialog.h"
#include "dialogs/ScriptBrowserDialog.h"
#include "dialogs/ConversionDialog.h"
#include "stacking/StackingProject.h"
#include "scripting/StackingCommands.h"
#include "scripting/ScriptRunner.h"
#include "dialogs/HelpDialog.h"
#include "dialogs/HeaderEditorDialog.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/ArcsinhStretchDialog.h"
#include "dialogs/HistogramStretchDialog.h"
#include "widgets/SplashScreen.h"
#include "dialogs/WavescaleHDRDialog.h"
#include "dialogs/StarAnalysisDialog.h"
#include "dialogs/PCCDistributionDialog.h"
#include <QDockWidget>
#include <opencv2/opencv.hpp>
#include <QTextEdit>
#include <QDateTime>
#include <QSplitter>
#include "dialogs/AstroSpikeDialog.h"
#include "dialogs/DebayerDialog.h"
#include "dialogs/ContinuumSubtractionDialog.h"
#include "dialogs/AnnotationToolDialog.h"
#include "widgets/AnnotationOverlay.h"
#include "widgets/SidebarWidget.h"
#include "widgets/HeaderPanel.h"
#include "dialogs/ExtractLuminanceDialog.h"
#include "dialogs/RecombineLuminanceDialog.h"
#include "dialogs/CorrectionBrushDialog.h"
#include "dialogs/ClaheDialog.h"
#include "dialogs/AberrationInspectorDialog.h"
#include "dialogs/SelectiveColorDialog.h"
#include <QResizeEvent>
#include <QStatusBar>
#include <QRegularExpression>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QShowEvent>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include "widgets/ResourceMonitorWidget.h"

#include <QThreadPool>
#include "../core/ThreadState.h"

MainWindow::~MainWindow() {
    // Destructor - cleanup is handled in closeEvent
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    
    setWindowOpacity(0.0); // Start invisible for fade-in
    setAcceptDrops(true);  // Enable drag and drop
    
    // === Setup UI ===
    QWidget* cwPtr = new QWidget(this);
    setCentralWidget(cwPtr);
    
    QHBoxLayout* mainLayout = new QHBoxLayout(cwPtr);
    
    // Icon Maker (handles SVG or File)
    auto makeIcon = [](const QString& source) -> QIcon {
        if (source.endsWith(".png") || source.endsWith(".jpg") || source.endsWith(".svg")) {
             QString path = source;
             // If relative and not resource, prepend app dir
             if (!source.startsWith(":") && !QDir::isAbsolutePath(source)) {
                 path = QCoreApplication::applicationDirPath() + "/" + source;
                 // If not found, try Resources folder (macOS DMG bundle)
                 if (!QFile::exists(path)) {
                     path = QCoreApplication::applicationDirPath() + "/../Resources/" + source;
                 }
             }
             return QIcon(path); // Load from file
        } else {
            // Assume SVG string
            QPixmap pm(24, 24);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            QSvgRenderer r(source.toUtf8());
            r.render(&p);
            return QIcon(pm);
        }
    };
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // 1. Sidebar (Left)
    m_sidebar = new SidebarWidget(this); // Width managed internally
    mainLayout->addWidget(m_sidebar);
    
    // 2. MDI Area (Right)
    // Custom MDI Area for Background Painting
    class TStarMdiArea : public QMdiArea {
    public:
        explicit TStarMdiArea(QWidget* parent = nullptr) : QMdiArea(parent) {}
    protected:
        void paintEvent(QPaintEvent* event) override {
            // 1. Fill Base Background
            QPainter p(viewport());
            p.fillRect(event->rect(), QColor(30, 30, 30)); // #1E1E1E

            // 2. Center Grid (Rotated 45 degrees, 400px square)
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            
            int w = width();
            int h = height();
            int cx = w / 2;
            int cy = h / 2;
            int side = 400;
            int half = side / 2;
            
            p.translate(cx, cy);
            p.rotate(45);
            
            // Define the square area
            QRect squareRect(-half, -half, side, side);
            
            p.setClipRect(squareRect);
            
            // Draw Grid lines inside the square
            QColor gridColor(40, 40, 40); // #282828
            QPen gridPen(gridColor);
            gridPen.setWidthF(3.0);
            p.setPen(gridPen);
            
            int step = 50;
            
            // Draw vertical lines (relative to rotated system)
            for (int x = -half; x <= half; x += step) {
                p.drawLine(x, -half, x, half);
            }
            
            // Draw horizontal lines (relative to rotated system)
            for (int y = -half; y <= half; y += step) {
                p.drawLine(-half, y, half, y);
            }
            
            p.restore();
            
            // 3. Bottom Right Text "TStar"
            p.save();
            p.setRenderHint(QPainter::TextAntialiasing);
            
            QString text = "TStar";
            // Try to find a script font
            QFont font("Segoe Script", 48, QFont::Bold); // Windows standard script
            if (!QFontInfo(font).exactMatch()) {
                font = QFont("Brush Script MT", 48, QFont::Normal, true); // Fallback: italic=true
            }
            if (!QFontInfo(font).exactMatch()) {
                font = QFont(font.family(), 48, QFont::Normal, true); // Generic Italic
            }
            
            p.setFont(font);
            p.setPen(QColor(51, 51, 51)); // Same 5% lighter color
            
            QFontMetrics fm(font);
            int tw = fm.horizontalAdvance(text);
            // int th = fm.height(); // Unused
            
            // Position: Bottom Right with padding
            int px = w - tw - 30;
            int py = h - 20; // Baseline
            
            p.drawText(px, py, text);
            p.restore();

            // 4. Draw Subwindows (handled by QMdiArea basic painting if we don't call base?)
            
            QMdiArea::paintEvent(event); 
        }
    };
    
    m_mdiArea = new TStarMdiArea(this);
    m_mdiArea->setBackground(Qt::NoBrush); // Disable default brush so our paintEvent dominates
    
    m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
    m_mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdiArea->setDocumentMode(true); // Tabbed view if we wanted, but we use subwindows
    // Prevent new windows from inheriting maximized state from active window
    m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
    mainLayout->addWidget(m_mdiArea);
    
    // 3. Resource Monitor (Status Bar, bottom-right)
    auto* resMonitor = new ResourceMonitorWidget(this);
    statusBar()->addPermanentWidget(resMonitor);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->setStyleSheet(
        "QStatusBar { background: #1a1a1a; color: #aaa; border-top: 1px solid #333; padding: 2px; }"
    );

    // Pixel Info Label (Left of Status Bar)
    m_pixelInfoLabel = new QLabel(this);
    m_pixelInfoLabel->setStyleSheet("color: #ccc; font-family: Consolas; padding-left: 10px;");
    statusBar()->addWidget(m_pixelInfoLabel);

    // Sidebar is now overlay, not in layout
    m_sidebar->setParent(this);
    m_sidebar->raise(); // Ensure on top
    
    // Create left margin to prevent windows from going under sidebar tab strip
    // The tab strip is 32px wide, add small padding
    int sidebarTabWidth = 34; // 32px tab + 2px buffer
    mainLayout->setContentsMargins(sidebarTabWidth, 0, 0, 0);

    
    // Immediate initial sync for overlay positioning
    if (this->centralWidget()) {
        m_sidebar->move(this->centralWidget()->x(), this->centralWidget()->y());
        m_sidebar->resize(m_sidebar->totalVisibleWidth(), this->centralWidget()->height());
    }
    
    // Setup Sidebar Panels
    
    // -- Console Panel --
    QTextEdit* consoleEdit = new QTextEdit();
    consoleEdit->setReadOnly(true);
    consoleEdit->setStyleSheet("background-color: transparent; color: #dcdcdc; border: none; font-family: Consolas, monospace;");
    m_sidebar->addPanel(tr("Console"), ":/images/console_icon.png", consoleEdit); // Need icon or fallback
    
    // -- Header Panel --
    m_headerPanel = new HeaderPanel();
    m_sidebar->addPanel(tr("Header"), ":/images/header_icon.png", m_headerPanel);

    // Tools move to Toolbar "Process" menu. 
    // Console and Header remain in Sidebar.

    // === TIMER SETUP IN CONSTRUCTOR ===
    m_tempConsoleTimer = new QTimer(this);
    connect(m_tempConsoleTimer, &QTimer::timeout, this, [this](){
        if (m_isConsoleTempOpen && m_sidebar) {
            if (m_sidebar->isInteracting()) {
                // Still interacting, timer will fire again
            } else {
                m_sidebar->collapse();
                m_isConsoleTempOpen = false;
                m_tempConsoleTimer->stop();
            }
        } else {
            m_tempConsoleTimer->stop();
        }
    });

    // Sync Manual Collapse with Timer State
    connect(m_sidebar, &SidebarWidget::expandedToggled, [this](bool expanded){
        if (!expanded && m_isConsoleTempOpen) {
            m_isConsoleTempOpen = false;
            if (m_tempConsoleTimer) m_tempConsoleTimer->stop();
        }
    });

    // Default connection for header updates


    setAcceptDrops(true);
    m_mdiArea->setAcceptDrops(true);
    m_mdiArea->setAcceptDrops(true);
    m_mdiArea->installEventFilter(this);
    
    if (m_mdiArea->viewport()) {
        m_mdiArea->viewport()->installEventFilter(this);
    }
    
    connect(m_mdiArea, &QMdiArea::subWindowActivated, [this](QMdiSubWindow *window) {
        qDebug() << "[MainWindow] subWindowActivated: " << (window ? "Valid Window" : "NULL");
        if (m_isUpdating) return;
        m_isUpdating = true;
        
        CustomMdiSubWindow* csw = qobject_cast<CustomMdiSubWindow*>(window);
        ImageViewer* v = csw ? csw->viewer() : nullptr;
        
        // 0. Update Border Highlighting
        for (auto sw : m_mdiArea->subWindowList()) {
            if (auto sub = qobject_cast<CustomMdiSubWindow*>(sw)) {
                sub->setActiveState(sw == window);
            }
        }
        
        // 0.1. Raise all tool windows above image views (delayed to ensure activation completes)
        QTimer::singleShot(0, this, [this](){
            for (auto sw : m_mdiArea->subWindowList()) {
                if (auto sub = qobject_cast<CustomMdiSubWindow*>(sw)) {
                    if (sub->isToolWindow()) {
                        sub->raise();
                    }
                }
            }
        });
        
        // 1. Update Header Panel
        if (v) {
            m_headerPanel->setMetadata(v->getBuffer().metadata());
        } else if (!window) {
            m_headerPanel->clear();
        }

        // 2. Handle Saturation Tool Retargeting
        if (window) {
            CustomMdiSubWindow* csw = qobject_cast<CustomMdiSubWindow*>(window);
            ImageViewer* v = csw ? csw->viewer() : nullptr;

            if (v && !v->property("isPreview").toBool()) {
                if (m_lastActiveImageViewer != v) {
                    // Save state of previous if needed
                    if (m_lastActiveImageViewer) {
                        if (m_ghsDlg) m_ghsStates[m_lastActiveImageViewer] = m_ghsDlg->getState();
                        if (m_curvesDlg) m_curvesStates[m_lastActiveImageViewer] = m_curvesDlg->getState();
                        if (m_satDlg) m_satStates[m_lastActiveImageViewer] = m_satDlg->getState();
                    }

                    m_lastActiveImageViewer = v;
                    log(tr("Active View Changed: %1").arg(v->windowTitle()), Log_Info);

                    // --- Sync Tools to New Viewer ---
                    if (m_abeDlg) m_abeDlg->setViewer(v);
                    if (m_bnDlg) m_bnDlg->setViewer(v);
                    if (m_wavescaleDlg) m_wavescaleDlg->setViewer(v);
                    if (m_histoDlg) m_histoDlg->setViewer(v);
                    if (m_stretchDlg) m_stretchDlg->setViewer(v);
                    if (m_ghsDlg) {
                         if (m_ghsTarget && m_ghsTarget != v) {
                             // GHS typically uses local copy or preview LUT.
                             // setTarget should handle cleanup.
                         }
                         m_ghsTarget = v; // Update tracker
                         m_ghsDlg->setTarget(v); 
                         if (m_ghsStates.contains(v)) m_ghsDlg->setState(m_ghsStates[v]);
                    }
                    if (m_curvesDlg) {
                        if (m_curvesTarget && m_curvesTarget != v) {
                             m_curvesTarget->clearPreviewLUT(); // Explicit cleanup for safe measure
                        }
                        m_curvesTarget = v; // Update tracker
                        m_curvesDlg->setViewer(v);
                        if (m_curvesStates.contains(v)) m_curvesDlg->setState(m_curvesStates[v]);
                    }
                    if (m_satDlg) {
                        // SaturationDialog handles backup/restore internally now
                        m_satTarget = v; // Update tracker for menu updates
                        m_satDlg->setViewer(v); 
                        if (m_satStates.contains(v)) m_satDlg->setState(m_satStates[v]);
                    }
                    if (m_arcsinhDlg) m_arcsinhDlg->setViewer(v);
                    if (m_scnrDlg) m_scnrDlg->setViewer(v);
                    if (m_pixelMathDialog) m_pixelMathDialog->setViewer(v);
                    if (m_rarDlg) m_rarDlg->setViewer(v);
                    if (m_starStretchDlg) m_starStretchDlg->setViewer(v);
                    if (m_starRecompDlg) m_starRecompDlg->setViewer(v);
                    if (m_ppDialog) m_ppDialog->setViewer(v);
                    if (m_plateSolveDlg) m_plateSolveDlg->setViewer(v);
                    if (m_pccDlg) m_pccDlg->setViewer(v);
                    if (m_cropDlg) m_cropDlg->setViewer(v);

                    if (m_pccDlg) m_pccDlg->setViewer(v);
                    if (m_cropDlg) m_cropDlg->setViewer(v);
                    if (m_astroSpikeDlg) m_astroSpikeDlg->setViewer(v);
                    if (m_annotatorDlg) m_annotatorDlg->setViewer(v);

                    if (m_headerPanel) m_headerPanel->setMetadata(v->getBuffer().metadata());
                    
                    // Sync Display Mode UI to the new viewer's state
                    if (m_stretchCombo) {
                        QSignalBlocker b(m_stretchCombo);
                        int idx = m_stretchCombo->findData(static_cast<int>(v->getDisplayMode()));
                        if (idx >= 0) m_stretchCombo->setCurrentIndex(idx);
                        m_displayMode = v->getDisplayMode();
                    }
                    if (m_linkChannelsBtn) {
                        QSignalBlocker b(m_linkChannelsBtn);
                        m_linkChannelsBtn->setChecked(v->isDisplayLinked());
                        m_displayLinked = v->isDisplayLinked();
    auto getImgPath = [](const QString& name) {
        QString path = QCoreApplication::applicationDirPath() + "/images/" + name;
        // If not found, try Resources folder (macOS DMG bundle)
        if (!QFile::exists(path)) {
            path = QCoreApplication::applicationDirPath() + "/../Resources/images/" + name;
        }
        return path;
    };

    m_linkChannelsBtn->setIcon(QIcon(
        m_displayLinked ? getImgPath("linked.svg") : getImgPath("unlinked.svg")
    ));
                    }
                    
                    // Ensure connections
                    connect(v, &ImageViewer::viewChanged, this, &MainWindow::propagateViewChange, Qt::UniqueConnection);
                    connect(v, &ImageViewer::historyChanged, this, &MainWindow::updateMenus, Qt::UniqueConnection);
                    updateMenus(); 
                }
            }
        }
        
        // 4. INTERACTIVE TOOL EXCLUSIVITY Logic
        if (window) {
            QWidget* widget = window->widget(); // The QFrame container
            if (widget) {
                // Search inside the QFrame
                ABEDialog* abe = widget->findChild<ABEDialog*>();
                BackgroundNeutralizationDialog* bn = widget->findChild<BackgroundNeutralizationDialog*>();
                GHSDialog* ghs = widget->findChild<GHSDialog*>();
                CropRotateDialog* crop = widget->findChild<CropRotateDialog*>();
                
                // Note: If the widget IS the dialog (rare with CustomMdiSubWindow wrapping), handle it:
                if (!abe) abe = qobject_cast<ABEDialog*>(widget); // unlikely with QFrame
                
                auto enforceExclusivity = [&](QWidget* activeToolWidget) {
                     m_activeInteractiveTool = activeToolWidget;
                     for (auto sub : m_mdiArea->subWindowList()) {
                         QWidget* w = sub->widget();
                         if (!w) continue;
                         
                         if (auto a = w->findChild<ABEDialog*>()) {
                             if (a != activeToolWidget) a->setAbeMode(false);
                         }
                         if (auto b = w->findChild<BackgroundNeutralizationDialog*>()) {
                             if (b != activeToolWidget) b->setInteractionEnabled(false);
                         }
                         if (auto g = w->findChild<GHSDialog*>()) {
                             if (g != activeToolWidget) g->setInteractionEnabled(false);
                         }
                     }
                     
                    // Check logic for other tools...
                    if (m_headerDlg) {
                        m_headerDlg->setViewer(v);
                    }
                    if (m_starAnalysisDlg) {
                        m_starAnalysisDlg->setViewer(v);
                    }
                    if (m_stretchDlg) {
                        m_stretchDlg->setViewer(v);
                    }
                };

                if (abe) {
                    if (m_activeInteractiveTool != abe) {
                        enforceExclusivity(abe);
                        abe->setAbeMode(true);
                        log(tr("Interactive Mode: Auto Background Extraction"), Log_Info);
                    }
                } else if (bn) {
                    if (m_activeInteractiveTool != bn) {
                       enforceExclusivity(bn);
                       bn->setInteractionEnabled(true);
                       log(tr("Interactive Mode: Background Neutralization"), Log_Info);
                    }
                } else if (ghs) {
                    if (m_activeInteractiveTool != ghs) {
                        enforceExclusivity(ghs);
                        ghs->setInteractionEnabled(true);
                        log(tr("Interactive Mode: GHS Point Picking"), Log_Info);
                    }
                } else if (crop) {
                    if (m_activeInteractiveTool != crop) {
                        enforceExclusivity(crop);
                        if(m_lastActiveImageViewer) m_lastActiveImageViewer->setCropMode(true);
                        log(tr("Interactive Mode: Crop"), Log_Info);
                    }
                }
            }
        }
        m_isUpdating = false;
    });
    
    // Timer is now created in the constructor - removed duplicate here


    // Menu Style
    menuBar()->setStyleSheet("QMenuBar { background-color: #252525; color: #ccc; } QMenuBar::item:selected { background: #444; }");
    menuBar()->setVisible(false); 

    m_stretchCombo = new QComboBox();
    m_stretchCombo->setFixedWidth(120);
    m_stretchCombo->addItem(tr("Linear"), ImageBuffer::Display_Linear);
    m_stretchCombo->addItem(tr("Auto Stretch"), ImageBuffer::Display_AutoStretch);
    m_stretchCombo->addItem(tr("Histogram"), ImageBuffer::Display_Histogram);
    m_stretchCombo->addItem(tr("ArcSinh"), ImageBuffer::Display_ArcSinh);
    m_stretchCombo->addItem(tr("Square Root"), ImageBuffer::Display_Sqrt);
    m_stretchCombo->addItem(tr("Logarithmic"), ImageBuffer::Display_Log);
    m_stretchCombo->setCurrentIndex(0);
    
    // Custom Styling for Combo to match Buttons
    m_stretchCombo->setStyleSheet(
        "QComboBox { "
        "   background-color: #333; "
        "   color: #e0e0e0; "
        "   border: 1px solid #555; "
        "   border-radius: 4px; "
        "   padding: 4px 10px; "
        "} "
        "QComboBox:hover { "
        "   background-color: #444; "
        "   border-color: #666; "
        "} "
        "QComboBox::drop-down { "
        "   subcontrol-origin: padding; "
        "   subcontrol-position: top right; "
        "   width: 20px; "
        "   border-left-width: 0px; "
        "} "
        "QComboBox QAbstractItemView { "
        "   background-color: #333; "
        "   color: #e0e0e0; "
        "   selection-background-color: #555; "
        "}"
    );
    
    connect(m_stretchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this]([[maybe_unused]] int index){
        m_displayMode = static_cast<ImageBuffer::DisplayMode>(m_stretchCombo->currentData().toInt());
        log(tr("Display Mode changed to: %1").arg(m_stretchCombo->currentText()), Log_Info);
        updateDisplay();
    });
    
    m_linkViewsAction = new QAction(tr("Link Views"), this);
    m_linkViewsAction->setCheckable(true);
    m_linkViewsAction->setChecked(false);
    m_linkViewsAction->setToolTip(tr("Link Zoom and Pan across all windows"));

    // Toolbar
    QToolBar* mainToolbar = addToolBar(tr("Main Toolbar"));
    mainToolbar->setMovable(false);
    mainToolbar->setIconSize(QSize(24, 24));
    mainToolbar->setStyleSheet(
        "QToolBar { background-color: #252525; border-bottom: 1px solid #1a1a1a; spacing: 5px; } "
        "QToolBar::separator { background: transparent; width: 5px; border: none; }"
    );
    mainToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly); // Icons only
    
    // Icon Maker (handles SVG or File)
    
    auto addBtn = [&](const QString& name, const QString& source, auto slot) -> QAction* {
        QAction* act = mainToolbar->addAction(makeIcon(source), name);
        connect(act, &QAction::triggered, this, slot);
        return act;
    };
    
    // 0. Home Directory (SVG)
    auto homeBtn = addBtn(tr("Set Home"), "images/home.svg", [this](){
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Home Directory"), QDir::currentPath());
        if (!dir.isEmpty()) {
            QDir::setCurrent(dir);
            log(tr("Home Directory changed to: %1").arg(dir), Log_Success);
            
            // Also update ScriptRunner default CWD if available
            // (Note: ScriptRunner reads QDir::currentPath() by default now, so this is implicit)
        }
    });
    homeBtn->setToolTip(tr("Set Home Directory (CWD)"));
    
    // 1. Open / Save (Files)
    addBtn(tr("Open"), "images/open.png", &MainWindow::openFile)->setShortcut(QKeySequence::Open);
    addBtn(tr("Save"), "images/save.png", &MainWindow::saveFile)->setShortcut(QKeySequence::Save);
    
    // Spacer
    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }

    // 2. Undo / Redo (SVG)
    m_undoAction = mainToolbar->addAction(makeIcon(Icons::UNDO), tr("Undo"));
    m_undoAction->setToolTip(tr("Undo (Ctrl+Z)"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    // m_undoAction->setEnabled(false); // Managed by updateMenus
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

    m_redoAction = mainToolbar->addAction(makeIcon(Icons::REDO), tr("Redo"));
    m_redoAction->setToolTip(tr("Redo (Ctrl+Shift+Z)"));
    m_redoAction->setShortcut(QKeySequence("Ctrl+Shift+Z"));
    // m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);
    
    // Spacer
    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }
    
    // 3. Zoom / Fit (SVG)
    addBtn(tr("Zoom In"), Icons::ZOOM_IN, [this](){ if(currentViewer()) currentViewer()->zoomIn(); })->setShortcut(QKeySequence::ZoomIn);
    addBtn(tr("Zoom Out"), Icons::ZOOM_OUT, [this](){ if(currentViewer()) currentViewer()->zoomOut(); })->setShortcut(QKeySequence::ZoomOut);
    addBtn(tr("Fit to Screen"), Icons::FIT_SCREEN, [this](){ if(currentViewer()) currentViewer()->fitToWindow(); })->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    addBtn(tr("1:1"), Icons::ZOOM_100, [this](){ if(currentViewer()) currentViewer()->zoom1to1(); })->setToolTip(tr("Zoom 100%"));
    
    // Spacer
    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }

    // 4. Geometry (SVG)
    addBtn(tr("Rotate Left"), Icons::ROTATE_LEFT, [this](){ applyGeometry(tr("Rotate CCW"), [](ImageBuffer& b){ b.rotate270(); }); });
    addBtn(tr("Rotate Right"), Icons::ROTATE_RIGHT, [this](){ applyGeometry(tr("Rotate CW"), [](ImageBuffer& b){ b.rotate90(); }); });
    addBtn(tr("Flip Horiz"), Icons::FLIP_HORIZ, [this](){ applyGeometry(tr("Mirror H"), [](ImageBuffer& b){ b.mirrorX(); }); });
    addBtn(tr("Flip Vert"), Icons::FLIP_VERT, [this](){ applyGeometry(tr("Mirror V"), [](ImageBuffer& b){ b.mirrorY(); }); });
    addBtn(tr("Crop / Rotate"), Icons::CROP, &MainWindow::cropTool);
    
    // Spacer
    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }
    
    // 5. Tools (Files)
    
    // Add Display Stretch Controls
    mainToolbar->addWidget(m_stretchCombo);
    
    // Spacer
    QWidget* spacer = new QWidget(this);
    spacer->setFixedWidth(5);
    mainToolbar->addWidget(spacer);
    
    // Replace Action with CheckBox
    m_linkChannelsBtn = new QToolButton(this);
    m_linkChannelsBtn->setCheckable(true);
    m_linkChannelsBtn->setChecked(true);
    m_linkChannelsBtn->setIcon(makeIcon("images/linked.svg"));
    m_linkChannelsBtn->setToolTip(tr("Toggle RGB Channel Linking for Stretch"));
    m_displayLinked = true;
    
    connect(m_linkChannelsBtn, &QToolButton::toggled, [this, makeIcon](bool checked){
        m_displayLinked = checked;
        m_linkChannelsBtn->setIcon(makeIcon(checked ? "images/linked.svg" : "images/unlinked.svg"));
        if (auto v = currentViewer()) {
            v->setDisplayState(m_displayMode, m_displayLinked);
            log(tr("RGB Link: %1").arg(checked ? tr("Enabled") : tr("Disabled")));
        }
    });
    mainToolbar->addWidget(m_linkChannelsBtn);

    // Spacer
    QWidget* s1 = new QWidget(this);
    s1->setFixedWidth(5);
    mainToolbar->addWidget(s1);

    m_invertBtn = new QToolButton(this);
    m_invertBtn->setCheckable(true);
    m_invertBtn->setIcon(makeIcon("images/invert.svg"));
    m_invertBtn->setToolTip(tr("Invert Image Colors"));
    connect(m_invertBtn, &QToolButton::toggled, [this](bool checked){
        if (auto v = currentViewer()) {
            v->setInverted(checked);
            log(tr("Invert: %1").arg(checked ? tr("Enabled") : tr("Disabled")));
        }
    });
    mainToolbar->addWidget(m_invertBtn);

    // Spacer
    QWidget* s2 = new QWidget(this);
    s2->setFixedWidth(5);
    mainToolbar->addWidget(s2);

    m_falseColorBtn = new QToolButton(this);
    m_falseColorBtn->setCheckable(true);
    m_falseColorBtn->setIcon(makeIcon("images/false-color.svg"));
    m_falseColorBtn->setToolTip(tr("False Color Visualization"));
    m_falseColorBtn->setToolTip(tr("False Color Visualization"));
    connect(m_falseColorBtn, &QToolButton::toggled, [this](bool checked){
        if (auto v = currentViewer()) {
            v->setFalseColor(checked);
            log(tr("False Color: %1").arg(checked ? tr("Enabled") : tr("Disabled")));
        }
    });
    mainToolbar->addWidget(m_falseColorBtn);
    
    // Spacer
    QWidget* s3 = new QWidget(this);
    s3->setFixedWidth(5);
    mainToolbar->addWidget(s3);
    
    // 6. Process Menu (Categorized Tools)
    QToolButton* processBtn = new QToolButton(this);
    processBtn->setText(tr("Process"));
    processBtn->setPopupMode(QToolButton::InstantPopup);
    processBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    
    QMenu* processMenu = new QMenu(this);
    processMenu->setStyleSheet(
        "QMenu { "
        "   background-color: #2b2b2b; "
        "   color: #e0e0e0; "
        "   border: 1px solid #555; "
        "} "
        "QMenu::item { "
        "   padding: 5px 25px 5px 10px; "
        "} "
        "QMenu::item:selected { "
        "   background-color: #444; "
        "} "
        "QMenu::separator { "
        "   height: 1px; "
        "   background: #555; "
        "   margin: 4px 0; "
        "} "
    );

    auto addMenuAction = [this](QMenu* menu, const QString& name, const QString& icon, auto slot) {
        QAction* act = menu->addAction(name);
        if (!icon.isEmpty()) act->setIcon(QIcon(icon));
        connect(act, &QAction::triggered, this, slot);
    };

    // --- A. Stretch ---
    QMenu* stretchMenu = processMenu->addMenu(tr("Stretch Tools"));
    addMenuAction(stretchMenu, tr("Auto Stretch"), "", [this](){
        openStretchDialog();
    });
    addMenuAction(stretchMenu, tr("ArcSinh Stretch"), "", [this](){
        openArcsinhStretchDialog();
    });
    addMenuAction(stretchMenu, tr("Curves Transformation"), "", [this](){
        openCurvesDialog();
    });
    addMenuAction(stretchMenu, tr("Histogram Transformation"), "", [this](){
        openHistogramStretchDialog();
    });
    addMenuAction(stretchMenu, tr("GHS (Generalized Hyperbolic)"), "", [this](){
        openGHSDialog();
    });
    addMenuAction(stretchMenu, tr("Star Stretch"), "", [this](){
        openStarStretchDialog();
    });

    // --- B. Color ---
    QMenu* colorMenu = processMenu->addMenu(tr("Color Management"));
    addMenuAction(colorMenu, tr("Auto Background Extraction (ABE)"), "", [this](){
        openAbeDialog();
    });
    addMenuAction(colorMenu, tr("Photometric Color Calibration"), "", [this](){
        openPCCDialog();
    });
    addMenuAction(colorMenu, tr("Background Neutralization"), "", [this](){
        openBackgroundNeutralizationDialog();
    });
    addMenuAction(colorMenu, tr("SCNR (Remove Green)"), "", [this](){
        openSCNRDialog(); 
    });
    addMenuAction(colorMenu, tr("PCC Distribution"), "", [this](){
        openPCCDistributionDialog();
    });
    addMenuAction(colorMenu, tr("Saturation"), "", [this](){
        openSaturationDialog();
    });
    addMenuAction(colorMenu, tr("Selective Color Correction"), "", [this](){
        openSelectiveColorDialog();
    });

    // --- C. AI ---
    QMenu* aiMenu = processMenu->addMenu(tr("AI Processing"));
    addMenuAction(aiMenu, tr("Cosmic Clarity"), "", [this](){
        if (activateTool(tr("Cosmic Clarity"))) return;
        if (!currentViewer()) { QMessageBox::warning(this, tr("No Image"), tr("Select image.")); return; }
        log(tr("Opening Cosmic Clarity..."), Log_Action, true);
        auto dlg = new CosmicClarityDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, [this, dlg](){ runCosmicClarity(dlg->getParams()); });
        setupToolSubwindow(nullptr, dlg, tr("Cosmic Clarity"));
    });
    addMenuAction(aiMenu, tr("GraXpert"), "", [this](){
        if (activateTool(tr("GraXpert"))) return;
        if (!currentViewer()) { QMessageBox::warning(this, tr("No Image"), tr("Select image.")); return; }
        log(tr("Opening GraXpert..."), Log_Action, true);
        auto dlg = new GraXpertDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, [this, dlg](){ runGraXpert(dlg->getParams()); });
        setupToolSubwindow(nullptr, dlg, tr("GraXpert"));
    });
    addMenuAction(aiMenu, tr("StarNet++"), "", [this](){
        if (activateTool(tr("Remove Stars (StarNet)"))) return;
        if (!currentViewer()) { QMessageBox::warning(this, tr("No Image"), tr("Select image.")); return; }
        log(tr("Opening StarNet++..."), Log_Action, true);
        auto dlg = new StarNetDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Remove Stars (StarNet)"));
        sub->resize(sub->width() + 100, sub->height());
        centerToolWindow(sub);
    });
    addMenuAction(aiMenu, tr("Aberration Remover"), "", [this](){
        openRARDialog();
    });

    // --- D. Channels ---
    QMenu* chanMenu = processMenu->addMenu(tr("Channel Operations"));
    addMenuAction(chanMenu, tr("Extract Channels"), "", &MainWindow::extractChannels);
    addMenuAction(chanMenu, tr("Extract Luminance"), "", [this](){
        openExtractLuminanceDialog();
    });
    addMenuAction(chanMenu, tr("Recombine Luminance"), "", [this](){
        openRecombineLuminanceDialog();
    });
    addMenuAction(chanMenu, tr("Remove Pedestal (Auto)"), "", [this](){
        removePedestal();
    });
    addMenuAction(chanMenu, tr("Combine Channels"), "", [this](){
        if (activateTool(tr("Combine Channels"))) return;
        combineChannels();
    });
    addMenuAction(chanMenu, tr("Star Recomposition"), "", [this](){
        openStarRecompositionDialog();
    });
    addMenuAction(chanMenu, tr("Perfect Palette Picker"), "", [this](){
        openPerfectPaletteDialog();
    });
    addMenuAction(chanMenu, tr("Debayer"), "", [this](){
        openDebayerDialog();
    });
    addMenuAction(chanMenu, tr("Continuum Subtraction"), "", [this](){
        openContinuumSubtractionDialog();
    });

    // --- E. Utilities ---
    QMenu* utilMenu = processMenu->addMenu(tr("Utilities"));
    addMenuAction(utilMenu, tr("Plate Solving"), "", [this](){
        openPlateSolvingDialog();
    });
    addMenuAction(utilMenu, tr("Pixel Math"), "", [this](){
        openPixelMathDialog();
    });
    addMenuAction(utilMenu, tr("Star Analysis"), "", [this](){
        openStarAnalysisDialog(); // Call handles checks
    });
    addMenuAction(utilMenu, tr("Wavescale HDR"), "", [this](){
        openWavescaleHDRDialog();
    });
    addMenuAction(utilMenu, tr("FITS Header Editor"), "", [this](){
         openHeaderEditorDialog();
    });
    addMenuAction(utilMenu, tr("Image Annotator"), "", [this](){
        openImageAnnotatorDialog();
    });
    addMenuAction(utilMenu, tr("Correction Brush"), "", [this](){
        openCorrectionBrushDialog();
    });
    addMenuAction(utilMenu, tr("CLAHE"), "", [this](){
        openClaheDialog();
    });
    addMenuAction(utilMenu, tr("Aberration Inspector (9-Points)"), "", [this](){
        openAberrationInspectorDialog();
    });

    // --- F. Effects ---
    QMenu* effectMenu = processMenu->addMenu(tr("Effects"));
    addMenuAction(effectMenu, tr("AstroSpike (Diffraction Spikes)"), "", [this](){
        openAstroSpikeDialog();
    });

    processBtn->setMenu(processMenu);
    processBtn->setStyleSheet(
        "QToolButton { "
        "   color: #e0e0e0; "
        "   border: 1px solid #555; "
        "   border-radius: 3px; "
        "   background-color: #2b2b2b; "
        "   padding: 3px 12px; "
        "} "
        "QToolButton:hover { "
        "   background-color: #3a3a3a; "
        "   border-color: #666; "
        "} "
        "QToolButton::menu-indicator { image: none; }"
    );
    mainToolbar->addWidget(processBtn);
    mainToolbar->addSeparator();

    // --- Stacking Menu ---
    QToolButton* stackBtn = new QToolButton(this);
    stackBtn->setText(tr("Stacking"));
    stackBtn->setPopupMode(QToolButton::InstantPopup);
    stackBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    stackBtn->setStyleSheet(processBtn->styleSheet());

    QMenu* stackMenu = new QMenu(this);
    stackMenu->setStyleSheet(processMenu->styleSheet());
    
    // Project Management
    addMenuAction(stackMenu, tr("New Project..."), "", &MainWindow::openNewProjectDialog);
    addMenuAction(stackMenu, tr("Open Project..."), "", &MainWindow::openExistingProject);
    stackMenu->addSeparator();
    
    // Conversion
    addMenuAction(stackMenu, tr("Convert RAW to FITS..."), "", &MainWindow::openConvertDialog);
    stackMenu->addSeparator();
    
    // Pipeline
    addMenuAction(stackMenu, tr("Preprocessing (Calibration)..."), "", &MainWindow::openPreprocessingDialog);
    addMenuAction(stackMenu, tr("Registration (Star Alignment)..."), "", &MainWindow::openRegistrationDialog);
    addMenuAction(stackMenu, tr("Stacking..."), "", &MainWindow::openStackingDialog);
    stackMenu->addSeparator();
    
    // Scripts
    addMenuAction(stackMenu, tr("Run Script..."), "", &MainWindow::openScriptDialog);
    
    stackBtn->setMenu(stackMenu);
    mainToolbar->addWidget(stackBtn);
    mainToolbar->addSeparator();

    // --- Mask Menu ---
    QToolButton* maskBtn = new QToolButton(this);
    maskBtn->setText(tr("Mask"));
    maskBtn->setPopupMode(QToolButton::InstantPopup);
    maskBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    maskBtn->setStyleSheet(processBtn->styleSheet()); // Re-use style

    QMenu* maskMenu = new QMenu(this);
    maskMenu->setStyleSheet(processMenu->styleSheet()); // Re-use style
    
    addMenuAction(maskMenu, tr("Create Mask..."), "", &MainWindow::createMaskAction);
    addMenuAction(maskMenu, tr("Apply Mask..."), "", &MainWindow::applyMaskAction);
    maskMenu->addSeparator();
    addMenuAction(maskMenu, tr("Remove Mask"), "", &MainWindow::removeMaskAction);
    addMenuAction(maskMenu, tr("Invert Mask"), "", &MainWindow::invertMaskAction);
    maskMenu->addSeparator();
    
    QAction* toggleOverlayAct = maskMenu->addAction(tr("Show Overlay"));
    toggleOverlayAct->setCheckable(true);
    toggleOverlayAct->setChecked(true); 
    connect(toggleOverlayAct, &QAction::triggered, this, &MainWindow::toggleMaskOverlayAction);
    
    maskBtn->setMenu(maskMenu);
    mainToolbar->addWidget(maskBtn);
    mainToolbar->addSeparator();

    // --- View Menu ---
    QToolButton* viewBtn = new QToolButton(this);
    viewBtn->setText(tr("View"));
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    viewBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    viewBtn->setStyleSheet(processBtn->styleSheet()); // Re-use style

    QMenu* viewMenu = new QMenu(this);
    viewMenu->setStyleSheet(processMenu->styleSheet()); // Re-use style
    
    addMenuAction(viewMenu, tr("Tile Images"), "", [this](){
        tileImageViews();
    });
    
    viewBtn->setMenu(viewMenu);
    mainToolbar->addWidget(viewBtn);
    mainToolbar->addSeparator();

    // --- Settings Button ---
    QToolButton* settingsBtn = new QToolButton(this);

    settingsBtn->setText(tr("Settings"));
    settingsBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // Remove arrow styling adjustments as we have no menu
    settingsBtn->setStyleSheet(
        "QToolButton { "
        "   color: #e0e0e0; "
        "   border: 1px solid #555; "
        "   border-radius: 3px; "
        "   background-color: #2b2b2b; "
        "   padding: 3px 12px; " /* Balanced padding */
        "} "
        "QToolButton:hover { "
        "   background-color: #3a3a3a; "
        "   border-color: #666; "
        "} "
    );
    
    // Direct connection to dialog
    connect(settingsBtn, &QToolButton::clicked, this, &MainWindow::onSettingsAction);
    
    mainToolbar->addWidget(settingsBtn);
    mainToolbar->addSeparator();

    // Help Button
    QToolButton* helpBtn = new QToolButton(this);
    helpBtn->setText(tr("Help"));
    helpBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    helpBtn->setStyleSheet(settingsBtn->styleSheet());
    connect(helpBtn, &QToolButton::clicked, this, [this](){
        HelpDialog dlg(this);
        
        // Manual Centering to match other tools
        dlg.adjustSize();
        QSize dlgSize = dlg.size();
        QRect mainGeom = this->geometry();
        QPoint center = mainGeom.center();
        int x = center.x() - dlgSize.width() / 2;
        int y = center.y() - dlgSize.height() / 2;
        
        // Screen bounds check
        if (auto scr = this->screen()) {
             QRect screenGeom = scr->availableGeometry();
             if (x < screenGeom.left()) x = screenGeom.left();
             if (y < screenGeom.top()) y = screenGeom.top();
        }
        
        dlg.move(x, y);
        dlg.exec();
    });
    mainToolbar->addWidget(helpBtn);
    
    mainToolbar->addSeparator();

    QToolButton* aboutBtn = new QToolButton(this);
    aboutBtn->setText(tr("About"));
    aboutBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    aboutBtn->setStyleSheet(settingsBtn->styleSheet()); // Re-use style
    connect(aboutBtn, &QToolButton::clicked, this, [this](){
        AboutDialog dlg(this, TStar::getVersion(), __DATE__); // Only Date
        
        // Manual Centering
        dlg.adjustSize();
        QSize dlgSize = dlg.size();
        QRect mainGeom = this->geometry();
        QPoint center = mainGeom.center();
        int x = center.x() - dlgSize.width() / 2;
        int y = center.y() - dlgSize.height() / 2;
        
         // Screen bounds check
        if (auto scr = this->screen()) {
             QRect screenGeom = scr->availableGeometry();
             if (x < screenGeom.left()) x = screenGeom.left();
             if (y < screenGeom.top()) y = screenGeom.top();
        }
        
        dlg.move(x, y);
        dlg.exec();
    });
    mainToolbar->addWidget(aboutBtn);

    // === Auto-Updater ===
    QTimer::singleShot(2000, this, [this](){
        QSettings settings;
        if (!settings.value("general/check_updates", true).toBool()) {
            return;
        }

        UpdateChecker* checker = new UpdateChecker(this);
        connect(checker, &UpdateChecker::updateAvailable, this, [this](const QString& ver, const QString& body, const QString& url){
            log(tr("New version found: %1").arg(ver), Log_Success, true);
            UpdateDialog dlg(this, ver, body, url);
            dlg.exec(); 
        });
        connect(checker, &UpdateChecker::noUpdateAvailable, this, [](){
            // log(tr("TStar is up to date."), Log_Info);
        });
        connect(checker, &UpdateChecker::errorOccurred, this, [this](const QString& err){
            log(tr("Update check failed: %1").arg(err), Log_Warning);
        });
        checker->checkForUpdates(TStar::getVersion());
    });

    resize(1280, 800);
    log(tr("Application Ready."));
}

void MainWindow::tileImageViews() {
    // Collect only image views (not tool windows)
    QList<CustomMdiSubWindow*> imageWindows;
    for (auto* sub : m_mdiArea->subWindowList()) {
        auto* csw = qobject_cast<CustomMdiSubWindow*>(sub);
        if (csw && !csw->isToolWindow() && csw->viewer()) {
            imageWindows.append(csw);
        }
    }
    
    int count = imageWindows.size();
    if (count < 2) {
        log(tr("Need at least 2 images to tile."), Log_Warning, true);
        return;
    }
    
    // Calculate layout grid
    QRect area = m_mdiArea->viewport()->rect();
    int cols, rows;
    
    if (count == 2) {
        cols = 2; rows = 1;  // Side by side (left/right)
    } else if (count <= 4) {
        cols = 2; rows = 2;  // 2x2 grid
    } else {
        // For more than 4, use a square-ish grid
        cols = std::ceil(std::sqrt((double)count));
        rows = std::ceil((double)count / cols);
    }
    
    int cellW = area.width() / cols;
    int cellH = area.height() / rows;
    
    // Position each window in its cell
    int idx = 0;
    for (int r = 0; r < rows && idx < count; ++r) {
        for (int c = 0; c < cols && idx < count; ++c) {
            auto* win = imageWindows[idx++];
            win->showNormal();  // De-maximize if needed
            win->setGeometry(c * cellW, r * cellH, cellW, cellH);
            
            // Fit image to new window size
            if (auto* v = win->viewer()) {
                QTimer::singleShot(50, v, &ImageViewer::fitToWindow);
            }
        }
    }
    
    log(tr("Tiled %1 images in %2x%3 layout.").arg(count).arg(cols).arg(rows), Log_Success, true);
}

void MainWindow::pushUndo() {

    if (auto v = currentViewer()) {
        log("Pushing Undo State...", Log_Info); 
        v->pushUndo();
        updateMenus();
        log(QString("Undo Stack Size: %1").arg(v->canUndo() ? "Active" : "Empty"), Log_Info);
    } else {
        log("Push Undo Failed: No Active Viewer", Log_Warning);
    }
}

void MainWindow::undo() {
    log("Undo triggered", Log_Action);
    if (auto v = currentViewer()) {
        log(QString("Undo stack size: %1").arg(v->canUndo() ? "has items" : "empty"), Log_Info);
        v->undo();
        updateMenus();
        log("Undo performed", Log_Success);
    } else {
        log("No active viewer for undo", Log_Warning);
    }
}

void MainWindow::redo() {
    log("Redo triggered", Log_Action);
    if (auto v = currentViewer()) {
        log(QString("Redo stack size: %1").arg(v->canRedo() ? "has items" : "empty"), Log_Info);
        v->redo();
        updateMenus();
        log("Redo performed", Log_Success);
    } else {
        log("No active viewer for redo", Log_Warning);
    }
}

void MainWindow::updateMenus() {
    auto v = currentViewer();
    bool canUndo = v && v->canUndo();
    bool canRedo = v && v->canRedo();
    m_undoAction->setEnabled(canUndo);
    m_redoAction->setEnabled(canRedo);
}

ImageViewer* MainWindow::currentViewer() const {
    return m_lastActiveImageViewer;
}

bool MainWindow::hasImage() const {
    ImageViewer* v = currentViewer();
    return v && v->getBuffer().isValid();
}


void MainWindow::createNewImageWindow(const ImageBuffer& buffer, const QString& title, ImageBuffer::DisplayMode mode) {
    ImageViewer* viewer = new ImageViewer(this); // Parent is temporary
    
    // Sync with current toolbar state, but allow override if mode is NOT linear (or if explicitly requested)
    // Use specific mode if passed, otherwise default logic handles it.
    
    // In this app, Display_Linear is index 0. 
    // Change: caller can pass the desired mode.
    viewer->setDisplayState(mode, m_displayLinked); 
    viewer->setBuffer(buffer, title);
    
    // Connect History Sync (live update of Undo/Redo menus)
    connect(viewer, &ImageViewer::historyChanged, this, &MainWindow::updateMenus);
    
    // Cleanup state maps on destruction to avoid dangling pointers
    connect(viewer, &QObject::destroyed, this, [this, viewer](){
        m_ghsStates.remove(viewer);
        m_curvesStates.remove(viewer);
        m_satStates.remove(viewer);
    });
    
    // Connect Signals
    connect(viewer, &ImageViewer::pixelInfoUpdated, this, &MainWindow::updatePixelInfo);


    
    connect(viewer, &ImageViewer::viewChanged, this, &MainWindow::propagateViewChange);
    connect(viewer, &ImageViewer::requestNewView, this, [this](const ImageBuffer& img, const QString& title){
        createNewImageWindow(img, title);
    });

    // Create Custom SubWindow
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    sub->setWidget(viewer); // This wraps it in our custom container
    sub->setSubWindowTitle(title); // Sets custom title bar text
    
    // Save maximized state of existing windows before adding new one
    // (Qt MDI area may unmaximize windows when activating a non-maximized window)
    QList<QMdiSubWindow*> existingWindows = m_mdiArea->subWindowList();
    QSet<QMdiSubWindow*> wasMaximized;
    for (auto* w : existingWindows) {
        if (w->isMaximized()) {
            wasMaximized.insert(w);
        }
    }
    
    m_mdiArea->addSubWindow(sub);
    sub->show();
    sub->showNormal(); // Ensure new views always open non-maximized
    m_mdiArea->setActiveSubWindow(sub);
    sub->raise();
    
    // Restore maximized state of windows that were maximized before
    for (auto* w : wasMaximized) {
        if (w != sub && !w->isMaximized()) {
            w->showMaximized();
        }
    }


    
    // Cascading Logic (Smart Center + Offset)
    // 1. Get viewport dimensions (actual usable space)
    int areaW = m_mdiArea->viewport()->width();
    int areaH = m_mdiArea->viewport()->height();
    int winW = 800; // Default width
    int winH = 600; // Default height
    
    // 2. Base "Center" position
    int startX = std::max(0, (areaW - winW) / 2);
    int startY = std::max(0, (areaH - winH) / 2);
    
    // 3. Calculate steps based on window count
    // Note: subWindowList() includes the window we just added (count >= 1)
    int count = m_mdiArea->subWindowList().size(); 
    int index = std::max(0, count - 1);
    
    int step = 25; // Cascade offset pixels
    
    // 4. Calculate max steps that fit vertically
    // We want: startY + k*step + winH <= areaH
    int availableH = areaH - winH - startY;
    int maxSteps = (availableH > 0) ? (availableH / step) : 0;
    
    // Avoid division by zero or negative logic
    if (maxSteps < 2) maxSteps = 5; // Fallback if tight space
    
    // 5. Determine cascade position (wrap around if hitting bottom)
    int cascadeIdx = index % (maxSteps + 1);
    int batchIdx = index / (maxSteps + 1);
    
    // 6. Calculate coordinates
    // Shift X slightly for each batch so they don't perfectly overlap previous batches
    int x = startX + (cascadeIdx * step) + (batchIdx * step);
    int y = startY + (cascadeIdx * step);
    
    // 7. Safety Bounds
    if (x + winW > areaW) x = std::max(0, areaW - winW); // Keep within right edge
    if (y + winH > areaH) y = std::max(0, areaH - winH); // Keep within bottom edge
    
    sub->move(x, y);
    
    sub->resize(800, 600);
    viewer->fitToWindow(); // Ensure full image is visible
}

void MainWindow::propagateViewChange(float scale, float hVal, float vVal) {
    // Sender
    ImageViewer* senderViewer = qobject_cast<ImageViewer*>(sender());
    if (!senderViewer) return;
    
    QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList();
    for (QMdiSubWindow* sub : windows) {
        ImageViewer* v = qobject_cast<ImageViewer*>(sub->widget());
        if (!v) v = sub->widget()->findChild<ImageViewer*>();
        
        if (v && v != senderViewer && v->isLinked()) {
            v->blockSignals(true); // Prevent feedback
            v->syncView(scale, hVal, vVal);
            v->blockSignals(false);
        }
    }
}



#include "io/XISFReader.h"

void MainWindow::openFile() {
    QString filter = tr("All Supported (*.fits *.fit *.tiff *.tif *.png *.jpg *.jpeg *.xisf);;") +
                     tr("FITS Files (*.fits *.fit);;") +
                     tr("XISF Files (*.xisf);;") +
                     tr("TIFF Files (*.tiff *.tif);;") +
                     tr("Images (*.png *.jpg *.jpeg)");
    QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open Image(s)"), "", filter);
    
    if (paths.isEmpty()) return;

    for (const QString& path : paths) {
        QFileInfo fi(path);
        QString ext = fi.suffix().toLower();
        QString errorMsg;
        
        // --- FITS / FIT ---
        if (ext == "fits" || ext == "fit") {
            // List extensions first
            QMap<QString, FitsExtensionInfo> exts = FitsLoader::listExtensions(path, &errorMsg);
            
            // If failed to list or empty, try basic single load as fallback
            if (exts.isEmpty()) {
                 ImageBuffer buf;
                 if (FitsLoader::load(path, buf, &errorMsg)) {
                     createNewImageWindow(buf, fi.fileName());
                     log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
                 } else {
                     log(tr("Failed to load %1: %2").arg(fi.fileName()).arg(errorMsg), Log_Error);
                 }
                 continue;
            }

            // Load all extensions found
            bool anyLoaded = false;
            // Iterate in index order (0, 1, 2...)
            // Map is sorted by key (Name), so we collect values and sort by index
            QList<FitsExtensionInfo> sortedExts = exts.values();
            std::sort(sortedExts.begin(), sortedExts.end(), [](const FitsExtensionInfo& a, const FitsExtensionInfo& b){
                return a.index < b.index;
            });

            for (const auto& info : sortedExts) {
                ImageBuffer buf;
                QString extErr;
                if (FitsLoader::loadExtension(path, info.index, buf, &extErr)) {
                    QString name = fi.fileName();
                    if (sortedExts.size() > 1) {
                         name += QString(" [%1]").arg(info.name);
                    }
                    createNewImageWindow(buf, name);
                    anyLoaded = true;
                } else {
                    log(tr("Failed to load extension %1: %2").arg(info.name).arg(extErr), Log_Error);
                }
            }
            
            if (anyLoaded) {
                log(tr("Opened FITS: %1 (%2 extensions)").arg(fi.fileName()).arg(sortedExts.size()), Log_Success, true);
            }
        
        } 
        // --- XISF ---
        else if (ext == "xisf") {
             QList<XISFImageInfo> imgs = XISFReader::listImages(path, &errorMsg);
             
             if (imgs.isEmpty()) {
                 // Fallback to single read
                 ImageBuffer buf;
                 if (XISFReader::read(path, buf, &errorMsg)) {
                     createNewImageWindow(buf, fi.fileName());
                     log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
                 } else {
                     log(tr("Failed to load %1: %2").arg(fi.fileName()).arg(errorMsg), Log_Error);
                 }
                 continue;
             }
             
             bool anyLoaded = false;
             for (const auto& info : imgs) {
                  ImageBuffer buf;
                  QString imgErr;
                  if (XISFReader::readImage(path, info.index, buf, &imgErr)) {
                      QString name = fi.fileName();
                      if (imgs.size() > 1) {
                          // Make name look like "file.xisf [Image_0]" or "file.xisf [Ha]"
                          name += QString(" [%1]").arg(info.name);
                      }
                      createNewImageWindow(buf, name);
                      anyLoaded = true;
                  } else {
                      log(tr("Failed to load XISF image %1: %2").arg(info.name).arg(imgErr), Log_Error);
                  }
             }

             if (anyLoaded) {
                 log(tr("Opened XISF: %1 (%2 images)").arg(fi.fileName()).arg(imgs.size()), Log_Success, true);
             }
        }
        // --- TIFF / Others ---
        else if (ext == "tiff" || ext == "tif") {
             ImageBuffer buf;
             QString dbg;
             bool success = buf.loadTiff32(path, &errorMsg, &dbg);
             if (!success) {
                 // Fallback
                 if (buf.loadStandard(path)) {
                     success = true;
                     errorMsg.clear();
                 }
             }
             if (success) {
                 createNewImageWindow(buf, fi.fileName());
                 log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
             } else {
                 log(tr("Failed to load %1: %2").arg(fi.fileName()).arg(errorMsg), Log_Error);
             }
        } 
        else {
            ImageBuffer buf;
            if (buf.loadStandard(path)) {
                createNewImageWindow(buf, fi.fileName());
                log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
            } else {
                log(tr("Failed to load %1").arg(fi.fileName()), Log_Error);
            }
        }
        
        showConsoleTemporarily(2000);
        QCoreApplication::processEvents(); 
    }
}

void MainWindow::saveFile() {
    ImageViewer* v = currentViewer();
    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    // 1. Get Filename first (Classic Windows Flow)
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, tr("Save Image As"), "", 
        tr("FITS (*.fits);;XISF (*.xisf);;TIFF (*.tif *.tiff);;PNG (*.png);;JPG (*.jpg)"), &selectedFilter);
        
    if (path.isEmpty()) return;
    
    // Infer format
    QString format = "PNG"; 
    if (selectedFilter.contains("FITS")) format = "FITS";
    else if (selectedFilter.contains("XISF")) format = "XISF";
    else if (selectedFilter.contains("TIFF")) format = "TIFF";
    else if (selectedFilter.contains("JPG")) format = "JPG";
    
    if (path.endsWith(".fits", Qt::CaseInsensitive) || path.endsWith(".fit", Qt::CaseInsensitive)) format = "FITS";
    else if (path.endsWith(".xisf", Qt::CaseInsensitive)) format = "XISF";
    else if (path.endsWith(".tiff", Qt::CaseInsensitive) || path.endsWith(".tif", Qt::CaseInsensitive)) format = "TIFF";
    else if (path.endsWith(".png", Qt::CaseInsensitive)) format = "PNG";
    else if (path.endsWith(".jpg", Qt::CaseInsensitive)) format = "JPG";

    // 2. Options Dialog (Modal)
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save Options"));
    QFormLayout* layout = new QFormLayout(&dlg);
    
    QComboBox* depthBox = new QComboBox(&dlg);
    QCheckBox* burnBox = new QCheckBox(tr("Burn Annotations"), &dlg);
    burnBox->setChecked(false);
    
    // Check if annotator is active
    if (m_annotatorDlg && m_annotatorDlg->isVisible()) {
        burnBox->setChecked(true); // Default to true if tool is open
    } else {
        burnBox->setEnabled(false);
        burnBox->setChecked(false);
        burnBox->setToolTip(tr("Open Image Annotator first"));
    }
    
    if (format == "FITS" || format == "TIFF" || format == "XISF") {
        depthBox->addItems({tr("32-bit Float"), tr("32-bit Integer"), tr("16-bit Integer"), tr("8-bit Integer")});
        
        // Disable burning for raw formats (destructive)
        if (format != "TIFF") { // TIFF is debatable (display or data?) let's allow TIFF burn if user wants (8/16bit)
            burnBox->setChecked(false);
            burnBox->setEnabled(false);
            burnBox->setToolTip(tr("Cannot burn annotations into raw data formats (FITS/XISF)"));
        }
    } else {
        depthBox->addItems({tr("8-bit Integer")});
        if (format == "PNG") depthBox->addItem(tr("16-bit Integer"));
    }
    
    layout->addRow(tr("Format:"), new QLabel(format));
    layout->addRow(tr("Bit Depth:"), depthBox);
    layout->addRow(tr(""), burnBox);
    
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    
    if (dlg.exec() != QDialog::Accepted) return;
    
    // --- BURN ANNOTATIONS LOGIC ---
    if (burnBox->isChecked() && m_annotatorDlg) {
         // Render the display image (what user sees)
         QImage displayImg = v->getBuffer().getDisplayImage(v->getDisplayMode(), v->isDisplayLinked());
         
         QPainter p(&displayImg);
         m_annotatorDlg->renderAnnotations(p, QRectF(displayImg.rect()));
         p.end(); 
         
         // Save QImage directly
         if (!displayImg.save(path, format.toLatin1().constData())) {
              QMessageBox::critical(this, tr("Error"), tr("Failed to save image with annotations."));
         } else {
              log(tr("Saved with Annotations: %1").arg(path), Log_Success, true);
              showConsoleTemporarily(2000);
         }
         return; // Skip standard save
    }
    
    // --- STANDARD SAVE ---
    QString dStr = depthBox->currentText();
    ImageBuffer::BitDepth d = ImageBuffer::Depth_8Int;
    
    if (dStr.contains(tr("32-bit Float"))) d = ImageBuffer::Depth_32Float;
    else if (dStr.contains(tr("32-bit Integer"))) d = ImageBuffer::Depth_32Int;
    else if (dStr.contains(tr("16-bit"))) d = ImageBuffer::Depth_16Int;
    else if (dStr.contains(tr("8-bit"))) d = ImageBuffer::Depth_8Int;
    
    QString err;
    if (!v->getBuffer().save(path, format, d, &err)) { // Use viewer buffer
        QMessageBox::critical(this, tr("Error"), tr("Save Failed:\n") + err);
    } else {
        v->setModified(false); // Clear modified flag
        log(tr("Saved: %1").arg(path), Log_Success, true);
        showConsoleTemporarily(2000);
    }
}

void MainWindow::extractChannels() {
    ImageViewer* v = currentViewer();
    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image to extract channels from."));
        return;
    }

    ImageBuffer src = v->getBuffer();
    if (src.channels() < 3) {
        QMessageBox::warning(this, tr("Error"), tr("Image must have at least 3 channels to extract."));
        return;
    }

    std::vector<ImageBuffer> channels = ChannelOps::extractChannels(src);
    if (channels.empty()) {
        log(tr("Failed to extract channels."), Log_Error);
        return;
    }

    QString baseTitle = v->windowTitle();
    QString suffixes[] = { "_R", "_G", "_B" };
    
    for (size_t i = 0; i < channels.size(); ++i) {
        if (i < 3) {
            createNewImageWindow(channels[i], baseTitle + suffixes[i]);
        }
    }
    log("Extracted channels for " + baseTitle, Log_Success);
}

void MainWindow::combineChannels() {
    // Gather all open buffers
    std::vector<ChannelCombinationDialog::ChannelSource> sources;
    QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList();
    
    for (QMdiSubWindow* win : windows) {
        CustomMdiSubWindow* cWin = qobject_cast<CustomMdiSubWindow*>(win);
        if (cWin) {
            ImageViewer* v = cWin->widget()->findChild<ImageViewer*>();
            if (v && v->getBuffer().isValid()) {
                // Currently taking whole buffers.
                sources.push_back({win->windowTitle(), v->getBuffer()});
            }
        }
    }

    if (sources.empty()) {
        QMessageBox::information(this, tr("Info"), tr("No open images to combine."));
        return;
    }

    auto dlg = new ChannelCombinationDialog(sources, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(dlg, &QDialog::accepted, this, [this, dlg](){
        ImageBuffer result = dlg->getResult();
        createNewImageWindow(result, "Combined_RGB");
        log("Channels Combined", Log_Success);
    });
    
    log(tr("Opening Combine Channels Tool..."), Log_Info, true);
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Combine Channels"));
    centerToolWindow(sub);
}



void MainWindow::openStretchDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer || !viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please open an image first."));
        return;
    }
    
    if (m_stretchDlg) {
        log(tr("Activating Statistical Stretch Tool..."), Log_Action, true);
        
        m_stretchDlg->setViewer(viewer);
        
        QWidget* p = m_stretchDlg->parentWidget();
        while (p && !qobject_cast<CustomMdiSubWindow*>(p)) p = p->parentWidget();
        if (auto sub = qobject_cast<CustomMdiSubWindow*>(p)) {
            centerToolWindow(sub); // User requested precise centering
            sub->showNormal();
            sub->raise();
            sub->activateWindow();
        } else {
            m_stretchDlg->show();
            m_stretchDlg->raise();
            m_stretchDlg->activateWindow();
        }
        return;
    }
    
    // Create new
    try {
        m_stretchDlg = new StretchDialog(nullptr); // No parent
        m_stretchDlg->setViewer(viewer);
        m_stretchDlg->setAttribute(Qt::WA_DeleteOnClose, false);
        
        connect(m_stretchDlg, &StretchDialog::applied, this, [this](const QString& msg){
            log(msg, Log_Success, true);
        });

        log(tr("Opening Statistical Stretch..."), Log_Info, true);
        CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
        setupToolSubwindow(sub, m_stretchDlg, tr("Statistical Stretch"));
        sub->resize(500, 550); // Appropriate size for the dialog
        centerToolWindow(sub); // Center after setup like GHS does
        
        // Restoration of Z-Order on accept handled by the generic signal if needed,
        // but StretchDialog now closes on apply.
        connect(m_stretchDlg, &QDialog::accepted, this, [this](){
             // Restore Z-Order: Bring the viewer to front
             if (m_stretchDlg && m_stretchDlg->viewer()) {
                 ImageViewer* v = m_stretchDlg->viewer();
                 QWidget* p = v->parentWidget(); 
                 while (p && !qobject_cast<CustomMdiSubWindow*>(p) && !qobject_cast<QMdiSubWindow*>(p)) {
                     p = p->parentWidget();
                 }
                 
                 if (auto sub = qobject_cast<QMdiSubWindow*>(p)) {
                     sub->raise();
                     sub->activateWindow();
                 }
             }
        });


    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to open Statistical Stretch dialog: %1").arg(e.what()));
        if (m_stretchDlg) {
            delete m_stretchDlg;
            m_stretchDlg = nullptr;
        }
    } catch (...) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to open Statistical Stretch dialog: Unknown error"));
        if (m_stretchDlg) {
            delete m_stretchDlg;
            m_stretchDlg = nullptr;
        }
    }
}



void MainWindow::updateDisplay() {
    ImageViewer* v = currentViewer();
    if (!v) return;
    v->setDisplayState(m_displayMode, m_displayLinked);
}


void MainWindow::cropTool() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image to crop."));
        return;
    }
    
    if (m_cropDlg) {
        m_cropDlg->raise();
        m_cropDlg->activateWindow();
        m_cropDlg->setViewer(v);
        return;
    }
    
    auto dlg = new CropRotateDialog(this);
    m_cropDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setViewer(v); // Sets initial viewer and enters crop mode
    
    log(tr("Opening Rotate / Crop Tool..."), Log_Info, true);
    setupToolSubwindow(nullptr, dlg, tr("Rotate / Crop"));
}

void MainWindow::openAbeDialog() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    v->clearAbePolygons();
    bool isStretched = (m_stretchCombo->currentData().toInt() != ImageBuffer::Display_Linear);

    if (m_abeDlg) {
        m_abeDlg->raise();
        m_abeDlg->activateWindow();
        return;
    }

    auto dlg = new ABEDialog(this, v, v->getBuffer(), isStretched);
    m_abeDlg = dlg; // Track
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("ABE - ") + v->windowTitle());
    centerToolWindow(sub);

    // When ABE applies, it emits a result buffer
    connect(dlg, &ABEDialog::applyResult, [this, v](const ImageBuffer& res) {
        if (!v) return;
        v->pushUndo();
        // Updated: Preserve View!
        v->setBuffer(res, "ABE_Result", true); 
        log(tr("ABE applied."), Log_Success, true);
    });
    
    // Logging connection
    connect(dlg, &ABEDialog::progressMsg, this, [this](const QString& msg){ log(msg, Log_Info); });

    log(tr("Opened ABE Tool."), Log_Action, true);
}

void MainWindow::openWavescaleHDRDialog() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_wavescaleDlg) {
        m_wavescaleDlg->raise();
        m_wavescaleDlg->activateWindow();
        m_wavescaleDlg->setViewer(v);
        return;
    }

    auto dlg = new WavescaleHDRDialog(this, v);
    m_wavescaleDlg = dlg; 
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Wavescale HDR"));
    centerToolWindow(sub);

    connect(dlg, &WavescaleHDRDialog::applyInternal, [this](const ImageBuffer& res) {
        Q_UNUSED(res);
        log(tr("Wavescale HDR applied."), Log_Success);
    });
    
    log(tr("Opened Wavescale HDR Tool."), Log_Action, true);
}



void MainWindow::openSaturationDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_satDlg) {
        m_satDlg->raise();
        m_satDlg->activateWindow();
        m_satDlg->setViewer(viewer);
        return;
    }

    log(tr("Opening Color Saturation tool..."), Log_Action, true);
    
    // SaturationDialog now handles its own state and retargeting
    m_satDlg = new SaturationDialog(this, viewer);
    m_satDlg->setAttribute(Qt::WA_DeleteOnClose);
    
    // Pass 'apply' directly to logger/history only if needed, 
    // but dialog handles undo/buffer internally.
    connect(m_satDlg, &SaturationDialog::applyInternal, this, [this]([[maybe_unused]] const ImageBuffer::SaturationParams& params) {
        log(tr("Saturation applied permanently"), Log_Success, true);
        if (m_satTarget) updateMenus();
    });

    connect(m_satDlg, &QObject::destroyed, this, [this]() {
        m_satDlg = nullptr;
        // Target tracking in MainWindow is less critical now as Dialog handles it,
        // but we keep m_satTarget for consistecy in logic elsewhere if any.
        m_satTarget = nullptr; 
    });
    
    m_satTarget = viewer;

    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, m_satDlg, tr("Color Saturation"));
    centerToolWindow(sub);
}

void MainWindow::openAstroSpikeDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer || !viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please open an image first."));
        return;
    }

    log(tr("Opening AstroSpike Tool..."), Log_Info, true);
    AstroSpikeDialog dlg(viewer, this);
    dlg.exec();
}

void MainWindow::openSCNRDialog() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (v->getBuffer().channels() < 3) {
        log(tr("SCNR requires color image."), Log_Warning);
        return;
    }

    if (m_scnrDlg) {
        m_scnrDlg->raise();
        m_scnrDlg->activateWindow();
        m_scnrDlg->setViewer(v);
        return;
    }

    auto dlg = new SCNRDialog(this);
    m_scnrDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(dlg, &SCNRDialog::apply, this, [this, dlg]() {
        ImageViewer* v = currentViewer();
        if (!v) return;
        float amount = dlg->getAmount();
        int method = dlg->getMethod();
        
        v->pushUndo();
        v->getBuffer().applySCNR(amount, method);
        // Updated: Preserve View!
        v->setBuffer(v->getBuffer(), v->windowTitle(), true);
        log(tr("SCNR applied."), Log_Success, true);
    });
    
    log(tr("Opening SCNR Tool..."), Log_Info, true);
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("SCNR"));
}

// Replaces MainWindow::log
void MainWindow::log(const QString& msg, LogType type, bool autoShow) {
    // 1. Log to File System immediately
    Logger::Level level = Logger::Info;
    QString prefix = "";
    
    switch (type) {
        case Log_Info:    level = Logger::Info; break;
        case Log_Success: level = Logger::Info; prefix = "[SUCCESS] "; break;
        case Log_Warning: level = Logger::Warning; break;
        case Log_Error:   level = Logger::Error; break;
        case Log_Action:  level = Logger::Info; prefix = "[ACTION] "; break;
    }
    
    // Log plain text version to file
    Logger::log(level, prefix + msg, "Console");

    // 2. Log to UI
    if (!m_sidebar) return;
    
    QString color = "white";
    if (type == Log_Success) color = "#90ee90"; // Light Green
    else if (type == Log_Warning) color = "orange";
    else if (type == Log_Error) color = "red";
    else if (type == Log_Action) color = "#add8e6"; // Light Blue
    
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString fullMsg = QString("<span style='color:gray'>[%1]</span> <span style='color:%2'>%3</span>")
                      .arg(timeStr).arg(color).arg(msg);
                      
    m_sidebar->logToConsole(fullMsg);
    // Ensure console is transparent even after updates
    if (QWidget* p = m_sidebar->getPanel("Console")) {
        p->setStyleSheet("background-color: transparent; border: none;");
    }

    // Auto-Show logic
    if (autoShow) {
        showConsoleTemporarily(2000);
    }
}

void MainWindow::showConsoleTemporarily(int durationMs) {
    if (!m_sidebar) return;
    if (!m_sidebar->isAutoOpenEnabled()) return;

    // Set flag and open
    m_isConsoleTempOpen = true;
    m_sidebar->openPanel("Console");
    
    // Timer was created in constructor - just start it
    if (m_tempConsoleTimer) {
        m_tempConsoleTimer->setInterval(durationMs);
        m_tempConsoleTimer->start();
    }
}

void MainWindow::startLongProcess() {
    if (!m_sidebar) return;
    
    // Capture active subwindow to restore it after opening console (User Request: Keep image on top)
    QMdiSubWindow* activeSub = m_mdiArea ? m_mdiArea->activeSubWindow() : nullptr;

    m_wasConsoleOpen = m_sidebar->isExpanded();
    m_sidebar->openPanel("Console");
    
    // Disable auto-close timer if running
    if (m_tempConsoleTimer && m_tempConsoleTimer->isActive()) {
        m_tempConsoleTimer->stop();
    }
    
    // Restore focus/z-order to the image
    if (activeSub && m_mdiArea) {
        m_mdiArea->setActiveSubWindow(activeSub);
    }
}

void MainWindow::endLongProcess() {
    if (!m_sidebar) return;
    
    // If it was closed before we started, we want to auto-close it after a delay
    // We treat this as a "Temporary Open" state now.
    if (!m_wasConsoleOpen) {
        m_isConsoleTempOpen = true;
        showConsoleTemporarily(3000); 
    }
}

void MainWindow::openHeaderDialog() {
    // Reliable check if the user is currently using the sidebar
    if (!m_sidebar) return;
    m_sidebar->openPanel("Header");
    
    // Ensure content is fresh
    ImageViewer* viewer = currentViewer();
    if (viewer && m_headerPanel) {
        m_headerPanel->setMetadata(viewer->getBuffer().metadata());
    }
}

void MainWindow::openHeaderEditorDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    HeaderEditorDialog dlg(viewer, this);
    dlg.exec();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_mdiArea) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-tstar-duplicate")) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
            if (dropEvent->mimeData()->hasFormat("application/x-tstar-duplicate")) {
                QByteArray data = dropEvent->mimeData()->data("application/x-tstar-duplicate");
                bool ok;
                quintptr ptrVal = data.toULongLong(&ok, 16);
                if (ok) {
                    CustomMdiSubWindow* sourceWin = reinterpret_cast<CustomMdiSubWindow*>(ptrVal);
                    if (sourceWin) {
                         ImageViewer* sourceViewer = sourceWin->widget()->findChild<ImageViewer*>();
                         if (sourceViewer) {
                             QString newTitle = generateUniqueTitle(sourceWin->windowTitle());
                             createNewImageWindow(sourceViewer->getBuffer(), newTitle);
                             log(tr("View Duplicated: ") + newTitle, Log_Success);
                         }
                    }
                }
                dropEvent->accept();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::activateTool(const QString& title) {
    for (auto* sub : m_mdiArea->subWindowList()) {
        if (auto* csw = qobject_cast<CustomMdiSubWindow*>(sub)) {
             // Check matches. Note: subWindowTitle might be set, or windowTitle. 
             // CustomMdiSubWindow::subWindowTitle() returns the title set on the bar.
             if (csw->windowTitle() == title) {
                 csw->showNormal();
                 csw->raise();
                 csw->activateWindow();
                 return true;
             }
        }
    }
    return false;
}

QString MainWindow::generateUniqueTitle(const QString& baseTitle) {
    QRegularExpression re("(.*) \\((\\d+)\\)$");
    QRegularExpressionMatch match = re.match(baseTitle);
    
    if (match.hasMatch()) {
        QString prefix = match.captured(1);
        int num = match.captured(2).toInt();
        return QString("%1 (%2)").arg(prefix).arg(num + 1);
    } else {
        return baseTitle + " (2)";
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_sidebar) {
        // Overlay sidebar on the left over the central widget area
        if (centralWidget()) {
            QRect cw = centralWidget()->geometry();
            m_sidebar->move(cw.x(), cw.y());
            m_sidebar->resize(m_sidebar->totalVisibleWidth(), cw.height());
        }
    }
}

void MainWindow::openRARDialog() {
    ImageViewer* v = currentViewer();
    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_rarDlg) {
        m_rarDlg->raise();
        m_rarDlg->activateWindow();
        m_rarDlg->setViewer(v);
        return;
    }

    log(tr("Opening Aberration Remover..."), Log_Info, true);
    auto dlg = new RARDialog(this);
    m_rarDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setViewer(v);

    setupToolSubwindow(nullptr, dlg, tr("Aberration Remover"));
}

void MainWindow::openStarStretchDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_starStretchDlg) {
        m_starStretchDlg->raise();
        m_starStretchDlg->activateWindow();
        m_starStretchDlg->setViewer(viewer);
        return;
    }

    log(tr("Opening Star Stretch..."), Log_Info, true);
    auto dlg = new StarStretchDialog(this, viewer);
    m_starStretchDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    setupToolSubwindow(nullptr, dlg, tr("Star Stretch"));
}

void MainWindow::openStarRecompositionDialog() {
    ImageViewer* v = currentViewer();
    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_starRecompDlg) {
        m_starRecompDlg->raise();
        m_starRecompDlg->activateWindow();
        m_starRecompDlg->setViewer(v);
        return;
    }
    
    log(tr("Opening Star Recomposition..."), Log_Info, true);
    auto dlg = new StarRecompositionDialog(this);
    m_starRecompDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    setupToolSubwindow(nullptr, dlg, tr("Star Recomposition"));
}

void MainWindow::openPerfectPaletteDialog() {
    ImageViewer* v = currentViewer();
    if (m_ppDialog) {
        m_ppDialog->raise();
        m_ppDialog->activateWindow();
        if (v) m_ppDialog->setViewer(v);
        return;
    }
    
    log(tr("Opening Perfect Palette..."), Log_Info, true);
    auto dlg = new PerfectPaletteDialog(this);
    m_ppDialog = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if(v) dlg->setViewer(v); // Set initial viewer

    setupToolSubwindow(nullptr, dlg, tr("Perfect Palette"));
}

void MainWindow::applyGeometry(const QString& op) {
    if (auto v = currentViewer()) {
        // NOTE: Flip and rotate should NOT be saved to undo/redo stack
        // to prevent other tools from resetting rotation state
        
        // Use OpenCV optimized calls directly via Buffer
        if (op == "rot90") v->getBuffer().rotate90();
        else if (op == "rot180") v->getBuffer().rotate180();
        else if (op == "rot270") v->getBuffer().rotate270();
        else if (op == "mirrorX") v->getBuffer().mirrorX();
        else if (op == "mirrorY") v->getBuffer().mirrorY();
        
        updateDisplay();
        log(tr("Geometry applied: ") + op, Log_Success);
    }
}

void MainWindow::applyGeometry(const QString& name, std::function<void(ImageBuffer&)> func) {
    if (auto v = currentViewer()) {
        // NOTE: Flip and rotate should NOT be saved to undo/redo stack
        // to prevent other tools from resetting rotation state
        func(v->getBuffer());
        updateDisplay();
        log(tr("Geometry applied: ") + name, Log_Success);
    }
}

void MainWindow::openGHSDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_ghsDlg) {
        log(tr("Activating GHS Tool..."), Log_Action, true);
        if (m_ghsTarget != viewer) {
            m_ghsTarget = viewer;
        }

        // If it's already open, raise its window
        QWidget* p = m_ghsDlg->parentWidget();
        while (p && !qobject_cast<CustomMdiSubWindow*>(p)) p = p->parentWidget();
        CustomMdiSubWindow* sub = qobject_cast<CustomMdiSubWindow*>(p);
        
        if (sub) {
            sub->showNormal();
            sub->raise();
            sub->activateWindow();
            m_ghsDlg->onReset();
            if (viewer) m_ghsDlg->setTarget(viewer);
            return;
        }
        m_ghsDlg->show();
        m_ghsDlg->raise();
        m_ghsDlg->activateWindow();
         m_ghsDlg->onReset();
        if (viewer) m_ghsDlg->setTarget(viewer);
        return;
    }
    
    m_ghsTarget = viewer;
    
    // Clean up if the target view is closed
    connect(viewer, &QObject::destroyed, this, [this, viewer](){
        if (m_ghsTarget == viewer) {
            m_ghsTarget = nullptr;
            if (m_ghsDlg) m_ghsDlg->setTarget(nullptr);
        }
    });
    
    m_ghsDlg = new GHSDialog(nullptr); // No parent initially, will be reparented by wrapper
    m_ghsDlg->setAttribute(Qt::WA_DeleteOnClose, false); // We manage lifecycle via wrapper
    
    log(tr("Opening GHS Tool..."), Log_Action, true);
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, m_ghsDlg, tr("Generalized Hyperbolic Stretch"));
    sub->resize(450, 650); // GHS specific size override
    centerToolWindow(sub); // Re-center after resize
    sub->move(sub->x(), sub->y() - 50); // Adjust vertical centering

     
    // Lifecycle: Delete on close to ensure clean reset on reopen.
    sub->setAttribute(Qt::WA_DeleteOnClose); 
    
    // Connect GHS Dialog destroyed signal
    connect(m_ghsDlg, &QObject::destroyed, [this](){ 
        m_ghsDlg = nullptr; 
        m_ghsTarget = nullptr;
    });

    connect(m_ghsDlg, &GHSDialog::finished, [sub](int){
        // If dialog calls 'done' (e.g. via Close button if it had one), close the subwindow
        if (sub) sub->close();
    });

    connect(m_ghsDlg, &GHSDialog::applied, this, [this](const QString& msg){
        log(msg, Log_Success, true);
    });
    
    if (viewer) m_ghsDlg->setTarget(viewer);
    m_ghsDlg->onReset();
    
    log(tr("Opened GHS Tool."), Log_Action, true);
}

void MainWindow::openPlateSolvingDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    auto dlg = new PlateSolvingDialog(nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose, false);
    dlg->setViewer(viewer);  // Use setViewer instead of setImageBuffer to ensure WCS is applied to viewer
    
    log(tr("Opening Plate Solving..."), Log_Info, true);
    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Plate Solving"));
    sub->resize(sub->width(), sub->height() + 150);
    centerToolWindow(sub);
    
    connect(dlg, &QDialog::accepted, [this, dlg, sub, viewer](){
        if (dlg->isSolved()) {
            NativeSolveResult res = dlg->result();
            if (res.success) {
                // Apply WCS to metadata
                ImageBuffer::Metadata meta = viewer->getBuffer().metadata();
                meta.ra = res.crval1;
                meta.dec = res.crval2;
                meta.crpix1 = res.crpix1;
                meta.crpix2 = res.crpix2;
                meta.cd1_1 = res.cd[0][0];
                meta.cd1_2 = res.cd[0][1];
                meta.cd2_1 = res.cd[1][0];
                meta.cd2_2 = res.cd[1][1];
                
                meta.catalogStars = res.catalogStars;
                
                pushUndo(); // Save undo state before modifying metadata
                viewer->getBuffer().setMetadata(meta);
                log(tr("Plate Solved! Center: RA %1, Dec %2").arg(meta.ra).arg(meta.dec), Log_Success, true);
            }
        }
        sub->close();
    });
}

void MainWindow::openPCCDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_pccDlg) {
        m_pccDlg->raise();
        m_pccDlg->activateWindow();
        m_pccDlg->setViewer(viewer);
        return;
    }
    
    auto dlg = new PCCDialog(viewer, nullptr);
    m_pccDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    log(tr("Opening Photometric Color Calibration..."), Log_Info, true);
    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Photometric Color Calibration"));
    // Let layout settle, then shrink to fit content
    QTimer::singleShot(50, sub, [sub](){ sub->adjustSize(); });
    
    connect(dlg, &QDialog::accepted, [this, dlg, sub, viewer](){
         PCCResult res = dlg->result();
         if (res.valid) {
             pushUndo();
             viewer->setBuffer(viewer->getBuffer(), viewer->windowTitle(), true); // Refresh display
             updateDisplay();
             log(tr("PCC Applied: R=%1 G=%2 B=%3 (BG: %4, %5, %6)")
                 .arg(res.R_factor).arg(res.G_factor).arg(res.B_factor)
                 .arg(res.bg_r).arg(res.bg_g).arg(res.bg_b), Log_Success, true);
         }
         sub->close();
    });
}

void MainWindow::openCurvesDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_curvesDlg) {
        log(tr("Activating Curves Tool..."), Log_Action, true);
        if (m_curvesTarget != viewer) {
            if (m_curvesTarget) m_curvesTarget->clearPreviewLUT();
            m_curvesTarget = viewer;
        }
        // Reuse logic similar to GHS...
        QWidget* p = m_curvesDlg->parentWidget();
        while (p && !qobject_cast<CustomMdiSubWindow*>(p)) p = p->parentWidget();
        CustomMdiSubWindow* sub = qobject_cast<CustomMdiSubWindow*>(p);
        if (sub) {
            sub->showNormal();
            sub->raise();
            sub->activateWindow();
            if (viewer) m_curvesDlg->setViewer(viewer);
            return;
        }
        m_curvesDlg->show();
        m_curvesDlg->raise();
        m_curvesDlg->activateWindow();
        if (viewer) m_curvesDlg->setViewer(viewer);
        return;
    }
    
    m_curvesTarget = viewer;
    
    // Clean up if the target view is closed
    connect(viewer, &QObject::destroyed, this, [this, viewer](){
        if (m_curvesTarget == viewer) {
            m_curvesTarget = nullptr;
            if (m_curvesDlg) m_curvesDlg->setViewer(nullptr);
        }
    });
    
    m_curvesDlg = new CurvesDialog(nullptr); // will be reparented
    m_curvesDlg->setAttribute(Qt::WA_DeleteOnClose, false);
    connect(m_curvesDlg, &QObject::destroyed, [this](){ 
        if (m_curvesTarget) m_curvesTarget->clearPreviewLUT();
        m_curvesDlg = nullptr; 
        m_curvesTarget = nullptr;
    });
    
    // Connect signals...
    connect(m_curvesDlg, &CurvesDialog::previewRequested, this, &MainWindow::applyCurvesPreview);
    connect(m_curvesDlg, &CurvesDialog::applyRequested, this, &MainWindow::applyCurves);

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, m_curvesDlg, tr("Curves Transformation"));
    sub->resize(650, 500);
    
    // Connect finished signal to clear preview and close subwindow
    connect(m_curvesDlg, &QDialog::finished, [this, sub](int){
        if (m_curvesTarget) {
            m_curvesTarget->clearPreviewLUT(); // Clear preview when closing
        }
        sub->close();
    });
    
    if (viewer) m_curvesDlg->setViewer(viewer);
    
    log(tr("Opened Curves Tool."), Log_Action, true);
}

void MainWindow::applyCurvesPreview(const std::vector<std::vector<float>>& lut) {
    if (m_curvesTarget) {
        if (lut.empty()) {
            m_curvesTarget->clearPreviewLUT();
        } else {
            m_curvesTarget->setPreviewLUT(lut);
        }
    }
}

void MainWindow::applyCurves(const SplineData& spline, const bool channels[3]) {
    if (m_curvesTarget) {
        ImageBuffer& buf = m_curvesTarget->getBuffer();
        m_curvesTarget->pushUndo();
        
        buf.applySpline(spline, channels);
        
        QImage newImg = buf.getDisplayImage(m_curvesTarget->getDisplayMode(), m_curvesTarget->isLinked());
        m_curvesTarget->setImage(newImg, true);
        
        log(tr("Curves applied to %1.").arg(m_curvesTarget->windowTitle()), Log_Success, true);
    }
}

// ============================================================================
// Background Neutralization
// ============================================================================
void MainWindow::openBackgroundNeutralizationDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please open an image first."));
        return;
    }
    
    if (viewer->getBuffer().channels() != 3) {
        QMessageBox::warning(this, tr("RGB Required"), tr("Background Neutralization requires an RGB image."));
        return;
    }
    
    // Check if already open
    if (m_bnDlg) {
        m_bnDlg->raise();
        m_bnDlg->activateWindow();
        if (viewer) m_bnDlg->setViewer(viewer);
        return;
    }

    auto dlg = new BackgroundNeutralizationDialog(this);
    m_bnDlg = dlg;
    
    // Cleanup on destroy
    connect(dlg, &QObject::destroyed, this, [this](){
        m_bnDlg = nullptr;
    });

    // Initial Setup
    if (auto v = currentViewer()) {
        m_bnDlg->setViewer(v);
        m_bnDlg->setInteractionEnabled(true);
    }
    
    setupToolSubwindow(nullptr, dlg, tr("Background Neutralization")); // Use helper to create window
    
    connect(dlg, &BackgroundNeutralizationDialog::apply, [this](const QRect& rect) {
        ImageViewer* v = currentViewer();
        if (!v) return;
        
        // Validate buffer before processing to prevent crashes
        if (!v->getBuffer().isValid() || v->getBuffer().channels() != 3) {
            QMessageBox::warning(this, tr("Error"), tr("Cannot apply: buffer is invalid or not RGB."));
            return;
        }
        
        // Capture buffer for undo before processing
        v->pushUndo();
        ImageBuffer buf = v->getBuffer();
        BackgroundNeutralizationDialog::neutralizeBackground(buf, rect);
        
        // Updated: Preserve View!
        v->setBuffer(buf, v->windowTitle(), true); 
        log(tr("Background Neutralization applied."), Log_Success, true);
    });
}
    

// ============================================================================
// Pixel Math
// ============================================================================
void MainWindow::openPixelMathDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please open an image first."));
        return;
    }
    
    // Check if tool already open
    if (m_pixelMathDialog) {
        log(tr("Activating PixelMath Tool..."), Log_Action, true);
         QWidget* p = m_pixelMathDialog->parentWidget();
         while (p && !qobject_cast<CustomMdiSubWindow*>(p)) p = p->parentWidget();
         if (auto s = qobject_cast<CustomMdiSubWindow*>(p)) { 
             s->showNormal();
             s->raise(); 
             s->activateWindow(); 
             if (viewer) m_pixelMathDialog->setViewer(viewer);
             return; 
         }
        // If independent window
        m_pixelMathDialog->show();
        m_pixelMathDialog->raise();
        m_pixelMathDialog->activateWindow();
        if (viewer) m_pixelMathDialog->setViewer(viewer);
        return;
    }
    
    m_pixelMathDialog = new PixelMathDialog(this, nullptr); 
    m_pixelMathDialog->setAttribute(Qt::WA_DeleteOnClose, true);

    
    // Logic setup...
    connect(m_pixelMathDialog, &PixelMathDialog::apply, [this](const QString& expr, bool rescale){
         ImageViewer* v = currentViewer();
         if(v) {
             pushUndo();
             QString err;
             if (PixelMathDialog::evaluateExpression(expr, v->getBuffer(), rescale, &err)) {
                 updateActiveImage();
                 log(tr("PixelMath Applied") + QString(rescale ? tr(" (Rescaled)") : "") + ": " + expr.left(30) + "...", Log_Success, true);
             } else {
                 QMessageBox::critical(this, tr("PixelMath Error"), err);
                 undo(); // Revert
             }
         }
    });

    setupToolSubwindow(nullptr, m_pixelMathDialog, tr("Pixel Math"));
    if (viewer) m_pixelMathDialog->setViewer(viewer);
}


CustomMdiSubWindow* MainWindow::setupToolSubwindow(CustomMdiSubWindow* sub, QWidget* dlg, const QString& title) {
    CustomMdiSubWindow* targetSub = sub;
    if (!targetSub) {
        targetSub = new CustomMdiSubWindow(m_mdiArea);
        targetSub->setAttribute(Qt::WA_DeleteOnClose);
    }

    targetSub->setWidget(dlg);
    targetSub->setSubWindowTitle(title);
    targetSub->setToolWindow(true); // Enable tool mode
    
    // Get the dialog's preferred size BEFORE adjustSize modifies it
    // If the dialog has explicitly called resize(), use that size
    QSize preferredSize;
    if (dlg) {
        preferredSize = dlg->size();
        // If dialog hasn't been explicitly sized, fall back to sizeHint
        if (preferredSize.width() < 100 || preferredSize.height() < 100) {
            preferredSize = dlg->sizeHint();
        }
    }
    
    // Add space for CustomMdiSubWindow chrome (title bar, borders)
    int titleBarH = 30; // Approximate title bar height
    int borderW = 6;    // Approximate border width
    
    QSize subSize;
    if (preferredSize.isValid() && preferredSize.width() >= 100 && preferredSize.height() >= 100) {
        subSize = QSize(preferredSize.width() + borderW, preferredSize.height() + titleBarH + borderW);
    } else {
        // Fallback to adjustSize if dialog has no valid size
        if (dlg) dlg->adjustSize();
        targetSub->adjustSize();
        subSize = targetSub->size();
        if (subSize.width() < 100 || subSize.height() < 100) {
            subSize = QSize(500, 400);
        }
    }
    
    targetSub->resize(subSize);

    // Center on main window geometry (more intuitive when image is maximized)
    const QRect mainGeom = this->geometry();
    const QPoint center = mainGeom.center();
    
    int x = center.x() - subSize.width() / 2;
    int y = center.y() - subSize.height() / 2;
    
    // Ensure it stays on screen
    const QRect screen = this->screen()->availableGeometry();
    if (x < screen.left()) x = screen.left();
    if (y < screen.top()) y = screen.top();
    if (x + subSize.width() > screen.right()) x = screen.right() - subSize.width();
    if (y + subSize.height() > screen.bottom()) y = screen.bottom() - subSize.height();
    
    targetSub->move(x, y);
    
    if (QDialog* qdlg = qobject_cast<QDialog*>(dlg)) {
        connect(qdlg, &QDialog::finished, [targetSub](int){ targetSub->close(); });
    }
    
    targetSub->showNormal();
    targetSub->show();
    targetSub->raise();
    targetSub->activateWindow();
    
    centerToolWindow(targetSub); // Ensure every tool is centered by default
    
    return targetSub;
}

void MainWindow::centerToolWindow(CustomMdiSubWindow* sub) {
    if (!sub) return;
    
    QSize subSize = sub->size();
    const QRect mainGeom = this->geometry();
    const QPoint center = mainGeom.center();
    
    int x = center.x() - subSize.width() / 2;
    int y = center.y() - subSize.height() / 2;
    
    // Ensure it stays on screen
    const QRect screen = this->screen()->availableGeometry();
    if (x < screen.left()) x = screen.left();
    if (y < screen.top()) y = screen.top();
    if (x + subSize.width() > screen.right()) x = screen.right() - subSize.width();
    if (y + subSize.height() > screen.bottom()) y = screen.bottom() - subSize.height();
    
    sub->move(x, y);
}


void MainWindow::onSettingsAction() {
    // If settings dialog already open, just raise and focus it
    if (m_settingsDlg) {
        m_settingsDlg->raise();
        m_settingsDlg->activateWindow();
        return;
    }
    
    auto dlg = new SettingsDialog(this);
    m_settingsDlg = dlg;
    
    connect(dlg, &SettingsDialog::settingsChanged, this, [this](){
        updateActiveImage();
        // Also update all other open viewers since this is a global setting
        for (auto sub : m_mdiArea->subWindowList()) {
            if (auto csw = qobject_cast<CustomMdiSubWindow*>(sub)) {
                if (auto v = csw->viewer()) {
                     v->refreshDisplay(true);
                }
            }
        }
        log(tr("Settings applied. Display refreshed."), Log_Success);
    });
    
    // Clean up when dialog closes
    connect(dlg, &QDialog::destroyed, this, [this]() { m_settingsDlg = nullptr; });

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Settings"));
    sub->resize(sub->width(), sub->height());
    centerToolWindow(sub);
}

void MainWindow::updateActiveImage() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) return;
    viewer->refreshDisplay(true);
}

void MainWindow::openArcsinhStretchDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_arcsinhDlg) {
        m_arcsinhDlg->raise();
        m_arcsinhDlg->activateWindow();
        m_arcsinhDlg->setViewer(viewer);
        return;
    }
    
    log(tr("Opening Arcsinh Stretch Tool..."), Log_Action, true);
    
    auto dlg = new ArcsinhStretchDialog(viewer, nullptr); // Parent null for reparenting
    m_arcsinhDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(dlg, &QDialog::accepted, this, [this, viewer](){
        log(tr("Arcsinh Stretch Applied to %1").arg(viewer->windowTitle()), Log_Success, true);
    });

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Arcsinh Stretch"));
    sub->resize(420, 300);
}

void MainWindow::openHistogramStretchDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_histoDlg) {
         m_histoDlg->raise();
         m_histoDlg->activateWindow();
         m_histoDlg->setViewer(viewer);
         return;
    }

    log(tr("Opening Histogram Transformation..."), Log_Action, true);
    
    auto dlg = new HistogramStretchDialog(viewer, this);
    m_histoDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    // Reset pointer when dialog is destroyed
    connect(dlg, &QObject::destroyed, this, [this](){
        m_histoDlg = nullptr;
    });
    
    // Log when applied
    connect(dlg, &HistogramStretchDialog::applied, this, [this](){
        log(tr("Histogram Transformation applied."), Log_Success, true);
    });
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Histogram Stretch"));
    sub->resize(520, 600);
    centerToolWindow(sub); // Re-center after resize
    sub->move(sub->x(), sub->y() - 50); // Adjust vertical centering
}


void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    startFadeIn();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_isClosing) {
        QMainWindow::closeEvent(event);
        return;
    }
    
    // Check if we are already in the shutdown sequence (re-entrant call or retry)
    bool shutdownRequested = property("shutdownRequested").toBool();
    
    if (!shutdownRequested) {
        QMessageBox::StandardButton res = QMessageBox::question(this, tr("Exit TStar"), tr("Are you sure you want to exit?"), 
                                                                QMessageBox::Yes|QMessageBox::No, QMessageBox::Yes);
        
        if (res != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        setProperty("shutdownRequested", true);
    }

    // === FORCE CLEANUP ===
    Threading::setThreadRun(false);
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone(500);

    // Attempt to close all MDI windows first
    if (m_mdiArea) {
        // Disable animations to ensure closeAllSubWindows doesn't stop
        for (auto* sub : m_mdiArea->subWindowList()) {
            if (auto* csw = qobject_cast<CustomMdiSubWindow*>(sub)) {
                csw->setSkipCloseAnimation(true);
            }
        }

        m_mdiArea->closeAllSubWindows();
        
        // Wait for animations to finish (Async close)
        // Give time for fade-out effects (150ms approx) + buffer
        // If user canceled a save prompt, the window remains open and loop terminates after timeout
        QTime deadline = QTime::currentTime().addMSecs(600);
        while (!m_mdiArea->subWindowList().isEmpty() && QTime::currentTime() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(10);
        }
        
        // If still open windows (e.g. user said NO to unsaved changes)
        if (!m_mdiArea->subWindowList().isEmpty()) {
            event->ignore();
            // Reset shutdown flag so user can try again
            setProperty("shutdownRequested", false);
            return;
        }
    }

    if (m_isClosing) {
        QMainWindow::closeEvent(event);
        return;
    }
    
    event->ignore();
    startFadeOut();
}

void MainWindow::startFadeIn() {
    if (m_anim) {
        m_anim->stop();
        delete m_anim;
    }
    setWindowOpacity(0.0);
    m_anim = new QPropertyAnimation(this, "windowOpacity", this);
    m_anim->setDuration(250);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutQuad);
    m_anim->start();
    m_anim->setProperty("type", "fadein"); 
}

void MainWindow::startFadeOut() {
    if (m_anim) {
        m_anim->stop();
        delete m_anim;
    }
    m_anim = new QPropertyAnimation(this, "windowOpacity", this);
    m_anim->setDuration(250);
    m_anim->setStartValue(windowOpacity());
    m_anim->setEndValue(0.0);
    m_anim->setEasingCurve(QEasingCurve::InQuad);
    connect(m_anim, &QPropertyAnimation::finished, this, [this](){
        m_isClosing = true;
        close();
        // Force exit processes as requested to prevent hanging threads
        std::exit(0); 
    });
    m_anim->start();
}

void MainWindow::openPCCDistributionDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    const PCCResult& res = viewer->getBuffer().metadata().pccResult;
    if (!res.valid) {
        QMessageBox::warning(this, tr("PCC Distribution"), tr("No valid PCC result found for this image.\nPlease run Photometric Color Calibration first."));
        return;
    }

    auto dlg = new PCCDistributionDialog(res, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    setupToolSubwindow(nullptr, dlg, tr("PCC Distribution"));

    log(tr("Opened PCC Distribution Tool"), Log_Action, true);
}
// setupSidebarTools removed - logic moved to Process menu
void MainWindow::setupSidebarTools() {}

void MainWindow::runCosmicClarity(const CosmicClarityParams& params) {
     ImageViewer* v = currentViewer();
     if (!v) return;

     // Force activation of the target window before starting process
     QWidget* p = v->parentWidget();
     while (p && !qobject_cast<QMdiSubWindow*>(p)) p = p->parentWidget();
     if (auto sub = qobject_cast<QMdiSubWindow*>(p)) {
         m_mdiArea->setActiveSubWindow(sub);
     }

     startLongProcess();
     
     QThread* thread = new QThread;
     CosmicClarityRunner* runner = new CosmicClarityRunner;
     runner->moveToThread(thread);
     
     connect(runner, &CosmicClarityRunner::processOutput, this, [this](const QString& msg){ log(msg.trimmed(), Log_Info); });
     
     QProgressDialog* pd = new QProgressDialog(tr("Running Cosmic Clarity..."), tr("Cancel"), 0, 0, this);
     pd->setWindowModality(Qt::WindowModal);
     pd->setMinimumDuration(0);
     pd->show();
     
     connect(pd, &QProgressDialog::canceled, runner, &CosmicClarityRunner::cancel, Qt::DirectConnection);
     
     ImageBuffer input = v->getBuffer();
     
     connect(thread, &QThread::started, runner, [runner, input, params, thread, pd, this]() mutable {
         ImageBuffer result;
         QString err;
         bool success = runner->run(input, result, params, &err);
         
         QMetaObject::invokeMethod(this, [=]() {
             pd->close();
             pd->deleteLater();
             thread->quit();
             thread->wait();
             thread->deleteLater();
             runner->deleteLater();
             
             endLongProcess();
             
             if (success) createNewImageWindow(result, "CC_Result", m_displayMode);
             else if (!err.isEmpty() && err != "Process cancelled by user.") QMessageBox::critical(this, tr("Cosmic Clarity Error"), err);
             else if (err == "Process cancelled by user.") log(tr("Cosmic Clarity cancelled."), Log_Warning);
         });
     });
     thread->start();
}

void MainWindow::runGraXpert(const GraXpertParams& params) {
     ImageViewer* v = currentViewer();
     if (!v) return;
     
     // Force activation of the target window before starting process
     QWidget* p = v->parentWidget();
     while (p && !qobject_cast<QMdiSubWindow*>(p)) p = p->parentWidget();
     if (auto sub = qobject_cast<QMdiSubWindow*>(p)) {
         m_mdiArea->setActiveSubWindow(sub);
     }

     startLongProcess();
     
     QThread* thread = new QThread;
     GraXpertRunner* runner = new GraXpertRunner;
     runner->moveToThread(thread);
     
     connect(runner, &GraXpertRunner::processOutput, this, [this](const QString& msg){ log(msg.trimmed(), Log_Info); });
     
     QProgressDialog* pd = new QProgressDialog(tr("Running GraXpert..."), tr("Cancel"), 0, 0, this);
     pd->setWindowModality(Qt::WindowModal);
     pd->setMinimumDuration(0);
     pd->show();
     
     connect(pd, &QProgressDialog::canceled, runner, &GraXpertRunner::cancel, Qt::DirectConnection);
     
     ImageBuffer input = v->getBuffer();
     
     connect(thread, &QThread::started, runner, [runner, input, params, thread, pd, this]() mutable {
         ImageBuffer result;
         QString err;
         bool success = runner->run(input, result, params, &err);
         
         QMetaObject::invokeMethod(this, [=]() {
             pd->close();
             pd->deleteLater();
             
             thread->quit();
             thread->wait();
             thread->deleteLater();
             runner->deleteLater();
             
             endLongProcess();
             
             if (success) createNewImageWindow(result, "GraXpert_Result", m_displayMode);
             else if (!err.isEmpty() && err != "Process cancelled by user.") QMessageBox::critical(this, tr("GraXpert Error"), err);
             else if (err == "Process cancelled by user.") log(tr("GraXpert cancelled."), Log_Warning);
         });
     });
     
     thread->start();
}

void MainWindow::openStarAnalysisDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_starAnalysisDlg) {
        QWidget* p = m_starAnalysisDlg->parentWidget();
        while (p && !qobject_cast<CustomMdiSubWindow*>(p)) p = p->parentWidget();
        CustomMdiSubWindow* sub = qobject_cast<CustomMdiSubWindow*>(p);
        
        if (sub) {
            sub->showNormal();
            sub->raise();
            sub->activateWindow();
            m_starAnalysisDlg->setViewer(viewer);
            return;
        }
        m_starAnalysisDlg->show();
        m_starAnalysisDlg->raise();
        m_starAnalysisDlg->activateWindow();
        m_starAnalysisDlg->setViewer(viewer);
        return;
    }
    
    auto dlg = new StarAnalysisDialog(nullptr, viewer);
    m_starAnalysisDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    setupToolSubwindow(nullptr, dlg, tr("Star Analysis"));
}


#include "dialogs/MaskGenerationDialog.h"
#include "dialogs/ApplyMaskDialog.h"
#include "core/MaskManager.h"
#include <QInputDialog>

void MainWindow::createMaskAction() {
    if (auto v = currentViewer()) {
        const ImageBuffer& img = v->getBuffer();
        if (!img.isValid()) return;
        
        MaskGenerationDialog dlg(img, this);
        if (dlg.exec() == QDialog::Accepted) {
             MaskLayer mask = dlg.getGeneratedMask();
             
             // Ask for a name to save the mask
             bool ok;
             QString maskName = QInputDialog::getText(this, tr("Save Mask"), 
                                                     tr("Enter a name to save this mask:"), 
                                                     QLineEdit::Normal, 
                                                     mask.name, &ok);
             if (ok && !maskName.isEmpty()) {
                 MaskManager::instance().addMask(maskName, mask);
             }

             // set to current viewer buffer
             v->getBuffer().setMask(mask);
             updateActiveImage();
             log(tr("Mask Created and Applied."), Log_Success);
        }
    } else {
        QMessageBox::warning(this, tr("No Image"), tr("Open an image first."));
    }
}

void MainWindow::applyMaskAction() {
    if (auto v = currentViewer()) {
        ApplyMaskDialog dlg(v->getBuffer().width(), v->getBuffer().height(), this);
        
        // 1. Add Saved Masks
        auto savedMasks = MaskManager::instance().getAllMasks();
        for (auto it = savedMasks.begin(); it != savedMasks.end(); ++it) {
            dlg.addAvailableMask(it.key(), it.value(), false);
        }
        
        // 2. Add Other Open Views
        QList<CustomMdiSubWindow*> windows = m_mdiArea->findChildren<CustomMdiSubWindow*>();
        for (auto w : windows) {
             if (ImageViewer* iv = w->findChild<ImageViewer*>()) {
                 if (iv != v) { // Exclude self
                     const ImageBuffer& src = iv->getBuffer();
                     MaskLayer m;
                     // Extract luma from buffer as mask
                     int tx = src.width();
                     int ty = src.height();
                     std::vector<float> data(tx * ty);
                     const std::vector<float>& sData = src.data();
                     int ch = src.channels();
                     for (int i=0; i<tx*ty; ++i) {
                         if (ch==1) data[i] = sData[i];
                         else data[i] = 0.2126f*sData[i*3] + 0.7152f*sData[i*3+1] + 0.0722f*sData[i*3+2];
                         data[i] = std::clamp(data[i], 0.0f, 1.0f);
                     }
                     m.data = data;
                     m.width = tx;
                     m.height = ty;
                     m.name = src.name();
                     dlg.addAvailableMask(src.name(), m, true);
                 }
             }
        }

        if (dlg.exec() == QDialog::Accepted) {
            MaskLayer mask = dlg.getSelectedMask();
            if (mask.isValid()) {
                int tx = v->getBuffer().width();
                int ty = v->getBuffer().height();
                
                // Resize if dimensions don't match
                if (mask.width != tx || mask.height != ty) {
                    cv::Mat srcMask(mask.height, mask.width, CV_32FC1, mask.data.data());
                    cv::Mat dstMask;
                    cv::resize(srcMask, dstMask, cv::Size(tx, ty), 0, 0, cv::INTER_LINEAR);
                    
                    mask.data.resize(tx * ty);
                    memcpy(mask.data.data(), dstMask.data, mask.data.size() * sizeof(float));
                    mask.width = tx;
                    mask.height = ty;
                    log(tr("Applied mask resized to match image."), Log_Info);
                }
                
                v->getBuffer().setMask(mask);
                updateActiveImage();
                log(tr("Mask Applied: %1").arg(mask.name), Log_Success);
            }
        }
    } else {
        QMessageBox::warning(this, tr("No Image"), tr("Open an image first."));
    }
}

void MainWindow::removeMaskAction() {
    if (auto v = currentViewer()) {
        if (v->getBuffer().hasMask()) {
            v->getBuffer().removeMask();
            updateActiveImage();
            log(tr("Mask Removed."), Log_Info);
        } else {
            log(tr("No mask to remove."), Log_Warning);
        }
    }
}

void MainWindow::invertMaskAction() {
    if (auto v = currentViewer()) {
        if (v->getBuffer().hasMask()) {
            v->getBuffer().invertMask();
            updateActiveImage();
            log(tr("Mask Inverted."), Log_Info);
        }
    }
}

void MainWindow::toggleMaskOverlayAction() {
    if (auto v = currentViewer()) {
        bool current = v->isMaskOverlayEnabled();
        v->setMaskOverlay(!current);
        
        QAction* act = qobject_cast<QAction*>(sender());
        if (act) {
            act->setChecked(!current);
        }
        v->refresh();
    }
}

void MainWindow::openDebayerDialog() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (v->getBuffer().channels() != 1) {
        QMessageBox::information(this, tr("Debayer"), tr("Image already has multiple channels."));
        return;
    }
    
    if (m_debayerDlg) {
        m_debayerDlg->raise();
        m_debayerDlg->activateWindow();
        m_debayerDlg->setViewer(v);
        return;
    }
    
    m_debayerDlg = new DebayerDialog(this);
    log(tr("Opening Debayer..."), Log_Action, true);
    m_debayerDlg->setAttribute(Qt::WA_DeleteOnClose);
    m_debayerDlg->setViewer(v);
    
    connect(m_debayerDlg, &QDialog::destroyed, this, [this]() { m_debayerDlg = nullptr; });
    
    setupToolSubwindow(nullptr, m_debayerDlg, tr("Debayer"));
}

void MainWindow::openContinuumSubtractionDialog() {
    if (m_continuumDlg) {
        m_continuumDlg->raise();
        m_continuumDlg->activateWindow();
        m_continuumDlg->refreshImageList();
        return;
    }
    
    m_continuumDlg = new ContinuumSubtractionDialog(this);
    log(tr("Opening Continuum Subtraction..."), Log_Action, true);
    m_continuumDlg->setAttribute(Qt::WA_DeleteOnClose);
    if (currentViewer()) {
        m_continuumDlg->setViewer(currentViewer());
    }
    
    connect(m_continuumDlg, &QDialog::destroyed, this, [this]() { m_continuumDlg = nullptr; });
    
    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, m_continuumDlg, tr("Continuum Subtraction"));
    centerToolWindow(sub);
}

void MainWindow::openImageAnnotatorDialog() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    if (m_annotatorDlg) {
        m_annotatorDlg->raise();
        m_annotatorDlg->activateWindow();
        m_annotatorDlg->setViewer(v);
        return;
    }
    
    m_annotatorDlg = new AnnotationToolDialog(this);
    
    // CRITICAL: Restore persisted annotations from previous dialog instance
    if (!m_persistedAnnotations.isEmpty()) {
        m_annotatorDlg->setPersistedAnnotations(m_persistedAnnotations);
    }
    

    m_annotatorDlg->setViewer(v);
    
    // Don't delete on close - just hide so annotations persist
    // Note: hideEvent() in AnnotationToolDialog will save annotations to m_persistedAnnotations
    connect(m_annotatorDlg, &QDialog::destroyed, this, [this]() { 
        m_annotatorDlg = nullptr; 
    });
    
    setupToolSubwindow(nullptr, m_annotatorDlg, tr("Annotation Tool"));
}

// Drag and Drop Support
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        // Accept if at least one URL is a supported image format
        for (const QUrl& url : mimeData->urls()) {
            QString path = url.toLocalFile().toLower();
            if (path.endsWith(".fits") || path.endsWith(".fit") ||
                path.endsWith(".xisf") ||
                path.endsWith(".tiff") || path.endsWith(".tif") ||
                path.endsWith(".png") || path.endsWith(".jpg") || path.endsWith(".jpeg")) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            QString path = url.toLocalFile();
            QFileInfo fi(path);
            QString ext = fi.suffix().toLower();
            QString errorMsg;
            
            // FITS - Handle multi-extension files like openFile() does
            if (ext == "fits" || ext == "fit") {
                QMap<QString, FitsExtensionInfo> exts = FitsLoader::listExtensions(path, &errorMsg);
                
                if (exts.isEmpty()) {
                    // Fallback to single load
                    ImageBuffer buf;
                    if (FitsLoader::load(path, buf, &errorMsg)) {
                        createNewImageWindow(buf, fi.fileName());
                        log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
                    } else {
                        log(tr("Failed to load %1: %2").arg(fi.fileName()).arg(errorMsg), Log_Error, true);
                    }
                } else {
                    // Load all extensions
                    QList<FitsExtensionInfo> sortedExts = exts.values();
                    std::sort(sortedExts.begin(), sortedExts.end(), [](const FitsExtensionInfo& a, const FitsExtensionInfo& b){
                        return a.index < b.index;
                    });
                    
                    bool anyLoaded = false;
                    for (const auto& info : sortedExts) {
                        ImageBuffer buf;
                        QString extErr;
                        if (FitsLoader::loadExtension(path, info.index, buf, &extErr)) {
                            QString name = fi.fileName();
                            if (sortedExts.size() > 1) {
                                name += QString(" [%1]").arg(info.name);
                            }
                            createNewImageWindow(buf, name);
                            anyLoaded = true;
                        } else {
                            log(tr("Failed to load extension %1: %2").arg(info.name).arg(extErr), Log_Error);
                        }
                    }
                    
                    if (anyLoaded) {
                        log(tr("Opened FITS: %1 (%2 extensions)").arg(fi.fileName()).arg(sortedExts.size()), Log_Success, true);
                    }
                }
            }
            // XISF - Handle multi-image files like openFile() does
            else if (ext == "xisf") {
                QList<XISFImageInfo> imgs = XISFReader::listImages(path, &errorMsg);
                
                if (imgs.isEmpty()) {
                    // Fallback to single read
                    ImageBuffer buf;
                    if (XISFReader::read(path, buf, &errorMsg)) {
                        createNewImageWindow(buf, fi.fileName());
                        log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
                    } else {
                        log(tr("Failed to load %1: %2").arg(fi.fileName()).arg(errorMsg), Log_Error, true);
                    }
                } else {
                    bool anyLoaded = false;
                    for (const auto& info : imgs) {
                        ImageBuffer buf;
                        QString imgErr;
                        if (XISFReader::readImage(path, info.index, buf, &imgErr)) {
                            QString name = fi.fileName();
                            if (imgs.size() > 1) {
                                name += QString(" [%1]").arg(info.name);
                            }
                            createNewImageWindow(buf, name);
                            anyLoaded = true;
                        } else {
                            log(tr("Failed to load XISF image %1: %2").arg(info.name).arg(imgErr), Log_Error);
                        }
                    }
                    
                    if (anyLoaded) {
                        log(tr("Opened XISF: %1 (%2 images)").arg(fi.fileName()).arg(imgs.size()), Log_Success, true);
                    }
                }
            }
            // TIFF
            else if (ext == "tiff" || ext == "tif") {
                ImageBuffer buf;
                QString dbg;
                bool success = buf.loadTiff32(path, &errorMsg, &dbg);
                if (!success) {
                    // Fallback
                    if (buf.loadStandard(path)) {
                        success = true;
                        errorMsg.clear();
                    }
                }
                if (success) {
                    createNewImageWindow(buf, fi.fileName());
                    log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
                } else {
                    log(tr("Failed to load %1: %2").arg(fi.fileName()).arg(errorMsg), Log_Error, true);
                }
            }
            // PNG/JPG
            else if (ext == "png" || ext == "jpg" || ext == "jpeg") {
                ImageBuffer buf;
                if (buf.loadStandard(path)) {
                    createNewImageWindow(buf, fi.fileName());
                    log(tr("Opened: %1").arg(fi.fileName()), Log_Success, true);
                } else {
                    log(tr("Failed to load %1").arg(fi.fileName()), Log_Error, true);
                }
            }
            
            showConsoleTemporarily(2000);
            QCoreApplication::processEvents();
        }
        event->acceptProposedAction();
    }
}

void MainWindow::openExtractLuminanceDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    log(tr("Opening Extract Luminance..."), Log_Action, true);
    
    auto* dlg = new ExtractLuminanceDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Extract Luminance"));
    sub->resize(400, 350);
    centerToolWindow(sub);
    
    connect(dlg, &QDialog::accepted, this, [this](){
        log(tr("Luminance extracted."), Log_Success, true);
    });
}

void MainWindow::openRecombineLuminanceDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    log(tr("Opening Recombine Luminance..."), Log_Action, true);
    
    auto* dlg = new RecombineLuminanceDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Recombine Luminance"));
    sub->resize(450, 400);
    centerToolWindow(sub);
    
    connect(dlg, &QDialog::accepted, this, [this](){
        log(tr("Luminance recombined."), Log_Success, true);
    });
}

void MainWindow::openCorrectionBrushDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    log(tr("Opening Correction Brush..."), Log_Action, true);
    
    auto* dlg = new CorrectionBrushDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Correction Brush"));
    sub->resize(950, 700);
    centerToolWindow(sub);
    sub->move(sub->x(), sub->y() - 50); // Adjust vertical centering
    
    connect(dlg, &QDialog::accepted, this, [this](){
        log(tr("Correction brush applied."), Log_Success, true);
    });
    
    // Update when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*){
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}

void MainWindow::removePedestal() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) return;
    
    pushUndo();
    ChannelOps::removePedestal(v->getBuffer());
    v->refreshDisplay();
    log(tr("Pedestal removed."), Log_Success, true);
}

void MainWindow::openClaheDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    log(tr("Opening CLAHE..."), Log_Action, true);
    
    auto* dlg = new ClaheDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("CLAHE"));
    sub->resize(850, 650);
    centerToolWindow(sub);
    sub->move(sub->x(), sub->y() - 50); // Adjust vertical centering
    
    connect(dlg, &QDialog::accepted, this, [this](){
        log(tr("CLAHE applied."), Log_Success, true);
    });
    
    // Update when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*){
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}

void MainWindow::openAberrationInspectorDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    log(tr("Opening Aberration Inspector..."), Log_Action, true);
    
    // SAFETY: Check currentViewer() is not nullptr
    if (!currentViewer()) {
        log(tr("No image loaded. Cannot open Aberration Inspector."), Log_Error, true);
        return;
    }
    
    auto* dlg = new AberrationInspectorDialog(currentViewer()->getBuffer(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Aberration Inspector"));
    sub->resize(550, 550);
    centerToolWindow(sub);
    
    // Update when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*){
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}

void MainWindow::openSelectiveColorDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    
    log(tr("Opening Selective Color Correction..."), Log_Action, true);
    
    auto* dlg = new SelectiveColorDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Selective Color Correction"));
    sub->resize(950, 700);
    centerToolWindow(sub);
    sub->move(sub->x(), sub->y() - 50); // Adjust vertical centering
    
    connect(dlg, &QDialog::accepted, this, [this](){
        log(tr("Selective Color Correction applied."), Log_Success, true);
    });
    
    // Update when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*){
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}

// ========== MainWindowCallbacks Pure Virtual Implementations ==========

ImageBuffer* MainWindow::getCurrentImageBuffer() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) return nullptr;
    return &viewer->getBuffer();
}

ImageViewer* MainWindow::getCurrentViewer() {
    return currentViewer();
}

void MainWindow::createResultWindow(const ImageBuffer& buffer, const QString& title) {
    createNewImageWindow(buffer, title);
}

void MainWindow::logMessage(const QString& message, int severity, bool showPopup) {
    // severity: 0=Info, 1=Success, 2=Warning, 3=Error
    LogType type = Log_Info;
    switch (severity) {
        case 1: type = Log_Success; break;
        case 2: type = Log_Warning; break;
        case 3: type = Log_Error; break;
        default: type = Log_Info; break;
    }
    log(message, type, showPopup);
}

// ========== Stacking Suite ==========

void MainWindow::openStackingDialog() {
    log(tr("Opening Stacking Dialog..."), Log_Action, true);
    StackingDialog dlg(this);
    dlg.exec();
}

void MainWindow::openRegistrationDialog() {
    log(tr("Opening Registration Dialog..."), Log_Action, true);
    RegistrationDialog dlg(this);
    dlg.exec();
}

void MainWindow::openPreprocessingDialog() {
    log(tr("Opening Preprocessing Dialog..."), Log_Action, true);
    PreprocessingDialog dlg(this);
    dlg.exec();
}

void MainWindow::openNewProjectDialog() {
    log(tr("Opening New Project Dialog..."), Log_Action, true);
    NewProjectDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        Stacking::StackingProject* project = dlg.createProject();
        if (project) {
            log(tr("Project created: %1").arg(project->rootDir()), Log_Success, true);
            delete project;
        } else {
            log(tr("Failed to create project."), Log_Error, true);
        }
    }
}

void MainWindow::openExistingProject() {
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Select Project Directory"),
        QDir::homePath());
    
    if (dir.isEmpty()) return;
    
    Stacking::StackingProject project;
    QString projectFile = Stacking::StackingProject::findProjectFile(dir);
    
    if (!projectFile.isEmpty() && project.load(projectFile)) {
        log(tr("Project loaded: %1").arg(project.name()), Log_Success, true);
        // Open StackingDialog with this project
        StackingDialog dlg(this);
        dlg.exec();
    } else {
        log(tr("No valid project found at: %1").arg(dir), Log_Warning, true);
    }
}

void MainWindow::openConvertDialog() {
    log(tr("Opening Conversion Dialog..."), Log_Action, true);
    ConversionDialog dlg(this);
    dlg.exec();
}

void MainWindow::openScriptDialog() {
    log(tr("Opening Script Browser..."), Log_Action, true);
    ScriptBrowserDialog browserDlg(this);
    
    if (browserDlg.exec() == QDialog::Accepted) {
        QString scriptFile = browserDlg.selectedScript();
        if (scriptFile.isEmpty()) return;
        
        log(tr("Running script: %1").arg(QFileInfo(scriptFile).fileName()), Log_Action, true);
        
        // Open ScriptDialog with console and progress bar
        ScriptDialog* scriptDlg = new ScriptDialog(this);
        scriptDlg->setAttribute(Qt::WA_DeleteOnClose);
        scriptDlg->loadScript(scriptFile);
        scriptDlg->exec();
        
        // Log result after dialog closes
        log(tr("Script dialog closed."), Log_Info);
    }
}

void MainWindow::updatePixelInfo(const QString& info) {
    if (m_pixelInfoLabel) {
         m_pixelInfoLabel->setText(info);
    }
}
