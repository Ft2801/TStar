#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMdiArea>
#include "ImageViewer.h"
#include "ImageBuffer.h"
#include <functional>
#include "algos/CubicSpline.h"
#include <QPointer>

class VizierClient;
// Removed forward decls that are now included
class StretchDialog; 
#include "dialogs/GHSDialog.h"
#include "dialogs/CurvesDialog.h"
#include "dialogs/SaturationDialog.h"
#include <QMap>
#include <QSettings>

class StretchDialog; 
class PixelMathDialog;
class CustomMdiSubWindow;
class ABEDialog;
class BackgroundNeutralizationDialog;
class WavescaleHDRDialog;
class HistogramStretchDialog;
class ArcsinhStretchDialog;
class SCNRDialog;
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
class HeaderPanel;
class AstroSpikeDialog;
class DebayerDialog;
class ContinuumSubtractionDialog;
class AnnotationToolDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    
    // Public API for Dialogs
    ImageViewer* currentViewer() const;
    bool hasImage() const;
    void startLongProcess();
    void endLongProcess();
    void createNewImageWindow(const ImageBuffer& buffer, const QString& title, ImageBuffer::DisplayMode mode = ImageBuffer::Display_Linear);
    
    // Helper to check if tool is already open and activate it
    bool activateTool(const QString& title);
    
    ImageBuffer::DisplayMode displayMode() const { return m_displayMode; }
    bool displayLinked() const { return m_displayLinked; }

private slots:
    void undo();
    void redo();
    void pushUndo(); // Call before destructive actions

    // Existing slots...
    void openFile();
    void saveFile();
    void openStretchDialog();
    void openAbeDialog();
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
    void openImageAnnotatorDialog();
    
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
    // Settings
    void onSettingsAction();

    // Zoom Link
    void propagateViewChange(float scale, float hVal, float vVal);

    void openCurvesDialog();
    void applyCurvesPreview(const std::vector<std::vector<float>>& lut);
    void applyCurves(const SplineData& spline, const bool channels[3]);

    void updateActiveImage(); // Public wrapper to refresh viewer

private:
    void updateDisplay();
    void updateMenus(); // Enable/Disable Undo/Redo

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
    void log(const QString& msg, LogType type = Log_Info, bool autoShow = false);

protected:
    void resizeEvent(class QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    
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

    // Tool Dialog Singletons
    QPointer<class ABEDialog> m_abeDlg;
    QPointer<class BackgroundNeutralizationDialog> m_bnDlg;
    QPointer<class WavescaleHDRDialog> m_wavescaleDlg;
    QPointer<class HistogramStretchDialog> m_histoDlg;
    QPointer<class ArcsinhStretchDialog> m_arcsinhDlg;
    QPointer<class SCNRDialog> m_scnrDlg;
    QPointer<class PixelMathDialog> m_pixelMathDialog;
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
    QPointer<class AnnotationToolDialog> m_annotatorDlg;
    
    void showConsoleTemporarily(int durationMs = 3000);
    // Move these to public above
    // void startLongProcess();
    // void endLongProcess();

    // UI Elements
    class QComboBox* m_stretchCombo;
    class QToolButton* m_linkChannelsBtn;
    class QToolButton* m_invertBtn;
    class QToolButton* m_falseColorBtn;
    class QAction* m_linkViewsAction;
    
    void setupToolSubwindow(CustomMdiSubWindow* sub, QWidget* dlg, const QString& title);
    QPointer<ImageViewer> m_lastActiveImageViewer;
    QPointer<ImageViewer> m_curvesTarget;
    QPointer<ImageViewer> m_ghsTarget;
    QPointer<ImageViewer> m_satTarget; // Track Saturation Target
    QPointer<QWidget> m_activeInteractiveTool; // Tracks presently exclusive tool (ABE vs BN)

    // Tool State Persistence
    QMap<ImageViewer*, GHSDialog::State> m_ghsStates;
    QMap<ImageViewer*, CurvesDialog::State> m_curvesStates;
    QMap<ImageViewer*, SaturationDialog::State> m_satStates;

    // Sidebar
    SidebarWidget* m_sidebar = nullptr;
    HeaderPanel* m_headerPanel = nullptr;
    
    // Animations
    bool m_isClosing = false;
    class QPropertyAnimation* m_anim = nullptr;
    void startFadeIn();
    void startFadeOut();

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    bool m_isUpdating = false;
};
 #endif // MAINWINDOW_H
