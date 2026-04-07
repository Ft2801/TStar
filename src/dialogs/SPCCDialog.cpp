/**
 * @file SPCCDialog.cpp
 * @brief Implementation of the Spectrophotometric Color Calibration dialog.
 *
 * Two-step user workflow:
 *
 * Step 1 (onFetchStars):
 *   - Reads the active ImageBuffer (must have WCS metadata).
 *   - Uses WCS to project catalog stars into pixel space.
 *   - For each catalog star, runs picklesMatchForSimbad() against the SED
 *     list from tstar_data.fits to assign a Pickles template.
 *   - If gaiaFallback is enabled, infers the spectral class from the Gaia
 *     BP-RP colour for stars that could not be matched to a Pickles SED.
 *
 * Step 2 (onRun -> startCalibration -> SPCC::calibrateWithStarList):
 *   - Builds per-channel system throughput: T_sys = T_filter * T_QE * T_LP.
 *   - For each matched star: measures aperture flux (background-subtracted
 *     annulus) and integrates the Pickles SED against T_sys to obtain
 *     expected colour ratios.
 *   - Fits a polynomial colour model (slope-only / affine / quadratic) that
 *     maps measured ratios to expected ones; the model with the lowest RMS
 *     fractional residual is chosen automatically.
 *   - Applies the model per-pixel: R' = f_R(R/G)*G, B' = f_B(B/G)*G.
 *   - Optionally removes a chromatic gradient via a poly2/poly3
 *     differential-magnitude surface fit, clamped to +/-0.05 mag peak.
 */

#include "SPCCDialog.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "../MainWindowCallbacks.h"
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

// =============================================================================
// Construction / Destruction
// =============================================================================

