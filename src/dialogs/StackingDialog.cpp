
#include "StackingDialog.h"
#include "../MainWindow.h"
#include "../ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QApplication>
#include <algorithm>

//=============================================================================
// CONSTRUCTOR / DESTRUCTOR
//=============================================================================

StackingDialog::StackingDialog(MainWindow* parent)
    : QDialog(parent)
    , m_mainWindow(parent)
{
    setWindowTitle(tr("Image Stacking"));
    setMinimumSize(900, 700);
    resize(1000, 700);
    
    setupUI();
    updateParameterVisibility();
    
    // Center on parent
    if (parent) {
        move(parent->geometry().center() - rect().center());
    }
}

StackingDialog::~StackingDialog() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
        m_worker->wait();
    }
}

//=============================================================================
// UI SETUP
//=============================================================================

void StackingDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    
    // Create horizontal split: left=sequence, right=params+output
    QHBoxLayout* topLayout = new QHBoxLayout();
    
    setupSequenceGroup();
    setupPlotTab();
    setupCometTab();
    
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(m_sequenceGroup, tr("Images"));
    m_tabWidget->addTab(m_plotTab, tr("Analysis"));
    m_tabWidget->addTab(m_cometTab, tr("Comet"));
    
    topLayout->addWidget(m_tabWidget, 3);
    
    QVBoxLayout* rightLayout = new QVBoxLayout();
    setupParametersGroup();
    setupOutputGroup();
    rightLayout->addWidget(m_paramsGroup);
    rightLayout->addWidget(m_outputGroup);
    topLayout->addLayout(rightLayout, 2);
    
    mainLayout->addLayout(topLayout, 1);
    
    setupProgressGroup();
    mainLayout->addWidget(m_progressGroup);
}

void StackingDialog::setupSequenceGroup() {
    m_sequenceGroup = new QGroupBox(tr("Image Sequence"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_sequenceGroup);
    layout->setContentsMargins(5, 10, 5, 5);
    
    // Toolbar
    QHBoxLayout* toolbar = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load Folder..."), this);
    m_addBtn = new QPushButton(tr("Add Files..."), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    m_selectAllBtn = new QPushButton(tr("Select All"), this);
    m_deselectAllBtn = new QPushButton(tr("Deselect All"), this);
    m_setRefBtn = new QPushButton(tr("Set Reference"), this);
    
    toolbar->addWidget(m_loadBtn);
    toolbar->addWidget(m_addBtn);
    toolbar->addWidget(m_removeBtn);
    toolbar->addStretch();
    toolbar->addWidget(m_selectAllBtn);
    toolbar->addWidget(m_deselectAllBtn);
    toolbar->addWidget(m_setRefBtn);
    layout->addLayout(toolbar);
    
    // Image table
    m_imageTable = new QTableWidget(this);
    m_imageTable->setColumnCount(7);
    m_imageTable->setHorizontalHeaderLabels({
        tr(""), tr("Filename"), tr("W×H"), tr("Exp"), 
        tr("FWHM"), tr("Quality"), tr("Shift")
    });
    m_imageTable->horizontalHeader()->setStretchLastSection(false);
    m_imageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_imageTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_imageTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_imageTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_imageTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_imageTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_imageTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_imageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_imageTable->setAlternatingRowColors(true);
    layout->addWidget(m_imageTable);
    
    // Filter row
    QHBoxLayout* filterRow = new QHBoxLayout();
    filterRow->addWidget(new QLabel(tr("Filter:"), this));
    
    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem(tr("All Images"), static_cast<int>(Stacking::ImageFilter::All));
    m_filterCombo->addItem(tr("Selected"), static_cast<int>(Stacking::ImageFilter::Selected));
    m_filterCombo->addItem(tr("Best FWHM"), static_cast<int>(Stacking::ImageFilter::BestFWHM));
    m_filterCombo->addItem(tr("Best Roundness"), static_cast<int>(Stacking::ImageFilter::BestRoundness));
    m_filterCombo->addItem(tr("Best Quality"), static_cast<int>(Stacking::ImageFilter::BestQuality));
    filterRow->addWidget(m_filterCombo);
    
    filterRow->addWidget(new QLabel(tr("Keep:"), this));
    m_filterValue = new QDoubleSpinBox(this);
    m_filterValue->setRange(1, 100);
    m_filterValue->setValue(90);
    m_filterValue->setSuffix("%");
    filterRow->addWidget(m_filterValue);
    
    filterRow->addStretch();
    
    m_sequenceSummary = new QLabel(tr("No sequence loaded"), this);
    filterRow->addWidget(m_sequenceSummary);
    
    layout->addLayout(filterRow);
    
    // Connect signals
    connect(m_loadBtn, &QPushButton::clicked, this, &StackingDialog::onLoadSequence);
    connect(m_addBtn, &QPushButton::clicked, this, &StackingDialog::onAddFiles);
    connect(m_removeBtn, &QPushButton::clicked, this, &StackingDialog::onRemoveSelected);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &StackingDialog::onSelectAll);
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &StackingDialog::onDeselectAll);
    connect(m_setRefBtn, &QPushButton::clicked, this, &StackingDialog::onSetReference);
    connect(m_imageTable, &QTableWidget::itemSelectionChanged, 
            this, &StackingDialog::onTableSelectionChanged);
    connect(m_imageTable, &QTableWidget::cellDoubleClicked,
            this, &StackingDialog::onTableItemDoubleClicked);
    connect(m_imageTable, &QTableWidget::cellDoubleClicked,
            this, &StackingDialog::onTableItemDoubleClicked);
    connect(m_imageTable, &QTableWidget::cellDoubleClicked,
            this, &StackingDialog::onTableItemDoubleClicked);
}

