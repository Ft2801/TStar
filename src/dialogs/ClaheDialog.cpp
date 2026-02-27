#include "ClaheDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QGridLayout>
#include <QGroupBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
#include <QTimer>
#include <QWheelEvent>
#include <opencv2/opencv.hpp>

ClaheDialog::ClaheDialog(QWidget* parent) 
    : DialogBase(parent, tr("CLAHE"))
{
    m_mainWindow = getCallbacks();
    setWindowTitle(tr("CLAHE (Contrast Limited Adaptive Histogram Equalization)"));
    resize(800, 600);
    
    // Grab current image
    if (m_mainWindow && m_mainWindow->getCurrentViewer() && m_mainWindow->getCurrentViewer()->getBuffer().isValid()) {
        m_sourceImage = m_mainWindow->getCurrentViewer()->getBuffer();
    }
    
    // Debounce timer: fires 200 ms after the last slider move
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(200);
    connect(m_previewTimer, &QTimer::timeout, this, &ClaheDialog::updatePreview);

    setupUi();
    // Defer initial preview until after the dialog has fully appeared
    QTimer::singleShot(300, this, &ClaheDialog::updatePreview);
}

ClaheDialog::~ClaheDialog() {}

bool ClaheDialog::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_view->viewport() && ev->type() == QEvent::Wheel) {
        QWheelEvent* we = static_cast<QWheelEvent*>(ev);
        float factor  = (we->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
        float newZoom = std::clamp(m_zoom * factor, 0.05f, 32.0f);
        float actual  = newZoom / m_zoom;
        m_zoom        = newZoom;
        m_view->scale(actual, actual);
        return true;
    }
    return DialogBase::eventFilter(obj, ev);
}

void ClaheDialog::setSource(const ImageBuffer& img) {
    m_sourceImage = img;
    updatePreview();
}

void ClaheDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 1. Controls
    QGroupBox* grp = new QGroupBox(tr("Parameters"));
    QGridLayout* grid = new QGridLayout(grp);
    
    // Clip Limit
    // Internally stored as integer × 0.01 → range [1, 5000]
    m_clipSlider = new QSlider(Qt::Horizontal);
    m_clipSlider->setRange(1, 5000);   // 0.01 – 50.00
    m_clipSlider->setValue(200);       // default 2.00
    m_clipSlider->setPageStep(10);     // page-step = 0.10 
    m_clipLabel  = new QLabel("2.00");
    m_clipLabel->setMinimumWidth(40);

    connect(m_clipSlider, &QSlider::valueChanged, [this](int val){
        m_clipLabel->setText(QString::number(val / 100.0, 'f', 2));
        if (m_chkPreview && m_chkPreview->isChecked())
            m_previewTimer->start();
    });

    grid->addWidget(new QLabel(tr("Clip Limit:")), 0, 0);
    grid->addWidget(m_clipSlider, 0, 1);
    grid->addWidget(m_clipLabel,  0, 2);

    // Tile Grid Size  (1 – 128, default 8)
    m_tileSlider = new QSlider(Qt::Horizontal);
    m_tileSlider->setRange(1, 128);  
    m_tileSlider->setValue(8);
    m_tileSlider->setPageStep(10);
    m_tileLabel  = new QLabel("8x8");
    m_tileLabel->setMinimumWidth(40);

    connect(m_tileSlider, &QSlider::valueChanged, [this](int val){
        m_tileLabel->setText(QString("%1x%1").arg(val));
        if (m_chkPreview && m_chkPreview->isChecked())
            m_previewTimer->start();
    });

    grid->addWidget(new QLabel(tr("Grid Size:")), 1, 0);
    grid->addWidget(m_tileSlider, 1, 1);
    grid->addWidget(m_tileLabel,  1, 2);

    // Opacity  (0 – 100%, default 100)
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);
    m_opacityLabel  = new QLabel("100%");
    m_opacityLabel->setMinimumWidth(40);

    connect(m_opacitySlider, &QSlider::valueChanged, [this](int val){
        m_opacityLabel->setText(QString("%1%").arg(val));
        if (m_chkPreview && m_chkPreview->isChecked())
            m_previewTimer->start();
    });

    grid->addWidget(new QLabel(tr("Opacity:")), 2, 0);
    grid->addWidget(m_opacitySlider, 2, 1);
    grid->addWidget(m_opacityLabel,  2, 2);

    mainLayout->addWidget(grp);
    
    // 2. Preview
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setBackgroundBrush(Qt::black);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->viewport()->installEventFilter(this);
    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
    mainLayout->addWidget(m_view, 1);
    
    // 3. Preview toggle
    m_chkPreview = new QCheckBox(tr("Preview"), this);
    m_chkPreview->setChecked(true);
    connect(m_chkPreview, &QCheckBox::toggled, [this](bool on){
        if (on) {
            updatePreview();   // show CLAHE result
        } else {
            // Show the original unprocessed image
            if (m_sourceImage.isValid()) {
                QImage qimg = m_sourceImage.getDisplayImage();
                m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
                m_scene->setSceneRect(m_pixmapItem->boundingRect());
                // do NOT call fitInView — preserve current zoom
            }
        }
    });

    // 4. Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnReset = new QPushButton(tr("Reset"));
    QPushButton* btnApply = new QPushButton(tr("Apply"));
    QPushButton* btnClose = new QPushButton(tr("Close"));

    connect(btnReset, &QPushButton::clicked, this, &ClaheDialog::onReset);
    connect(btnApply, &QPushButton::clicked, this, &ClaheDialog::onApply);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(m_chkPreview);
    btnLayout->addStretch();
    btnLayout->addWidget(btnReset);
    btnLayout->addWidget(btnClose);
    btnLayout->addWidget(btnApply);
    mainLayout->addLayout(btnLayout);
}