SPCCDialog::SPCCDialog(ImageViewer* viewer, MainWindow* mw, QWidget* parent)
    : QDialog(parent)
    , m_viewer(viewer)
    , m_mainWindow(mw)
{
    setWindowTitle(tr("Spectrophotometric Color Calibration"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setMinimumWidth(900);

    // Snapshot the current buffer so Reset can restore it later
    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();

    // Resolve the data directory containing tstar_data.fits.
    // Search order: <appdir>/data, <bundle>/Resources/data, <appdir>/../data
    m_dataPath = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + "/data");

    if (!QFile::exists(m_dataPath + "/tstar_data.fits")) {
        m_dataPath = QDir::cleanPath(
            QCoreApplication::applicationDirPath() + "/../Resources/data");
    }
    if (!QFile::exists(m_dataPath + "/tstar_data.fits")) {
        m_dataPath = QDir::cleanPath(
            QCoreApplication::applicationDirPath() + "/../data");
    }

    // Load spectral database eagerly so combo boxes can be populated
    bool fitsLoaded = SPCC::loadTStarFits(m_dataPath, m_store);
    bool dbLoaded   = SPCC::loadTStarDatabase(
        m_dataPath + "/TStar-spcc-database", m_store);
    m_storeLoaded = fitsLoaded || dbLoaded;

    // Initialize background worker watchers and catalog client
    m_fetchWatcher = new QFutureWatcher<std::vector<StarRecord>>(this);
    m_calibWatcher = new QFutureWatcher<SPCCResult>(this);
    m_catalog      = new CatalogClient(this);

    buildUI();
    connectSignals();
    populateCombosFromFits();
    restoreSettings();
}

SPCCDialog::~SPCCDialog()
{
    cleanup();
}

void SPCCDialog::setViewer(ImageViewer* viewer)
{
    m_viewer = viewer;
    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();
}

void SPCCDialog::closeEvent(QCloseEvent* ev)
{
    cleanup();
    QDialog::closeEvent(ev);
}

void SPCCDialog::cleanup()
{
    if (m_fetchWatcher && m_fetchWatcher->isRunning())
        m_fetchWatcher->cancel();
    if (m_calibWatcher && m_calibWatcher->isRunning())
        m_calibWatcher->cancel();
}

// =============================================================================
// UI Construction
// =============================================================================

void SPCCDialog::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // --- Two-column layout ---
    auto* columnsLayout = new QHBoxLayout;
    auto* leftCol  = new QVBoxLayout;
    auto* rightCol = new QVBoxLayout;
    leftCol->setSpacing(10);
    rightCol->setSpacing(10);

    // ---- LEFT COLUMN ----

    // Step 1 action bar
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

    // Equipment / Filters group
    auto* grpEquip  = new QGroupBox(tr("Equipment / Filters"), this);
    auto* formEquip = new QFormLayout(grpEquip);

    m_whiteRefCombo  = new QComboBox;
    m_rFilterCombo   = new QComboBox;
    m_gFilterCombo   = new QComboBox;
    m_bFilterCombo   = new QComboBox;
    m_sensorCombo    = new QComboBox;
    m_lpFilter1Combo = new QComboBox;
    m_lpFilter2Combo = new QComboBox;

    // Configure all equipment combos with consistent appearance
    const QString comboStyle =
        "QComboBox { combobox-popup: 0; } "
        "QAbstractItemView { max-height: 300px; }";

    QComboBox* equipCombos[] = {
        m_whiteRefCombo, m_rFilterCombo, m_gFilterCombo, m_bFilterCombo,
        m_sensorCombo, m_lpFilter1Combo, m_lpFilter2Combo
    };
    for (QComboBox* cb : equipCombos) {
        cb->setMaxVisibleItems(10);
        cb->setView(new QListView());
        cb->setStyleSheet(comboStyle);
    }

    formEquip->addRow(tr("White Reference (SED):"), m_whiteRefCombo);
    m_whiteRefCombo->setToolTip(
        tr("Stellar SED used as the white point reference (e.g., G2V for Sun-like)."));

    formEquip->addRow(tr("R Filter:"), m_rFilterCombo);
    m_rFilterCombo->setToolTip(
        tr("Transmission curve for the Red channel filter. Use '(None)' for flat response."));

    formEquip->addRow(tr("G Filter:"), m_gFilterCombo);
    m_gFilterCombo->setToolTip(
        tr("Transmission curve for the Green channel filter."));

    formEquip->addRow(tr("B Filter:"), m_bFilterCombo);
    m_bFilterCombo->setToolTip(
        tr("Transmission curve for the Blue channel filter."));

    formEquip->addRow(tr("Sensor (QE curve):"), m_sensorCombo);
    m_sensorCombo->setToolTip(
        tr("Quantum Efficiency curve of the camera sensor. Applies to all channels."));

    formEquip->addRow(tr("LP / Cut Filter 1:"), m_lpFilter1Combo);
    m_lpFilter1Combo->setToolTip(
        tr("Optional Light Pollution or IR/UV cut filter curve."));

    formEquip->addRow(tr("LP / Cut Filter 2:"), m_lpFilter2Combo);
    m_lpFilter2Combo->setToolTip(
        tr("A second optional filter curve (e.g., combining L-Pro with an IR-cut)."));

    leftCol->addWidget(grpEquip);

    // Options group
    auto* grpOpt = new QGroupBox(tr("Options"), this);
    auto* optL   = new QVBoxLayout(grpOpt);
    auto* formOpt = new QFormLayout;

    m_sepThreshSpin = new QDoubleSpinBox;
    m_sepThreshSpin->setRange(1.0, 50.0);
    m_sepThreshSpin->setValue(5.0);
    m_sepThreshSpin->setToolTip(
        tr("SEP source extraction threshold in units of background sigma."));
    formOpt->addRow(tr("Star Detect Threshold (sigma):"), m_sepThreshSpin);

    m_bgMethodCombo = new QComboBox;
    m_bgMethodCombo->addItems({"None", "Simple", "Poly2", "Poly3", "RBF"});
    m_bgMethodCombo->setToolTip(
        tr("Method used to model and subtract the sky background."));
    formOpt->addRow(tr("Background Method:"), m_bgMethodCombo);

    m_gradMethodCombo = new QComboBox;
    m_gradMethodCombo->addItems({"poly2", "poly3"});
    m_gradMethodCombo->setCurrentText("poly3");
    m_gradMethodCombo->setToolTip(
        tr("Polynomial degree for fitting chromatic gradients across the field."));
    formOpt->addRow(tr("Gradient Surface Method:"), m_gradMethodCombo);

    optL->addLayout(formOpt);

    m_fullMatrixCheck = new QCheckBox(
        tr("Estimate full 3x3 correction matrix (uncheck for diagonal only)"));
    m_fullMatrixCheck->setChecked(true);
    optL->addWidget(m_fullMatrixCheck);

    m_linearModeCheck = new QCheckBox(
        tr("Standard Linear Application (Recommended)"));
    m_linearModeCheck->setChecked(true);
    m_linearModeCheck->setToolTip(
        tr("Applies a single global multiplier per channel. "
           "If unchecked, applies a non-linear polynomial warping."));
    optL->addWidget(m_linearModeCheck);

    m_runGradientCheck = new QCheckBox(
        tr("Run chromatic gradient extraction after calibration"));
    m_runGradientCheck->setChecked(false);
    optL->addWidget(m_runGradientCheck);

    leftCol->addWidget(grpOpt);
    leftCol->addStretch();

    // ---- RIGHT COLUMN ----

    // Results group
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
    rightCol->addStretch();

    // Assemble the two-column layout
    columnsLayout->addLayout(leftCol, 1);
    columnsLayout->addLayout(rightCol, 1);
    mainLayout->addLayout(columnsLayout);

    // ---- BOTTOM AREA (full width) ----

    // Step 2 and action buttons
    auto* btnLayout = new QHBoxLayout;
    m_runBtn = new QPushButton(tr("Step 2: Run Calibration"));
    {
        QFont f = m_runBtn->font();
        f.setBold(true);
        m_runBtn->setFont(f);
    }
    m_resetBtn  = new QPushButton(tr("Reset"));
    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_cancelBtn->setEnabled(false);
    m_closeBtn  = new QPushButton(tr("Close"));

    btnLayout->addWidget(m_closeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_resetBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_runBtn);
    mainLayout->addLayout(btnLayout);

    // Status and progress indicators
    m_statusLabel = new QLabel;
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_progressBar);

    if (!m_storeLoaded) {
        m_statusLabel->setText(
            tr("Warning: tstar_data.fits not found --- combo boxes will be empty."));
    }
}

