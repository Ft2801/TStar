/*
 * DeconvolutionDialog.cpp  —  Qt6 dialog for image deconvolution
 */

#include "DeconvolutionDialog.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "core/Logger.h"
#include "Deconvolution.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QTabWidget>
#include <QSplitter>
#include <QPainter>
#include <QPixmap>
#include <QApplication>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <algorithm>

// ─── Constructor ──────────────────────────────────────────────────────────────

DeconvolutionDialog::DeconvolutionDialog(ImageViewer* viewer, MainWindow* mw,
                                          QWidget* parent)
    : QDialog(parent), m_viewer(viewer), m_mainWindow(mw)
{
    setWindowTitle(tr("Deconvolution"));
    setWindowFlags(windowFlags() | Qt::Tool);
    
    // Doubled the minimum width for the two-column layout
    setMinimumWidth(860);

    if (m_viewer && m_viewer->getBuffer().isValid())
        m_originalBuffer = m_viewer->getBuffer();

    m_watcher = new QFutureWatcher<DeconvResult>(this);
    buildUI();
    connectSignals();
    renderPSFThumbnail();
}

DeconvolutionDialog::~DeconvolutionDialog() {
    if (m_watcher->isRunning()) m_watcher->cancel();
}

void DeconvolutionDialog::setViewer(ImageViewer* viewer)
{
    m_viewer = viewer;
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer();
    }
}

// ─── Build UI ────────────────────────────────────────────────────────────────

