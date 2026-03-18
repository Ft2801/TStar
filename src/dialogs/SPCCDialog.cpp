/*
 * SPCCDialog.cpp  —  Spectrophotometric Color Calibration dialog (Qt6)
 */

#include "SPCCDialog.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "core/Logger.h"
#include "SPCC.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QPushButton>
#include <QProgressBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFrame>
#include <QPainter>
#include <QProgressDialog>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

// ─── Constructor ──────────────────────────────────────────────────────────────

SPCCDialog::SPCCDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent)
    : QDialog(parent), m_viewer(viewer), m_mainWindow(mw)
{
    setWindowTitle(tr("Spectrophotometric Color Calibration (SPCC)"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setMinimumWidth(960); // Doubled width to accommodate two columns

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/data/spcc",
        appDir + "/../Resources/data/spcc",
        appDir + "/../data/spcc",
        appDir + "/data",
        appDir + "/../Resources/data",
        appDir + "/../data"
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c + "/pickles_spectra.bin") || QFileInfo::exists(c + "/filter_responses.json")) {
            m_dataPath = c;
            break;
        }
    }
    if (m_dataPath.isEmpty()) {
        m_dataPath = appDir + "/data/spcc";
    }

    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();

    m_watcher = new QFutureWatcher<SPCCResult>(this);
    m_catalog = new CatalogClient(this);

    buildUI();
    connectSignals();
    populateProfiles();
}

SPCCDialog::~SPCCDialog() {
    if (m_watcher->isRunning()) m_watcher->cancel();
}

void SPCCDialog::setViewer(ImageViewer* viewer)
{
    m_viewer = viewer;
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer();
    }
}

// ─── Build UI ────────────────────────────────────────────────────────────────

void SPCCDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(12, 12, 12, 12);

    // Create scrollable area for options
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    auto* scrollWidget = new QWidget;
    auto* scrollLayout = new QVBoxLayout(scrollWidget);
    
    // Create horizontal layout for the two columns inside scroll area
    auto* hCols = new QHBoxLayout;
    auto* vLeft = new QVBoxLayout;
    auto* vRight = new QVBoxLayout;

    // ── LEFT COLUMN ───────────────────────────────────────────────────────

    // ── Object Type ───────────────────────────────────────────────────────
    auto* grpObj = new QGroupBox(tr("Object Type"), this);
    auto* frmObj = new QFormLayout(grpObj);
    m_objectTypeCombo = new QComboBox(this);
    m_objectTypeCombo->addItems({tr("Star Field"), tr("Dark Nebula"), tr("Emission Nebula"), 
                                 tr("Planetary Nebula"), tr("Galaxy"), tr("Custom")});
    m_objectTypeCombo->setCurrentIndex(0);
    m_objectTypeCombo->setToolTip(tr("Select the object type to optimize color calibration"));
    frmObj->addRow(tr("Object Type:"), m_objectTypeCombo);
    vLeft->addWidget(grpObj);

    // ── Sensor / Filter ───────────────────────────────────────────────────
    auto* grpCam = new QGroupBox(tr("Sensor & Filter Profile"), this);
    auto* frmCam = new QFormLayout(grpCam);

    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setToolTip(tr("Select the spectral response profile of your camera sensor.\n"
                                 "These profiles describe how each channel responds to different wavelengths."));
    frmCam->addRow(tr("Camera / Sensor:"), m_cameraCombo);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->setToolTip(tr("Optional optical filter in the optical path (L, UV/IR cut, etc.)"));
    frmCam->addRow(tr("Filter:"), m_filterCombo);

    vLeft->addWidget(grpCam);

    // ── Star detection ────────────────────────────────────────────────────
    auto* grpDet = new QGroupBox(tr("Star Detection & Cross-Match"), this);
    auto* frmDet = new QFormLayout(grpDet);

    m_minSNRSpin = new QDoubleSpinBox(this);
    m_minSNRSpin->setRange(5.0, 200.0); m_minSNRSpin->setSingleStep(5.0);
    m_minSNRSpin->setValue(20.0);
    m_minSNRSpin->setToolTip(tr("Minimum signal-to-noise ratio for a star to be used."));
    frmDet->addRow(tr("Minimum SNR:"), m_minSNRSpin);

    m_maxStarsSpin = new QSpinBox(this);
    m_maxStarsSpin->setRange(10, 500); m_maxStarsSpin->setSingleStep(10);
    m_maxStarsSpin->setValue(200);
    frmDet->addRow(tr("Max stars:"), m_maxStarsSpin);

    m_apertureSpin = new QDoubleSpinBox(this);
    m_apertureSpin->setRange(1.0, 20.0); m_apertureSpin->setSingleStep(0.5);
    m_apertureSpin->setValue(4.0); m_apertureSpin->setSuffix(" px");
    frmDet->addRow(tr("Aperture radius:"), m_apertureSpin);

    auto* hMag = new QHBoxLayout;
    m_limitMagCheck = new QCheckBox(tr("Limit to mag <"), this);
    m_limitMagCheck->setChecked(true);
    m_magLimitSpin = new QDoubleSpinBox(this);
    m_magLimitSpin->setRange(8.0, 20.0); m_magLimitSpin->setSingleStep(0.5);
    m_magLimitSpin->setValue(13.5);
    hMag->addWidget(m_limitMagCheck);
    hMag->addWidget(m_magLimitSpin);
    hMag->addStretch();
    frmDet->addRow(tr("Magnitude limit:"), hMag);

    vLeft->addWidget(grpDet);

    // ── Options ───────────────────────────────────────────────────────────
    auto* grpOpts = new QGroupBox(tr("Calibration Options"), this);
    auto* vOpts   = new QVBoxLayout(grpOpts);
    m_fullMatrixCheck = new QCheckBox(tr("Use full 3×3 colour matrix (vs. diagonal)"), this);
    m_fullMatrixCheck->setChecked(false);
    m_fullMatrixCheck->setToolTip(tr("Full 3×3 corrects cross-channel colour mixing; "
                                     "diagonal only rescales each channel independently.\n"
                                     "Use full matrix only if your sensor has significant channel crosstalk."));
    m_solarRefCheck = new QCheckBox(tr("Normalise to solar (G2V) white point"), this);
    m_solarRefCheck->setChecked(true);
    m_neutralBgCheck = new QCheckBox(tr("Subtract background before calibration"), this);
    m_neutralBgCheck->setChecked(true);
    vOpts->addWidget(m_fullMatrixCheck);
    vOpts->addWidget(m_solarRefCheck);
    vOpts->addWidget(m_neutralBgCheck);
    
    vLeft->addWidget(grpOpts);
    vLeft->addStretch(); // Push everything in the left column to the top

    // ── RIGHT COLUMN ──────────────────────────────────────────────────────

    // ── Results ───────────────────────────────────────────────────────────
    auto* grpRes = new QGroupBox(tr("Calibration Results"), this);
    auto* vRes   = new QVBoxLayout(grpRes);

    auto* hInfo = new QHBoxLayout;
    m_starsLabel    = new QLabel(tr("Stars used: —"), this);
    m_residualLabel = new QLabel(tr("Residual: —"),    this);
    m_scalesLabel   = new QLabel(tr("R/G/B scales: —"), this);
    hInfo->addWidget(m_starsLabel);
    hInfo->addWidget(m_residualLabel);
    vRes->addLayout(hInfo);
    vRes->addWidget(m_scalesLabel);

    // Colour swatch (shows resulting white balance)
    auto* hSwatch = new QHBoxLayout;
    hSwatch->addWidget(new QLabel(tr("White balance:"), this));
    m_colourSwatch = new QLabel(this);
    m_colourSwatch->setFixedSize(120, 22);
    m_colourSwatch->setFrameShape(QFrame::StyledPanel);
    hSwatch->addWidget(m_colourSwatch);
    hSwatch->addStretch();
    vRes->addLayout(hSwatch);

    // 3×3 matrix display
    m_matrixTable = new QTableWidget(3, 3, this);
    m_matrixTable->setHorizontalHeaderLabels({tr("R_in"), tr("G_in"), tr("B_in")});
    m_matrixTable->setVerticalHeaderLabels({tr("R_out"), tr("G_out"), tr("B_out")});
    m_matrixTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_matrixTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_matrixTable->setMaximumHeight(120);
    m_matrixTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            auto* it = new QTableWidgetItem(i==j ? "1.0000" : "0.0000");
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_matrixTable->setItem(i, j, it);
        }
    vRes->addWidget(m_matrixTable);
    
    vRight->addWidget(grpRes);
    vRight->addStretch(); // Push everything in the right column to the top

    // Add columns to the scroll layout
    hCols->addLayout(vLeft);
    hCols->addLayout(vRight);
    scrollLayout->addLayout(hCols);
    scrollLayout->addStretch();
    
    scrollArea->setWidget(scrollWidget);
    root->addWidget(scrollArea);

    // ── BOTTOM SECTION (Progress & Buttons) ───────────────────────────────

    // ── Progress ──────────────────────────────────────────────────────────
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_progressBar);
    root->addWidget(m_statusLabel);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* hBtns = new QHBoxLayout;
    m_resetBtn = new QPushButton(tr("Reset"), this);
    m_runBtn   = new QPushButton(tr("Run SPCC"), this);
    m_closeBtn = new QPushButton(tr("Close"), this);
    m_runBtn->setDefault(true);
    hBtns->addWidget(m_resetBtn);
    hBtns->addStretch();
    hBtns->addWidget(m_runBtn);
    hBtns->addWidget(m_closeBtn);
    root->addLayout(hBtns);
}