void StackingDialog::setupCometTab() {
    m_cometTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_cometTab);
    
    QLabel* info = new QLabel(tr("Comet Alignment:\n"
        "1. Open First frame, Click 'Pick First', click Comet center.\n"
        "2. Open Last frame, Click 'Pick Last', click Comet center.\n"
        "3. Click 'Compute' to interpolate comet motion."), this);
    info->setWordWrap(true);
    layout->addWidget(info);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_pickCometFirstBtn = new QPushButton(tr("Pick First"), this);
    m_pickCometFirstBtn->setCheckable(true);
    connect(m_pickCometFirstBtn, &QPushButton::clicked, this, &StackingDialog::onPickCometFirst);
    btnLayout->addWidget(m_pickCometFirstBtn);
    
    m_pickCometLastBtn = new QPushButton(tr("Pick Last"), this);
    m_pickCometLastBtn->setCheckable(true);
    connect(m_pickCometLastBtn, &QPushButton::clicked, this, &StackingDialog::onPickCometLast);
    btnLayout->addWidget(m_pickCometLastBtn);
    layout->addLayout(btnLayout);
    
    m_cometStatusLabel = new QLabel(tr("Status: No comet positions set."), this);
    layout->addWidget(m_cometStatusLabel);
    
    m_computeCometBtn = new QPushButton(tr("Compute Comet Shifts"), this);
    m_computeCometBtn->setEnabled(false);
    connect(m_computeCometBtn, &QPushButton::clicked, this, &StackingDialog::onComputeCometShifts);
    layout->addWidget(m_computeCometBtn);
    
    layout->addStretch();
}

void StackingDialog::onPickCometFirst() {
    if (!m_mainWindow) return;
    auto* v = m_mainWindow->currentViewer();
    if (!v) return;
    
    m_pickCometLastBtn->setChecked(false); // Exclusive
    
    if (m_pickCometFirstBtn->isChecked()) {
        v->setPickMode(true);
        connect(v, &ImageViewer::pointPicked, this, &StackingDialog::onViewerPointPicked, Qt::UniqueConnection);
        m_logText->append(tr("Comet: Please click the comet center in the current image (First)."));
    } else {
        v->setPickMode(false);
        disconnect(v, &ImageViewer::pointPicked, this, &StackingDialog::onViewerPointPicked);
    }
}

void StackingDialog::onPickCometLast() {
    if (!m_mainWindow) return;
    auto* v = m_mainWindow->currentViewer();
    if (!v) return;
    
    m_pickCometFirstBtn->setChecked(false);
    
    if (m_pickCometLastBtn->isChecked()) {
        v->setPickMode(true);
        connect(v, &ImageViewer::pointPicked, this, &StackingDialog::onViewerPointPicked, Qt::UniqueConnection);
        m_logText->append(tr("Comet: Please click the comet center in the current image (Last)."));
    } else {
        v->setPickMode(false);
        disconnect(v, &ImageViewer::pointPicked, this, &StackingDialog::onViewerPointPicked);
    }
}

void StackingDialog::onViewerPointPicked(QPointF p) {
    if (!m_sequence) return;
    
    auto* v = m_mainWindow->currentViewer();
    if (!v) return;
    
    // Attempt to identify which image index matches the viewer
    // Simple approach: Match by filename if the viewer has one, or assume user loaded the right one
    // Ideally user selects row in table, then double clicks to view, then picks.
    // We can use the table selection as the "Index".
    int currentRow = m_imageTable->currentRow();
    if (currentRow < 0 || currentRow >= m_sequence->count()) {
        m_logText->append(tr("Error: Please select the corresponding image row in the table first."));
        v->setPickMode(false);
        if (m_pickCometFirstBtn->isChecked()) m_pickCometFirstBtn->setChecked(false);
        if (m_pickCometLastBtn->isChecked()) m_pickCometLastBtn->setChecked(false);
        return;
    }
    
    auto& img = m_sequence->images()[currentRow]; // Access non-const
    img.registration.cometX = p.x();
    img.registration.cometY = p.y();
    
    QString status;
    if (m_pickCometFirstBtn->isChecked()) {
        m_cometRef1Index = currentRow;
        status = tr("Ref 1 set to frame %1 (x=%2, y=%3)").arg(currentRow+1).arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1);
        m_pickCometFirstBtn->setChecked(false);
    } else if (m_pickCometLastBtn->isChecked()) {
        m_cometRef2Index = currentRow;
        status = tr("Ref 2 set to frame %1 (x=%2, y=%3)").arg(currentRow+1).arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1);
        m_pickCometLastBtn->setChecked(false);
    }
    
    v->setPickMode(false);
    disconnect(v, &ImageViewer::pointPicked, this, &StackingDialog::onViewerPointPicked);
    
    m_logText->append(status);
    m_cometStatusLabel->setText(status);
    
    if (m_cometRef1Index >= 0 && m_cometRef2Index >= 0 && m_cometRef1Index != m_cometRef2Index) {
        m_computeCometBtn->setEnabled(true);
    }
}