void DeconvolutionDialog::buildUI()
{
    // Main vertical layout for the whole dialog
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(12, 12, 12, 12);

    // Horizontal layout to hold the two columns
    auto* columnsLayout = new QHBoxLayout();
    auto* leftColumn = new QVBoxLayout();
    auto* rightColumn = new QVBoxLayout();
    
    // Setup spacing for columns
    leftColumn->setSpacing(10);
    rightColumn->setSpacing(10);

    // =========================================================================
    // LEFT COLUMN
    // =========================================================================

    // ── Algorithm selector ────────────────────────────────────────────────
    auto* grpAlgo = new QGroupBox(tr("Algorithm"), this);
    auto* hAlgo   = new QHBoxLayout(grpAlgo);
    m_algoCombo   = new QComboBox(this);
    m_algoCombo->addItems({ tr("Richardson-Lucy (RL)"),
                            tr("RL + Total Variation (RLTV)  [recommended]"),
                            tr("Wiener / MMSE") });
    m_algoCombo->setCurrentIndex(1);
    m_algoCombo->setToolTip(
        tr("RL: classic iterative, fast, can ring.\n"
           "RLTV: RL with TV regularisation, suppresses ringing - best for nebulae.\n"
           "Wiener: frequency-domain single-pass, fastest, stable."));
    hAlgo->addWidget(new QLabel(tr("Method:"), this));
    hAlgo->addWidget(m_algoCombo);
    leftColumn->addWidget(grpAlgo);

    // ── PSF group ─────────────────────────────────────────────────────────
    auto* grpPSF = new QGroupBox(tr("Point Spread Function (PSF)"), this);
    auto* vPSF   = new QVBoxLayout(grpPSF);
    auto* frmPSF = new QFormLayout;
    frmPSF->setSpacing(5);

    m_psfSourceCombo = new QComboBox(this);
    m_psfSourceCombo->addItems({ tr("Gaussian"), tr("Moffat"), tr("Disk"), tr("Airy"), tr("Custom image") });
    m_psfSourceCombo->setToolTip(
        tr("Gaussian: simple symmetric bell, good for well-corrected optics.\n"
           "Moffat: more realistic for seeing-limited images; heavier wings.\n"
           "Disk: uniform disk of given diameter.\n"
           "Airy: theoretical diffraction pattern of a circular aperture.\n"
           "Custom: use a measured star as the PSF kernel."));
    frmPSF->addRow(tr("PSF type:"), m_psfSourceCombo);

    m_fwhmSpin = new QDoubleSpinBox(this);
    m_fwhmSpin->setRange(0.5, 20.0); m_fwhmSpin->setSingleStep(0.1);
    m_fwhmSpin->setValue(2.0); m_fwhmSpin->setSuffix(" px");
    m_fwhmSpin->setToolTip(tr("Full Width at Half Maximum of the PSF in pixels."));
    auto* hFWHM = new QHBoxLayout;
    hFWHM->addWidget(m_fwhmSpin);
    m_estimateFWHMBtn = new QPushButton(tr("Auto"), this);
    m_estimateFWHMBtn->setFixedWidth(52);
    m_estimateFWHMBtn->setToolTip(tr("Measure FWHM from stars in the image."));
    hFWHM->addWidget(m_estimateFWHMBtn);
    frmPSF->addRow(tr("FWHM:"), hFWHM);

    m_betaSpin = new QDoubleSpinBox(this);
    m_betaSpin->setRange(1.0, 20.0); m_betaSpin->setSingleStep(0.1);
    m_betaSpin->setValue(4.5);
    m_betaSpin->setToolTip(tr("Moffat beta exponent. Typical seeing: 2-4, "
                              "well-corrected: 4-10."));
    frmPSF->addRow(tr("Moffat beta:"), m_betaSpin);

    m_angleSpin = new QDoubleSpinBox(this);
    m_angleSpin->setRange(-90.0, 90.0); m_angleSpin->setSingleStep(1.0);
    m_angleSpin->setValue(0.0); m_angleSpin->setSuffix(" deg");
    frmPSF->addRow(tr("Angle:"), m_angleSpin);

    m_roundnessSpin = new QDoubleSpinBox(this);
    m_roundnessSpin->setRange(0.01, 1.0); m_roundnessSpin->setSingleStep(0.05);
    m_roundnessSpin->setValue(1.0);
    m_roundnessSpin->setToolTip(tr("Minor/major axis ratio. 1.0 = circular PSF."));
    frmPSF->addRow(tr("Roundness:"), m_roundnessSpin);

    m_airyWavelengthSpin = new QDoubleSpinBox(this);
    m_airyWavelengthSpin->setRange(100.0, 1000.0); m_airyWavelengthSpin->setValue(550.0);
    m_airyWavelengthSpin->setSuffix(" nm");
    frmPSF->addRow(tr("Airy Wavelength:"), m_airyWavelengthSpin);

    m_airyApertureSpin = new QDoubleSpinBox(this);
    m_airyApertureSpin->setRange(10.0, 5000.0); m_airyApertureSpin->setValue(200.0);
    m_airyApertureSpin->setSuffix(" mm");
    frmPSF->addRow(tr("Airy Aperture:"), m_airyApertureSpin);

    m_airyFocalLenSpin = new QDoubleSpinBox(this);
    m_airyFocalLenSpin->setRange(10.0, 10000.0); m_airyFocalLenSpin->setValue(1000.0);
    m_airyFocalLenSpin->setSuffix(" mm");
    frmPSF->addRow(tr("Airy Focal Len:"), m_airyFocalLenSpin);

    m_airyPixelSizeSpin = new QDoubleSpinBox(this);
    m_airyPixelSizeSpin->setRange(0.5, 50.0); m_airyPixelSizeSpin->setValue(3.76);
    m_airyPixelSizeSpin->setSuffix(" um");
    frmPSF->addRow(tr("Airy Pixel Size:"), m_airyPixelSizeSpin);

    m_airyObstructionSpin = new QDoubleSpinBox(this);
    m_airyObstructionSpin->setRange(0.0, 0.99); m_airyObstructionSpin->setValue(0.0); m_airyObstructionSpin->setSingleStep(0.05);
    frmPSF->addRow(tr("Airy Obstruction:"), m_airyObstructionSpin);

    m_kernelSizeSpin = new QSpinBox(this);
    m_kernelSizeSpin->setRange(0, 255); m_kernelSizeSpin->setSingleStep(2);
    m_kernelSizeSpin->setValue(0);
    m_kernelSizeSpin->setSpecialValueText(tr("Auto"));
    frmPSF->addRow(tr("Kernel size:"), m_kernelSizeSpin);

    vPSF->addLayout(frmPSF);

    // PSF thumbnail
    auto* hThumb = new QHBoxLayout;
    hThumb->addStretch();
    m_psfPreview = new QLabel(this);
    m_psfPreview->setFixedSize(64, 64);
    m_psfPreview->setFrameShape(QFrame::StyledPanel);
    m_psfPreview->setAlignment(Qt::AlignCenter);
    m_psfPreview->setToolTip(tr("PSF kernel preview (logarithmic scale)"));
    hThumb->addWidget(new QLabel(tr("PSF preview:"), this));
    hThumb->addWidget(m_psfPreview);
    hThumb->addStretch();
    vPSF->addLayout(hThumb);
    leftColumn->addWidget(grpPSF);
    
    // Add vertical stretch to keep left column elements aligned to the top
    leftColumn->addStretch();

    // =========================================================================
    // RIGHT COLUMN
    // =========================================================================

    // ── RL / RLTV iterations ──────────────────────────────────────────────
    m_grpIter = new QGroupBox(tr("Iteration Control"), this);
    auto* frmIter = new QFormLayout(m_grpIter);
    m_iterSpin = new QSpinBox(this);
    m_iterSpin->setRange(1, 2000); m_iterSpin->setSingleStep(10);
    m_iterSpin->setValue(100);
    frmIter->addRow(tr("Iterations:"), m_iterSpin);
    m_tolSpin = new QDoubleSpinBox(this);
    m_tolSpin->setDecimals(6); m_tolSpin->setRange(0, 0.01);
    m_tolSpin->setSingleStep(1e-5); m_tolSpin->setValue(1e-4);
    frmIter->addRow(tr("Convergence tol:"), m_tolSpin);
    rightColumn->addWidget(m_grpIter);

    // ── RLTV ─────────────────────────────────────────────────────────────
    m_grpTV = new QGroupBox(tr("TV Regularisation"), this);
    auto* frmTV = new QFormLayout(m_grpTV);
    m_tvWeightSpin = new QDoubleSpinBox(this);
    m_tvWeightSpin->setRange(0.0001, 1.0); m_tvWeightSpin->setSingleStep(0.005);
    m_tvWeightSpin->setValue(0.01);
    m_tvWeightSpin->setToolTip(
          tr("lambda_TV - higher values produce smoother output but reduce sharpening.\n"
              "Typical range: 0.005-0.05"));
     frmTV->addRow(tr("lambda_TV weight:"), m_tvWeightSpin);
    rightColumn->addWidget(m_grpTV);

    // ── Wiener ───────────────────────────────────────────────────────────
    m_grpWiener = new QGroupBox(tr("Wiener Parameters"), this);
    auto* frmW = new QFormLayout(m_grpWiener);
    m_wienerKSpin = new QDoubleSpinBox(this);
    m_wienerKSpin->setDecimals(5); m_wienerKSpin->setRange(0.0001, 1.0);
    m_wienerKSpin->setSingleStep(0.001); m_wienerKSpin->setValue(0.001);
    m_wienerKSpin->setToolTip(
        tr("Noise-to-signal power ratio K.\n"
           "Lower K = more aggressive (noisier); higher K = less sharpening (smoother)."));
    frmW->addRow(tr("K (noise power):"), m_wienerKSpin);
    rightColumn->addWidget(m_grpWiener);

    // ── Star protection mask ──────────────────────────────────────────────
    m_grpStarMask = new QGroupBox(tr("Star Protection Mask"), this);
    auto* frmSM = new QFormLayout(m_grpStarMask);
    m_starMaskCheck = new QCheckBox(tr("Enable star mask"), this);
    m_starMaskCheck->setChecked(true);
    m_starMaskCheck->setToolTip(
        tr("Protects bright stars from ringing artefacts by blending\n"
           "deconvolved and original data in masked regions."));
    frmSM->addRow(m_starMaskCheck);

    m_starMaskThreshSpin = new QDoubleSpinBox(this);
    m_starMaskThreshSpin->setRange(0.5, 1.0); m_starMaskThreshSpin->setSingleStep(0.02);
    m_starMaskThreshSpin->setValue(0.85);
    frmSM->addRow(tr("Threshold:"), m_starMaskThreshSpin);

    m_starMaskRadiusSpin = new QDoubleSpinBox(this);
    m_starMaskRadiusSpin->setRange(0.5, 20.0); m_starMaskRadiusSpin->setSingleStep(0.5);
    m_starMaskRadiusSpin->setValue(2.0); m_starMaskRadiusSpin->setSuffix(" px");
    frmSM->addRow(tr("Dilation radius:"), m_starMaskRadiusSpin);

    m_starMaskBlendSpin = new QDoubleSpinBox(this);
    m_starMaskBlendSpin->setRange(0.0, 1.0); m_starMaskBlendSpin->setSingleStep(0.05);
    m_starMaskBlendSpin->setValue(0.5);
    m_starMaskBlendSpin->setToolTip(tr("0 = original in masked area, 1 = deconvolved."));
    frmSM->addRow(tr("Blend in mask:"), m_starMaskBlendSpin);
    rightColumn->addWidget(m_grpStarMask);

    // ── Border ────────────────────────────────────────────────────────────
    auto* grpBorder = new QGroupBox(tr("Border Handling"), this);
    auto* frmBorder = new QFormLayout(grpBorder);
    m_borderPadSpin = new QSpinBox(this);
    m_borderPadSpin->setRange(0, 128); m_borderPadSpin->setSingleStep(8);
    m_borderPadSpin->setValue(32); m_borderPadSpin->setSuffix(" px");
    m_borderPadSpin->setToolTip(tr("Mirror-pad the image before deconvolution\n"
                                    "to reduce edge wrap artefacts."));
    frmBorder->addRow(tr("Mirror padding:"), m_borderPadSpin);
    rightColumn->addWidget(grpBorder);

    // Add vertical stretch to keep right column elements aligned to the top
    rightColumn->addStretch();

    // Add columns to the main horizontal layout
    columnsLayout->addLayout(leftColumn);
    columnsLayout->addLayout(rightColumn);
    
    // Add columns layout to the root vertical layout
    root->addLayout(columnsLayout);

    // =========================================================================
    // BOTTOM SECTION
    // =========================================================================

    // ── Progress ──────────────────────────────────────────────────────────
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_progressBar);
    root->addWidget(m_statusLabel);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* hBtns = new QHBoxLayout;
    m_resetBtn   = new QPushButton(tr("Reset"),   this);
    m_previewBtn = new QPushButton(tr("Preview"), this);
    m_applyBtn   = new QPushButton(tr("Apply"),   this);
    m_closeBtn   = new QPushButton(tr("Close"),   this);
    m_applyBtn->setDefault(true);
    hBtns->addWidget(m_resetBtn);
    hBtns->addStretch();
    hBtns->addWidget(m_previewBtn);
    hBtns->addWidget(m_applyBtn);
    hBtns->addWidget(m_closeBtn);
    root->addLayout(hBtns);

    // Initial widget visibility
    setAlgorithmWidgets(DeconvAlgorithm::RLTV);
}