// ─── Populate camera/filter combos ───────────────────────────────────────────

void SPCCDialog::populateProfiles()
{
    m_cameraCombo->clear();
    m_filterCombo->clear();

    QStringList cameras = SPCC::availableCameraProfiles(m_dataPath);
    if (cameras.isEmpty()) {
        cameras << "Generic DSLR" << "Generic Mono" << "Canon EOS Ra"
                << "Nikon Z6 II" << "Sony A7S III" << "ZWO ASI2600MC"
                << "ZWO ASI6200MM";
    }
    m_cameraCombo->addItems(cameras);

    QStringList filters = SPCC::availableFilterProfiles(m_dataPath);
    if (!filters.contains("Luminance")) filters.prepend("Luminance");
    if (!filters.contains("No Filter")) filters.append("No Filter");
    if (filters.size() <= 2) {
        filters << "UV/IR Cut" << "Baader L-Booster" << "Optolong L-Pro" << "Astronomik L-2";
    }
    m_filterCombo->addItems(filters);

    const int noFilterIdx = m_filterCombo->findText("No Filter");
    if (noFilterIdx >= 0) {
        m_filterCombo->setCurrentIndex(noFilterIdx);
    }
}

// ─── Connect signals ──────────────────────────────────────────────────────────

void SPCCDialog::connectSignals()
{
    connect(m_runBtn,   &QPushButton::clicked, this, &SPCCDialog::onRun);
    connect(m_resetBtn, &QPushButton::clicked, this, &SPCCDialog::onReset);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_watcher,  &QFutureWatcher<SPCCResult>::finished, this, &SPCCDialog::onFinished);
    connect(m_cameraCombo, &QComboBox::currentTextChanged, this, &SPCCDialog::onCameraChanged);
    
    // Catalog connections (online Gaia queries)
    connect(m_catalog, &CatalogClient::catalogReady, this, &SPCCDialog::onCatalogReady);
    connect(m_catalog, &CatalogClient::errorOccurred, this, &SPCCDialog::onCatalogError);
}

// ─── Collect params ───────────────────────────────────────────────────────────

SPCCParams SPCCDialog::collectParams() const
{
    SPCCParams p;
    p.cameraProfile     = m_cameraCombo->currentText();
    p.filterProfile     = m_filterCombo->currentText();
    p.useFullMatrix     = m_fullMatrixCheck->isChecked();
    p.solarReference    = m_solarRefCheck->isChecked();
    p.neutralBackground = m_neutralBgCheck->isChecked();
    p.minSNR            = m_minSNRSpin->value();
    p.maxStars          = m_maxStarsSpin->value();
    p.apertureR         = m_apertureSpin->value();
    p.limitMagnitude    = m_limitMagCheck->isChecked();
    p.magLimit          = m_magLimitSpin->value();
    p.dataPath          = m_dataPath;
    return p;
}

// ─── Run slot ─────────────────────────────────────────────────────────────────