// =============================================================================
// Combo Box Population
// =============================================================================

/**
 * @brief Populate equipment combo boxes from the in-memory SPCCDataStore.
 *
 * Populates white reference (SEDs), R/G/B filter curves, sensor QE curves,
 * and light pollution filter curves. Duplicate names are suppressed.
 * Filter combos are pre-filtered by channel name when applicable.
 */
void SPCCDialog::populateCombosFromFits()
{
    const QString none = tr("(None)");

    // --- White reference: unique SED names ---
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
        if (!alreadyAdded)
            addedSeds << o.name;
    }
    addedSeds.sort(Qt::CaseInsensitive);
    m_whiteRefCombo->addItems(addedSeds);

    // Pre-select a sensible default (G2V preferred, then A0V)
    {
        int idx = m_whiteRefCombo->findText("G2V");
        if (idx < 0) idx = m_whiteRefCombo->findText("A0V");
        if (idx >= 0) m_whiteRefCombo->setCurrentIndex(idx);
    }

    // --- Filter combos (with leading "(None)" entry) ---
    // The lambda optionally restricts entries to those matching a channel name.
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
            // Skip duplicates
            bool alreadyAdded = false;
            for (const QString& s : uniqueNames) {
                if (s.compare(o.name, Qt::CaseInsensitive) == 0) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (alreadyAdded) continue;

            // Skip entries that do not match the channel filter
            if (channelRe.isValid() && !o.name.contains(channelRe)) continue;

            uniqueNames << o.name;
        }
        uniqueNames.sort(Qt::CaseInsensitive);
        cb->addItems(uniqueNames);
    };

    populateFilter(m_rFilterCombo,   "Red");
    populateFilter(m_gFilterCombo,   "Green");
    populateFilter(m_bFilterCombo,   "Blue");
    populateFilter(m_lpFilter1Combo);
    populateFilter(m_lpFilter2Combo);

    // --- Sensor QE curves ---
    m_sensorCombo->clear();
    m_sensorCombo->addItem(none);
    QStringList uniqueSensors;
    for (const SPCCObject& o : m_store.sensor_list) {
        // Strip per-channel suffixes to group sensor variants under one name
        QString displayName = o.name;
        displayName.remove(QRegularExpression(
            "\\s+(Red|Green|Blue|R|G|B)$",
            QRegularExpression::CaseInsensitiveOption));

        bool alreadyAdded = false;
        for (const QString& s : uniqueSensors) {
            if (s.compare(displayName, Qt::CaseInsensitive) == 0) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded)
            uniqueSensors << displayName;
    }
    uniqueSensors.sort(Qt::CaseInsensitive);
    m_sensorCombo->addItems(uniqueSensors);
}

// =============================================================================
// Signal/Slot Connections
// =============================================================================

