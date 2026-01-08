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
#include <QScrollBar>
#include <QMessageBox>
#include <QComboBox>
#include <cmath>
#include <algorithm>
#include <QScrollBar>
#include <opencv2/opencv.hpp>
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
        // OpenCV Telea Inpaint - Professional quality content-aware fill
        int w = img.width();
        int h = img.height();
        int c = img.channels();
        
        // Define ROI to minimize processing
        int pad = radius + 2;
        int x0 = std::max(0, x - pad);
        int y0 = std::max(0, y - pad);
        int x1 = std::min(w, x + pad + 1);
        int y1 = std::min(h, y + pad + 1);
        int roiW = x1 - x0;
        int roiH = y1 - y0;
        
        if (roiW <= 0 || roiH <= 0) return img;
        
        cv::Mat roiMat;
        if (c == 3) roiMat = cv::Mat(roiH, roiW, CV_8UC3);
        else roiMat = cv::Mat(roiH, roiW, CV_8UC1);
        
        const float* srcData = img.data().data();
        
        for (int i=0; i<roiH; ++i) {
            for (int j=0; j<roiW; ++j) {
                int sx = x0 + j;
                int sy = y0 + i;
                if (c == 3) {
                    roiMat.at<cv::Vec3b>(i,j)[0] = (uint8_t)(std::clamp(srcData[(sy*w+sx)*c+2], 0.0f, 1.0f) * 255.0f); // B
                    roiMat.at<cv::Vec3b>(i,j)[1] = (uint8_t)(std::clamp(srcData[(sy*w+sx)*c+1], 0.0f, 1.0f) * 255.0f); // G
                    roiMat.at<cv::Vec3b>(i,j)[2] = (uint8_t)(std::clamp(srcData[(sy*w+sx)*c+0], 0.0f, 1.0f) * 255.0f); // R
                } else {
                    roiMat.at<uint8_t>(i,j) = (uint8_t)(std::clamp(srcData[(sy*w+sx)*c], 0.0f, 1.0f) * 255.0f);
                }
            }
        }
        
        // Create Mask
        cv::Mat mask = cv::Mat::zeros(roiH, roiW, CV_8UC1);
        cv::circle(mask, cv::Point(x - x0, y - y0), radius, cv::Scalar(255), -1);
        
        // Inpaint using Telea (Fast Marching Method - best quality)
        cv::Mat dilatedMask;
        cv::dilate(mask, dilatedMask, cv::Mat(), cv::Point(-1,-1), 2);
        
        cv::Mat inpainted;
        cv::inpaint(roiMat, mask, inpainted, 3.0, cv::INPAINT_TELEA);
        
        // Blend back
        ImageBuffer result = img;
        float* dstData = result.data().data();
        
        for (int i=0; i<roiH; ++i) {
            for (int j=0; j<roiW; ++j) {
                // Check if inside mask (with feather support)
                float dist = std::hypot(x0 + j - x, y0 + i - y);
                if (dist > radius) continue; // Pure outside
                
                float weight = 1.0f;
                // Feather
                if (feather > 0.0f) {
                     weight = std::clamp((radius - dist) / (radius * feather), 0.0f, 1.0f);
                }
                
                float wOp = weight * opacity;
                
                int sx = x0 + j;
                int sy = y0 + i;
                
                if (c == 3) {
                    float b = inpainted.at<cv::Vec3b>(i,j)[0] / 255.0f;
                    float g = inpainted.at<cv::Vec3b>(i,j)[1] / 255.0f;
                    float r = inpainted.at<cv::Vec3b>(i,j)[2] / 255.0f;
                    
                    dstData[(sy*w+sx)*c+0] = dstData[(sy*w+sx)*c+0] * (1.0f - wOp) + r * wOp;
                    dstData[(sy*w+sx)*c+1] = dstData[(sy*w+sx)*c+1] * (1.0f - wOp) + g * wOp;
                    dstData[(sy*w+sx)*c+2] = dstData[(sy*w+sx)*c+2] * (1.0f - wOp) + b * wOp;
                } else {
                    float v = inpainted.at<uint8_t>(i,j) / 255.0f;
                    dstData[(sy*w+sx)*c] = dstData[(sy*w+sx)*c] * (1.0f - wOp) + v * wOp;
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
    m_autoStretchCheck->setChecked(true);
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
    onFit();

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
            v->setBuffer(m_currentImage, v->getBuffer().name());
            accept();
        }
    }
}
