#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMdiArea>
#include <QStack>
#include "ImageViewer.h"
#include "ImageBuffer.h"
#include <functional>
#include "algos/CubicSpline.h"
#include <QPointer>
#include "MainWindowCallbacks.h"
#include "dialogs/GHSDialog.h"
#include "dialogs/CurvesDialog.h"
#include "dialogs/SaturationDialog.h"
#include "dialogs/TemperatureTintDialog.h"
#include "dialogs/PixelMathDialog.h"
#include <QMap>
#include <QSettings>
#include <QQueue>
#include <QMutex>
#include <memory>


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
class CropRotateDialog; // Forward declaration
class StarAnalysisDialog;
class HeaderViewerDialog;
class SidebarWidget;
class RightSidebarWidget;
class HeaderPanel;
class AstroSpikeDialog;
class DebayerDialog;
class ContinuumSubtractionDialog;
class AlignChannelsDialog;
class AnnotationToolDialog;
class MultiscaleDecompDialog;
class NarrowbandNormalizationDialog;
class NBtoRGBStarsDialog;
struct Annotation;
class BlinkComparatorDialog;

// Background image loading result - for thread-safe queue communication
enum ImageLoadLogLevel {
    ImageLog_Success = 0,
    ImageLog_Error = 1,
    ImageLog_Info = 2
};

struct ImageFileLoadResult {
    bool             success    = false;
    ImageBuffer      buffer;           // valid only when success == true
    QString          title;
    QString          logMsg;
    ImageLoadLogLevel logLevel = ImageLog_Info;
    bool             logPopup   = false;
};

class MainWindow : public QMainWindow, public MainWindowCallbacks {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    
    // Public API for Dialogs
    ImageViewer* currentViewer() const;
    bool hasImage() const;
    void startLongProcess() override;
    void endLongProcess() override;
    CustomMdiSubWindow* createNewImageWindow(const ImageBuffer& buffer, const QString& title,
                              ImageBuffer::DisplayMode mode = ImageBuffer::Display_Linear,
                              float autoStretchMedian = 0.25f, bool displayLinked = true);
    void pushUndo(); // Call before destructive actions
    
    // MainWindowCallbacks Implementation
    ImageBuffer* getCurrentImageBuffer() override;
    ImageViewer* getCurrentViewer() override;
    void createResultWindow(const ImageBuffer& buffer, const QString& title) override;
    void logMessage(const QString& message, int severity, bool showPopup = false) override;
    bool isViewerInUse(class ImageViewer* viewer, QString* toolName = nullptr) const override;
    
    // Helper to check if tool is already open and activate it
    bool activateTool(const QString& title);
    
    ImageBuffer::DisplayMode displayMode() const { return m_displayMode; }
    bool displayLinked() const { return m_displayLinked; }

private slots:
    void undo();
    void redo();
    void onBurnDisplay();  // Burn the current display view to the buffer
    // Existing slots...
    void openFile();
    void saveFile();
    void openStretchDialog();
    void openAbeDialog();
    void openCbeDialog();
    void openSCNRDialog();
    void openGHSDialog();
    void extractChannels();
    void combineChannels();
    void cropTool(); 
    void openRARDialog();
    void openStarStretchDialog();
    void openStarRecompositionDialog();
    void openPerfectPaletteDialog();
    void applyGeometry(const QString& operation);
    void applyGeometry(const QString& name, std::function<void(ImageBuffer&)> func);
    void openPlateSolvingDialog();
    void openPCCDialog();
    void openPCCDistributionDialog();
    void openBackgroundNeutralizationDialog();
    void openPixelMathDialog();
    void openHeaderDialog();
    void openStarAnalysisDialog(); // Added
    void openArcsinhStretchDialog();
    void openHistogramStretchDialog();
    void openWavescaleHDRDialog();
    void openSaturationDialog();
    void openAstroSpikeDialog();
    void openHeaderEditorDialog();
    void openDebayerDialog();
    void openContinuumSubtractionDialog();
    void openAlignChannelsDialog();
    void openImageAnnotatorDialog();
    void openMultiscaleDecompDialog();
    void openNarrowbandNormalizationDialog();
    void openNBtoRGBStarsDialog();
    void openBlinkComparatorDialog();
    