void SPCCDialog::connectSignals()
{
    // Action buttons
    connect(m_closeBtn,      &QPushButton::clicked, this, &SPCCDialog::reject);
    connect(m_runBtn,        &QPushButton::clicked, this, &SPCCDialog::onRun);
    connect(m_resetBtn,      &QPushButton::clicked, this, &SPCCDialog::onReset);
    connect(m_cancelBtn,     &QPushButton::clicked, this, &SPCCDialog::onCancel);
    connect(m_fetchStarsBtn, &QPushButton::clicked, this, &SPCCDialog::onFetchStars);
    connect(m_saspViewerBtn, &QPushButton::clicked, this, &SPCCDialog::onOpenSaspViewer);

    // Background worker watchers
    connect(m_fetchWatcher,
            &QFutureWatcher<std::vector<StarRecord>>::finished,
            this, &SPCCDialog::onFetchStarsFinished);
    connect(m_calibWatcher,
            &QFutureWatcher<SPCCResult>::finished,
            this, &SPCCDialog::onCalibrationFinished);

    // Catalog client signals
    connect(m_catalog, &CatalogClient::catalogReady,
            this, &SPCCDialog::onCatalogReady);
    connect(m_catalog, &CatalogClient::errorOccurred,
            this, &SPCCDialog::onCatalogError);
    connect(m_catalog, &CatalogClient::mirrorStatus,
            this, [this](const QString& msg) {
                m_statusLabel->setText(msg);
            });

    // Persist equipment selections when changed
    connect(m_whiteRefCombo,   qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveWhiteRefSetting);
    connect(m_rFilterCombo,    qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveRFilterSetting);
    connect(m_gFilterCombo,    qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveGFilterSetting);
    connect(m_bFilterCombo,    qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveBFilterSetting);
    connect(m_sensorCombo,     qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveSensorSetting);
    connect(m_lpFilter1Combo,  qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveLpFilter1Setting);
    connect(m_lpFilter2Combo,  qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SPCCDialog::saveLpFilter2Setting);
    connect(m_gradMethodCombo, &QComboBox::currentTextChanged,
            this, &SPCCDialog::saveGradMethodSetting);
    connect(m_sepThreshSpin,   qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &SPCCDialog::saveSepThreshSetting);
    connect(m_fullMatrixCheck, &QCheckBox::toggled,
            this, &SPCCDialog::saveFullMatrixSetting);
    connect(m_linearModeCheck, &QCheckBox::toggled,
            this, &SPCCDialog::saveLinearModeSetting);
}

// =============================================================================
// Settings Persistence
// =============================================================================

void SPCCDialog::restoreSettings()
{
    QSettings s;
    s.beginGroup(kSettingsGroup);

    auto restoreCombo = [&](QComboBox* cb, const QString& key) {
        const QString val = s.value(key).toString();
        if (!val.isEmpty()) {
            const int idx = cb->findText(val);
            if (idx >= 0) cb->setCurrentIndex(idx);
        }
    };

    restoreCombo(m_whiteRefCombo,   "WhiteRef");
    restoreCombo(m_rFilterCombo,    "RFilter");
    restoreCombo(m_gFilterCombo,    "GFilter");
    restoreCombo(m_bFilterCombo,    "BFilter");
    restoreCombo(m_sensorCombo,     "Sensor");
    restoreCombo(m_lpFilter1Combo,  "LPFilter1");
    restoreCombo(m_lpFilter2Combo,  "LPFilter2");
    restoreCombo(m_gradMethodCombo, "GradMethod");

    m_sepThreshSpin->setValue(s.value("SEPThreshold", 5.0).toDouble());
    m_fullMatrixCheck->setChecked(s.value("FullMatrix", true).toBool());
    m_linearModeCheck->setChecked(s.value("linearMode", true).toBool());

    s.endGroup();
}

/* Individual setting save slots - each persists a single combo/control value */

void SPCCDialog::saveWhiteRefSetting(int)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("WhiteRef", m_whiteRefCombo->currentText());
    s.endGroup();
}

void SPCCDialog::saveRFilterSetting(int)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("RFilter", m_rFilterCombo->currentText());
    s.endGroup();
}

void SPCCDialog::saveGFilterSetting(int)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("GFilter", m_gFilterCombo->currentText());
    s.endGroup();
}

void SPCCDialog::saveBFilterSetting(int)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("BFilter", m_bFilterCombo->currentText());
    s.endGroup();
}

void SPCCDialog::saveSensorSetting(int)
{
    QString sensor = m_sensorCombo->currentText();
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("Sensor", sensor);
    s.endGroup();

    if (!m_storeLoaded) return;

    bool isOSC = false;
    if (sensor != tr("(None)")) {
        for (const SPCCObject& o : m_store.sensor_list) {
            QString baseName = o.name;
            baseName.remove(QRegularExpression("\\s+(Red|Green|Blue|R|G|B)$", QRegularExpression::CaseInsensitiveOption));
            
            if (baseName.compare(sensor, Qt::CaseInsensitive) == 0) {
                if (o.type == OSC_SENSOR) {
                    isOSC = true;
                    break;
                }
            }
        }
    }

    if (isOSC) {
        m_rFilterCombo->setCurrentText(tr("(None)"));
        m_gFilterCombo->setCurrentText(tr("(None)"));
        m_bFilterCombo->setCurrentText(tr("(None)"));
        m_rFilterCombo->setEnabled(false);
        m_gFilterCombo->setEnabled(false);
        m_bFilterCombo->setEnabled(false);
    } else {
        m_rFilterCombo->setEnabled(true);
        m_gFilterCombo->setEnabled(true);
        m_bFilterCombo->setEnabled(true);
    }
}

