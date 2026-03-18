#pragma once
/*
 * SPCCDialog.h  —  Spectrophotometric Color Calibration dialog (Qt6)
 */

#ifndef SPCCDIALOG_H
#define SPCCDIALOG_H

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
#include "ImageBuffer.h"
#include "SPCC.h"
#include "photometry/CatalogClient.h"

class ImageViewer;
class MainWindow;
class QProgressDialog;

class SPCCDialog : public QDialog {
    Q_OBJECT

public:
    explicit SPCCDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent = nullptr);
    ~SPCCDialog() override;

    void setViewer(ImageViewer* viewer);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onRun();
    void onReset();
    void onFinished();
    void onCatalogReady(const std::vector<CatalogStar>& stars);
    void onCatalogError(const QString& msg);
    void onCameraChanged(const QString& name);

private:
    void buildUI();
    void connectSignals();
    SPCCParams collectParams() const;
    void populateProfiles();
    void showResults(const SPCCResult& res);
    void setControlsEnabled(bool en);
    void updateColourPreview(double r, double g, double b);
    void startCalibration();

    // ── Object Type ────────────────────────────────────────────────────────
    QComboBox*      m_objectTypeCombo  = nullptr;
    
    // ── Camera / filter ────────────────────────────────────────────────────
    QComboBox*      m_cameraCombo      = nullptr;
    QComboBox*      m_filterCombo      = nullptr;

    // ── Detection ─────────────────────────────────────────────────────────
    QDoubleSpinBox* m_minSNRSpin       = nullptr;
    QSpinBox*       m_maxStarsSpin     = nullptr;
    QDoubleSpinBox* m_apertureSpin     = nullptr;
    QDoubleSpinBox* m_magLimitSpin     = nullptr;
    QCheckBox*      m_limitMagCheck    = nullptr;

    // ── Options ────────────────────────────────────────────────────────────
    QCheckBox*      m_fullMatrixCheck  = nullptr;
    QCheckBox*      m_solarRefCheck    = nullptr;
    QCheckBox*      m_neutralBgCheck   = nullptr;

    // ── Results ────────────────────────────────────────────────────────────
    QLabel*         m_starsLabel       = nullptr;
    QLabel*         m_residualLabel    = nullptr;
    QLabel*         m_scalesLabel      = nullptr;
    QLabel*         m_colourSwatch     = nullptr;   // visual white-balance indicator
    QTableWidget*   m_matrixTable      = nullptr;   // 3×3 correction matrix display

    // ── Progress ───────────────────────────────────────────────────────────
    QProgressBar*   m_progressBar      = nullptr;
    QLabel*         m_statusLabel      = nullptr;

    // ── Buttons ────────────────────────────────────────────────────────────
    QPushButton*    m_runBtn           = nullptr;
    QPushButton*    m_resetBtn         = nullptr;
    QPushButton*    m_closeBtn         = nullptr;

    // ── State ──────────────────────────────────────────────────────────────
    ImageViewer*    m_viewer           = nullptr;
    MainWindow*     m_mainWindow       = nullptr;
    ImageBuffer     m_originalBuffer;

    CatalogClient*  m_catalog          = nullptr;
    std::vector<CatalogStar> m_catalogStars;
    bool            m_useOnlineCatalog = false;

    QFutureWatcher<SPCCResult>* m_watcher = nullptr;
    QString         m_dataPath;
    QProgressDialog* m_busyDialog = nullptr;
};

#endif // SPCCDIALOG_H