void ClaheDialog::onReset() {
    // Block signals to avoid multiple premature preview updates
    m_clipSlider->blockSignals(true);
    m_tileSlider->blockSignals(true);
    m_clipSlider->setValue(200);      // = 2.00  
    m_clipLabel->setText("2.00");
    m_tileSlider->setValue(8);        // = 8×8   
    m_tileLabel->setText("8x8");
    if (m_opacitySlider) { m_opacitySlider->blockSignals(true); m_opacitySlider->setValue(100); m_opacityLabel->setText("100%"); m_opacitySlider->blockSignals(false); }
    m_clipSlider->blockSignals(false);
    m_tileSlider->blockSignals(false);
    if (m_chkPreview) m_chkPreview->setChecked(true);
    updatePreview();
}

void ClaheDialog::updatePreview() {
    if (!m_sourceImage.isValid()) return;
    if (m_chkPreview && !m_chkPreview->isChecked()) return;

    float clip = m_clipSlider->value() / 100.0f;  // scale: integer/100 → 0.01–50
    int grid = m_tileSlider->value();
    
    createPreview(m_sourceImage, clip, grid);

    float opacity = m_opacitySlider ? m_opacitySlider->value() / 100.0f : 1.0f;
    ImageBuffer displayBuf = m_previewImage;
    if (m_sourceImage.hasMask()) {
        MaskLayer ml = *m_sourceImage.getMask();
        ml.opacity *= opacity;
        displayBuf.setMask(ml);
        displayBuf.blendResult(m_sourceImage);
    } else if (opacity < 1.0f) {
        const auto& orig = m_sourceImage.data();
        auto& bd = displayBuf.data();
        for (size_t i = 0; i < bd.size(); ++i)
            bd[i] = bd[i] * opacity + orig[i] * (1.0f - opacity);
    }

    // Convert to QPixmap for display
    QImage qimg = displayBuf.getDisplayImage();
    m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    if (m_firstDisplay) {
        m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
        m_zoom = static_cast<float>(m_view->transform().m11());
        m_firstDisplay = false;
    }
}