void SPCCDialog::saveLpFilter1Setting(int)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("LPFilter1", m_lpFilter1Combo->currentText());
    s.endGroup();
}

void SPCCDialog::saveLpFilter2Setting(int)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("LPFilter2", m_lpFilter2Combo->currentText());
    s.endGroup();
}

void SPCCDialog::saveGradMethodSetting(const QString& v)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("GradMethod", v);
    s.endGroup();
}

void SPCCDialog::saveSepThreshSetting(double v)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("SEPThreshold", v);
    s.endGroup();
}

void SPCCDialog::saveFullMatrixSetting(bool v)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("FullMatrix", v);
    s.endGroup();
}

void SPCCDialog::saveLinearModeSetting(bool v)
{
    QSettings s; s.beginGroup(kSettingsGroup);
    s.setValue("linearMode", v);
    s.endGroup();
}

// =============================================================================
// Step 1: Star Fetching
// =============================================================================

/**
 * @brief Initiate the star fetching workflow (Step 1).
 *
 * Validates that the active image has a valid WCS solution, then queries
 * the Gaia DR3 catalog via CatalogClient. Upon receiving the catalog
 * response (onCatalogReady), a background thread cross-matches detected
 * sources with catalog stars and assigns Pickles SED templates.
 */
void SPCCDialog::onFetchStars()
{
    // Validate prerequisites
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
            tr("Image must be plate solved before running SPCC.\n"
               "After stacking, run the ASTAP solver, then retry."));
        return;
    }

    setControlsEnabled(false);
    m_statusLabel->setText(tr("Querying star catalog (Gaia DR3)..."));
    m_progressBar->setValue(0);
    m_starList.clear();

    // Determine field center for the catalog query
    double center_ra, center_dec;
    if (!WCSUtils::getFieldCenter(meta,
                                  m_viewer->getBuffer().width(),
                                  m_viewer->getBuffer().height(),
                                  center_ra, center_dec)) {
        center_ra  = meta.ra;
        center_dec = meta.dec;
    }

    // Fixed 1.0 degree search radius balances completeness with server load
    const double searchRadius = 1.0;
    m_catalog->queryGaiaDR3(center_ra, center_dec, searchRadius);
}

/**
 * @brief Handle successful catalog query response.
 *
 * Launches a background thread to cross-match detected image sources with
 * catalog entries and assign Pickles SED templates based on BP-RP colour
 * or spectral type.
 */
