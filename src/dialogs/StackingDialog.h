
#ifndef STACKING_DIALOG_H
#define STACKING_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QTabWidget>
#include <memory> 
#include "../widgets/SimplePlotWidget.h"

#include "../stacking/StackingTypes.h"
#include "../stacking/StackingSequence.h"
#include "../stacking/StackingEngine.h"

class MainWindow;
class ImageViewer;

/**
 * @brief Main stacking dialog
 * 
 * Provides interface for:
 * - Loading image sequences
 * - Selecting/filtering images
 * - Configuring stacking parameters
 * - Executing stacking operation
 * - Viewing results
 */
class StackingDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit StackingDialog(MainWindow* parent = nullptr);
    ~StackingDialog() override;
    
    /**
     * @brief Set the image sequence
     */
    void setSequence(std::unique_ptr<Stacking::ImageSequence> sequence);
    
    /**
     * @brief Get the stacking result
     */
    ImageBuffer* result() { return m_result.get(); }
    
signals:
    /**
     * @brief Emitted when stacking completes with result
     */
    void stackingComplete(ImageBuffer* result);
    
private slots:
    // UI slots
    void onLoadSequence();
    void onAddFiles();
    void onRemoveSelected();
    void onSelectAll();
    void onDeselectAll();
    void onSetReference();
    
    void onMethodChanged(int index);
    void onRejectionChanged(int index);
    void onNormalizationChanged(int index);
    
    void onStartStacking();
    void onCancel();
    
    // Progress slots
    void onProgressChanged(const QString& message, double progress);
    void onLogMessage(const QString& message, const QString& color);
    void onStackingFinished(bool success);
    
    // Table slots
    void onTableSelectionChanged();
    void onTableItemDoubleClicked(int row, int column);
    
    // Comet slots
    void onPickCometFirst();
    void onPickCometLast();
    void onComputeCometShifts();
    void onViewerPointPicked(QPointF p);
    
private:
    void setupUI();
    void setupSequenceGroup();
    void setupPlotTab();
    void setupCometTab();
    void setupParametersGroup();
    void setupOutputGroup();
    void setupProgressGroup();
    
    void updateTable();
    void updateParameterVisibility();
    void updateSummary();
    
    Stacking::StackingParams gatherParams() const;
    QString generateOutputFilename() const;
    
    // UI Components - Sequence
    QGroupBox* m_sequenceGroup;
    QTableWidget* m_imageTable;
    QPushButton* m_loadBtn;
    QPushButton* m_addBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_selectAllBtn;
    QPushButton* m_deselectAllBtn;
    QPushButton* m_setRefBtn;
    QLabel* m_sequenceSummary;
    
    // UI Components - Filtering
    QComboBox* m_filterCombo;
    QDoubleSpinBox* m_filterValue;
    
    // UI Components - Parameters
    QGroupBox* m_paramsGroup;
    QComboBox* m_methodCombo;
    QComboBox* m_rejectionCombo;
    QDoubleSpinBox* m_sigmaLow;
    QDoubleSpinBox* m_sigmaHigh;
    QComboBox* m_normCombo;
    QComboBox* m_weightingCombo;
    QSpinBox* m_featherSpin;
    
    // UI Components - Options
    QCheckBox* m_force32BitCheck;
    QCheckBox* m_outputNormCheck;
    QCheckBox* m_equalizeRGBCheck;
    QCheckBox* m_maximizeFramingCheck;
    QCheckBox* m_createRejMapsCheck;

    // Debayer Options
    QCheckBox* m_debayerCheck;
    QComboBox* m_bayerPatternCombo;
    QComboBox* m_debayerAlgoCombo;
    
    // Drizzle Options
    QCheckBox* m_drizzleCheck;
    QDoubleSpinBox* m_drizzleScale;
    QDoubleSpinBox* m_drizzlePixFrac;

    // Plotting
    QTabWidget* m_tabWidget;
    QWidget* m_plotTab;
    SimplePlotWidget* m_plotWidget;
    QComboBox* m_plotTypeCombo;
    void updatePlot();
    void onPlotTypeChanged(int index);

    // Comet Tab
    QWidget* m_cometTab;
    QLabel* m_cometStatusLabel;
    QPushButton* m_pickCometFirstBtn;
    QPushButton* m_pickCometLastBtn;
    QPushButton* m_computeCometBtn;
    int m_cometRef1Index = -1;
    int m_cometRef2Index = -1;
    
    // UI Components - Output
    QGroupBox* m_outputGroup;
    QLineEdit* m_outputPath;
    QPushButton* m_browseBtn;
    
    // UI Components - Progress
    QGroupBox* m_progressGroup;
    QProgressBar* m_progressBar;
    QTextEdit* m_logText;
    QPushButton* m_startBtn;
    QPushButton* m_cancelBtn;
    
    // Data
    std::unique_ptr<Stacking::ImageSequence> m_sequence;
    std::unique_ptr<Stacking::StackingWorker> m_worker;
    std::unique_ptr<ImageBuffer> m_result;
    MainWindow* m_mainWindow;
    bool m_isRunning = false;
};

#endif // STACKING_DIALOG_H
