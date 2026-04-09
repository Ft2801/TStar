// =============================================================================
// MainWindow.cpp
// =============================================================================
// Main application window implementation for TStar astrophotography software.
// Handles UI setup, MDI workspace, toolbars, menus, image loading/saving,
// dialog management, undo/redo, console logging, and workspace projects.
// =============================================================================

// ---------------------------------------------------------------------------
// Section 1: Includes
// ---------------------------------------------------------------------------

// --- Core Application Headers ---
#include "MainWindow.h"
#include "core/Version.h"
#include "core/Logger.h"
#include "core/ColorProfileManager.h"
#include "core/ThreadState.h"
#include "Icons.h"

// --- Widget Headers ---
#include "widgets/CustomMdiSubWindow.h"
#include "widgets/SplashScreen.h"
#include "widgets/ResourceMonitorWidget.h"
#include "widgets/AnnotationOverlay.h"
#include "widgets/SidebarWidget.h"
#include "widgets/RightSidebarWidget.h"
#include "widgets/HeaderPanel.h"
#include "widgets/HistoryPanel.h"

// --- Dialog Headers: Stretch Tools ---
#include "dialogs/StretchDialog.h"
#include "dialogs/ArcsinhStretchDialog.h"
#include "dialogs/HistogramStretchDialog.h"
#include "dialogs/GHSDialog.h"
#include "dialogs/CurvesDialog.h"
#include "dialogs/StarStretchDialog.h"

// --- Dialog Headers: Color Management ---
#include "dialogs/ABEDialog.h"
#include "dialogs/CBEDialog.h"
#include "dialogs/PCCDialog.h"
#include "dialogs/SPCCDialog.h"
#include "dialogs/BackgroundNeutralizationDialog.h"
#include "dialogs/SCNRDialog.h"
#include "dialogs/SaturationDialog.h"
#include "dialogs/SelectiveColorDialog.h"
#include "dialogs/TemperatureTintDialog.h"
#include "dialogs/MagentaCorrectionDialog.h"
#include "dialogs/PCCDistributionDialog.h"
#include "dialogs/ColorProfileDialog.h"

// --- Dialog Headers: AI Processing ---
#include "dialogs/CosmicClarityDialog.h"
#include "dialogs/GraXpertDialog.h"
#include "dialogs/StarNetDialog.h"
#include "dialogs/RARDialog.h"
#include "dialogs/AberrationInspectorDialog.h"

// --- Dialog Headers: Channel Operations ---
#include "dialogs/ChannelCombinationDialog.h"
#include "dialogs/AlignChannelsDialog.h"
#include "dialogs/ExtractLuminanceDialog.h"
#include "dialogs/RecombineLuminanceDialog.h"
#include "dialogs/StarRecompositionDialog.h"
#include "dialogs/ImageBlendingDialog.h"
#include "dialogs/PerfectPaletteDialog.h"
#include "dialogs/DebayerDialog.h"
#include "dialogs/ContinuumSubtractionDialog.h"
#include "dialogs/NarrowbandNormalizationDialog.h"
#include "dialogs/NBtoRGBStarsDialog.h"

// --- Dialog Headers: Utilities ---
#include "dialogs/PlateSolvingDialog.h"
#include "dialogs/PixelMathDialog.h"
#include "dialogs/BinningDialog.h"
#include "dialogs/UpscaleDialog.h"
#include "dialogs/StarAnalysisDialog.h"
#include "dialogs/WavescaleHDRDialog.h"
#include "dialogs/HeaderViewerDialog.h"
#include "dialogs/HeaderEditorDialog.h"
#include "dialogs/AnnotationToolDialog.h"
#include "dialogs/CorrectionBrushDialog.h"
#include "dialogs/ClaheDialog.h"
#include "dialogs/StarHaloRemovalDialog.h"
#include "dialogs/MorphologyDialog.h"
#include "dialogs/MultiscaleDecompDialog.h"
#include "dialogs/BlinkComparatorDialog.h"
#include "dialogs/WCSMosaicDialog.h"
// #include "dialogs/DeconvolutionDialog.h"

// --- Dialog Headers: Effects ---
#include "dialogs/RawEditorDialog.h"
#include "dialogs/AstroSpikeDialog.h"
#include "dialogs/CropRotateDialog.h"

// --- Dialog Headers: Stacking Pipeline ---
#include "dialogs/StackingDialog.h"
#include "dialogs/RegistrationDialog.h"
#include "dialogs/PreprocessingDialog.h"
#include "dialogs/NewProjectDialog.h"
#include "dialogs/ConversionDialog.h"
#include "dialogs/ScriptDialog.h"
#include "dialogs/ScriptBrowserDialog.h"

// --- Dialog Headers: Application ---
#include "dialogs/SettingsDialog.h"
#include "dialogs/UpdateDialog.h"
#include "dialogs/HelpDialog.h"
#include "dialogs/AboutDialog.h"

// --- Network ---
#include "network/UpdateChecker.h"

// --- I/O Loaders ---
#include "io/FitsLoader.h"
#include "io/SimpleTiffReader.h"
#include "io/RawLoader.h"
#include "io/XISFReader.h"

// --- Algorithm Headers ---
#include "algos/ChannelOps.h"

// --- Stacking / Scripting ---
#include "stacking/StackingProject.h"
#include "scripting/StackingCommands.h"
#include "scripting/ScriptRunner.h"

// --- Qt Core ---
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QAtomicInt>
#include <QMutex>
#include <QQueue>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QDirIterator>
#include <QSaveFile>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QTime>
#include <QSet>

// --- Qt GUI ---
#include <QPainter>
#include <QSvgRenderer>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QResizeEvent>
#include <QShowEvent>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

// --- Qt Widgets ---
#include <QMenuBar>
#include <QMenu>
#include <QToolButton>
#include <QDesktopServices>
#include <QUrl>
#include <QToolBar>
#include <QStatusBar>
#include <QComboBox>
#include <QDockWidget>
#include <QSplitter>
#include <QTextEdit>
#include <QLabel>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QProgressDialog>

// --- Third-Party ---
#include <opencv2/opencv.hpp>

// --- Standard Library ---
#include <cstdlib>
#include <cmath>
#include <exception>
#include <memory>


// ---------------------------------------------------------------------------
// Section 2: Helper Structs for Workspace Project Snapshot I/O
// ---------------------------------------------------------------------------

/**
 * @brief Job descriptor for saving an image buffer snapshot to disk.
 */
struct SaveSnapshotJob {
    ImageBuffer buffer;
    QString     filePath;
    bool        success = false;
    QString     error;
    QString     viewTitle;
};

/**
 * @brief Job descriptor for loading an image buffer snapshot from disk.
 */
struct LoadSnapshotJob {
    QString     filePath;
    QString     relPath;     // Reference key in JSON
    ImageBuffer buffer;
    bool        success = false;
    QString     error;
    QString     viewTitle;
};


// ---------------------------------------------------------------------------
// Section 3: Destructor
// ---------------------------------------------------------------------------

MainWindow::~MainWindow() {
    // Cleanup is handled in closeEvent
}


