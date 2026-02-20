#include "CorrectionBrushDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QGroupBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QScrollBar>
#include <QMessageBox>
#include <QComboBox>
#include <cmath>
#include <algorithm>
#include <QScrollBar>
#include <opencv2/opencv.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/imgproc.hpp>
#include "CorrectionBrushDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../ImageViewer.h"

// ================= Worker =================

CorrectionWorker::CorrectionWorker(const ImageBuffer& img, int x, int y, int radius, float feather, float opacity, 
                                   const std::vector<int>& channels, CorrectionMethod method)
    : m_image(img), m_x(x), m_y(y), m_radius(radius), m_feather(feather), m_opacity(opacity), 
      m_channels(channels), m_method(method)
{
    setAutoDelete(true);
}

void CorrectionWorker::run() {
    ImageBuffer res = removeBlemish(m_image, m_x, m_y, m_radius, m_feather, m_opacity, m_channels, m_method);
    emit finished(res);
}

float CorrectionWorker::medianCircle(const ImageBuffer& img, int cx, int cy, int radius, const std::vector<int>& channels) {
    int w = img.width();
    int h = img.height();
    int c = img.channels();
    const float* data = img.data().data();
    
    int x0 = std::max(0, cx - radius);
    int x1 = std::min(w, cx + radius + 1);
    int y0 = std::max(0, cy - radius);
    int y1 = std::min(h, cy + radius + 1);
    
    if (x0 >= x1 || y0 >= y1) return 0.0f;
    
    std::vector<float> vals;
    vals.reserve((x1-x0)*(y1-y0)*channels.size());
    
    for (int ch : channels) {
        if (ch >= c) continue;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if ((x - cx)*(x - cx) + (y - cy)*(y - cy) <= radius*radius) {
                    vals.push_back(data[(y*w+x)*c + ch]);
                }
            }
        }
    }
    
    if (vals.empty()) return 0.0f;
    std::nth_element(vals.begin(), vals.begin() + vals.size()/2, vals.end());
    return vals[vals.size()/2];
}

