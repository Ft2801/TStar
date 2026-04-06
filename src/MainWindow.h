// =============================================================================
// MainWindow.h
//
// Primary application window for TStar. Manages the MDI workspace, menu and
// toolbar actions, tool dialog lifecycle, undo/redo, workspace project
// persistence, display state, view linking, image loading queue, and the
// sidebar / header panel layout.
// =============================================================================

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// ---- Qt headers -------------------------------------------------------------
#include <QMainWindow>
#include <QMdiArea>
#include <QStack>
#include <QPointer>
#include <QMap>
#include <QSettings>
#include <QQueue>
#include <QMutex>

// ---- Standard library -------------------------------------------------------
#include <functional>
#include <memory>

// ---- Project headers --------------------------------------------------------
#include "ImageViewer.h"
#include "ImageBuffer.h"
#include "MainWindowCallbacks.h"
#include "algos/CubicSpline.h"
#include "dialogs/GHSDialog.h"
#include "dialogs/CurvesDialog.h"
#include "dialogs/SaturationDialog.h"
#include "dialogs/TemperatureTintDialog.h"
#include "dialogs/PixelMathDialog.h"

// ---- Forward declarations ---------------------------------------------------

class QJsonObject;
class VizierClient;
class StretchDialog;
class CustomMdiSubWindow;
class ABEDialog;
class CBEDialog;
class BackgroundNeutralizationDialog;
class WavescaleHDRDialog;
class HistogramStretchDialog;
class ArcsinhStretchDialog;
class SCNRDialog;
class RARDialog;
class StarStretchDialog;
class StarRecompositionDialog;
class PerfectPaletteDialog;
class PlateSolvingDialog;
class PCCDialog;
class SPCCDialog;
class CropRotateDialog;
class StarAnalysisDialog;
class HeaderViewerDialog;
class SidebarWidget;
class RightSidebarWidget;
class HeaderPanel;
class AstroSpikeDialog;
class RawEditorDialog;
class DebayerDialog;
class ContinuumSubtractionDialog;
class AlignChannelsDialog;
class AnnotationToolDialog;
class MultiscaleDecompDialog;
class NarrowbandNormalizationDialog;
class NBtoRGBStarsDialog;
class BlinkComparatorDialog;
class MorphologyDialog;

struct Annotation;

// =============================================================================
// Background image loading result
// =============================================================================

/// Severity levels for image loading log messages.
enum ImageLoadLogLevel {
    ImageLog_Success = 0,
    ImageLog_Error   = 1,
    ImageLog_Info    = 2
};

/// Result of a background image file load operation.
/// Queued for thread-safe hand-off from the loader thread to the UI thread.
struct ImageFileLoadResult {
    bool              success    = false;
    ImageBuffer       buffer;             ///< Valid only when success == true
    QString           title;
    QString           sourcePath;
    QString           logMsg;
    ImageLoadLogLevel logLevel   = ImageLog_Info;
    bool              logPopup   = false;
};

// =============================================================================
// MainWindow class
// =============================================================================

class MainWindow : public QMainWindow, public MainWindowCallbacks {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // ---- Public API for dialogs ---------------------------------------------

    /// Returns the viewer in the currently active MDI sub-window, or nullptr.
    ImageViewer* currentViewer() const;

    /// Returns true if there is an active image loaded.
    bool hasImage() const;

    /// Show/hide the global progress indicator and disable/enable the UI.
    void startLongProcess() override;
    void endLongProcess()   override;

    /// Create a new MDI sub-window displaying the given buffer.
    CustomMdiSubWindow* createNewImageWindow(
        const ImageBuffer& buffer, const QString& title,
        ImageBuffer::DisplayMode mode = static_cast<ImageBuffer::DisplayMode>(-1),
        float autoStretchMedian = 0.25f, bool displayLinked = true
    );

    /// Push the current buffer state onto the undo stack.
    void pushUndo(const QString& description = QString());

    // ---- MainWindowCallbacks implementation ---------------------------------

    ImageBuffer*  getCurrentImageBuffer() override;
    ImageViewer*  getCurrentViewer()      override;

    void createResultWindow(const ImageBuffer& buffer, const QString& title,
                            int mode = -1, float median = 0.25f,
                            bool linked = true) override;

    void logMessage(const QString& message, int severity,
                    bool showPopup = false) override;

    bool isViewerInUse(ImageViewer* viewer,
                       QString* toolName = nullptr) const override;

    void refreshHeaderPanel() override;

    /// Activate an existing tool dialog by title, returning true if found.
    bool activateTool(const QString& title);

    // ---- Display state accessors --------------------------------------------