void StackingDialog::onComputeCometShifts() {
    if (!m_sequence) return;
    
    if (m_sequence->computeCometShifts(m_cometRef1Index, m_cometRef2Index)) {
        m_logText->append(tr("Comet shifts computed successfully."));
        // Update table to show shifts or custom column?
        // For now just Log.
        QMessageBox::information(this, tr("Success"), tr("Comet shifts applied. You can now stack with 'Comet' mode (ensure registration is done)."));
    } else {
         m_logText->append(tr("Error computing comet shifts. Check dates/keywords."));
         QMessageBox::warning(this, tr("Error"), tr("Failed to compute shifts. Check DATE-OBS headers."));
    }
}

void StackingDialog::setupPlotTab() {
    m_plotTab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_plotTab);
    
    // Controls
    QHBoxLayout* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(tr("Metric:"), this));
    
    m_plotTypeCombo = new QComboBox(this);
    m_plotTypeCombo->addItem(tr("FWHM"), 0);
    m_plotTypeCombo->addItem(tr("Roundness"), 1);
    m_plotTypeCombo->addItem(tr("Background"), 2);
    m_plotTypeCombo->addItem(tr("Star Count"), 3);
    m_plotTypeCombo->addItem(tr("Quality Score"), 4);
    controls->addWidget(m_plotTypeCombo);
    
    controls->addStretch();
    layout->addLayout(controls);
    
    // Plot
    m_plotWidget = new SimplePlotWidget(this);
    layout->addWidget(m_plotWidget, 1);
    
    // Connect
    connect(m_plotTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StackingDialog::onPlotTypeChanged);
            
    // Bidirectional sync
    connect(m_plotWidget, &SimplePlotWidget::pointSelected, this, [this](int index) {
        if (!m_sequence || index < 0 || index >= m_sequence->count()) return;
        m_imageTable->selectRow(index);
    });
}

