#include "ABEDialog.h"
#include "../MainWindow.h"
#include "algos/AbeMath.h"
#include "ImageViewer.h" 
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QApplication>
#include <QCloseEvent>
#include <QIcon>
#include <QLabel>
#include <QProgressDialog>

ABEDialog::ABEDialog(QWidget* parent, ImageViewer* viewer, const ImageBuffer& buffer, bool initialStretch)
    : QDialog(parent), m_viewer(viewer), m_applied(false)
{
    if (m_viewer) {
        m_originalBuffer = buffer; // Copy
    }
    setWindowTitle(tr("Auto Background Extraction"));
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowIcon(QIcon(":/images/Logo.png"));
    
    // Enter ABE Mode (initially handled by setAbeMode(true) from MW or default)
    if (m_viewer) m_viewer->setAbeMode(true);

    // Layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QFormLayout* form = new QFormLayout();
    m_spinDegree = new QSpinBox(); m_spinDegree->setRange(0,6); m_spinDegree->setValue(2);
    m_spinSamples = new QSpinBox(); m_spinSamples->setRange(10, 10000); m_spinSamples->setValue(100);
    m_spinDown = new QSpinBox(); m_spinDown->setRange(1, 16); m_spinDown->setValue(4);
    m_spinPatch = new QSpinBox(); m_spinPatch->setRange(5, 151); m_spinPatch->setSingleStep(2); m_spinPatch->setValue(15);
    
    m_checkRBF = new QCheckBox(tr("Enable RBF")); m_checkRBF->setChecked(true);
    m_spinSmooth = new QDoubleSpinBox(); m_spinSmooth->setRange(0.01, 10.0); m_spinSmooth->setValue(1.0);
    m_spinSmooth->setSingleStep(0.1);
    
    m_checkShowBG = new QCheckBox(tr("Result = Background Model"));
    
    form->addRow(tr("Degree:"), m_spinDegree);
    form->addRow(tr("Samples:"), m_spinSamples);
    form->addRow(tr("Downsample:"), m_spinDown);
    form->addRow(tr("Patch Size:"), m_spinPatch);
    form->addRow(m_checkRBF);
    form->addRow(tr("RBF Smooth:"), m_spinSmooth);
    form->addRow(m_checkShowBG);
    
    mainLayout->addLayout(form);
    
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    
    QPushButton* btnClear = new QPushButton(tr("Clear Selections"));
    connect(btnClear, &QPushButton::clicked, this, &ABEDialog::clearPolys);
    bottomLayout->addWidget(btnClear);

    QLabel* copyLabel = new QLabel(tr("© 2026 SetiAstro"));
    copyLabel->setStyleSheet("color: #888; font-size: 10px; margin-left: 10px; margin-right: 10px;");
    bottomLayout->addWidget(copyLabel);
    bottomLayout->addStretch();
    
    QPushButton* btnApply = new QPushButton(tr("Apply"));
    connect(btnApply, &QPushButton::clicked, this, &ABEDialog::onApply);
    bottomLayout->addWidget(btnApply);
    
    QPushButton* btnClose = new QPushButton(tr("Close"));
    connect(btnClose, &QPushButton::clicked, this, &ABEDialog::close);
    bottomLayout->addWidget(btnClose);
    
    mainLayout->addLayout(bottomLayout);
    
    mainLayout->setSizeConstraint(QLayout::SetFixedSize); // Shrink to fit content
}

ABEDialog::~ABEDialog() {
    setAbeMode(false);
}

void ABEDialog::setAbeMode(bool enabled) {
    if (m_viewer) m_viewer->setAbeMode(enabled);
}

void ABEDialog::setViewer(ImageViewer* viewer) {
    if (m_viewer == viewer) return;
    
    // Disable mode on old viewer
    if (m_viewer) m_viewer->setAbeMode(false);
    
    m_viewer = viewer;
    m_applied = false;
    m_originalBuffer = ImageBuffer();
    
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_originalBuffer = m_viewer->getBuffer();
        m_viewer->setAbeMode(true);
    }
}