ImageBuffer CorrectionWorker::removeBlemish(const ImageBuffer& img, int x, int y, int radius, float feather, float opacity, const std::vector<int>& channels, CorrectionMethod method) {
    if (method == CorrectionMethod::ContentAware) {
        // Smart Patch - Context-Aware Fill
        // 1. Search for best matching texture in neighborhood
        // 2. Seamlessly clone the best patch into the blemish area

        int w = img.width();
        int h = img.height();
        int c = img.channels();
        
        // Search Radius: How far to look for a patch (3x blemish radius)
        int searchRad = radius * 4;
        int pad = searchRad + 20; // Extra padding for safe convolution/cloning
        
        int x0 = std::max(0, x - pad);
        int y0 = std::max(0, y - pad);
        int x1 = std::min(w, x + pad + 1);
        int y1 = std::min(h, y + pad + 1);
        
        int roiW = x1 - x0;
        int roiH = y1 - y0;
        
        if (roiW <= 0 || roiH <= 0) return img;
        
        // Convert ROI to 8-bit CV_8UC3 for seamlessClone (required)
        cv::Mat roiMat;
        if (c == 3) roiMat = cv::Mat(roiH, roiW, CV_8UC3);
        else roiMat = cv::Mat(roiH, roiW, CV_8UC3); // Force 3 channels for seamlessClone support
        
        const float* srcData = img.data().data();
        
        for (int i=0; i<roiH; ++i) {
            for (int j=0; j<roiW; ++j) {
                int sx = x0 + j;
                int sy = y0 + i;
                uchar b, g, r;
                if (c >= 3) {
                    b = (uchar)std::clamp(srcData[(sy*w+sx)*c+2] * 255.0f, 0.0f, 255.0f);
                    g = (uchar)std::clamp(srcData[(sy*w+sx)*c+1] * 255.0f, 0.0f, 255.0f);
                    r = (uchar)std::clamp(srcData[(sy*w+sx)*c+0] * 255.0f, 0.0f, 255.0f);
                } else {
                    uchar v = (uchar)std::clamp(srcData[(sy*w+sx)*c] * 255.0f, 0.0f, 255.0f);
                    b = g = r = v;
                }
                roiMat.at<cv::Vec3b>(i,j) = cv::Vec3b(b, g, r);
            }
        }
        
        // Define Template (The hole surroundings)
        int tRad = radius + 2; // Template radius (slightly larger than blemish)
        int tSize = 2 * tRad + 1;
        
        // Center of blemish in ROI coordinates
        int cx = x - x0;
        int cy = y - y0;
        
        // Check bounds
        if (cx - tRad < 0 || cy - tRad < 0 || cx + tRad >= roiW || cy + tRad >= roiH) {
             // Fallback to simple logic if too close to edge
             return img;
        }

        // Create Mask for Template Matching (1=Valid Context, 0=Hole)
        cv::Mat tMask = cv::Mat::ones(tSize, tSize, CV_8UC1);
        cv::circle(tMask, cv::Point(tRad, tRad), radius, cv::Scalar(0), -1); // Mask out the hole
        
        // Extract Template from ROI (The context around the hole)
        cv::Rect tRect(cx - tRad, cy - tRad, tSize, tSize);
        cv::Mat templ = roiMat(tRect);
        
        // Template Matching
        cv::Mat matchResult;
        
        // Perform template matching using SQDIFF (Squared Difference).
        // tMask is used to ignore the blemish area during matching, ensuring we only match the context.
        // If the OpenCV version does not support masked matchTemplate with SQDIFF, we fall back to unmasked matching.
        try {
            cv::matchTemplate(roiMat, templ, matchResult, cv::TM_SQDIFF, tMask);
        } catch(...) {
            cv::matchTemplate(roiMat, templ, matchResult, cv::TM_SQDIFF);
        }
        
        // Blank out the area around the blemish in the result map so we don't pick the blemish itself
        // The result map corresponds to the top-left corner of the sliding window.
        // The self-match corresponds to placement at (cx - tRad, cy - tRad).
        int excludeRad = radius + 5;
        // Fix: Coordinates must be top-left of the match, not the center of ROI.
        cv::circle(matchResult, cv::Point(cx - tRad, cy - tRad), excludeRad, cv::Scalar(FLT_MAX), -1);
        
        double minVal; cv::Point minLoc;
        cv::minMaxLoc(matchResult, &minVal, nullptr, &minLoc, nullptr);
        
        // Best source patch top-left in ROI
        cv::Point bestTL = minLoc;
        
        // Verify source patch is within bounds
        cv::Rect srcRect(bestTL.x, bestTL.y, tSize, tSize);
        if (srcRect.x < 0 || srcRect.y < 0 || srcRect.x + srcRect.width > roiW || srcRect.y + srcRect.height > roiH) {
             return img;
        }

        cv::Mat sourcePatch = roiMat(srcRect);
        
        // Seamless Clone
        // src: The source patch
        // dst: The ROI
        // mask: The area we want to replace (the hole).
        // p: The center of the hole in ROI.
        
        cv::Mat cloneMask = cv::Mat::zeros(tSize, tSize, CV_8UC1);
        cv::circle(cloneMask, cv::Point(tRad, tRad), radius, cv::Scalar(255), -1);
        
        cv::Mat blendedROI;
        try {
            // Note: seamlessClone assumes sourcePatch is the 'object' to be placed.
            // We want to place sourcePatch into roiMat at (cx, cy).
            cv::seamlessClone(sourcePatch, roiMat, cloneMask, cv::Point(cx, cy), blendedROI, cv::NORMAL_CLONE);
        } catch (const cv::Exception& e) {
            // Fallback if seamless clone fails (e.g. boundary issues)
             blendedROI = roiMat.clone();
             // Simple copy
             sourcePatch.copyTo(blendedROI(cv::Rect(cx - tRad, cy - tRad, tSize, tSize)), cloneMask);
        }
        
        // Write back to result
        ImageBuffer result = img;
        float* dstData = result.data().data();
        
        for (int i=0; i<roiH; ++i) {
            for (int j=0; j<roiW; ++j) {
                // Determine if we are in the modified area (blemish + feather)
                float dist = std::hypot(x0 + j - x, y0 + i - y);
                if (dist > radius + 2) continue; // Only update affected area
                
                float weight = 1.0f;
                if (feather > 0.0f && dist > radius * (1.0f - feather)) {
                     // Simple feathering at edge
                     // Note: seamlessClone handles internal gradient blending, 
                     // but we might want to feather the opacity of the entire correction
                }
                
                // Mask for blending original vs corrected
                if (dist > radius) weight = 0.0f; // Hard cut at radius for now, let seamlessClone handle blend
                else weight = 1.0f;
                
                // Opacity mix
                float wOp = weight * opacity;
                if (wOp <= 0.001f) continue;

                int sx = x0 + j;
                int sy = y0 + i;
                
                cv::Vec3b px = blendedROI.at<cv::Vec3b>(i, j);
                float r = px[2] / 255.0f;
                float g = px[1] / 255.0f;
                float b = px[0] / 255.0f;
                
                if (c == 3) {
                    dstData[(sy*w+sx)*c+0] = dstData[(sy*w+sx)*c+0] * (1.0f - wOp) + r * wOp;
                    dstData[(sy*w+sx)*c+1] = dstData[(sy*w+sx)*c+1] * (1.0f - wOp) + g * wOp;
                    dstData[(sy*w+sx)*c+2] = dstData[(sy*w+sx)*c+2] * (1.0f - wOp) + b * wOp;
                } else {
                    // For mono, just take G channel or average
                    float gray = (r + g + b) / 3.0f;
                    dstData[(sy*w+sx)*c] = dstData[(sy*w+sx)*c] * (1.0f - wOp) + gray * wOp;
                }
            }
        }
        
        return result;
    }
    
    // Standard Median Logic
    ImageBuffer result = img; // Copy
    float* outData = result.data().data();
    const float* inData = img.data().data();
    int w = img.width();
    int h = img.height();
    int c = img.channels();

    std::vector<std::pair<int,int>> centers;
    int angles[] = {0, 60, 120, 180, 240, 300};
    for(int ang : angles) {
        float rrad = ang * 3.14159f / 180.0f;
        int dx = (int)(cos(rrad) * (radius * 1.5f));
        int dy = (int)(sin(rrad) * (radius * 1.5f));
        centers.push_back({x + dx, y + dy});
    }
    
    float tgtMedian = medianCircle(img, x, y, radius, channels);
    std::vector<std::pair<float, int>> diffs;
    
    for(size_t i=0; i<centers.size(); ++i) {
        float m = medianCircle(img, centers[i].first, centers[i].second, radius, channels);
        diffs.push_back({std::abs(m - tgtMedian), (int)i});
    }
    std::sort(diffs.begin(), diffs.end());
    
    std::vector<std::pair<int,int>> selCenters;
    for(int i=0; i<(int)std::min((size_t)3, diffs.size()); ++i) {
        selCenters.push_back(centers[diffs[i].second]);
    }
    
    // Process pixels in circle
    int x0 = std::max(0, x - radius);
    int x1 = std::min(w, x + radius + 1);
    int y0 = std::max(0, y - radius);
    int y1 = std::min(h, y + radius + 1);
    
    for (int i = y0; i < y1; ++i) {
        for (int j = x0; j < x1; ++j) {
            float dist = std::hypot(j - x, i - y);
            if (dist > radius) continue;
            
            [[maybe_unused]] float weight = 1.0f;
            if (feather > 0.0f) {
                weight = std::clamp((radius - dist) / (radius * feather), 0.0f, 1.0f);
            }
            
            // Collect samples from neighbors
            std::vector<float> samples;
            for(auto& center : selCenters) {
                int sj = j + (center.first - x);
                int si = i + (center.second - y);
                if (sj >= 0 && sj < w && si >= 0 && si < h) {
                     for (int ch : channels) {
                         if (ch < c) samples.push_back(inData[(si*w+sj)*c + ch]);
                     }
                }
            }
            
            if (samples.empty()) {
                 continue; 
            }
        }
    }
    
    for (int ch : channels) {
        if (ch >= c) continue;
        for (int i = y0; i < y1; ++i) {
            for (int j = x0; j < x1; ++j) {
                float dist = std::hypot(j - x, i - y);
                if (dist > radius) continue;
                
                float weight = 1.0f;
                if (feather > 0.0f) {
                    weight = std::clamp((radius - dist) / (radius * feather), 0.0f, 1.0f);
                }
                
                std::vector<float> samples;
                for(auto& center : selCenters) {
                     int sj = j + (center.first - x);
                     int si = i + (center.second - y);
                     if (sj >= 0 && sj < w && si >= 0 && si < h) {
                         samples.push_back(inData[(si*w+sj)*c + ch]);
                     }
                }
                
                float replVal = inData[(i*w+j)*c + ch];
                if (!samples.empty()) {
                    std::nth_element(samples.begin(), samples.begin() + samples.size()/2, samples.end());
                    replVal = samples[samples.size()/2];
                }
                
                float orig = inData[(i*w+j)*c + ch];
                float finalVal = (1.0f - opacity * weight) * orig + (opacity * weight) * replVal;
                outData[(i*w+j)*c + ch] = finalVal;
            }
        }
    }
    
    return result;
}