void StackingDialog::setupParametersGroup() {
    m_paramsGroup = new QGroupBox(tr("Stacking Parameters"), this);
    QGridLayout* layout = new QGridLayout(m_paramsGroup);
    
    int row = 0;
    
    // Method
    layout->addWidget(new QLabel(tr("Method:"), this), row, 0);
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Mean"), static_cast<int>(Stacking::Method::Mean));
    m_methodCombo->addItem(tr("Median"), static_cast<int>(Stacking::Method::Median));
    m_methodCombo->addItem(tr("Sum"), static_cast<int>(Stacking::Method::Sum));
    m_methodCombo->addItem(tr("Maximum"), static_cast<int>(Stacking::Method::Max));
    m_methodCombo->addItem(tr("Minimum"), static_cast<int>(Stacking::Method::Min));
    layout->addWidget(m_methodCombo, row++, 1);
    
    // Rejection
    layout->addWidget(new QLabel(tr("Rejection:"), this), row, 0);
    m_rejectionCombo = new QComboBox(this);
    m_rejectionCombo->addItem(tr("None"), static_cast<int>(Stacking::Rejection::None));
    m_rejectionCombo->addItem(tr("Percentile"), static_cast<int>(Stacking::Rejection::Percentile));
    m_rejectionCombo->addItem(tr("Sigma Clipping"), static_cast<int>(Stacking::Rejection::Sigma));
    m_rejectionCombo->addItem(tr("MAD Clipping"), static_cast<int>(Stacking::Rejection::MAD));
    m_rejectionCombo->addItem(tr("Sigma-Median"), static_cast<int>(Stacking::Rejection::SigmaMedian));
    m_rejectionCombo->addItem(tr("Winsorized Sigma"), static_cast<int>(Stacking::Rejection::Winsorized));
    m_rejectionCombo->addItem(tr("Linear Fit"), static_cast<int>(Stacking::Rejection::LinearFit));
    m_rejectionCombo->addItem(tr("Generalized ESD"), static_cast<int>(Stacking::Rejection::GESDT));
    m_rejectionCombo->setCurrentIndex(5); // Winsorized default
    layout->addWidget(m_rejectionCombo, row++, 1);
    
    // Sigma values
    layout->addWidget(new QLabel(tr("Sigma Low:"), this), row, 0);
    m_sigmaLow = new QDoubleSpinBox(this);
    m_sigmaLow->setRange(0.1, 10.0);
    m_sigmaLow->setValue(3.0);
    m_sigmaLow->setDecimals(1);
    layout->addWidget(m_sigmaLow, row++, 1);
    
    layout->addWidget(new QLabel(tr("Sigma High:"), this), row, 0);
    m_sigmaHigh = new QDoubleSpinBox(this);
    m_sigmaHigh->setRange(0.1, 10.0);
    m_sigmaHigh->setValue(3.0);
    m_sigmaHigh->setDecimals(1);
    layout->addWidget(m_sigmaHigh, row++, 1);
    
    // Normalization
    layout->addWidget(new QLabel(tr("Normalization:"), this), row, 0);
    m_normCombo = new QComboBox(this);
    m_normCombo->addItem(tr("None"), static_cast<int>(Stacking::NormalizationMethod::None));
    m_normCombo->addItem(tr("Additive"), static_cast<int>(Stacking::NormalizationMethod::Additive));
    m_normCombo->addItem(tr("Multiplicative"), static_cast<int>(Stacking::NormalizationMethod::Multiplicative));
    m_normCombo->addItem(tr("Additive + Scaling"), static_cast<int>(Stacking::NormalizationMethod::AdditiveScaling));
    m_normCombo->addItem(tr("Multiplicative + Scaling"), static_cast<int>(Stacking::NormalizationMethod::MultiplicativeScaling));
    m_normCombo->setCurrentIndex(3); // Additive+Scaling default
    layout->addWidget(m_normCombo, row++, 1);
    
    // Weighting
    layout->addWidget(new QLabel(tr("Weighting:"), this), row, 0);
    m_weightingCombo = new QComboBox(this);
    m_weightingCombo->addItem(tr("None"), static_cast<int>(Stacking::WeightingType::None));
    m_weightingCombo->addItem(tr("Star Count"), static_cast<int>(Stacking::WeightingType::StarCount));
    m_weightingCombo->addItem(tr("FWHM"), static_cast<int>(Stacking::WeightingType::WeightedFWHM));
    m_weightingCombo->addItem(tr("Noise"), static_cast<int>(Stacking::WeightingType::Noise));
    m_weightingCombo->addItem(tr("Roundness"), static_cast<int>(Stacking::WeightingType::Roundness));
    m_weightingCombo->addItem(tr("Quality"), static_cast<int>(Stacking::WeightingType::Quality));
    m_weightingCombo->setCurrentIndex(3); // Noise default (matches Scripts)
    layout->addWidget(m_weightingCombo, row++, 1);

    // Feathering
    layout->addWidget(new QLabel(tr("Feathering (px):"), this), row, 0);
    m_featherSpin = new QSpinBox(this);
    m_featherSpin->setRange(0, 500);
    m_featherSpin->setValue(0);
    layout->addWidget(m_featherSpin, row++, 1);
    
    // Separator
    layout->addWidget(new QLabel("", this), row++, 0);
    
    // Debayering
    m_debayerCheck = new QCheckBox(tr("Debayer (CFA Images)"), this);
    m_debayerCheck->setToolTip(tr("Debayer images on-the-fly during stacking. Enable this for RAW/CFA images."));
    layout->addWidget(m_debayerCheck, row++, 0, 1, 2);
    
    QHBoxLayout* debayerLayout = new QHBoxLayout();
    debayerLayout->setContentsMargins(20, 0, 0, 0);
    
    m_bayerPatternCombo = new QComboBox(this);
    m_bayerPatternCombo->addItem(tr("RGGB"), static_cast<int>(Preprocessing::BayerPattern::RGGB));
    m_bayerPatternCombo->addItem(tr("BGGR"), static_cast<int>(Preprocessing::BayerPattern::BGGR));
    m_bayerPatternCombo->addItem(tr("GBRG"), static_cast<int>(Preprocessing::BayerPattern::GBRG));
    m_bayerPatternCombo->addItem(tr("GRBG"), static_cast<int>(Preprocessing::BayerPattern::GRBG));
    debayerLayout->addWidget(new QLabel(tr("Pattern:"), this));
    debayerLayout->addWidget(m_bayerPatternCombo);
    
    m_debayerAlgoCombo = new QComboBox(this);
    m_debayerAlgoCombo->addItem(tr("VNG (Best)"), static_cast<int>(Preprocessing::DebayerAlgorithm::VNG));
    m_debayerAlgoCombo->addItem(tr("Bilinear (Fast)"), static_cast<int>(Preprocessing::DebayerAlgorithm::Bilinear));
    debayerLayout->addWidget(new QLabel(tr("Algo:"), this));
    debayerLayout->addWidget(m_debayerAlgoCombo);
    debayerLayout->addStretch();
    
    layout->addLayout(debayerLayout, row++, 0, 1, 2);
    
    connect(m_debayerCheck, &QCheckBox::toggled, this, &StackingDialog::updateParameterVisibility);
    
    // Options
    m_force32BitCheck = new QCheckBox(tr("32-bit output"), this);
    m_force32BitCheck->setChecked(true);
    layout->addWidget(m_force32BitCheck, row++, 0, 1, 2);
    
    m_outputNormCheck = new QCheckBox(tr("Normalize output to [0,1]"), this);
    m_outputNormCheck->setChecked(false); // Default OFF (Matches Scripts)
    layout->addWidget(m_outputNormCheck, row++, 0, 1, 2);
    
    m_equalizeRGBCheck = new QCheckBox(tr("Equalize RGB channels"), this);
    m_equalizeRGBCheck->setChecked(false); // Default OFF (Matches Scripts)
    layout->addWidget(m_equalizeRGBCheck, row++, 0, 1, 2);
    
    m_maximizeFramingCheck = new QCheckBox(tr("Maximize framing"), this);
    m_maximizeFramingCheck->setChecked(false);
    layout->addWidget(m_maximizeFramingCheck, row++, 0, 1, 2);
    
    m_createRejMapsCheck = new QCheckBox(tr("Create rejection maps"), this);
    layout->addWidget(m_createRejMapsCheck, row++, 0, 1, 2);
    
    // Drizzle
    m_drizzleCheck = new QCheckBox(tr("Enable Drizzle 2x"), this);
    layout->addWidget(m_drizzleCheck, row, 0);
    
    QHBoxLayout* drizzleLayout = new QHBoxLayout();
    drizzleLayout->setContentsMargins(0,0,0,0);
    
    m_drizzleScale = new QDoubleSpinBox(this);
    m_drizzleScale->setRange(1.0, 4.0);
    m_drizzleScale->setValue(2.0);
    m_drizzleScale->setSingleStep(0.5);
    m_drizzleScale->setPrefix("x");
    drizzleLayout->addWidget(m_drizzleScale);
    
    m_drizzlePixFrac = new QDoubleSpinBox(this);
    m_drizzlePixFrac->setRange(0.1, 1.0);
    m_drizzlePixFrac->setValue(0.9);
    m_drizzlePixFrac->setSingleStep(0.1);
    m_drizzlePixFrac->setPrefix(tr("PixFrac: "));
    drizzleLayout->addWidget(m_drizzlePixFrac);
    
    layout->addLayout(drizzleLayout, row++, 1);

    connect(m_drizzleCheck, &QCheckBox::toggled, this, &StackingDialog::updateParameterVisibility);
    
    layout->setRowStretch(row, 1);
    
    // Connect signals
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StackingDialog::onMethodChanged);
    connect(m_rejectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StackingDialog::onRejectionChanged);
    connect(m_normCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StackingDialog::onNormalizationChanged);
}