// ---------------------------------------------------------------------------
// Section 4: Constructor
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // --- 4.1: Window Initialization ---
    setWindowOpacity(0.0);   // Start invisible for fade-in animation
    setAcceptDrops(true);    // Enable drag-and-drop file loading

    // --- 4.2: Restore Working Directory from Settings ---
    QSettings settings("TStar", "TStar");
    QString lastWorkingDir = settings.value("General/LastWorkingDir").toString();
    m_lastDialogDir = settings.value("General/LastDialogDir").toString();

    if (!lastWorkingDir.isEmpty() && QDir(lastWorkingDir).exists()) {
        QDir::setCurrent(lastWorkingDir);
        if (m_lastDialogDir.isEmpty()) m_lastDialogDir = lastWorkingDir;
    } else {
        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (!desktopPath.isEmpty() && QDir(desktopPath).exists()) {
            QDir::setCurrent(desktopPath);
            if (m_lastDialogDir.isEmpty()) m_lastDialogDir = desktopPath;
        }
    }

    // --- 4.3: Central Widget and Main Layout ---
    QWidget* cwPtr = new QWidget(this);
    setCentralWidget(cwPtr);

    QHBoxLayout* mainLayout = new QHBoxLayout(cwPtr);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- 4.4: Icon Factory Lambda ---
    // Creates a QIcon from either a file path (PNG/JPG/SVG) or inline SVG string.
    auto makeIcon = [](const QString& source) -> QIcon {
        if (source.endsWith(".png") || source.endsWith(".jpg") || source.endsWith(".svg")) {
            QString path = source;
            // Resolve relative paths against the application directory
            if (!source.startsWith(":") && !QDir::isAbsolutePath(source)) {
                path = QCoreApplication::applicationDirPath() + "/" + source;
                // Fallback: macOS DMG bundle Resources folder
                if (!QFile::exists(path)) {
                    path = QCoreApplication::applicationDirPath() + "/../Resources/" + source;
                }
            }
            return QIcon(path);
        } else {
            // Treat as inline SVG XML string
            QPixmap pm(24, 24);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            QSvgRenderer r(source.toUtf8());
            r.render(&p);
            return QIcon(pm);
        }
    };

    // --- 4.5: Left Sidebar ---
    m_sidebar = new SidebarWidget(this);
    mainLayout->addWidget(m_sidebar);

    // --- 4.6: Custom MDI Area with Branded Background ---
    // Subclass QMdiArea to paint a custom background with the TStar logo,
    // branding text, and keyboard shortcut hints.
    class TStarMdiArea : public QMdiArea {
    public:
        explicit TStarMdiArea(QWidget* parent = nullptr) : QMdiArea(parent) {}

    protected:
        void paintEvent(QPaintEvent* event) override {
            QPainter p(viewport());

            // Step 1: Fill dark background
            p.fillRect(event->rect(), QColor(30, 30, 30));

            // Step 2: Render centered solar system SVG logo
            p.save();
            p.setRenderHint(QPainter::Antialiasing);

            int w    = width();
            int h    = height();
            int cx   = w / 2;
            int cy   = h / 2;
            int side = 600;
            int half = side / 2;

            QSvgRenderer renderer(QString(":/images/solar_system.svg"));
            if (renderer.isValid()) {
                QRectF targetRect(cx - half, cy - half, side, side);
                renderer.render(&p, targetRect);
            }
            p.restore();

            // Step 3: Draw "TStar" branding text at bottom-right
            p.save();
            p.setRenderHint(QPainter::TextAntialiasing);

            QString text = "TStar";
            QFont font("Segoe Script", 48, QFont::Bold);
            if (!QFontInfo(font).exactMatch()) {
                font = QFont("Brush Script MT", 48, QFont::Normal, true);
            }
            if (!QFontInfo(font).exactMatch()) {
                font = QFont(font.family(), 48, QFont::Normal, true);
            }

            p.setFont(font);
            p.setPen(QColor(51, 51, 51));

            QFontMetrics fm(font);
            int tw = fm.horizontalAdvance(text);
            int px = w - tw - 30;
            int py = h - 20;

            p.drawText(px, py, text);
            p.restore();

            // Step 4: Draw keyboard shortcut hints at top-left
            p.save();
            p.setFont(QFont("Segoe UI, sans-serif", 14));
            p.setPen(QColor(136, 136, 136));
            int sx = 25, sy = 35;
            int lineHeight = 18;

            // Title
            QFont titleFont("Segoe UI, sans-serif", 14);
            titleFont.setBold(true);
            p.setFont(titleFont);
            p.drawText(sx, sy, QCoreApplication::translate("MainWindow", "Shortcuts"));
            sy += lineHeight;

            // Shortcut list
            p.setFont(QFont("Segoe UI, sans-serif", 12));
            p.setPen(QColor(136, 136, 136));

            QString sc1 = QCoreApplication::translate("MainWindow", "Ctrl+O: open image");
            QString sc2 = QCoreApplication::translate("MainWindow", "Ctrl+S: save image");
            QString sc3 = QCoreApplication::translate("MainWindow", "Shift + draw a selection: create a new view");
            QString sc4 = QCoreApplication::translate("MainWindow", "Ctrl+Z / Ctrl+Shift+Z: undo / redo");
            QString sc5 = QCoreApplication::translate("MainWindow", "Ctrl+0: fit to screen");
            QString sc6 = QCoreApplication::translate("MainWindow", "Ctrl+M: minigame");

            QStringList shortcuts = { sc1, sc2, sc3, sc4, sc5, sc6 };
            for (const auto& sc : shortcuts) {
                p.drawText(sx, sy, sc);
                sy += lineHeight;
            }
            p.restore();

            // Step 5: Signal successful startup for self-healing mechanism
            QSettings startupSettings("TStar", "StartupCheck");
            startupSettings.setValue("last_launch_successful", true);
            startupSettings.sync();

            QMdiArea::paintEvent(event);
        }
    };

    m_mdiArea = new TStarMdiArea(this);
    m_mdiArea->setBackground(Qt::NoBrush);
    m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
    m_mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdiArea->setDocumentMode(true);
    m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
    mainLayout->addWidget(m_mdiArea);

    // --- 4.7: Status Bar with Resource Monitor and Pixel Info ---
    auto* resMonitor = new ResourceMonitorWidget(this);
    statusBar()->addPermanentWidget(resMonitor);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->setStyleSheet(
        "QStatusBar { background: #1a1a1a; color: #aaa; border-top: 1px solid #333; padding: 2px; }"
    );

    m_pixelInfoLabel = new QLabel(this);
    m_pixelInfoLabel->setStyleSheet("color: #ccc; font-family: Consolas; padding-left: 10px;");
    statusBar()->addWidget(m_pixelInfoLabel);

    // --- 4.8: Sidebar Overlay Positioning ---
    // Left sidebar is an overlay, not part of the layout
    m_sidebar->setParent(this);
    m_sidebar->raise();

    // Right sidebar: collapsed-view thumbnails overlay on the right edge
    m_rightSidebar = new RightSidebarWidget(this);
    m_rightSidebar->setParent(this);
    m_rightSidebar->raise();

    // Create margins to prevent MDI windows from hiding under sidebar tab strips
    int sidebarTabWidth = 34;  // 32px tab + 2px buffer
    mainLayout->setContentsMargins(sidebarTabWidth, 0, 34, 0);

    // Initial sync for overlay positioning
    if (this->centralWidget()) {
        m_sidebar->move(this->centralWidget()->x(), this->centralWidget()->y());
        m_sidebar->resize(m_sidebar->totalVisibleWidth(), this->centralWidget()->height());

        QRect cw = this->centralWidget()->geometry();
        m_rightSidebar->setAnchorGeometry(cw.right(), cw.y(), cw.height());
    }

    // --- 4.9: Right Sidebar Connections ---
    // Activate a window when its thumbnail is clicked in the right sidebar
    connect(m_rightSidebar, &RightSidebarWidget::thumbnailActivated, this, [this](CustomMdiSubWindow* sub) {
        if (sub && m_mdiArea) {
            if (sub->isHidden()) sub->show();
            if (sub->isShaded()) sub->toggleShade();
            m_mdiArea->setActiveSubWindow(sub);
            sub->raise();
        }
    });

    // Toggle visibility of minimized (shaded) views
    connect(m_rightSidebar, &RightSidebarWidget::hideMinimizedViewsToggled, this, [this](bool hidden) {
        if (!m_mdiArea) return;
        for (auto* window : m_mdiArea->subWindowList()) {
            if (auto* sub = qobject_cast<CustomMdiSubWindow*>(window)) {
                if (sub->isShaded()) {
                    if (hidden) {
                        sub->hide();
                    } else {
                        sub->show();
                    }
                }
            }
        }
    });

    // --- 4.10: Sidebar Panels ---

    // Console panel
    QTextEdit* consoleEdit = new QTextEdit();
    consoleEdit->setReadOnly(true);
    consoleEdit->setStyleSheet("background-color: transparent; color: #dcdcdc; border: none; font-family: Consolas, monospace;");
    m_sidebar->addPanel(tr("Console"), ":/images/console_icon.png", consoleEdit);

    // Header panel
    m_headerPanel = new HeaderPanel();
    m_sidebar->addPanel(tr("Header"), ":/images/header_icon.png", m_headerPanel);

    // History panel
    m_historyPanel = new HistoryPanel();
    m_sidebar->addPanel(tr("History"), ":/images/history.png", m_historyPanel);

    connect(m_historyPanel, &HistoryPanel::historyStateSelected, this, [this](int index) {
        if (ImageViewer* v = currentViewer()) {
            v->jumpToHistoryState(index);
        }
    });

    // --- 4.11: Timer Setup ---

    // Console auto-close timer
    m_tempConsoleTimer = new QTimer(this);
    connect(m_tempConsoleTimer, &QTimer::timeout, this, [this](){
        if (m_isConsoleTempOpen && m_sidebar) {
            if (m_sidebar->isInteracting()) {
                // Still interacting; timer will fire again
            } else {
                m_sidebar->collapse();
                m_isConsoleTempOpen = false;
                m_tempConsoleTimer->stop();
            }
        } else {
            m_tempConsoleTimer->stop();
        }
    });

    // Image display timer: process one loaded image per tick to avoid UI lag
    m_imageDisplayTimer = new QTimer(this);
    connect(m_imageDisplayTimer, &QTimer::timeout, this, &MainWindow::processImageLoadQueue);

    // Sync manual sidebar collapse with timer state
    connect(m_sidebar, &SidebarWidget::expandedToggled, [this](bool expanded){
        if (!expanded && m_isConsoleTempOpen) {
            m_isConsoleTempOpen = false;
            if (m_tempConsoleTimer) m_tempConsoleTimer->stop();
        }
    });

    // --- 4.12: Drag-and-Drop and Event Filters ---
    setAcceptDrops(true);
    m_mdiArea->setAcceptDrops(true);
    m_mdiArea->setAcceptDrops(true);
    m_mdiArea->installEventFilter(this);

    if (m_mdiArea->viewport()) {
        m_mdiArea->viewport()->installEventFilter(this);
    }

    // --- 4.13: Color Profile Manager Connections ---
    connect(&core::ColorProfileManager::instance(), &core::ColorProfileManager::conversionStarted,
            this, [this](quint64 /*id*/) {
        if (m_sidebar) {
            m_sidebar->openPanel("Console");
            if (m_tempConsoleTimer) m_tempConsoleTimer->stop();
        }
        log(tr("Color profile conversion started..."), Log_Info);
    });

    connect(&core::ColorProfileManager::instance(), &core::ColorProfileManager::conversionFinished,
            this, [this](quint64 /*id*/) {
        if (m_lastActiveImageViewer) {
            m_lastActiveImageViewer->refreshDisplay(true);
        }
        log(tr("Color profile conversion finished successfully."), Log_Success);
        showConsoleTemporarily(2000);

        if (m_colorProfileDlg && m_colorProfileDlg->isVisible()) {
            QMetaObject::invokeMethod(m_colorProfileDlg, "loadCurrentInfo", Qt::AutoConnection);
        }
        if (m_tempConsoleTimer) m_tempConsoleTimer->start(3000);
    });

    connect(&core::ColorProfileManager::instance(), &core::ColorProfileManager::conversionFailed,
            this, [this](quint64 /*id*/, const QString& error) {
        log(tr("Color profile conversion failed: %1").arg(error), Log_Error);
        if (m_tempConsoleTimer) m_tempConsoleTimer->start(5000);
    });

    // --- 4.14: Sidebar Tools Setup ---
    setupSidebarTools();

    // --- 4.15: MDI Sub-Window Activation Handler ---
    // This is the central dispatcher that syncs all tools, panels, and UI state
    // whenever the user switches between image views.
    connect(m_mdiArea, &QMdiArea::subWindowActivated, [this](QMdiSubWindow *window) {
        qDebug() << "[MainWindow] subWindowActivated: " << (window ? "Valid Window" : "NULL");
        if (m_isUpdating) return;
        m_isUpdating = true;

        CustomMdiSubWindow* csw = qobject_cast<CustomMdiSubWindow*>(window);
        ImageViewer* v = csw ? csw->viewer() : nullptr;

        // Step 0: Update border highlighting for all subwindows
        for (auto sw : m_mdiArea->subWindowList()) {
            if (auto sub = qobject_cast<CustomMdiSubWindow*>(sw)) {
                sub->setActiveState(sw == window);
            }
        }

        // Step 0.1: Raise all tool windows above image views (deferred)
        QTimer::singleShot(0, this, [this](){
            for (auto sw : m_mdiArea->subWindowList()) {
                if (auto sub = qobject_cast<CustomMdiSubWindow*>(sw)) {
                    if (sub->isToolWindow()) {
                        sub->raise();
                    }
                }
            }
        });

        // Step 1: Update header panel metadata
        if (v) {
            m_headerPanel->setMetadata(v->getBuffer().metadata());
            m_historyPanel->updateHistory(v);
        } else if (!window) {
            m_headerPanel->clear();
            m_historyPanel->clear();
            if (m_autoStretchMedianBtn) {
                m_autoStretchMedianValue = 0.25f;
                m_autoStretchMedianBtn->setText("0.25");
            }
        }

        // Step 2: Handle tool retargeting when active view changes
        if (window) {
            CustomMdiSubWindow* csw = qobject_cast<CustomMdiSubWindow*>(window);
            ImageViewer* v = csw ? csw->viewer() : nullptr;

            if (v && !v->property("isPreview").toBool()) {
                if (m_lastActiveImageViewer != v) {
                    // Save state of previous viewer's tools
                    if (m_lastActiveImageViewer) {
                        if (m_ghsDlg)      m_ghsStates[m_lastActiveImageViewer]      = m_ghsDlg->getState();
                        if (m_curvesDlg)   m_curvesStates[m_lastActiveImageViewer]   = m_curvesDlg->getState();
                        if (m_satDlg)      m_satStates[m_lastActiveImageViewer]      = m_satDlg->getState();
                        if (m_tempTintDlg) m_tempTintStates[m_lastActiveImageViewer] = m_tempTintDlg->getState();
                    }

                    m_lastActiveImageViewer = v;
                    log(tr("Active View Changed: %1").arg(v->windowTitle()), Log_Info);

                    // --- Sync all open tools to the new viewer ---
                    if (m_abeDlg)       m_abeDlg->setViewer(v);
                    if (m_bnDlg)        m_bnDlg->setViewer(v);
                    if (m_wavescaleDlg) m_wavescaleDlg->setViewer(v);
                    if (m_histoDlg)     m_histoDlg->setViewer(v);
                    if (m_stretchDlg)   m_stretchDlg->setViewer(v);

                    if (m_ghsDlg) {
                        if (m_ghsTarget && m_ghsTarget != v) {
                            // GHS typically uses local copy or preview LUT.
                            // setTarget should handle cleanup.
                        }
                        m_ghsTarget = v;
                        m_ghsDlg->setTarget(v);
                        if (m_ghsStates.contains(v)) m_ghsDlg->setState(m_ghsStates[v]);
                    }

                    if (m_curvesDlg) {
                        if (m_curvesTarget && m_curvesTarget != v) {
                            m_curvesTarget->clearPreviewLUT();
                        }
                        m_curvesTarget = v;
                        m_curvesDlg->setViewer(v);
                        if (m_curvesStates.contains(v)) m_curvesDlg->setState(m_curvesStates[v]);
                    }

                    if (m_satDlg) {
                        m_satTarget = v;
                        m_satDlg->setViewer(v);
                        if (m_satStates.contains(v)) m_satDlg->setState(m_satStates[v]);
                    }

                    if (m_tempTintDlg) {
                        m_tempTintTarget = v;
                        m_tempTintDlg->setViewer(v);
                        if (m_tempTintStates.contains(v)) m_tempTintDlg->setState(m_tempTintStates[v]);
                    }

                    if (m_arcsinhDlg)   m_arcsinhDlg->setViewer(v);
                    if (m_scnrDlg)      m_scnrDlg->setViewer(v);

                    if (m_pixelMathDialog) {
                        m_pixelMathDialog->setViewer(v);
                        m_pixelMathDialog->setImages(getImageRefsForPixelMath());
                    }

                    if (m_rarDlg)           m_rarDlg->setViewer(v);
                    if (m_starStretchDlg)   m_starStretchDlg->setViewer(v);
                    if (m_starRecompDlg)    m_starRecompDlg->setViewer(v);
                    if (m_ppDialog)         m_ppDialog->setViewer(v);
                    if (m_plateSolveDlg)    m_plateSolveDlg->setViewer(v);
                    if (m_pccDlg)           m_pccDlg->setViewer(v);
                    if (m_spccDlg)          m_spccDlg->setViewer(v);
                    if (m_cropDlg)          m_cropDlg->setViewer(v);
                    if (m_upscaleDlg)       m_upscaleDlg->setViewer(v);
                    if (m_pccDlg)           m_pccDlg->setViewer(v);
                    if (m_cropDlg)          m_cropDlg->setViewer(v);
                    if (m_astroSpikeDlg)    m_astroSpikeDlg->setViewer(v);
                    if (m_annotatorDlg)     m_annotatorDlg->setViewer(v);
                    // if (m_deconvolutionDlg) m_deconvolutionDlg->setViewer(v);
                    if (m_headerPanel)      m_headerPanel->setMetadata(v->getBuffer().metadata());
                    if (m_historyPanel)     m_historyPanel->updateHistory(v);

                    // Color profile check
                    checkAndHandleColorProfile(v->getBuffer(), v->windowTitle());

                    // Sync display mode combo to the new viewer's state
                    if (m_stretchCombo) {
                        QSignalBlocker b(m_stretchCombo);
                        int idx = m_stretchCombo->findData(static_cast<int>(v->getDisplayMode()));
                        if (idx >= 0) m_stretchCombo->setCurrentIndex(idx);
                        m_displayMode = v->getDisplayMode();
                    }

                    // Sync auto-stretch median button
                    if (m_autoStretchMedianBtn) {
                        float mv = v->getAutoStretchMedian();
                        m_autoStretchMedianValue = mv;
                        m_autoStretchMedianBtn->setText(QString::number(mv, 'f', 2));
                    }

                    // Sync link-channels button
                    if (m_linkChannelsBtn) {
                        QSignalBlocker b(m_linkChannelsBtn);
                        m_linkChannelsBtn->setChecked(v->isDisplayLinked());
                        m_displayLinked = v->isDisplayLinked();

                        auto getImgPath = [](const QString& name) {
                            QString path = QCoreApplication::applicationDirPath() + "/images/" + name;
                            if (!QFile::exists(path)) {
                                path = QCoreApplication::applicationDirPath() + "/../Resources/images/" + name;
                            }
                            return path;
                        };

                        m_linkChannelsBtn->setIcon(QIcon(
                            m_displayLinked ? getImgPath("linked.svg") : getImgPath("unlinked.svg")
                        ));
                    }

                    // Sync channel view button
                    if (m_channelViewBtn) {
                        bool isColor = (v->getBuffer().channels() == 3);
                        m_channelViewBtn->setEnabled(true);
                        if (!isColor) {
                            m_channelViewBtn->setText(tr("Mono"));
                            m_channelViewBtn->setEnabled(false);
                        } else {
                            switch (v->channelView()) {
                            case ImageBuffer::ChannelRGB: m_channelViewBtn->setText(tr("RGB")); break;
                            case ImageBuffer::ChannelR:   m_channelViewBtn->setText(tr("R"));   break;
                            case ImageBuffer::ChannelG:   m_channelViewBtn->setText(tr("G"));   break;
                            case ImageBuffer::ChannelB:   m_channelViewBtn->setText(tr("B"));   break;
                            }
                        }
                    }

                    // CBE tracks active view
                    if (m_cbeDlg) m_cbeDlg->setViewer(v);

                    // Ensure signal connections
                    connect(v, &ImageViewer::viewChanged,    this, &MainWindow::propagateViewChange, Qt::UniqueConnection);
                    connect(v, &ImageViewer::historyChanged, this, &MainWindow::updateMenus,         Qt::UniqueConnection);
                    connect(v, &ImageViewer::historyChanged, this, [this, v]() {
                        if (currentViewer() == v && m_historyPanel) {
                            m_historyPanel->updateHistory(v);
                        }
                    }, Qt::UniqueConnection);
                    updateMenus();
                }
            }
        }

        // Step 4: Interactive tool exclusivity logic
        if (window) {
            QWidget* widget = window->widget();
            if (widget) {
                ABEDialog*                      abe  = widget->findChild<ABEDialog*>();
                BackgroundNeutralizationDialog* bn   = widget->findChild<BackgroundNeutralizationDialog*>();
                GHSDialog*                      ghs  = widget->findChild<GHSDialog*>();
                CropRotateDialog*               crop = widget->findChild<CropRotateDialog*>();

                if (!abe) abe = qobject_cast<ABEDialog*>(widget);

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

                    if (m_headerDlg)       m_headerDlg->setViewer(v);
                    if (m_starAnalysisDlg) m_starAnalysisDlg->setViewer(v);
                    if (m_stretchDlg)      m_stretchDlg->setViewer(v);
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

    // --- 4.16: Menu Bar Configuration ---
    menuBar()->setStyleSheet("QMenuBar { background-color: #252525; color: #ccc; } QMenuBar::item:selected { background: #444; }");
    menuBar()->setVisible(false);
    menuBar()->setFocusPolicy(Qt::NoFocus);
    menuBar()->installEventFilter(this);

    // --- 4.17: Display Mode Combo Box ---
    m_stretchCombo = new QComboBox();
    m_stretchCombo->setFixedWidth(120);
    m_stretchCombo->addItem(tr("Linear"),       ImageBuffer::Display_Linear);
    m_stretchCombo->addItem(tr("Auto Stretch"),  ImageBuffer::Display_AutoStretch);
    m_stretchCombo->addItem(tr("Histogram"),     ImageBuffer::Display_Histogram);
    m_stretchCombo->addItem(tr("ArcSinh"),       ImageBuffer::Display_ArcSinh);
    m_stretchCombo->addItem(tr("Square Root"),   ImageBuffer::Display_Sqrt);
    m_stretchCombo->addItem(tr("Logarithmic"),   ImageBuffer::Display_Log);

    // Load default stretch mode from settings
    QString defaultStretch = m_settings.value("display/default_stretch", "Linear").toString();
    int stretchIdx = 0;
    if      (defaultStretch == "AutoStretch") stretchIdx = m_stretchCombo->findData(ImageBuffer::Display_AutoStretch);
    else if (defaultStretch == "Histogram")   stretchIdx = m_stretchCombo->findData(ImageBuffer::Display_Histogram);
    else if (defaultStretch == "ArcSinh")     stretchIdx = m_stretchCombo->findData(ImageBuffer::Display_ArcSinh);
    else if (defaultStretch == "Sqrt")        stretchIdx = m_stretchCombo->findData(ImageBuffer::Display_Sqrt);
    else if (defaultStretch == "Log")         stretchIdx = m_stretchCombo->findData(ImageBuffer::Display_Log);

    if (stretchIdx != -1) m_stretchCombo->setCurrentIndex(stretchIdx);
    else                  m_stretchCombo->setCurrentIndex(0);

    m_stretchCombo->setStyleSheet(
        "QComboBox { background-color: #333; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; padding: 4px 10px; } "
        "QComboBox:hover { background-color: #444; border-color: #666; } "
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border-left-width: 0px; } "
        "QComboBox QAbstractItemView { background-color: #333; color: #e0e0e0; selection-background-color: #555; }"
    );

    connect(m_stretchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this]([[maybe_unused]] int index){
        m_displayMode = static_cast<ImageBuffer::DisplayMode>(m_stretchCombo->currentData().toInt());
        log(tr("Display Mode changed to: %1").arg(m_stretchCombo->currentText()), Log_Info);
        updateDisplay();
    });

    // --- 4.18: Auto-Stretch Target Median Button ---
    const QString popupBtnStyle =
        "QToolButton { background-color:#333; color:#e0e0e0; border:1px solid #555;"
        " border-radius:3px; padding:2px 5px; font-size:11px; }"
        "QToolButton:hover { background-color:#444; border-color:#666; }"
        "QToolButton::menu-indicator { width:0; }";

    m_autoStretchMedianBtn = new QToolButton(this);
    m_autoStretchMedianBtn->setFixedWidth(45);
    m_autoStretchMedianBtn->setText("0.25");
    m_autoStretchMedianBtn->setPopupMode(QToolButton::InstantPopup);
    m_autoStretchMedianBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_autoStretchMedianBtn->setToolTip(tr("Target Median for Auto Stretch"));
    m_autoStretchMedianBtn->setStyleSheet(popupBtnStyle);
    {
        QMenu* medianMenu = new QMenu(this);
        medianMenu->setStyleSheet(
            "QMenu { background-color:#2b2b2b; color:#e0e0e0; border:1px solid #555; }"
            "QMenu::item { padding:5px 20px; }"
            "QMenu::item:selected { background-color:#444; }"
        );
        for (float v : {0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f}) {
            QString label = QString::number(v, 'f', 2);
            QAction* a = medianMenu->addAction(label);
            connect(a, &QAction::triggered, this, [this, v, label](){
                m_autoStretchMedianValue = v;
                m_autoStretchMedianBtn->setText(label);
                if (auto viewer = currentViewer()) viewer->setAutoStretchMedian(v);
            });
        }
        m_autoStretchMedianBtn->setMenu(medianMenu);
    }

    // --- 4.19: Link Views Action ---
    m_linkViewsAction = new QAction(tr("Link Views"), this);
    m_linkViewsAction->setCheckable(true);
    m_linkViewsAction->setChecked(false);
    m_linkViewsAction->setToolTip(tr("Link Zoom and Pan across all windows"));

    // --- 4.20: Main Toolbar ---
    QToolBar* mainToolbar = addToolBar(tr("Main Toolbar"));
    mainToolbar->setMovable(false);
    mainToolbar->setIconSize(QSize(24, 24));
    mainToolbar->setStyleSheet(
        "QToolBar { background-color: #252525; border-bottom: 1px solid #1a1a1a; spacing: 5px; } "
        "QToolBar::separator { background: transparent; width: 5px; border: none; }"
    );
    mainToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Helper lambda: add a toolbar button with icon and connect to a slot
    auto addBtn = [&](const QString& name, const QString& source, auto slot) -> QAction* {
        QAction* act = mainToolbar->addAction(makeIcon(source), name);
        connect(act, &QAction::triggered, this, slot);
        return act;
    };

    // -- Toolbar Group: Home Directory --
    auto homeBtn = addBtn(tr("Set Home"), "images/home.svg", [this](){
        QString current = QDir::currentPath();
        QSettings settings("TStar", "TStar");
        QString lastHome = settings.value("General/LastWorkingDir", current).toString();

        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Home Directory"), lastHome);
        if (!dir.isEmpty()) {
            QDir::setCurrent(dir);
            m_lastDialogDir = dir;

            settings.setValue("General/LastWorkingDir", dir);
            settings.setValue("General/LastDialogDir", dir);
            
            // Also update specific tool folders to the new home to ensure consistency
            settings.setValue("Stacking/InputFolder", dir);
            settings.setValue("Stacking/OutputFolder", dir);
            settings.setValue("Registration/InputFolder", dir);
            settings.setValue("Preprocessing/InputFolder", dir);
            settings.setValue("Project/LastPath", dir);
            settings.sync();

            log(tr("Home Directory changed to: %1").arg(dir), Log_Success);
            showConsoleTemporarily(2000);
        }
    });
    homeBtn->setToolTip(tr("Set Home Directory (CWD)"));

    // -- Toolbar Group: Open / Save --
    addBtn(tr("Open"), "images/open.png", &MainWindow::openFile)->setShortcut(QKeySequence::Open);
    addBtn(tr("Save"), "images/save.png", &MainWindow::saveFile)->setShortcut(QKeySequence::Save);

    // -- Toolbar Group: Project Menu --
    QToolButton* projectBtn = new QToolButton(this);
    projectBtn->setText(tr("Project"));
    projectBtn->setToolTip(tr("Project Workspace Management"));
    projectBtn->setPopupMode(QToolButton::InstantPopup);
    projectBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    projectBtn->setAutoRaise(true);
    projectBtn->setStyleSheet(
        "QToolButton { background: transparent; border: none; color: white; padding: 2px 2px; } "
        "QToolButton:hover { background-color: #3a3a3a; border-radius: 3px; } "
        "QToolButton::menu-indicator { image: none; }"
    );

    // Common dark menu style used throughout
    const QString darkMenuStyle =
        "QMenu { background-color: #2b2b2b; color: #e0e0e0; border: 1px solid #555; } "
        "QMenu::item { padding: 5px 25px 5px 10px; } "
        "QMenu::item:selected { background-color: #444; } "
        "QMenu::separator { height: 1px; background: #555; margin: 4px 0; }";

    QMenu* projectMenu = new QMenu(this);
    projectMenu->setStyleSheet(darkMenuStyle);

    m_newWorkspaceProjectAction = projectMenu->addAction(tr("New Workspace Project..."));
    connect(m_newWorkspaceProjectAction, &QAction::triggered, this, &MainWindow::newWorkspaceProject);
    m_openWorkspaceProjectAction = projectMenu->addAction(tr("Open Workspace Project..."));
    connect(m_openWorkspaceProjectAction, &QAction::triggered, this, &MainWindow::openWorkspaceProject);
    projectMenu->addSeparator();
    m_saveWorkspaceProjectAction = projectMenu->addAction(tr("Save Workspace Project"));
    connect(m_saveWorkspaceProjectAction, &QAction::triggered, this, &MainWindow::saveWorkspaceProject);
    m_saveWorkspaceProjectAsAction = projectMenu->addAction(tr("Save Workspace Project As..."));
    connect(m_saveWorkspaceProjectAsAction, &QAction::triggered, this, &MainWindow::saveWorkspaceProjectAs);
    projectMenu->addSeparator();
    m_closeWorkspaceProjectAction = projectMenu->addAction(tr("Close Workspace Project"));
    connect(m_closeWorkspaceProjectAction, &QAction::triggered, this, &MainWindow::closeWorkspaceProject);
    m_deleteWorkspaceProjectAction = projectMenu->addAction(tr("Delete Workspace Project..."));
    connect(m_deleteWorkspaceProjectAction, &QAction::triggered, this, &MainWindow::deleteWorkspaceProject);

    projectBtn->setMenu(projectMenu);
    mainToolbar->addWidget(projectBtn);

    // Spacer
    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }

    // -- Toolbar Group: Undo / Redo --
    m_undoAction = mainToolbar->addAction(makeIcon(Icons::UNDO), tr("Undo"));
    m_undoAction->setToolTip(tr("Undo (Ctrl+Z)"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

    m_redoAction = mainToolbar->addAction(makeIcon(Icons::REDO), tr("Redo"));
    m_redoAction->setToolTip(tr("Redo (Ctrl+Shift+Z)"));
    m_redoAction->setShortcut(QKeySequence("Ctrl+Shift+Z"));
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);

    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }

    // -- Toolbar Group: Zoom / Fit --
    addBtn(tr("Zoom In"),  Icons::ZOOM_IN,  [this](){ if(currentViewer()) currentViewer()->zoomIn(); })->setShortcut(QKeySequence::ZoomIn);
    addBtn(tr("Zoom Out"), Icons::ZOOM_OUT, [this](){ if(currentViewer()) currentViewer()->zoomOut(); })->setShortcut(QKeySequence::ZoomOut);
    addBtn(tr("Fit to Screen"), Icons::FIT_SCREEN, [this](){ if(currentViewer()) currentViewer()->fitToWindow();
    })->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    addBtn(tr("1:1"), Icons::ZOOM_100, [this](){ if(currentViewer()) currentViewer()->zoom1to1(); })->setToolTip(tr("Zoom 100%"));

    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }

    // -- Toolbar Group: Geometry Transforms --
    addBtn(tr("Rotate Left"),  Icons::ROTATE_LEFT,  [this](){ applyGeometry(tr("Rotate CCW"), [](ImageBuffer& b){ b.rotate270(); }); });
    addBtn(tr("Rotate Right"), Icons::ROTATE_RIGHT, [this](){ applyGeometry(tr("Rotate CW"),  [](ImageBuffer& b){ b.rotate90();  }); });
    addBtn(tr("Flip Horiz"),   Icons::FLIP_HORIZ,   [this](){ applyGeometry(tr("Mirror H"),   [](ImageBuffer& b){ b.mirrorX();   }); });
    addBtn(tr("Flip Vert"),    Icons::FLIP_VERT,    [this](){ applyGeometry(tr("Mirror V"),   [](ImageBuffer& b){ b.mirrorY();   }); });
    addBtn(tr("Crop / Rotate"), Icons::CROP, &MainWindow::cropTool);

    { QWidget* s = new QWidget(this); s->setFixedWidth(2); mainToolbar->addWidget(s); }

    // -- Toolbar Group: Channel View Selector --
    {
        m_channelViewBtn = new QToolButton(this);
        m_channelViewBtn->setFixedWidth(45);
        m_channelViewBtn->setText(tr("RGB"));
        m_channelViewBtn->setPopupMode(QToolButton::InstantPopup);
        m_channelViewBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_channelViewBtn->setToolTip(tr("Channel View"));
        m_channelViewBtn->setStyleSheet(popupBtnStyle);

        QMenu* chanMenu = new QMenu(this);
        chanMenu->setStyleSheet(
            "QMenu { background-color:#2b2b2b; color:#e0e0e0; border:1px solid #555; }"
            "QMenu::item { padding:5px 20px; }"
            "QMenu::item:selected { background-color:#444; }"
        );

        auto addChan = [&](const QString& label, ImageBuffer::ChannelView cv) {
            QAction* a = chanMenu->addAction(label);
            connect(a, &QAction::triggered, this, [this, label, cv](){
                m_channelViewBtn->setText(label);
                if (auto v = currentViewer()) v->setChannelView(cv);
            });
        };
        addChan(tr("RGB"), ImageBuffer::ChannelRGB);
        addChan(tr("R"),   ImageBuffer::ChannelR);
        addChan(tr("G"),   ImageBuffer::ChannelG);
        addChan(tr("B"),   ImageBuffer::ChannelB);
        m_channelViewBtn->setMenu(chanMenu);

        mainToolbar->addWidget(m_channelViewBtn);
        { QWidget* s = new QWidget(this); s->setFixedWidth(4); mainToolbar->addWidget(s); }
    }

    // -- Toolbar: Auto-Stretch Median + Display Combo --
    mainToolbar->addWidget(m_autoStretchMedianBtn);
    { QWidget* s = new QWidget(this); s->setFixedWidth(3); mainToolbar->addWidget(s); }

    mainToolbar->addWidget(m_stretchCombo);

    { QWidget* spacer = new QWidget(this); spacer->setFixedWidth(5); mainToolbar->addWidget(spacer); }

    // -- Toolbar: Link Channels Toggle --
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

    // -- Toolbar: Burn Display --
    m_burnDisplayBtn = new QToolButton(this);
    m_burnDisplayBtn->setIcon(makeIcon("images/burn.svg"));
    m_burnDisplayBtn->setToolTip(tr("Burn Display View to Buffer\n(Applies current stretch/display mode permanently)"));
    connect(m_burnDisplayBtn, &QToolButton::clicked, this, &MainWindow::onBurnDisplay);
    mainToolbar->addWidget(m_burnDisplayBtn);

    // -- Toolbar: Invert Toggle --
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

    { QWidget* s2 = new QWidget(this); s2->setFixedWidth(5); mainToolbar->addWidget(s2); }

    // -- Toolbar: False Color Toggle --
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

    { QWidget* s3 = new QWidget(this); s3->setFixedWidth(5); mainToolbar->addWidget(s3); }

    // --- 4.21: Process Menu (Categorized Tools) ---
    // Common button style for text-only toolbar menus
    const QString toolBtnStyle =
        "QToolButton { color: #e0e0e0; border: 1px solid #555; border-radius: 3px; "
        "background-color: #2b2b2b; padding: 3px 12px; } "
        "QToolButton:hover { background-color: #3a3a3a; border-color: #666; } "
        "QToolButton::menu-indicator { image: none; }";

    QToolButton* processBtn = new QToolButton(this);
    processBtn->setText(tr("Process"));
    processBtn->setPopupMode(QToolButton::InstantPopup);
    processBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);

    QMenu* processMenu = new QMenu(this);
    processMenu->setStyleSheet(darkMenuStyle);

    // Helper lambda: add a menu action connected to a slot
    auto addMenuAction = [this](QMenu* menu, const QString& name, const QString& icon, auto slot) {
        QAction* act = menu->addAction(name);
        if (!icon.isEmpty()) act->setIcon(QIcon(icon));
        connect(act, &QAction::triggered, this, slot);
    };

    // --- A. Stretch Tools ---
    QMenu* stretchMenu = processMenu->addMenu(tr("Stretch Tools"));
    addMenuAction(stretchMenu, tr("Auto Stretch"),                  "", [this](){ openStretchDialog(); });
    addMenuAction(stretchMenu, tr("ArcSinh Stretch"),               "", [this](){ openArcsinhStretchDialog(); });
    addMenuAction(stretchMenu, tr("Curves Transformation"),         "", [this](){ openCurvesDialog(); });
    addMenuAction(stretchMenu, tr("Histogram Transformation"),      "", [this](){ openHistogramStretchDialog(); });
    addMenuAction(stretchMenu, tr("GHS (Generalized Hyperbolic)"),  "", [this](){ openGHSDialog(); });
    addMenuAction(stretchMenu, tr("Star Stretch"),                  "", [this](){ openStarStretchDialog(); });

    // --- B. Color Management ---
    QMenu* colorMenu = processMenu->addMenu(tr("Color Management"));
    addMenuAction(colorMenu, tr("Auto Background Extraction (ABE)"),            "", [this](){ openAbeDialog(); });
    addMenuAction(colorMenu, tr("Catalog Background Extraction (CBE)"),         "", [this](){ openCbeDialog(); });
    addMenuAction(colorMenu, tr("Photometric Color Calibration"),               "", [this](){ openPCCDialog(); });
    addMenuAction(colorMenu, tr("Spectrophotometric Color Calibration (SPCC)"), "", [this](){ openSPCCDialog(); });
    addMenuAction(colorMenu, tr("Background Neutralization"),                   "", [this](){ openBackgroundNeutralizationDialog(); });
    addMenuAction(colorMenu, tr("SCNR (Remove Green)"),                         "", [this](){ openSCNRDialog(); });
    addMenuAction(colorMenu, tr("Magenta Correction"),                          "", [this](){ openMagentaCorrectionDialog(); });
    addMenuAction(colorMenu, tr("PCC Distribution"),                            "", [this](){ openPCCDistributionDialog(); });
    addMenuAction(colorMenu, tr("Saturation"),                                  "", [this](){ openSaturationDialog(); });
    addMenuAction(colorMenu, tr("Selective Color Correction"),                  "", [this](){ openSelectiveColorDialog(); });
    addMenuAction(colorMenu, tr("Temperature / Tint"),                          "", [this](){ openTemperatureTintDialog(); });

    // --- C. AI Processing ---
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
    addMenuAction(aiMenu, tr("Aberration Remover"), "", [this](){ openRARDialog(); });

    // --- D. Channel Operations ---
    QMenu* chanMenu = processMenu->addMenu(tr("Channel Operations"));
    addMenuAction(chanMenu, tr("Extract Channels"),        "", &MainWindow::extractChannels);
    addMenuAction(chanMenu, tr("Combine Channels"),        "", [this](){ if (activateTool(tr("Combine Channels"))) return; combineChannels(); });
    addMenuAction(chanMenu, tr("Align Channels"),          "", [this](){ openAlignChannelsDialog(); });
    addMenuAction(chanMenu, tr("Extract Luminance"),       "", [this](){ openExtractLuminanceDialog(); });
    addMenuAction(chanMenu, tr("Recombine Luminance"),     "", [this](){ openRecombineLuminanceDialog(); });
    addMenuAction(chanMenu, tr("Remove Pedestal (Auto)"),  "", [this](){ removePedestal(); });
    addMenuAction(chanMenu, tr("Star Recomposition"),      "", [this](){ openStarRecompositionDialog(); });
    addMenuAction(chanMenu, tr("Image Blending"),          "", [this](){ openImageBlendingDialog(); });
    addMenuAction(chanMenu, tr("Perfect Palette Picker"),  "", [this](){ openPerfectPaletteDialog(); });
    addMenuAction(chanMenu, tr("Debayer"),                 "", [this](){ openDebayerDialog(); });
    addMenuAction(chanMenu, tr("Continuum Subtraction"),   "", [this](){ openContinuumSubtractionDialog(); });
    addMenuAction(chanMenu, tr("Narrowband Normalization"),"", [this](){ openNarrowbandNormalizationDialog(); });
    addMenuAction(chanMenu, tr("NB -> RGB Stars"),         "", [this](){ openNBtoRGBStarsDialog(); });

    // --- E. Utilities ---
    QMenu* utilMenu = processMenu->addMenu(tr("Utilities"));
    addMenuAction(utilMenu, tr("Plate Solving"),                  "", [this](){ openPlateSolvingDialog(); });
    addMenuAction(utilMenu, tr("Pixel Math"),                     "", [this](){ openPixelMathDialog(); });
    addMenuAction(utilMenu, tr("Binning"),                        "", [this](){ openBinningDialog(); });
    addMenuAction(utilMenu, tr("Upscale"),                        "", [this](){ openUpscaleDialog(); });
    addMenuAction(utilMenu, tr("Star Analysis"),                  "", [this](){ openStarAnalysisDialog(); });
    addMenuAction(utilMenu, tr("Wavescale HDR"),                  "", [this](){ openWavescaleHDRDialog(); });
    /*
    addMenuAction(utilMenu, tr("Deconvolution"),                  "", [this](){ openDeconvolutionDialog(); });
    */
    addMenuAction(utilMenu, tr("FITS Header Editor"),             "", [this](){ openHeaderEditorDialog(); });
    addMenuAction(utilMenu, tr("Image Annotator"),                "", [this](){ openImageAnnotatorDialog(); });
    addMenuAction(utilMenu, tr("Correction Brush"),               "", [this](){ openCorrectionBrushDialog(); });
    addMenuAction(utilMenu, tr("CLAHE"),                          "", [this](){ openClaheDialog(); });
    addMenuAction(utilMenu, tr("Star Halo Removal"),              "", [this](){ openStarHaloRemovalDialog(); });
    addMenuAction(utilMenu, tr("Morphology"),                     "", [this](){ openMorphologyDialog(); });
    addMenuAction(utilMenu, tr("Aberration Inspector (9-Points)"),"", [this](){ openAberrationInspectorDialog(); });
    addMenuAction(utilMenu, tr("Multiscale Decomposition"),       "", [this](){ openMultiscaleDecompDialog(); });
    addMenuAction(utilMenu, tr("Blink Comparator"),               "", [this](){ openBlinkComparatorDialog(); });
    addMenuAction(utilMenu, tr("WCS Mosaic Builder"),             "", [this](){ openWCSMosaicDialog(); });

    // --- F. Effects ---
    QMenu* effectMenu = processMenu->addMenu(tr("Effects"));
    addMenuAction(effectMenu, tr("RawEditor (Light and Color)"),     "", [this](){ openRawEditorDialog(); });
    addMenuAction(effectMenu, tr("AstroSpike (Diffraction Spikes)"), "", [this](){ openAstroSpikeDialog(); });

    processBtn->setMenu(processMenu);
    processBtn->setStyleSheet(toolBtnStyle);
    mainToolbar->addWidget(processBtn);
    mainToolbar->addSeparator();

    // --- 4.22: Stacking Menu ---
    QToolButton* stackBtn = new QToolButton(this);
    stackBtn->setText(tr("Stacking"));
    stackBtn->setPopupMode(QToolButton::InstantPopup);
    stackBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    stackBtn->setStyleSheet(processBtn->styleSheet());

    QMenu* stackMenu = new QMenu(this);
    stackMenu->setStyleSheet(processMenu->styleSheet());

    addMenuAction(stackMenu, tr("New Project..."),                      "", &MainWindow::openNewProjectDialog);
    addMenuAction(stackMenu, tr("Open Project..."),                     "", &MainWindow::openExistingProject);
    stackMenu->addSeparator();
    addMenuAction(stackMenu, tr("Convert RAW to FITS..."),              "", &MainWindow::openConvertDialog);
    stackMenu->addSeparator();
    addMenuAction(stackMenu, tr("Preprocessing (Calibration)..."),      "", &MainWindow::openPreprocessingDialog);
    addMenuAction(stackMenu, tr("Registration (Star Alignment)..."),    "", &MainWindow::openRegistrationDialog);
    addMenuAction(stackMenu, tr("Stacking..."),                         "", &MainWindow::openStackingDialog);
    stackMenu->addSeparator();
    addMenuAction(stackMenu, tr("Run Script..."),                       "", &MainWindow::openScriptDialog);

    stackBtn->setMenu(stackMenu);
    mainToolbar->addWidget(stackBtn);
    mainToolbar->addSeparator();

    // --- 4.23: Mask Menu ---
    QToolButton* maskBtn = new QToolButton(this);
    maskBtn->setText(tr("Mask"));
    maskBtn->setPopupMode(QToolButton::InstantPopup);
    maskBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    maskBtn->setStyleSheet(processBtn->styleSheet());

    QMenu* maskMenu = new QMenu(this);
    maskMenu->setStyleSheet(processMenu->styleSheet());

    addMenuAction(maskMenu, tr("Create Mask..."), "", &MainWindow::createMaskAction);
    addMenuAction(maskMenu, tr("Apply Mask..."),  "", &MainWindow::applyMaskAction);
    maskMenu->addSeparator();
    addMenuAction(maskMenu, tr("Remove Mask"),    "", &MainWindow::removeMaskAction);
    addMenuAction(maskMenu, tr("Invert Mask"),    "", &MainWindow::invertMaskAction);
    maskMenu->addSeparator();

    m_toggleOverlayAct = maskMenu->addAction(tr("Show Overlay"));
    m_toggleOverlayAct->setCheckable(true);
    m_toggleOverlayAct->setChecked(false);
    connect(m_toggleOverlayAct, &QAction::triggered, this, &MainWindow::toggleMaskOverlayAction);

    maskBtn->setMenu(maskMenu);
    mainToolbar->addWidget(maskBtn);
    mainToolbar->addSeparator();

    // --- 4.24: View Menu ---
    QToolButton* viewBtn = new QToolButton(this);
    viewBtn->setText(tr("View"));
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    viewBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    viewBtn->setStyleSheet(processBtn->styleSheet());

    QMenu* viewMenu = new QMenu(this);
    viewMenu->setStyleSheet(processMenu->styleSheet());

    addMenuAction(viewMenu, tr("Tile Images (Smart Grid)"), "", [this](){ tileImageViews(); });
    addMenuAction(viewMenu, tr("Tile Images Vertical"),     "", [this](){ tileImageViewsVertical(); });
    addMenuAction(viewMenu, tr("Tile Images Horizontal"),   "", [this](){ tileImageViewsHorizontal(); });

    viewBtn->setMenu(viewMenu);
    mainToolbar->addWidget(viewBtn);
    mainToolbar->addSeparator();

    // --- 4.25: Settings Button ---
    QToolButton* settingsBtn = new QToolButton(this);
    settingsBtn->setText(tr("Settings"));
    settingsBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    settingsBtn->setStyleSheet(
        "QToolButton { color: #e0e0e0; border: 1px solid #555; border-radius: 3px; "
        "background-color: #2b2b2b; padding: 3px 12px; } "
        "QToolButton:hover { background-color: #3a3a3a; border-color: #666; } "
    );
    connect(settingsBtn, &QToolButton::clicked, this, &MainWindow::onSettingsAction);

    mainToolbar->addWidget(settingsBtn);
    mainToolbar->addSeparator();

    // --- 4.26: Auto-Updater Check (Deferred) ---
    QTimer::singleShot(2000, this, [this](){
        QSettings settings;
        if (!settings.value("general/check_updates", true).toBool()) {
            return;
        }

        UpdateChecker* checker = new UpdateChecker(this);
        connect(checker, &UpdateChecker::updateAvailable, this, [this](const QString& ver, const QString& body, const QString& url){
            log(tr("New version found: %1").arg(ver), Log_Success, true);
            showConsoleTemporarily(2000);
            UpdateDialog dlg(this, ver, body, url);
            dlg.exec();
        });
        connect(checker, &UpdateChecker::noUpdateAvailable, this, [this](){
            log(tr("TStar is up to date."), Log_Info);
        });
        connect(checker, &UpdateChecker::errorOccurred, this, [this](const QString& err){
            log(tr("Update check failed: %1").arg(err), Log_Warning);
        });
        checker->checkForUpdates(TStar::getVersion());
    });

    // --- 4.27: Easter Egg - Minigame Shortcut ---
    // Hidden action for Ctrl+M that launches the minigame
    QAction* minigameAction = new QAction(this);
    minigameAction->setShortcut(Qt::CTRL | Qt::Key_M);
    addAction(minigameAction);
    connect(minigameAction, &QAction::triggered, this, &MainWindow::openMinigame);

    // --- 4.28: Final Window Setup ---
    if (m_rightSidebar) {
        auto registerActions = [&](auto& self, QList<QAction*> actions) -> void {
            for (QAction* act : actions) {
                if (act->menu()) {
                    self(self, act->menu()->actions());
                } else if (!act->text().isEmpty() && !act->isSeparator()) {
                    m_rightSidebar->registerToolAction(act);
                }
            }
        };
        registerActions(registerActions, mainToolbar->actions());
        registerActions(registerActions, projectMenu->actions());
        registerActions(registerActions, processMenu->actions());
        registerActions(registerActions, stackMenu->actions());
        registerActions(registerActions, maskMenu->actions());
        registerActions(registerActions, viewMenu->actions());
    }

    resize(1280, 800);
    log(tr("Application Ready."));
}