    ImageBuffer::DisplayMode displayMode()   const { return m_displayMode; }
    bool                     displayLinked()  const { return m_displayLinked; }
    ImageBuffer::DisplayMode getDefaultDisplayMode() const;

    // ---- Workspace project management (public entry points) -----------------

    void loadWorkspaceProjectAtStartup(const QString& projectFilePath);

    // ---- Logging ------------------------------------------------------------

    enum LogType {
        Log_Info,
        Log_Success,
        Log_Warning,
        Log_Error,
        Log_Action
    };

    void log(const QString& msg, LogType type = Log_Info,
             bool autoShow = false, bool isTransient = false);

    // ---- Persisted annotations (survive dialog destruction) -----------------

    QVector<Annotation>           m_persistedAnnotations;
    QStack<QVector<Annotation>>   m_persistedUndoStack;
    QStack<QVector<Annotation>>   m_persistedRedoStack;

private slots:
    // ---- Undo / redo --------------------------------------------------------
    void undo();
    void redo();

    // ---- Display action -----------------------------------------------------
    void onBurnDisplay();

    // ---- File actions -------------------------------------------------------
    void openFile();
    void saveFile();

    // ---- Processing tool dialogs --------------------------------------------
    void openStretchDialog();
    void openAbeDialog();
    void openCbeDialog();
    void openSCNRDialog();
    void openGHSDialog();
    void openCurvesDialog();
    void openSaturationDialog();
    void openArcsinhStretchDialog();
    void openHistogramStretchDialog();
    void openWavescaleHDRDialog();
    void openRARDialog();
    void openStarStretchDialog();
    void openStarRecompositionDialog();
    void openImageBlendingDialog();
    void openPerfectPaletteDialog();
    void openBackgroundNeutralizationDialog();
    void openPixelMathDialog();
    void openAstroSpikeDialog();
    void openRawEditorDialog();
    void openDebayerDialog();
    void openContinuumSubtractionDialog();
    void openAlignChannelsDialog();
    void openImageAnnotatorDialog();
    void openMultiscaleDecompDialog();
    void openNarrowbandNormalizationDialog();
    void openNBtoRGBStarsDialog();
    void openBlinkComparatorDialog();
    void openMorphologyDialog();
    void openTemperatureTintDialog();
    void openMagentaCorrectionDialog();
    void openSelectiveColorDialog();
    void openClaheDialog();
    void openStarHaloRemovalDialog();
    void openCorrectionBrushDialog();
    void openColorProfileDialog();
    void openAnnotationToolDialog();
    void openAberrationInspectorDialog();

    // ---- Channel operations -------------------------------------------------
    void extractChannels();
    void combineChannels();
    void openExtractLuminanceDialog();
    void openRecombineLuminanceDialog();

    // ---- Geometry operations ------------------------------------------------
    void cropTool();
    void applyGeometry(const QString& operation);
    void applyGeometry(const QString& name,
                       std::function<void(ImageBuffer&)> func);
    void openBinningDialog();
    void openUpscaleDialog();
    void removePedestal();

    // ---- Astrometry & photometry --------------------------------------------
    void openPlateSolvingDialog();
    void openPCCDialog();
    void openSPCCDialog();
    void openPCCDistributionDialog();

    // ---- Inspection tools ---------------------------------------------------
    void openHeaderDialog();
    void openHeaderEditorDialog();
    void openStarAnalysisDialog();

    // ---- Stacking suite -----------------------------------------------------
    void openStackingDialog();
    void openRegistrationDialog();
    void openPreprocessingDialog();
    void openNewProjectDialog();
    void openExistingProject();
    void openConvertDialog();
    void openScriptDialog();

    // ---- Workspace project management ---------------------------------------
    void newWorkspaceProject();
    void openWorkspaceProject();
    void saveWorkspaceProject();
    void saveWorkspaceProjectAs();
    void closeWorkspaceProject();
    void deleteWorkspaceProject();

    // ---- Mask tool actions ---------------------------------------------------
    void createMaskAction();
    void applyMaskAction();
    void removeMaskAction();
    void invertMaskAction();
    void toggleMaskOverlayAction();

    // ---- Sidebar & tools ----------------------------------------------------
    void setupSidebarTools();
    void runCosmicClarity(const struct CosmicClarityParams& params);
    void runGraXpert(const struct GraXpertParams& params);

    // ---- Window arrangement -------------------------------------------------
    void tileImageViews();
    void tileImageViewsVertical();
    void tileImageViewsHorizontal();

    // ---- Settings -----------------------------------------------------------
    void onSettingsAction();

    // ---- View linking -------------------------------------------------------
    void propagateViewChange(float scale, float hVal, float vVal);

    // ---- Curves integration -------------------------------------------------
    void applyCurvesPreview(const std::vector<std::vector<float>>& lut);
    void applyCurves(const SplineData& spline, const bool channels[3]);

