/*
 * SPCCDialog.cpp  —  Spectrophotometric Color Calibration dialog
 *
 * Two-step user workflow:
 * Step 1 (onFetchStars)
 * - Reads the active ImageBuffer (must have WCS metadata).
 * - Uses the WCS to project SIMBAD stars into pixel space.
 * - For each SIMBAD star, runs picklesMatchForSimbad() against the SED
 * list from tstar_data.fits to assign a Pickles template.
 * - If gaiaFallback is enabled, infers the spectral letter from the Gaia
 * BP-RP colour for stars that could not be matched to a Pickles SED.
 *
 * Step 2 (onRun -> startCalibration -> SPCC::calibrateWithStarList)
 * - Builds per-channel system throughput: T_sys = T_filter * T_QE * T_LP.
 * - For each matched star: measures aperture flux (background-subtracted
 * annulus) and integrates the Pickles SED against T_sys to obtain
 * expected colour ratios.
 * - Fits a polynomial colour model (slope-only / affine / quadratic) that
 * maps measured ratios to expected ones; the model with the lowest RMS
 * fractional residual is chosen automatically.
 * - Applies the model per-pixel: R' = f_R(R/G)*G, B' = f_B(B/G)*G.
 * - Optionally removes a chromatic gradient via a poly2/poly3 differential-
 * magnitude surface fit, clamped to ±0.05 mag peak amplitude.
 */