// ---------------------------------------------------------------------------
// Section 5: View Tiling
// ---------------------------------------------------------------------------

/**
 * @brief Tiles all image views in a smart grid layout.
 */
void MainWindow::tileImageViews() {
    QList<CustomMdiSubWindow*> imageWindows;
    for (auto* sub : m_mdiArea->subWindowList(QMdiArea::CreationOrder)) {
        auto* csw = qobject_cast<CustomMdiSubWindow*>(sub);
        if (csw && !csw->isToolWindow() && csw->viewer() && !csw->isHidden() && !csw->isShaded()) {
            imageWindows.append(csw);
        }
    }

    int count = imageWindows.size();
    if (count < 2) {
        log(tr("Need at least 2 images to tile."), Log_Warning, true);
        return;
    }

    QRect area = m_mdiArea->viewport()->rect();
    int cols, rows;

    if (count == 2) {
        cols = 2; rows = 1;
    } else if (count <= 4) {
        cols = 2; rows = 2;
    } else {
        cols = std::ceil(std::sqrt((double)count));
        rows = std::ceil((double)count / cols);
    }

    int cellW = area.width() / cols;
    int cellH = area.height() / rows;

    int idx = 0;
    for (int r = 0; r < rows && idx < count; ++r) {
        for (int c = 0; c < cols && idx < count; ++c) {
            auto* win = imageWindows[idx++];
            win->showNormal();
            win->setGeometry(c * cellW, r * cellH, cellW, cellH);
            if (auto* v = win->viewer()) {
                QTimer::singleShot(50, v, &ImageViewer::fitToWindow);
            }
        }
    }

    log(tr("Tiled %1 images in %2x%3 layout.").arg(count).arg(cols).arg(rows), Log_Success, true);
    showConsoleTemporarily(2000);
}

/**
 * @brief Tiles all image views in a vertical (stacked) layout.
 */
void MainWindow::tileImageViewsVertical() {
    QList<CustomMdiSubWindow*> imageWindows;
    for (auto* sub : m_mdiArea->subWindowList(QMdiArea::CreationOrder)) {
        auto* csw = qobject_cast<CustomMdiSubWindow*>(sub);
        if (csw && !csw->isToolWindow() && csw->viewer() && !csw->isHidden() && !csw->isShaded()) imageWindows.append(csw);
    }

    int count = imageWindows.size();
    if (count < 1) {
        log(tr("No images to arrange."), Log_Warning, true);
        return;
    }

    QRect area = m_mdiArea->viewport()->rect();
    int cellH = area.height() / count;
    int cellW = area.width();

    for (int i = 0; i < count; ++i) {
        auto* win = imageWindows[i];
        win->showNormal();
        win->setGeometry(0, i * cellH, cellW, cellH);
        if (auto* v = win->viewer()) QTimer::singleShot(50, v, &ImageViewer::fitToWindow);
    }

    log(tr("Arranged %1 images vertically.").arg(count), Log_Success, true);
    showConsoleTemporarily(2000);
}

/**
 * @brief Tiles all image views in a horizontal (side-by-side) layout.
 */
void MainWindow::tileImageViewsHorizontal() {
    QList<CustomMdiSubWindow*> imageWindows;
    for (auto* sub : m_mdiArea->subWindowList(QMdiArea::CreationOrder)) {
        auto* csw = qobject_cast<CustomMdiSubWindow*>(sub);
        if (csw && !csw->isToolWindow() && csw->viewer() && !csw->isHidden() && !csw->isShaded()) imageWindows.append(csw);
    }

    int count = imageWindows.size();
    if (count < 1) {
        log(tr("No images to arrange."), Log_Warning, true);
        return;
    }

    QRect area = m_mdiArea->viewport()->rect();
    int cellW = area.width() / count;
    int cellH = area.height();

    for (int i = 0; i < count; ++i) {
        auto* win = imageWindows[i];
        win->showNormal();
        win->setGeometry(i * cellW, 0, cellW, cellH);
        if (auto* v = win->viewer()) QTimer::singleShot(50, v, &ImageViewer::fitToWindow);
    }

    log(tr("Arranged %1 images horizontally.").arg(count), Log_Success, true);
    showConsoleTemporarily(2000);
}


// ---------------------------------------------------------------------------
// Section 6: Undo / Redo
// ---------------------------------------------------------------------------

void MainWindow::pushUndo(const QString& description) {
    if (auto v = currentViewer()) {
        v->pushUndo(description);
        updateMenus();
    }
}

void MainWindow::undo() {
    if (auto v = currentViewer()) {
        QString desc = v->getUndoDescription();
        v->undo();
        updateMenus();
        if (!desc.isEmpty()) {
            log(tr("Undo: %1 performed.").arg(desc), Log_Success);
        } else {
            log(tr("Undo performed."), Log_Success);
        }
        showConsoleTemporarily(2000);
    }
}

void MainWindow::redo() {
    if (auto v = currentViewer()) {
        QString desc = v->getRedoDescription();
        v->redo();
        updateMenus();
        if (!desc.isEmpty()) {
            log(tr("Redo: %1 performed.").arg(desc), Log_Success);
        } else {
            log(tr("Redo performed."), Log_Success);
        }
        showConsoleTemporarily(2000);
    }
}


// ---------------------------------------------------------------------------
// Section 7: Burn Display
// ---------------------------------------------------------------------------

/**
 * @brief Permanently applies the current display stretch/mode to the image buffer.
 */
void MainWindow::onBurnDisplay() {
    if (auto v = currentViewer()) {
        v->pushUndo(tr("Burn Display"));

        ImageBuffer& buffer = v->getBuffer();
        if (!buffer.isValid()) {
            log(tr("Cannot burn display: Invalid buffer"), Log_Warning, true);
            return;
        }

        // Capture original display state
        ImageBuffer::DisplayMode originalMode       = v->getDisplayMode();
        bool                     originalInverted    = v->isDisplayInverted();
        bool                     originalFalseColor  = v->isDisplayFalseColor();
        bool                     originalLinked      = v->isDisplayLinked();

        // Apply transformation at full 32-bit float precision
        buffer.applyDisplayTransform(originalMode, originalLinked, m_autoStretchMedianValue,
                                     originalInverted, originalFalseColor);

        // Reset display state to Linear (transformation is now baked in)
        v->setDisplayState(ImageBuffer::Display_Linear, true);
        v->setInverted(false);
        v->setFalseColor(false);
        v->refreshDisplay();

        // Sync the combo box
        if (m_stretchCombo) {
            QSignalBlocker blocker(m_stretchCombo);
            m_stretchCombo->setCurrentIndex(0);
            blocker.unblock();
            m_displayMode = ImageBuffer::Display_Linear;
        }

        // Build descriptive log message
        QString modeStr = (originalMode == ImageBuffer::Display_AutoStretch ? "AutoStretch" :
                           originalMode == ImageBuffer::Display_ArcSinh     ? "ArcSinh" :
                           originalMode == ImageBuffer::Display_Sqrt        ? "Sqrt" :
                           originalMode == ImageBuffer::Display_Log         ? "Log" :
                           "Linear");

        QString extraStr = "";
        if (originalInverted)   extraStr += " + Inverted";
        if (originalFalseColor) extraStr += " + FalseColor";

        log(tr("Display burned to buffer (") + modeStr + extraStr + ")", Log_Success, true);
        showConsoleTemporarily(2000);
        updateMenus();
    } else {
        log(tr("Cannot burn display: No active viewer"), Log_Warning, true);
    }
}


// ---------------------------------------------------------------------------
// Section 8: Menu State Updates
// ---------------------------------------------------------------------------

void MainWindow::updateMenus() {
    auto v = currentViewer();
    bool canUndo = v && v->canUndo();
    bool canRedo = v && v->canRedo();

    m_undoAction->setEnabled(canUndo);
    m_redoAction->setEnabled(canRedo);

    if (canUndo) {
        QString desc = v->getUndoDescription();
        if (!desc.isEmpty()) {
            m_undoAction->setText(tr("Undo: %1").arg(desc));
            m_undoAction->setToolTip(tr("Undo: %1 (Ctrl+Z)").arg(desc));
        } else {
            m_undoAction->setText(tr("Undo"));
            m_undoAction->setToolTip(tr("Undo (Ctrl+Z)"));
        }
    } else {
        m_undoAction->setText(tr("Undo"));
        m_undoAction->setToolTip(tr("Undo (Ctrl+Z)"));
    }

    if (canRedo) {
        QString desc = v->getRedoDescription();
        if (!desc.isEmpty()) {
            m_redoAction->setText(tr("Redo: %1").arg(desc));
            m_redoAction->setToolTip(tr("Redo: %1 (Ctrl+Shift+Z)").arg(desc));
        } else {
            m_redoAction->setText(tr("Redo"));
            m_redoAction->setToolTip(tr("Redo (Ctrl+Shift+Z)"));
        }
    } else {
        m_redoAction->setText(tr("Redo"));
        m_redoAction->setToolTip(tr("Redo (Ctrl+Shift+Z)"));
    }
}


// ---------------------------------------------------------------------------
// Section 9: Viewer Accessors
// ---------------------------------------------------------------------------

ImageViewer* MainWindow::currentViewer() const {
    return m_lastActiveImageViewer;
}

bool MainWindow::hasImage() const {
    ImageViewer* v = currentViewer();
    return v && v->getBuffer().isValid();
}


// ---------------------------------------------------------------------------
// Section 10: Title / Naming Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Builds a child view title by stripping the file extension from the
 *        parent title and appending a suffix.
 */
QString buildChildTitle(const QString& parentTitle, const QString& suffix) {
    QString t = parentTitle;
    if (t.endsWith('*')) t.chop(1);

    static const QStringList exts = {"fits","fit","tif","tiff","png","jpg","jpeg","xisf","bmp"};
    int dot = t.lastIndexOf('.');
    if (dot >= 0 && exts.contains(t.mid(dot+1).toLower()))
        t = t.left(dot);

    return t.trimmed() + suffix;
}

/**
 * @brief Sanitizes a string for use as a filename/project component.
 */
static QString sanitizeProjectComponent(const QString& input) {
    QString out = input;
    out.replace(QRegularExpression("[^A-Za-z0-9_\\-]"), "_");
    while (out.contains("__")) out.replace("__", "_");
    out = out.trimmed();
    if (out.isEmpty()) out = "item";
    return out;
}


// ---------------------------------------------------------------------------
// Section 11: Workspace Project Snapshot I/O
// ---------------------------------------------------------------------------

/**
 * @brief Saves an ImageBuffer to a compressed snapshot file.
 */
static bool saveProjectBufferSnapshot(const ImageBuffer& buffer, const QString& filePath, QString* errOut) {
    try {
        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << buffer;

        const QByteArray compressed = qCompress(data, 1);

        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            if (errOut) *errOut = QObject::tr("Cannot open snapshot file for writing.");
            return false;
        }
        file.write(compressed);
        if (!file.commit()) {
            if (errOut) *errOut = QObject::tr("Cannot commit snapshot file.");
            return false;
        }
        return true;
    } catch (...) {
        if (errOut) *errOut = QObject::tr("Unknown error during snapshot saving.");
        return false;
    }
}

/**
 * @brief Loads an ImageBuffer from a compressed snapshot file.
 *        Falls back to legacy FITS loading if the magic header is missing.
 */