void SPCCDialog::onRun()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    if (m_watcher->isRunning()) return;

    // Check if image is plate-solved
    const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();
    if (meta.ra == 0 && meta.dec == 0) {
        QMessageBox::critical(this, tr("Error"), tr("Image must be plate solved first."));
        return;
    }

    // Validate data directory
    if (m_dataPath.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Data directory not configured."));
        return;
    }

    // NOTE: Pickles spectral library is optional — we use Gaia XP-sampled spectra instead.
    // If embedded catalog exists, load it; otherwise query Gaia DR3 online (like PCC does).
    
    // Check if embedded Gaia catalogue file exists
    bool hasEmbeddedCatalogue = QFileInfo::exists(m_dataPath + "/spcc/gaia_bv_catalogue.bin");
    
    if (hasEmbeddedCatalogue) {
        // Use embedded catalogue directly
        m_useOnlineCatalog = false;
        m_catalogStars.clear();
        Logger::info("[SPCC] Using embedded Gaia catalog...");
        m_statusLabel->setText(tr("Using embedded Gaia catalog..."));
        startCalibration();
    } else {
        // Fall back to online Gaia DR3 query (like PCC does)
        m_useOnlineCatalog = true;
        Logger::info(QString("[SPCC] Querying Gaia DR3 online: RA=%1 Dec=%2").arg(meta.ra).arg(meta.dec));
        m_statusLabel->setText(tr("Embedded catalog not found. Querying Gaia DR3 via VizieR (this may take a moment)..."));
        m_runBtn->setEnabled(false);
        m_catalog->queryGaiaDR3(meta.ra, meta.dec, 1.0);
    }
}

void SPCCDialog::startCalibration()
{
    if (m_watcher->isRunning()) return;

    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_statusLabel->setText(tr("Running SPCC…"));

    Logger::info(QString("[SPCC] Starting calibration with %1 stars").arg(m_catalogStars.size()));

    if (!m_busyDialog) {
        m_busyDialog = new QProgressDialog(tr("Running SPCC..."), QString(), 0, 0, this);
        m_busyDialog->setWindowTitle(tr("Spectrophotometric Color Calibration"));
        m_busyDialog->setWindowModality(Qt::WindowModal);
        m_busyDialog->setCancelButton(nullptr);
        m_busyDialog->setMinimumDuration(0);
    }
    m_busyDialog->setLabelText(tr("Matching stars and solving color matrix..."));
    m_busyDialog->show();

    auto buf    = std::make_shared<ImageBuffer>(m_viewer->getBuffer());
    auto params = collectParams();
    auto stars  = m_catalogStars;  // Copy catalogue stars for online mode

    auto* raw = new std::shared_ptr<ImageBuffer>(buf);
    m_watcher->setProperty("bufPtr", QVariant::fromValue(static_cast<void*>(raw)));

    const bool useOnlineCatalog = m_useOnlineCatalog;
    QFuture<SPCCResult> fut = QtConcurrent::run([buf, params, stars, useOnlineCatalog]() {
        // Call the real SPCC algorithm with Gaia DR3 catalog stars
        SPCCResult result = SPCC::calibrateWithCatalog(*buf, params, stars);
        if (result.success) {
            Logger::info(result.log_msg);
        } else {
            Logger::warning(QString("[SPCC] Calibration failed: %1").arg(result.error_msg));
        }
        return result;
    });
    m_watcher->setFuture(fut);
}

void SPCCDialog::onCatalogReady(const std::vector<CatalogStar>& stars)
{
    m_catalogStars = stars;
    if (stars.empty()) {
        m_statusLabel->setText(tr("WARNING: Gaia query returned 0 stars. Check image coordinates."));
        QMessageBox::warning(this, tr("Empty Catalog"), 
            tr("Gaia DR3 query returned no stars. The image location may have no reference stars.\n\n"
               "Try:\n- Verify plate-solving is correct (RA/Dec)\n- Check magnitude limits\n- Use embedded catalog if available"));
        setControlsEnabled(true);
        m_runBtn->setEnabled(true);
    } else {
        Logger::info(QString("[SPCC] Catalog loaded: %1 stars").arg(stars.size()));
        m_statusLabel->setText(tr("Catalog loaded (%1 stars). Running Calibration...").arg(stars.size()));
        startCalibration();
    }
}

void SPCCDialog::onCatalogError(const QString& msg)
{
    setControlsEnabled(true);
    m_runBtn->setEnabled(true);
    
    Logger::warning(QString("[SPCC] Catalog error: %1").arg(msg));
    
    // Provide actionable error messages
    QString userMsg = msg;
    if (msg.toLower().contains("timeout")) {
        userMsg = tr("Gaia download timeout. Check your internet connection.\n\n"
                    "You can use the embedded catalog if available, or retry with a stable connection.");
    } else if (msg.toLower().contains("all vizier mirrors failed")) {
        userMsg = tr("Could not reach any VizieR mirror server.\n\n"
                    "Possible causes:\n"
                    "- No internet connection\n"
                    "- Firewall/proxy blocking\n"
                    "- All mirror servers temporarily down\n\n"
                    "Try again later, or use embedded catalog if available.");
    } else if (msg.toLower().contains("html")) {
        userMsg = tr("VizieR server returned an error page.\n\n"
                    "This may be temporary. Try:\n"
                    "1. Wait a few minutes\n"
                    "2. Check your internet connection\n"
                    "3. Use embedded catalog if available");
    }
    
    m_statusLabel->setText(tr("Catalog error: ") + msg);
    QMessageBox::critical(this, tr("Catalog Download Error"), userMsg);
}

