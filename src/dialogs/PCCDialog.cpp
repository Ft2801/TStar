#include "PCCDialog.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCoreApplication>
#include "PCCDistributionDialog.h"
#include "../photometry/StarDetector.h"
#include "../ImageViewer.h"
#include "MainWindowCallbacks.h"


PCCDialog::PCCDialog(ImageViewer* viewer, QWidget* parent) : DialogBase(parent, tr("Photometric Color Calibration"), 300, 120), m_viewer(viewer) {
    
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(6);
    
    // Status label
    m_status = new QLabel(tr("Ready. Image must be plate solved."), this);
    m_status->setWordWrap(false);
    lay->addWidget(m_status);
    
    // Checkbox
    m_chkNeutralizeBackground = new QCheckBox(tr("Background Neutralization"), this);
    m_chkNeutralizeBackground->setChecked(true);
    lay->addWidget(m_chkNeutralizeBackground);

    // Button
    m_btnRun = new QPushButton(tr("Run PCC"), this);
    lay->addWidget(m_btnRun);
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();
        // Update status if stars cached
        if (!meta.catalogStars.empty()) {
            m_status->setText(tr("Ready. Cached stars: %1").arg(meta.catalogStars.size()));
        }
        
        if (meta.ra == 0 && meta.dec == 0) {
            m_status->setText(tr("Warning: No WCS coordinates found!"));
        } else if (!meta.catalogStars.empty()) {
            m_status->setText(tr("Ready (Catalog stars cached)."));
        }
    } else {
        m_status->setText(tr("Error: No valid image."));
        m_btnRun->setEnabled(false);
    }
    
    connect(m_btnRun, &QPushButton::clicked, this, &PCCDialog::onRun);
    
    m_catalog = new CatalogClient(this);
    connect(m_catalog, &CatalogClient::catalogReady, this, &PCCDialog::onCatalogReady);
    connect(m_catalog, &CatalogClient::errorOccurred, this, &PCCDialog::onCatalogError);
    
    m_calibrator = new PCCCalibrator();
    
    m_watcher = new QFutureWatcher<PCCResult>(this);
    connect(m_watcher, &QFutureWatcher<PCCResult>::finished, this, &PCCDialog::onCalibrationFinished);
}

void PCCDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    m_viewer = v;
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();
        // Update status if stars cached
        if (!meta.catalogStars.empty()) {
            m_status->setText(tr("Ready. Cached stars: %1").arg(meta.catalogStars.size()));
        } else if (meta.ra == 0 && meta.dec == 0) {
            m_status->setText(tr("Warning: No WCS coordinates found!"));
        } else {
            m_status->setText(tr("Ready (RA: %1, Dec: %2)").arg(meta.ra).arg(meta.dec));
        }
        m_btnRun->setEnabled(true);
    } else {
        m_status->setText(tr("Error: No valid image."));
        m_btnRun->setEnabled(false);
    }
}

void PCCDialog::onRun() {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
         m_status->setText(tr("Error: Image closed."));
         return;
    }

    const ImageBuffer::Metadata& meta = m_viewer->getBuffer().metadata();

    if (!meta.catalogStars.empty()) {
        onCatalogReady(meta.catalogStars);
        return;
    }

    double ra = meta.ra;
    double dec = meta.dec;
    
    if (ra == 0 && dec == 0) {
        QMessageBox::critical(this, tr("Error"), tr("Image must be plate solved first."));
        return;
    }
    
    m_status->setText(tr("Downloading Gaia DR3 Catalog..."));
    m_btnRun->setEnabled(false);
    m_catalog->queryGaiaDR3(ra, dec, 1.0);
}

// Include QtConcurrent
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