// ─── Connect signals ──────────────────────────────────────────────────────────

void DeconvolutionDialog::connectSignals()
{
    connect(m_algoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeconvolutionDialog::onAlgorithmChanged);
    connect(m_psfSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeconvolutionDialog::onPSFSourceChanged);
    connect(m_estimateFWHMBtn, &QPushButton::clicked, this, &DeconvolutionDialog::onEstimateFWHM);
    connect(m_fwhmSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &DeconvolutionDialog::onFWHMChanged);
    connect(m_betaSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ renderPSFThumbnail(); });
    connect(m_angleSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ renderPSFThumbnail(); });
    connect(m_roundnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ renderPSFThumbnail(); });
    connect(m_airyWavelengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double){ renderPSFThumbnail(); });
    connect(m_airyApertureSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double){ renderPSFThumbnail(); });
    connect(m_airyFocalLenSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double){ renderPSFThumbnail(); });
    connect(m_airyPixelSizeSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double){ renderPSFThumbnail(); });
    connect(m_airyObstructionSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double){ renderPSFThumbnail(); });
    connect(m_previewBtn, &QPushButton::clicked, this, &DeconvolutionDialog::onPreview);
    connect(m_applyBtn,   &QPushButton::clicked, this, &DeconvolutionDialog::onApply);
    connect(m_resetBtn,   &QPushButton::clicked, this, &DeconvolutionDialog::onReset);
    connect(m_closeBtn,   &QPushButton::clicked, this, &QDialog::close);
    connect(m_watcher, &QFutureWatcher<DeconvResult>::finished,
            this, &DeconvolutionDialog::onFinished);
}