    void openExtractLuminanceDialog();
    void openRecombineLuminanceDialog();
    void openCorrectionBrushDialog();
    void removePedestal();
    void openClaheDialog();
    void openAberrationInspectorDialog();
    void openSelectiveColorDialog();
    void openTemperatureTintDialog();
    void openMagentaCorrectionDialog();
    
    // Stacking Suite
    void openStackingDialog();
    void openRegistrationDialog();
    void openPreprocessingDialog();
    void openNewProjectDialog();
    void openExistingProject();
    void openConvertDialog();
    void openScriptDialog();
    
    // Mask Tool Actions
    void createMaskAction();
    void applyMaskAction();
    void removeMaskAction();
    void invertMaskAction();
    void toggleMaskOverlayAction();
    
    // Tools
    void setupSidebarTools();
    void runCosmicClarity(const struct CosmicClarityParams& params);
    void runGraXpert(const struct GraXpertParams& params);
    void tileImageViews();
    void tileImageViewsVertical();
    void tileImageViewsHorizontal();
    // Settings
    void onSettingsAction();

    // Zoom Link
    void propagateViewChange(float scale, float hVal, float vVal);

    void openCurvesDialog();
    void applyCurvesPreview(const std::vector<std::vector<float>>& lut);
    void applyCurves(const SplineData& spline, const bool channels[3]);

    void updateActiveImage(); // Public wrapper to refresh viewer

private:
    void updateDisplay() override;
    void updateMenus(); // Enable/Disable Undo/Redo

    // Returns all open image viewers as PMImageRef variables for Pixel Math.
    QVector<PMImageRef> getImageRefsForPixelMath() const;

    QMdiArea* m_mdiArea;
    // Moved to public: void createNewImageWindow(const ImageBuffer& buffer, const QString& title);
    QString generateUniqueTitle(const QString& baseTitle);

    // Removed single m_viewer
    // ImageViewer* m_viewer;
    ImageBuffer m_buffer;
    
    // History
    std::vector<ImageBuffer> m_undoStack;
    std::vector<ImageBuffer> m_redoStack;
    const size_t MAX_HISTORY = 20; // Limit memory usage

    QAction* m_undoAction;
    QAction* m_redoAction;

    
    // State
    
