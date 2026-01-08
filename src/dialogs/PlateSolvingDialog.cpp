#include "PlateSolvingDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDoubleValidator>
#include <QMessageBox>
#include "MainWindowCallbacks.h"
#include "ImageViewer.h"


PlateSolvingDialog::PlateSolvingDialog(QWidget* parent) : DialogBase(parent, tr("Plate Solving"), 420, 320) {
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(6);
    
    // Object Search
    QGroupBox* grpSearch = new QGroupBox(tr("Target Coordinates"), this);
    QHBoxLayout* searchLayout = new QHBoxLayout(grpSearch);
    m_objectName = new QLineEdit(this);
    m_objectName->setPlaceholderText(tr("Object Name (e.g. M31)"));
    QPushButton* btnSearch = new QPushButton(tr("Search Simbad"), this);
    searchLayout->addWidget(m_objectName);
    searchLayout->addWidget(btnSearch);
    mainLayout->addWidget(grpSearch);
    
    // Coordinates
    QHBoxLayout* coordLayout = new QHBoxLayout();
    m_raHint = new QLineEdit(this);
    m_raHint->setPlaceholderText(tr("RA (deg)"));
    m_decHint = new QLineEdit(this);
    m_decHint->setPlaceholderText(tr("Dec (deg)"));
    coordLayout->addWidget(new QLabel(tr("RA:")));
    coordLayout->addWidget(m_raHint);
    coordLayout->addWidget(new QLabel(tr("Dec:")));
    coordLayout->addWidget(m_decHint);
    mainLayout->addLayout(coordLayout);
    
    // Solver Settings - FOV
    QHBoxLayout* fovBox = new QHBoxLayout();
    m_fov = new QLineEdit("1.0", this); // Default 1 deg radius
    fovBox->addWidget(new QLabel(tr("Search Radius (deg):")));
    fovBox->addWidget(m_fov);
    mainLayout->addLayout(fovBox);
    
    // Optical Settings
    QGroupBox* grpOptics = new QGroupBox(tr("Optical Settings"), this);
    QGridLayout* opticsLayout = new QGridLayout(grpOptics);
    
    m_focalLength = new QLineEdit(this);
    m_focalLength->setPlaceholderText(tr("Focal Length (mm)"));
    m_pixelSizeUm = new QLineEdit(this);
    m_pixelSizeUm->setPlaceholderText(tr("Pixel Size (µm)"));
    m_pixelScale = new QLineEdit(this);
    m_pixelScale->setPlaceholderText(tr("Auto-calculated"));
    m_pixelScale->setReadOnly(true);
    
    QPushButton* btnCalcScale = new QPushButton(tr("Calculate"), this);
    connect(btnCalcScale, &QPushButton::clicked, this, &PlateSolvingDialog::calculatePixelScale);
    
    opticsLayout->addWidget(new QLabel(tr("Focal Length (mm):")), 0, 0);
    opticsLayout->addWidget(m_focalLength, 0, 1);
    opticsLayout->addWidget(new QLabel(tr("Pixel Size (µm):")), 1, 0);
    opticsLayout->addWidget(m_pixelSizeUm, 1, 1);
    opticsLayout->addWidget(new QLabel(tr("Pixel Scale (″/px):")), 2, 0);
    opticsLayout->addWidget(m_pixelScale, 2, 1);
    opticsLayout->addWidget(btnCalcScale, 2, 2);
    
    mainLayout->addWidget(grpOptics);
    
    // Log
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(120); // Prevent log from taking over the window
    mainLayout->addWidget(m_log);
    
    // Actions
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->setContentsMargins(0, 0, 0, 0);
    m_solveBtn = new QPushButton(tr("Solve"), this);
    
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &PlateSolvingDialog::reject);
    
    btnLay->addWidget(m_solveBtn);
    btnLay->addStretch();
    btnLay->addWidget(closeBtn);
    mainLayout->addLayout(btnLay);
    
    // Objects
    m_simbad = new SimbadSearcher(this);
    m_solver = new NativePlateSolver(this);
    
    // Connects
    connect(btnSearch, &QPushButton::clicked, this, &PlateSolvingDialog::onSearchSimbad);
    connect(m_solveBtn, &QPushButton::clicked, this, &PlateSolvingDialog::onSolve);
    
    connect(m_simbad, &SimbadSearcher::objectFound, this, [this](const QString&, double ra, double dec){
        m_raHint->setText(QString::number(ra));
        m_decHint->setText(QString::number(dec));
        m_log->append(tr("Simbad found: RA %1, Dec %2").arg(ra).arg(dec));
    });
    connect(m_simbad, &SimbadSearcher::errorOccurred, this, [this](const QString& err){
        m_log->append(tr("Simbad Error: %1").arg(err));
    });
    
    connect(m_solver, &NativePlateSolver::logMessage, this, &PlateSolvingDialog::onSolverLog);
    connect(m_solver, &NativePlateSolver::finished, this, &PlateSolvingDialog::onSolverFinished);
}