// ─── Algorithm widget visibility ─────────────────────────────────────────────

void DeconvolutionDialog::setAlgorithmWidgets(DeconvAlgorithm algo)
{
    m_grpIter->setVisible(algo != DeconvAlgorithm::Wiener);
    m_grpTV->setVisible(algo == DeconvAlgorithm::RLTV);
    m_grpWiener->setVisible(algo == DeconvAlgorithm::Wiener);
    m_grpStarMask->setVisible(algo != DeconvAlgorithm::Wiener);
    adjustSize();
}

void DeconvolutionDialog::onAlgorithmChanged(int index)
{
    static const DeconvAlgorithm map[] = {
        DeconvAlgorithm::RichardsonLucy,
        DeconvAlgorithm::RLTV,
        DeconvAlgorithm::Wiener
    };
    if (index >= 0 && index < 3) setAlgorithmWidgets(map[index]);
}

void DeconvolutionDialog::onPSFSourceChanged(int index)
{
    bool isMoffat = (index == 1);
    bool isGaussian = (index == 0);
    bool isDisk = (index == 2);
    bool isAiry = (index == 3);
    
    m_betaSpin->setEnabled(isMoffat);
    m_fwhmSpin->setEnabled(isMoffat || isGaussian || isDisk);
    m_estimateFWHMBtn->setEnabled(isMoffat || isGaussian || isDisk);
    m_angleSpin->setEnabled(isMoffat || isGaussian);
    m_roundnessSpin->setEnabled(isMoffat || isGaussian);
    
    m_airyWavelengthSpin->setEnabled(isAiry);
    m_airyApertureSpin->setEnabled(isAiry);
    m_airyFocalLenSpin->setEnabled(isAiry);
    m_airyPixelSizeSpin->setEnabled(isAiry);
    m_airyObstructionSpin->setEnabled(isAiry);

    renderPSFThumbnail();
}