#include "SPCCDialog.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "core/Logger.h"
#include "../photometry/StarDetector.h"
#include "../astrometry/WCSUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QHeaderView>
#include <QSettings>
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QListView>
#include <QtConcurrent/QtConcurrentRun>

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
SPCCDialog::SPCCDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent)
    : QDialog(parent), m_viewer(viewer), m_mainWindow(mw)
{
    setWindowTitle(tr("Spectrophotometric Color Calibration"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setMinimumWidth(900);

    // Snapshot the current buffer so Reset can restore it
    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();

    // Resolve data path (directory containing tstar_data.fits)
    m_dataPath = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + "/data");
    if (!QFile::exists(m_dataPath + "/tstar_data.fits")) {
        // Try macOS bundle Resources path
        m_dataPath = QDir::cleanPath(
            QCoreApplication::applicationDirPath() + "/../Resources/data");
    }
    if (!QFile::exists(m_dataPath + "/tstar_data.fits")) {
        // Try one level up (common in build trees or some linux installs)
        m_dataPath = QDir::cleanPath(
            QCoreApplication::applicationDirPath() + "/../data");
    }

    // Load spectral database eagerly so combo boxes can be populated
    bool fitsLoaded = SPCC::loadTStarFits(m_dataPath, m_store);
    bool dbLoaded   = SPCC::loadTStarDatabase(m_dataPath + "/TStar-spcc-database", m_store);
    m_storeLoaded = fitsLoaded || dbLoaded;

    // Background workers
    m_fetchWatcher = new QFutureWatcher<std::vector<StarRecord>>(this);
    m_calibWatcher = new QFutureWatcher<SPCCResult>(this);
    m_catalog = new CatalogClient(this);

    buildUI();
    connectSignals();
    populateCombosFromFits();
    restoreSettings();
}

SPCCDialog::~SPCCDialog() {
    cleanup();
}

void SPCCDialog::setViewer(ImageViewer* viewer) {
    m_viewer = viewer;
    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();
}

void SPCCDialog::closeEvent(QCloseEvent* ev) {
    cleanup();
    QDialog::closeEvent(ev);
}

void SPCCDialog::cleanup() {
    if (m_fetchWatcher && m_fetchWatcher->isRunning()) m_fetchWatcher->cancel();
    if (m_calibWatcher && m_calibWatcher->isRunning()) m_calibWatcher->cancel();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Two-column layout
    auto* columnsLayout = new QHBoxLayout;
    auto* leftCol = new QVBoxLayout;
    auto* rightCol = new QVBoxLayout;

    // Ensure columns are properly spaced
    leftCol->setSpacing(10);
    rightCol->setSpacing(10);

    // ════════ LEFT COLUMN ════════

    // ── Step 1 button ─────────────────────────────────────────────────────────
    auto* step1Layout = new QHBoxLayout;
    m_fetchStarsBtn = new QPushButton(tr("Step 1: Fetch Stars from Active Image"));
    {
        QFont f = m_fetchStarsBtn->font();
        f.setBold(true);
        m_fetchStarsBtn->setFont(f);
    }
    step1Layout->addWidget(m_fetchStarsBtn);

    m_saspViewerBtn = new QPushButton(tr("Open Spectral Viewer"));
    step1Layout->addWidget(m_saspViewerBtn);
    leftCol->addLayout(step1Layout);

    // ── Equipment group ───────────────────────────────────────────────────────
    auto* grpEquip  = new QGroupBox(tr("Equipment / Filters"), this);
    auto* formEquip = new QFormLayout(grpEquip);

    m_whiteRefCombo  = new QComboBox;
    m_rFilterCombo   = new QComboBox;
    m_gFilterCombo   = new QComboBox;
    m_bFilterCombo   = new QComboBox;
    m_sensorCombo    = new QComboBox;
    m_lpFilter1Combo = new QComboBox;
    m_lpFilter2Combo = new QComboBox;

    m_whiteRefCombo->setMaxVisibleItems(10);
    m_rFilterCombo->setMaxVisibleItems(10);
    m_gFilterCombo->setMaxVisibleItems(10);
    m_bFilterCombo->setMaxVisibleItems(10);
    m_sensorCombo->setMaxVisibleItems(10);
    m_lpFilter1Combo->setMaxVisibleItems(10);
    m_lpFilter2Combo->setMaxVisibleItems(10);

    m_whiteRefCombo->setView(new QListView());
    m_rFilterCombo->setView(new QListView());
    m_gFilterCombo->setView(new QListView());
    m_bFilterCombo->setView(new QListView());
    m_sensorCombo->setView(new QListView());
    m_lpFilter1Combo->setView(new QListView());
    m_lpFilter2Combo->setView(new QListView());

    const QString s = "QComboBox { combobox-popup: 0; } QAbstractItemView { max-height: 300px; }";
    m_whiteRefCombo->setStyleSheet(s);
    m_rFilterCombo->setStyleSheet(s);
    m_gFilterCombo->setStyleSheet(s);
    m_bFilterCombo->setStyleSheet(s);
    m_sensorCombo->setStyleSheet(s);
    m_lpFilter1Combo->setStyleSheet(s);
    m_lpFilter2Combo->setStyleSheet(s);

    formEquip->addRow(tr("White Reference (SED):"), m_whiteRefCombo);
    formEquip->addRow(tr("R Filter:"),              m_rFilterCombo);
    formEquip->addRow(tr("G Filter:"),              m_gFilterCombo);
    formEquip->addRow(tr("B Filter:"),              m_bFilterCombo);
    formEquip->addRow(tr("Sensor (QE curve):"),     m_sensorCombo);
    formEquip->addRow(tr("LP / Cut Filter 1:"),     m_lpFilter1Combo);
    formEquip->addRow(tr("LP / Cut Filter 2:"),     m_lpFilter2Combo);
    leftCol->addWidget(grpEquip);

    // ── Options group ─────────────────────────────────────────────────────────
    auto* grpOpt = new QGroupBox(tr("Options"), this);
    auto* optL   = new QVBoxLayout(grpOpt);
    auto* formOpt = new QFormLayout;

    m_sepThreshSpin = new QDoubleSpinBox;
    m_sepThreshSpin->setRange(1.0, 50.0);
    m_sepThreshSpin->setValue(5.0);
    m_sepThreshSpin->setToolTip(tr("SEP source extraction threshold in units of background sigma."));
    formOpt->addRow(tr("Star Detect Threshold (sigma):"), m_sepThreshSpin);

    m_bgMethodCombo = new QComboBox;
    m_bgMethodCombo->addItems({"None", "Simple", "Poly2", "Poly3", "RBF"});
    formOpt->addRow(tr("Background Method:"), m_bgMethodCombo);

    m_gradMethodCombo = new QComboBox;
    m_gradMethodCombo->addItems({"poly2", "poly3"});
    m_gradMethodCombo->setCurrentText("poly3");
    formOpt->addRow(tr("Gradient Surface Method:"), m_gradMethodCombo);

    optL->addLayout(formOpt);

    m_gaiaFallbackCheck = new QCheckBox(
        tr("Gaia XP fallback for stars without Pickles match (slower)"));
    m_gaiaFallbackCheck->setChecked(true);
    optL->addWidget(m_gaiaFallbackCheck);

    m_fullMatrixCheck = new QCheckBox(
        tr("Estimate full 3x3 correction matrix (uncheck for diagonal only)"));
    m_fullMatrixCheck->setChecked(true);
    optL->addWidget(m_fullMatrixCheck);

    m_runGradientCheck = new QCheckBox(
        tr("Run chromatic gradient extraction after calibration"));
    m_runGradientCheck->setChecked(false);
    optL->addWidget(m_runGradientCheck);

    leftCol->addWidget(grpOpt);
    leftCol->addStretch(); // Push content up

    // ════════ RIGHT COLUMN ════════

    // ── Results group ─────────────────────────────────────────────────────────
    auto* grpRes = new QGroupBox(tr("Results"), this);
    auto* resL   = new QVBoxLayout(grpRes);

    m_starsLabel    = new QLabel(tr("Matched stars: -"));
    m_residualLabel = new QLabel(tr("RMS residual: -"));
    m_scalesLabel   = new QLabel(tr("Scale factors (R, G, B): -, -, -"));
    m_modelLabel    = new QLabel(tr("Model: -"));
    resL->addWidget(m_starsLabel);
    resL->addWidget(m_residualLabel);
    resL->addWidget(m_scalesLabel);
    resL->addWidget(m_modelLabel);

    m_matrixTable = new QTableWidget(3, 3);
    m_matrixTable->setHorizontalHeaderLabels({"R", "G", "B"});
    m_matrixTable->setVerticalHeaderLabels({"R'", "G'", "B'"});
    m_matrixTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_matrixTable->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_matrixTable->setFixedHeight(120);
    resL->addWidget(m_matrixTable);
    
    rightCol->addWidget(grpRes);
    rightCol->addStretch(); // Push results up

    // Add columns to the horizontal layout
    columnsLayout->addLayout(leftCol, 1);
    columnsLayout->addLayout(rightCol, 1);

    // Add the columns area to the main layout
    mainLayout->addLayout(columnsLayout);

    // ════════ BOTTOM AREA (Full Width) ════════

    // ── Step 2 + action buttons ───────────────────────────────────────────────
    auto* btnLayout = new QHBoxLayout;
    m_runBtn   = new QPushButton(tr("Step 2: Run Calibration"));
    {
        QFont f = m_runBtn->font();
        f.setBold(true);
        m_runBtn->setFont(f);
    }
    m_resetBtn = new QPushButton(tr("Reset"));
    m_closeBtn = new QPushButton(tr("Close"));
    btnLayout->addWidget(m_closeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_resetBtn);
    btnLayout->addWidget(m_runBtn);
    mainLayout->addLayout(btnLayout);

    // ── Status / progress ─────────────────────────────────────────────────────
    m_statusLabel = new QLabel;
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_progressBar);

    if (!m_storeLoaded) {
        m_statusLabel->setText(tr("Warning: tstar_data.fits not found — combo boxes will be empty."));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// populateCombosFromFits
// Fills the equipment combo boxes from the in-memory SPCCDataStore.
// Mirrors Python _reload_hdu_lists() + _build_ui() combo population.
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::populateCombosFromFits() {
    const QString none = tr("(None)");

    // 1. White reference: SED names only
    m_whiteRefCombo->clear();
    QStringList addedSeds;
    for (const SPCCObject& o : m_store.sed_list) {
        bool alreadyAdded = false;
        for (const QString& s : addedSeds) {
            if (s.compare(o.name, Qt::CaseInsensitive) == 0) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) addedSeds << o.name;
    }
    addedSeds.sort(Qt::CaseInsensitive);
    m_whiteRefCombo->addItems(addedSeds);

    // Try to pre-select G2V or A0V as a sensible default
    {
        int idx = m_whiteRefCombo->findText("G2V");
        if (idx < 0) idx = m_whiteRefCombo->findText("A0V");
        if (idx >= 0) m_whiteRefCombo->setCurrentIndex(idx);
    }

    // 2. Filters (with leading "(None)" entry)
    auto populateFilter = [&](QComboBox* cb, const QString& channel = QString()) {
        cb->clear();
        cb->addItem(none);
        QStringList uniqueNames;
        
        QRegularExpression channelRe;
        if (!channel.isEmpty()) {
             QString initial = channel.left(1); 
             QString pattern = QString("(%1|\\b%2\\b|\\s%2|[_-]%2)")
                               .arg(channel, initial);
             channelRe.setPattern(pattern);
             channelRe.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }

        for (const SPCCObject& o : m_store.filter_list) {
            bool alreadyAdded = false;
            for (const QString& s : uniqueNames) {
                if (s.compare(o.name, Qt::CaseInsensitive) == 0) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (alreadyAdded) continue;

            if (channelRe.isValid() && !o.name.contains(channelRe)) continue;

            uniqueNames << o.name;
        }
        uniqueNames.sort(Qt::CaseInsensitive);
        cb->addItems(uniqueNames);
    };

    populateFilter(m_rFilterCombo, "Red");
    populateFilter(m_gFilterCombo, "Green");
    populateFilter(m_bFilterCombo, "Blue");
    populateFilter(m_lpFilter1Combo);
    populateFilter(m_lpFilter2Combo);

    // 3. Sensors
    m_sensorCombo->clear();
    m_sensorCombo->addItem(none);
    QStringList uniqueSensors;
    for (const SPCCObject& o : m_store.sensor_list) {
        QString displayName = o.name;
        displayName.remove(QRegularExpression("\\s+(Red|Green|Blue|R|G|B)$", QRegularExpression::CaseInsensitiveOption));
        
        bool alreadyAdded = false;
        for (const QString& s : uniqueSensors) {
            if (s.compare(displayName, Qt::CaseInsensitive) == 0) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) uniqueSensors << displayName;
    }
    uniqueSensors.sort(Qt::CaseInsensitive);
    m_sensorCombo->addItems(uniqueSensors);
}

// ─────────────────────────────────────────────────────────────────────────────
// connectSignals
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::connectSignals() {
    connect(m_closeBtn,      &QPushButton::clicked, this, &SPCCDialog::reject);
    connect(m_runBtn,        &QPushButton::clicked, this, &SPCCDialog::onRun);
    connect(m_resetBtn,      &QPushButton::clicked, this, &SPCCDialog::onReset);
    connect(m_fetchStarsBtn, &QPushButton::clicked, this, &SPCCDialog::onFetchStars);
    connect(m_saspViewerBtn, &QPushButton::clicked, this, &SPCCDialog::onOpenSaspViewer);

    // Worker watchers
    connect(m_fetchWatcher, &QFutureWatcher<std::vector<StarRecord>>::finished,
            this, &SPCCDialog::onFetchStarsFinished);
    connect(m_calibWatcher, &QFutureWatcher<SPCCResult>::finished,
            this, &SPCCDialog::onCalibrationFinished);
    connect(m_catalog, &CatalogClient::catalogReady, this, &SPCCDialog::onCatalogReady);
    connect(m_catalog, &CatalogClient::errorOccurred, this, &SPCCDialog::onCatalogError);
    connect(m_catalog, &CatalogClient::mirrorStatus, this, [this](const QString& msg){
        m_statusLabel->setText(msg);
    });

    // Settings persistence
    connect(m_whiteRefCombo,  qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveWhiteRefSetting);
    connect(m_rFilterCombo,   qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveRFilterSetting);
    connect(m_gFilterCombo,   qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveGFilterSetting);
    connect(m_bFilterCombo,   qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveBFilterSetting);
    connect(m_sensorCombo,    qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveSensorSetting);
    connect(m_lpFilter1Combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveLpFilter1Setting);
    connect(m_lpFilter2Combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveLpFilter2Setting);
    connect(m_gradMethodCombo, &QComboBox::currentTextChanged,
            this, &SPCCDialog::saveGradMethodSetting);
    connect(m_sepThreshSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &SPCCDialog::saveSepThreshSetting);
    connect(m_gaiaFallbackCheck, &QCheckBox::toggled,
            this, &SPCCDialog::saveGaiaFallbackSetting);
    connect(m_fullMatrixCheck, &QCheckBox::toggled,
            this, &SPCCDialog::saveFullMatrixSetting);
}

// ─────────────────────────────────────────────────────────────────────────────
// restoreSettings
// Mirrors Python load_settings().
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::restoreSettings() {
    QSettings s;
    s.beginGroup(kSettingsGroup);

    auto restoreCombo = [&](QComboBox* cb, const QString& key) {
        const QString val = s.value(key).toString();
        if (!val.isEmpty()) {
            const int idx = cb->findText(val);
            if (idx >= 0) cb->setCurrentIndex(idx);
        }
    };

    restoreCombo(m_whiteRefCombo,  "WhiteRef");
    restoreCombo(m_rFilterCombo,   "RFilter");
    restoreCombo(m_gFilterCombo,   "GFilter");
    restoreCombo(m_bFilterCombo,   "BFilter");
    restoreCombo(m_sensorCombo,    "Sensor");
    restoreCombo(m_lpFilter1Combo, "LPFilter1");
    restoreCombo(m_lpFilter2Combo, "LPFilter2");
    restoreCombo(m_gradMethodCombo,"GradMethod");

    m_sepThreshSpin->setValue(s.value("SEPThreshold", 5.0).toDouble());
    m_gaiaFallbackCheck->setChecked(s.value("GaiaFallback", true).toBool());
    m_fullMatrixCheck->setChecked(s.value("FullMatrix", true).toBool());

    s.endGroup();
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings save slots
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::saveWhiteRefSetting(int)  { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("WhiteRef",   m_whiteRefCombo->currentText());  s.endGroup(); }
void SPCCDialog::saveRFilterSetting(int)   { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("RFilter",    m_rFilterCombo->currentText());   s.endGroup(); }
void SPCCDialog::saveGFilterSetting(int)   { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("GFilter",    m_gFilterCombo->currentText());   s.endGroup(); }
void SPCCDialog::saveBFilterSetting(int)   { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("BFilter",    m_bFilterCombo->currentText());   s.endGroup(); }
void SPCCDialog::saveSensorSetting(int)    { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("Sensor",     m_sensorCombo->currentText());    s.endGroup(); }
void SPCCDialog::saveLpFilter1Setting(int) { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("LPFilter1",  m_lpFilter1Combo->currentText()); s.endGroup(); }
void SPCCDialog::saveLpFilter2Setting(int) { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("LPFilter2",  m_lpFilter2Combo->currentText()); s.endGroup(); }
void SPCCDialog::saveGradMethodSetting(const QString& v) { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("GradMethod", v); s.endGroup(); }
void SPCCDialog::saveSepThreshSetting(double v)  { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("SEPThreshold", v); s.endGroup(); }
void SPCCDialog::saveGaiaFallbackSetting(bool v) { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("GaiaFallback", v); s.endGroup(); }
void SPCCDialog::saveFullMatrixSetting(bool v)   { QSettings s; s.beginGroup(kSettingsGroup); s.setValue("FullMatrix",   v); s.endGroup(); }

// ─────────────────────────────────────────────────────────────────────────────
// onFetchStars  (Step 1)
// Queries the WCS-annotated image for SIMBAD stars and builds m_starList.
// The actual network call runs on a QtConcurrent background thread; the result
// is picked up in onFetchStarsFinished().
//
// This is a placeholder skeleton: the application-level SIMBAD query
// (astroquery equivalent) and SEP star detection are expected to be provided
// by the surrounding infrastructure (CatalogClient, StarDetector etc.).
// Here we wire together the pieces that are internal to SPCC:
//   - picklesMatchForSimbad() for spectral type matching
//   - inferLetterFromBpRp()   for Gaia colour fallback
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::onFetchStars() {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"),
            tr("Please open and plate-solve an image before fetching stars."));
        return;
    }
    if (!m_storeLoaded) {
        QMessageBox::critical(this, tr("Database Missing"),
            tr("tstar_data.fits could not be loaded from: %1").arg(m_dataPath));
        return;
    }

    const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) {
        QMessageBox::warning(this, tr("No WCS"),
            tr("Image must be plate-solved first."));
        return;
    }

    setControlsEnabled(false);
    m_statusLabel->setText(tr("Querying star catalog (Gaia DR3)..."));
    m_progressBar->setValue(0);

    // Ensure we start a fresh list
    m_starList.clear();

    // The CatalogClient needs the center RA/Dec.
    double center_ra, center_dec;
    if (!WCSUtils::getFieldCenter(meta, m_viewer->getBuffer().width(), m_viewer->getBuffer().height(), center_ra, center_dec)) {
        // Fallback to CRVAL
        center_ra = meta.ra;
        center_dec = meta.dec;
    }

    // Use fixed 1.0 deg radius for Gaia DR3 (consistent with PCC).
    // This balances catalog completeness with VizieR server reliability.
    const double searchRadius = 1.0;

    // Call the catalog client.
    m_statusLabel->setText(tr("Querying star catalog (Gaia DR3)..."));
    m_catalog->queryGaiaDR3(center_ra, center_dec, searchRadius);
}

void SPCCDialog::onCatalogReady(const std::vector<CatalogStar>& catalogStars) {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;

    if (catalogStars.empty()) {
        m_statusLabel->setText(tr("No catalog stars returned."));
        setControlsEnabled(true);
        QMessageBox::warning(this, tr("No Stars"), tr("No reference stars found in the catalog."));
        return;
    }

    m_statusLabel->setText(tr("Matching %1 catalog stars to image...").arg(catalogStars.size()));
    m_progressBar->setValue(20);

    const ImageBuffer bufCopy = m_viewer->getBuffer();
    const SPCCDataStore& storeRef = m_store;
    const QStringList allSEDs = SPCC::availableSEDs(storeRef);
    const bool useGaia = m_gaiaFallbackCheck->isChecked();
    const double sepThreshold = m_sepThreshSpin->value();

    QFuture<std::vector<StarRecord>> future = QtConcurrent::run(
        [bufCopy, catalogStars, allSEDs, useGaia, sepThreshold, &storeRef]() -> std::vector<StarRecord>
    {
        std::vector<StarRecord> matchedStars;

        // 1. Detect stars using the Green channel (or Mono) as a proxy for the centroid
        StarDetector detector;
        detector.setMaxStars(2000); // Plentiful detections
        detector.setThresholdSigma(sepThreshold);
        
        int ch = (bufCopy.channels() >= 3) ? 1 : 0;
        std::vector<DetectedStar> detected = detector.detect(bufCopy, ch);

        const ImageBuffer::Metadata& meta = bufCopy.metadata();
        // Tolerance around 15 arcseconds for cross-matching (WCS distortion tolerance).
        const double matchRadiusDeg = 15.0 / 3600.0;

        // 2. Cross-match physical stars to catalog
        for (const DetectedStar& det : detected) {
            if (det.saturated) continue;

            double ra, dec;
            if (!WCSUtils::pixelToWorld(meta, det.x, det.y, ra, dec)) continue;

            const CatalogStar* bestMatch = nullptr;
            double bestDist = matchRadiusDeg;

            for (const CatalogStar& cat : catalogStars) {
                if (std::abs(cat.dec - dec) > matchRadiusDeg) continue;
                
                double dRA = std::abs(cat.ra - ra);
                if (dRA > 180.0) dRA = 360.0 - dRA;
                
                double dRA_sky = dRA * std::cos(dec * M_PI / 180.0);
                if (dRA_sky > matchRadiusDeg) continue;

                double dist = std::sqrt(dRA_sky * dRA_sky + (cat.dec - dec) * (cat.dec - dec));
                if (dist < bestDist) {
                    bestDist = dist;
                    bestMatch = &cat;
                }
            }

            if (bestMatch) {
                StarRecord sr;
                sr.x_img = det.x;
                sr.y_img = det.y;
                sr.ra = bestMatch->ra;
                sr.dec = bestMatch->dec;
                sr.semi_a = (det.fwhm > 0) ? (det.fwhm * 1.5) : 3.0;
                sr.gaia_bp_rp = bestMatch->bp_rp;
                sr.gaia_gmag = bestMatch->magV > 0 ? bestMatch->magV : bestMatch->magB;

                // 3. Obtain a Pickles match via Gaia BP-RP Fallback
                if (std::isfinite(sr.gaia_bp_rp)) {
                    sr.sp_clean = SPCC::inferLetterFromBpRp(sr.gaia_bp_rp);
                    if (!sr.sp_clean.isEmpty()) {
                        QStringList cands = SPCC::picklesMatchForSimbad(sr.sp_clean, allSEDs);
                        if (!cands.isEmpty()) {
                            sr.pickles_match = cands.first();
                        }
                    }
                }
                
                matchedStars.push_back(sr);
            }
        }
        return matchedStars;
    });

    m_fetchWatcher->setFuture(future);
}

void SPCCDialog::onCatalogError(const QString& err) {
    m_statusLabel->setText(tr("Catalog Error: %1").arg(err));
    setControlsEnabled(true);
    QMessageBox::warning(this, tr("Catalog Error"), err);
}

void SPCCDialog::onFetchStarsFinished() {
    m_starList = m_fetchWatcher->result();
    setControlsEnabled(true);

    const int n = (int)m_starList.size();
    int nMatched = 0;
    for (const StarRecord& sr : m_starList)
        if (!sr.pickles_match.isEmpty()) ++nMatched;

    m_statusLabel->setText(
        tr("Fetched %1 stars; %2 have Pickles SED matches.").arg(n).arg(nMatched));
    m_progressBar->setValue(100);

    if (n == 0) {
        QMessageBox::warning(this, tr("No Stars Found"),
            tr("No SIMBAD stars were found in the field.\n"
               "Ensure the image has a valid plate solution."));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onRun  (Step 2)
// Validates inputs and launches the background calibration.
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::onRun() {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"), tr("No active image to calibrate."));
        return;
    }
    if (!m_storeLoaded) {
        QMessageBox::critical(this, tr("Database Missing"),
            tr("tstar_data.fits could not be loaded."));
        return;
    }
    if (m_whiteRefCombo->currentText().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Input"),
            tr("Select a white reference SED (e.g. G2V or A0V)."));
        return;
    }
    if (m_rFilterCombo->currentText() == tr("(None)") &&
        m_gFilterCombo->currentText() == tr("(None)") &&
        m_bFilterCombo->currentText() == tr("(None)")) {
        QMessageBox::warning(this, tr("Missing Input"),
            tr("Select at least one of the R, G or B filter curves."));
        return;
    }
    if (m_starList.empty()) {
        QMessageBox::warning(this, tr("No Stars"),
            tr("Fetch stars (Step 1) before running calibration."));
        return;
    }

    // Take a fresh snapshot for Reset
    m_originalBuffer = m_viewer->getBuffer();

    setControlsEnabled(false);
    m_statusLabel->setText(tr("Starting calibration..."));
    m_progressBar->setValue(0);

    startCalibration();
}

// ─────────────────────────────────────────────────────────────────────────────
// startCalibration
// Gathers parameters and dispatches to SPCC::calibrateWithStarList() on a
// background thread.
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::startCalibration() {
    const ImageBuffer bufCopy = m_viewer->getBuffer();
    const SPCCParams  p       = collectParams();
    const std::vector<StarRecord> starsCopy = m_starList;

    QFuture<SPCCResult> future = QtConcurrent::run(
        [bufCopy, p, starsCopy]() -> SPCCResult {
            return SPCC::calibrateWithStarList(bufCopy, p, starsCopy);
        });
    m_calibWatcher->setFuture(future);
}

// ─────────────────────────────────────────────────────────────────────────────
// collectParams
// Assembles an SPCCParams struct from the current dialog state.
// ─────────────────────────────────────────────────────────────────────────────
SPCCParams SPCCDialog::collectParams() const {
    SPCCParams p;
    p.dataPath     = m_dataPath;
    p.whiteRef     = m_whiteRefCombo->currentText();
    p.rFilter      = m_rFilterCombo->currentText();
    p.gFilter      = m_gFilterCombo->currentText();
    p.bFilter      = m_bFilterCombo->currentText();
    p.sensor       = m_sensorCombo->currentText();
    p.lpFilter1    = m_lpFilter1Combo->currentText();
    p.lpFilter2    = m_lpFilter2Combo->currentText();
    p.bgMethod     = m_bgMethodCombo->currentText();
    p.sepThreshold = m_sepThreshSpin->value();
    p.gaiaFallback = m_gaiaFallbackCheck->isChecked();
    p.useFullMatrix = m_fullMatrixCheck->isChecked();
    p.runGradient  = m_runGradientCheck->isChecked();
    p.gradientMethod = m_gradMethodCombo->currentText();

    // Progress callback: marshal updates to the UI thread via QMetaObject.
    // The lambda captures 'this' by pointer; SPCCDialog outlives the worker
    // because we cancel the watcher in cleanup().
    SPCCDialog* self = const_cast<SPCCDialog*>(this);
    p.progressCallback = [self](int pct, const QString& msg) {
        QMetaObject::invokeMethod(self, [self, pct, msg]() {
            if (self->m_progressBar) self->m_progressBar->setValue(pct);
            if (self->m_statusLabel) self->m_statusLabel->setText(msg);
        }, Qt::QueuedConnection);
    };
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// onCalibrationFinished
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::onCalibrationFinished() {
    setControlsEnabled(true);
    const SPCCResult r = m_calibWatcher->result();

    if (!r.success) {
        m_statusLabel->setText(tr("Calibration failed."));
        QMessageBox::critical(this, tr("SPCC Error"), r.error_msg);
        return;
    }

    if (r.modifiedBuffer && m_viewer) {
        m_viewer->pushUndo();
        m_viewer->setBuffer(*r.modifiedBuffer, QString(), true);
    }

    showResults(r);
    m_statusLabel->setText(tr("Calibration complete."));
    m_progressBar->setValue(100);

    // Summary popup (mirrors Python QMessageBox.information at end of run_spcc)
    const QString modelName =
        (r.model.kind == MODEL_SLOPE_ONLY) ? "slope-only" :
        (r.model.kind == MODEL_AFFINE)     ? "affine"     : "quadratic";
    QMessageBox::information(this, tr("SPCC Complete"),
        tr("Calibration applied using %1 stars.\n"
           "Model: %2\n"
           "R scale @ x=1: %3\n"
           "B scale @ x=1: %4\n"
           "RMS residual: %5")
        .arg(r.stars_used)
        .arg(modelName)
        .arg(r.scaleR, 0, 'f', 4)
        .arg(r.scaleB, 0, 'f', 4)
        .arg(r.residual, 0, 'f', 4));
}

// ─────────────────────────────────────────────────────────────────────────────
// onReset
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::onReset() {
    if (m_viewer && m_originalBuffer.isValid())
        m_viewer->setBuffer(m_originalBuffer, QString(), false);

    m_matrixTable->clearContents();
    m_starsLabel->setText(tr("Matched stars: -"));
    m_residualLabel->setText(tr("RMS residual: -"));
    m_scalesLabel->setText(tr("Scale factors (R, G, B): -, -, -"));
    m_modelLabel->setText(tr("Model: -"));
    m_statusLabel->clear();
    m_progressBar->setValue(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// onOpenSaspViewer
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::onOpenSaspViewer() {
    // Placeholder: the SaspViewer window is an independent dialog that plots
    // the SED * T_sys responses for the currently selected equipment.
    // It is expected to be implemented elsewhere in the application and invoked
    // from here.  For now, show the database path to the user.
    QMessageBox::information(this, tr("Spectral Viewer"),
        tr("Spectral database path:\n%1\n\n"
           "SEDs: %2   Filters: %3   Sensors: %4")
        .arg(m_dataPath)
        .arg(m_store.sed_list.size())
        .arg(m_store.filter_list.size())
        .arg(m_store.sensor_list.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// showResults
// Populates the Results group from a completed SPCCResult.
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::showResults(const SPCCResult& r) {
    m_starsLabel->setText(
        tr("Matched stars: %1 (from %2 detected)").arg(r.stars_used).arg(r.stars_found));
    m_residualLabel->setText(
        tr("RMS residual: %1").arg(r.residual, 0, 'f', 4));
    m_scalesLabel->setText(
        tr("Scale factors (R, G, B): %1, %2, %3")
        .arg(r.scaleR, 0, 'f', 4).arg(r.scaleG, 0, 'f', 4).arg(r.scaleB, 0, 'f', 4));

    const QString modelName =
        (r.model.kind == MODEL_SLOPE_ONLY) ? tr("slope-only") :
        (r.model.kind == MODEL_AFFINE)     ? tr("affine")     : tr("quadratic");
    m_modelLabel->setText(tr("Model: %1").arg(modelName));

    // Fill the 3x3 matrix table
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            auto* item = new QTableWidgetItem(
                QString::number(r.corrMatrix[i][j], 'f', 4));
            item->setTextAlignment(Qt::AlignCenter);
            m_matrixTable->setItem(i, j, item);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setControlsEnabled
// ─────────────────────────────────────────────────────────────────────────────
void SPCCDialog::setControlsEnabled(bool en) {
    m_fetchStarsBtn ->setEnabled(en);
    m_runBtn        ->setEnabled(en);
    m_resetBtn      ->setEnabled(en);
    m_whiteRefCombo ->setEnabled(en);
    m_rFilterCombo  ->setEnabled(en);
    m_gFilterCombo  ->setEnabled(en);
    m_bFilterCombo  ->setEnabled(en);
    m_sensorCombo   ->setEnabled(en);
    m_lpFilter1Combo->setEnabled(en);
    m_lpFilter2Combo->setEnabled(en);
    m_sepThreshSpin ->setEnabled(en);
    m_bgMethodCombo ->setEnabled(en);
    m_gradMethodCombo->setEnabled(en);
    m_gaiaFallbackCheck->setEnabled(en);
    m_fullMatrixCheck->setEnabled(en);
    m_runGradientCheck->setEnabled(en);
}