// ================= Dialog =================

CorrectionBrushDialog::CorrectionBrushDialog(QWidget* parent) : DialogBase(parent, tr("Correction Brush"), 900, 650) {
    if (MainWindowCallbacks* mw = getCallbacks()) {
        if (mw->getCurrentViewer()) {
            m_currentImage = mw->getCurrentViewer()->getBuffer();
        }
    }
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // View
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
    m_view->setDragMode(QGraphicsView::NoDrag); // We handle clicks
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    
    m_pixItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixItem);
    
    m_cursorItem = new QGraphicsEllipseItem(0,0,10,10);
    m_cursorItem->setPen(QPen(Qt::red));
    m_cursorItem->setBrush(Qt::NoBrush);
    m_cursorItem->setVisible(false);
    m_cursorItem->setZValue(100);
    m_scene->addItem(m_cursorItem);
    
    mainLayout->addWidget(m_view);
    
    // Zoom Controls
    QHBoxLayout* zoomLayout = new QHBoxLayout();
    zoomLayout->addStretch();
    QPushButton* zOut = new QPushButton(tr("Zoom Out"));
    QPushButton* zIn = new QPushButton(tr("Zoom In"));
    QPushButton* zFit = new QPushButton(tr("Fit"));
    zoomLayout->addWidget(zOut);
    zoomLayout->addWidget(zIn);
    zoomLayout->addWidget(zFit);
    zoomLayout->addStretch();
    mainLayout->addLayout(zoomLayout);
    
    connect(zOut, &QPushButton::clicked, this, &CorrectionBrushDialog::onZoomOut);
    connect(zIn, &QPushButton::clicked, this, &CorrectionBrushDialog::onZoomIn);
    connect(zFit, &QPushButton::clicked, this, &CorrectionBrushDialog::onFit);
    
    // Controls
    QGroupBox* ctrlGroup = new QGroupBox(tr("Controls"));
    QFormLayout* form = new QFormLayout(ctrlGroup);
    
    m_radiusSlider = new QSlider(Qt::Horizontal); m_radiusSlider->setRange(1, 200); m_radiusSlider->setValue(12);
    m_featherSlider = new QSlider(Qt::Horizontal); m_featherSlider->setRange(0, 100); m_featherSlider->setValue(50);
    m_opacitySlider = new QSlider(Qt::Horizontal); m_opacitySlider->setRange(0, 100); m_opacitySlider->setValue(100);
    
    form->addRow(tr("Radius:"), m_radiusSlider);
    form->addRow(tr("Feather:"), m_featherSlider);
    form->addRow(tr("Opacity:"), m_opacitySlider);
    
    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(tr("Content-Aware (Slow, Best)"), (int)CorrectionMethod::ContentAware);
    m_methodCombo->addItem(tr("Standard (Median)"), (int)CorrectionMethod::Standard);
    form->addRow(tr("Method:"), m_methodCombo);
    
    m_autoStretchCheck = new QCheckBox(tr("Auto-stretch preview"));
    m_autoStretchCheck->setChecked(false);
    m_linkedCheck = new QCheckBox(tr("Linked channels"));
    m_linkedCheck->setChecked(true);
    m_targetMedianSpin = new QDoubleSpinBox();
    m_targetMedianSpin->setRange(0.01, 0.9); m_targetMedianSpin->setValue(0.25);
    
    form->addRow(m_autoStretchCheck);
    form->addRow(tr("Target Median:"), m_targetMedianSpin);
    form->addRow(m_linkedCheck);
    
    mainLayout->addWidget(ctrlGroup);
    
    // Bottom btns
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_undoBtn = new QPushButton(tr("Undo"));
    m_redoBtn = new QPushButton(tr("Redo"));
    QPushButton* applyBtn = new QPushButton(tr("Apply to Document"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);
    
    btnLayout->addWidget(m_undoBtn);
    btnLayout->addWidget(m_redoBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
    
    connect(m_undoBtn, &QPushButton::clicked, this, &CorrectionBrushDialog::onUndo);
    connect(m_redoBtn, &QPushButton::clicked, this, &CorrectionBrushDialog::onRedo);
    connect(applyBtn, &QPushButton::clicked, this, &CorrectionBrushDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &CorrectionBrushDialog::reject);
    
    connect(m_autoStretchCheck, &QCheckBox::toggled, this, &CorrectionBrushDialog::updateDisplay);
    connect(m_linkedCheck, &QCheckBox::toggled, this, &CorrectionBrushDialog::updateDisplay);
    connect(m_targetMedianSpin, &QDoubleSpinBox::valueChanged, this, &CorrectionBrushDialog::updateDisplay);
    
    updateDisplay();
    // Defer fit until the dialog is fully laid out and visible
    QTimer::singleShot(0, this, &CorrectionBrushDialog::onFit);

}

CorrectionBrushDialog::~CorrectionBrushDialog() {
    // Worker deletes itself
}

void CorrectionBrushDialog::setSource(const ImageBuffer& img) {
    m_currentImage = img;
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);
    updateDisplay();
    onFit();
}