// ─── PSF thumbnail ────────────────────────────────────────────────────────────

void DeconvolutionDialog::renderPSFThumbnail()
{
    DeconvParams p = collectParams();
    cv::Mat k = Deconvolution::buildPSF(p, 63);

    // Log-scale normalisation for display
    double mn, mx;
    cv::minMaxLoc(k, &mn, &mx);
    if (mx < 1e-12) return;

    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::black);
    for (int y = 0; y < k.rows; y++) {
        for (int x = 0; x < k.cols; x++) {
            double v = k.at<float>(y, x) / static_cast<float>(mx);
            v = std::log1p(v * 9.0) / std::log(10.0); // log stretch
            int gray = static_cast<int>(std::clamp(v, 0.0, 1.0) * 255.0);
            img.setPixel(x, y, qRgb(gray, gray, gray));
        }
    }
    m_psfPreview->setPixmap(QPixmap::fromImage(img));
}

// ─── Collect params ───────────────────────────────────────────────────────────

DeconvParams DeconvolutionDialog::collectParams() const
{
    DeconvParams p;
    static const DeconvAlgorithm algos[] = {
        DeconvAlgorithm::RichardsonLucy,
        DeconvAlgorithm::RLTV,
        DeconvAlgorithm::Wiener
    };
    int idx = m_algoCombo ? m_algoCombo->currentIndex() : 1;
    p.algo          = algos[std::clamp(idx, 0, 2)];

    static const PSFSource psfMap[] = { PSFSource::Gaussian, PSFSource::Moffat, PSFSource::Disk, PSFSource::Airy, PSFSource::Custom };
    int psf_idx = m_psfSourceCombo ? m_psfSourceCombo->currentIndex() : 0;
    p.psfSource     = psfMap[std::clamp(psf_idx, 0, 4)];
    p.psfFWHM       = m_fwhmSpin      ? m_fwhmSpin->value()       : 2.0;
    p.psfBeta       = m_betaSpin      ? m_betaSpin->value()        : 4.5;
    p.psfAngle      = m_angleSpin     ? m_angleSpin->value()       : 0.0;
    p.psfRoundness  = m_roundnessSpin ? m_roundnessSpin->value()   : 1.0;
    
    p.airyWavelength = m_airyWavelengthSpin ? m_airyWavelengthSpin->value() : 550.0;
    p.airyAperture   = m_airyApertureSpin ? m_airyApertureSpin->value() : 200.0;
    p.airyFocalLen   = m_airyFocalLenSpin ? m_airyFocalLenSpin->value() : 1000.0;
    p.airyPixelSize  = m_airyPixelSizeSpin ? m_airyPixelSizeSpin->value() : 3.76;
    p.airyObstruction= m_airyObstructionSpin ? m_airyObstructionSpin->value() : 0.0;
    
    p.kernelSize    = m_kernelSizeSpin? m_kernelSizeSpin->value()  : 0;

    p.maxIter       = m_iterSpin      ? m_iterSpin->value()        : 100;
    p.convergenceTol= m_tolSpin       ? m_tolSpin->value()         : 1e-4;
    p.tvRegWeight   = m_tvWeightSpin  ? m_tvWeightSpin->value()    : 0.01;
    p.wienerK       = m_wienerKSpin   ? m_wienerKSpin->value()     : 0.001;
    p.borderPad     = m_borderPadSpin ? m_borderPadSpin->value()   : 32;

    p.starMask.useMask   = m_starMaskCheck      && m_starMaskCheck->isChecked();
    p.starMask.threshold = m_starMaskThreshSpin ? m_starMaskThreshSpin->value() : 0.85;
    p.starMask.radius    = m_starMaskRadiusSpin ? m_starMaskRadiusSpin->value() : 2.0;
    p.starMask.blend     = m_starMaskBlendSpin  ? m_starMaskBlendSpin->value()  : 0.5;

    return p;
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void DeconvolutionDialog::onPreview()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    if (m_watcher->isRunning()) return;
    m_isPreview = true;
    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Computing preview..."));

    // Always use the original clean buffer as input, not the progressively destroyed one
    auto buf    = std::make_shared<ImageBuffer>(m_originalBuffer);
    auto params = collectParams();
    params.maxIter = std::min(params.maxIter, 20);
    params.progressCallback = [this](int pct, const QString& msg) {
        QMetaObject::invokeMethod(this, [this, pct, msg]() {
            m_progressBar->setValue(pct);
            if (!msg.isEmpty()) m_statusLabel->setText(msg);
        }, Qt::QueuedConnection);
    };

    auto* raw = new std::shared_ptr<ImageBuffer>(buf);
    m_watcher->setProperty("bufPtr", QVariant::fromValue(static_cast<void*>(raw)));
    m_watcher->setFuture(QtConcurrent::run([buf, params]() {
        return Deconvolution::apply(*buf, params);
    }));
}