void ABEDialog::closeEvent(QCloseEvent* event) {
    if (m_viewer) m_viewer->setAbeMode(false);
    QDialog::closeEvent(event);
}

void ABEDialog::clearPolys() {
    if (m_viewer) m_viewer->clearAbePolygons();
}

void ABEDialog::onApply() {
    
    if (!m_originalBuffer.isValid()) {
        return;
    }
    
    // Validate buffer data before processing
    const auto& data = m_originalBuffer.data();
    bool hasValidData = false;
    for (size_t i = 0; i < std::min(data.size(), (size_t)1000); ++i) {
        float v = data[i];
        if (std::isfinite(v) && v != 0.0f) {
            hasValidData = true;
            break;
        }
    }
    if (!hasValidData) {
        emit progressMsg(tr("Error: Image data appears to be empty or invalid."));
        return;
    }
    
    // Create progress dialog
    QProgressDialog progressDlg(tr("Running ABE..."), QString(), 0, 0, this);
    progressDlg.setWindowTitle(tr("Auto Background Extraction"));
    progressDlg.setWindowModality(Qt::WindowModal);
    progressDlg.setCancelButton(nullptr); // No cancel
    progressDlg.setMinimumDuration(0); // Show immediately
    progressDlg.show();
    QApplication::processEvents();
    
    // Connect progressMsg to update the dialog
    QMetaObject::Connection conn = connect(this, &ABEDialog::progressMsg, [&progressDlg](const QString& msg) {
        progressDlg.setLabelText(msg);
        QApplication::processEvents();
    });
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    try {
        ImageBuffer result = m_originalBuffer;
        generateModel(result);
        clearPolys();
        emit applyResult(result);
    } catch (const std::exception& e) {
        emit progressMsg(tr("ABE Error: %1").arg(QString::fromStdString(e.what())));
    } catch (...) {
        emit progressMsg(tr("ABE Error: Unknown exception occurred."));
    }
    
    disconnect(conn);
    QApplication::restoreOverrideCursor();
}