void StackingDialog::setupOutputGroup() {
    m_outputGroup = new QGroupBox(tr("Output"), this);
    QHBoxLayout* layout = new QHBoxLayout(m_outputGroup);
    
    layout->addWidget(new QLabel(tr("Save to:"), this));
    
    m_outputPath = new QLineEdit(this);
    m_outputPath->setPlaceholderText(tr("result_stacked.fit"));
    layout->addWidget(m_outputPath, 1);
    
    m_browseBtn = new QPushButton(tr("Browse..."), this);
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this,
            tr("Save Stacked Image"),
            m_outputPath->text().isEmpty() ? "result_stacked.fit" : m_outputPath->text(),
            tr("FITS Files (*.fit *.fits);;All Files (*)"));
        if (!path.isEmpty()) {
            m_outputPath->setText(path);
        }
    });
    layout->addWidget(m_browseBtn);
}

void StackingDialog::setupProgressGroup() {
    m_progressGroup = new QGroupBox(tr("Progress"), this);
    QVBoxLayout* layout = new QVBoxLayout(m_progressGroup);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);
    
    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    m_logText->setMaximumHeight(100);
    m_logText->setStyleSheet("background-color: #1e1e1e; color: #ffffff;");
    layout->addWidget(m_logText);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_startBtn = new QPushButton(tr("Start Stacking"), this);
    m_startBtn->setMinimumWidth(120);
    connect(m_startBtn, &QPushButton::clicked, this, &StackingDialog::onStartStacking);
    buttonLayout->addWidget(m_startBtn);
    
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &StackingDialog::onCancel);
    buttonLayout->addWidget(m_cancelBtn);
    
    layout->addLayout(buttonLayout);
}

//=============================================================================
// SEQUENCE MANAGEMENT
//=============================================================================

void StackingDialog::onLoadSequence() {
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Select Image Folder"),
        QString(),
        QFileDialog::ShowDirsOnly);
    
    if (dir.isEmpty()) return;
    
    m_sequence = std::make_unique<Stacking::ImageSequence>();
    
    m_logText->append(tr("Loading sequence from: %1").arg(dir));
    
    // Support multiple extensions
    QStringList filters;
    filters << "*.fit" << "*.fits" << "*.fts" << "*.tif" << "*.tiff";
    
    bool success = m_sequence->loadFromDirectory(dir, filters,
        [this](const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            m_logText->append(msg);
            QApplication::processEvents();
        });
    
    if (success) {
        updateTable();
        updateSummary();
        m_logText->append(tr("Loaded %1 images").arg(m_sequence->count()));
    } else {
        m_logText->append(tr("<span style='color:red'>Failed to load sequence or no images found</span>"));
        m_sequence.reset();
    }
    
    m_progressBar->setValue(0);
}