static bool loadProjectBufferSnapshot(const QString& filePath, ImageBuffer& outBuffer, QString* errOut) {
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (errOut) *errOut = QObject::tr("Cannot open snapshot file.");
        return false;
    }
    const QByteArray compressed = in.readAll();
    in.close();

    const QByteArray data = qUncompress(compressed);
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);

    // Check magic signature for native snapshot format
    if (data.size() < 4 || std::memcmp(data.constData(), "TSNP", 4) != 0) {
        // Fallback for legacy projects (plain FITS snapshots)
        QString err;
        if (FitsLoader::load(filePath, outBuffer, &err)) return true;
        if (outBuffer.loadStandard(filePath)) return true;
        if (errOut) *errOut = QObject::tr("Failed to load legacy FITS snapshot.");
        return false;
    }

    stream >> outBuffer;
    if (stream.status() != QDataStream::Ok) {
        if (errOut) *errOut = QObject::tr("Snapshot data corruption or incompatible version.");
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// Section 12: Default Display Mode
// ---------------------------------------------------------------------------

ImageBuffer::DisplayMode MainWindow::getDefaultDisplayMode() const {
    QString stretchStr = m_settings.value("display/default_stretch", "Linear").toString();
    if (stretchStr == "AutoStretch") return ImageBuffer::Display_AutoStretch;
    if (stretchStr == "ArcSinh")    return ImageBuffer::Display_ArcSinh;
    if (stretchStr == "Log")        return ImageBuffer::Display_Log;
    if (stretchStr == "Sqrt")       return ImageBuffer::Display_Sqrt;
    if (stretchStr == "Histogram")  return ImageBuffer::Display_Histogram;
    return ImageBuffer::Display_Linear;
}


// ---------------------------------------------------------------------------
// Section 13: Image Window Creation
// ---------------------------------------------------------------------------

/**
 * @brief Creates a new MDI sub-window displaying the given image buffer.
 *        Handles viewer setup, signal connections, window positioning, and sizing.
 */
CustomMdiSubWindow* MainWindow::createNewImageWindow(const ImageBuffer& buffer, const QString& title,
                                                      ImageBuffer::DisplayMode mode,
                                                      float autoStretchMedian, bool displayLinked)
{
    if (static_cast<int>(mode) == -1) {
        mode = getDefaultDisplayMode();
    }

    // --- Create and configure the ImageViewer ---
    ImageViewer* viewer = new ImageViewer(this);

    ImageBuffer bufCopy = buffer;
    bufCopy.setMetadata(buffer.metadata());

    viewer->setDisplayState(mode, displayLinked);
    viewer->setAutoStretchMedian(autoStretchMedian);
    viewer->setBuffer(bufCopy, title);

    // Connect history/modification tracking
    connect(viewer, &ImageViewer::historyChanged,  this, &MainWindow::updateMenus);
    connect(viewer, &ImageViewer::historyChanged,  this, &MainWindow::markWorkspaceProjectDirty);
    connect(viewer, &ImageViewer::historyChanged,  this, [this, viewer]() {
        if (currentViewer() == viewer && m_historyPanel) {
            m_historyPanel->updateHistory(viewer);
        }
    });
    connect(viewer, &ImageViewer::bufferChanged,   this, &MainWindow::markWorkspaceProjectDirty);
    connect(viewer, &ImageViewer::modifiedChanged,  this, &MainWindow::markWorkspaceProjectDirty);

    // Cleanup state maps on viewer destruction
    connect(viewer, &QObject::destroyed, this, [this, viewer](){
        m_ghsStates.remove(viewer);
        m_curvesStates.remove(viewer);
        m_satStates.remove(viewer);
    });

    // Connect pixel info and view sync signals
    connect(viewer, &ImageViewer::pixelInfoUpdated, this, &MainWindow::updatePixelInfo);
    connect(viewer, &ImageViewer::viewChanged,      this, &MainWindow::propagateViewChange);
    connect(viewer, &ImageViewer::requestNewView,   this, [this, viewer](const ImageBuffer& img, const QString& title){
        auto mode   = viewer->getDisplayMode();
        auto median = viewer->getAutoStretchMedian();
        auto linked = viewer->isDisplayLinked();
        QString childTitle = buildChildTitle(viewer->windowTitle(), "_" + title.toLower().remove(' '));
        createNewImageWindow(img, childTitle, mode, median, linked);
    });

    // --- Create the MDI sub-window ---
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    sub->setWidget(viewer);
    sub->setSubWindowTitle(title);

    // Preserve maximized state of existing windows
    QList<QMdiSubWindow*> existingWindows = m_mdiArea->subWindowList();
    QSet<QMdiSubWindow*> wasMaximized;
    for (auto* w : existingWindows) {
        if (w->isMaximized()) {
            wasMaximized.insert(w);
        }
    }

    m_mdiArea->addSubWindow(sub);
    connectSubwindowProjectTracking(sub);
    sub->show();
    sub->showNormal();
    m_mdiArea->setActiveSubWindow(sub);
    sub->raise();

    // --- Connect to right sidebar for collapsed-view thumbnails ---
    if (m_rightSidebar) {
        connect(sub, &CustomMdiSubWindow::shadingChanged, this, [this, sub](bool shaded, const QPixmap& thumb) {
            if (shaded) {
                int creationIdx = m_mdiArea->subWindowList(QMdiArea::CreationOrder).indexOf(sub);
                m_rightSidebar->addThumbnail(sub, thumb, sub->windowTitle(), creationIdx);
                if (m_rightSidebar->isHideMinimizedViewsEnabled()) {
                    sub->hide();
                }
            } else {
                m_rightSidebar->removeThumbnail(sub);
            }
        });

        connect(sub, &CustomMdiSubWindow::destroyed, this, [this, sub]() {
            if (m_rightSidebar) m_rightSidebar->removeThumbnail(sub);
        });
    }

    // Restore maximized state of windows that were maximized before
    if (!wasMaximized.isEmpty()) {
        for (auto* w : wasMaximized) {
            if (w != sub && !w->isMaximized()) {
                w->showMaximized();
            }
        }
    }

    // --- Cascading Window Placement ---
    int areaW = m_mdiArea->viewport()->width();
    int areaH = m_mdiArea->viewport()->height();

    // Compute window size preserving aspect ratio, capped at 75% of viewport
    int imgW = buffer.width();
    int imgH = buffer.height();
    int winW, winH;

    if (imgW > 0 && imgH > 0) {
        double aspect = (double)imgW / imgH;
        int maxW = std::max(400, (int)(areaW * 0.75));
        int maxH = std::max(300, (int)(areaH * 0.75));
        if ((double)maxW / maxH > aspect) {
            winH = maxH;
            winW = (int)(winH * aspect);
        } else {
            winW = maxW;
            winH = (int)(winW / aspect);
        }
        winW = std::max(winW, 300);
        winH = std::max(winH, 200);
    } else {
        winW = 800;
        winH = 600;
    }

    // Center position with cascade offset
    int startX = std::max(0, (areaW - winW) / 2);
    int startY = std::max(0, (areaH - winH) / 2);

    int count = m_mdiArea->subWindowList().size();
    int index = std::max(0, count - 1);
    int step  = 25;

    int availableH = areaH - winH - startY;
    int maxSteps   = (availableH > 0) ? (availableH / step) : 0;
    if (maxSteps < 2) maxSteps = 5;

    int cascadeIdx = index % (maxSteps + 1);
    int batchIdx   = index / (maxSteps + 1);

    int x = startX + (cascadeIdx * step) + (batchIdx * step);
    int y = startY + (cascadeIdx * step);

    // Safety bounds
    if (x + winW > areaW) x = std::max(0, areaW - winW);
    if (y + winH > areaH) y = std::max(0, areaH - winH);

    sub->move(x, y);
    sub->resize(winW, winH);
    viewer->fitToWindow();

    if (m_workspaceProject.active && !m_restoringWorkspaceProject) {
        markWorkspaceProjectDirty();
    }

    return sub;
}


// ---------------------------------------------------------------------------
// Section 14: View Synchronization
// ---------------------------------------------------------------------------

/**
 * @brief Propagates zoom/pan changes from one viewer to all linked viewers.
 */
void MainWindow::propagateViewChange(float scale, float hVal, float vVal) {
    ImageViewer* senderViewer = qobject_cast<ImageViewer*>(sender());
    if (!senderViewer) return;

    QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList();
    for (QMdiSubWindow* sub : windows) {
        ImageViewer* v = qobject_cast<ImageViewer*>(sub->widget());
        if (!v) v = sub->widget()->findChild<ImageViewer*>();

        if (v && v != senderViewer && v->isLinked()) {
            v->blockSignals(true);
            v->syncView(scale, hVal, vVal);
            v->blockSignals(false);
        }
    }
}


// ---------------------------------------------------------------------------
// Section 15: Background Image Loading
// ---------------------------------------------------------------------------

/**
 * @brief Loads all images from a single file path. May produce multiple results
 *        for multi-extension FITS or multi-image XISF files.
 *        Designed to run safely on any thread.
 */
static QList<ImageFileLoadResult> loadImageFile(const QString& path)
{
    QList<ImageFileLoadResult> out;
    QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    QString err;

    // --- FITS ---
    if (ext == "fits" || ext == "fit") {
        QMap<QString, FitsExtensionInfo> exts = FitsLoader::listExtensions(path, &err);
        if (exts.isEmpty()) {
            ImageFileLoadResult r;
            if (FitsLoader::load(path, r.buffer, &err)) {
                r.success = true; r.title = fi.fileName(); r.sourcePath = path;
                r.logMsg  = QCoreApplication::translate("MainWindow", "Opened: %1").arg(fi.fileName());
                r.logLevel = ImageLog_Success; r.logPopup = true;
            } else {
                r.logMsg  = QCoreApplication::translate("MainWindow", "Failed to load %1: %2").arg(fi.fileName()).arg(err);
                r.logLevel = ImageLog_Error;
            }
            out << r; return out;
        }

        QList<FitsExtensionInfo> sorted = exts.values();
        std::sort(sorted.begin(), sorted.end(),
                  [](const FitsExtensionInfo& a, const FitsExtensionInfo& b){ return a.index < b.index; });

        bool anyLoaded = false;
        for (const auto& info : sorted) {
            ImageFileLoadResult r; QString extErr;
            if (FitsLoader::loadExtension(path, info.index, r.buffer, &extErr)) {
                r.success = true; r.title = fi.fileName(); r.sourcePath = path;
                if (sorted.size() > 1) r.title += QString(" [%1]").arg(info.name);
                anyLoaded = true;
            } else {
                r.logMsg  = QCoreApplication::translate("MainWindow", "Failed to load extension %1: %2").arg(info.name).arg(extErr);
                r.logLevel = ImageLog_Error;
            }
            out << r;
        }
        if (anyLoaded) {
            ImageFileLoadResult s;
            s.logMsg   = QCoreApplication::translate("MainWindow", "Opened FITS: %1 (%2 extensions)").arg(fi.fileName()).arg(sorted.size());
            s.logLevel = ImageLog_Success; s.logPopup = true;
            out << s;
        }
        return out;
    }

    // --- XISF ---
    if (ext == "xisf") {
        QList<XISFImageInfo> imgs = XISFReader::listImages(path, &err);
        if (imgs.isEmpty()) {
            ImageFileLoadResult r;
            if (XISFReader::read(path, r.buffer, &err)) {
                r.success = true; r.title = fi.fileName(); r.sourcePath = path;
                r.logMsg  = QCoreApplication::translate("MainWindow", "Opened: %1").arg(fi.fileName());
                r.logLevel = ImageLog_Success; r.logPopup = true;
            } else {
                r.logMsg  = QCoreApplication::translate("MainWindow", "Failed to load %1: %2").arg(fi.fileName()).arg(err);
                r.logLevel = ImageLog_Error;
            }
            out << r; return out;
        }

        bool anyLoaded = false;
        for (const auto& info : imgs) {
            ImageFileLoadResult r; QString imgErr;
            if (XISFReader::readImage(path, info.index, r.buffer, &imgErr)) {
                r.success = true; r.title = fi.fileName(); r.sourcePath = path;
                if (imgs.size() > 1) r.title += QString(" [%1]").arg(info.name);
                anyLoaded = true;
            } else {
                r.logMsg  = QCoreApplication::translate("MainWindow", "Failed to load XISF image %1: %2").arg(info.name).arg(imgErr);
                r.logLevel = ImageLog_Error;
            }
            out << r;
        }
        if (anyLoaded) {
            ImageFileLoadResult s;
            s.logMsg   = QCoreApplication::translate("MainWindow", "Opened XISF: %1 (%2 images)").arg(fi.fileName()).arg(imgs.size());
            s.logLevel = ImageLog_Success; s.logPopup = true;
            out << s;
        }
        return out;
    }

    // --- TIFF ---
    if (ext == "tiff" || ext == "tif") {
        ImageFileLoadResult r; QString dbg;
        bool ok = r.buffer.loadTiff32(path, &err, &dbg);
        if (!ok) ok = r.buffer.loadStandard(path);
        if (ok) {
            r.success = true; r.title = fi.fileName(); r.sourcePath = path;
            r.logMsg  = QCoreApplication::translate("MainWindow", "Opened: %1").arg(fi.fileName());
            r.logLevel = ImageLog_Success; r.logPopup = true;
        } else {
            r.logMsg  = QCoreApplication::translate("MainWindow", "Failed to load %1: %2").arg(fi.fileName()).arg(err);
            r.logLevel = ImageLog_Error;
        }
        out << r; return out;
    }

    // --- RAW Camera Files ---
    if (RawLoader::isSupportedExtension(ext)) {
#ifdef HAVE_LIBRAW
        ImageFileLoadResult r; QString rawErr;
        if (RawLoader::load(path, r.buffer, &rawErr)) {
            r.buffer.setName(fi.fileName());
            r.success = true; r.title = fi.fileName(); r.sourcePath = path;
            if (!r.buffer.metadata().bayerPattern.isEmpty() &&
                r.buffer.metadata().bayerPattern != "XTRANS") {
                r.logMsg = QCoreApplication::translate("MainWindow",
                    "Opened RAW: %1 (Bayer pattern: %2) -- use Debayer to convert to colour.")
                    .arg(fi.fileName()).arg(r.buffer.metadata().bayerPattern);
            } else {
                r.logMsg = QCoreApplication::translate("MainWindow", "Opened RAW: %1").arg(fi.fileName());
            }
            r.logLevel = ImageLog_Success; r.logPopup = true;
        } else {
            r.logMsg  = QCoreApplication::translate("MainWindow", "Failed to load RAW %1: %2").arg(fi.fileName()).arg(rawErr);
            r.logLevel = ImageLog_Error;
        }
        out << r;
#else
        { ImageFileLoadResult r;
          r.logMsg  = QCoreApplication::translate("MainWindow",
              "%1: RAW support not available (compiled without LibRaw).").arg(fi.fileName());
          r.logLevel = ImageLog_Error; out << r; }
#endif
        return out;
    }

    // --- Fallback (PNG, JPG, BMP, etc.) ---
    {
        ImageFileLoadResult r;
        if (r.buffer.loadStandard(path)) {
            r.success = true; r.title = fi.fileName(); r.sourcePath = path;
            r.logMsg  = QCoreApplication::translate("MainWindow", "Opened: %1").arg(fi.fileName());
            r.logLevel = ImageLog_Success; r.logPopup = true;
        } else {
            r.logMsg  = QCoreApplication::translate("MainWindow", "Failed to load %1").arg(fi.fileName());
            r.logLevel = ImageLog_Error;
        }
        out << r;
    }
    return out;
}

/**
 * @brief Returns a shared thread pool for image loading, reserving cores for UI.
 */
static QThreadPool* getImageLoadThreadPool() {
    static QThreadPool pool;
    static bool initialized = false;
    if (!initialized) {
        int idealThreads  = QThread::idealThreadCount();
        int workerThreads = std::max(1, idealThreads - 2);
        pool.setMaxThreadCount(workerThreads);
        initialized = true;
    }
    return &pool;
}

/**
 * @brief Processes one loaded image from the queue. Called by timer on UI thread.
 */
void MainWindow::processImageLoadQueue() {
    std::shared_ptr<ImageFileLoadResult> ptr;
    bool isQueueNowEmpty = false;
    {
        QMutexLocker lock(&m_imageLoadMutex);
        if (m_imageLoadQueue.isEmpty()) {
            m_imageDisplayTimer->stop();
            m_isLoadingBatch = false;
            return;
        }
        ptr = m_imageLoadQueue.dequeue();
        isQueueNowEmpty = m_imageLoadQueue.isEmpty();
    }

    if (ptr->success) {
        checkAndHandleColorProfile(ptr->buffer, ptr->title);
        ImageBuffer::DisplayMode mode = getDefaultDisplayMode();
        CustomMdiSubWindow* sub = createNewImageWindow(ptr->buffer, ptr->title, mode);
        if (sub && sub->viewer()) {
            sub->viewer()->setFilePath(ptr->sourcePath);
            sub->viewer()->setModified(false);
        }
    }

    if (!ptr->logMsg.isEmpty()) {
        MainWindow::LogType logType = Log_Info;
        if      (ptr->logLevel == ImageLog_Success) logType = Log_Success;
        else if (ptr->logLevel == ImageLog_Error)   logType = Log_Error;
        log(ptr->logMsg, logType, ptr->logPopup);
    }

    if (isQueueNowEmpty) {
        m_imageDisplayTimer->stop();
        m_isLoadingBatch = false;
        showConsoleTemporarily(2000);
    }
}


// ---------------------------------------------------------------------------
// Section 16: Open / Save File
// ---------------------------------------------------------------------------

void MainWindow::openFile() {
    QString filter =
        tr("All Supported (*.fits *.fit *.tiff *.tif *.png *.jpg *.jpeg *.xisf "
           "*.cr2 *.cr3 *.crw *.nef *.nrw *.arw *.dng *.orf *.rw2 *.raf "
           "*.pef *.raw *.mrw *.srw *.erf *.mef *.mos *.x3f);;") +
        tr("FITS Files (*.fits *.fit);;") +
        tr("XISF Files (*.xisf);;") +
        tr("TIFF Files (*.tiff *.tif);;") +
        tr("Images (*.png *.jpg *.jpeg);;") +
#ifdef HAVE_LIBRAW
        tr("RAW Camera Files (*.cr2 *.cr3 *.crw *.nef *.nrw *.arw *.dng "
           "*.orf *.ori *.rw2 *.raf *.pef *.ptx *.raw *.rwl *.mrw *.srw "
           "*.erf *.mef *.mos *.x3f *.3fr *.fff *.gpr *.kdc *.k25 *.mdc "
           "*.ari *.obm *.r3d *.bay *.cap *.iiq *.eip *.srw2);;") +
#endif
        tr("All Files (*)");

    QString startDir = m_lastDialogDir;
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        startDir = getProjectWorkingDirectory();
    }

    QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open Image(s)"), startDir, filter);
    if (paths.isEmpty()) return;

    m_isLoadingBatch = true;

    // Remember last used directory
    updateLastUsedDir(paths.first());

    int total = paths.size();
    auto loadedCount = std::make_shared<std::atomic<int>>(0);

    // Launch parallel loading in background threads
    (void)QtConcurrent::run([this, paths, total, loadedCount]() {
        QList<std::shared_ptr<ImageFileLoadResult>> allResults;
        QMutex resultsMutex;

        int maxThreads = std::max(1, QThread::idealThreadCount() - 1);
        QThreadPool pool;
        pool.setMaxThreadCount(maxThreads);

        QList<QFuture<void>> futures;
        for (const QString& path : paths) {
            futures << QtConcurrent::run(&pool, [this, path, total, loadedCount, &allResults, &resultsMutex]() {
                QList<ImageFileLoadResult> results = loadImageFile(path);

                int current = ++(*loadedCount);
                QMetaObject::invokeMethod(this, [this, current, total]() {
                    log(tr("Loading image %1/%2...").arg(current).arg(total), Log_Info, false, true);
                }, Qt::QueuedConnection);

                QMutexLocker lock(&resultsMutex);
                for (auto& r : results) {
                    allResults << std::make_shared<ImageFileLoadResult>(std::move(r));
                }
            });
        }

        for (auto& f : futures) f.waitForFinished();

        // Transfer results to UI display queue
        QMetaObject::invokeMethod(this, [this, allResults]() {
            {
                QMutexLocker lock(&m_imageLoadMutex);
                for (auto& res : allResults) {
                    m_imageLoadQueue.enqueue(res);
                }
            }
            if (!m_imageDisplayTimer->isActive())
                m_imageDisplayTimer->start(100);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::saveFile() {
    ImageViewer* v = currentViewer();
    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    // File dialog
    QString selectedFilter;
    QString startDir = getProjectWorkingDirectory();

    QString path = QFileDialog::getSaveFileName(this, tr("Save Image As"), startDir,
        tr("FITS (*.fits);;XISF (*.xisf);;TIFF (*.tif *.tiff);;PNG (*.png);;JPG (*.jpg)"), &selectedFilter);

    if (!path.isEmpty()) {
        updateLastUsedDir(path);
    }
    if (path.isEmpty()) return;

    // Determine format from filter and extension
    QString format = "PNG";
    if      (selectedFilter.contains("FITS")) format = "FITS";
    else if (selectedFilter.contains("XISF")) format = "XISF";
    else if (selectedFilter.contains("TIFF")) format = "TIFF";
    else if (selectedFilter.contains("JPG"))  format = "JPG";

    if      (path.endsWith(".fits", Qt::CaseInsensitive) || path.endsWith(".fit", Qt::CaseInsensitive))  format = "FITS";
    else if (path.endsWith(".xisf", Qt::CaseInsensitive))                                                format = "XISF";
    else if (path.endsWith(".tiff", Qt::CaseInsensitive) || path.endsWith(".tif", Qt::CaseInsensitive))  format = "TIFF";
    else if (path.endsWith(".png", Qt::CaseInsensitive))                                                 format = "PNG";
    else if (path.endsWith(".jpg", Qt::CaseInsensitive))                                                 format = "JPG";

    // --- Save Options Dialog ---
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save Options"));
    QFormLayout* layout = new QFormLayout(&dlg);

    QComboBox* depthBox = new QComboBox(&dlg);
    QCheckBox* burnBox  = new QCheckBox(tr("Burn Annotations"), &dlg);
    burnBox->setChecked(false);

    // Check if annotator is active
    if (m_annotatorDlg && m_annotatorDlg->isVisible()) {
        burnBox->setChecked(true);
    } else {
        burnBox->setEnabled(false);
        burnBox->setChecked(false);
        burnBox->setToolTip(tr("Open Image Annotator first"));
    }

    if (format == "FITS" || format == "TIFF" || format == "XISF") {
        depthBox->addItems({tr("32-bit Float"), tr("32-bit Integer"), tr("16-bit Integer"), tr("8-bit Integer")});
        if (format != "TIFF") {
            burnBox->setChecked(false);
            burnBox->setEnabled(false);
            burnBox->setToolTip(tr("Cannot burn annotations into raw data formats (FITS/XISF)"));
        }
    } else {
        if (format == "PNG") {
            depthBox->addItems({tr("16-bit Integer"), tr("8-bit Integer")});
        } else {
            depthBox->addItems({tr("8-bit Integer")});
        }
    }

    layout->addRow(tr("Format:"),    new QLabel(format));
    layout->addRow(tr("Bit Depth:"), depthBox);
    layout->addRow(tr(""),           burnBox);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    // --- Burn Annotations Path ---
    if (burnBox->isChecked() && m_annotatorDlg) {
        QImage displayImg = v->getBuffer().getDisplayImage(v->getDisplayMode(), v->isDisplayLinked(), nullptr, 0, 0, false, v->isDisplayInverted(),
            v->isDisplayFalseColor(), v->getAutoStretchMedian());

        QPainter p(&displayImg);
        m_annotatorDlg->renderAnnotations(p, QRectF(displayImg.rect()));
        p.end();

        if (!displayImg.save(path, format.toLatin1().constData())) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to save image with annotations."));
        } else {
            v->setFilePath(path);
            log(tr("Saved with Annotations: %1").arg(path), Log_Success, true);
            showConsoleTemporarily(2000);
        }
        return;
    }

    // --- Standard Save Path ---
    QString dStr = depthBox->currentText();
    ImageBuffer::BitDepth d = ImageBuffer::Depth_8Int;

    if      (dStr.contains(tr("32-bit Float")))   d = ImageBuffer::Depth_32Float;
    else if (dStr.contains(tr("32-bit Integer"))) d = ImageBuffer::Depth_32Int;
    else if (dStr.contains(tr("16-bit")))         d = ImageBuffer::Depth_16Int;
    else if (dStr.contains(tr("8-bit")))          d = ImageBuffer::Depth_8Int;

    ImageBuffer bufToSave = v->getBuffer();
    ImageBuffer::Metadata meta = bufToSave.metadata();

    // Appending HISTORY/PROGRAM annotations for FITS/XISF
    if (format == "FITS" || format == "XISF") {
        // Find the position of existing PROGRAM keyword to preserve ordering
        int programPos = -1;
        for (size_t i = 0; i < meta.rawHeaders.size(); ++i) {
            if (meta.rawHeaders[i].key.trimmed().toUpper() == "PROGRAM") {
                programPos = static_cast<int>(i);
                break;
            }
        }

        // Remove all existing PROGRAM entries
        auto removeProgram = std::remove_if(meta.rawHeaders.begin(), meta.rawHeaders.end(),
                                           [](const auto& card) {
                                               return card.key.trimmed().toUpper() == "PROGRAM";
                                           });
        meta.rawHeaders.erase(removeProgram, meta.rawHeaders.end());

        QString tstarVersion = QString(TStar::getVersion());

        // Reinsert PROGRAM at its original position or at the beginning if it didn't exist
        if (programPos >= 0 && programPos < static_cast<int>(meta.rawHeaders.size())) {
            meta.rawHeaders.insert(meta.rawHeaders.begin() + programPos,
                                  {"PROGRAM", "TStar v" + tstarVersion, "Edited with TStar"});
        } else if (programPos >= 0) {
            meta.rawHeaders.push_back({"PROGRAM", "TStar v" + tstarVersion, "Edited with TStar"});
        } else {
            meta.rawHeaders.insert(meta.rawHeaders.begin(),
                                  {"PROGRAM", "TStar v" + tstarVersion, "Edited with TStar"});
        }

        // Append all sequential history steps with numbered labels to ensure all steps are preserved
        const auto& undoDescs = v->undoDescriptions();
        for (size_t i = 0; i < undoDescs.size(); ++i) {
            QString histLabel = QString("HISTORY %1").arg(i + 1);
            meta.rawHeaders.push_back({histLabel, "Modification: " + undoDescs[i], ""});
        }
        meta.rawHeaders.push_back({"HISTORY", "Saved with TStar v" + tstarVersion, ""});
    }
    bufToSave.setMetadata(meta);

    QString err;
    if (!bufToSave.save(path, format, d, &err)) {
        QMessageBox::critical(this, tr("Error"), tr("Save Failed: ") + err);
    } else {
        v->setFilePath(path);
        v->setModified(false);
        log(tr("Saved: %1").arg(path), Log_Success, true);
        showConsoleTemporarily(2000);
    }
}

// ---------------------------------------------------------------------------
// Section 16: Minigame
// ---------------------------------------------------------------------------

void MainWindow::openMinigame() {
    // Construct the path to the minigame HTML file
    QString minigamePath = QCoreApplication::applicationDirPath() + "/minigame/index.html";
    
    // Try the macOS bundle structure as fallback
    if (!QFile::exists(minigamePath)) {
        minigamePath = QCoreApplication::applicationDirPath() + "/../Resources/minigame/index.html";
    }
    
    // Try source build directory
    if (!QFile::exists(minigamePath)) {
        minigamePath = QCoreApplication::applicationDirPath() + "/src/minigame/index.html";
    }
    
    if (!QFile::exists(minigamePath)) {
        QMessageBox::warning(this, tr("Minigame Not Found"), 
                            tr("Could not locate minigame/index.html. Tried: %1").arg(minigamePath));
        log(tr("Minigame file not found at: %1").arg(minigamePath), Log_Error);
        return;
    }
    
    // Open the HTML file in the default web browser
    QDesktopServices::openUrl(QUrl::fromLocalFile(minigamePath));
    log(tr("Minigame launched: %1").arg(minigamePath), Log_Success);
}

// ---------------------------------------------------------------------------
// Section 17: Channel Operations
// ---------------------------------------------------------------------------

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
    ImageBuffer::DisplayMode srcMode   = v->getDisplayMode();
    float                    srcMedian = v->getAutoStretchMedian();
    bool                     srcLinked = v->isDisplayLinked();
    QString suffixes[] = { "_R", "_G", "_B" };

    for (size_t i = 0; i < channels.size(); ++i) {
        if (i < 3) {
            channels[i].addHistoryAction(tr("extracted %1 channel").arg(suffixes[i].mid(1)));
            createNewImageWindow(channels[i], buildChildTitle(baseTitle, suffixes[i]), srcMode, srcMedian, srcLinked);
        }
    }

    log("Extracted channels for " + baseTitle, Log_Success);
    showConsoleTemporarily(2000);
}

void MainWindow::combineChannels() {
    std::vector<ChannelCombinationDialog::ChannelSource> sources;
    QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList();

    for (QMdiSubWindow* win : windows) {
        CustomMdiSubWindow* cWin = qobject_cast<CustomMdiSubWindow*>(win);
        if (cWin) {
            ImageViewer* v = cWin->widget()->findChild<ImageViewer*>();
            if (v && v->getBuffer().isValid()) {
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
        result.addHistoryAction(tr("Combined Channels (RGB)"));
        createNewImageWindow(result, "Combined_RGB");
        log("Channels Combined", Log_Success);
        showConsoleTemporarily(2000);
    });

    log(tr("Opening Combine Channels Tool..."), Log_Info, true);
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Combine Channels"));
    centerToolWindow(sub);
}


// ---------------------------------------------------------------------------
// Section 18: Tool Dialog Openers
// ---------------------------------------------------------------------------

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
            centerToolWindow(sub);
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

    try {
        m_stretchDlg = new StretchDialog(nullptr);
        m_stretchDlg->setViewer(viewer);
        m_stretchDlg->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(m_stretchDlg, &StretchDialog::applied, this, [this](const QString& msg){
            log(msg, Log_Success, true);
            showConsoleTemporarily(2000);
        });

        log(tr("Opening Statistical Stretch..."), Log_Info, true);
        CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
        setupToolSubwindow(sub, m_stretchDlg, tr("Statistical Stretch"));
        sub->resize(500, 550);
        centerToolWindow(sub);

        connect(m_stretchDlg, &QDialog::accepted, this, [this](){
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
        if (m_stretchDlg) { delete m_stretchDlg; m_stretchDlg = nullptr; }
    } catch (...) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to open Statistical Stretch dialog: Unknown error"));
        if (m_stretchDlg) { delete m_stretchDlg; m_stretchDlg = nullptr; }
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
    dlg->setViewer(v);

    log(tr("Opening Rotate / Crop Tool..."), Log_Info, true);
    setupToolSubwindow(nullptr, dlg, tr("Rotate / Crop"));
}

void MainWindow::openCbeDialog() {
    if (activateTool(tr("Catalog Background Extraction"))) return;
    auto v = currentViewer();
    if (!v) {
        log(tr("No image loaded for CBE."), Log_Warning, true);
        return;
    }

    log(tr("Opening Catalog Background Extractor..."), Log_Action);
    auto dlg = new CBEDialog(this, v, v->getBuffer());
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Catalog Background Extraction"));
    m_cbeDlg = dlg;

    connect(dlg, &CBEDialog::applyResult, [this, v](const ImageBuffer& res) {
        ImageBuffer::DisplayMode srcMode   = v->getDisplayMode();
        float                    srcMedian = v->getAutoStretchMedian();
        bool                     srcLinked = v->isDisplayLinked();
        QString newName = buildChildTitle(v->windowTitle(), "_cbe");
        ImageBuffer resBuffer = res;
        resBuffer.setName(newName);
        createNewImageWindow(resBuffer, newName, srcMode, srcMedian, srcLinked);
        log(tr("CBE successful."), Log_Success, true);
        showConsoleTemporarily(2000);
    });
    connect(dlg, &CBEDialog::progressMsg, this, [this](const QString& msg){ log(msg, Log_Info); });

    centerToolWindow(sub);
    sub->raise();
    dlg->setFocus();
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
    m_abeDlg = dlg;

    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("ABE"));
    centerToolWindow(sub);

    connect(dlg, &ABEDialog::applyResult, [this, v](const ImageBuffer& res) {
        v->pushUndo(tr("ABE applied"));
        v->setBuffer(res, v->windowTitle(), true);
        log(tr("ABE applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });
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
        showConsoleTemporarily(2000);
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

    m_satDlg = new SaturationDialog(this, viewer);
    m_satDlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(m_satDlg, &SaturationDialog::applyInternal, this, [this]([[maybe_unused]] const ImageBuffer::SaturationParams& params) {
        log(tr("Saturation applied permanently"), Log_Success, true);
        showConsoleTemporarily(2000);
        if (m_satTarget) updateMenus();
    });
    connect(m_satDlg, &QObject::destroyed, this, [this]() {
        m_satDlg    = nullptr;
        m_satTarget = nullptr;
    });

    m_satTarget = viewer;
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, m_satDlg, tr("Color Saturation"));
    centerToolWindow(sub);
}

void MainWindow::openTemperatureTintDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_tempTintDlg) {
        m_tempTintDlg->raise();
        m_tempTintDlg->activateWindow();
        m_tempTintDlg->setViewer(viewer);
        return;
    }

    log(tr("Opening Temperature / Tint tool..."), Log_Action, true);

    m_tempTintDlg = new TemperatureTintDialog(this, viewer);
    m_tempTintDlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(m_tempTintDlg, &TemperatureTintDialog::applyInternal, this, [this]() {
        log(tr("Temperature / Tint applied permanently"), Log_Success, true);
        showConsoleTemporarily(2000);
        updateMenus();
    });
    connect(m_tempTintDlg, &QObject::destroyed, this, [this]() {
        m_tempTintDlg    = nullptr;
        m_tempTintTarget = nullptr;
    });

    m_tempTintTarget = viewer;
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, m_tempTintDlg, tr("Temperature / Tint"));
    centerToolWindow(sub);
}

void MainWindow::openMagentaCorrectionDialog() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    if (v->getBuffer().channels() < 3) {
        log(tr("Magenta Correction requires a color image."), Log_Warning);
        return;
    }

    if (m_magentaDlg) {
        m_magentaDlg->raise();
        m_magentaDlg->activateWindow();
        m_magentaDlg->setViewer(v);
        return;
    }

    auto dlg = new MagentaCorrectionDialog(this);
    m_magentaDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(dlg, &MagentaCorrectionDialog::apply, this, [this, dlg]() {
        ImageViewer* v = currentViewer();
        if (!v) return;
        float mod_b     = dlg->getAmount();
        float threshold = dlg->getThreshold();
        bool starmask   = dlg->isWithStarMask();

        v->pushUndo(tr("Magenta Correction applied"));
        v->getBuffer().applyMagentaCorrection(mod_b, threshold, starmask);
        v->setBuffer(v->getBuffer(), v->windowTitle(), true);
        log(tr("Magenta Correction applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    log(tr("Opening Magenta Correction Tool..."), Log_Info, true);
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Magenta Correction"));
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

void MainWindow::openRawEditorDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer || !viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please open an image first."));
        return;
    }

    log(tr("Opening RawEditor Tool..."), Log_Info, true);
    RawEditorDialog dlg(viewer, this);
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
        int method   = dlg->getMethod();

        v->pushUndo(tr("SCNR applied"));
        v->getBuffer().applySCNR(amount, method);
        v->setBuffer(v->getBuffer(), v->windowTitle(), true);
        log(tr("SCNR applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    log(tr("Opening SCNR Tool..."), Log_Info, true);
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("SCNR"));
}


// ---------------------------------------------------------------------------
// Section 19: Console Logging
// ---------------------------------------------------------------------------

/**
 * @brief Logs a message to both the file system and the UI console panel.
 *        Handles transient (overwritable) messages for progress updates.
 */
void MainWindow::log(const QString& msg, LogType type, bool autoShow, bool isTransientParam) {
    // --- Log to file system ---
    Logger::Level level = Logger::Info;
    QString prefix = "";

    switch (type) {
    case Log_Info:    level = Logger::Info;    break;
    case Log_Success: level = Logger::Info;    prefix = "[SUCCESS] "; break;
    case Log_Warning: level = Logger::Warning; break;
    case Log_Error:   level = Logger::Error;   break;
    case Log_Action:  level = Logger::Info;    prefix = "[ACTION] "; break;
    }

    Logger::log(level, prefix + msg, "Console");

    // --- Log to UI console ---
    if (!m_sidebar) return;

    QString color = "white";
    if      (type == Log_Success) color = "#90ee90";   // Light green
    else if (type == Log_Warning) color = "orange";
    else if (type == Log_Error)   color = "red";
    else if (type == Log_Action)  color = "#add8e6";   // Light blue

    // Process multi-line messages (e.g. from external tools like GraXpert)
    QStringList lines = msg.split('\n', Qt::KeepEmptyParts);
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        // Clean bridge/python logging metadata
        static QRegularExpression
            logMetadataRe("(\\d{4}-\\d{2}-\\d{2}\\s+\\d{2}:\\d{2}:\\d{2},\\d{3}\\s+MainProcess\\s+.*?\\s+(INFO|DEBUG|WARNING|ERROR)\\s*)|(\\[Bridge\\]\\s*)");
        trimmed.remove(logMetadataRe);

        // Determine if this line is transient (progress/status that should overwrite previous)
        bool isTransient = isTransientParam ||
            trimmed.contains("Progress:", Qt::CaseInsensitive) ||
            trimmed.contains("chunk ", Qt::CaseInsensitive) ||
            trimmed.contains("%") ||
            trimmed.contains("DEBUG:", Qt::CaseInsensitive) ||
            trimmed.contains("INFO:", Qt::CaseInsensitive) ||
            trimmed.contains("Shape=", Qt::CaseInsensitive) ||
            trimmed.contains("patch size", Qt::CaseInsensitive) ||
            trimmed.contains("mode=", Qt::CaseInsensitive) ||
            trimmed.contains("Input:", Qt::CaseInsensitive) ||
            trimmed.contains("Prepared:", Qt::CaseInsensitive) ||
            trimmed.contains("Before sharpen:", Qt::CaseInsensitive) ||
            trimmed.contains("After sharpen:", Qt::CaseInsensitive) ||
            trimmed.contains("tiles...", Qt::CaseInsensitive) ||
            trimmed.contains("Inference finished", Qt::CaseInsensitive) ||
            trimmed.contains(" - ", Qt::CaseInsensitive) ||
            trimmed.contains("Providers :", Qt::CaseInsensitive) ||
            trimmed.contains("Used providers :", Qt::CaseInsensitive) ||
            trimmed.contains("Model enforces", Qt::CaseInsensitive) ||
            trimmed.contains(QRegularExpression("\\d+\\s*/\\s*\\d+"));

        // Override: milestone/success messages should always be persistent
        if (trimmed.contains("RESULT:", Qt::CaseInsensitive) ||
            trimmed.contains("Complete", Qt::CaseInsensitive) ||
            trimmed.contains("Successful", Qt::CaseInsensitive) ||
            trimmed.contains("Saved TIFF", Qt::CaseInsensitive) ||
            trimmed.contains("Apertura", Qt::CaseInsensitive) ||
            trimmed.contains("Avvio", Qt::CaseInsensitive) ||
            trimmed.contains("Loading result", Qt::CaseInsensitive) ||
            type == Log_Success || type == Log_Action) {
            isTransient = false;
        }

        // Strip leading technical tokens from transient messages
        if (isTransient) {
            QStringList tokens = {"Progress:", "chunk ", "INFO:", "DEBUG:"};
            for (const QString& tok : tokens) {
                int idx = trimmed.indexOf(tok, 0, Qt::CaseInsensitive);
                if (idx >= 0) {
                    trimmed = trimmed.mid(idx);
                    break;
                }
            }
        }

        // Format and display
        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
        QString fullMsg = QString("<span style='color:gray'>[%1]</span> <span style='color:%2'>%3</span>")
            .arg(timeStr).arg(color).arg(trimmed);

        if (isTransient && m_lastWasProgress) {
            m_sidebar->updateLastLogLine(fullMsg);
        } else {
            m_sidebar->logToConsole(fullMsg);
        }
        m_lastWasProgress = isTransient;
    }

    // Ensure console panel remains transparent
    if (QWidget* p = m_sidebar->getPanel("Console")) {
        p->setStyleSheet("background-color: transparent; border: none;");
    }

    // Auto-open console on explicit request (suppress during batch loading)
    if (autoShow && !m_isLoadingBatch) {
        showConsoleTemporarily(2000);
    }
}


// ---------------------------------------------------------------------------
// Section 20: Console Auto-Show / Long Process Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Temporarily opens the console panel and auto-closes it after a duration.
 */
void MainWindow::showConsoleTemporarily(int durationMs) {
    if (!m_sidebar) return;
    if (!m_sidebar->isAutoOpenEnabled()) return;

    suppressDirtyFlag(durationMs + 500);

    m_isConsoleTempOpen = true;
    m_sidebar->openPanel("Console");

    if (m_tempConsoleTimer) {
        m_tempConsoleTimer->setInterval(durationMs);
        m_tempConsoleTimer->start();
    }
}

/**
 * @brief Temporarily suppresses workspace dirty-flag marking.
 */
void MainWindow::suppressDirtyFlag(int durationMs) {
    m_dirtySuppressCount++;
    QTimer::singleShot(durationMs, this, [this](){
        m_dirtySuppressCount = std::max(0, m_dirtySuppressCount - 1);
    });
}

/**
 * @brief Opens the console and keeps it open for the duration of a long process.
 */
void MainWindow::startLongProcess() {
    if (!m_sidebar) return;

    QMdiSubWindow* activeSub = m_mdiArea ? m_mdiArea->activeSubWindow() : nullptr;
    m_wasConsoleOpen = m_sidebar->isExpanded();
    m_sidebar->openPanel("Console");

    if (m_tempConsoleTimer && m_tempConsoleTimer->isActive()) {
    m_tempConsoleTimer->stop();
    }

    // Restore focus/z-order to the image
    if (activeSub && m_mdiArea) {
        m_mdiArea->setActiveSubWindow(activeSub);
    }
}

/**
 * @brief Ends a long process and optionally auto-closes the console.
 *
 * If the console was not open before the process started, it is treated
 * as temporarily opened and will auto-close after a short delay.
 */
void MainWindow::endLongProcess() {
    if (!m_sidebar) return;

    if (!m_wasConsoleOpen) {
        m_isConsoleTempOpen = true;
        showConsoleTemporarily(3000);
    }
}


// ============================================================================
// Header Dialogs
// ============================================================================

/**
 * @brief Opens the header panel in the sidebar and populates it with
 *        the current viewer's metadata.
 */
void MainWindow::openHeaderDialog() {
    if (!m_sidebar) return;
    m_sidebar->openPanel("Header");

    // Ensure content is fresh
    ImageViewer* viewer = currentViewer();
    if (viewer && m_headerPanel) {
        m_headerPanel->setMetadata(viewer->getBuffer().metadata());
        m_historyPanel->updateHistory(viewer);
    }
}

/**
 * @brief Opens a modal header editor dialog for the active image viewer.
 */
void MainWindow::openHeaderEditorDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    HeaderEditorDialog dlg(viewer, this);
    dlg.exec();
}


// ============================================================================
// Event Filter
// ============================================================================

/**
 * @brief Global event filter for the main window.
 *
 * Handles:
 *  - Suppressing Alt/AltGr key presses on the hidden menu bar to prevent
 *    it from stealing focus.
 *  - Accepting drag-enter and drop events on the MDI area for the custom
 *    "application/x-tstar-duplicate" MIME type (view duplication via drag).
 */
bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // --- Menu bar: suppress Alt key focus grab ---
    if (obj == menuBar()) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Alt || keyEvent->key() == Qt::Key_AltGr) {
                return true; // Swallow the event
            }
        }
    }

    // --- MDI area: handle view-duplication drag & drop ---
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
                            createNewImageWindow(sourceViewer->getBuffer(), newTitle,
                                sourceViewer->getDisplayMode(),
                                sourceViewer->getAutoStretchMedian(),
                                sourceViewer->isDisplayLinked());
                            log(tr("View Duplicated: ") + newTitle, Log_Success);
                            showConsoleTemporarily(2000);
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


// ============================================================================
// MDI Sub-Window Utilities
// ============================================================================

/**
 * @brief Activates (raises and focuses) an existing tool sub-window by its title.
 * @param title  The window title to search for.
 * @return true if the window was found and activated, false otherwise.
 */
bool MainWindow::activateTool(const QString& title) {
    for (auto* sub : m_mdiArea->subWindowList()) {
        if (auto* csw = qobject_cast<CustomMdiSubWindow*>(sub)) {
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

/**
 * @brief Generates a unique window title by appending or incrementing a
 *        numeric suffix, e.g. "Image" → "Image (2)" → "Image (3)".
 */
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


// ============================================================================
// Window Resize Handling
// ============================================================================

/**
 * @brief Keeps the left and right sidebars properly positioned and sized
 *        whenever the main window is resized.
 */
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (centralWidget()) {
        QRect cw = centralWidget()->geometry();
        if (m_sidebar) {
            m_sidebar->move(cw.x(), cw.y());
            m_sidebar->resize(m_sidebar->totalVisibleWidth(), cw.height());
        }
        if (m_rightSidebar) {
            m_rightSidebar->setAnchorGeometry(cw.right(), cw.y(), cw.height());
        }
    }
}


// ============================================================================
// Tool Dialog Launchers — Aberration / Star Processing
// ============================================================================

/**
 * @brief Opens the RAR (Remove Aberration) dialog as a tool sub-window.
 */
void MainWindow::openRARDialog() {
    ImageViewer* v = currentViewer();
    if (!v) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    // If already open, just raise and update the viewer
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

/**
 * @brief Opens the Star Stretch dialog as a tool sub-window.
 */
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

/**
 * @brief Opens the Image Blending dialog as a tool sub-window.
 */
void MainWindow::openImageBlendingDialog() {
    ImageViewer* v = currentViewer();

    if (m_imageBlendingDlg) {
        m_imageBlendingDlg->raise();
        m_imageBlendingDlg->activateWindow();
        if (v) m_imageBlendingDlg->setViewer(v);
        return;
    }

    log(tr("Opening Image Blending..."), Log_Action, true);
    auto dlg = new ImageBlendingDialog(this);
    m_imageBlendingDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (v) dlg->setViewer(v);

    setupToolSubwindow(nullptr, dlg, tr("Image Blending"));
}

/**
 * @brief Opens the Star Recomposition dialog as a tool sub-window.
 */
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

/**
 * @brief Opens the Perfect Palette dialog as a tool sub-window.
 */
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
    if (v) dlg->setViewer(v); // Set initial viewer

    setupToolSubwindow(nullptr, dlg, tr("Perfect Palette"));
}

/**
 * @brief Checks whether a given viewer is currently being used by a tool
 *        that requires exclusive access (e.g. Star Recomposition).
 * @param viewer    The viewer to check.
 * @param toolName  If non-null, receives the name of the tool using the viewer.
 * @return true if the viewer is in use by a locking tool.
 */
bool MainWindow::isViewerInUse(ImageViewer* viewer, QString* toolName) const {
    if (m_starRecompDlg && m_starRecompDlg->isUsingViewer(viewer)) {
        if (toolName) *toolName = "Star Recomposition";
        return true;
    }
    return false;
}


// ============================================================================
// Geometry Transformations (Rotate / Mirror)
// ============================================================================

/**
 * @brief Applies a named geometry transformation to the active viewer's buffer.
 * @param op  Operation identifier: "rot90", "rot180", "rot270", "mirrorX", "mirrorY".
 *
 * Uses OpenCV-optimized calls via the ImageBuffer API. If the image dimensions
 * change (e.g. 90° rotation), the view is reset; otherwise it is preserved.
 */
void MainWindow::applyGeometry(const QString& op) {
    if (auto v = currentViewer()) {
        // Map operation identifier to human-readable name
        QString name;
        if      (op == "rot90")   name = tr("Rotate CW");
        else if (op == "rot180")  name = tr("Rotate 180");
        else if (op == "rot270")  name = tr("Rotate CCW");
        else if (op == "mirrorX") name = tr("Mirror H");
        else if (op == "mirrorY") name = tr("Mirror V");

        int oldW = v->getBuffer().width();
        int oldH = v->getBuffer().height();
        v->pushUndo(name);

        // Execute the transform
        if      (op == "rot90")   v->getBuffer().rotate90();
        else if (op == "rot180")  v->getBuffer().rotate180();
        else if (op == "rot270")  v->getBuffer().rotate270();
        else if (op == "mirrorX") v->getBuffer().mirrorX();
        else if (op == "mirrorY") v->getBuffer().mirrorY();

        // Reset view if dimensions changed, otherwise preserve
        if (v->getBuffer().width() != oldW || v->getBuffer().height() != oldH) {
            v->refreshDisplay(false);
        } else {
            v->refreshDisplay(true);
        }

        log(tr("Geometry applied: ") + name, Log_Success);
        showConsoleTemporarily(2000);
    }
}

/**
 * @brief Applies a custom geometry transformation via a callable.
 * @param name  Human-readable name for undo and logging.
 * @param func  A function that modifies the ImageBuffer in place.
 */
void MainWindow::applyGeometry(const QString& name, std::function<void(ImageBuffer&)> func) {
    if (auto v = currentViewer()) {
        int oldW = v->getBuffer().width();
        int oldH = v->getBuffer().height();

        v->pushUndo(name);
        func(v->getBuffer());

        if (v->getBuffer().width() != oldW || v->getBuffer().height() != oldH) {
            v->refreshDisplay(false);
        } else {
            v->refreshDisplay(true);
        }

        log(tr("Geometry applied: ") + name, Log_Success);
        showConsoleTemporarily(2000);
    }
}


// ============================================================================
// Tool Dialog Launchers — Stretch / Curves / GHS
// ============================================================================

/**
 * @brief Opens the Generalized Hyperbolic Stretch (GHS) dialog.
 *
 * The GHS dialog is a persistent tool: it is not deleted on close but instead
 * hidden and re-shown. Its lifecycle is managed via the wrapper sub-window.
 * When re-opened, it resets its state and retargets the current viewer.
 */
void MainWindow::openGHSDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    // --- Reactivate existing dialog ---
    if (m_ghsDlg) {
        log(tr("Activating GHS Tool..."), Log_Action, true);
        if (m_ghsTarget != viewer) {
            m_ghsTarget = viewer;
        }

        // Walk up the parent chain to find the containing sub-window
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

        // Fallback: show as standalone widget
        m_ghsDlg->show();
        m_ghsDlg->raise();
        m_ghsDlg->activateWindow();
        m_ghsDlg->onReset();
        if (viewer) m_ghsDlg->setTarget(viewer);
        return;
    }

    // --- Create new GHS dialog ---
    m_ghsTarget = viewer;

    // Clean up if the target view is destroyed while the tool is open
    connect(viewer, &QObject::destroyed, this, [this, viewer]() {
        if (m_ghsTarget == viewer) {
            m_ghsTarget = nullptr;
            if (m_ghsDlg) m_ghsDlg->setTarget(nullptr);
        }
    });

    m_ghsDlg = new GHSDialog(nullptr); // No parent initially — will be reparented by the sub-window
    m_ghsDlg->setAttribute(Qt::WA_DeleteOnClose, false); // Lifecycle managed via wrapper

    log(tr("Opening GHS Tool..."), Log_Action, true);
    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, m_ghsDlg, tr("Generalized Hyperbolic Stretch"));

    sub->resize(450, 650);    // GHS-specific size override
    centerToolWindow(sub);    // Re-center after resize

    // Lifecycle: delete on close to ensure clean reset on reopen
    sub->setAttribute(Qt::WA_DeleteOnClose);

    // Reset pointer when dialog is destroyed
    connect(m_ghsDlg, &QObject::destroyed, [this]() {
        m_ghsDlg = nullptr;
        m_ghsTarget = nullptr;
    });

    // Close the sub-window if the dialog emits finished (e.g. via a Close button)
    connect(m_ghsDlg, &GHSDialog::finished, [sub](int) {
        if (sub) sub->close();
    });

    // Log applied stretches
    connect(m_ghsDlg, &GHSDialog::applied, this, [this](const QString& msg) {
        log(msg, Log_Success, true);
        showConsoleTemporarily(2000);
    });

    if (viewer) m_ghsDlg->setTarget(viewer);
    m_ghsDlg->onReset();

    log(tr("Opened GHS Tool."), Log_Action, true);
}


// ============================================================================
// Tool Dialog Launchers — Binning / Upscale
// ============================================================================

/**
 * @brief Opens the Image Binning dialog as a tool sub-window.
 */
void MainWindow::openBinningDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_binDlg) {
        if (auto sub = qobject_cast<CustomMdiSubWindow*>(m_binDlg->parentWidget())) {
            sub->raise();
            sub->activateWindow();
        } else {
            m_binDlg->raise();
            m_binDlg->activateWindow();
        }
        m_binDlg->setViewer(viewer);
        return;
    }

    auto dlg = new BinningDialog(this);
    m_binDlg = dlg;
    dlg->setViewer(viewer);

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Image Binning"));
    centerToolWindow(sub);

    connect(dlg, &QObject::destroyed, [this]() { m_binDlg = nullptr; });

    log(tr("Opened Binning Tool."), Log_Action, true);
}

