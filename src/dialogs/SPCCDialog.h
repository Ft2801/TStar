#pragma once

#ifndef SPCCDIALOG_H
#define SPCCDIALOG_H

/*
 * SPCCDialog.h  —  Spectrophotometric Color Calibration dialog
 *
 * Workflow exposed to the user:
 *   Step 1  — "Fetch Stars": plate-solve the active image, query SIMBAD for
 *             in-field stars, build StarRecord list with Pickles SED matches.
 *             Optionally run the Gaia XP fallback for stars lacking a match.
 *   Step 2  — "Run Calibration": launch SPCC::calibrateWithStarList() on a
 *             background thread; display results and apply to the viewer.
 *   Reset   — restore the original unmodified buffer.
 *
 * Equipment combos (white reference, R/G/B filters, sensor QE, LP filters)
 * are populated dynamically from tstar_data.fits via SPCC::loadTStarFits().
 * Selections are persisted across sessions with QSettings.
 */

#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QProgressBar>
#include <QTableWidget>
#include <QFutureWatcher>
#include <QSettings>

#include "ImageBuffer.h"
#include "SPCC.h"
#include "../photometry/CatalogClient.h"


// Forward declarations
class ImageViewer;
class MainWindow;
class QProgressDialog;

// ─────────────────────────────────────────────────────────────────────────────
// SPCCDialog
// ─────────────────────────────────────────────────────────────────────────────
class SPCCDialog : public QDialog {
    Q_OBJECT

public:
    explicit SPCCDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent = nullptr);
    ~SPCCDialog() override;

    /// Called when the active document changes externally.
    void setViewer(ImageViewer* viewer);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // ── Step 1: star fetching ─────────────────────────────────────────────────
    void onFetchStars();
    void onFetchStarsFinished();
    void onCatalogReady(const std::vector<CatalogStar>& stars);
    void onCatalogError(const QString& err);

    // ── Step 2: calibration ───────────────────────────────────────────────────
    void onRun();
    void onCalibrationFinished();

    // ── Controls ──────────────────────────────────────────────────────────────
    void onReset();
    void onOpenSaspViewer();
    void onCancel();

    // ── Settings persistence ──────────────────────────────────────────────────
    void saveWhiteRefSetting(int);
    void saveRFilterSetting(int);
    void saveGFilterSetting(int);
    void saveBFilterSetting(int);
    void saveSensorSetting(int);
    void saveLpFilter1Setting(int);
    void saveLpFilter2Setting(int);
    void saveGradMethodSetting(const QString&);
    void saveSepThreshSetting(double);
    void saveFullMatrixSetting(bool);
    void saveLinearModeSetting(bool);

private:
    // ── UI construction ───────────────────────────────────────────────────────
    void buildUI();
    void connectSignals();
    void populateCombosFromFits();
    void restoreSettings();
    void showResults(const SPCCResult& r);
    void setControlsEnabled(bool en);

    // ── Calibration launch ────────────────────────────────────────────────────
    void startCalibration();
    SPCCParams collectParams() const;

    // ── Cleanup ───────────────────────────────────────────────────────────────
    void cleanup();

    // ─────────────────────────────────────────────────────────────────────────
    // UI elements — Equipment group
    // ─────────────────────────────────────────────────────────────────────────
    QComboBox*      m_whiteRefCombo    = nullptr;   ///< SED / white reference
    QComboBox*      m_rFilterCombo     = nullptr;
    QComboBox*      m_gFilterCombo     = nullptr;
    QComboBox*      m_bFilterCombo     = nullptr;
    QComboBox*      m_sensorCombo      = nullptr;
    QComboBox*      m_lpFilter1Combo   = nullptr;
    QComboBox*      m_lpFilter2Combo   = nullptr;

    // ─────────────────────────────────────────────────────────────────────────
    // UI elements — Options group
    // ─────────────────────────────────────────────────────────────────────────
    QDoubleSpinBox* m_sepThreshSpin    = nullptr;   ///< SEP detection sigma
    QComboBox*      m_bgMethodCombo    = nullptr;   ///< Background subtraction method
    QComboBox*      m_gradMethodCombo  = nullptr;   ///< Gradient surface method
    QCheckBox*      m_fullMatrixCheck  = nullptr;
    QCheckBox*      m_linearModeCheck  = nullptr;
    QCheckBox*      m_runGradientCheck = nullptr;   ///< Enable gradient extraction

    // ─────────────────────────────────────────────────────────────────────────
    // UI elements — Results group
    // ─────────────────────────────────────────────────────────────────────────
    QLabel*         m_starsLabel       = nullptr;   ///< "Matched stars: N"
    QLabel*         m_residualLabel    = nullptr;   ///< "RMS error: X.XXXX"
    QLabel*         m_scalesLabel      = nullptr;   ///< "Scale factors: R, G, B"
    QLabel*         m_modelLabel       = nullptr;   ///< "Model: affine"
    QTableWidget*   m_matrixTable      = nullptr;   ///< 3x3 correction matrix

    // ─────────────────────────────────────────────────────────────────────────
    // UI elements — Status / progress
    // ─────────────────────────────────────────────────────────────────────────
    QProgressBar*   m_progressBar      = nullptr;
    QLabel*         m_statusLabel      = nullptr;

    // ─────────────────────────────────────────────────────────────────────────
    // UI elements — Buttons
    // ─────────────────────────────────────────────────────────────────────────
    QPushButton*    m_fetchStarsBtn    = nullptr;   ///< Step 1
    QPushButton*    m_runBtn           = nullptr;   ///< Step 2
    QPushButton*    m_resetBtn         = nullptr;
    QPushButton*    m_cancelBtn        = nullptr;
    QPushButton*    m_saspViewerBtn    = nullptr;
    QPushButton*    m_closeBtn         = nullptr;

    // ─────────────────────────────────────────────────────────────────────────
    // State
    // ─────────────────────────────────────────────────────────────────────────
    ImageViewer*    m_viewer           = nullptr;
    MainWindow*     m_mainWindow       = nullptr;
    ImageBuffer     m_originalBuffer;               ///< Snapshot for Reset

    // In-memory spectral database (loaded once from tstar_data.fits)
    SPCCDataStore   m_store;
    bool            m_storeLoaded      = false;

    // Star list built by onFetchStars(), consumed by startCalibration()
    std::vector<StarRecord> m_starList;

    // Background workers
    QFutureWatcher<std::vector<StarRecord>>* m_fetchWatcher = nullptr;
    QFutureWatcher<SPCCResult>*              m_calibWatcher = nullptr;
    CatalogClient*                           m_catalog = nullptr;
    std::atomic<bool>                        m_cancelFlag{false};

    // Data path (resolved at construction from MainWindow or application dir)
    QString         m_dataPath;

    // QSettings group key
    static constexpr const char* kSettingsGroup = "SPCC";
};

#endif // SPCCDIALOG_H