void PCCDialog::onCatalogReady(const std::vector<CatalogStar>& stars) {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;

    m_status->setText(tr("Catalog loaded (%1 stars). Running Calibration (Async)...").arg(stars.size()));
    m_btnRun->setEnabled(false);
    
    // Activate Console
    if (MainWindowCallbacks* mw = getCallbacks()) {
        mw->startLongProcess();
        mw->logMessage(tr("Starting PCC with %1 catalog stars...").arg(stars.size()), 0, false);
    }
    
    // Create a deep copy for thread safety as the worker runs asynchronously
    ImageBuffer scopy = m_viewer->getBuffer();
    
    // Run Heavy Lifting in Background
    QFuture<PCCResult> future = QtConcurrent::run([scopy, stars]() {
        // 1. Background Neutralization Stats (Standard defaults: -2.8 sigma, +2.0 sigma)
        float bg_r = scopy.getRobustMedian(0, -2.8f, +2.0f);
        float bg_g = scopy.getRobustMedian(1, -2.8f, +2.0f);
        float bg_b = scopy.getRobustMedian(2, -2.8f, +2.0f);
        
        // 2. Detect Stars
        StarDetector detector;
        detector.setMaxStars(2000); // Increase limit for robustness
        detector.setThresholdSigma(2.0f); // Sensible default?
        
        std::vector<DetectedStar> sR = detector.detect(scopy, 0);
        std::vector<DetectedStar> sG = detector.detect(scopy, 1);
        std::vector<DetectedStar> sB = detector.detect(scopy, 2);
        
        // 3. Calibrate using aperture photometry (Standard algorithm)
        ImageBuffer::Metadata meta = scopy.metadata();
        PCCCalibrator calibrator;
        calibrator.setWCS(meta.ra, meta.dec, meta.crpix1, meta.crpix2, 
                             meta.cd1_1, meta.cd1_2, meta.cd2_1, meta.cd2_2);
        
        // Use new aperture-based calibration (Standard)
        PCCResult res = calibrator.calibrateWithAperture(scopy, stars);
        
        // Store stats
        res.bg_r = bg_r;
        res.bg_g = bg_g;
        res.bg_b = bg_b;
        
        return res;
    });
    
    // Watcher is now a member, persisting across calls (reused)
    if (m_watcher->isRunning()) {
        // Should not happen as button is disabled
        return;
    }
    m_watcher->setFuture(future);
}

void PCCDialog::onCalibrationFinished() {
    // Capture the specific target viewer
    
    if (!m_viewer) return;

    PCCResult res = m_watcher->result();
    m_result = res;
    
    // Persist in metadata for standalone tool
    ImageBuffer::Metadata meta = m_viewer->getBuffer().metadata();
    meta.pccResult = res;
    m_viewer->getBuffer().setMetadata(meta);

    m_btnRun->setEnabled(true);
    
    MainWindowCallbacks* mw = getCallbacks();
    if (mw) mw->endLongProcess();
    
    if (!res.valid) {
        m_status->setText(tr("Calibration Failed."));
        QMessageBox::warning(this, tr("PCC Failed"), tr("Could not match enough stars. Check WCS and Image Quality."));
    } else {
        m_status->setText(tr("Success. Applying correction..."));
        
        bool neutralize = m_chkNeutralizeBackground->isChecked();
        
        float t0 = -2.8f; 
        float t1 = +2.0f;
        
        float bg[3];
        bg[0] = m_viewer->getBuffer().getRobustMedian(0, t0, t1);
        bg[1] = m_viewer->getBuffer().getRobustMedian(1, t0, t1);
        bg[2] = m_viewer->getBuffer().getRobustMedian(2, t0, t1);
        
        if (!neutralize) {
            bg[0] = 0.0f; bg[1] = 0.0f; bg[2] = 0.0f;
        }
        
        float kw[3] = { static_cast<float>(res.R_factor), static_cast<float>(res.G_factor), static_cast<float>(res.B_factor) };
        float bg_mean = (bg[0] + bg[1] + bg[2]) / 3.0f;
        
        float offset[3];
        for (int c=0; c<3; c++) {
            offset[c] = -bg[c] * kw[c] + bg_mean;
        }

        QString msg = tr("Factors (K):\nR: %1\nG: %2\nB: %3\n\nBackground Ref (Detected):\nR: %4\nG: %5\nB: %6\n\nComputed Offsets:\nR: %7\nG: %8\nB: %9")
                      .arg(kw[0], 0, 'f', 6).arg(kw[1], 0, 'f', 6).arg(kw[2], 0, 'f', 6)
                      .arg(bg[0], 0, 'e', 5).arg(bg[1], 0, 'e', 5).arg(bg[2], 0, 'e', 5)
                      .arg(offset[0], 0, 'e', 5).arg(offset[1], 0, 'e', 5).arg(offset[2], 0, 'e', 5);
        
        // Critical: Push Undo before applying
        m_viewer->pushUndo();
        m_viewer->getBuffer().applyPCC(kw[0], kw[1], kw[2], bg[0], bg[1], bg[2], bg_mean);
        // Refresh display needed? Usually buffer change signals handled, but explicit might be safer
        m_viewer->refreshDisplay(); 
        
        if (mw) mw->logMessage(msg, 1, true);
        
        QMessageBox::information(this, tr("PCC Result"), msg);
        accept(); // Close dialog
    }
}

void PCCDialog::onCatalogError(const QString& err) {
    m_status->setText(tr("Catalog Error: %1").arg(err));
    m_btnRun->setEnabled(true);
}
// onShowGraphs Removed