    // ---- Color profile handling ---------------------------------------------
    void checkAndHandleColorProfile(ImageBuffer& buffer, const QString& title);

    // ---- Display refresh ----------------------------------------------------
    void updateActiveImage();

private:
    // ---- Display & menu updates ---------------------------------------------
    void updateDisplay() override;
    void updateMenus();

    // ---- Workspace project internals ----------------------------------------

    void    markWorkspaceProjectDirty();
    bool    maybeSaveWorkspaceProject(const QString& reason);
    bool    saveWorkspaceProjectTo(const QString& projectFilePath);
    bool    loadWorkspaceProjectFrom(const QString& projectFilePath);
    QString getWorkspaceProjectsDir() const;
    QString getProjectWorkingDirectory() const;

    QJsonObject captureWorkspaceProjectState(
        const QString& dataDirPath, const QString& projectBaseDir,
        QList<struct SaveSnapshotJob>& saveJobs
    );

    bool restoreWorkspaceProjectState(
        const QJsonObject& root, const QString& dataDirPath,
        const QString& projectBaseDir
    );

    bool closeAllWorkspaceWindows();
    void connectSubwindowProjectTracking(CustomMdiSubWindow* sub);

    /// Collect all open image viewers as Pixel Math variable references.
    QVector<PMImageRef> getImageRefsForPixelMath() const;

    // ---- MDI workspace ------------------------------------------------------

    QMdiArea* m_mdiArea;
    QString   generateUniqueTitle(const QString& baseTitle);

    // ---- Image data & history -----------------------------------------------

    ImageBuffer              m_buffer;
    std::vector<ImageBuffer> m_undoStack;
    std::vector<ImageBuffer> m_redoStack;
    const size_t             MAX_HISTORY = 20;

    // ---- Actions ------------------------------------------------------------

    QAction* m_undoAction                      = nullptr;
    QAction* m_redoAction                      = nullptr;
    QAction* m_newWorkspaceProjectAction        = nullptr;
    QAction* m_openWorkspaceProjectAction       = nullptr;
    QAction* m_saveWorkspaceProjectAction       = nullptr;
    QAction* m_saveWorkspaceProjectAsAction     = nullptr;
    QAction* m_closeWorkspaceProjectAction      = nullptr;
    QAction* m_deleteWorkspaceProjectAction     = nullptr;

    // ---- Display state ------------------------------------------------------

    ImageBuffer::DisplayMode m_displayMode   = ImageBuffer::Display_Linear;
    bool                     m_displayLinked  = true;

    // ---- Workspace project state --------------------------------------------

    struct WorkspaceProjectState {
        bool    active      = false;
        bool    dirty       = false;
        bool    untitled    = false;
        QString filePath;
        QString displayName;
    };

    WorkspaceProjectState m_workspaceProject;
    bool m_restoringWorkspaceProject = false;
    int  m_dirtySuppressCount        = 0;

    bool isDirtyBlocked() const {
        return m_restoringWorkspaceProject || m_dirtySuppressCount > 0;
    }

    void suppressDirtyFlag(int durationMs);

    // ---- Console state (sidebar process feedback) ---------------------------

    bool    m_wasConsoleOpen     = false;
    bool    m_isConsoleTempOpen  = false;
    bool    m_isLoadingBatch     = false;
    QTimer* m_tempConsoleTimer   = nullptr;

    // ---- Tool dialog singletons (QPointers for safe access) -----------------