/**
 * @brief Opens the Image Upscale dialog as a tool sub-window.
 */
void MainWindow::openUpscaleDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_upscaleDlg) {
        if (auto sub = qobject_cast<CustomMdiSubWindow*>(m_upscaleDlg->parentWidget())) {
            sub->raise();
            sub->activateWindow();
        } else {
            m_upscaleDlg->raise();
            m_upscaleDlg->activateWindow();
        }
        m_upscaleDlg->setViewer(viewer);
        return;
    }

    auto dlg = new UpscaleDialog(this);
    m_upscaleDlg = dlg;
    dlg->setViewer(viewer);

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Image Upscale"));
    centerToolWindow(sub);

    connect(dlg, &QObject::destroyed, [this]() { m_upscaleDlg = nullptr; });

    log(tr("Opened Upscale Tool."), Log_Action, true);
}


// ============================================================================
// Tool Dialog Launchers — Plate Solving / Color Calibration
// ============================================================================

/**
 * @brief Opens the Plate Solving dialog. On success, WCS metadata is written
 *        to the active viewer's buffer.
 */
void MainWindow::openPlateSolvingDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    auto dlg = new PlateSolvingDialog(nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose, false);
    dlg->setViewer(viewer); // Use setViewer to ensure WCS is applied to viewer

    log(tr("Opening Plate Solving..."), Log_Info, true);
    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Plate Solving"));
    sub->resize(sub->width(), sub->height() + 150);
    centerToolWindow(sub);

    connect(dlg, &QDialog::accepted, [this, dlg, sub, viewer]() {
        if (dlg->isSolved()) {
            NativeSolveResult res = dlg->result();
            if (res.success) {
                // Apply WCS coordinates to the image metadata
                ImageBuffer::Metadata meta = viewer->getBuffer().metadata();
                meta.ra     = res.crval1;
                meta.dec    = res.crval2;
                meta.crpix1 = res.crpix1;
                meta.crpix2 = res.crpix2;
                meta.cd1_1  = res.cd[0][0];
                meta.cd1_2  = res.cd[0][1];
                meta.cd2_1  = res.cd[1][0];
                meta.cd2_2  = res.cd[1][1];
                meta.catalogStars = res.catalogStars;

                pushUndo(tr("Plate Solving applied"));
                viewer->getBuffer().setMetadata(meta);
                log(tr("Plate Solved! Center: RA %1, Dec %2").arg(meta.ra).arg(meta.dec), Log_Success, true);
                showConsoleTemporarily(2000);
            }
        }
        sub->close();
    });
}

/**
 * @brief Opens the Photometric Color Calibration (PCC) dialog.
 */
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
    QTimer::singleShot(50, sub, [this, sub]() {
        sub->adjustSize();
        centerToolWindow(sub);
    });

    connect(dlg, &QDialog::accepted, [this, dlg, sub, viewer]() {
        PCCResult res = dlg->result();
        if (res.valid) {
            viewer->setBuffer(viewer->getBuffer(), viewer->windowTitle(), true); // Refresh display
            updateDisplay();
            log(tr("PCC Applied: R=%1 G=%2 B=%3 (BG: %4, %5, %6)")
                .arg(res.R_factor).arg(res.G_factor).arg(res.B_factor)
                .arg(res.bg_r).arg(res.bg_g).arg(res.bg_b), Log_Success, true);
            showConsoleTemporarily(2000);
        }
        sub->close();
    });
}

/**
 * @brief Opens the Spectrophotometric Color Calibration (SPCC) dialog.
 */
void MainWindow::openSPCCDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_spccDlg) {
        m_spccDlg->raise();
        m_spccDlg->activateWindow();
        m_spccDlg->setViewer(viewer);
        return;
    }

    auto dlg = new SPCCDialog(viewer, this, nullptr);
    m_spccDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    log(tr("Opening SPCC..."), Log_Info, true);
    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Spectrophotometric Color Calibration"));
    QTimer::singleShot(50, sub, [this, sub]() {
        sub->adjustSize();
        centerToolWindow(sub);
    });
}

/*
void MainWindow::openDeconvolutionDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }
    if (m_deconvolutionDlg) {
        m_deconvolutionDlg->raise();
        m_deconvolutionDlg->activateWindow();
        m_deconvolutionDlg->setViewer(viewer);
        return;
    }
    auto dlg = new DeconvolutionDialog(viewer, this, nullptr);
    m_deconvolutionDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    log(tr("Opening Deconvolution..."), Log_Info, true);
    setupToolSubwindow(nullptr, dlg, tr("Deconvolution"));
}
*/


// ============================================================================
// Tool Dialog Launchers — Curves Transformation
// ============================================================================

/**
 * @brief Opens the Curves Transformation dialog.
 *
 * Similar to GHS, this is a persistent tool that supports real-time LUT
 * preview. The preview LUT is cleared when the dialog closes or when
 * the target viewer changes.
 */
void MainWindow::openCurvesDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    // --- Reactivate existing dialog ---
    if (m_curvesDlg) {
        log(tr("Activating Curves Tool..."), Log_Action, true);
        if (m_curvesTarget != viewer) {
            if (m_curvesTarget) m_curvesTarget->clearPreviewLUT();
            m_curvesTarget = viewer;
        }

        // Walk up to find the sub-window
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

        // Fallback: show as standalone
        m_curvesDlg->show();
        m_curvesDlg->raise();
        m_curvesDlg->activateWindow();
        if (viewer) m_curvesDlg->setViewer(viewer);
        return;
    }

    // --- Create new Curves dialog ---
    m_curvesTarget = viewer;

    // Clean up if the target view is destroyed
    connect(viewer, &QObject::destroyed, this, [this, viewer]() {
        if (m_curvesTarget == viewer) {
            m_curvesTarget = nullptr;
            if (m_curvesDlg) m_curvesDlg->setViewer(nullptr);
        }
    });

    m_curvesDlg = new CurvesDialog(nullptr); // Will be reparented by sub-window
    m_curvesDlg->setAttribute(Qt::WA_DeleteOnClose, false);

    connect(m_curvesDlg, &QObject::destroyed, [this]() {
        if (m_curvesTarget) m_curvesTarget->clearPreviewLUT();
        m_curvesDlg = nullptr;
        m_curvesTarget = nullptr;
    });

    // Connect preview and apply signals
    connect(m_curvesDlg, &CurvesDialog::previewRequested, this, &MainWindow::applyCurvesPreview);
    connect(m_curvesDlg, &CurvesDialog::applyRequested,   this, &MainWindow::applyCurves);

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, m_curvesDlg, tr("Curves Transformation"));
    sub->resize(650, 500);
    centerToolWindow(sub);

    // Clear preview and close sub-window when dialog finishes
    connect(m_curvesDlg, &QDialog::finished, [this, sub](int) {
        if (m_curvesTarget) {
            m_curvesTarget->clearPreviewLUT();
        }
        sub->close();
    });

    if (viewer) m_curvesDlg->setViewer(viewer);

    log(tr("Opened Curves Tool."), Log_Action, true);
}

/**
 * @brief Applies a real-time LUT preview to the curves target viewer.
 * @param lut  Per-channel lookup table. Empty to clear the preview.
 */
void MainWindow::applyCurvesPreview(const std::vector<std::vector<float>>& lut) {
    if (m_curvesTarget) {
        if (lut.empty()) {
            m_curvesTarget->clearPreviewLUT();
        } else {
            m_curvesTarget->setPreviewLUT(lut);
        }
    }
}

/**
 * @brief Permanently applies a spline curves transformation to the target image.
 * @param spline    The spline data describing the curve.
 * @param channels  Boolean array indicating which channels (R, G, B) to affect.
 */
void MainWindow::applyCurves(const SplineData& spline, const bool channels[3]) {
    if (m_curvesTarget) {
        ImageBuffer& buf = m_curvesTarget->getBuffer();
        m_curvesTarget->pushUndo(tr("Curves applied"));

        buf.applySpline(spline, channels);

        QImage newImg = buf.getDisplayImage(
            m_curvesTarget->getDisplayMode(), m_curvesTarget->isLinked(),
            nullptr, 0, 0, false, false, false,
            m_curvesTarget->getAutoStretchMedian());
        m_curvesTarget->setImage(newImg, true);

        log(tr("Curves applied to %1.").arg(m_curvesTarget->windowTitle()), Log_Success, true);
        showConsoleTemporarily(2000);
    }
}


// ============================================================================
// Background Neutralization
// ============================================================================

/**
 * @brief Opens the Background Neutralization dialog.
 *
 * Requires an RGB (3-channel) image. The dialog allows the user to select
 * a reference region, and applies neutralization on confirmation.
 */
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

    // If already open, raise and update
    if (m_bnDlg) {
        m_bnDlg->raise();
        m_bnDlg->activateWindow();
        if (viewer) m_bnDlg->setViewer(viewer);
        return;
    }

    auto dlg = new BackgroundNeutralizationDialog(this);
    m_bnDlg = dlg;

    // Cleanup on destroy
    connect(dlg, &QObject::destroyed, this, [this]() {
        m_bnDlg = nullptr;
    });

    // Initial setup
    if (auto v = currentViewer()) {
        m_bnDlg->setViewer(v);
        m_bnDlg->setInteractionEnabled(true);
    }

    setupToolSubwindow(nullptr, dlg, tr("Background Neutralization"));

    // Apply handler
    connect(dlg, &BackgroundNeutralizationDialog::apply, [this](const QRect& rect) {
        ImageViewer* v = currentViewer();
        if (!v) return;

        // Validate buffer before processing
        if (!v->getBuffer().isValid() || v->getBuffer().channels() != 3) {
            QMessageBox::warning(this, tr("Error"), tr("Cannot apply: buffer is invalid or not RGB."));
            return;
        }

        // Capture undo state, then neutralize
        v->pushUndo(tr("Background Neutralization applied"));
        ImageBuffer buf = v->getBuffer();
        BackgroundNeutralizationDialog::neutralizeBackground(buf, rect);

        v->setBuffer(buf, v->windowTitle(), true); // Preserve view
        log(tr("Background Neutralization applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });
}


// ============================================================================
// Pixel Math
// ============================================================================

/**
 * @brief Builds a list of all open image viewers as PMImageRef variables
 *        (I1, I2, …) for use in PixelMath expressions.
 *
 * The most recently activated viewer appears first (I1). At most 10 images
 * are included.
 */
QVector<PMImageRef> MainWindow::getImageRefsForPixelMath() const {
    QVector<PMImageRef> result;
    int idx = 1;

    // Walk sub-window list in activation order (last activated = first)
    for (auto* sub : m_mdiArea->subWindowList(QMdiArea::ActivationHistoryOrder)) {
        auto* csw = qobject_cast<CustomMdiSubWindow*>(sub);
        if (!csw || csw->isToolWindow() || !csw->viewer()) continue;

        ImageViewer* v = csw->viewer();
        if (!v->getBuffer().isValid()) continue;

        PMImageRef ref;
        ref.varId  = QString("I%1").arg(idx++);
        ref.name   = csw->windowTitle();
        if (ref.name.isEmpty()) ref.name = v->windowTitle();
        ref.buffer = &v->getBuffer();
        result.append(ref);

        if (idx > 10) break;
    }
    return result;
}

/**
 * @brief Opens the Pixel Math dialog as a tool sub-window.
 */
void MainWindow::openPixelMathDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please open an image first."));
        return;
    }

    // --- Reactivate existing dialog ---
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
        // Fallback: show as independent window
        m_pixelMathDialog->show();
        m_pixelMathDialog->raise();
        m_pixelMathDialog->activateWindow();
        if (viewer) m_pixelMathDialog->setViewer(viewer);
        return;
    }

    // --- Create new Pixel Math dialog ---
    m_pixelMathDialog = new PixelMathDialog(this, nullptr);
    m_pixelMathDialog->setAttribute(Qt::WA_DeleteOnClose, true);

    connect(m_pixelMathDialog, &PixelMathDialog::apply, [this](const QString& expr, bool rescale) {
        ImageViewer* v = currentViewer();
        if (v) {
            pushUndo(tr("Pixel Math applied"));
            QString err;
            QVector<PMImageRef> imgs = getImageRefsForPixelMath();
            if (PixelMathDialog::evaluateExpression(expr, v->getBuffer(), imgs, rescale, &err)) {
                updateActiveImage();
                log(tr("PixelMath Applied") + QString(rescale ? tr(" (Rescaled)") : "") + ": " + expr.left(40) + "...", Log_Success, true);
                showConsoleTemporarily(2000);
            } else {
                QMessageBox::critical(this, tr("PixelMath Error"), err);
                undo();
            }
        }
    });

    setupToolSubwindow(nullptr, m_pixelMathDialog, tr("Pixel Math"));
    if (viewer) m_pixelMathDialog->setViewer(viewer);
    m_pixelMathDialog->setImages(getImageRefsForPixelMath());
}