void StackingDialog::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this,
        tr("Select Image Files"),
        QString(),
        tr("FITS Files (*.fit *.fits *.fts);;TIFF Files (*.tif *.tiff);;All Files (*)"));
    
    if (files.isEmpty()) return;
    
    if (!m_sequence) {
        m_sequence = std::make_unique<Stacking::ImageSequence>();
    }
    
    m_sequence->loadFromFiles(files);
    updateTable();
    updateSummary();
}

void StackingDialog::onRemoveSelected() {
    if (!m_sequence) return;
    
    QList<QTableWidgetItem*> selected = m_imageTable->selectedItems();
    QSet<int> rowsToRemove;
    
    for (auto* item : selected) {
        rowsToRemove.insert(item->row());
    }
    
    // Remove in reverse order to preserve indices
    QList<int> sortedRows = rowsToRemove.values();
    std::sort(sortedRows.rbegin(), sortedRows.rend());
    
    for (int row : sortedRows) {
        if (row < m_sequence->count()) {
            m_sequence->removeImage(row);
        }
    }
    
    updateTable();
    updateSummary();
}

void StackingDialog::onSelectAll() {
    if (m_sequence) {
        m_sequence->selectAll();
        updateTable();
        updateSummary();
    }
}

void StackingDialog::onDeselectAll() {
    if (m_sequence) {
        m_sequence->deselectAll();
        updateTable();
        updateSummary();
    }
}

void StackingDialog::onSetReference() {
    if (!m_sequence) return;
    
    int row = m_imageTable->currentRow();
    if (row >= 0 && row < m_sequence->count()) {
        m_sequence->setReferenceImage(row);
        updateTable();
    }
}

void StackingDialog::setSequence(std::unique_ptr<Stacking::ImageSequence> sequence) {
    m_sequence = std::move(sequence);
    updateTable();
    updateSummary();
}

void StackingDialog::updateTable() {
    m_imageTable->setRowCount(0);
    
    if (!m_sequence) return;
    
    m_imageTable->setRowCount(m_sequence->count());
    
    for (int i = 0; i < m_sequence->count(); ++i) {
        const auto& img = m_sequence->image(i);
        
        // Checkbox (selected)
        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setCheckState(img.selected ? Qt::Checked : Qt::Unchecked);
        m_imageTable->setItem(i, 0, checkItem);
        
        // Filename
        QString filename = img.fileName();
        if (i == m_sequence->referenceImage()) {
            filename += " ★";  // Mark reference
        }
        m_imageTable->setItem(i, 1, new QTableWidgetItem(filename));
        
        // Dimensions
        m_imageTable->setItem(i, 2, new QTableWidgetItem(
            QString("%1×%2").arg(img.width).arg(img.height)));
        
        // Exposure
        m_imageTable->setItem(i, 3, new QTableWidgetItem(
            img.exposure > 0 ? QString("%1s").arg(img.exposure, 0, 'f', 1) : "-"));
        
        // FWHM
        m_imageTable->setItem(i, 4, new QTableWidgetItem(
            img.quality.hasMetrics ? QString("%1").arg(img.quality.fwhm, 0, 'f', 2) : "-"));
        
        // Quality
        m_imageTable->setItem(i, 5, new QTableWidgetItem(
            img.quality.hasMetrics ? QString("%1").arg(img.quality.quality, 0, 'f', 2) : "-"));
        
        // Shift
        if (img.registration.hasRegistration) {
            m_imageTable->setItem(i, 6, new QTableWidgetItem(
                QString("%1, %2").arg(img.registration.shiftX, 0, 'f', 1)
                                .arg(img.registration.shiftY, 0, 'f', 1)));
        } else {
            m_imageTable->setItem(i, 6, new QTableWidgetItem("-"));
        }
    }
    
    // Resize columns to contents, but allow filename column to stretch
    m_imageTable->resizeColumnsToContents();
    
    // Apply elide text for filename column
    QFontMetrics fm(m_imageTable->font());
    for (int i = 0; i < m_imageTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_imageTable->item(i, 1);
        if (item && m_imageTable->columnWidth(1) > 0) {
            QString text = item->text();
            QString elidedText = fm.elidedText(text, Qt::ElideRight, m_imageTable->columnWidth(1) - 10);
            item->setText(elidedText);
        }
    }
    
    updatePlot();
}

