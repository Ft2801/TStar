#ifndef CONTINUUM_SUBTRACTION_DIALOG_H
#define CONTINUUM_SUBTRACTION_DIALOG_H

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QGroupBox>
#include <QThread>
#include <QVBoxLayout>
#include "DialogBase.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"

class ImageViewer;
class MainWindowCallbacks;

// ============================================================================
// Processing worker thread — runs full pipeline off the main thread
// ============================================================================
class ContinuumSubtractWorker : public QThread {
    Q_OBJECT
public:
    struct Task {
        QString name;              // e.g. "Ha", "SII", "OIII"
        ImageBuffer nbStarry;      // Narrowband (starry)
        ImageBuffer contStarry;    // Continuum   (starry)
        ImageBuffer nbStarless;    // Narrowband (starless) — may be invalid
        ImageBuffer contStarless;  // Continuum   (starless) — may be invalid
        bool starlessOnly = false; // True if only starless pair is loaded
    };

    ContinuumSubtractWorker(const std::vector<Task>& tasks,
                            const ContinuumSubtractParams& params,
                            bool denoiseWithCC,
                            const QString& cosmicClarityPath,
                            QObject* parent = nullptr);

signals:
    void resultReady(const QString& name, const ImageBuffer& result);
    void statusUpdate(const QString& message);
    void allDone();

protected:
    void run() override;

private:
    std::vector<Task> m_tasks;
    ContinuumSubtractParams m_params;
    bool m_denoiseCC;
    QString m_ccPath;
};

// ============================================================================
// Dialog — multi-image continuum subtraction UI
// ============================================================================
class ContinuumSubtractionDialog : public DialogBase {
    Q_OBJECT
public:
    explicit ContinuumSubtractionDialog(QWidget* parent = nullptr);
    ~ContinuumSubtractionDialog();

    void setViewer(ImageViewer* v);
    void refreshImageList();

private slots:
    void onExecute();
    void onClear();
    void onQFactorChanged(double val);
    void onResultReady(const QString& name, const ImageBuffer& result);
    void onWorkerStatus(const QString& msg);
    void onWorkerDone();

private:
    // Load image from open views or file
    void loadImage(const QString& channel);
    void populateCombo(QComboBox* combo);

    // Image slots for NB filters (starry + starless)
    struct ImageSlot {
        ImageBuffer buffer;
        QLabel*     label = nullptr;
        QPushButton* button = nullptr;
        bool loaded() const { return buffer.isValid(); }
        void clear() { buffer = ImageBuffer(); if (label) label->setText("—"); }
    };

    // --- Narrowband filter slots ---
    ImageSlot m_haStarry,   m_haStarless;
    ImageSlot m_siiStarry,  m_siiStarless;
    ImageSlot m_oiiiStarry, m_oiiiStarless;

    // --- Composite slots (HaO3, S2O3) ---
    ImageSlot m_hao3Starry,  m_hao3Starless;
    ImageSlot m_s2o3Starry,  m_s2o3Starless;

    // --- Continuum source slots ---
    ImageSlot m_redStarry,   m_redStarless;
    ImageSlot m_greenStarry, m_greenStarless;
    ImageSlot m_oscStarry,   m_oscStarless;

    // Helper: resolve continuum for a given NB filter
    // Ha/SII → Red or OSC R-channel; OIII → Green or OSC G-channel
    bool getContinuumForFilter(const QString& filter, bool starless,
                               ImageBuffer& outCont);

    // Handle HaO3/S2O3 composite extraction
    void extractFromComposite(const QString& composite, bool starless, const ImageBuffer& img);

    // UI widgets
    MainWindowCallbacks* m_mainWindow = nullptr;
    ImageViewer* m_viewer = nullptr;

    // Parameters
    QDoubleSpinBox* m_qFactorSpin;
    QSlider*        m_qFactorSlider;
    QCheckBox*      m_outputLinearCheck;
    QCheckBox*      m_denoiseCheck;

    // Settings
    QDoubleSpinBox* m_thresholdSpin;
    QDoubleSpinBox* m_curvesSpin;

    // Status
    QLabel*       m_statusLabel;
    QProgressBar* m_progress;
    QPushButton*  m_executeBtn;

    // Worker
    ContinuumSubtractWorker* m_worker = nullptr;

    // Helper: create a Load button + label pair, wired to loadImage()
    void createSlotUI(QVBoxLayout* layout, ImageSlot& slot,
                      const QString& label, const QString& channel);
};

#endif // CONTINUUM_SUBTRACTION_DIALOG_H