// ============================================================================
// Tool Sub-Window Setup Helpers
// ============================================================================

/**
 * @brief Creates and configures a CustomMdiSubWindow to host a tool dialog.
 *
 * If @p sub is null, a new sub-window is created. The dialog is set as the
 * sub-window's widget, given a title, marked as a tool window, properly sized,
 * centered, and shown.
 *
 * @param sub    Pre-existing sub-window to use, or nullptr to create a new one.
 * @param dlg    The dialog widget to embed.
 * @param title  The title to display on the sub-window's title bar.
 * @return       The configured sub-window.
 */
CustomMdiSubWindow* MainWindow::setupToolSubwindow(CustomMdiSubWindow* sub, QWidget* dlg, const QString& title) {
    CustomMdiSubWindow* targetSub = sub;
    if (!targetSub) {
        targetSub = new CustomMdiSubWindow(m_mdiArea);
        targetSub->setAttribute(Qt::WA_DeleteOnClose);
    }
    connectSubwindowProjectTracking(targetSub);

    targetSub->setWidget(dlg);
    targetSub->setSubWindowTitle(title);
    targetSub->setToolWindow(true);

    // --- Determine the preferred size ---
    // Use the dialog's explicit size if set, otherwise fall back to sizeHint.
    QSize preferredSize;
    if (dlg) {
        preferredSize = dlg->size();
        if (preferredSize.width() < 100 || preferredSize.height() < 100) {
            preferredSize = dlg->sizeHint();
        }
    }

    // Add space for CustomMdiSubWindow chrome (title bar and borders)
    int titleBarH = 30;
    int borderW   = 6;

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

    // Auto-close the sub-window when the hosted QDialog emits finished()
    if (QDialog* qdlg = qobject_cast<QDialog*>(dlg)) {
        connect(qdlg, &QDialog::finished, [targetSub](int) { targetSub->close(); });
    }

    targetSub->showNormal();
    targetSub->show();
    targetSub->raise();
    targetSub->activateWindow();

    centerToolWindow(targetSub);

    // Track workspace project dirty state
    if (m_workspaceProject.active && !m_restoringWorkspaceProject) {
        markWorkspaceProjectDirty();
    }

    return targetSub;
}

/**
 * @brief Centers a tool sub-window within the MDI area's viewport.
 */
void MainWindow::centerToolWindow(CustomMdiSubWindow* sub) {
    if (!sub || !m_mdiArea) return;

    QRect viewportRect = m_mdiArea->viewport()->rect();
    sub->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, sub->size(), viewportRect));
}


// ============================================================================
// Settings Dialog
// ============================================================================

/**
 * @brief Opens the application settings dialog. Refreshes all viewers
 *        when settings are changed.
 */
void MainWindow::onSettingsAction() {
    // If already open, just raise
    if (m_settingsDlg) {
        m_settingsDlg->raise();
        m_settingsDlg->activateWindow();
        return;
    }

    auto dlg = new SettingsDialog(this);
    m_settingsDlg = dlg;

    connect(dlg, &SettingsDialog::settingsChanged, this, [this]() {
        core::ColorProfileManager::instance().syncSettings();
        updateActiveImage();

        // Refresh all open viewers since display settings are global
        for (auto sub : m_mdiArea->subWindowList()) {
            if (auto csw = qobject_cast<CustomMdiSubWindow*>(sub)) {
                if (auto v = csw->viewer()) {
                    v->refreshDisplay(true);
                }
            }
        }
        log(tr("Settings applied. Display refreshed."), Log_Success);
        showConsoleTemporarily(2000);
    });

    connect(dlg, &QDialog::destroyed, this, [this]() { m_settingsDlg = nullptr; });

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Settings"));
    sub->resize(sub->width(), sub->height());
    centerToolWindow(sub);
}


// ============================================================================
// Display Refresh
// ============================================================================

/**
 * @brief Refreshes the display of the currently active image viewer.
 */
void MainWindow::updateActiveImage() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) return;
    viewer->refreshDisplay(true);
}


// ============================================================================
// Tool Dialog Launchers — Arcsinh Stretch / Histogram Stretch
// ============================================================================

/**
 * @brief Opens the Arcsinh Stretch dialog as a tool sub-window.
 */
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

    connect(dlg, &QDialog::accepted, this, [this, viewer]() {
        log(tr("Arcsinh Stretch Applied to %1").arg(viewer->windowTitle()), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, dlg, tr("Arcsinh Stretch"));
    sub->resize(420, 300);
}

/**
 * @brief Opens the Histogram Transformation dialog as a tool sub-window.
 */
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
    connect(dlg, &QObject::destroyed, this, [this]() {
        m_histoDlg = nullptr;
    });

    // Log when applied
    connect(dlg, &HistogramStretchDialog::applied, this, [this]() {
        log(tr("Histogram Transformation applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Histogram Stretch"));
    sub->resize(520, 600);
    centerToolWindow(sub);
}


// ============================================================================
// Window Show / Close Events & Fade Animations
// ============================================================================

/**
 * @brief Triggers a fade-in animation when the main window is first shown.
 */
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    startFadeIn();
}

/**
 * @brief Handles the main window close event.
 *
 * Prompts the user for confirmation, saves the workspace project if dirty,
 * stops background threads, closes all MDI windows transactionally, saves
 * persistent settings, and initiates a fade-out animation before exiting.
 */
void MainWindow::closeEvent(QCloseEvent* event) {
    // If we are already in the final close, just pass through
    if (m_isClosing) {
        QMainWindow::closeEvent(event);
        return;
    }

    // Check for re-entrant shutdown
    bool shutdownRequested = property("shutdownRequested").toBool();

    // First entry: ask user for confirmation
    if (!shutdownRequested) {
        QMessageBox::StandardButton res = QMessageBox::question(
            this, tr("Exit TStar"), tr("Are you sure you want to exit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (res != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        setProperty("shutdownRequested", true);
    }

    // Offer to save the workspace project
    if (!maybeSaveWorkspaceProject(tr("before exiting the application"))) {
        event->ignore();
        setProperty("shutdownRequested", false);
        return;
    }

    // === Force cleanup of background threads ===
    Threading::setThreadRun(false);
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone(500);

    // Attempt to close all MDI windows transactionally
    if (!closeAllWorkspaceWindows()) {
        event->ignore();
        setProperty("shutdownRequested", false);
        return;
    }

    if (m_isClosing) {
        QMainWindow::closeEvent(event);
        return;
    }

    // Save persistent settings
    QSettings settings("TStar", "TStar");
    settings.setValue("General/LastWorkingDir", QDir::currentPath());

    event->ignore();
    startFadeOut();
}

/**
 * @brief Starts a 250ms fade-in animation from fully transparent to fully opaque.
 */
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

/**
 * @brief Starts a 250ms fade-out animation, then force-exits the application.
 */
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
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        m_isClosing = true;
        close();
        std::exit(0); // Force exit to prevent hanging threads
    });
    m_anim->start();
}


// ============================================================================
// PCC Distribution Dialog
// ============================================================================

/**
 * @brief Opens a dialog showing the PCC result distribution for the active image.
 *        Requires that PCC has been previously run on the image.
 */
void MainWindow::openPCCDistributionDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    const PCCResult& res = viewer->getBuffer().metadata().pccResult;
    if (!res.valid) {
        QMessageBox::warning(this, tr("PCC Distribution"),
            tr("No valid PCC result found for this image.\nPlease run Photometric Color Calibration first."));
        return;
    }

    auto dlg = new PCCDistributionDialog(res, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    setupToolSubwindow(nullptr, dlg, tr("PCC Distribution"));

    log(tr("Opened PCC Distribution Tool"), Log_Action, true);
}


// ============================================================================
// Sidebar Tool Buttons Setup
// ============================================================================

/**
 * @brief Populates the sidebar with bottom-anchored action buttons
 *        (Help, About, Image Annotation, Color Profile Management).
 */
void MainWindow::setupSidebarTools() {
    if (m_sidebar) {
        // Helper lambda to resolve icon paths from both resource and filesystem
        auto makeIconLocal = [](const QString& source) -> QIcon {
            QString path = source;
            if (!source.startsWith(":") && !QDir::isAbsolutePath(source)) {
                path = QCoreApplication::applicationDirPath() + "/" + source;
                if (!QFile::exists(path)) {
                    path = QCoreApplication::applicationDirPath() + "/../Resources/" + source;
                }
            }
            QIcon icon;
            icon.addFile(path);
            return icon;
        };

        // --- Help button ---
        m_sidebar->addBottomAction(makeIconLocal("images/help.svg"), tr("Help"), [this]() {
            HelpDialog dlg(this);
            QRect mainGeom = this->geometry();
            dlg.move(mainGeom.center() - QPoint(dlg.width() / 2, dlg.height() / 2));
            dlg.exec();
        });

        // --- About button ---
        m_sidebar->addBottomAction(makeIconLocal("images/info.svg"), tr("About TStar"), [this]() {
            AboutDialog dlg(this, TStar::getVersion(), __DATE__);

            // Manual centering with screen bounds check
            dlg.adjustSize();
            QSize dlgSize = dlg.size();
            QRect mainGeom = this->geometry();
            QPoint center = mainGeom.center();
            int x = center.x() - dlgSize.width() / 2;
            int y = center.y() - dlgSize.height() / 2;

            if (auto scr = this->screen()) {
                QRect screenGeom = scr->availableGeometry();
                if (x < screenGeom.left()) x = screenGeom.left();
                if (y < screenGeom.top())  y = screenGeom.top();
            }

            dlg.move(x, y);
            dlg.exec();
        });

        // --- Image Annotation button ---
        m_sidebar->addBottomAction(QIcon(":/images/astrometry.svg"), tr("Image Annotation"), [this]() {
            openAnnotationToolDialog();
        });

        // --- Color Profile Management button ---
        m_sidebar->addBottomAction(QIcon(":/images/color_management.svg"), tr("Color Profile Management"), [this]() {
            openColorProfileDialog();
        });
    }
}


// ============================================================================
// Annotation & Color Profile Dialogs
// ============================================================================

/**
 * @brief Opens the Annotation Tool dialog (standalone, not in MDI).
 */
void MainWindow::openAnnotationToolDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (!m_annotatorDlg) {
        m_annotatorDlg = new AnnotationToolDialog(this);
    }
    m_annotatorDlg->setViewer(viewer);
    m_annotatorDlg->show();
    m_annotatorDlg->raise();
    m_annotatorDlg->activateWindow();
}

/**
 * @brief Opens the Color Profile dialog for the active image's buffer.
 *        Refreshes the viewer's display on acceptance.
 */
void MainWindow::openColorProfileDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (!m_colorProfileDlg) {
        m_colorProfileDlg = new ColorProfileDialog(&viewer->getBuffer(), viewer, this);
        connect(m_colorProfileDlg, &QDialog::accepted, this, [this]() {
            if (currentViewer()) currentViewer()->refreshDisplay();
        });
    } else {
        m_colorProfileDlg->setBuffer(&viewer->getBuffer(), viewer);
    }

    m_colorProfileDlg->show();
    m_colorProfileDlg->raise();
    m_colorProfileDlg->activateWindow();
}

/**
 * @brief Checks whether an image buffer's embedded color profile mismatches
 *        the workspace profile, and handles conversion based on user settings.
 *
 * Depending on the AutoConversionMode setting, the image may be:
 *  - Always auto-converted
 *  - Converted after user confirmation
 *  - Left unconverted
 *
 * The "handled" flag is set on the metadata to prevent redundant checks.
 */
void MainWindow::checkAndHandleColorProfile(ImageBuffer& buffer, const QString& title) {
    bool alreadyHandled = buffer.metadata().colorProfileHandled;
    if (alreadyHandled) return;

    // Determine the image's color profile from ICC data or stored type
    const auto& meta = buffer.metadata();
    core::ColorProfile imageProfile;
    if (!meta.iccData.isEmpty()) {
        imageProfile = core::ColorProfile(meta.iccData);
    } else if (meta.iccProfileType >= 0 && meta.iccProfileType <= static_cast<int>(core::StandardProfile::LinearRGB)) {
        imageProfile = core::ColorProfile(static_cast<core::StandardProfile>(meta.iccProfileType));
    } else {
        return; // No profile info — nothing to check
    }

    if (core::ColorProfileManager::instance().isMismatch(imageProfile)) {
        core::AutoConversionMode mode = core::ColorProfileManager::instance().autoConversionMode();

        if (mode == core::AutoConversionMode::Always) {
            // Silently convert
            core::ColorProfileManager::instance().convertToWorkspace(buffer, imageProfile);
            ImageBuffer::Metadata m = buffer.metadata();
            m.colorProfileHandled = true;
            buffer.setMetadata(m);
            log(tr("Auto-converted image '%1' to workspace profile.").arg(title), Log_Success);
            showConsoleTemporarily(2000);

        } else if (mode == core::AutoConversionMode::Ask) {
            // Prompt the user synchronously before window creation
            QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Color Profile Mismatch"),
                tr("The image '%1' has a color profile (%2) that differs from the workspace profile (%3). "
                   "Would you like to convert it to the workspace profile?")
                    .arg(title, imageProfile.name(),
                         core::ColorProfileManager::instance().workspaceProfile().name()),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                core::ColorProfileManager::instance().convertToWorkspace(buffer, imageProfile);
                ImageBuffer::Metadata m = buffer.metadata();
                m.colorProfileHandled = true;
                buffer.setMetadata(m);
                log(tr("Converted image '%1' to workspace profile.").arg(title), Log_Success);
                showConsoleTemporarily(2000);
            } else {
                ImageBuffer::Metadata m = buffer.metadata();
                m.colorProfileHandled = true;
                buffer.setMetadata(m);
            }
        }
    } else {
        // No mismatch — mark as handled to avoid redundant checks
        ImageBuffer::Metadata m = buffer.metadata();
        m.colorProfileHandled = true;
        buffer.setMetadata(m);
    }
}


// ============================================================================
// External Tool Runners — Cosmic Clarity / GraXpert
// ============================================================================

/**
 * @brief Runs the Cosmic Clarity external process on the active image.
 *
 * Executes on a background thread with a modal progress dialog. On success,
 * a new image window is created with the result.
 */
void MainWindow::runCosmicClarity(const CosmicClarityParams& params) {
    ImageViewer* v = currentViewer();
    if (!v) return;

    // Force activation of the target window before starting
    QWidget* p = v->parentWidget();
    while (p && !qobject_cast<QMdiSubWindow*>(p)) p = p->parentWidget();
    if (auto sub = qobject_cast<QMdiSubWindow*>(p)) {
        m_mdiArea->setActiveSubWindow(sub);
    }

    // Capture source state for the result window
    QString srcTitle                  = v->windowTitle();
    ImageBuffer::DisplayMode srcMode  = v->getDisplayMode();
    float srcMedian                   = v->getAutoStretchMedian();
    bool srcLinked                    = v->isDisplayLinked();

    startLongProcess();

    // Set up background thread and runner
    QThread* thread            = new QThread;
    CosmicClarityRunner* runner = new CosmicClarityRunner;
    runner->moveToThread(thread);

    connect(runner, &CosmicClarityRunner::processOutput, this,
        [this](const QString& msg) { log(msg.trimmed(), Log_Info); });

    // Modal progress dialog
    QProgressDialog* pd = new QProgressDialog(tr("Running Cosmic Clarity..."), tr("Cancel"), 0, 0, this);
    pd->setWindowModality(Qt::WindowModal);
    pd->setMinimumDuration(0);
    pd->show();

    connect(pd, &QProgressDialog::canceled, runner, &CosmicClarityRunner::cancel, Qt::DirectConnection);

    ImageBuffer input = v->getBuffer();

    connect(thread, &QThread::started, runner,
        [runner, input, params, thread, pd, this,
         srcTitle, srcMode, srcMedian, srcLinked]() mutable {
        ImageBuffer result;
        QString err;
        bool success = runner->run(input, result, params, &err);

        QMetaObject::invokeMethod(this, [=]() mutable {
            pd->close();
            pd->deleteLater();
            thread->quit();
            thread->wait();
            thread->deleteLater();
            runner->deleteLater();

            endLongProcess();

            if (success) {
                result.addHistoryAction(tr("Cosmic Clarity application"));
                createNewImageWindow(result, buildChildTitle(srcTitle, "_cc"), srcMode, srcMedian, srcLinked);
            } else if (!err.isEmpty() && err != "Process cancelled by user.") {
                QMessageBox::critical(this, tr("Cosmic Clarity Error"), err);
            } else if (err == "Process cancelled by user.") {
                log(tr("Cosmic Clarity cancelled."), Log_Warning);
            }
        });
    });

    thread->start();
}

/**
 * @brief Runs the GraXpert external process on the active image.
 *
 * Executes on a background thread with a modal progress dialog. On success,
 * a new image window is created with the result.
 */
void MainWindow::runGraXpert(const GraXpertParams& params) {
    ImageViewer* v = currentViewer();
    if (!v) return;

    // Force activation of the target window before starting
    QWidget* p = v->parentWidget();
    while (p && !qobject_cast<QMdiSubWindow*>(p)) p = p->parentWidget();
    if (auto sub = qobject_cast<QMdiSubWindow*>(p)) {
        m_mdiArea->setActiveSubWindow(sub);
    }

    // Capture source state
    QString srcTitle                  = v->windowTitle();
    ImageBuffer::DisplayMode srcMode  = v->getDisplayMode();
    float srcMedian                   = v->getAutoStretchMedian();
    bool srcLinked                    = v->isDisplayLinked();

    startLongProcess();

    // Set up background thread and runner
    QThread* thread         = new QThread;
    GraXpertRunner* runner  = new GraXpertRunner;
    runner->moveToThread(thread);

    connect(runner, &GraXpertRunner::processOutput, this,
        [this](const QString& msg) { log(msg.trimmed(), Log_Info); });

    // Modal progress dialog
    QProgressDialog* pd = new QProgressDialog(tr("Running GraXpert..."), tr("Cancel"), 0, 0, this);
    pd->setWindowModality(Qt::WindowModal);
    pd->setMinimumDuration(0);
    pd->show();

    connect(pd, &QProgressDialog::canceled, runner, &GraXpertRunner::cancel, Qt::DirectConnection);

    ImageBuffer input = v->getBuffer();

    connect(thread, &QThread::started, runner,
        [runner, input, params, thread, pd, this,
         srcTitle, srcMode, srcMedian, srcLinked]() mutable {
        ImageBuffer result;
        QString err;
        bool success = runner->run(input, result, params, &err);

        QMetaObject::invokeMethod(this, [=]() mutable {
            pd->close();
            pd->deleteLater();
            thread->quit();
            thread->wait();
            thread->deleteLater();
            runner->deleteLater();

            endLongProcess();

            if (success) {
                result.addHistoryAction(tr("GraXpert application"));
                createNewImageWindow(result, buildChildTitle(srcTitle, "_graxpert"), srcMode, srcMedian, srcLinked);
            }
        });
    });

    thread->start();
}


// ============================================================================
// Tool Dialog Launchers — Star Analysis
// ============================================================================

/**
 * @brief Opens the Star Analysis dialog as a tool sub-window.
 */
void MainWindow::openStarAnalysisDialog() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    // Reactivate existing dialog
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


// ============================================================================
// Mask Management
// ============================================================================

#include "dialogs/MaskGenerationDialog.h"
#include "dialogs/ApplyMaskDialog.h"
#include "core/MaskManager.h"
#include <QInputDialog>

/**
 * @brief Opens the mask generation dialog. On acceptance, saves the generated
 *        mask (optionally with a user-provided name) and applies it to the
 *        current viewer's buffer.
 */
void MainWindow::createMaskAction() {
    if (auto v = currentViewer()) {
        const ImageBuffer& img = v->getBuffer();
        if (!img.isValid()) return;

        MaskGenerationDialog dlg(img, this);
        if (dlg.exec() == QDialog::Accepted) {
            MaskLayer mask = dlg.getGeneratedMask();

            // Prompt the user for a name to save the mask
            bool ok;
            QString maskName = QInputDialog::getText(this, tr("Save Mask"),
                tr("Enter a name to save this mask:"),
                QLineEdit::Normal, mask.name, &ok);
            if (ok && !maskName.isEmpty()) {
                MaskManager::instance().addMask(maskName, mask);
            }

            // Apply mask to the current viewer's buffer
            v->getBuffer().setMask(mask);
            if (m_toggleOverlayAct) {
                m_toggleOverlayAct->setChecked(v->isMaskOverlayEnabled());
            }
            updateActiveImage();
            log(tr("Mask Created and Applied."), Log_Success);
            showConsoleTemporarily(2000);
        }
    } else {
        QMessageBox::warning(this, tr("No Image"), tr("Open an image first."));
    }
}

/**
 * @brief Opens the apply-mask dialog, populated with saved masks and
 *        luminance extracts from all other open image views.
 *
 * If the selected mask's dimensions differ from the target image, it is
 * automatically resized to match.
 */
void MainWindow::applyMaskAction() {
    if (auto v = currentViewer()) {
        ApplyMaskDialog dlg(v->getBuffer().width(), v->getBuffer().height(), this);

        // 1. Add saved masks from MaskManager
        auto savedMasks = MaskManager::instance().getAllMasks();
        for (auto it = savedMasks.begin(); it != savedMasks.end(); ++it) {
            dlg.addAvailableMask(it.key(), it.value(), false);
        }

        // 2. Add other open views (luminance extracted as mask)
        QList<CustomMdiSubWindow*> windows = m_mdiArea->findChildren<CustomMdiSubWindow*>();
        for (auto w : windows) {
            if (ImageViewer* iv = w->findChild<ImageViewer*>()) {
                if (iv != v) { // Exclude self
                    const ImageBuffer& src = iv->getBuffer();
                    MaskLayer m;

                    // Extract luminance from buffer as a mask
                    int tx = src.width();
                    int ty = src.height();
                    std::vector<float> data(tx * ty);
                    const std::vector<float>& sData = src.data();
                    int ch = src.channels();
                    for (int i = 0; i < tx * ty; ++i) {
                        if (ch == 1)
                            data[i] = sData[i];
                        else
                            data[i] = 0.2126f * sData[i * 3] + 0.7152f * sData[i * 3 + 1] + 0.0722f * sData[i * 3 + 2];
                        data[i] = std::clamp(data[i], 0.0f, 1.0f);
                    }
                    m.data   = data;
                    m.width  = tx;
                    m.height = ty;
                    m.name   = src.name();
                    dlg.addAvailableMask(src.name(), m, true);
                }
            }
        }

        if (dlg.exec() == QDialog::Accepted) {
            MaskLayer mask = dlg.getSelectedMask();
            if (mask.isValid()) {
                int tx = v->getBuffer().width();
                int ty = v->getBuffer().height();

                // Resize mask if dimensions don't match
                if (mask.width != tx || mask.height != ty) {
                    cv::Mat srcMask(mask.height, mask.width, CV_32FC1, mask.data.data());
                    cv::Mat dstMask;
                    cv::resize(srcMask, dstMask, cv::Size(tx, ty), 0, 0, cv::INTER_LINEAR);

                    mask.data.resize(tx * ty);
                    memcpy(mask.data.data(), dstMask.data, mask.data.size() * sizeof(float));
                    mask.width  = tx;
                    mask.height = ty;
                    log(tr("Applied mask resized to match image."), Log_Info);
                }

                v->getBuffer().setMask(mask);
                if (m_toggleOverlayAct) {
                    m_toggleOverlayAct->setChecked(v->isMaskOverlayEnabled());
                }
                updateActiveImage();
                log(tr("Mask Applied: %1").arg(mask.name), Log_Success);
                showConsoleTemporarily(2000);
            }
        }
    } else {
        QMessageBox::warning(this, tr("No Image"), tr("Open an image first."));
    }
}

/**
 * @brief Removes the mask from the current viewer's buffer and resets the overlay.
 */
void MainWindow::removeMaskAction() {
    if (auto v = currentViewer()) {
        if (v->getBuffer().hasMask()) {
            v->getBuffer().removeMask();
            v->setMaskOverlay(false);
            if (m_toggleOverlayAct) {
                m_toggleOverlayAct->setChecked(false);
            }
            updateActiveImage();
            log(tr("Mask Removed."), Log_Info);
        } else {
            log(tr("No mask to remove."), Log_Warning);
        }
    }
}

/**
 * @brief Inverts the current mask on the active viewer's buffer.
 */
void MainWindow::invertMaskAction() {
    if (auto v = currentViewer()) {
        if (v->getBuffer().hasMask()) {
            v->getBuffer().invertMask();
            updateActiveImage();
            log(tr("Mask Inverted."), Log_Info);
        }
    }
}

/**
 * @brief Toggles the mask overlay visualization on the active viewer.
 */
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


// ============================================================================
// Tool Dialog Launchers — Debayer / Continuum Subtraction / Align Channels
// ============================================================================

/**
 * @brief Opens the Debayer dialog for single-channel (Bayer pattern) images.
 */
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

/**
 * @brief Opens the Continuum Subtraction dialog.
 */
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

    setupToolSubwindow(nullptr, m_continuumDlg, tr("Continuum Subtraction"));
}

/**
 * @brief Opens the Align Channels dialog.
 */
void MainWindow::openAlignChannelsDialog() {
    if (m_alignChannelsDlg) {
        m_alignChannelsDlg->raise();
        m_alignChannelsDlg->activateWindow();
        m_alignChannelsDlg->refreshImageList();
        return;
    }

    m_alignChannelsDlg = new AlignChannelsDialog(this);
    log(tr("Opening Align Channels..."), Log_Action, true);
    m_alignChannelsDlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_alignChannelsDlg, &QDialog::destroyed, this, [this]() { m_alignChannelsDlg = nullptr; });

    CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, m_alignChannelsDlg, tr("Align Channels"));
    sub->resize(450, 450);
    centerToolWindow(sub);
}

// ============================================================================
// WCS Mosaic Builder
// ============================================================================

/**
 * @brief Opens the WCS Mosaic Builder dialog as a tool sub-window.
 */