void SPCCDialog::onFinished()
{
    m_progressBar->setVisible(false);
    setControlsEnabled(true);
    if (m_busyDialog) {
        m_busyDialog->hide();
    }

    auto* raw = static_cast<std::shared_ptr<ImageBuffer>*>(
        m_watcher->property("bufPtr").value<void*>());
    if (!raw) return;

    SPCCResult res = m_watcher->result();
    if (res.success && m_viewer && res.modifiedBuffer) {
        m_viewer->pushUndo();
        m_viewer->setBuffer(*res.modifiedBuffer, m_viewer->windowTitle(), true);
        m_viewer->refreshDisplay(true);
        showResults(res);
        if (m_mainWindow) {
            Logger::info(res.log_msg);
        }
        m_statusLabel->setText(tr("Calibration complete."));
    } else {
        m_statusLabel->setText(tr("Failed: ") + res.error_msg);
        QMessageBox::warning(this, tr("SPCC"), tr("Calibration failed:\n") + res.error_msg);
    }
    delete raw;
}

void SPCCDialog::onReset()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    m_viewer->refreshDisplay(true);
    m_statusLabel->setText(tr("Reset to original."));
}

void SPCCDialog::onCameraChanged(const QString& /*name*/) {
    // Could reload filter options per camera here
}

// ─── Show results ─────────────────────────────────────────────────────────────

void SPCCDialog::showResults(const SPCCResult& res)
{
    m_starsLabel->setText(tr("Stars used: %1 / %2")
        .arg(res.stars_used).arg(res.stars_found));
    m_residualLabel->setText(tr("RMS residual: %1")
        .arg(res.residual, 0, 'f', 5));
    m_scalesLabel->setText(tr("Scales  R=×%1   G=×%2   B=×%3")
        .arg(res.scaleR, 0, 'f', 4)
        .arg(res.scaleG, 0, 'f', 4)
        .arg(res.scaleB, 0, 'f', 4));

    // Matrix display
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m_matrixTable->item(i, j)->setText(
                QString::number(res.corrMatrix[i][j], 'f', 4));

    // Colour swatch: render a gradient representing the white-balance shift
    updateColourPreview(res.scaleR, res.scaleG, res.scaleB);
}

void SPCCDialog::updateColourPreview(double r, double g, double b)
{
    QPixmap pm(m_colourSwatch->size());
    pm.fill(Qt::transparent);
    QPainter p(&pm);

    double mx = std::max({r, g, b});
    if (mx < 1e-6) mx = 1.0;
    int ri = std::min(255, static_cast<int>(255.0 * r / mx));
    int gi = std::min(255, static_cast<int>(255.0 * g / mx));
    int bi = std::min(255, static_cast<int>(255.0 * b / mx));

    // Left half: theoretical white (neutral)
    p.fillRect(0, 0, pm.width()/2, pm.height(), QColor(200, 200, 200));
    // Right half: calibrated colour
    p.fillRect(pm.width()/2, 0, pm.width()/2, pm.height(), QColor(ri, gi, bi));
    p.end();

    m_colourSwatch->setPixmap(pm);
    m_colourSwatch->setToolTip(
        tr("Left: neutral reference   Right: calibrated white point\n"
           "R=%1  G=%2  B=%3").arg(ri).arg(gi).arg(bi));
}

void SPCCDialog::setControlsEnabled(bool en)
{
    m_cameraCombo->setEnabled(en);
    m_filterCombo->setEnabled(en);
    m_minSNRSpin->setEnabled(en);
    m_maxStarsSpin->setEnabled(en);
    m_apertureSpin->setEnabled(en);
    m_magLimitSpin->setEnabled(en);
    m_fullMatrixCheck->setEnabled(en);
    m_solarRefCheck->setEnabled(en);
    m_neutralBgCheck->setEnabled(en);
    m_runBtn->setEnabled(en);
    m_resetBtn->setEnabled(en);
}

void SPCCDialog::closeEvent(QCloseEvent* event)
{
    if (m_watcher->isRunning()) { m_watcher->cancel(); m_watcher->waitForFinished(); }
    QDialog::closeEvent(event);
}