void SPCCDialog::onCatalogReady(const std::vector<CatalogStar>& catalogStars)
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;

    if (catalogStars.empty()) {
        m_statusLabel->setText(tr("No catalog stars returned."));
        setControlsEnabled(true);
        QMessageBox::warning(this, tr("No Stars"),
            tr("No reference stars found in the catalog."));
        return;
    }

    m_statusLabel->setText(
        tr("Matching %1 catalog stars to image...").arg(catalogStars.size()));
    m_progressBar->setValue(20);

    // Capture copies for the background thread
    const ImageBuffer bufCopy        = m_viewer->getBuffer();
    const SPCCDataStore& storeRef    = m_store;
    const QStringList allSEDs        = SPCC::availableSEDs(storeRef);
    const double sepThreshold        = m_sepThreshSpin->value();

    QFuture<std::vector<StarRecord>> future = QtConcurrent::run(
        [bufCopy, catalogStars, allSEDs, sepThreshold]()
        -> std::vector<StarRecord>
    {
        std::vector<StarRecord> matchedStars;

        // Detect sources using the Green channel (or Mono) for centroids
        StarDetector detector;
        detector.setMaxStars(2000);
        detector.setThresholdSigma(sepThreshold);

        int ch = (bufCopy.channels() >= 3) ? 1 : 0;
        std::vector<DetectedStar> detected = detector.detect(bufCopy, ch);

        const ImageBuffer::Metadata& meta = bufCopy.metadata();

        // Cross-match tolerance: 15 arcseconds (accommodates WCS distortion)
        const double matchRadiusDeg = 15.0 / 3600.0;

        for (const DetectedStar& det : detected) {
            if (det.saturated) continue;

            double ra, dec;
            if (!WCSUtils::pixelToWorld(meta, det.x, det.y, ra, dec))
                continue;

            // Find the nearest catalog star within the match radius
            const CatalogStar* bestMatch = nullptr;
            double bestDist = matchRadiusDeg;

            for (const CatalogStar& cat : catalogStars) {
                if (std::abs(cat.dec - dec) > matchRadiusDeg) continue;

                double dRA = std::abs(cat.ra - ra);
                if (dRA > 180.0) dRA = 360.0 - dRA;

                double dRA_sky = dRA * std::cos(dec * M_PI / 180.0);
                if (dRA_sky > matchRadiusDeg) continue;

                double dist = std::sqrt(dRA_sky * dRA_sky +
                                        (cat.dec - dec) * (cat.dec - dec));
                if (dist < bestDist) {
                    bestDist  = dist;
                    bestMatch = &cat;
                }
            }

            if (!bestMatch) continue;

            // Build the StarRecord from the cross-match
            StarRecord sr;
            sr.x_img      = det.x;
            sr.y_img      = det.y;
            sr.ra         = bestMatch->ra;
            sr.dec        = bestMatch->dec;
            sr.semi_a     = (det.fwhm > 0) ? (det.fwhm * 1.5) : 3.0;
            sr.gaia_bp_rp = bestMatch->bp_rp;
            sr.gaia_gmag  = bestMatch->magV > 0 ? bestMatch->magV
                                                 : bestMatch->magB;

            // Determine the spectral class for Pickles SED assignment.
            // Priority: Gaia BP-RP > existing sp_type > Teff estimate.
            QString spClass;
            if (std::isfinite(sr.gaia_bp_rp)) {
                spClass = SPCC::inferTypeFromBpRp(sr.gaia_bp_rp);
            } else if (!sr.sp_type.isEmpty()) {
                spClass = sr.sp_type;
                sr.gaia_bp_rp = SPCC::bpRpFromType(spClass);
            } else if (bestMatch->teff > 0) {
                // Approximate BP-RP from effective temperature
                if      (bestMatch->teff > 30000) sr.gaia_bp_rp = -0.4;
                else if (bestMatch->teff > 15000) sr.gaia_bp_rp = -0.15;
                else if (bestMatch->teff >  9000) sr.gaia_bp_rp =  0.0;
                else if (bestMatch->teff >  7000) sr.gaia_bp_rp =  0.3;
                else if (bestMatch->teff >  6000) sr.gaia_bp_rp =  0.58;
                else if (bestMatch->teff >  5500) sr.gaia_bp_rp =  0.65;
                else if (bestMatch->teff >  5000) sr.gaia_bp_rp =  0.9;
                else if (bestMatch->teff >  4000) sr.gaia_bp_rp =  1.3;
                else                              sr.gaia_bp_rp =  2.0;
                spClass = SPCC::inferTypeFromBpRp(sr.gaia_bp_rp);
            }

            if (!spClass.isEmpty()) {
                QStringList cands = SPCC::picklesMatchForSimbad(spClass, allSEDs);
                if (!cands.isEmpty()) {
                    sr.pickles_match = cands.first();
                }
            }

            matchedStars.push_back(sr);
        }

        return matchedStars;
    });

    m_fetchWatcher->setFuture(future);
}

void SPCCDialog::onCatalogError(const QString& err)
{
    m_statusLabel->setText(tr("Catalog Error: %1").arg(err));
    setControlsEnabled(true);
    QMessageBox::warning(this, tr("Catalog Error"), err);
}

void SPCCDialog::onFetchStarsFinished()
{
    m_starList = m_fetchWatcher->result();
    setControlsEnabled(true);

    const int n = static_cast<int>(m_starList.size());
    int nMatched = 0;
    for (const StarRecord& sr : m_starList) {
        if (!sr.pickles_match.isEmpty()) ++nMatched;
    }

    m_statusLabel->setText(
        tr("Fetched %1 stars; %2 have Pickles SED matches.").arg(n).arg(nMatched));
    m_progressBar->setValue(100);

    if (n == 0) {
        QMessageBox::warning(this, tr("No Stars Found"),
            tr("No catalog stars were found in the field.\n"
               "Ensure the image has a valid plate solution."));
    }
}

// =============================================================================
// Step 2: Calibration
// =============================================================================

/**
 * @brief Validate inputs and launch the calibration workflow (Step 2).
 */