    ImageBuffer::DisplayMode m_displayMode = ImageBuffer::Display_Linear;
    bool m_displayLinked = true;

public:
    enum LogType { Log_Info, Log_Success, Log_Warning, Log_Error, Log_Action };
    void log(const QString& msg, LogType type = Log_Info, bool autoShow = false, bool isTransient = false);

protected:
    void resizeEvent(class QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void dragEnterEvent(class QDragEnterEvent* event) override;
    void dropEvent(class QDropEvent* event) override;
    
    QSettings m_settings;

private:
    // Console Logic for Process Feedback (Moved to Sidebar)
    bool m_wasConsoleOpen = false; // Remembers state before process started
    bool m_isConsoleTempOpen = false; // True if opened by timer/auto
    QTimer* m_tempConsoleTimer = nullptr;
    
    QPointer<class GHSDialog> m_ghsDlg;
    QPointer<class CurvesDialog> m_curvesDlg;
    QPointer<class StretchDialog> m_stretchDlg;
    QPointer<class SaturationDialog> m_satDlg;
    QPointer<class TemperatureTintDialog> m_tempTintDlg;
    
    bool m_lastWasProgress = false; // Tracks if the previous log line was a progress update

    // Tool Dialog Singletons
    QPointer<class ABEDialog> m_abeDlg;
    QPointer<class CBEDialog> m_cbeDlg;
    QPointer<class BackgroundNeutralizationDialog> m_bnDlg;
    QPointer<class WavescaleHDRDialog> m_wavescaleDlg;
    QPointer<class HistogramStretchDialog> m_histoDlg;
    QPointer<class ArcsinhStretchDialog> m_arcsinhDlg;
    QPointer<class SCNRDialog> m_scnrDlg;
    QPointer<class MagentaCorrectionDialog> m_magentaDlg;
    QPointer<PixelMathDialog> m_pixelMathDialog;
    QPointer<class RARDialog> m_rarDlg;
    QPointer<class StarStretchDialog> m_starStretchDlg;
    QPointer<class StarRecompositionDialog> m_starRecompDlg;
    QPointer<class PerfectPaletteDialog> m_ppDialog;
    QPointer<class PlateSolvingDialog> m_plateSolveDlg;
    QPointer<class PCCDialog> m_pccDlg;
    QPointer<class CropRotateDialog> m_cropDlg;
    QPointer<class StarAnalysisDialog> m_starAnalysisDlg;
    QPointer<class HeaderViewerDialog> m_headerDlg;
    QPointer<class AstroSpikeDialog> m_astroSpikeDlg;
    QPointer<class DebayerDialog> m_debayerDlg;
    QPointer<class ContinuumSubtractionDialog> m_continuumDlg;
    QPointer<class AlignChannelsDialog> m_alignChannelsDlg;
    QPointer<class AnnotationToolDialog> m_annotatorDlg;
    QPointer<class MultiscaleDecompDialog> m_multiscaleDecompDlg;
    QPointer<class NarrowbandNormalizationDialog> m_nbNormDlg;
    QPointer<class NBtoRGBStarsDialog> m_nbToRGBStarsDlg;
    QPointer<class BlinkComparatorDialog> m_blinkComparatorDlg;
    QPointer<class SettingsDialog> m_settingsDlg;
    
public:
    // Persisted annotations across dialog destruction
    QVector<struct Annotation> m_persistedAnnotations;
    QStack<QVector<struct Annotation>> m_persistedUndoStack;
    QStack<QVector<struct Annotation>> m_persistedRedoStack;

private:


    // UI Elements
    class QComboBox* m_stretchCombo;
    class QToolButton* m_autoStretchMedianBtn = nullptr;
    float m_autoStretchMedianValue = 0.25f;
    class QToolButton* m_linkChannelsBtn;
    class QToolButton* m_burnDisplayBtn;
    class QToolButton* m_invertBtn;
    class QToolButton* m_falseColorBtn;
    class QAction* m_linkViewsAction;
    // Channel view button (popup: RGB / R / G / B)
    class QToolButton* m_channelViewBtn = nullptr;
    class QAction* m_toggleOverlayAct = nullptr;
    
    CustomMdiSubWindow* setupToolSubwindow(CustomMdiSubWindow* sub, QWidget* dlg, const QString& title);
    void centerToolWindow(CustomMdiSubWindow* sub);
    QPointer<ImageViewer> m_lastActiveImageViewer;

    QPointer<ImageViewer> m_curvesTarget;
    QPointer<ImageViewer> m_ghsTarget;
    QPointer<ImageViewer> m_satTarget; // Track Saturation Target
    QPointer<ImageViewer> m_tempTintTarget; // Track Temperature/Tint Target
    QPointer<QWidget> m_activeInteractiveTool; // Tracks presently exclusive tool (ABE vs BN)

    // Tool State Persistence
    QMap<ImageViewer*, GHSDialog::State> m_ghsStates;
    QMap<ImageViewer*, CurvesDialog::State> m_curvesStates;
    QMap<ImageViewer*, SaturationDialog::State> m_satStates;
    QMap<ImageViewer*, TemperatureTintDialog::State> m_tempTintStates;

    // Sidebar
    SidebarWidget* m_sidebar = nullptr;
    RightSidebarWidget* m_rightSidebar = nullptr;
    HeaderPanel* m_headerPanel = nullptr;
    
    // Animations
    bool m_isClosing = false;
    class QPropertyAnimation* m_anim = nullptr;
    void startFadeIn();
    void startFadeOut();
    void showConsoleTemporarily(int durationMs = 3000);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    bool m_isUpdating = false;
    
    // Pixel Info
    class QLabel* m_pixelInfoLabel = nullptr;
    void updatePixelInfo(const QString& info);
    
    // Image Loading Queue - decouples fast file loading from slower UI window creation
    QMutex m_imageLoadMutex;
    QQueue<std::shared_ptr<struct ImageFileLoadResult>> m_imageLoadQueue;
    QTimer* m_imageDisplayTimer = nullptr;
    void processImageLoadQueue();  // Process one image from the queue per tick
};
 #endif // MAINWINDOW_H
