/**
 * @file PlateSolvingDialog.cpp
 * @brief Astrometric plate solving dialog implementation.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#include "PlateSolvingDialog.h"
#include "MainWindowCallbacks.h"
#include "ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDoubleValidator>
#include <QMessageBox>
#include <QComboBox>
#include <QRegularExpression>

// ============================================================================
// Construction
// ============================================================================

PlateSolvingDialog::PlateSolvingDialog(QWidget* parent)
    : DialogBase(parent, tr("Plate Solving"), 400, 380)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // -- Target coordinates section --
    QGroupBox* grpSearch = new QGroupBox(tr("Target Coordinates"), this);
    QHBoxLayout* searchLayout = new QHBoxLayout(grpSearch);
    searchLayout->setContentsMargins(8, 8, 8, 8);
    searchLayout->setSpacing(6);

    m_objectName = new QLineEdit(this);
    m_objectName->setPlaceholderText(tr("Object Name (e.g. M31)"));
    QPushButton* btnSearch = new QPushButton(tr("Search Simbad"), this);
    searchLayout->addWidget(m_objectName);
    searchLayout->addWidget(btnSearch);
    mainLayout->addWidget(grpSearch);

    // -- Manual coordinate entry --
    QHBoxLayout* coordLayout = new QHBoxLayout();
    coordLayout->setContentsMargins(0, 0, 0, 0);
    coordLayout->setSpacing(6);

    m_raHint = new QLineEdit(this);
    m_raHint->setPlaceholderText(tr("RA (deg)"));
    m_decHint = new QLineEdit(this);
    m_decHint->setPlaceholderText(tr("Dec (deg)"));

    coordLayout->addWidget(new QLabel(tr("RA:")));
    coordLayout->addWidget(m_raHint);
    coordLayout->addWidget(new QLabel(tr("Dec:")));
    coordLayout->addWidget(m_decHint);
    mainLayout->addLayout(coordLayout);

    // -- Solver engine selection --
    QHBoxLayout* engineLayout = new QHBoxLayout();
    engineLayout->setContentsMargins(0, 0, 0, 0);
    engineLayout->setSpacing(6);

    m_engineCombo = new QComboBox(this);
    m_engineCombo->addItem(tr("ASTAP"),                  "astap");
    m_engineCombo->addItem(tr("Internal Native Solver"), "native");

    engineLayout->addWidget(new QLabel(tr("Solver Engine:")));
    engineLayout->addWidget(m_engineCombo);
    mainLayout->addLayout(engineLayout);

    // -- Search radius --
    QHBoxLayout* fovBox = new QHBoxLayout();
    fovBox->setContentsMargins(0, 0, 0, 0);
    fovBox->setSpacing(6);

    m_fov = new QLineEdit("3.0", this);
    fovBox->addWidget(new QLabel(tr("Search Radius (deg):")));
    fovBox->addWidget(m_fov);
    mainLayout->addLayout(fovBox);

    // -- Optical settings --
    QGroupBox* grpOptics = new QGroupBox(tr("Optical Settings"), this);
    QGridLayout* opticsLayout = new QGridLayout(grpOptics);
    opticsLayout->setContentsMargins(8, 8, 8, 8);
    opticsLayout->setHorizontalSpacing(8);
    opticsLayout->setVerticalSpacing(6);

    m_focalLength = new QLineEdit(this);
    m_focalLength->setPlaceholderText(tr("Focal Length (mm)"));

    m_pixelSizeUm = new QLineEdit(this);
    m_pixelSizeUm->setPlaceholderText(tr("Pixel Size (um)"));

    m_pixelScale = new QLineEdit(this);
    m_pixelScale->setPlaceholderText(tr("Auto-calculated"));
    m_pixelScale->setReadOnly(true);

    QPushButton* btnCalcScale = new QPushButton(tr("Calculate"), this);
    connect(btnCalcScale, &QPushButton::clicked,
            this,         &PlateSolvingDialog::calculatePixelScale);

    opticsLayout->addWidget(new QLabel(tr("Focal Length (mm):")), 0, 0);
    opticsLayout->addWidget(m_focalLength,                        0, 1);
    opticsLayout->addWidget(new QLabel(tr("Pixel Size (um):")),   1, 0);
    opticsLayout->addWidget(m_pixelSizeUm,                        1, 1);
    opticsLayout->addWidget(new QLabel(tr("Pixel Scale (arcsec/px):")), 2, 0);
    opticsLayout->addWidget(m_pixelScale,                         2, 1);
    opticsLayout->addWidget(btnCalcScale,                         2, 2);

    mainLayout->addWidget(grpOptics);

    // -- Log output --
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(180);
    mainLayout->addWidget(m_log);

    // -- Action buttons --
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->setContentsMargins(0, 0, 0, 0);
    btnLay->setSpacing(6);

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    m_solveBtn  = new QPushButton(tr("Solve"), this);

    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &PlateSolvingDialog::reject);

    btnLay->addWidget(closeBtn);
    btnLay->addStretch();
    btnLay->addWidget(m_cancelBtn);
    btnLay->addWidget(m_solveBtn);
    mainLayout->addLayout(btnLay);

    // -- Initialize solver backends --
    m_simbad     = new SimbadSearcher(this);
    m_solver     = new NativePlateSolver(this);
    m_astapSolver = new AstapSolver(this);

    // -- Wire up signal connections --
    connect(btnSearch,   &QPushButton::clicked, this, &PlateSolvingDialog::onSearchSimbad);
    connect(m_solveBtn,  &QPushButton::clicked, this, &PlateSolvingDialog::onSolve);
    connect(m_cancelBtn, &QPushButton::clicked, this, &PlateSolvingDialog::onCancel);

    connect(m_simbad, &SimbadSearcher::objectFound, this,
            [this](const QString&, double ra, double dec) {
        m_raHint->setText(QString::number(ra));
        m_decHint->setText(QString::number(dec));
        m_log->append(tr("Simbad found: RA %1, Dec %2").arg(ra).arg(dec));
    });
    connect(m_simbad, &SimbadSearcher::errorOccurred, this,
            [this](const QString& err) {
        m_log->append(tr("Simbad Error: %1").arg(err));
    });

    connect(m_solver,     &NativePlateSolver::logMessage, this, &PlateSolvingDialog::onSolverLog);
    connect(m_solver,     &NativePlateSolver::finished,   this, &PlateSolvingDialog::onSolverFinished);
    connect(m_astapSolver, &AstapSolver::logMessage,       this, &PlateSolvingDialog::onSolverLog);
    connect(m_astapSolver, &AstapSolver::finished,         this, &PlateSolvingDialog::onSolverFinished);
}

// ============================================================================
// Image and viewer management
// ============================================================================

void PlateSolvingDialog::setImageBuffer(const ImageBuffer& img)
{
    m_image = img;
}

void PlateSolvingDialog::setViewer(ImageViewer* v)
{
    if (m_viewer == v)
        return;
    m_viewer = v;

    if (m_viewer) {
        setImageBuffer(m_viewer->getBuffer());
        updateScaleFromMetadata();
    }
}

// ============================================================================
// Auto-populate fields from FITS/XISF metadata
// ============================================================================

void PlateSolvingDialog::updateScaleFromMetadata()
{
    const auto& meta = m_image.metadata();

    if (meta.focalLength > 0)
        m_focalLength->setText(QString::number(meta.focalLength, 'f', 1));

    if (meta.pixelSize > 0)
        m_pixelSizeUm->setText(QString::number(meta.pixelSize, 'f', 2));

    // Auto-populate RA/Dec from header metadata
    if (meta.ra != 0.0 || meta.dec != 0.0) {
        m_raHint->setText(QString::number(meta.ra, 'f', 6));
        m_decHint->setText(QString::number(meta.dec, 'f', 6));
    }

    // Auto-calculate pixel scale if both optical parameters are present
    if (meta.focalLength > 0 && meta.pixelSize > 0)
        calculatePixelScale();
}

// ============================================================================
// Pixel scale calculation
// ============================================================================

void PlateSolvingDialog::calculatePixelScale()
{
    double focalMm = m_focalLength->text().toDouble();
    double pixelUm = m_pixelSizeUm->text().toDouble();

    if (focalMm > 0 && pixelUm > 0) {
        // Standard formula: scale (arcsec/px) = (pixel_size_um * 206.265) / focal_mm
        double scale = (pixelUm * 206.265) / focalMm;
        m_pixelScale->setText(QString::number(scale, 'f', 3));
        m_log->append(tr("Calculated pixel scale: %1 arcsec/px")
                      .arg(scale, 0, 'f', 3));
    } else {
        m_pixelScale->clear();
        m_log->append(tr("Error: Enter valid Focal Length and Pixel Size "
                         "to calculate scale."));
    }
}

// ============================================================================
// SIMBAD search
// ============================================================================

void PlateSolvingDialog::onSearchSimbad()
{
    if (m_objectName->text().isEmpty())
        return;
    m_log->append(tr("Searching Simbad..."));
    m_simbad->search(m_objectName->text());
}

// ============================================================================
// Start solving
// ============================================================================

void PlateSolvingDialog::onSolve()
{
    double r     = m_fov->text().toDouble();
    double ra    = m_raHint->text().toDouble();
    double dec   = m_decHint->text().toDouble();
    double scale = m_pixelScale->text().toDouble();

    m_solveBtn->setEnabled(false);
    m_log->clear();
    m_log->append(tr("Starting Solver..."));
    m_log->append(tr("Parameters: RA=%1, Dec=%2, Radius=%3, PixelScale=%4")
                  .arg(ra).arg(dec).arg(r).arg(scale, 0, 'f', 3));

    if (MainWindowCallbacks* mw = getCallbacks()) {
        mw->startLongProcess();
        mw->logMessage(tr("Starting Plate Solving..."), 0, false);
    }

    // Record the target viewer for async result application
    m_jobTarget = m_viewer;

    m_cancelBtn->setVisible(true);
    m_cancelBtn->setEnabled(true);
    m_isFallbackLoop = false;

    // Cache parameters for potential fallback retry
    m_lastRA     = ra;
    m_lastDec    = dec;
    m_lastRadius = r;
    m_lastScale  = scale;

    // Dispatch to selected solver engine
    QString engine = m_engineCombo->currentData().toString();
    if (engine == "astap")
        m_astapSolver->solve(m_image, ra, dec, r, scale);
    else
        m_solver->solve(m_image, ra, dec, r, scale);
}

// ============================================================================
// Cancel solving
// ============================================================================

void PlateSolvingDialog::onCancel()
{
    m_log->append(tr("Cancel requested."));

    QString engine = m_engineCombo->currentData().toString();
    if (engine == "astap") {
        m_astapSolver->cancelSolve();
    } else {
        m_solver->cancelSolve();
        m_log->append(tr("Native solver cancel requested."));
    }

    m_cancelBtn->setEnabled(false);
    m_solveBtn->setEnabled(true);
}

// ============================================================================
// Close event -- prevent closing while a solve is in progress
// ============================================================================

void PlateSolvingDialog::closeEvent(QCloseEvent* event)
{
    if (m_cancelBtn && m_cancelBtn->isVisible()) {
        m_log->append(tr("Solve in progress -- cancel before closing."));
        event->ignore();
        return;
    }
    DialogBase::closeEvent(event);
}

// ============================================================================
// Solver log relay
// ============================================================================

void PlateSolvingDialog::onSolverLog(const QString& text)
{
    m_log->append(text);
}

// ============================================================================
// Solver finished -- apply WCS or attempt fallback
// ============================================================================

void PlateSolvingDialog::onSolverFinished(const NativeSolveResult& res)
{
    m_solveBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);

    MainWindowCallbacks* mw = getCallbacks();
    if (mw)
        mw->endLongProcess();

    if (res.success) {
        // -- Successful solve: apply WCS to image metadata --
        m_solved = true;
        m_result = res;

        m_log->append(tr("<b>Solved!</b>"));
        m_log->append(tr("CRVAL: %1, %2")
                      .arg(res.crval1, 0, 'f', 5)
                      .arg(res.crval2, 0, 'f', 5));

        // Build updated metadata with WCS solution
        ImageBuffer::Metadata meta = m_image.metadata();
        meta.ra     = res.crval1;
        meta.dec    = res.crval2;
        meta.crpix1 = res.crpix1;
        meta.crpix2 = res.crpix2;
        meta.cd1_1  = res.cd[0][0];
        meta.cd1_2  = res.cd[0][1];
        meta.cd2_1  = res.cd[1][0];
        meta.cd2_2  = res.cd[1][1];

        // Clear any pre-existing SIP distortion coefficients to prevent conflicts
        meta.sipOrderA  = 0;
        meta.sipOrderB  = 0;
        meta.sipOrderAP = 0;
        meta.sipOrderBP = 0;
        meta.sipCoeffs.clear();

        // Remove all legacy WCS-related raw headers
        QRegularExpression sipRegex(
            "^(A|B|AP|BP|_?PV|_?PROJP)_\\d+_\\d+$");
        auto it = meta.rawHeaders.begin();
        while (it != meta.rawHeaders.end()) {
            QString k = it->key.toUpper();
            if (k.startsWith("CTYPE")  || k.startsWith("CRVAL")  ||
                k.startsWith("CRPIX")  || k.startsWith("CD1_")   ||
                k.startsWith("CD2_")   || k.startsWith("PC1_")   ||
                k.startsWith("PC2_")   || k.startsWith("CDELT")  ||
                k.startsWith("CROTA")  || k.startsWith("CUNIT")  ||
                k.startsWith("PV1_")   || k.startsWith("PV2_")   ||
                k.startsWith("PROJP")  ||
                k == "A_ORDER"  || k == "B_ORDER"  ||
                k == "AP_ORDER" || k == "BP_ORDER" ||
                sipRegex.match(k).hasMatch()) {
                it = meta.rawHeaders.erase(it);
            } else {
                ++it;
            }
        }

        // Force purely linear TAN projection identifier
        meta.ctype1 = "RA---TAN";
        meta.ctype2 = "DEC--TAN";

        // Cache catalog stars for downstream PCC use
        meta.catalogStars = res.catalogStars;
        m_image.setMetadata(meta);

        // Apply the solution to the viewer that initiated the solve
        if (mw) {
            if (m_jobTarget) {
                m_jobTarget->pushUndo(tr("Plate Solving"));
                ImageBuffer& liveBuf = m_jobTarget->getBuffer();
                liveBuf.setMetadata(meta);
                liveBuf.syncWcsToHeaders();
                mw->logMessage(tr("WCS applied to %1.")
                               .arg(m_jobTarget->windowTitle()), 1, true);
            } else if (m_viewer) {
                // Fallback if the original target was closed
                m_viewer->pushUndo(tr("Plate Solving"));
                m_viewer->getBuffer().setMetadata(meta);
                m_viewer->getBuffer().syncWcsToHeaders();
                mw->logMessage(tr("WCS applied to active image."), 1, true);
            }
        }

        QMessageBox::information(this, tr("Success"),
            tr("Plate solving successful.\nSolution applied."));

    } else {
        // -- Solve failed --
        m_log->append(tr("<b>Failed:</b> %1").arg(res.errorMsg));

        // Attempt automatic fallback to the alternate solver engine
        if (!m_isFallbackLoop) {
            m_isFallbackLoop = true;
            QString currentEngine = m_engineCombo->currentData().toString();
            QString nextEngine = (currentEngine == "astap") ? "native" : "astap";
            QString nextLabel  = (nextEngine == "astap")
                                     ? tr("ASTAP")
                                     : tr("Internal Solver");

            m_log->append(
                tr("<font color='orange'>Automatic fallback: trying %1...</font>")
                .arg(nextLabel));

            if (nextEngine == "astap")
                m_astapSolver->solve(m_image, m_lastRA, m_lastDec,
                                     m_lastRadius, m_lastScale);
            else
                m_solver->solve(m_image, m_lastRA, m_lastDec,
                                m_lastRadius, m_lastScale);
            return;  // Do not reset button state yet
        }

        if (mw)
            mw->logMessage(tr("Solving Failed: %1").arg(res.errorMsg), 3, true);
    }
}