void SPCCDialog::onRun()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"),
            tr("No active image to calibrate."));
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
    if (m_rFilterCombo->currentText()  == tr("(None)") &&
        m_gFilterCombo->currentText()  == tr("(None)") &&
        m_bFilterCombo->currentText()  == tr("(None)") &&
        m_sensorCombo->currentText()   == tr("(None)")) {
        QMessageBox::warning(this, tr("Missing Input"),
            tr("Select at least one R, G, or B filter curve, or a Sensor curve."));
        return;
    }
    if (m_starList.empty()) {
        QMessageBox::warning(this, tr("No Stars"),
            tr("Fetch stars (Step 1) before running calibration."));
        return;
    }
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        const ImageBuffer::Metadata& currentMeta = m_viewer->getBuffer().metadata();
        if (!WCSUtils::hasValidWCS(currentMeta)) {
            QMessageBox::critical(this, tr("No WCS"),
                tr("Active image has no plate solution. Solve with ASTAP first."));
            return;
        }
    }

    // Snapshot for Reset before applying calibration
    m_originalBuffer = m_viewer->getBuffer();

    setControlsEnabled(false);
    m_statusLabel->setText(tr("Starting calibration..."));
    m_progressBar->setValue(0);

    startCalibration();
}

/**
 * @brief Dispatch calibration to SPCC::calibrateWithStarList() on a background thread.
 */
void SPCCDialog::startCalibration()
{
    m_cancelFlag.store(false);

    const ImageBuffer bufCopy              = m_viewer->getBuffer();
    const SPCCParams p                     = collectParams();
    const std::vector<StarRecord> starsCopy = m_starList;

    QFuture<SPCCResult> future = QtConcurrent::run(
        [bufCopy, p, starsCopy]() -> SPCCResult {
            try {
                return SPCC::calibrateWithStarList(bufCopy, p, starsCopy);
            } catch (const std::exception& e) {
                SPCCResult r;
                r.success   = false;
                r.error_msg = QString("SPCC exception: %1").arg(e.what());
                return r;
            } catch (...) {
                SPCCResult r;
                r.success   = false;
                r.error_msg = "SPCC: unknown exception during calibration.";
                return r;
            }
        });

    m_calibWatcher->setFuture(future);
}

/**
 * @brief Assemble an SPCCParams struct from the current dialog state.
 */
SPCCParams SPCCDialog::collectParams() const
{
    SPCCParams p;
    p.dataPath       = m_dataPath;
    p.whiteRef       = m_whiteRefCombo->currentText();
    p.rFilter        = m_rFilterCombo->currentText();
    p.gFilter        = m_gFilterCombo->currentText();
    p.bFilter        = m_bFilterCombo->currentText();
    p.sensor         = m_sensorCombo->currentText();
    p.lpFilter1      = m_lpFilter1Combo->currentText();
    p.lpFilter2      = m_lpFilter2Combo->currentText();
    p.bgMethod       = m_bgMethodCombo->currentText();
    p.sepThreshold   = m_sepThreshSpin->value();
    p.gaiaFallback   = true;
    p.useFullMatrix  = m_fullMatrixCheck->isChecked();
    p.linearMode     = m_linearModeCheck->isChecked();
    p.runGradient    = m_runGradientCheck->isChecked();
    p.gradientMethod = m_gradMethodCombo->currentText();
    p.cancelFlag     = const_cast<std::atomic<bool>*>(&m_cancelFlag);

    // Marshal progress updates to the UI thread via QMetaObject
    SPCCDialog* self = const_cast<SPCCDialog*>(this);
    p.progressCallback = [self](int pct, const QString& msg) {
        QMetaObject::invokeMethod(self, [self, pct, msg]() {
            if (self->m_progressBar) self->m_progressBar->setValue(pct);
            if (self->m_statusLabel) self->m_statusLabel->setText(msg);
        }, Qt::QueuedConnection);
    };

    return p;
}

// =============================================================================
// Calibration Completion
// =============================================================================