void ABEDialog::generateModel(ImageBuffer& output) {
    emit progressMsg(tr("Starting ABE Model generation..."));
    
    // 1. Get Params
    int ds = m_spinDown->value();
    int w = output.width();
    int h = output.height();
    int channels = output.channels();
    
    // 2. Get Polygons from Viewer
    std::vector<QPolygonF> polygons;
    if (m_viewer) polygons = m_viewer->getAbePolygons();

    // Create Exclusion Mask (scaled)
    int dw = std::max(1, w / ds);
    int dh = std::max(1, h / ds);
    std::vector<bool> exMask(dw * dh, true); // true = allowed
    if (!polygons.empty()) {
        for(int y=0; y<dh; ++y) {
            for(int x=0; x<dw; ++x) {
                // Map to full
                float fx = x * ds;
                float fy = y * ds;
                QPointF p(fx, fy);
                for(const auto& poly : polygons) {
                    if (poly.containsPoint(p, Qt::OddEvenFill)) {
                        exMask[y*dw + x] = false; // Excluded
                        break;
                    }
                }
            }
        }
    }

    // Downsample Data
    emit progressMsg(tr("Downsampling image (Factor: %1)...").arg(ds));

    std::vector<float> smallData(dw * dh * channels);

    const auto& fullData = output.data();

    for(int y=0; y<dh; ++y) {
        for(int x=0; x<dw; ++x) {
            // Box Average Downsampling (Area Interpolation)
            int sy_start = y * ds;
            int sy_end = std::min(h, (y + 1) * ds);
            int sx_start = x * ds;
            int sx_end = std::min(w, (x + 1) * ds);
            
            // Use stack arrays instead of vectors to avoid heap allocation
            float sum[3] = {0.0f, 0.0f, 0.0f};
            int validCounts[3] = {0, 0, 0};

            for(int sy = sy_start; sy < sy_end; ++sy) {
                for(int sx = sx_start; sx < sx_end; ++sx) {
                    size_t src_idx = ((size_t)sy * w + sx) * channels;
                    // Bounds check
                    if (src_idx + channels > fullData.size()) continue;
                    
                    for(int c=0; c<channels; ++c) {
                        float val = fullData[src_idx + c];
                        if (std::isfinite(val)) {
                            sum[c] += val;
                            validCounts[c]++;
                        }
                    }
                }
            }

            size_t dst_idx = ((size_t)y * dw + x) * channels;
            for(int c=0; c<channels; ++c) {
                if (validCounts[c] > 0) {
                    smallData[dst_idx + c] = sum[c] / validCounts[c];
                } else {
                    smallData[dst_idx + c] = 0.0f;
                }
            }
        }
        // Progress every 100 rows
        if (y % 100 == 0) {

        }
    }

    // Process Per Channel
    int degree = m_spinDegree->value();
    bool useRbf = m_checkRBF->isChecked();
    float smooth = m_spinSmooth->value();
    int numSamples = m_spinSamples->value();
    int patchSize = m_spinPatch->value(); // New

    // 2. Normalize Stats
    qDebug() << "[ABE] Getting medians...";
    std::vector<float> origMedians(channels, 0.0f);
    for(int c=0; c<channels; ++c) {
        origMedians[c] = m_originalBuffer.getChannelMedian(c);
    }
    qDebug() << "[ABE] Allocating totalBg: size_t cast to avoid int overflow...";
    std::vector<float> totalBg((size_t)w * h * channels, 0.0f);
    qDebug() << "[ABE] totalBg allocated, size =" << totalBg.size();

    // 3. Generate Common Sample Points (Luminance)

    std::vector<float> grayData(dw * dh);
    for(int i=0; i<dw*dh; ++i) {
        float sum = 0.0f;
        for(int c=0; c<channels; ++c) {
            sum += smallData[i*channels + c];
        }
        grayData[i] = sum / channels;
    }


    emit progressMsg(tr("Sampling points..."));

    auto commonPoints = AbeMath::generateSamples(grayData, dw, dh, numSamples, patchSize, exMask);

    
    // Safety: If no samples found, use grid fallback (saspro-style)
    if (commonPoints.empty()) {
        emit progressMsg(tr("No valid sample points. Using grid fallback..."));
        int gridN = std::max(3, (int)std::sqrt(numSamples));
        float stepX = (float)dw / gridN;
        float stepY = (float)dh / gridN;
        for (int gy = 0; gy < gridN; ++gy) {
            for (int gx = 0; gx < gridN; ++gx) {
                float x = stepX * (gx + 0.5f);
                float y = stepY * (gy + 0.5f);
                if (x >= 0 && x < dw && y >= 0 && y < dh) {
                    commonPoints.push_back({x, y});
                }
            }
        }
    }
    
    emit progressMsg(tr("Found %1 valid sample points.").arg(commonPoints.size()));

    // 4. Process Each Channel using Common Points
    for(int c=0; c<channels; ++c) {

         // Extract Channel Data for fitting
         std::vector<float> chData(dw * dh);
         for(int i=0; i<dw*dh; ++i) chData[i] = smallData[i*channels + c];

         // Extract Samples
         std::vector<AbeMath::Sample> samples;
         for(const auto& p : commonPoints) {
             // Robust Z: Median of patch around sample point in Downsampled image
             float zVal = AbeMath::getMedianBox(chData, dw, dh, (int)p.x, (int)p.y, patchSize);
             samples.push_back({p.x, p.y, zVal});
         }

         std::vector<float> polyCoeffs;
         AbeMath::RbfModel rbfModel;
         
         // Poly
         if (degree > 0 && !samples.empty()) {
             emit progressMsg(tr("Fitting Polynomial degree %1 (Channel %2)...").arg(degree).arg(c));

             polyCoeffs = AbeMath::fitPolynomial(samples, degree);

         }
         
         // Subtract Poly from Samples for RBF
         if (useRbf && degree > 0) {

             for(auto& s : samples) {
                 // Normalize coords to [0,1] for eval (matching fitPolynomial)
                 float nx = s.x / (float)(dw - 1);
                 float ny = s.y / (float)(dh - 1);
                 float pv = AbeMath::evalPolynomial(nx, ny, polyCoeffs, degree);
                 s.z -= pv;
             }

         }
         
         // RBF
         if (useRbf && !samples.empty()) {
             emit progressMsg(tr("Fitting RBF (Smooth: %1) (Channel %2)...").arg(smooth).arg(c));

             rbfModel = AbeMath::fitRbf(samples, smooth);

         }
         
         // Generate Full BG for this channel
         emit progressMsg(tr("Evaluating Model (Channel %1)...").arg(c));

         std::vector<float> smallBg(dw * dh);

         
         // Disable OMP temporarily for debugging
         // #pragma omp parallel for
         for(int i=0; i<dw*dh; ++i) {
             int y = i / dw;
             int x = i % dw;
             float v = 0.0f;
             // Normalize coords to [0,1] for eval (matching fitPolynomial)
             float nx = (float)x / (float)(dw - 1);
             float ny = (float)y / (float)(dh - 1);
             if (degree > 0) v += AbeMath::evalPolynomial(nx, ny, polyCoeffs, degree);
             if (useRbf) v += AbeMath::evalRbf((float)x, (float)y, rbfModel); // RBF uses raw coords
             smallBg[i] = v;
         }

         
         // Bilinear Upscale 'smallBg' to 'totalBg' channel c
         for(int y=0; y<h; ++y) {
             for(int x=0; x<w; ++x) {
                 float sx = (float)x / ds;
                 float sy = (float)y / ds;
                 
                 int x0 = (int)sx; int x1 = std::min(x0+1, dw-1);
                 int y0 = (int)sy; int y1 = std::min(y0+1, dh-1);
                 
                 // Bounds checks
                 x0 = std::max(0, std::min(x0, dw-1));
                 x1 = std::max(0, std::min(x1, dw-1));
                 y0 = std::max(0, std::min(y0, dh-1));
                 y1 = std::max(0, std::min(y1, dh-1));
                 
                 float fx = sx - x0;
                 float fy = sy - y0;
                 
                 // Bounds check for smallBg access
                 size_t idx00 = (size_t)y0*dw + x0;
                 size_t idx01 = (size_t)y0*dw + x1;
                 size_t idx10 = (size_t)y1*dw + x0;
                 size_t idx11 = (size_t)y1*dw + x1;
                 
                 if (idx00 >= smallBg.size() || idx01 >= smallBg.size() || 
                     idx10 >= smallBg.size() || idx11 >= smallBg.size()) continue;
                 
                 float v00 = smallBg[idx00];
                 float v01 = smallBg[idx01];
                 float v10 = smallBg[idx10];
                 float v11 = smallBg[idx11];
                 
                 float top = v00*(1-fx) + v01*fx;
                 float bot = v10*(1-fx) + v11*fx;
                 float val = top*(1-fy) + bot*fy;
                 
                 // Bounds check for totalBg access
                 size_t dstIdx = ((size_t)y*w + x)*channels + c;
                 if (dstIdx >= totalBg.size()) continue;
                 
                 totalBg[dstIdx] = val;
             }
             // Progress every 500 rows
             if (y % 500 == 0) {

             }
         }

         QApplication::processEvents(); // Keep UI responsive
    }
    
    // Apply Substraction (Correction)
    emit progressMsg(tr("Applying correction..."));
    bool showBg = m_checkShowBG->isChecked();
    std::vector<float>& outData = output.data(); // Ref to modify
    
    for(size_t i=0; i<outData.size(); ++i) {
        int idx = i;
        int c = idx % channels; // Interleaved
        
        if (showBg) {
            outData[i] = totalBg[i];
        } else {
            // Norm: Src - BG + OrigMedian
            outData[i] = outData[i] - totalBg[i] + origMedians[c];
            
            // Clamp to valid range
            if(outData[i] < 0.0f) outData[i] = 0.0f;
            if(outData[i] > 1.0f) outData[i] = 1.0f;
        }
    }
}