bool CorrectionBrushDialog::eventFilter(QObject* src, QEvent* ev) {
    if (src == m_view->viewport()) {
        if (ev->type() == QEvent::MouseMove) {
             QMouseEvent* me = static_cast<QMouseEvent*>(ev);
             QPointF p = m_view->mapToScene(me->pos());
             int r = m_radiusSlider->value();
             m_cursorItem->setRect(p.x()-r, p.y()-r, 2*r, 2*r);
             m_cursorItem->setVisible(true);
             
             // Right-button drag for panning
             if (me->buttons() & Qt::RightButton) {
                 QPointF delta = me->pos() - m_lastPanPos;
                 m_view->horizontalScrollBar()->setValue(m_view->horizontalScrollBar()->value() - delta.x());
                 m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->value() - delta.y());
                 m_lastPanPos = me->pos();
                 return true;
             }
        } else if (ev->type() == QEvent::Wheel) {
            QWheelEvent* we = static_cast<QWheelEvent*>(ev);
            float factor = (we->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
            float newZoom = std::clamp(m_zoom * factor, 0.05f, 8.0f);
            float actualFactor = newZoom / m_zoom;
            m_zoom = newZoom;
            m_view->scale(actualFactor, actualFactor);
            return true;
        } else if (ev->type() == QEvent::Leave) {
             m_cursorItem->setVisible(false);
        } else if (ev->type() == QEvent::MouseButtonPress) {
             QMouseEvent* me = static_cast<QMouseEvent*>(ev);
             if (me->button() == Qt::LeftButton) {
                 healAt(m_view->mapToScene(me->pos()));
                 return true;
             } else if (me->button() == Qt::RightButton) {
                 m_lastPanPos = me->pos();
                 return true;
             }
        }
    }
    return QDialog::eventFilter(src, ev);
}