void MainWindow::openWCSMosaicDialog() {
    if (m_wcsMosaicDlg) {
        m_wcsMosaicDlg->raise();
        m_wcsMosaicDlg->activateWindow();
    } else {
        m_wcsMosaicDlg = new WCSMosaicDialog(this);
        log(tr("Opening WCS Mosaic Builder..."), Log_Action, true);
        m_wcsMosaicDlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_wcsMosaicDlg, &QDialog::destroyed, this, [this]() { m_wcsMosaicDlg = nullptr; });
        
        connect(m_wcsMosaicDlg, &WCSMosaicDialog::mosaicReady, this, [this](const ImageBuffer& buf, const QString& title) {
            createNewImageWindow(buf, title);
        });

        CustomMdiSubWindow* sub = setupToolSubwindow(nullptr, m_wcsMosaicDlg, tr("WCS Mosaic Builder"));
        sub->resize(600, 450);
        centerToolWindow(sub);
    }
    
    QVector<ImageViewer*> viewers;
    for (auto* subItem : m_mdiArea->subWindowList()) {
        if (auto* csw = qobject_cast<CustomMdiSubWindow*>(subItem)) {
            if (!csw->isToolWindow() && csw->viewer()) {
                viewers.push_back(csw->viewer());
            }
        }
    }
    m_wcsMosaicDlg->setAvailableImages(viewers);
    m_wcsMosaicDlg->setViewer(currentViewer());
}


// ============================================================================
// Image Annotator (MDI-hosted)
// ============================================================================

/**
 * @brief Opens the Image Annotator dialog as a tool sub-window.
 *        Persisted annotations are restored across dialog instances.
 */
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

    // Restore persisted annotations from previous dialog instance
    if (!m_persistedAnnotations.isEmpty()) {
        m_annotatorDlg->setPersistedAnnotations(m_persistedAnnotations);
    }

    m_annotatorDlg->setViewer(v);

    // Don't delete on close — just hide so annotations persist
    connect(m_annotatorDlg, &QDialog::destroyed, this, [this]() {
        m_annotatorDlg = nullptr;
    });

    setupToolSubwindow(nullptr, m_annotatorDlg, tr("Annotation Tool"));
}


// ============================================================================
// Drag and Drop Support
// ============================================================================

/**
 * @brief Accepts drag-enter events if at least one URL is a supported
 *        image format (FITS, XISF, TIFF, PNG, JPEG, or RAW camera files).
 */
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            QString path = url.toLocalFile().toLower();

            // Standard image formats
            if (path.endsWith(".fits") || path.endsWith(".fit") ||
                path.endsWith(".xisf") ||
                path.endsWith(".tiff") || path.endsWith(".tif") ||
                path.endsWith(".png")  || path.endsWith(".jpg") || path.endsWith(".jpeg")) {
                event->acceptProposedAction();
                return;
            }

            // RAW camera files
            QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
            if (RawLoader::isSupportedExtension(ext)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

/**
 * @brief Handles file drops by loading images in parallel.
 *
 * Files are loaded on background threads using (CPU cores - 1) threads.
 * Results are queued and displayed one at a time on the UI thread to
 * avoid lag.
 */
void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : mimeData->urls())
            paths << url.toLocalFile();

        for (const QString& path : paths) {
            (void)QtConcurrent::run(getImageLoadThreadPool(), [this, path]() {
                QList<ImageFileLoadResult> results = loadImageFile(path);
                for (ImageFileLoadResult& r : results) {
                    auto ptr = std::make_shared<ImageFileLoadResult>(std::move(r));
                    {
                        QMutexLocker lock(&m_imageLoadMutex);
                        m_imageLoadQueue.enqueue(ptr);
                    }
                }
                // Start the display timer if not already running
                QMetaObject::invokeMethod(this, [this]() {
                    if (!m_imageDisplayTimer->isActive())
                        m_imageDisplayTimer->start(50); // Process one image every 50ms
                }, Qt::QueuedConnection);
            });
        }
        event->acceptProposedAction();
    }
}


// ============================================================================
// Tool Dialog Launchers — Luminance Extract / Recombine
// ============================================================================

/**
 * @brief Opens the Extract Luminance dialog as a tool sub-window.
 */
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

    connect(dlg, &QDialog::accepted, this, [this]() {
        log(tr("Luminance extracted."), Log_Success, true);
        showConsoleTemporarily(2000);
    });
}

/**
 * @brief Opens the Recombine Luminance dialog as a tool sub-window.
 */
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
    sub->resize(450, 200);
    centerToolWindow(sub);

    connect(dlg, &QDialog::accepted, this, [this]() {
        log(tr("Luminance recombined."), Log_Success, true);
        showConsoleTemporarily(2000);
    });
}


// ============================================================================
// Tool Dialog Launchers — Correction Brush / CLAHE / Pedestal
// ============================================================================