void PlateSolvingDialog::setImageBuffer(const ImageBuffer& img) {
    m_image = img;
}

void PlateSolvingDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    m_viewer = v;
    if (m_viewer) {
        setImageBuffer(m_viewer->getBuffer());
        updateScaleFromMetadata(); // Auto-populate optical settings from FITS header
    }
}

void PlateSolvingDialog::updateScaleFromMetadata() {
    const auto& meta = m_image.metadata();
    
    // Populate from FITS header if available
    if (meta.focalLength > 0) {
        m_focalLength->setText(QString::number(meta.focalLength, 'f', 1));
    }
    if (meta.pixelSize > 0) {
        m_pixelSizeUm->setText(QString::number(meta.pixelSize, 'f', 2));
    }
    
    // Auto-calculate scale if both values are present
    if (meta.focalLength > 0 && meta.pixelSize > 0) {
        calculatePixelScale();
    }
}

void PlateSolvingDialog::calculatePixelScale() {
    double focalMm = m_focalLength->text().toDouble();
    double pixelUm = m_pixelSizeUm->text().toDouble();
    
    if (focalMm > 0 && pixelUm > 0) {
        // Formula: scale (arcsec/px) = (pixel_size_um * 206.265) / focal_mm
        double scale = (pixelUm * 206.265) / focalMm;
        m_pixelScale->setText(QString::number(scale, 'f', 3));
        m_log->append(tr("Calculated pixel scale: %1 arcsec/px").arg(scale, 0, 'f', 3));
    } else {
        m_pixelScale->clear();
        m_log->append(tr("Error: Enter valid Focal Length and Pixel Size to calculate scale."));
    }
}

void PlateSolvingDialog::onSearchSimbad() {
    if (m_objectName->text().isEmpty()) return;
    m_log->append(tr("Searching Simbad..."));
    m_simbad->search(m_objectName->text());
}

void PlateSolvingDialog::onSolve() {
    double r = m_fov->text().toDouble();
    double ra = m_raHint->text().toDouble();
    double dec = m_decHint->text().toDouble();
    double scale = m_pixelScale->text().toDouble();
    
    m_solveBtn->setEnabled(false);
    m_log->clear();
    m_log->append(tr("Starting Solver..."));
    
    if (MainWindowCallbacks* mw = getCallbacks()) {
        mw->startLongProcess();
        mw->logMessage(tr("Starting Plate Solving..."), 0, false);
    }
    
    // Capture target for consistency check
    m_jobTarget = m_viewer;
    m_solver->solve(m_image, ra, dec, r, scale);
}

void PlateSolvingDialog::onSolverLog(const QString& text) {
    m_log->append(text);
}

void PlateSolvingDialog::onSolverFinished(const NativeSolveResult& res) {
    m_solveBtn->setEnabled(true);
    
    MainWindowCallbacks* mw = getCallbacks();
    if (mw) mw->endLongProcess();

    if (res.success) {
        m_solved = true;
        m_result = res;
        
        m_log->append(tr("<b>Solved!</b>"));
        m_log->append(tr("CRVAL: %1, %2").arg(res.crval1, 0, 'f', 5).arg(res.crval2, 0, 'f', 5));
        
        // CRITICAL: Apply WCS to LIVE Image Metadata immediately
        ImageBuffer::Metadata meta = m_image.metadata();
        meta.ra = res.crval1;
        meta.dec = res.crval2;
        meta.crpix1 = res.crpix1;
        meta.crpix2 = res.crpix2;
        meta.cd1_1 = res.cd[0][0];
        meta.cd1_2 = res.cd[0][1];
        meta.cd2_1 = res.cd[1][0];
        meta.cd2_2 = res.cd[1][1];
        meta.catalogStars = res.catalogStars; // Cache for PCC
        m_image.setMetadata(meta);
        
        if (mw) {
            // Apply to the TARGET viewer we started with, if valid
            if (m_jobTarget) {
                 ImageBuffer& liveBuf = m_jobTarget->getBuffer();
                 liveBuf.setMetadata(meta);
                 liveBuf.syncWcsToHeaders();
                 mw->logMessage(tr("WCS applied to %1.").arg(m_jobTarget->windowTitle()), 1, true);
            } else if (m_viewer) {
                 // Fallback if job target closed but m_viewer replaced? Less likely but safe
                 m_viewer->getBuffer().setMetadata(meta);
                 m_viewer->getBuffer().syncWcsToHeaders();
                 mw->logMessage(tr("WCS applied to active image."), 1, true);
            }
        }
        
        QMessageBox::information(this, tr("Success"), tr("Plate solving successful.\nSolution applied."));
    } else {
        m_log->append(tr("<b>Failed:</b> %1").arg(res.errorMsg));
        if (mw) mw->logMessage(tr("Solving Failed: %1").arg(res.errorMsg), 3, true);
    }
}