    QPointer<GHSDialog>                        m_ghsDlg;
    QPointer<CurvesDialog>                     m_curvesDlg;
    QPointer<StretchDialog>                    m_stretchDlg;
    QPointer<SaturationDialog>                 m_satDlg;
    QPointer<TemperatureTintDialog>            m_tempTintDlg;
    QPointer<ABEDialog>                        m_abeDlg;
    QPointer<CBEDialog>                        m_cbeDlg;
    QPointer<BackgroundNeutralizationDialog>   m_bnDlg;
    QPointer<WavescaleHDRDialog>               m_wavescaleDlg;
    QPointer<HistogramStretchDialog>           m_histoDlg;
    QPointer<ArcsinhStretchDialog>             m_arcsinhDlg;
    QPointer<SCNRDialog>                       m_scnrDlg;
    QPointer<class MagentaCorrectionDialog>    m_magentaDlg;
    QPointer<PixelMathDialog>                  m_pixelMathDialog;
    QPointer<RARDialog>                        m_rarDlg;
    QPointer<StarStretchDialog>                m_starStretchDlg;
    QPointer<StarRecompositionDialog>          m_starRecompDlg;
    QPointer<class ImageBlendingDialog>        m_imageBlendingDlg;
    QPointer<PerfectPaletteDialog>             m_ppDialog;
    QPointer<PlateSolvingDialog>               m_plateSolveDlg;
    QPointer<PCCDialog>                        m_pccDlg;
    QPointer<SPCCDialog>                       m_spccDlg;
    QPointer<CropRotateDialog>                 m_cropDlg;
    QPointer<StarAnalysisDialog>               m_starAnalysisDlg;
    QPointer<HeaderViewerDialog>               m_headerDlg;
    QPointer<AstroSpikeDialog>                 m_astroSpikeDlg;
    QPointer<RawEditorDialog>                  m_rawEditorDlg;
    QPointer<DebayerDialog>                    m_debayerDlg;
    QPointer<ContinuumSubtractionDialog>       m_continuumDlg;
    QPointer<AlignChannelsDialog>              m_alignChannelsDlg;
    QPointer<AnnotationToolDialog>             m_annotatorDlg;
    QPointer<MultiscaleDecompDialog>           m_multiscaleDecompDlg;
    QPointer<NarrowbandNormalizationDialog>    m_nbNormDlg;
    QPointer<NBtoRGBStarsDialog>               m_nbToRGBStarsDlg;
    QPointer<BlinkComparatorDialog>            m_blinkComparatorDlg;
    QPointer<class SettingsDialog>             m_settingsDlg;
    QPointer<class ColorProfileDialog>         m_colorProfileDlg;
    QPointer<MorphologyDialog>                 m_morphologyDlg;
    QPointer<class BinningDialog>              m_binDlg;
    QPointer<class UpscaleDialog>              m_upscaleDlg;

    bool m_lastWasProgress = false;

    // ---- UI elements --------------------------------------------------------

    class QComboBox*    m_stretchCombo          = nullptr;
    class QToolButton*  m_autoStretchMedianBtn  = nullptr;
    float               m_autoStretchMedianValue = 0.25f;
    class QToolButton*  m_linkChannelsBtn       = nullptr;
    class QToolButton*  m_burnDisplayBtn        = nullptr;
    class QToolButton*  m_invertBtn             = nullptr;
    class QToolButton*  m_falseColorBtn         = nullptr;
    class QAction*      m_linkViewsAction       = nullptr;
    class QToolButton*  m_channelViewBtn        = nullptr;
    class QAction*      m_toggleOverlayAct      = nullptr;

    // ---- Tool window management ---------------------------------------------

    CustomMdiSubWindow* setupToolSubwindow(CustomMdiSubWindow* sub,
                                           QWidget* dlg, const QString& title);
    void centerToolWindow(CustomMdiSubWindow* sub);

    // ---- Active viewer tracking ---------------------------------------------

    QPointer<ImageViewer> m_lastActiveImageViewer;
    QPointer<ImageViewer> m_curvesTarget;
    QPointer<ImageViewer> m_ghsTarget;
    QPointer<ImageViewer> m_satTarget;
    QPointer<ImageViewer> m_tempTintTarget;
    QPointer<QWidget>     m_activeInteractiveTool;

    // ---- Per-viewer tool state persistence -----------------------------------

    QMap<ImageViewer*, GHSDialog::State>                m_ghsStates;
    QMap<ImageViewer*, CurvesDialog::State>             m_curvesStates;
    QMap<ImageViewer*, SaturationDialog::State>         m_satStates;
    QMap<ImageViewer*, TemperatureTintDialog::State>    m_tempTintStates;

    // ---- Sidebar panels -----------------------------------------------------

    SidebarWidget*      m_sidebar       = nullptr;
    RightSidebarWidget* m_rightSidebar  = nullptr;
    HeaderPanel*        m_headerPanel   = nullptr;

    // ---- Animations ---------------------------------------------------------

    bool                     m_isClosing = false;
    class QPropertyAnimation* m_anim     = nullptr;

    void startFadeIn();
    void startFadeOut();
    void showConsoleTemporarily(int durationMs = 3000);

    // ---- Event overrides ----------------------------------------------------

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    QSettings m_settings;

private:
    bool    m_isUpdating = false;
    QString m_lastDialogDir;
    void updateLastUsedDir(const QString& path);
    
    // ---- Pixel info label ---------------------------------------------------

    class QLabel* m_pixelInfoLabel = nullptr;
    void updatePixelInfo(const QString& info);

    // ---- Asynchronous image loading queue ------------------------------------

    QMutex                                              m_imageLoadMutex;
    QQueue<std::shared_ptr<ImageFileLoadResult>>        m_imageLoadQueue;
    QTimer*                                             m_imageDisplayTimer = nullptr;

    /// Process one queued image load result per timer tick.
    void processImageLoadQueue();
};

#endif // MAINWINDOW_H