void DeconvolutionDialog::onApply()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    if (m_watcher->isRunning()) return;
    m_isPreview = false;
    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Applying deconvolution..."));

    // Always use the original clean buffer as input
    auto buf    = std::make_shared<ImageBuffer>(m_originalBuffer);
    auto params = collectParams();
    params.progressCallback = [this](int pct, const QString& msg) {
        QMetaObject::invokeMethod(this, [this, pct, msg]() {
            m_progressBar->setValue(pct);
            if (!msg.isEmpty()) m_statusLabel->setText(msg);
        }, Qt::QueuedConnection);
    };

    auto* raw = new std::shared_ptr<ImageBuffer>(buf);
    m_watcher->setProperty("bufPtr", QVariant::fromValue(static_cast<void*>(raw)));
    m_watcher->setFuture(QtConcurrent::run([buf, params]() {
        return Deconvolution::apply(*buf, params);
    }));
}

void DeconvolutionDialog::onFinished()
{
    m_progressBar->setVisible(false);
    setControlsEnabled(true);

    auto* raw = static_cast<std::shared_ptr<ImageBuffer>*>(
        m_watcher->property("bufPtr").value<void*>());
    if (!raw) return;

    DeconvResult res = m_watcher->result();
    if (res.success && m_viewer) {
        if (m_isPreview) {
            m_viewer->setBuffer(**raw, m_viewer->windowTitle(), true);
            m_statusLabel->setText(tr("Preview - %1 iterations").arg(res.iterations));
        } else {
            // Restore original quickly so pushUndo grabs the accurate unaltered state first!
            m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
            m_viewer->pushUndo();
            // Now apply the newly computed DECONVOLVED state
            m_viewer->setBuffer(**raw, m_viewer->windowTitle(), true);
            m_originalBuffer = **raw; // Commit! Future previews start from here.
            m_viewer->refreshDisplay(true);
            QString algo = m_algoCombo->currentText();
            m_statusLabel->setText(tr("Done - %1 iterations, d=%2")
                .arg(res.iterations).arg(res.finalChange, 0, 'e', 2));
            if (m_mainWindow)
                Logger::info(tr("Deconvolution [%1] applied: FWHM=%2px, iter=%3")
                    .arg(algo).arg(m_fwhmSpin->value()).arg(res.iterations));
        }
    } else {
        m_statusLabel->setText(tr("Failed: ") + res.errorMsg);
        QMessageBox::warning(this, tr("Deconvolution"), tr("Failed:\n") + res.errorMsg);
    }
    delete raw;
}