void SPCCDialog::onCalibrationFinished()
{
    setControlsEnabled(true);
    const SPCCResult r = m_calibWatcher->result();

    if (!r.success) {
        m_statusLabel->setText(tr("Calibration failed."));
        QMessageBox::critical(this, tr("SPCC Error"), r.error_msg);
        return;
    }

    if (r.modifiedBuffer && m_viewer) {
        // Build a PCCResult structure for downstream visualization
        PCCResult pccRes;
        pccRes.valid    = true;
        pccRes.R_factor = r.scaleR;
        pccRes.G_factor = r.scaleG;
        pccRes.B_factor = r.scaleB;

        // Extract diagnostic scatter plot data (inliers only)
        for (const auto& ds : r.diagnostics) {
            if (ds.is_inlier) {
                pccRes.CatRG.push_back(ds.exp_RG);
                pccRes.ImgRG.push_back(ds.meas_RG);
                pccRes.CatBG.push_back(ds.exp_BG);
                pccRes.ImgBG.push_back(ds.meas_BG);
            }
        }

        // Map SPCC polynomial coefficients to PCC slope/intercept fields.
        // Model form: a*x^2 + b*x + c => slope=b, intercept=c
        pccRes.slopeRG = r.model.coeff_R[1];
        pccRes.iceptRG = r.model.coeff_R[2];
        pccRes.slopeBG = r.model.coeff_B[1];
        pccRes.iceptBG = r.model.coeff_B[2];

        for (int i = 0; i < 3; ++i) {
            pccRes.polyRG[i] = r.model.coeff_R[i];
            pccRes.polyBG[i] = r.model.coeff_B[i];
        }
        pccRes.isQuadratic = (r.model.kind == MODEL_QUADRATIC);

        // Attach calibration result to image metadata
        ImageBuffer::Metadata meta = r.modifiedBuffer->metadata();
        meta.pccResult = pccRes;
        r.modifiedBuffer->setMetadata(meta);

        m_viewer->pushUndo(tr("SPCC"));
        m_viewer->setBuffer(*r.modifiedBuffer, QString(), true);

        if (m_mainWindow) {
            m_mainWindow->log(tr("SPCC applied."),
                              MainWindow::Log_Success, true);
        }
    }

    showResults(r);
    m_statusLabel->setText(tr("Calibration complete."));
    m_progressBar->setValue(100);

    // Display summary popup
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

// =============================================================================
// Reset / Cancel / Spectral Viewer
// =============================================================================

void SPCCDialog::onReset()
{
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

void SPCCDialog::onCancel()
{
    m_cancelFlag.store(true);
    m_statusLabel->setText(tr("Cancelling..."));
    if (m_fetchWatcher && m_fetchWatcher->isRunning()) {
        m_fetchWatcher->cancel();
    }
}

void SPCCDialog::onOpenSaspViewer()
{
    // Placeholder: SaspViewer is implemented elsewhere in the application.
    // Display database summary for diagnostic purposes.
    QMessageBox::information(this, tr("Spectral Viewer"),
        tr("Spectral database path:\n%1\n\n"
           "SEDs: %2    Filters: %3    Sensors: %4")
            .arg(m_dataPath)
            .arg(m_store.sed_list.size())
            .arg(m_store.filter_list.size())
            .arg(m_store.sensor_list.size()));
}

// =============================================================================
// Results Display
// =============================================================================

void SPCCDialog::showResults(const SPCCResult& r)
{
    m_starsLabel->setText(
        tr("Matched stars: %1 (from %2 detected)")
            .arg(r.stars_used).arg(r.stars_found));
    m_residualLabel->setText(
        tr("RMS residual: %1").arg(r.residual, 0, 'f', 4));
    m_scalesLabel->setText(
        tr("Scale factors (R, G, B): %1, %2, %3")
            .arg(r.scaleR, 0, 'f', 4)
            .arg(r.scaleG, 0, 'f', 4)
            .arg(r.scaleB, 0, 'f', 4));

    const QString modelName =
        (r.model.kind == MODEL_SLOPE_ONLY) ? tr("slope-only") :
        (r.model.kind == MODEL_AFFINE)     ? tr("affine")     :
                                             tr("quadratic");
    m_modelLabel->setText(tr("Model: %1").arg(modelName));

    // Fill the 3x3 correction matrix table
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            auto* item = new QTableWidgetItem(
                QString::number(r.corrMatrix[i][j], 'f', 4));
            item->setTextAlignment(Qt::AlignCenter);
            m_matrixTable->setItem(i, j, item);
        }
    }
}

// =============================================================================
// Control State Management
// =============================================================================

void SPCCDialog::setControlsEnabled(bool en)
{
    m_fetchStarsBtn->setEnabled(en);
    m_runBtn->setEnabled(en);
    m_resetBtn->setEnabled(en);
    m_whiteRefCombo->setEnabled(en);
    m_rFilterCombo->setEnabled(en);
    m_gFilterCombo->setEnabled(en);
    m_bFilterCombo->setEnabled(en);
    m_sensorCombo->setEnabled(en);
    m_lpFilter1Combo->setEnabled(en);
    m_lpFilter2Combo->setEnabled(en);
    m_sepThreshSpin->setEnabled(en);
    m_bgMethodCombo->setEnabled(en);
    m_gradMethodCombo->setEnabled(en);
    m_fullMatrixCheck->setEnabled(en);
    m_runGradientCheck->setEnabled(en);

    if (m_cancelBtn)
        m_cancelBtn->setEnabled(!en);
}