/**
 * @brief Opens the Correction Brush dialog as a tool sub-window.
 *        Auto-updates its source when the active image changes.
 */
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

    connect(dlg, &QDialog::accepted, this, [this]() {
        log(tr("Correction brush applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    // Update source when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*) {
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}

/**
 * @brief Removes the pedestal (minimum value offset) from the active image.
 */
void MainWindow::removePedestal() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) return;

    pushUndo(tr("Remove Pedestal applied"));
    ChannelOps::removePedestal(v->getBuffer());
    v->refreshDisplay();
    log(tr("Pedestal removed."), Log_Success, true);
    showConsoleTemporarily(2000);
}

/**
 * @brief Opens the CLAHE (Contrast Limited Adaptive Histogram Equalization)
 *        dialog as a tool sub-window.
 */
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

    connect(dlg, &QDialog::accepted, this, [this]() {
        log(tr("CLAHE applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    // Update source when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*) {
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}


// ============================================================================
// Tool Dialog Launchers — Star Halo Removal / Morphology
// ============================================================================

/**
 * @brief Opens the Star Halo Removal dialog as a tool sub-window.
 */
void MainWindow::openStarHaloRemovalDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    log(tr("Opening Star Halo Removal..."), Log_Action, true);

    auto* dlg = new StarHaloRemovalDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Star Halo Removal"));
    sub->resize(930, 690);
    centerToolWindow(sub);

    connect(dlg, &QDialog::accepted, this, [this]() {
        log(tr("Star Halo Removal applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*) {
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}

/**
 * @brief Opens the Morphology dialog as a tool sub-window.
 */
void MainWindow::openMorphologyDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    if (m_morphologyDlg) {
        m_morphologyDlg->raise();
        m_morphologyDlg->activateWindow();
        m_morphologyDlg->setSource(currentViewer()->getBuffer());
        return;
    }

    log(tr("Opening Morphology Tool..."), Log_Action, true);

    auto* dlg = new MorphologyDialog(this);
    m_morphologyDlg = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    CustomMdiSubWindow* sub = new CustomMdiSubWindow(m_mdiArea);
    setupToolSubwindow(sub, dlg, tr("Morphology"));
    sub->resize(930, 690);
    centerToolWindow(sub);

    connect(dlg, &QDialog::accepted, this, [this]() {
        log(tr("Morphology applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    connect(dlg, &QObject::destroyed, this, [this]() {
        m_morphologyDlg = nullptr;
    });

    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*) {
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });

    if (currentViewer()) {
        dlg->setSource(currentViewer()->getBuffer());
    }
}


// ============================================================================
// Tool Dialog Launchers — Aberration Inspector / Selective Color
// ============================================================================

/**
 * @brief Opens the Aberration Inspector dialog as a tool sub-window.
 */
void MainWindow::openAberrationInspectorDialog() {
    if (!currentViewer()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
        return;
    }

    log(tr("Opening Aberration Inspector..."), Log_Action, true);

    // Safety check (currentViewer may have been invalidated between the guard above and here)
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

    // Update source when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*) {
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}

/**
 * @brief Opens the Selective Color Correction dialog as a tool sub-window.
 */
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

    connect(dlg, &QDialog::accepted, this, [this]() {
        log(tr("Selective Color Correction applied."), Log_Success, true);
        showConsoleTemporarily(2000);
    });

    // Update source when active image changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated, dlg, [this, dlg](QMdiSubWindow*) {
        if (currentViewer() && currentViewer()->getBuffer().isValid()) {
            dlg->setSource(currentViewer()->getBuffer());
        }
    });
}


// ============================================================================
// MainWindowCallbacks — Pure Virtual Implementations
// ============================================================================

/**
 * @brief Returns the image buffer of the currently active viewer.
 */
ImageBuffer* MainWindow::getCurrentImageBuffer() {
    ImageViewer* viewer = currentViewer();
    if (!viewer) return nullptr;
    return &viewer->getBuffer();
}

/**
 * @brief Returns the currently active ImageViewer.
 */
ImageViewer* MainWindow::getCurrentViewer() {
    return currentViewer();
}

/**
 * @brief Creates a new image window from a buffer, using the source viewer's
 *        display settings as defaults when mode == -1.
 */
void MainWindow::createResultWindow(const ImageBuffer& buffer, const QString& title, int mode, float median, bool linked) {
    ImageViewer* src = currentViewer();
    ImageBuffer::DisplayMode dMode;
    if (mode == -1) {
        dMode = src ? src->getDisplayMode() : getDefaultDisplayMode();
    } else {
        dMode = static_cast<ImageBuffer::DisplayMode>(mode);
    }

    float dMedian = (mode == -1 && src) ? src->getAutoStretchMedian() : median;
    bool  dLinked = (mode == -1 && src) ? src->isDisplayLinked()      : linked;

    QString childTitle = (title.startsWith('_') && src)
                         ? buildChildTitle(src->windowTitle(), title)
                         : title;
    createNewImageWindow(buffer, childTitle, dMode, dMedian, dLinked);
}

/**
 * @brief Logs a message to the console with the specified severity.
 * @param severity  0=Info, 1=Success, 2=Warning, 3=Error.
 */
void MainWindow::logMessage(const QString& message, int severity, bool showPopup) {
    LogType type = Log_Info;
    switch (severity) {
        case 1:  type = Log_Success; break;
        case 2:  type = Log_Warning; break;
        case 3:  type = Log_Error;   break;
        default: type = Log_Info;    break;
    }
    log(message, type, showPopup);
}


// ============================================================================
// Workspace Project — Dirty State Tracking
// ============================================================================

/**
 * @brief Connects a sub-window's layout and destruction signals to the
 *        workspace project dirty flag.
 */
void MainWindow::connectSubwindowProjectTracking(CustomMdiSubWindow* sub) {
    if (!sub) return;
    connect(sub, &CustomMdiSubWindow::layoutChanged, this, &MainWindow::markWorkspaceProjectDirty, Qt::UniqueConnection);
    connect(sub, &QObject::destroyed,                this, &MainWindow::markWorkspaceProjectDirty, Qt::UniqueConnection);
}

/**
 * @brief Marks the workspace project as dirty (unsaved changes exist).
 */
void MainWindow::markWorkspaceProjectDirty() {
    if (!m_workspaceProject.active || isDirtyBlocked()) return;
    m_workspaceProject.dirty = true;
}


// ============================================================================
// Workspace Project — Close All Windows
// ============================================================================

/**
 * @brief Closes all MDI sub-windows transactionally.
 *
 * Uses a two-phase approach:
 *  1. Preflight: checks canClose() on every window. If any refuses, aborts.
 *  2. Commit: closes all windows without re-prompting.
 *
 * @return true if all windows were successfully closed.
 */
bool MainWindow::closeAllWorkspaceWindows() {
    if (!m_mdiArea) return true;

    QList<QPointer<CustomMdiSubWindow>> windows;
    for (QMdiSubWindow* sub : m_mdiArea->subWindowList(QMdiArea::CreationOrder)) {
        if (auto* csw = qobject_cast<CustomMdiSubWindow*>(sub)) {
            windows.push_back(csw);
        }
    }

    // --- Preflight: check all windows can close ---
    for (const auto& win : windows) {
        if (!win) continue;
        if (!win->canClose()) {
            return false;
        }
    }

    // --- Commit: close all without re-prompting ---
    for (const auto& win : windows) {
        if (!win) continue;
        win->setProperty("bypassCloseChecks", true);
        win->setSkipCloseAnimation(true);
    }
    for (const auto& win : windows) {
        if (win) win->close();
    }

    // Wait up to 800ms for all windows to finish closing
    QTime deadline = QTime::currentTime().addMSecs(800);
    while (!m_mdiArea->subWindowList().isEmpty() && QTime::currentTime() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(10);
    }

    // Reset bypass flags on any surviving windows
    for (QMdiSubWindow* sub : m_mdiArea->subWindowList()) {
        if (auto* csw = qobject_cast<CustomMdiSubWindow*>(sub)) {
            csw->setProperty("bypassCloseChecks", false);
        }
    }

    return m_mdiArea->subWindowList().isEmpty();
}


// ============================================================================
// Header Panel Refresh
// ============================================================================

/**
 * @brief Re-syncs the header panel with the current active viewer's metadata.
 *        Called after batch operations that may modify non-active windows.
 */
void MainWindow::refreshHeaderPanel() {
    ImageViewer* viewer = currentViewer();
    if (viewer && m_headerPanel) {
        m_headerPanel->setMetadata(viewer->getBuffer().metadata());
    }
}


// ============================================================================
// Workspace Project — Save / Load / Manage
// ============================================================================

/**
 * @brief Prompts the user to save the workspace project if it has unsaved changes.
 * @param reason  A human-readable reason string (e.g. "before exiting").
 * @return true if it is safe to proceed (saved, discarded, or not dirty).
 *         false if the user cancelled.
 */
bool MainWindow::maybeSaveWorkspaceProject(const QString& reason) {
    if (!m_workspaceProject.active || !m_workspaceProject.dirty) return true;

    QMessageBox::StandardButton res = QMessageBox::warning(
        this,
        tr("Workspace project modified"),
        tr("The workspace project '%1' has unsaved changes (%2). Do you want to save it?")
            .arg(m_workspaceProject.displayName.isEmpty() ? tr("Untitled") : m_workspaceProject.displayName)
            .arg(reason),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (res == QMessageBox::Cancel)  return false;
    if (res == QMessageBox::Discard) return true;

    QString targetPath = m_workspaceProject.filePath;
    if (targetPath.isEmpty()) {
        targetPath = QFileDialog::getSaveFileName(
            this,
            tr("Save Workspace Project"),
            QDir::homePath(),
            tr("TStar Workspace Project (*.tstarproj)"));
        if (targetPath.isEmpty()) return false;
    }
    return saveWorkspaceProjectTo(targetPath);
}

/**
 * @brief Captures the entire workspace state into a JSON object and queues
 *        snapshot save jobs for parallel execution.
 *
 * For each image window, the current buffer plus all undo/redo history entries
 * are serialized as .tsnap files. Display settings, geometry, and source paths
 * are stored in the JSON.
 */
QJsonObject MainWindow::captureWorkspaceProjectState(const QString& dataDirPath, const QString& projectBaseDir,
    QList<SaveSnapshotJob>& saveJobs) {
    QJsonObject root;
    root["version"]     = 1;
    root["app"]         = "TStar";
    root["name"]        = m_workspaceProject.displayName;
    root["savedUtc"]    = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["snapshotDir"] = QFileInfo(dataDirPath).absoluteFilePath();

    QJsonArray images;
    QDir dataDir(dataDirPath);
    dataDir.mkpath(".");

    // Helper: convert QRect to JSON object
    auto rectToJson = [](const QRect& r) {
        QJsonObject o;
        o["x"] = r.x();
        o["y"] = r.y();
        o["w"] = r.width();
        o["h"] = r.height();
        return o;
    };

    int imageIdx = 0;
    const auto subs = m_mdiArea->subWindowList(QMdiArea::CreationOrder);
    for (QMdiSubWindow* rawSub : subs) {
        auto* sub = qobject_cast<CustomMdiSubWindow*>(rawSub);
        if (!sub) continue;
        if (sub->isToolWindow()) continue;

        ImageViewer* v = sub->viewer();
        if (!v || !v->getBuffer().isValid()) continue;

        // Build a unique base name for this view's snapshot files
        QString base = QString("view_%1_%2")
            .arg(imageIdx++, 4, 10, QLatin1Char('0'))
            .arg(sanitizeProjectComponent(sub->windowTitle()));

        // Queue save jobs for current buffer, undo, and redo history
        QString currentRel = base + "_current.tsnap";
        saveJobs.append({v->getBuffer(), dataDir.filePath(currentRel), false, "", sub->windowTitle()});

        QJsonArray undoEntries;
        const QVector<ImageBuffer> undoHistory = v->undoHistory();
        for (int i = 0; i < undoHistory.size(); ++i) {
            QString rel = QString("%1_undo_%2.tsnap").arg(base).arg(i, 3, 10, QLatin1Char('0'));
            saveJobs.append({undoHistory[i], dataDir.filePath(rel), false, "", QString("%1 (Undo %2)").arg(sub->windowTitle()).arg(i)});
            undoEntries.append(rel);
        }

        QJsonArray redoEntries;
        const QVector<ImageBuffer> redoHistory = v->redoHistory();
        for (int i = 0; i < redoHistory.size(); ++i) {
            QString rel = QString("%1_redo_%2.tsnap").arg(base).arg(i, 3, 10, QLatin1Char('0'));
            saveJobs.append({redoHistory[i], dataDir.filePath(rel), false, "", QString("%1 (Redo %2)").arg(sub->windowTitle()).arg(i)});
            redoEntries.append(rel);
        }

        // Build the JSON descriptor for this view
        QJsonObject viewObj;
        viewObj["title"]              = sub->windowTitle();
        viewObj["sourcePath"]         = v->filePath(); // Legacy key
        viewObj["sourcePathAbs"]      = v->filePath();
        viewObj["sourcePathRel"]      = v->filePath().isEmpty() ? QString() : QDir(projectBaseDir).relativeFilePath(v->filePath());
        viewObj["sourceFileName"]     = v->filePath().isEmpty() ? QString() : QFileInfo(v->filePath()).fileName();
        viewObj["currentSnapshot"]    = currentRel;
        viewObj["undoSnapshots"]      = undoEntries;
        viewObj["redoSnapshots"]      = redoEntries;
        viewObj["modified"]           = v->isModified();
        viewObj["displayMode"]        = static_cast<int>(v->getDisplayMode());
        viewObj["displayLinked"]      = v->isDisplayLinked();
        viewObj["displayInverted"]    = v->isDisplayInverted();
        viewObj["displayFalseColor"]  = v->isDisplayFalseColor();
        viewObj["autoStretchMedian"]  = v->getAutoStretchMedian();
        viewObj["channelView"]        = static_cast<int>(v->channelView());
        viewObj["zoom"]               = v->getScale();
        viewObj["hScroll"]            = v->getHBarLoc();
        viewObj["vScroll"]            = v->getVBarLoc();
        viewObj["linked"]             = v->isLinked();
        viewObj["geometry"]           = rectToJson(sub->geometry());
        viewObj["shaded"]             = sub->isShaded();
        viewObj["maximized"]          = sub->isMaximized();

        images.append(viewObj);
    }

    root["images"] = images;
    return root;
}

/**
 * @brief Restores workspace state from a JSON descriptor and snapshot files.
 *
 * Closes all existing windows, then loads snapshot files in parallel. For each
 * view, the image buffer is loaded from its snapshot (with fallback to the
 * original source file if the snapshot is missing), and undo/redo history is
 * reconstructed. Linked views are reconnected.
 *
 * @return true if the workspace was successfully restored.
 */
bool MainWindow::restoreWorkspaceProjectState(const QJsonObject& root, const QString& dataDirPath, const QString& projectBaseDir) {
    QDir dataDir(dataDirPath);
    if (!closeAllWorkspaceWindows()) return false;

    m_restoringWorkspaceProject = true;

    const QJsonArray images = root["images"].toArray();
    if (images.isEmpty()) {
        m_restoringWorkspaceProject = false;
        return true;
    }

    // --- 1. Gather all load jobs for parallelization ---
    QList<LoadSnapshotJob> loadJobs;
    for (const QJsonValue& value : images) {
        const QJsonObject viewObj = value.toObject();
        QString title = viewObj["title"].toString();

        auto addJob = [&](const QString& rel, const QString& type) {
            if (rel.isEmpty()) return;
            loadJobs.append({dataDir.filePath(rel), rel, ImageBuffer(), false, "", QString("%1 (%2)").arg(title).arg(type)});
        };

        addJob(viewObj["currentSnapshot"].toString(), tr("Current"));

        QJsonArray undoArr = viewObj["undoSnapshots"].toArray();
        for (int i = 0; i < undoArr.size(); ++i) addJob(undoArr[i].toString(), tr("Undo %1").arg(i));

        QJsonArray redoArr = viewObj["redoSnapshots"].toArray();
        for (int i = 0; i < redoArr.size(); ++i) addJob(redoArr[i].toString(), tr("Redo %1").arg(i));
    }

    // --- 2. Parallel loading with progress dialog ---
    if (!loadJobs.isEmpty()) {
        QProgressDialog progress(tr("Loading Workspace Snapshots..."), tr("Abort"), 0, loadJobs.size(), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(400);
        progress.show();

        QAtomicInt finishedCount(0);
        auto future = QtConcurrent::map(loadJobs, [&finishedCount](LoadSnapshotJob& job) {
            job.success = loadProjectBufferSnapshot(job.filePath, job.buffer, &job.error);
            finishedCount.fetchAndAddRelaxed(1);
        });

        while (!future.isFinished()) {
            progress.setValue(finishedCount.loadRelaxed());
            if (progress.wasCanceled()) {
                future.cancel();
                break;
            }
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        progress.setValue(loadJobs.size());
    }

    // Build a lookup map of loaded snapshots by relative path
    QMap<QString, ImageBuffer> loadedSnapshots;
    for (const auto& job : loadJobs) {
        if (job.success) loadedSnapshots[job.relPath] = job.buffer;
    }

    // --- 3. Source path relinking & window reconstruction ---
    QHash<QString, QString> relinkCache;
    QString relinkRootDir;
    bool relinkPrompted = false;

    // Lambda: resolve the source file path from multiple strategies
    auto resolveSourcePath = [&](const QJsonObject& viewObj) -> QString {
        const QString absPath  = viewObj["sourcePathAbs"].toString();
        const QString relPath  = viewObj["sourcePathRel"].toString();
        const QString legacy   = viewObj["sourcePath"].toString();
        const QString fileName = viewObj["sourceFileName"].toString();

        auto tryPath = [](const QString& p) -> QString {
            if (!p.isEmpty() && QFileInfo::exists(p)) return QFileInfo(p).absoluteFilePath();
            return QString();
        };

        // Try absolute path
        QString found = tryPath(absPath);
        if (!found.isEmpty()) return found;

        // Try relative path from project base
        if (!relPath.isEmpty()) {
            found = tryPath(QDir(projectBaseDir).absoluteFilePath(relPath));
            if (!found.isEmpty()) return found;
        }

        // Try legacy path
        if (!legacy.isEmpty()) {
            found = tryPath(legacy);
            if (!found.isEmpty()) return found;
            found = tryPath(QDir(projectBaseDir).absoluteFilePath(legacy));
            if (!found.isEmpty()) return found;
        }

        // Determine the file name for relink search
        QString keyName = fileName;
        if (keyName.isEmpty() && !legacy.isEmpty()) keyName = QFileInfo(legacy).fileName();
        if (keyName.isEmpty()) keyName = QFileInfo(absPath).fileName();
        if (keyName.isEmpty()) return QString();

        // Check relink cache
        if (relinkCache.contains(keyName)) {
            const QString cached = relinkCache.value(keyName);
            return QFileInfo::exists(cached) ? cached : QString();
        }

        // Prompt user for relink root directory (once)
        if (!relinkPrompted) {
            relinkPrompted = true;
            auto choice = QMessageBox::question(this, tr("Missing source images"),
                tr("Some source images are missing. Do you want to select a folder for automatic relink?"),
                QMessageBox::Yes | QMessageBox::No);
            if (choice == QMessageBox::Yes) {
                relinkRootDir = QFileDialog::getExistingDirectory(this, tr("Select relink root folder"), projectBaseDir);
            }
        }

        // Search relink root for matching file name
        if (!relinkRootDir.isEmpty()) {
            QDirIterator it(relinkRootDir, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString candidate = it.next();
                if (QFileInfo(candidate).fileName().compare(keyName, Qt::CaseInsensitive) == 0) {
                    relinkCache.insert(keyName, candidate);
                    return candidate;
                }
            }
        }

        return QString();
    };

    // Helper: convert JSON object to QRect
    auto jsonToRect = [](const QJsonObject& o) {
        return QRect(o["x"].toInt(), o["y"].toInt(), o["w"].toInt(), o["h"].toInt());
    };

    // --- 4. Reconstruct windows ---
    QVector<ImageViewer*> linkedViewers;
    for (const QJsonValue& val : images) {
        QJsonObject viewObj = val.toObject();
        QString snapRel = viewObj["currentSnapshot"].toString();
        if (snapRel.isEmpty()) continue;

        // Load the buffer (from pre-loaded snapshot or fallback to source)
        ImageBuffer buffer;
        bool snapshotLoaded = false;
        if (loadedSnapshots.contains(snapRel)) {
            buffer = loadedSnapshots[snapRel];
            snapshotLoaded = true;
        }

        if (!snapshotLoaded) {
            QString loadErr;
            const bool hasSnapshot = !snapRel.isEmpty() && QFileInfo::exists(dataDir.filePath(snapRel));

            if (hasSnapshot) {
                if (!loadProjectBufferSnapshot(dataDir.filePath(snapRel), buffer, &loadErr)) {
                    log(tr("Failed to load project snapshot '%1': %2").arg(snapRel).arg(loadErr), Log_Error, true);
                    continue;
                }
            } else {
                // Fallback to source image if snapshot is missing
                const QString resolvedSource = resolveSourcePath(viewObj);
                if (resolvedSource.isEmpty()) {
                    log(tr("Cannot restore '%1': snapshot and source image are missing.").arg(viewObj["title"].toString()), Log_Error, true);
                    continue;
                }

                const QList<ImageFileLoadResult> lr = loadImageFile(resolvedSource);
                bool okLoad = false;
                for (const auto& entry : lr) {
                    if (entry.success) {
                        buffer = entry.buffer;
                        okLoad = true;
                        break;
                    }
                }
                if (!okLoad) {
                    if (!buffer.loadStandard(resolvedSource)) {
                        log(tr("Cannot restore '%1' from source image.").arg(viewObj["title"].toString()), Log_Error, true);
                        continue;
                    }
                }
            }
        }

        // Create the image window
        CustomMdiSubWindow* sub = createNewImageWindow(buffer, viewObj["title"].toString(),
            (ImageBuffer::DisplayMode)viewObj["displayMode"].toInt(0),
            (float)viewObj["autoStretchMedian"].toDouble(0.25),
            viewObj["displayLinked"].toBool(true));

        if (!sub || !sub->viewer()) continue;
        ImageViewer* v = sub->viewer();

        // Restore viewer state
        v->setFilePath(resolveSourcePath(viewObj));
        v->setInverted(viewObj["displayInverted"].toBool(false));
        v->setFalseColor(viewObj["displayFalseColor"].toBool(false));
        v->setChannelView((ImageBuffer::ChannelView)viewObj["channelView"].toInt(0));

        // Restore undo history
        QVector<ImageBuffer> undoHistory;
        for (const QJsonValue& u : viewObj["undoSnapshots"].toArray()) {
            QString r = u.toString();
            if (loadedSnapshots.contains(r)) undoHistory.push_back(loadedSnapshots[r]);
        }

        // Restore redo history
        QVector<ImageBuffer> redoHistory;
        for (const QJsonValue& redoSnapshot : viewObj["redoSnapshots"].toArray()) {
            QString rPath = redoSnapshot.toString();
            if (loadedSnapshots.contains(rPath)) redoHistory.push_back(loadedSnapshots[rPath]);
        }

        v->setHistory(undoHistory, redoHistory);
        v->setModified(viewObj["modified"].toBool(false));
        v->syncView((float)viewObj["zoom"].toDouble(1.0), (float)viewObj["hScroll"].toDouble(0.0), (float)viewObj["vScroll"].toDouble(0.0));

        // Restore geometry and window state
        QRect g = jsonToRect(viewObj["geometry"].toObject());
        if (g.isValid()) sub->setGeometry(g);
        if (viewObj["maximized"].toBool(false)) sub->showMaximized();
        if (viewObj["shaded"].toBool(false) && !sub->isShaded()) sub->toggleShade();
        if (viewObj["linked"].toBool(false)) linkedViewers.push_back(v);

        connectSubwindowProjectTracking(sub);
    }

    // --- 5. Reconnect linked viewers ---
    for (int i = 0; i < linkedViewers.size(); ++i) {
        for (int j = i + 1; j < linkedViewers.size(); ++j) {
            connect(linkedViewers[i], &ImageViewer::viewChanged, linkedViewers[j], &ImageViewer::syncView, Qt::UniqueConnection);
            connect(linkedViewers[j], &ImageViewer::viewChanged, linkedViewers[i], &ImageViewer::syncView, Qt::UniqueConnection);
        }
        linkedViewers[i]->setLinked(true);
    }

    m_restoringWorkspaceProject = false;
    m_workspaceProject.active = true;
    m_workspaceProject.dirty  = false;
    return true;
}

/**
 * @brief Saves the workspace project to a specified file path.
 *
 * Captures the workspace state, saves all snapshot files in parallel,
 * and writes the JSON project descriptor atomically.
 *
 * @return true on success.
 */
bool MainWindow::saveWorkspaceProjectTo(const QString& projectFilePath) {
    if (projectFilePath.isEmpty()) return false;

    // Check if there are any valid images in the workspace
    int validImageCount = 0;
    const auto subs = m_mdiArea->subWindowList(QMdiArea::CreationOrder);
    for (QMdiSubWindow* rawSub : subs) {
        auto* sub = qobject_cast<CustomMdiSubWindow*>(rawSub);
        if (sub && !sub->isToolWindow()) {
            ImageViewer* v = sub->viewer();
            if (v && v->getBuffer().isValid()) {
                validImageCount++;
            }
        }
    }

    if (validImageCount == 0) {
        auto choice = QMessageBox::warning(this, tr("Project is Empty"),
            tr("The workspace contains no images. Save an empty project anyway?"),
            QMessageBox::Yes | QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            return false;
        }
    }

    // Ensure the project directory exists
    QFileInfo fi(projectFilePath);
    QDir baseDir = fi.absoluteDir();
    if (!baseDir.exists() && !baseDir.mkpath(".")) {
        QMessageBox::critical(this, tr("Project Save Error"), tr("Cannot create project directory: %1").arg(baseDir.absolutePath()));
        return false;
    }

    QString fileName = fi.completeBaseName();
    if (fileName.isEmpty()) fileName = "workspace";

    // Snapshots go to [ProjectsRoot]/{projectName}_data
    QString projDataDir = getWorkspaceProjectsDir();
    QDir appDataProjDir(projDataDir);
    if (!appDataProjDir.exists() && !appDataProjDir.mkpath(".")) {
        QMessageBox::critical(this, tr("Project Save Error"), tr("Cannot create projects directory: %1").arg(projDataDir));
        return false;
    }
    QString dataDirPath = appDataProjDir.filePath(fileName + "_data");

    // Capture state and build save job list
    QList<SaveSnapshotJob> saveJobs;
    QJsonObject root = captureWorkspaceProjectState(dataDirPath, baseDir.absolutePath(), saveJobs);

    // --- Parallel saving with progress dialog ---
    if (!saveJobs.isEmpty()) {
        QProgressDialog progress(tr("Saving Workspace Project..."), tr("Abort"), 0, saveJobs.size(), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(500);
        progress.show();

        QAtomicInt finishedCount(0);
        auto future = QtConcurrent::map(saveJobs, [&finishedCount](SaveSnapshotJob& job) {
            job.success = saveProjectBufferSnapshot(job.buffer, job.filePath, &job.error);
            finishedCount.fetchAndAddRelaxed(1);
        });

        while (!future.isFinished()) {
            progress.setValue(finishedCount.loadRelaxed());
            if (progress.wasCanceled()) {
                future.cancel();
                break;
            }
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        progress.setValue(saveJobs.size());

        // Report any failed save jobs
        for (const auto& job : saveJobs) {
            if (!job.success) {
                log(tr("Failed to save project snapshot for '%1': %2").arg(job.viewTitle).arg(job.error), Log_Error, true);
            }
        }
    }

    // Write the JSON descriptor atomically
    QSaveFile out(projectFilePath);
    if (!out.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Project Save Error"), tr("Cannot open project file for writing: %1").arg(projectFilePath));
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        QMessageBox::critical(this, tr("Project Save Error"), tr("Cannot commit project file: %1").arg(projectFilePath));
        return false;
    }

    // Update project state
    m_workspaceProject.active      = true;
    m_workspaceProject.untitled    = false;
    m_workspaceProject.filePath    = projectFilePath;
    m_workspaceProject.displayName = fileName;

    // Clear modified flag on all viewers (use restoration guard to prevent re-dirtying)
    m_restoringWorkspaceProject = true;
    for (QMdiSubWindow* rawSub : m_mdiArea->subWindowList()) {
        auto* sub = qobject_cast<CustomMdiSubWindow*>(rawSub);
        if (sub && !sub->isToolWindow() && sub->viewer()) {
            sub->viewer()->setModified(false);
        }
    }

    m_workspaceProject.dirty = false;
    log(tr("Workspace project saved: %1").arg(projectFilePath), Log_Success, true);
    showConsoleTemporarily(2000);
    m_restoringWorkspaceProject = false;

    return true;
}

/**
 * @brief Loads a workspace project from a .tstarproj file.
 * @return true on success.
 */
bool MainWindow::loadWorkspaceProjectFrom(const QString& projectFilePath) {
    QFile file(projectFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Project Load Error"), tr("Cannot open project file: %1").arg(projectFilePath));
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::critical(this, tr("Project Load Error"), tr("Invalid project file: %1").arg(parseErr.errorString()));
        return false;
    }

    QFileInfo fi(projectFilePath);

    // Try to get snapshot directory from project file first (new format)
    QString dataDirPath = doc.object()["snapshotDir"].toString();

    // Fallback: check in configured Project Root or AppData
    if (dataDirPath.isEmpty()) {
        QString projDataDir = getWorkspaceProjectsDir();
        dataDirPath = QDir(projDataDir).filePath(fi.completeBaseName() + "_data");
    }

    // Final fallback: check next to the .tstarproj file (old format)
    if (!QDir(dataDirPath).exists()) {
        dataDirPath = fi.absoluteDir().filePath(fi.completeBaseName() + "_data");
    }

    m_restoringWorkspaceProject = true;
    bool ok = restoreWorkspaceProjectState(doc.object(), dataDirPath, fi.absoluteDir().absolutePath());
    if (!ok) {
        m_restoringWorkspaceProject = false;
        return false;
    }

    m_workspaceProject.active      = true;
    m_workspaceProject.dirty       = false;
    m_workspaceProject.untitled    = false;
    m_workspaceProject.filePath    = projectFilePath;
    m_workspaceProject.displayName = fi.completeBaseName();

    showConsoleTemporarily(2000);
    log(tr("Workspace project loaded: %1").arg(projectFilePath), Log_Success, true);
    m_restoringWorkspaceProject = false;
    return true;
}


// ============================================================================
// Workspace Project — Directory Helpers
// ============================================================================

/**
 * @brief Returns the directory where workspace project data (snapshots) is stored.
 *
 * Prefers a user-configured project root from settings. Falls back to
 * AppData/Local/TStar/TStar/projects.
 */
QString MainWindow::getWorkspaceProjectsDir() const {
    // Check if user has defined a custom project root in Settings
    QSettings settings;
    QString userRoot = settings.value("paths/project_root").toString().trimmed();
    if (!userRoot.isEmpty()) {
        QDir rootDir(userRoot);
        if (rootDir.exists() || rootDir.mkpath(".")) {
            return rootDir.absolutePath();
        }
    }

    // Default fallback to AppData/Local/TStar/TStar/projects
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString projectsDir = appDataPath + "/projects";

    QDir dir(projectsDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return projectsDir;
}

/**
 * @brief Returns the most appropriate working directory for file dialogs.
 *
 * Priority order:
 *  1. Active workspace project directory
 *  2. Current working directory (user-set "Home")
 *  3. Persisted last working directory from settings
 *  4. Desktop path
 *  5. User home path
 */
QString MainWindow::getProjectWorkingDirectory() const {
    // 1. Active workspace project directory
    if (m_workspaceProject.active && !m_workspaceProject.filePath.isEmpty()) {
        const QString projDir = QFileInfo(m_workspaceProject.filePath).absoluteDir().absolutePath();
        if (!projDir.isEmpty() && QDir(projDir).exists()) return projDir;
    }

    // 2. Last used dialog directory (Session-local or persistent)
    if (!m_lastDialogDir.isEmpty() && QDir(m_lastDialogDir).exists()) {
        return m_lastDialogDir;
    }

    // 3. User Desktop (Universal fallback prioritized over Home)
    const QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktopPath.isEmpty() && QDir(desktopPath).exists()) {
        return desktopPath;
    }

    // 4. Current working directory
    const QString cwd = QDir::currentPath();
    if (!cwd.isEmpty() && QDir(cwd).exists()) return cwd;

    // 5. User Home (Ultimate fallback)
    return QDir::homePath();
}

/**
 * @brief Updates the last used directory and synchronizes with system CWD.
 */
void MainWindow::updateLastUsedDir(const QString& path) {
    if (path.isEmpty()) return;

    QString chosenDir;
    QFileInfo fi(path);
    if (fi.isDir()) {
        chosenDir = fi.absoluteFilePath();
    } else {
        chosenDir = fi.absolutePath();
    }

    if (!chosenDir.isEmpty() && QDir(chosenDir).exists()) {
        m_lastDialogDir = chosenDir;

        // Update process CWD so standard file dialogs and other tools follow
        QDir::setCurrent(chosenDir);

        // Persist to settings
        QSettings settings("TStar", "TStar");
        settings.setValue("General/LastDialogDir", chosenDir);
        settings.setValue("General/LastWorkingDir", chosenDir);
        settings.sync(); // Force write to avoid loss on crash
    }
}


// ============================================================================
// Workspace Project — New / Open / Save / Close / Delete Actions
// ============================================================================

/**
 * @brief Creates a new empty workspace project, closing any existing one.
 */
void MainWindow::newWorkspaceProject() {
    if (!maybeSaveWorkspaceProject(tr("before creating a new project"))) {
        return;
    }
    if (!closeAllWorkspaceWindows()) {
        return;
    }

    m_restoringWorkspaceProject = true;
    m_workspaceProject.active      = true;
    m_workspaceProject.dirty       = false;
    m_workspaceProject.untitled    = true;
    m_workspaceProject.filePath.clear();
    m_workspaceProject.displayName = tr("Untitled Workspace Project");
    log(tr("New workspace project created."), Log_Success, true);
    showConsoleTemporarily(2000);
    m_restoringWorkspaceProject = false;
}

/**
 * @brief Opens a workspace project from a user-selected .tstarproj file.
 */
void MainWindow::openWorkspaceProject() {
    if (!maybeSaveWorkspaceProject(tr("before opening another project"))) {
        return;
    }

    QString projectFile = QFileDialog::getOpenFileName(
        this,
        tr("Open Workspace Project"),
        getProjectWorkingDirectory(),
        tr("TStar Workspace Project (*.tstarproj)"));
    if (projectFile.isEmpty()) return;

    const QString chosenDir = QFileInfo(projectFile).absolutePath();
    if (!chosenDir.isEmpty() && QDir(chosenDir).exists()) {
        QDir::setCurrent(chosenDir);
        QSettings settings("TStar", "TStar");
        settings.setValue("General/LastWorkingDir", chosenDir);
    }

    loadWorkspaceProjectFrom(projectFile);
}

/**
 * @brief Saves the workspace project to its current file path,
 *        or prompts for a path if it hasn't been saved before.
 */
void MainWindow::saveWorkspaceProject() {
    if (m_workspaceProject.filePath.isEmpty()) {
        saveWorkspaceProjectAs();
        return;
    }
    saveWorkspaceProjectTo(m_workspaceProject.filePath);
}

/**
 * @brief Prompts the user for a new file path and saves the workspace project.
 */
void MainWindow::saveWorkspaceProjectAs() {
    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save Workspace Project As"),
        getProjectWorkingDirectory(),
        tr("TStar Workspace Project (*.tstarproj)"));
    if (path.isEmpty()) return;

    const QString chosenDir = QFileInfo(path).absolutePath();
    if (!chosenDir.isEmpty() && QDir(chosenDir).exists()) {
        QDir::setCurrent(chosenDir);
        QSettings settings("TStar", "TStar");
        settings.setValue("General/LastWorkingDir", chosenDir);
    }

    saveWorkspaceProjectTo(path);
}

/**
 * @brief Closes the current workspace project after offering to save changes.
 */
void MainWindow::closeWorkspaceProject() {
    if (!maybeSaveWorkspaceProject(tr("before closing project"))) {
        return;
    }
    if (!closeAllWorkspaceWindows()) {
        return;
    }

    m_workspaceProject = WorkspaceProjectState{};
    log(tr("Workspace project closed."), Log_Info, true);
}

/**
 * @brief Deletes a workspace project and its snapshot data.
 *
 * Presents a list of available projects (detected by _data folders), allows
 * the user to select one, confirms deletion, removes the snapshot directory,
 * and optionally removes associated .tstarproj files found in common locations.
 */
void MainWindow::deleteWorkspaceProject() {
    // Collect available projects from the current workspace root
    QString projDataDir = getWorkspaceProjectsDir();
    QDir projDir(projDataDir);
    if (!projDir.exists()) {
        QMessageBox::information(this, tr("No Projects"), tr("No workspace projects found."));
        return;
    }

    // Find all _data folders
    QStringList filters;
    filters << "*_data";
    QStringList dataDirs = projDir.entryList(filters, QDir::Dirs);
    if (dataDirs.isEmpty()) {
        QMessageBox::information(this, tr("No Projects"), tr("No workspace projects found."));
        return;
    }

    // Extract project names from _data folders
    QStringList projectNames;
    QMap<QString, QString> projectMap; // name → dataDirPath
    for (const QString& dataDir : dataDirs) {
        QString projName = dataDir;
        projName.chop(5); // Remove "_data" suffix
        projectNames << projName;
        projectMap[projName] = projDir.filePath(dataDir);
    }

    // Prompt user to select a project
    bool ok = false;
    QString selectedProject = QInputDialog::getItem(this,
        tr("Delete Workspace Project"),
        tr("Select a project to delete:"),
        projectNames, 0, false, &ok);
    if (!ok || selectedProject.isEmpty()) {
        return;
    }

    QString dataDirPath = projectMap[selectedProject];

    // If the selected project is currently active, close it first
    QString currentProjFile = m_workspaceProject.filePath;
    QString currentProjName = m_workspaceProject.displayName;
    bool wasActive = false;
    if (m_workspaceProject.active && (currentProjName == selectedProject || QFileInfo(currentProjFile).baseName() == selectedProject)) {
        wasActive = true;
        if (!maybeSaveWorkspaceProject(tr("before deleting project"))) {
            return;
        }
        if (!closeAllWorkspaceWindows()) {
            QMessageBox::warning(this, tr("Deletion Error"), tr("Cannot delete project while it is open."));
            return;
        }
    }

    // Confirm deletion
    auto choice = QMessageBox::warning(this,
        tr("Confirm Deletion"),
        tr("Delete project '%1' and all its snapshots? This action cannot be undone.").arg(selectedProject),
        QMessageBox::Yes | QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }

    // Delete the snapshots directory
    QDir dataDir(dataDirPath);
    if (!dataDir.removeRecursively()) {
        QMessageBox::critical(this, tr("Deletion Error"),
            tr("Failed to delete project snapshots directory: %1").arg(dataDirPath));
        return;
    }

    // Locate and optionally delete associated .tstarproj files
    QStringList candidateProjFiles;
    if (wasActive && !currentProjFile.isEmpty() && QFile::exists(currentProjFile)) {
        candidateProjFiles << currentProjFile;
    }

    // Search common user locations for .tstarproj files referencing this snapshot dir
    QStringList searchRoots;
    searchRoots << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    searchRoots << QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (const QString& root : searchRoots) {
        if (root.isEmpty()) continue;
        QDirIterator it(root, QStringList() << "*.tstarproj", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString fpath = it.next();
            if (candidateProjFiles.contains(fpath)) continue;

            QFile f(fpath);
            if (!f.open(QIODevice::ReadOnly)) continue;
            QByteArray bytes = f.readAll();
            f.close();

            QJsonParseError perr;
            QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
            if (perr.error == QJsonParseError::NoError && doc.isObject()) {
                QString snap = doc.object().value("snapshotDir").toString();
                if (!snap.isEmpty()) {
                    QDir snapDir(snap);
                    if (QDir(dataDirPath).absolutePath() == snapDir.absolutePath()) {
                        candidateProjFiles << fpath;
                    }
                }
            }
        }
    }

    QStringList deletedProjFiles;
    if (!candidateProjFiles.isEmpty()) {
        QString infoList = candidateProjFiles.join("\n");
        auto delChoice = QMessageBox::question(this,
            tr("Delete Workspace Project"),
            tr("The following project files referencing this workspace were found: %1 Delete these files?").arg(infoList),
            QMessageBox::Yes | QMessageBox::No);
        if (delChoice == QMessageBox::Yes) {
            for (const QString& pf : candidateProjFiles) {
                if (QFile::remove(pf)) deletedProjFiles << pf;
            }
        }
    }

    log(tr("Deleted workspace project '%1' and its snapshots.").arg(selectedProject), Log_Info, true);

    QString infoMsg = tr("Workspace project '%1' has been deleted.").arg(selectedProject);
    if (!deletedProjFiles.isEmpty()) {
        infoMsg += "\n" + tr("Deleted project files: %1").arg(deletedProjFiles.join(", "));
    } else {
        infoMsg += "\n" + tr("Note: The .tstarproj file itself (if it exists) was NOT deleted and must be manually removed.");
    }
    QMessageBox::information(this, tr("Project Deleted"), infoMsg);
}

/**
 * @brief Loads a workspace project from a .tstarproj file at application startup.
 *        Called from main.cpp when a project file is passed as a command-line argument.
 */
void MainWindow::loadWorkspaceProjectAtStartup(const QString& projectFilePath) {
    if (!projectFilePath.isEmpty() && QFile::exists(projectFilePath)) {
        loadWorkspaceProjectFrom(projectFilePath);
    }
}


// ============================================================================
// Stacking Suite
// ============================================================================

/**
 * @brief Opens the Stacking dialog (modal).
 */
void MainWindow::openStackingDialog() {
    log(tr("Opening Stacking Dialog..."), Log_Action, true);
    StackingDialog dlg(this);
    dlg.exec();
}

/**
 * @brief Opens the Registration dialog (modal).
 */
void MainWindow::openRegistrationDialog() {
    log(tr("Opening Registration Dialog..."), Log_Action, true);
    RegistrationDialog dlg(this);
    dlg.exec();
}

/**
 * @brief Opens the Preprocessing dialog (modal).
 */
void MainWindow::openPreprocessingDialog() {
    log(tr("Opening Preprocessing Dialog..."), Log_Action, true);
    PreprocessingDialog dlg(this);
    dlg.exec();
}

/**
 * @brief Opens the New Project dialog. On acceptance, creates a stacking project
 *        and sets the working directory to the project root.
 */
void MainWindow::openNewProjectDialog() {
    log(tr("Opening New Project Dialog..."), Log_Action, true);
    NewProjectDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        Stacking::StackingProject* project = dlg.createProject();
        if (project) {
            QString dir = project->rootDir();
            QDir::setCurrent(dir);
            QSettings settings("TStar", "TStar");
            settings.setValue("General/LastWorkingDir", dir);
            log(tr("Project created and activated: %1").arg(dir), Log_Success, true);
            showConsoleTemporarily(2000);
            delete project;
        } else {
            log(tr("Failed to create project."), Log_Error, true);
        }
    }
}

/**
 * @brief Opens an existing stacking project from a user-selected directory.
 */
void MainWindow::openExistingProject() {
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Select Project Directory"),
        getProjectWorkingDirectory());

    if (dir.isEmpty()) return;

    if (QDir(dir).exists()) {
        QDir::setCurrent(dir);
        QSettings settings("TStar", "TStar");
        settings.setValue("General/LastWorkingDir", dir);
    }

    Stacking::StackingProject project;
    QString projectFile = Stacking::StackingProject::findProjectFile(dir);

    if (!projectFile.isEmpty() && project.load(projectFile)) {
        log(tr("Project loaded: %1").arg(project.name()), Log_Success, true);
        showConsoleTemporarily(2000);
        StackingDialog dlg(this);
        dlg.exec();
    } else {
        log(tr("No valid project found at: %1").arg(dir), Log_Warning, true);
    }
}

/**
 * @brief Opens the batch file Conversion dialog (modal).
 */
void MainWindow::openConvertDialog() {
    log(tr("Opening Conversion Dialog..."), Log_Action, true);
    ConversionDialog dlg(this);
    dlg.exec();
}


// ============================================================================
// Script Browser
// ============================================================================

/**
 * @brief Opens the Script Browser dialog. On selection, runs the chosen script
 *        in a non-modal ScriptDialog with console and progress bar.
 */
void MainWindow::openScriptDialog() {
    log(tr("Opening Script Browser..."), Log_Action, true);
    ScriptBrowserDialog browserDlg(this);

    if (browserDlg.exec() == QDialog::Accepted) {
        QString scriptFile = browserDlg.selectedScript();
        if (scriptFile.isEmpty()) return;

        log(tr("Running script: %1").arg(QFileInfo(scriptFile).fileName()), Log_Action, true);

        ScriptDialog* scriptDlg = new ScriptDialog(this);
        scriptDlg->setAttribute(Qt::WA_DeleteOnClose);
        scriptDlg->loadScript(scriptFile);

        connect(scriptDlg, &QDialog::finished, this, [this](int) {
            log(tr("Script dialog closed."), Log_Info);
        });

        scriptDlg->show();
    }
}


// ============================================================================
// Status Bar — Pixel Info
// ============================================================================

/**
 * @brief Updates the pixel information label in the status bar.
 */
void MainWindow::updatePixelInfo(const QString& info) {
    if (m_pixelInfoLabel) {
        m_pixelInfoLabel->setText(info);
    }
}


// ============================================================================
// Multiscale Decomposition
// ============================================================================

/**
 * @brief Opens the Multiscale Decomposition dialog as a tool sub-window.
 */
void MainWindow::openMultiscaleDecompDialog() {
    ImageViewer* v = currentViewer();
    if (!v || !v->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("Please open an image first."));
        return;
    }

    if (m_multiscaleDecompDlg) {
        m_multiscaleDecompDlg->raise();
        m_multiscaleDecompDlg->activateWindow();
        m_multiscaleDecompDlg->setViewer(v);
        return;
    }

    m_multiscaleDecompDlg = new MultiscaleDecompDialog(this);
    log(tr("Opening Multiscale Decomposition..."), Log_Action, true);
    m_multiscaleDecompDlg->setAttribute(Qt::WA_DeleteOnClose);
    m_multiscaleDecompDlg->setViewer(v);

    connect(m_multiscaleDecompDlg, &QDialog::destroyed, this, [this]() {
        m_multiscaleDecompDlg = nullptr;
    });

    setupToolSubwindow(nullptr, m_multiscaleDecompDlg, tr("Multiscale Decomposition"));
}


// ============================================================================
// Narrowband Normalization
// ============================================================================

/**
 * @brief Opens the Narrowband Normalization dialog as a tool sub-window.
 */
void MainWindow::openNarrowbandNormalizationDialog() {
    if (m_nbNormDlg) {
        m_nbNormDlg->raise();
        m_nbNormDlg->activateWindow();
        m_nbNormDlg->refreshImageList();
        return;
    }

    m_nbNormDlg = new NarrowbandNormalizationDialog(this);
    log(tr("Opening Narrowband Normalization..."), Log_Action, true);
    m_nbNormDlg->setAttribute(Qt::WA_DeleteOnClose);
    if (currentViewer()) {
        m_nbNormDlg->setViewer(currentViewer());
    }

    connect(m_nbNormDlg, &QDialog::destroyed, this, [this]() {
        m_nbNormDlg = nullptr;
    });

    setupToolSubwindow(nullptr, m_nbNormDlg, tr("Narrowband Normalization"));
}


// ============================================================================
// NB -> RGB Stars
// ============================================================================

/**
 * @brief Opens the NB → RGB Stars dialog as a tool sub-window.
 */
void MainWindow::openNBtoRGBStarsDialog() {
    if (m_nbToRGBStarsDlg) {
        m_nbToRGBStarsDlg->raise();
        m_nbToRGBStarsDlg->activateWindow();
        return;
    }

    m_nbToRGBStarsDlg = new NBtoRGBStarsDialog(this);
    log(tr("Opening NB -> RGB Stars..."), Log_Action, true);
    m_nbToRGBStarsDlg->setAttribute(Qt::WA_DeleteOnClose);
    if (currentViewer()) {
        m_nbToRGBStarsDlg->setViewer(currentViewer());
    }

    connect(m_nbToRGBStarsDlg, &QDialog::destroyed, this, [this]() {
        m_nbToRGBStarsDlg = nullptr;
    });

    setupToolSubwindow(nullptr, m_nbToRGBStarsDlg, tr("NB -> RGB Stars"));
}


// ============================================================================
// Blink Comparator
// ============================================================================

/**
 * @brief Opens the Blink Comparator dialog as a tool sub-window.
 */
void MainWindow::openBlinkComparatorDialog() {
    if (m_blinkComparatorDlg) {
        m_blinkComparatorDlg->raise();
        m_blinkComparatorDlg->activateWindow();
        m_blinkComparatorDlg->updateViewLists();
        return;
    }

    m_blinkComparatorDlg = new BlinkComparatorDialog(this, this);
    log(tr("Opening Blink Comparator..."), Log_Action, true);
    m_blinkComparatorDlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(m_blinkComparatorDlg, &QDialog::destroyed, this, [this]() {
        m_blinkComparatorDlg = nullptr;
    });

    setupToolSubwindow(nullptr, m_blinkComparatorDlg, tr("Blink Comparator"));
}