void StackingDialog::updatePlot() {
    if (!m_sequence || !m_sequence->hasQualityMetrics()) {
        m_plotWidget->setData({}, {});
        m_plotWidget->setTitle(tr("No quality data available"));
        return;
    }
    
    QVector<double> x, y;
    int count = m_sequence->count();
    x.reserve(count);
    y.reserve(count);
    
    int metricType = m_plotTypeCombo->currentData().toInt();
    QString yLabel;
    
    for (int i = 0; i < count; ++i) {
        const auto& img = m_sequence->image(i);
        x.append(i + 1); // Frame numbers start at 1
        
        double val = 0.0;
        switch (metricType) {
            case 0: val = img.quality.fwhm; break;
            case 1: val = img.quality.roundness; break;
            case 2: val = img.quality.background; break;
            case 3: val = static_cast<double>(img.quality.starCount); break;
            case 4: val = img.quality.quality; break;
        }
        y.append(val);
    }
    
    switch (metricType) {
        case 0: yLabel = "FWHM (px)"; break;
        case 1: yLabel = "Roundness"; break;
        case 2: yLabel = "Background (ADU)"; break;
        case 3: yLabel = "Stars"; break;
        case 4: yLabel = "Score"; break;
    }
    
    m_plotWidget->setData(x, y);
    m_plotWidget->setAxisLabels("Frame #", yLabel);
    m_plotWidget->setTitle(tr("%1 vs Frame").arg(m_plotTypeCombo->currentText()));
    
    // Sync selection
    QVector<int> selected;
    for (int i = 0; i < count; ++i) {
        if (m_sequence->image(i).selected) {
            selected.append(i);
        }
    }
    m_plotWidget->setSelection(selected);
}

void StackingDialog::onPlotTypeChanged(int index) {
    Q_UNUSED(index);
    updatePlot();
}

void StackingDialog::updateSummary() {
    if (!m_sequence || m_sequence->count() == 0) {
        m_sequenceSummary->setText(tr("No sequence loaded"));
        return;
    }
    
    int total = m_sequence->count();
    int selected = m_sequence->selectedCount();
    double exposure = m_sequence->totalExposure();
    
    m_sequenceSummary->setText(tr("%1/%2 images selected, %3 total exposure")
        .arg(selected).arg(total)
        .arg(exposure > 3600 ? QString("%1h").arg(exposure/3600, 0, 'f', 1) :
             QString("%1m").arg(exposure/60, 0, 'f', 1)));
}

//=============================================================================
// PARAMETER HANDLING
//=============================================================================

void StackingDialog::onMethodChanged(int index) {
    Q_UNUSED(index);
    updateParameterVisibility();
}

void StackingDialog::onRejectionChanged(int index) {
    Q_UNUSED(index);
    updateParameterVisibility();
}

void StackingDialog::onNormalizationChanged(int index) {
    Q_UNUSED(index);
}

void StackingDialog::updateParameterVisibility() {
    auto method = static_cast<Stacking::Method>(
        m_methodCombo->currentData().toInt());
    
    // Rejection only for Mean
    bool showRejection = (method == Stacking::Method::Mean);
    m_rejectionCombo->setEnabled(showRejection);
    m_sigmaLow->setEnabled(showRejection && m_rejectionCombo->currentIndex() > 0);
    m_sigmaHigh->setEnabled(showRejection && m_rejectionCombo->currentIndex() > 0);
    
    // Normalization for Mean/Median
    bool showNorm = (method == Stacking::Method::Mean || 
                     method == Stacking::Method::Median);
    m_normCombo->setEnabled(showNorm);
    m_normCombo->setEnabled(showNorm);
    m_weightingCombo->setEnabled(showNorm);
    
    // Drizzle options
    bool drizzleCtx = m_drizzleCheck->isChecked();
    m_drizzleScale->setEnabled(drizzleCtx);
    m_drizzlePixFrac->setEnabled(drizzleCtx);
    
    // Debayer options
    bool debayerCtx = m_debayerCheck->isChecked();
    m_bayerPatternCombo->setEnabled(debayerCtx);
    m_debayerAlgoCombo->setEnabled(debayerCtx);
}

Stacking::StackingParams StackingDialog::gatherParams() const {
    Stacking::StackingParams params;
    
    // Debayer
    params.debayer = m_debayerCheck->isChecked();
    params.bayerPattern = static_cast<Preprocessing::BayerPattern>(
        m_bayerPatternCombo->currentData().toInt());
    params.debayerMethod = static_cast<Preprocessing::DebayerAlgorithm>(
        m_debayerAlgoCombo->currentData().toInt());
    
    params.method = static_cast<Stacking::Method>(
        m_methodCombo->currentData().toInt());
    params.rejection = static_cast<Stacking::Rejection>(
        m_rejectionCombo->currentData().toInt());
    params.normalization = static_cast<Stacking::NormalizationMethod>(
        m_normCombo->currentData().toInt());
    params.weighting = static_cast<Stacking::WeightingType>(
        m_weightingCombo->currentData().toInt());
    
    params.featherDistance = m_featherSpin->value();
    
    params.sigmaLow = static_cast<float>(m_sigmaLow->value());
    params.sigmaHigh = static_cast<float>(m_sigmaHigh->value());
    
    params.force32Bit = m_force32BitCheck->isChecked();
    params.outputNormalization = m_outputNormCheck->isChecked();
    params.equalizeRGB = m_equalizeRGBCheck->isChecked();
    params.maximizeFraming = m_maximizeFramingCheck->isChecked();
    params.createRejectionMaps = m_createRejMapsCheck->isChecked();
    
    params.drizzle = m_drizzleCheck->isChecked();
    params.drizzleScale = m_drizzleScale->value();
    params.drizzlePixFrac = m_drizzlePixFrac->value();
    
    params.filter = static_cast<Stacking::ImageFilter>(
        m_filterCombo->currentData().toInt());
    params.filterParameter = m_filterValue->value();
    
    params.outputFilename = m_outputPath->text();
    if (params.outputFilename.isEmpty()) {
        params.outputFilename = generateOutputFilename();
    }
    
    if (m_sequence) {
        params.refImageIndex = m_sequence->referenceImage();
    }
    
    return params;
}