void DeconvolutionDialog::onReset()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
    m_viewer->refreshDisplay(true);
    m_statusLabel->setText(tr("Reset to original."));
}

void DeconvolutionDialog::onEstimateFWHM()
{
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;
    setControlsEnabled(false);
    m_statusLabel->setText(tr("Estimating FWHM from stars..."));
    QApplication::processEvents();

    double fwhm = Deconvolution::estimateFWHM(m_viewer->getBuffer());
    m_fwhmSpin->setValue(fwhm);
    m_statusLabel->setText(tr("Estimated FWHM: %1 px").arg(fwhm, 0, 'f', 2));
    setControlsEnabled(true);
}

void DeconvolutionDialog::onFWHMChanged(double) {
    renderPSFThumbnail();
}

void DeconvolutionDialog::setControlsEnabled(bool en)
{
    m_algoCombo->setEnabled(en);
    m_psfSourceCombo->setEnabled(en);
    m_fwhmSpin->setEnabled(en);
    m_betaSpin->setEnabled(en && m_psfSourceCombo->currentIndex() == 1);
    m_angleSpin->setEnabled(en);
    m_roundnessSpin->setEnabled(en);
    m_estimateFWHMBtn->setEnabled(en);
    m_iterSpin->setEnabled(en);
    m_tolSpin->setEnabled(en);
    m_tvWeightSpin->setEnabled(en);
    m_wienerKSpin->setEnabled(en);
    m_starMaskCheck->setEnabled(en);
    m_previewBtn->setEnabled(en);
    m_applyBtn->setEnabled(en);
    m_resetBtn->setEnabled(en);
}

void DeconvolutionDialog::closeEvent(QCloseEvent* event)
{
    if (m_watcher->isRunning()) { m_watcher->cancel(); m_watcher->waitForFinished(); }
    
    // If the user closes the dialog after previewing without hitting apply, restore original!
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_viewer->setBuffer(m_originalBuffer, m_viewer->windowTitle(), true);
        m_viewer->refreshDisplay(true);
    }
    
    QDialog::closeEvent(event);
}