void ClaheDialog::createPreview(const ImageBuffer& src, float clipLimit, int tileGridSize) {
    // OpenCV CLAHE Logic
    int w = src.width();
    int h = src.height();
    int c = src.channels();
    
    const float* data = src.data().data();
    
    // Apply CLAHE
    auto clahe = cv::createCLAHE(clipLimit, cv::Size(tileGridSize, tileGridSize));
    
    ImageBuffer out = src; // Copy meta
    float* outData = out.data().data();

    if (c == 3) {
        // Color images: work in 32F for Lab conversion, 16-bit for CLAHE on L channel
        cv::Mat mat32f(h, w, CV_32FC3);
        
        // Float [0,1] RGB -> 32F BGR for OpenCV
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            int y = i / w, x = i % w;
            mat32f.at<cv::Vec3f>(y, x)[0] = std::clamp(data[i * 3 + 2], 0.0f, 1.0f); // B
            mat32f.at<cv::Vec3f>(y, x)[1] = std::clamp(data[i * 3 + 1], 0.0f, 1.0f); // G
            mat32f.at<cv::Vec3f>(y, x)[2] = std::clamp(data[i * 3 + 0], 0.0f, 1.0f); // R
        }
        
        // Convert to Lab in 32F (L range [0,100], a/b stay full precision)
        cv::Mat lab;
        cv::cvtColor(mat32f, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> lab_planes;
        cv::split(lab, lab_planes);
        
        // CLAHE only works on 8-bit or 16-bit; convert L from [0,100] to 16-bit [0,65535]
        // using scale factor 65535/100 to maximize 16-bit range
        lab_planes[0].convertTo(lab_planes[0], CV_16U, 65535.0 / 100.0);
        clahe->apply(lab_planes[0], lab_planes[0]);
        lab_planes[0].convertTo(lab_planes[0], CV_32F, 100.0 / 65535.0);
        
        // Merge and convert back to BGR 32F
        cv::merge(lab_planes, lab);
        cv::Mat res;
        cv::cvtColor(lab, res, cv::COLOR_Lab2BGR);
        
        // 32F BGR -> Float [0,1] RGB
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            int y = i / w, x = i % w;
            outData[i * 3 + 0] = std::clamp(res.at<cv::Vec3f>(y, x)[2], 0.0f, 1.0f); // R
            outData[i * 3 + 1] = std::clamp(res.at<cv::Vec3f>(y, x)[1], 0.0f, 1.0f); // G
            outData[i * 3 + 2] = std::clamp(res.at<cv::Vec3f>(y, x)[0], 0.0f, 1.0f); // B
        }
    } else {
        // Grayscale: can use 16-bit for better precision
        cv::Mat mat16(h, w, CV_16UC1);
        
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            mat16.at<uint16_t>(i / w, i % w) = (uint16_t)(std::clamp(data[i], 0.0f, 1.0f) * 65535.0f);
        }
        
        cv::Mat res;
        clahe->apply(mat16, res);
        
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            outData[i] = res.at<uint16_t>(i / w, i % w) / 65535.0f;
        }
    }
    
    m_previewImage = out;
}

void ClaheDialog::onApply() {
    float clip = m_clipSlider->value() / 100.0f;  // scale: integer/100 → 0.01–50
    int grid = m_tileSlider->value();
    
    if (!m_mainWindow) return;
    // Apply directly to current image
    ImageViewer* viewer = m_mainWindow->getCurrentViewer();
    if (viewer && viewer->getBuffer().isValid()) {
        ImageBuffer& buffer = viewer->getBuffer();
        
        // Save original for mask blending or opacity
        ImageBuffer original;
        float opacity = m_opacitySlider ? m_opacitySlider->value() / 100.0f : 1.0f;
        if (buffer.hasMask() || opacity < 1.0f) {
            original = buffer;
        }

        viewer->pushUndo(); // Save state before applying

        // Re-run logic on full image
        createPreview(buffer, clip, grid);
        buffer = m_previewImage;

        // Blend with mask and/or opacity
        if (original.isValid() && original.hasMask()) {
            MaskLayer ml = *original.getMask();
            ml.opacity *= opacity;
            buffer.setMask(ml);
            buffer.blendResult(original);
        } else if (original.isValid() && opacity < 1.0f) {
            const auto& orig = original.data();
            auto& bd = buffer.data();
            for (size_t i = 0; i < bd.size(); ++i)
                bd[i] = bd[i] * opacity + orig[i] * (1.0f - opacity);
        }
        
        viewer->refreshDisplay();
        m_mainWindow->logMessage(tr("CLAHE applied."), 1, true);
    }
    
    accept();
}
