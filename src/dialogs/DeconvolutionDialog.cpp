/*
 * DeconvolutionDialog.cpp
 *
 * UI for single-frame and multi-frame deconvolution workflows.
 */

/*
#include "DeconvolutionDialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QSizePolicy>
#include <QRegularExpression>
#include <QScrollBar>
#include <QApplication>

#include "ImageViewer.h"
#include "MainWindow.h"
#include "StarDetector.h"

// ─────────────────────────────────────────────────────────────────────────────
// MFDeconvWorker
// ─────────────────────────────────────────────────────────────────────────────

MFDeconvWorker::MFDeconvWorker(const MFDeconvParams& params, QObject* parent)
    : QThread(parent), m_params(params)
{}

MFDeconvWorker::~MFDeconvWorker()
{
    requestInterruption();
    wait(5000);
}

void MFDeconvWorker::requestStop()
{
    m_stopRequested = true;
    requestInterruption();
}


// Parse "__PROGRESS__ 0.xxxx msg" messages and convert them to
// progressUpdated(percent 0-100, msg). All other lines are forwarded
// as statusMessage.

void MFDeconvWorker::handleStatusLine(const QString& line)
{
    if (line.startsWith("__PROGRESS__ ")) {
        QString rest = line.mid(13).trimmed();
        QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
        bool ok = false;
        double frac = parts.isEmpty() ? 0.0 : parts[0].toDouble(&ok);
        if (!ok) frac = 0.0;
        QString msg = (parts.size() > 1) ? parts.mid(1).join(' ') : QString();
        int percent = (int)(frac * 100.0);
        emit progressUpdated(percent, msg);
    } else {
        emit statusMessage(line);
    }
}

void MFDeconvWorker::run()
{
    // Configure callback that forwards backend messages to the Qt thread.
    MFDeconvParams params = m_params;
    params.statusCallback = [this](const QString& msg) {
        handleStatusLine(msg);
    };

    MFDeconvResult result = Deconvolution::applyMultiFrame(params);

    if (result.success)
        emit finished(true, "MF deconvolution complete.", result.outputPath);
    else
        emit finished(false, "MF deconvolution failed: " + result.errorMsg, QString());
}

// ─────────────────────────────────────────────────────────────────────────────
// DeconvolutionDialog
// ─────────────────────────────────────────────────────────────────────────────

DeconvolutionDialog::DeconvolutionDialog(ImageViewer* viewer, MainWindow* mw,
                                           QWidget* parent)
    : QDialog(parent), m_viewer(viewer), m_mainWindow(mw)
{
    setWindowTitle(tr("Convolution / Deconvolution"));
    setWindowFlag(Qt::Window, true);
    setModal(false);
    resize(420, 650);
    buildUI();
    connectSignals();
    updatePSFPreview();
}

DeconvolutionDialog::~DeconvolutionDialog()
{
    if (m_mfWorker) {
        m_mfWorker->requestStop();
        m_mfWorker->wait(5000);
        delete m_mfWorker;
    }
    delete m_watcher;
}

void DeconvolutionDialog::setViewer(ImageViewer* viewer)
{
    m_viewer = viewer;
}

void DeconvolutionDialog::closeEvent(QCloseEvent* event)
{
    if (m_mfWorker && m_mfWorker->isRunning()) {
        m_mfWorker->requestStop();
        m_mfWorker->wait(2000);
    }
    QDialog::closeEvent(event);
}

void DeconvolutionDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_viewer && !m_originalBuffer.isValid()) {
        // Snapshot current image as baseline for preview/undo.
        m_originalBuffer = m_viewer->getBuffer();
    }
    updatePSFPreview();
}

// ─────────────────────────────────────────────────────────────────────────────
// buildUI
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    scroll->setWidget(widget);
    mainLayout->addWidget(scroll);

    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs);

    buildConvolutionTab();
    buildDeconvolutionTab();
    buildPSFEstimatorTab();
    buildTVDenoiseTab();
    buildMultiFrameTab();

    // PSF preview chip
    m_convPSFLabel = new QLabel;
    m_convPSFLabel->setFixedSize(64, 64);
    m_convPSFLabel->setStyleSheet("border: 1px solid #888;");
    layout->addWidget(m_convPSFLabel, 0, Qt::AlignHCenter);

    // Strength slider (single image only)
    auto* srow = new QHBoxLayout;
    srow->addWidget(new QLabel(tr("Strength:")));
    m_strengthSlider = new QSlider(Qt::Horizontal);
    m_strengthSlider->setRange(0, 100);
    m_strengthSlider->setValue(100);
    srow->addWidget(m_strengthSlider);
    m_strengthSpin = new QDoubleSpinBox;
    m_strengthSpin->setRange(0.0, 1.0);
    m_strengthSpin->setSingleStep(0.01);
    m_strengthSpin->setValue(1.0);
    m_strengthSpin->setFixedWidth(60);
    srow->addWidget(m_strengthSpin);
    layout->addLayout(srow);

    // Single image buttons
    auto* row1 = new QHBoxLayout;
    m_previewBtn = new QPushButton(tr("Preview"));
    m_undoBtn    = new QPushButton(tr("Undo"));
    row1->addWidget(m_previewBtn);
    row1->addWidget(m_undoBtn);
    layout->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    m_closeBtn   = new QPushButton(tr("Close"));
    m_pushBtn    = new QPushButton(tr("Apply"));
    row2->addWidget(m_closeBtn);
    row2->addWidget(m_pushBtn);
    layout->addLayout(row2);

    layout->addStretch();

    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet("color:#fff;background:#333;padding:4px;");
    m_statusLabel->setFixedHeight(24);
    layout->addWidget(m_statusLabel);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildConvolutionTab
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::buildConvolutionTab()
{
    auto* tab    = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* form   = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_convRadiusSpin = new QDoubleSpinBox; m_convRadiusSpin->setRange(0.1, 200.0); m_convRadiusSpin->setValue(5.0);  m_convRadiusSpin->setSuffix(" px");
    m_convShapeSpin  = new QDoubleSpinBox; m_convShapeSpin->setRange(0.1, 10.0);   m_convShapeSpin->setValue(2.0);
    m_convAspectSpin = new QDoubleSpinBox; m_convAspectSpin->setRange(0.1, 10.0);  m_convAspectSpin->setValue(1.0);
    m_convRotSpin    = new QDoubleSpinBox; m_convRotSpin->setRange(0.0, 360.0);    m_convRotSpin->setValue(0.0);    m_convRotSpin->setSuffix("°");

    form->addRow(tr("Radius:"),       m_convRadiusSpin);
    form->addRow(tr("Kurtosis (σ):"), m_convShapeSpin);
    form->addRow(tr("Aspect Ratio:"), m_convAspectSpin);
    form->addRow(tr("Rotation:"),     m_convRotSpin);
    layout->addLayout(form);
    layout->addStretch();
    m_tabs->addTab(tab, tr("Convolution"));
}

// ─────────────────────────────────────────────────────────────────────────────
// buildDeconvolutionTab
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::buildDeconvolutionTab()
{
    auto* tab    = new QWidget;
    auto* layout = new QVBoxLayout(tab);

    // Algo combo
    auto* algoRow = new QHBoxLayout;
    algoRow->addWidget(new QLabel(tr("Algorithm:")));
    m_algoCombo = new QComboBox;
    m_algoCombo->addItems({"Richardson-Lucy", "Wiener", "Larson-Sekanina", "Van Cittert"});
    algoRow->addWidget(m_algoCombo);
    algoRow->addStretch();
    layout->addLayout(algoRow);

    // PSF sliders (shared RL/Wiener)
    m_psfParamGroup = new QWidget;
    auto* psfForm = new QFormLayout(m_psfParamGroup);
    psfForm->setLabelAlignment(Qt::AlignRight);
    m_rlPsfRadiusSpin  = new QDoubleSpinBox; m_rlPsfRadiusSpin->setRange(0.1, 100.0); m_rlPsfRadiusSpin->setValue(3.0);  m_rlPsfRadiusSpin->setSuffix(" px");
    m_rlPsfShapeSpin   = new QDoubleSpinBox; m_rlPsfShapeSpin->setRange(0.1, 10.0);   m_rlPsfShapeSpin->setValue(2.0);
    m_rlPsfAspectSpin  = new QDoubleSpinBox; m_rlPsfAspectSpin->setRange(0.1, 10.0);  m_rlPsfAspectSpin->setValue(1.0);
    m_rlPsfRotSpin     = new QDoubleSpinBox; m_rlPsfRotSpin->setRange(0.0, 360.0);    m_rlPsfRotSpin->setValue(0.0);     m_rlPsfRotSpin->setSuffix("°");
    psfForm->addRow(tr("PSF Radius:"),   m_rlPsfRadiusSpin);
    psfForm->addRow(tr("PSF Kurtosis:"), m_rlPsfShapeSpin);
    psfForm->addRow(tr("PSF Aspect:"),   m_rlPsfAspectSpin);
    psfForm->addRow(tr("PSF Rotation:"), m_rlPsfRotSpin);
    layout->addWidget(m_psfParamGroup);

    // Custom PSF bar (visible when stellar PSF is active)
    m_customPsfBar = new QWidget;
    auto* barLayout = new QHBoxLayout(m_customPsfBar);
    barLayout->setContentsMargins(0,0,0,0);
    m_customPsfLabel = new QLabel(tr("Using Stellar PSF"));
    m_customPsfLabel->setStyleSheet("color:#fff;background:#007acc;padding:2px;");
    m_disableCustomBtn = new QPushButton(tr("Disable Stellar PSF"));
    barLayout->addWidget(m_customPsfLabel);
    barLayout->addWidget(m_disableCustomBtn);
    barLayout->addStretch();
    m_customPsfBar->setVisible(false);
    layout->addWidget(m_customPsfBar);

    // RL widget
    m_rlWidget = new QWidget;
    auto* rlForm = new QFormLayout(m_rlWidget);
    rlForm->setLabelAlignment(Qt::AlignRight);
    m_rlIterSpin = new QDoubleSpinBox; m_rlIterSpin->setRange(1, 200); m_rlIterSpin->setValue(30);
    m_rlRegCombo = new QComboBox; m_rlRegCombo->addItems({"None (Plain R–L)", "Tikhonov (L2)", "Total Variation (TV)"});
    m_rlClipCheck = new QCheckBox(tr("Enable de-ring")); m_rlClipCheck->setChecked(true);
    m_rlLumCheck  = new QCheckBox(tr("Deconvolve L* Only")); m_rlLumCheck->setChecked(true);
    rlForm->addRow(tr("Iterations:"),     m_rlIterSpin);
    rlForm->addRow(tr("Regularization:"), m_rlRegCombo);
    rlForm->addRow(QString(), m_rlClipCheck);
    rlForm->addRow(QString(), m_rlLumCheck);
    layout->addWidget(m_rlWidget);

    // Wiener widget
    m_wienerWidget = new QWidget;
    auto* wForm = new QFormLayout(m_wienerWidget);
    wForm->setLabelAlignment(Qt::AlignRight);
    m_wienerNsrSpin = new QDoubleSpinBox; m_wienerNsrSpin->setRange(0.0, 1.0); m_wienerNsrSpin->setSingleStep(0.001); m_wienerNsrSpin->setValue(0.01);
    m_wienerRegCombo = new QComboBox; m_wienerRegCombo->addItems({"None (Classical Wiener)", "Tikhonov (L2)"});
    m_wienerLumCheck = new QCheckBox(tr("Deconvolve L* Only")); m_wienerLumCheck->setChecked(true);
    m_wienerDeringCheck = new QCheckBox(tr("Enable de-ring")); m_wienerDeringCheck->setChecked(true);
    wForm->addRow(tr("Noise/Signal (λ):"), m_wienerNsrSpin);
    wForm->addRow(tr("Regularization:"),   m_wienerRegCombo);
    wForm->addRow(QString(), m_wienerLumCheck);
    wForm->addRow(QString(), m_wienerDeringCheck);
    m_wienerWidget->setVisible(false);
    layout->addWidget(m_wienerWidget);

    // LS widget
    m_lsWidget = new QWidget;
    auto* lsForm = new QFormLayout(m_lsWidget);
    lsForm->setLabelAlignment(Qt::AlignRight);
    m_lsRadStepSpin = new QDoubleSpinBox; m_lsRadStepSpin->setRange(0.0, 50.0); m_lsRadStepSpin->setSuffix(" px");
    m_lsAngStepSpin = new QDoubleSpinBox; m_lsAngStepSpin->setRange(0.1, 360.0); m_lsAngStepSpin->setValue(1.0); m_lsAngStepSpin->setSuffix("°");
    m_lsOpCombo    = new QComboBox; m_lsOpCombo->addItems({"Divide", "Subtract"});
    m_lsBlendCombo = new QComboBox; m_lsBlendCombo->addItems({"SoftLight", "Screen"});
    lsForm->addRow(tr("Radial Step (px):"), m_lsRadStepSpin);
    lsForm->addRow(tr("Angular Step (°):"), m_lsAngStepSpin);
    lsForm->addRow(tr("LS Operator:"),      m_lsOpCombo);
    lsForm->addRow(tr("Blend Mode:"),       m_lsBlendCombo);
    m_lsWidget->setVisible(false);
    layout->addWidget(m_lsWidget);

    // VC widget
    m_vcWidget = new QWidget;
    auto* vcForm = new QFormLayout(m_vcWidget);
    vcForm->setLabelAlignment(Qt::AlignRight);
    m_vcIterSpin  = new QDoubleSpinBox; m_vcIterSpin->setRange(1, 1000); m_vcIterSpin->setValue(10);
    m_vcRelaxSpin = new QDoubleSpinBox; m_vcRelaxSpin->setRange(0.0, 1.0); m_vcRelaxSpin->setSingleStep(0.01);
    vcForm->addRow(tr("Iterations:"),      m_vcIterSpin);
    vcForm->addRow(tr("Relaxation (0–1):"), m_vcRelaxSpin);
    m_vcWidget->setVisible(false);
    layout->addWidget(m_vcWidget);

    layout->addStretch();
    m_tabs->addTab(tab, tr("Deconvolution"));

    // Update visibility when algorithm changes
    connect(m_algoCombo, &QComboBox::currentTextChanged, this, &DeconvolutionDialog::onAlgorithmChanged);
    onAlgorithmChanged(m_algoCombo->currentText());
}

// ─────────────────────────────────────────────────────────────────────────────
// buildPSFEstimatorTab
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::buildPSFEstimatorTab()
{
    auto* tab    = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* form   = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_sepThreshSpin    = new QDoubleSpinBox; m_sepThreshSpin->setRange(1.0, 5.0);      m_sepThreshSpin->setValue(2.5);   m_sepThreshSpin->setSuffix(" σ");
    m_sepMinAreaSpin   = new QSpinBox;       m_sepMinAreaSpin->setRange(1, 100);        m_sepMinAreaSpin->setValue(5);
    m_sepSatSpin       = new QDoubleSpinBox; m_sepSatSpin->setRange(1000, 100000);      m_sepSatSpin->setValue(50000);    m_sepSatSpin->setSuffix(" ADU");
    m_sepMaxStarsSpin  = new QSpinBox;       m_sepMaxStarsSpin->setRange(1, 500);       m_sepMaxStarsSpin->setValue(50);
    m_sepStampSpin     = new QSpinBox;       m_sepStampSpin->setRange(5, 50);           m_sepStampSpin->setValue(15);

    form->addRow(tr("Detection σ:"),       m_sepThreshSpin);
    form->addRow(tr("Min Area (px²):"),    m_sepMinAreaSpin);
    form->addRow(tr("Saturation Cutoff:"), m_sepSatSpin);
    form->addRow(tr("Max Stars:"),         m_sepMaxStarsSpin);
    form->addRow(tr("Half-Width (px):"),   m_sepStampSpin);
    layout->addLayout(form);

    auto* hBtn = new QHBoxLayout;
    m_sepRunBtn  = new QPushButton(tr("Run SEP Extraction"));
    m_sepSaveBtn = new QPushButton(tr("Save PSF…"));
    m_sepUseBtn  = new QPushButton(tr("Use as Current PSF"));
    hBtn->addWidget(m_sepRunBtn);
    hBtn->addWidget(m_sepSaveBtn);
    hBtn->addWidget(m_sepUseBtn);
    layout->addLayout(hBtn);

    layout->addWidget(new QLabel(tr("Estimated PSF (64×64):")));
    m_sepPsfPreview = new QLabel;
    m_sepPsfPreview->setFixedSize(64, 64);
    m_sepPsfPreview->setStyleSheet("border: 1px solid #888;");
    layout->addWidget(m_sepPsfPreview, 0, Qt::AlignHCenter);
    layout->addStretch();
    m_tabs->addTab(tab, tr("PSF Estimator"));
}

// ─────────────────────────────────────────────────────────────────────────────
// buildTVDenoiseTab
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::buildTVDenoiseTab()
{
    auto* tab    = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    auto* form   = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_tvWeightSpin = new QDoubleSpinBox; m_tvWeightSpin->setRange(0.0, 1.0); m_tvWeightSpin->setSingleStep(0.01); m_tvWeightSpin->setValue(0.1);
    m_tvIterSpin   = new QDoubleSpinBox; m_tvIterSpin->setRange(1, 100);    m_tvIterSpin->setValue(10);
    m_tvMultiCheck = new QCheckBox(tr("Multi-channel")); m_tvMultiCheck->setChecked(true);

    form->addRow(tr("TV Weight:"),      m_tvWeightSpin);
    form->addRow(tr("Max Iterations:"), m_tvIterSpin);
    form->addRow(QString(), m_tvMultiCheck);
    layout->addLayout(form);
    layout->addStretch();
    m_tabs->addTab(tab, tr("TV Denoise"));
}

// ─────────────────────────────────────────────────────────────────────────────
// buildMultiFrameTab
// Exposes all MFDeconv parameters.
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::buildMultiFrameTab()
{
    auto* tab       = new QWidget;
    auto* outerScrl = new QScrollArea;
    outerScrl->setWidgetResizable(true);
    outerScrl->setWidget(tab);

    auto* layout = new QVBoxLayout(tab);

    // ──────────────────────────────────────────────────────────────────────
    // 1. Frame list
    // ──────────────────────────────────────────────────────────────────────
    auto* frameGroup = new QGroupBox(tr("Aligned Frames"));
    auto* frameLayout = new QVBoxLayout(frameGroup);

    m_mfFrameList = new QListWidget;
    m_mfFrameList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_mfFrameList->setMinimumHeight(120);
    frameLayout->addWidget(m_mfFrameList);

    auto* frameBtn = new QHBoxLayout;
    m_mfAddBtn     = new QPushButton(tr("Add Frames…"));
    m_mfRemoveBtn  = new QPushButton(tr("Remove"));
    m_mfClearBtn   = new QPushButton(tr("Clear All"));
    m_mfMoveUpBtn  = new QPushButton(tr("▲"));
    m_mfMoveDownBtn= new QPushButton(tr("▼"));
    m_mfMoveUpBtn->setFixedWidth(30); m_mfMoveDownBtn->setFixedWidth(30);
    frameBtn->addWidget(m_mfAddBtn); frameBtn->addWidget(m_mfRemoveBtn);
    frameBtn->addWidget(m_mfClearBtn); frameBtn->addStretch();
    frameBtn->addWidget(m_mfMoveUpBtn); frameBtn->addWidget(m_mfMoveDownBtn);
    frameLayout->addLayout(frameBtn);

    m_mfFrameCountLabel = new QLabel(tr("0 frames loaded"));
    frameLayout->addWidget(m_mfFrameCountLabel);
    layout->addWidget(frameGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 2. Output path
    // ──────────────────────────────────────────────────────────────────────
    auto* outGroup  = new QGroupBox(tr("Output"));
    auto* outLayout = new QFormLayout(outGroup);
    auto* outRow    = new QHBoxLayout;
    m_mfOutputEdit  = new QLineEdit;
    m_mfBrowseOutBtn= new QPushButton(tr("Browse…"));
    outRow->addWidget(m_mfOutputEdit); outRow->addWidget(m_mfBrowseOutBtn);
    outLayout->addRow(tr("Output path:"), outRow);
    layout->addWidget(outGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 3. Base parameters — iters, kappa, relax, loss
    // ──────────────────────────────────────────────────────────────────────
    auto* baseGroup  = new QGroupBox(tr("Iteration Parameters"));
    auto* baseForm   = new QFormLayout(baseGroup);

    m_mfItersSpin   = new QSpinBox;       m_mfItersSpin->setRange(1, 500);     m_mfItersSpin->setValue(20);
    m_mfMinItersSpin= new QSpinBox;       m_mfMinItersSpin->setRange(1, 100);  m_mfMinItersSpin->setValue(3);
    m_mfKappaSpin   = new QDoubleSpinBox; m_mfKappaSpin->setRange(1.01, 100);  m_mfKappaSpin->setValue(2.0);   m_mfKappaSpin->setSingleStep(0.1);
    m_mfRelaxSpin   = new QDoubleSpinBox; m_mfRelaxSpin->setRange(0.0, 1.0);   m_mfRelaxSpin->setValue(0.7);   m_mfRelaxSpin->setSingleStep(0.05);

    baseForm->addRow(tr("Max Iterations (iters=):"),    m_mfItersSpin);
    baseForm->addRow(tr("Min Iterations (min_iters=):"), m_mfMinItersSpin);
    baseForm->addRow(tr("Kappa (ratio clamp):"),         m_mfKappaSpin);
    baseForm->addRow(tr("Relaxation (relax=):"),         m_mfRelaxSpin);

    // Loss function
    auto* rhoRow = new QHBoxLayout;
    m_mfRhoCombo = new QComboBox;
    m_mfRhoCombo->addItems({"huber", "l2"});
    rhoRow->addWidget(new QLabel(tr("Loss (rho=):"))); rhoRow->addWidget(m_mfRhoCombo); rhoRow->addStretch();
    baseForm->addRow(rhoRow);

    m_mfHuberDeltaLabel = new QLabel(tr("Huber δ (huber_delta=):"));
    m_mfHuberDeltaSpin  = new QDoubleSpinBox;
    m_mfHuberDeltaSpin->setRange(-10.0, 10.0);
    m_mfHuberDeltaSpin->setValue(0.0);
    m_mfHuberDeltaSpin->setSingleStep(0.1);
    m_mfHuberDeltaSpin->setToolTip(tr(">0 absolute | <0 auto (|delta| x RMS via MAD) | 0 = flat (like L2)"));
    baseForm->addRow(m_mfHuberDeltaLabel, m_mfHuberDeltaSpin);

    // Color mode
    m_mfColorModeCombo = new QComboBox;
    m_mfColorModeCombo->addItems({"luma", "perchannel"});
    m_mfColorModeCombo->setToolTip(tr("luma: process ITU-R BT.709 luminance\n"
                                       "perchannel: process R, G, B independently"));
    baseForm->addRow(tr("Color Mode (color_mode=):"), m_mfColorModeCombo);

    layout->addWidget(baseGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 4. Seed mode
    // ──────────────────────────────────────────────────────────────────────
    auto* seedGroup  = new QGroupBox(tr("Seed (seed_mode=)"));
    auto* seedLayout = new QVBoxLayout(seedGroup);

    m_mfSeedBtnGroup = new QButtonGroup(this);
    m_mfSeedRobustRadio = new QRadioButton(tr("Robust (bootstrap + +/-4*MAD clip + sigma-clipped Welford)"));
    m_mfSeedMedianRadio = new QRadioButton(tr("Median (tiled exact median, RAM-bounded)"));
    m_mfSeedRobustRadio->setChecked(true);
    m_mfSeedBtnGroup->addButton(m_mfSeedRobustRadio, 0);
    m_mfSeedBtnGroup->addButton(m_mfSeedMedianRadio, 1);
    seedLayout->addWidget(m_mfSeedRobustRadio);
    seedLayout->addWidget(m_mfSeedMedianRadio);

    auto* seedParamForm = new QFormLayout;
    m_mfBootstrapSpin = new QSpinBox;       m_mfBootstrapSpin->setRange(1, 200); m_mfBootstrapSpin->setValue(20);
    m_mfClipSigmaSpin = new QDoubleSpinBox; m_mfClipSigmaSpin->setRange(1.0, 20.0); m_mfClipSigmaSpin->setValue(5.0); m_mfClipSigmaSpin->setSingleStep(0.5);
    seedParamForm->addRow(tr("Bootstrap frames (bootstrap_frames=):"), m_mfBootstrapSpin);
    seedParamForm->addRow(tr("Clip σ (clip_sigma=):"),                 m_mfClipSigmaSpin);
    seedLayout->addLayout(seedParamForm);
    layout->addWidget(seedGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 5. Star masks
    // ──────────────────────────────────────────────────────────────────────
    m_mfStarMaskCheck = new QCheckBox(tr("Use Star Masks (use_star_masks=)"));
    layout->addWidget(m_mfStarMaskCheck);

    m_mfStarMaskGroup = new QGroupBox(tr("Star Mask Configuration"));
    auto* smForm = new QFormLayout(m_mfStarMaskGroup);

    const DeconvStarMask defaultMask;
    m_mfSmThreshSpin   = new QDoubleSpinBox; m_mfSmThreshSpin->setRange(0.5, 20.0); m_mfSmThreshSpin->setValue(defaultMask.threshSigma); m_mfSmThreshSpin->setSuffix(" sigma");
    m_mfSmMaxObjsSpin  = new QSpinBox;       m_mfSmMaxObjsSpin->setRange(1, 10000); m_mfSmMaxObjsSpin->setValue(defaultMask.maxObjs);
    m_mfSmGrowPxSpin   = new QSpinBox;       m_mfSmGrowPxSpin->setRange(0, 50);     m_mfSmGrowPxSpin->setValue(defaultMask.growPx);            m_mfSmGrowPxSpin->setSuffix(" px");
    m_mfSmEllScaleSpin = new QDoubleSpinBox; m_mfSmEllScaleSpin->setRange(0.1, 5.0); m_mfSmEllScaleSpin->setValue(defaultMask.ellipseScale);   m_mfSmEllScaleSpin->setSingleStep(0.1);
    m_mfSmSoftSigmaSpin= new QDoubleSpinBox; m_mfSmSoftSigmaSpin->setRange(0.0, 20.0); m_mfSmSoftSigmaSpin->setValue(defaultMask.softSigma);
    m_mfSmMaxRadiusSpin= new QSpinBox;       m_mfSmMaxRadiusSpin->setRange(1, 200); m_mfSmMaxRadiusSpin->setValue(defaultMask.maxRadiusPx); m_mfSmMaxRadiusSpin->setSuffix(" px");
    m_mfSmKeepFloorSpin= new QDoubleSpinBox; m_mfSmKeepFloorSpin->setRange(0.0, 0.99); m_mfSmKeepFloorSpin->setValue(defaultMask.keepFloor);  m_mfSmKeepFloorSpin->setSingleStep(0.01);

    smForm->addRow(tr("thresh_sigma="),  m_mfSmThreshSpin);
    smForm->addRow(tr("max_objs="),      m_mfSmMaxObjsSpin);
    smForm->addRow(tr("grow_px="),       m_mfSmGrowPxSpin);
    smForm->addRow(tr("ellipse_scale="), m_mfSmEllScaleSpin);
    smForm->addRow(tr("soft_sigma="),    m_mfSmSoftSigmaSpin);
    smForm->addRow(tr("max_radius_px="), m_mfSmMaxRadiusSpin);
    smForm->addRow(tr("keep_floor="),    m_mfSmKeepFloorSpin);

    auto* refRow = new QHBoxLayout;
    m_mfSmRefPathEdit  = new QLineEdit; m_mfSmRefPathEdit->setPlaceholderText(tr("(optional) star_mask_ref_path"));
    m_mfSmRefBrowseBtn = new QPushButton(tr("Browse…"));
    refRow->addWidget(m_mfSmRefPathEdit); refRow->addWidget(m_mfSmRefBrowseBtn);
    smForm->addRow(tr("star_mask_ref_path="), refRow);

    m_mfStarMaskGroup->setEnabled(false);
    layout->addWidget(m_mfStarMaskGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 6. Variance maps
    // ──────────────────────────────────────────────────────────────────────
    m_mfVarMapCheck = new QCheckBox(tr("Use Variance Maps (use_variance_maps=)"));
    layout->addWidget(m_mfVarMapCheck);

    m_mfVarMapGroup = new QGroupBox(tr("Variance Map Configuration"));
    auto* vmForm = new QFormLayout(m_mfVarMapGroup);
    m_mfVmBwSpin     = new QSpinBox;       m_mfVmBwSpin->setRange(8, 256);    m_mfVmBwSpin->setValue(64);    m_mfVmBwSpin->setSuffix(" px");
    m_mfVmBhSpin     = new QSpinBox;       m_mfVmBhSpin->setRange(8, 256);    m_mfVmBhSpin->setValue(64);    m_mfVmBhSpin->setSuffix(" px");
    m_mfVmSmoothSpin = new QDoubleSpinBox; m_mfVmSmoothSpin->setRange(0.0, 10.0); m_mfVmSmoothSpin->setValue(1.0); m_mfVmSmoothSpin->setSingleStep(0.1);
    m_mfVmFloorSpin  = new QDoubleSpinBox; m_mfVmFloorSpin->setRange(1e-12, 1e-4); m_mfVmFloorSpin->setValue(1e-8); m_mfVmFloorSpin->setDecimals(10);
    vmForm->addRow(tr("bw="),           m_mfVmBwSpin);
    vmForm->addRow(tr("bh="),           m_mfVmBhSpin);
    vmForm->addRow(tr("smooth_sigma="), m_mfVmSmoothSpin);
    vmForm->addRow(tr("floor="),        m_mfVmFloorSpin);
    m_mfVarMapGroup->setEnabled(false);
    layout->addWidget(m_mfVarMapGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 7. Early stopping
    // ──────────────────────────────────────────────────────────────────────
    m_mfEarlyStopGroup = new QGroupBox(tr("Early Stopping (EarlyStopper)"));
    auto* esForm = new QFormLayout(m_mfEarlyStopGroup);
    m_mfEsTolUpdSpin    = new QDoubleSpinBox; m_mfEsTolUpdSpin->setRange(1e-6, 0.1); m_mfEsTolUpdSpin->setValue(2e-4); m_mfEsTolUpdSpin->setDecimals(6);
    m_mfEsTolRelSpin    = new QDoubleSpinBox; m_mfEsTolRelSpin->setRange(1e-6, 0.1); m_mfEsTolRelSpin->setValue(5e-4); m_mfEsTolRelSpin->setDecimals(6);
    m_mfEsEarlyFracSpin = new QDoubleSpinBox; m_mfEsEarlyFracSpin->setRange(0.01, 1.0); m_mfEsEarlyFracSpin->setValue(0.40); m_mfEsEarlyFracSpin->setSingleStep(0.05);
    m_mfEsEmaAlphaSpin  = new QDoubleSpinBox; m_mfEsEmaAlphaSpin->setRange(0.01, 1.0);  m_mfEsEmaAlphaSpin->setValue(0.5);  m_mfEsEmaAlphaSpin->setSingleStep(0.05);
    m_mfEsPatienceSpin  = new QSpinBox;       m_mfEsPatienceSpin->setRange(1, 20);       m_mfEsPatienceSpin->setValue(2);
    esForm->addRow(tr("tol_upd_floor="),  m_mfEsTolUpdSpin);
    esForm->addRow(tr("tol_rel_floor="),  m_mfEsTolRelSpin);
    esForm->addRow(tr("early_frac="),     m_mfEsEarlyFracSpin);
    esForm->addRow(tr("ema_alpha="),      m_mfEsEmaAlphaSpin);
    esForm->addRow(tr("patience="),       m_mfEsPatienceSpin);
    layout->addWidget(m_mfEarlyStopGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 8. Super-Resolution
    // ──────────────────────────────────────────────────────────────────────
    auto* srRow = new QHBoxLayout;
    srRow->addWidget(new QLabel(tr("Super-Resolution factor (super_res_factor=):")));
    m_mfSrFactorSpin = new QSpinBox; m_mfSrFactorSpin->setRange(1, 4); m_mfSrFactorSpin->setValue(1);
    srRow->addWidget(m_mfSrFactorSpin); srRow->addStretch();
    layout->addLayout(srRow);

    m_mfSrGroup = new QGroupBox(tr("SR-PSF Solver (visible only if factor > 1)"));
    auto* srForm = new QFormLayout(m_mfSrGroup);
    m_mfSrSigmaSpin    = new QDoubleSpinBox; m_mfSrSigmaSpin->setRange(0.1, 10.0); m_mfSrSigmaSpin->setValue(1.1); m_mfSrSigmaSpin->setSingleStep(0.1);
    m_mfSrOptItersSpin = new QSpinBox;       m_mfSrOptItersSpin->setRange(10, 1000); m_mfSrOptItersSpin->setValue(250);
    m_mfSrOptLrSpin    = new QDoubleSpinBox; m_mfSrOptLrSpin->setRange(1e-4, 1.0);  m_mfSrOptLrSpin->setValue(0.1); m_mfSrOptLrSpin->setDecimals(4);
    srForm->addRow(tr("sr_sigma="),          m_mfSrSigmaSpin);
    srForm->addRow(tr("sr_psf_opt_iters="),  m_mfSrOptItersSpin);
    srForm->addRow(tr("sr_psf_opt_lr="),     m_mfSrOptLrSpin);
    m_mfSrGroup->setVisible(false);
    layout->addWidget(m_mfSrGroup);

    // ──────────────────────────────────────────────────────────────────────
    // 9. Intermediate saves
    // ──────────────────────────────────────────────────────────────────────
    m_mfSaveIntermCheck = new QCheckBox(tr("Save Intermediate Iterations (save_intermediate=)"));
    layout->addWidget(m_mfSaveIntermCheck);
    auto* siRow = new QHBoxLayout;
    siRow->addWidget(new QLabel(tr("Save every (save_every=):")));
    m_mfSaveEverySpin = new QSpinBox; m_mfSaveEverySpin->setRange(1, 50); m_mfSaveEverySpin->setValue(1);
    m_mfSaveEverySpin->setEnabled(false);
    siRow->addWidget(m_mfSaveEverySpin); siRow->addStretch();
    layout->addLayout(siRow);

    // ──────────────────────────────────────────────────────────────────────
    // 10. Low-mem mode
    // ──────────────────────────────────────────────────────────────────────
    m_mfLowMemCheck = new QCheckBox(tr("Low-memory mode (low_mem=True)"));
    m_mfLowMemCheck->setToolTip(tr("Reduces LRU cache, disables prefetch, limits batch"));
    layout->addWidget(m_mfLowMemCheck);

    // ──────────────────────────────────────────────────────────────────────
    // 11. Run/stop controls + progress
    // ──────────────────────────────────────────────────────────────────────
    auto* runLayout = new QHBoxLayout;
    m_mfStartBtn = new QPushButton(tr("▶ Start MFDeconv"));
    m_mfStopBtn  = new QPushButton(tr("■ Stop"));
    m_mfStopBtn->setEnabled(false);
    runLayout->addWidget(m_mfStartBtn); runLayout->addWidget(m_mfStopBtn);
    layout->addLayout(runLayout);

    m_mfProgressBar = new QProgressBar;
    m_mfProgressBar->setRange(0, 100);
    m_mfProgressBar->setValue(0);
    layout->addWidget(m_mfProgressBar);

    m_mfStatusLabel = new QLabel;
    m_mfStatusLabel->setStyleSheet("color:#eee;background:#222;padding:2px;");
    layout->addWidget(m_mfStatusLabel);

    // Multi-line log area
    m_mfLogEdit = new QPlainTextEdit;
    m_mfLogEdit->setReadOnly(true);
    m_mfLogEdit->setMaximumBlockCount(2000);
    m_mfLogEdit->setFixedHeight(120);
    m_mfLogEdit->setStyleSheet("font-family:monospace;font-size:10px;background:#1a1a1a;color:#ccc;");
    layout->addWidget(m_mfLogEdit);

    layout->addStretch();

    // Add the tab through a scroll area
    m_tabs->addTab(outerScrl, tr("Multi-Frame"));
}

// ─────────────────────────────────────────────────────────────────────────────
// connectSignals
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::connectSignals()
{
    // Single image
    connect(m_previewBtn, &QPushButton::clicked, this, &DeconvolutionDialog::onPreview);
    connect(m_pushBtn,    &QPushButton::clicked, this, &DeconvolutionDialog::onApply);
    connect(m_undoBtn,    &QPushButton::clicked, this, &DeconvolutionDialog::onUndo);
    connect(m_closeBtn,   &QPushButton::clicked, this, &DeconvolutionDialog::onClose);

    connect(m_sepRunBtn, &QPushButton::clicked, this, &DeconvolutionDialog::onRunSEP);
    connect(m_sepUseBtn, &QPushButton::clicked, this, &DeconvolutionDialog::onUseStellarPSF);

    connect(m_algoCombo, &QComboBox::currentTextChanged,
            this, &DeconvolutionDialog::onAlgorithmChanged);
    connect(m_algoCombo, &QComboBox::currentTextChanged,
            this, &DeconvolutionDialog::updatePSFPreview);

    // Update PSF preview when sliders change
    for (auto* sp : {m_convRadiusSpin, m_convShapeSpin, m_convAspectSpin, m_convRotSpin,
                     m_rlPsfRadiusSpin, m_rlPsfShapeSpin, m_rlPsfAspectSpin, m_rlPsfRotSpin}) {
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &DeconvolutionDialog::updatePSFPreview);
    }

    connect(m_lsOpCombo, &QComboBox::currentTextChanged, [this](const QString& op) {
        m_lsBlendCombo->setCurrentText(op == "Divide" ? "SoftLight" : "Screen");
    });

    // Disable custom PSF if sliders change
    if (m_disableCustomBtn)
        connect(m_disableCustomBtn, &QPushButton::clicked, this, [this](){
            m_useCustom = false;
            m_customKernel.release();
            m_customPsfBar->setVisible(false);
        });

    // ── MFDeconv ──────────────────────────────────────────────────────────
    connect(m_mfAddBtn,      &QPushButton::clicked, this, &DeconvolutionDialog::onMFAddFrames);
    connect(m_mfRemoveBtn,   &QPushButton::clicked, this, &DeconvolutionDialog::onMFRemoveFrame);
    connect(m_mfClearBtn,    &QPushButton::clicked, this, &DeconvolutionDialog::onMFClearFrames);
    connect(m_mfMoveUpBtn,   &QPushButton::clicked, this, &DeconvolutionDialog::onMFMoveUp);
    connect(m_mfMoveDownBtn, &QPushButton::clicked, this, &DeconvolutionDialog::onMFMoveDown);
    connect(m_mfBrowseOutBtn,&QPushButton::clicked, this, &DeconvolutionDialog::onMFBrowseOutput);
    connect(m_mfStartBtn,    &QPushButton::clicked, this, &DeconvolutionDialog::onMFStart);
    connect(m_mfStopBtn,     &QPushButton::clicked, this, &DeconvolutionDialog::onMFStop);

    connect(m_mfColorModeCombo, &QComboBox::currentTextChanged,
            this, &DeconvolutionDialog::onMFColorModeChanged);
    connect(m_mfRhoCombo, &QComboBox::currentTextChanged,
            this, &DeconvolutionDialog::onMFRhoChanged);
    connect(m_mfSrFactorSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DeconvolutionDialog::onMFSuperResChanged);
    connect(m_mfStarMaskCheck, &QCheckBox::toggled,
            this, &DeconvolutionDialog::onMFStarMaskToggled);
    connect(m_mfVarMapCheck, &QCheckBox::toggled,
            this, &DeconvolutionDialog::onMFVarMapToggled);
    connect(m_mfSaveIntermCheck, &QCheckBox::toggled,
            this, &DeconvolutionDialog::onMFSaveIntermediateToggled);
    connect(m_mfLowMemCheck, &QCheckBox::toggled,
            this, &DeconvolutionDialog::onMFLowMemToggled);

    connect(m_mfSeedBtnGroup, &QButtonGroup::buttonToggled,
            this, [this](QAbstractButton*, bool) { onMFSeedModeChanged(); });
}

// ─────────────────────────────────────────────────────────────────────────────
// collectParams — collects parameters for a single image
// ─────────────────────────────────────────────────────────────────────────────
DeconvParams DeconvolutionDialog::collectParams() const
{
    DeconvParams p;
    p.globalStrength = m_strengthSpin->value();

    QString algo = m_algoCombo->currentText();
    if      (algo == "Richardson-Lucy") p.algo = DeconvAlgorithm::RichardsonLucy;
    else if (algo == "Wiener")          p.algo = DeconvAlgorithm::Wiener;
    else if (algo == "Larson-Sekanina") p.algo = DeconvAlgorithm::LarsonSekanina;
    else if (algo == "Van Cittert")     p.algo = DeconvAlgorithm::VanCittert;

    p.psfFWHM       = m_rlPsfRadiusSpin->value();
    p.psfBeta       = m_rlPsfShapeSpin->value();
    p.psfRoundness  = m_rlPsfAspectSpin->value();
    p.psfAngle      = m_rlPsfRotSpin->value();

    if (m_useCustom && !m_customKernel.empty()) {
        p.psfSource    = PSFSource::Custom;
        p.customKernel = m_customKernel.clone();
    }

    p.maxIter = (int)m_rlIterSpin->value();
    QString reg = m_rlRegCombo->currentText();
    if      (reg.contains("Tikhonov")) p.regType = 1;
    else if (reg.contains("TV"))       p.regType = 2;
    else                               p.regType = 0;

    p.dering       = m_rlClipCheck->isChecked();
    p.luminanceOnly= m_rlLumCheck->isChecked();
    p.wienerK      = m_wienerNsrSpin->value();

    p.lsRadialStep  = m_lsRadStepSpin->value();
    p.lsAngularStep = m_lsAngStepSpin->value();
    p.lsOp          = (m_lsOpCombo->currentText() == "Divide")
                        ? LSOperator::Divide : LSOperator::Subtract;
    p.lsBlend       = (m_lsBlendCombo->currentText() == "SoftLight")
                        ? LSBlendMode::SoftLight : LSBlendMode::Screen;

    p.vcRelax = m_vcRelaxSpin->value();
    p.maxIter = (algo == "Van Cittert") ? (int)m_vcIterSpin->value() : p.maxIter;

    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// collectMFParams — collects parameters for Multi-Frame
// ─────────────────────────────────────────────────────────────────────────────
MFDeconvParams DeconvolutionDialog::collectMFParams() const
{
    MFDeconvParams p;

    // Frame list
    for (int i = 0; i < m_mfFrameList->count(); ++i)
        p.framePaths.push_back(m_mfFrameList->item(i)->text());
    p.outputPath = m_mfOutputEdit->text();

    // Iteration
    p.maxIters  = m_mfItersSpin->value();
    p.minIters  = m_mfMinItersSpin->value();
    p.kappa     = m_mfKappaSpin->value();
    p.relax     = m_mfRelaxSpin->value();

    // Loss
    p.rho        = (m_mfRhoCombo->currentText() == "l2") ? MFLossType::L2 : MFLossType::Huber;
    p.huberDelta = m_mfHuberDeltaSpin->value();

    // Color mode
    p.colorMode  = m_mfColorModeCombo->currentText();

    // Seed
    p.seedMode       = m_mfSeedRobustRadio->isChecked() ? MFSeedMode::Robust : MFSeedMode::Median;
    p.bootstrapFrames= m_mfBootstrapSpin->value();
    p.clipSigma      = m_mfClipSigmaSpin->value();

    // Star masks
    p.useStarMasks = m_mfStarMaskCheck->isChecked();
    if (p.useStarMasks) {
        p.starMaskCfg.threshSigma  = m_mfSmThreshSpin->value();
        p.starMaskCfg.maxObjs      = m_mfSmMaxObjsSpin->value();
        p.starMaskCfg.growPx       = m_mfSmGrowPxSpin->value();
        p.starMaskCfg.ellipseScale = m_mfSmEllScaleSpin->value();
        p.starMaskCfg.softSigma    = m_mfSmSoftSigmaSpin->value();
        p.starMaskCfg.maxRadiusPx  = m_mfSmMaxRadiusSpin->value();
        p.starMaskCfg.keepFloor    = m_mfSmKeepFloorSpin->value();
        p.starMaskRefPath          = m_mfSmRefPathEdit->text();
    }

    // Variance maps
    p.useVarianceMaps = m_mfVarMapCheck->isChecked();
    if (p.useVarianceMaps) {
        p.varmapCfg.bw          = m_mfVmBwSpin->value();
        p.varmapCfg.bh          = m_mfVmBhSpin->value();
        p.varmapCfg.smoothSigma = m_mfVmSmoothSpin->value();
        p.varmapCfg.floor       = m_mfVmFloorSpin->value();
    }

    // Early stopping
    p.earlyStop.tolUpdFloor  = m_mfEsTolUpdSpin->value();
    p.earlyStop.tolRelFloor  = m_mfEsTolRelSpin->value();
    p.earlyStop.earlyFrac    = m_mfEsEarlyFracSpin->value();
    p.earlyStop.emaAlpha     = m_mfEsEmaAlphaSpin->value();
    p.earlyStop.patience     = m_mfEsPatienceSpin->value();
    p.earlyStop.minIters     = p.minIters;

    // SR
    p.superResFactor = m_mfSrFactorSpin->value();
    if (p.superResFactor > 1) {
        p.srSigma      = m_mfSrSigmaSpin->value();
        p.srPsfOptIters= m_mfSrOptItersSpin->value();
        p.srPsfOptLr   = m_mfSrOptLrSpin->value();
    }

    // Intermediates
    p.saveIntermediate = m_mfSaveIntermCheck->isChecked();
    p.saveEvery        = m_mfSaveEverySpin->value();

    // Low-mem
    p.lowMem = m_mfLowMemCheck->isChecked();

    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slot: single image
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::onPreview()
{
    if (!m_viewer) return;
    if (!m_originalBuffer.isValid()) return;
    
    DeconvParams params = collectParams();
    m_previewBuffer = m_originalBuffer;
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    DeconvResult result = Deconvolution::apply(m_previewBuffer, params);
    QApplication::restoreOverrideCursor();
    
    if (result.success) {
        m_viewer->setBuffer(m_previewBuffer, m_previewBuffer.name(), false);
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Deconvolution failed: ") + result.errorMsg);
    }
}

void DeconvolutionDialog::onApply()
{
    if (!m_viewer) return;
    if (!m_originalBuffer.isValid()) return;

    DeconvParams params = collectParams();
    m_previewBuffer = m_originalBuffer;
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    DeconvResult result = Deconvolution::apply(m_previewBuffer, params);
    QApplication::restoreOverrideCursor();
    
    if (result.success) {
        m_viewer->pushUndo();
        m_viewer->setBuffer(m_previewBuffer, m_previewBuffer.name(), true);
        m_originalBuffer = m_previewBuffer;
        m_previewBtn->setEnabled(false); // disable until change
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Deconvolution failed: ") + result.errorMsg);
    }
}

void DeconvolutionDialog::onUndo()
{
    if (!m_viewer || !m_originalBuffer.isValid()) return;
    m_viewer->setBuffer(m_originalBuffer, m_originalBuffer.name(), true);
}

void DeconvolutionDialog::onClose() { close(); }

void DeconvolutionDialog::onFinished()
{
    setControlsEnabled(true);
}

void DeconvolutionDialog::onAlgorithmChanged(const QString& algo)
{
    m_rlWidget->setVisible(algo == "Richardson-Lucy");
    m_wienerWidget->setVisible(algo == "Wiener");
    m_lsWidget->setVisible(algo == "Larson-Sekanina");
    m_vcWidget->setVisible(algo == "Van Cittert");
    bool showPsf = (algo == "Richardson-Lucy" || algo == "Wiener");
    m_psfParamGroup->setVisible(showPsf);
    m_customPsfBar->setVisible(showPsf && m_useCustom && !m_customKernel.empty());
}

void DeconvolutionDialog::updatePSFPreview()
{
    QString currentTab = m_tabs->tabText(m_tabs->currentIndex());
    QString algo = m_algoCombo->currentText();
    
    if (currentTab == "Convolution") {
        double r = m_convRadiusSpin->value();
        double k = m_convShapeSpin->value();
        double a = m_convAspectSpin->value();
        double rot = m_convRotSpin->value();
        displayPSF(r, k, a, rot);
    } else if (currentTab == "Deconvolution" && (algo == "Richardson-Lucy" || algo == "Wiener")) {
        if (m_useCustom && !m_customKernel.empty()) {
            displayStellarPSF();
        } else {
            double r = m_rlPsfRadiusSpin->value();
            double k = m_rlPsfShapeSpin->value();
            double a = m_rlPsfAspectSpin->value();
            double rot = m_rlPsfRotSpin->value();
            displayPSF(r, k, a, rot);
        }
    } else {
        m_convPSFLabel->clear();
    }
}

void DeconvolutionDialog::onRunSEP()
{
    if (!m_originalBuffer.isValid()) return;
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int stampSize = m_sepStampSpin->value();
    cv::Mat psf = Deconvolution::estimateEmpiricalPSF(m_originalBuffer, stampSize);
    QApplication::restoreOverrideCursor();
    
    if (psf.empty()) {
        QMessageBox::warning(this, tr("No Stars"), tr("Could not estimate PSF from the image."));
        return;
    }
    
    m_customKernel = psf.clone();
    
    // Convert cv::Mat to QImage for preview
    cv::Mat u8;
    double minVal, maxVal;
    cv::minMaxLoc(psf, &minVal, &maxVal);
    if (maxVal > 0) {
        psf.convertTo(u8, CV_8U, 255.0 / maxVal);
    } else {
        psf.convertTo(u8, CV_8U);
    }
    
    QImage img(u8.data, u8.cols, u8.rows, u8.step, QImage::Format_Grayscale8);
    QPixmap pixmap = QPixmap::fromImage(img).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_sepPsfPreview->setPixmap(pixmap);
    m_sepUseBtn->setEnabled(true);
}

void DeconvolutionDialog::onUseStellarPSF()
{
    if (m_customKernel.empty()) return;
    m_useCustom = true;
    m_customPsfBar->setVisible(true);
    updatePSFPreview();
}

void DeconvolutionDialog::setControlsEnabled(bool en)
{
    m_previewBtn->setEnabled(en);
    m_pushBtn->setEnabled(en);
    m_undoBtn->setEnabled(en);
}

void DeconvolutionDialog::displayPSF(double r, double k, double a, double rot) {
    DeconvParams p;
    p.psfFWHM = r;
    p.psfBeta = k;
    p.psfRoundness = a;
    p.psfAngle = rot;
    
    int ksize = (int)(r * 3) | 1; // Enforce odd kernel size
    if (ksize < 11) ksize = 11;
    cv::Mat psf = Deconvolution::buildPSF(p, ksize);
    if (psf.empty()) return;
    
    cv::Mat u8;
    double maxVal;
    cv::minMaxLoc(psf, nullptr, &maxVal);
    if (maxVal > 0) psf.convertTo(u8, CV_8U, 255.0 / maxVal);
    else psf.convertTo(u8, CV_8U);
    
    QImage qimg(u8.data, u8.cols, u8.rows, u8.step, QImage::Format_Grayscale8);
    QPixmap pixmap = QPixmap::fromImage(qimg).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_convPSFLabel->setPixmap(pixmap);
}

void DeconvolutionDialog::displayStellarPSF() {
    if (m_customKernel.empty()) return;
    cv::Mat u8;
    double maxVal;
    cv::minMaxLoc(m_customKernel, nullptr, &maxVal);
    if (maxVal > 0) m_customKernel.convertTo(u8, CV_8U, 255.0 / maxVal);
    else m_customKernel.convertTo(u8, CV_8U);
    
    QImage qimg(u8.data, u8.cols, u8.rows, u8.step, QImage::Format_Grayscale8);
    QPixmap pixmap = QPixmap::fromImage(qimg).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_convPSFLabel->setPixmap(pixmap);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slot: Multi-Frame Deconvolution
// ─────────────────────────────────────────────────────────────────────────────
void DeconvolutionDialog::onMFAddFrames()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select aligned FITS frames"),
        QString(),
        tr("FITS Files (*.fits *.fit *.fts);;All Files (*)"));
    for (const QString& f : files) {
        if (!f.isEmpty()) {
            m_mfFrameList->addItem(f);
            // Auto-populate output path from the first frame
            if (m_mfOutputEdit->text().isEmpty()) {
                QFileInfo fi(f);
                m_mfOutputEdit->setText(fi.absolutePath() + "/" + fi.baseName() + "_MFDeconv.fits");
            }
        }
    }
    updateMFFrameCount();
}

void DeconvolutionDialog::onMFRemoveFrame()
{
    for (auto* item : m_mfFrameList->selectedItems()) delete item;
    updateMFFrameCount();
}

void DeconvolutionDialog::onMFClearFrames()
{
    m_mfFrameList->clear();
    updateMFFrameCount();
}

void DeconvolutionDialog::onMFMoveUp()
{
    int row = m_mfFrameList->currentRow();
    if (row <= 0) return;
    auto* item = m_mfFrameList->takeItem(row);
    m_mfFrameList->insertItem(row - 1, item);
    m_mfFrameList->setCurrentRow(row - 1);
}

void DeconvolutionDialog::onMFMoveDown()
{
    int row = m_mfFrameList->currentRow();
    if (row < 0 || row >= m_mfFrameList->count() - 1) return;
    auto* item = m_mfFrameList->takeItem(row);
    m_mfFrameList->insertItem(row + 1, item);
    m_mfFrameList->setCurrentRow(row + 1);
}

void DeconvolutionDialog::onMFBrowseOutput()
{
    QString f = QFileDialog::getSaveFileName(
        this, tr("Output path"),
        m_mfOutputEdit->text(),
        tr("FITS Files (*.fits *.fit);;All Files (*)"));
    if (!f.isEmpty()) m_mfOutputEdit->setText(f);
}

void DeconvolutionDialog::onMFStart()
{
    if (m_mfFrameList->count() == 0) {
        QMessageBox::warning(this, tr("No frames"), tr("Add at least one FITS frame."));
        return;
    }
    if (m_mfOutputEdit->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing output"), tr("Specify an output path."));
        return;
    }

    MFDeconvParams params = collectMFParams();
    setMFControlsEnabled(false);
    m_mfProgressBar->setValue(0);
    m_mfLogEdit->clear();

    m_mfWorker = new MFDeconvWorker(params, this);
    connect(m_mfWorker, &MFDeconvWorker::statusMessage,
            this, &DeconvolutionDialog::onMFWorkerStatus);
    connect(m_mfWorker, &MFDeconvWorker::progressUpdated,
            this, &DeconvolutionDialog::onMFWorkerProgress);
    connect(m_mfWorker, &MFDeconvWorker::finished,
            this, &DeconvolutionDialog::onMFWorkerFinished);
    connect(m_mfWorker, &QThread::finished,
            m_mfWorker, &QObject::deleteLater);

    m_mfWorker->start();
    m_mfStopBtn->setEnabled(true);
}

void DeconvolutionDialog::onMFStop()
{
    if (m_mfWorker) m_mfWorker->requestStop();
    m_mfStopBtn->setEnabled(false);
}

void DeconvolutionDialog::onMFWorkerStatus(const QString& msg)
{
    // Show the last line in the label and add to the log
    m_mfStatusLabel->setText(msg.left(120));
    m_mfLogEdit->appendPlainText(msg);
    // Scroll to the bottom
    m_mfLogEdit->verticalScrollBar()->setValue(m_mfLogEdit->verticalScrollBar()->maximum());
}

void DeconvolutionDialog::onMFWorkerProgress(int percent, const QString& msg)
{
    m_mfProgressBar->setValue(percent);
    if (!msg.isEmpty()) m_mfStatusLabel->setText(msg.left(120));
}

void DeconvolutionDialog::onMFWorkerFinished(bool ok, const QString& msg, const QString& out)
{
    setMFControlsEnabled(true);
    m_mfStopBtn->setEnabled(false);
    m_mfProgressBar->setValue(ok ? 100 : 0);
    m_mfWorker = nullptr;

    if (ok) {
        m_mfStatusLabel->setText(tr("Completed: %1").arg(out));
        QMessageBox::information(this, tr("MFDeconv"), tr("Deconvolution completed. Saved: %1").arg(out));
    } else {
        m_mfStatusLabel->setText(tr("Error: %1").arg(msg));
        QMessageBox::critical(this, tr("MFDeconv"), msg);
    }
}

void DeconvolutionDialog::onMFColorModeChanged(const QString& mode)
{
    // UI updates depending on color mode (e.g. SR tooltip)
    m_mfSrGroup->setVisible(m_mfSrFactorSpin->value() > 1);
}

void DeconvolutionDialog::onMFSeedModeChanged()
{
    bool robust = m_mfSeedRobustRadio->isChecked();
    m_mfBootstrapSpin->setEnabled(robust);
    m_mfClipSigmaSpin->setEnabled(robust);
}

void DeconvolutionDialog::onMFRhoChanged(const QString& rho)
{
    bool isHuber = (rho == "huber");
    m_mfHuberDeltaSpin->setEnabled(isHuber);
    m_mfHuberDeltaLabel->setEnabled(isHuber);
}

void DeconvolutionDialog::onMFSuperResChanged(int r)
{
    m_mfSrGroup->setVisible(r > 1);
}

void DeconvolutionDialog::onMFStarMaskToggled(bool on)
{
    m_mfStarMaskGroup->setEnabled(on);
}

void DeconvolutionDialog::onMFVarMapToggled(bool on)
{
    m_mfVarMapGroup->setEnabled(on);
}

void DeconvolutionDialog::onMFSaveIntermediateToggled(bool on)
{
    m_mfSaveEverySpin->setEnabled(on);
}

void DeconvolutionDialog::onMFLowMemToggled(bool on)
{
    if (on)
    m_mfLogEdit->appendPlainText(tr("[low_mem=True] Reduced LRU cache, prefetch disabled."));
}

void DeconvolutionDialog::setMFControlsEnabled(bool en)
{
    m_mfStartBtn->setEnabled(en);
    m_mfAddBtn->setEnabled(en);
    m_mfRemoveBtn->setEnabled(en);
    m_mfClearBtn->setEnabled(en);
    m_mfOutputEdit->setEnabled(en);
    m_mfBrowseOutBtn->setEnabled(en);
}

void DeconvolutionDialog::updateMFFrameCount()
{
    int n = m_mfFrameList->count();
    m_mfFrameCountLabel->setText(tr("%1 frames loaded").arg(n));
}
*/