void CorrectionBrushDialog::healAt(QPointF scenePos) {
    if (m_busy) return;
    if (!m_currentImage.isValid()) return;
    
    int x = (int)scenePos.x();
    int y = (int)scenePos.y();
    if (x < 0 || y < 0 || x >= m_currentImage.width() || y >= m_currentImage.height()) return;
    
    int r = m_radiusSlider->value();
    float f = m_featherSlider->value() / 100.0f;
    float op = m_opacitySlider->value() / 100.0f;
    CorrectionMethod method = (CorrectionMethod)m_methodCombo->currentData().toInt();
    
    std::vector<int> chans = {0, 1, 2}; // All channels
    
    m_busy = true;
    m_undoStack.push_back(m_currentImage);
    m_undoBtn->setEnabled(true);
    m_redoStack.clear();
    m_redoBtn->setEnabled(false);
    
    CorrectionWorker* w = new CorrectionWorker(m_currentImage, x, y, r, f, op, chans, method);
    connect(w, &CorrectionWorker::finished, this, &CorrectionBrushDialog::onWorkerFinished);
    QThreadPool::globalInstance()->start(w);
}

void CorrectionBrushDialog::onWorkerFinished(ImageBuffer result) {
    m_currentImage = result;
    updateDisplay();
    m_busy = false;
}

