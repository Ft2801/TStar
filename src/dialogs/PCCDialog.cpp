#include "PCCDialog.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCoreApplication>
#include "PCCDistributionDialog.h"
#include "../photometry/StarDetector.h"
#include "MainWindow.h"

// Helper to find MainWindow even if reparented
static MainWindow* getMainWindow(QWidget* w) {
    while (w) {
        if (MainWindow* mw = qobject_cast<MainWindow*>(w)) return mw;
        w = w->parentWidget();
    }
    return nullptr;
}

PCCDialog::PCCDialog(ImageViewer* viewer, QWidget* parent) : QDialog(parent), m_viewer(viewer) {
    setWindowTitle(tr("Photometric Color Calibration"));
    setMinimumWidth(300);
    setMaximumHeight(120); // Strictly limit height
    
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

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
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

// ... (QtConcurrent includes remain)
// Include QtConcurrent
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

void PCCDialog::onCatalogReady(const std::vector<CatalogStar>& stars) {
    if (!m_viewer || !m_viewer->getBuffer().isValid()) return;

    m_status->setText(tr("Catalog loaded (%1 stars). Running Calibration (Async)...").arg(stars.size()));
    m_btnRun->setEnabled(false);
    
    // Activate Console
    if (MainWindow* mw = getMainWindow(this)) {
        mw->startLongProcess();
        mw->log(tr("Starting PCC with %1 catalog stars...").arg(stars.size()), MainWindow::Log_Info);
    }
    
    // Copy image for thread safety? 
    // PCC needs to read image. Passing reference to main thread object is risky if main thread modifies it?
    // PCC is read-only on image for detection. 
    // But buffer might be modified by other tools?
    // Ideally we clone the buffer for the worker.
    ImageBuffer scopy = m_viewer->getBuffer(); // DEEP COPY
    
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
    
    // Watcher must be on heap or member (but we are in a Dialog, member is safer)
    // We'll use a local watcher connected to a lambda that deletes itself? 
    // Risky if dialog closes. Better: Make watcher a member.
    // For now, let's just create a watcher on heap and parent it to this.
    
    // Watcher must be on heap or member
    QFutureWatcher<PCCResult>* watcher = new QFutureWatcher<PCCResult>(this);
    
    // Capture the specific target viewer for this job
    QPointer<ImageViewer> targetViewer = m_viewer;
    
    connect(watcher, &QFutureWatcher<PCCResult>::finished, this, [this, watcher, targetViewer](){
        if (!targetViewer) { watcher->deleteLater(); return; } // Target gone
        
        PCCResult res = watcher->result();
        m_result = res;
        
        // Persist in metadata for standalone tool
        ImageBuffer::Metadata meta = targetViewer->getBuffer().metadata();
        meta.pccResult = res;
        targetViewer->getBuffer().setMetadata(meta);

        watcher->deleteLater();
        m_btnRun->setEnabled(true);
        
        MainWindow* mw = getMainWindow(this);
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
            bg[0] = targetViewer->getBuffer().getRobustMedian(0, t0, t1);
            bg[1] = targetViewer->getBuffer().getRobustMedian(1, t0, t1);
            bg[2] = targetViewer->getBuffer().getRobustMedian(2, t0, t1);
            
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
            targetViewer->pushUndo();
            targetViewer->getBuffer().applyPCC(kw[0], kw[1], kw[2], bg[0], bg[1], bg[2], bg_mean);
            // Refresh display needed? Usually buffer change signals handled, but explicit might be safer
            targetViewer->refreshDisplay(); 
            
            if (mw) mw->log(msg, MainWindow::Log_Success, true);
            
            QMessageBox::information(this, tr("PCC Result"), msg);
            accept(); // Close dialog? Or stay open? Previous logic was accept.
        }
    });
    
    watcher->setFuture(future);
}

void PCCDialog::onCatalogError(const QString& err) {
    m_status->setText(tr("Catalog Error: %1").arg(err));
    m_btnRun->setEnabled(true);
}
// onShowGraphs Removed