QString StackingDialog::generateOutputFilename() const {
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return QString("result_%1.fit").arg(timestamp);
}

//=============================================================================
// STACKING EXECUTION
//=============================================================================

void StackingDialog::onStartStacking() {
    if (!m_sequence || m_sequence->selectedCount() < 2) {
        QMessageBox::warning(this, tr("Cannot Stack"),
            tr("Please load at least 2 images to stack."));
        return;
    }
    
    m_isRunning = true;
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_logText->clear();
    
    // Gather parameters
    Stacking::StackingArgs args;
    args.params = gatherParams();
    args.sequence = m_sequence.get();
    
    // Create worker thread
    m_worker = std::make_unique<Stacking::StackingWorker>(std::move(args));
    
    connect(m_worker.get(), &Stacking::StackingWorker::progressChanged,
            this, &StackingDialog::onProgressChanged);
    connect(m_worker.get(), &Stacking::StackingWorker::logMessage,
            this, &StackingDialog::onLogMessage);
    connect(m_worker.get(), &Stacking::StackingWorker::finished,
            this, &StackingDialog::onStackingFinished);
    
    m_logText->append(tr("Starting stacking with %1 images...")
                     .arg(m_sequence->selectedCount()));
    
    m_worker->start();
}

void StackingDialog::onCancel() {
    if (m_worker && m_worker->isRunning()) {
        m_logText->append(tr("Cancelling..."));
        m_worker->requestCancel();
    }
}

void StackingDialog::onProgressChanged([[maybe_unused]] const QString& message, double progress) {
    if (progress >= 0) {
        m_progressBar->setValue(static_cast<int>(progress * 100));
    } else {
        m_progressBar->setRange(0, 0);  // Indeterminate
    }
}

void StackingDialog::onLogMessage(const QString& message, const QString& color) {
    QString finalColor = color;
    if (finalColor.toLower() == "neutral") finalColor = "";

    if (finalColor.isEmpty()) {
        m_logText->append(message);
    } else {
        m_logText->append(QString("<span style='color:%1'>%2</span>")
                         .arg(finalColor, message));
    }
}

void StackingDialog::onStackingFinished(bool success) {
    m_isRunning = false;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(success ? 100 : 0);
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    
    if (success) {
        m_logText->append(tr("<span style='color:green'>Stacking complete!</span>"));
        
        // Get result from worker
        m_result = std::make_unique<ImageBuffer>(std::move(m_worker->args().result));
        emit stackingComplete(m_result.get());
        
        // Save the result to disk
        QString outPath = m_worker->args().params.outputFilename;
        if (!outPath.isEmpty()) {
            // Ensure absolute path
            QFileInfo fi(outPath);
            if (fi.isRelative()) {
                outPath = QDir::current().absoluteFilePath(outPath);
            }
            
            // Determine bit depth
            ImageBuffer::BitDepth depth = ImageBuffer::Depth_32Float;
            if (!m_worker->args().params.force32Bit) {
                depth = ImageBuffer::Depth_16Int;
            }
            
            QString errorMsg;
            if (m_result->save(outPath, "FITS", depth, &errorMsg)) {
                m_logText->append(tr("<b>Saved output to: %1</b>").arg(outPath));
            } else {
                m_logText->append(tr("<span style='color:red'>Failed to save output: %1</span>").arg(errorMsg));
            }
        }
        
        // Save Rejection Maps if generated
        if (m_worker->args().rejectionMaps.isInitialized()) {
             // Reuse outPath base
            if (!outPath.isEmpty()) {
                QFileInfo fi(outPath);
                QString basePath = fi.absolutePath();
                QString baseName = fi.completeBaseName();
                m_worker->args().rejectionMaps.save(basePath, baseName);
                m_logText->append(tr("Rejection maps saved as %1_low.fit / %1_high.fit").arg(baseName));
            }
        }
    } else {
        m_logText->append(tr("<span style='color:red'>Stacking failed or cancelled</span>"));
    }
    
    m_worker.reset();
}

void StackingDialog::onTableSelectionChanged() {
    bool hasSelection = !m_imageTable->selectedItems().isEmpty();
    m_removeBtn->setEnabled(hasSelection);
    m_setRefBtn->setEnabled(hasSelection);
}

void StackingDialog::onTableItemDoubleClicked(int row, int column) {
    Q_UNUSED(column);
    
    // Toggle selection
    if (m_sequence && row < m_sequence->count()) {
        m_sequence->toggleSelection(row);
        
        QTableWidgetItem* item = m_imageTable->item(row, 0);
        if (item) {
            item->setCheckState(m_sequence->image(row).selected ? Qt::Checked : Qt::Unchecked);
        }
        
        updateSummary();
    }
}