void CorrectionBrushDialog::onUndo() {
    if (m_undoStack.empty() || m_busy) return;
    m_redoStack.push_back(m_currentImage);
    m_currentImage = m_undoStack.back();
    m_undoStack.pop_back();
    m_undoBtn->setEnabled(!m_undoStack.empty());
    m_redoBtn->setEnabled(true);
    updateDisplay();
}

void CorrectionBrushDialog::onRedo() {
    if (m_redoStack.empty() || m_busy) return;
    m_undoStack.push_back(m_currentImage);
    m_currentImage = m_redoStack.back();
    m_redoStack.pop_back();
    m_undoBtn->setEnabled(true);
    m_redoBtn->setEnabled(!m_redoStack.empty());
    updateDisplay();
}

void CorrectionBrushDialog::updateDisplay() {
    if (!m_currentImage.isValid()) return;
    
    ImageBuffer::DisplayMode mode = m_autoStretchCheck->isChecked() ? ImageBuffer::Display_AutoStretch : ImageBuffer::Display_Linear;
    
    QImage qimg = m_currentImage.getDisplayImage(mode, m_linkedCheck->isChecked());
    m_pixItem->setPixmap(QPixmap::fromImage(qimg));
    // Lock scene rect to the pixmap bounds so the cursor ellipse item
    // moving outside never expands the scene, which would cause the
    // view to auto-scroll and make the image appear to drift.
    m_scene->setSceneRect(m_pixItem->boundingRect());
}

void CorrectionBrushDialog::onZoomIn() { setZoom(m_zoom * 1.25f); }
void CorrectionBrushDialog::onZoomOut() { setZoom(m_zoom / 1.25f); }

void CorrectionBrushDialog::setZoom(float z) {
    m_zoom = std::clamp(z, 0.05f, 4.0f);
    m_view->resetTransform();
    m_view->scale(m_zoom, m_zoom);
}

void CorrectionBrushDialog::onFit() {
    if (m_pixItem->pixmap().isNull()) return;
    m_view->fitInView(m_pixItem, Qt::KeepAspectRatio);
    m_zoom = m_view->transform().m11();
}

void CorrectionBrushDialog::onApply() {
    if (MainWindowCallbacks* mw = getCallbacks()) {
        ImageViewer* v = mw->getCurrentViewer();
        if (v) {
            v->pushUndo(); // Save undo state before modifying the document
            v->setBuffer(m_currentImage, v->getBuffer().name(), true);
            accept();
        }
    }
}
