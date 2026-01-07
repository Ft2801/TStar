#include "ClaheDialog.h"
#include "../MainWindow.h"
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QGridLayout>
#include <QGroupBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
#include <QTimer>
#include <opencv2/opencv.hpp>

ClaheDialog::ClaheDialog(MainWindow* parent) 
    : QDialog(parent), m_mainWindow(parent), m_previewDirty(false)
{
    setWindowTitle(tr("CLAHE (Contrast Limited Adaptive Histogram Equalization)"));
    resize(800, 600);
    
    // Grab current image
    if (m_mainWindow->currentViewer() && m_mainWindow->currentViewer()->getBuffer().isValid()) {
        m_sourceImage = m_mainWindow->currentViewer()->getBuffer();
    }
    
    setupUi();
    onReset(); // Default values

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

ClaheDialog::~ClaheDialog() {}

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
    m_clipSlider = new QSlider(Qt::Horizontal);
    m_clipSlider->setRange(10, 80); // 1.0 to 8.0
    m_clipSlider->setValue(20);
    m_clipLabel = new QLabel("2.0");
    
    connect(m_clipSlider, &QSlider::valueChanged, [this](int val){
        m_clipLabel->setText(QString::number(val / 10.0, 'f', 1));
        updatePreview();
    });
    
    grid->addWidget(new QLabel(tr("Clip Limit:")), 0, 0);
    grid->addWidget(m_clipSlider, 0, 1);
    grid->addWidget(m_clipLabel, 0, 2);
    
    // Tile Size
    m_tileSlider = new QSlider(Qt::Horizontal);
    m_tileSlider->setRange(1, 16); // Grid Size (e.g. 8 means 8x8)
    m_tileSlider->setValue(8);
    m_tileLabel = new QLabel("8x8");
    
    connect(m_tileSlider, &QSlider::valueChanged, [this](int val){
        m_tileLabel->setText(QString("%1x%1").arg(val));
        updatePreview();
    });
    
    grid->addWidget(new QLabel(tr("Grid Size:")), 1, 0);
    grid->addWidget(m_tileSlider, 1, 1);
    grid->addWidget(m_tileLabel, 1, 2);
    
    mainLayout->addWidget(grp);
    
    // 2. Preview
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setBackgroundBrush(Qt::black);
    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
    mainLayout->addWidget(m_view, 1);
    
    // 3. Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnReset = new QPushButton(tr("Reset"));
    QPushButton* btnApply = new QPushButton(tr("Apply"));
    QPushButton* btnClose = new QPushButton(tr("Close"));
    
    connect(btnReset, &QPushButton::clicked, this, &ClaheDialog::onReset);
    connect(btnApply, &QPushButton::clicked, this, &ClaheDialog::onApply);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch();
    btnLayout->addWidget(btnReset);
    btnLayout->addWidget(btnApply);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);
}

void ClaheDialog::onReset() {
    m_clipSlider->setValue(20);
    m_tileSlider->setValue(8);
    updatePreview();
}

void ClaheDialog::updatePreview() {
    if (!m_sourceImage.isValid()) return;
    
    float clip = m_clipSlider->value() / 10.0f;
    int grid = m_tileSlider->value();
    
    createPreview(m_sourceImage, clip, grid);
    
    // Convert to QPixmap for display
    QImage qimg = m_previewImage.getDisplayImage();
    m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
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
        // For color images: cvtColor BGR2Lab requires 8-bit or 32-bit float
        // Use 8-bit for simplicity (OpenCV 4.5.5 limitation)
        cv::Mat mat8(h, w, CV_8UC3);
        
        // Float -> 8-bit
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            mat8.at<cv::Vec3b>(i / w, i % w)[0] = (uint8_t)(std::clamp(data[i * 3 + 2], 0.0f, 1.0f) * 255.0f); // B
            mat8.at<cv::Vec3b>(i / w, i % w)[1] = (uint8_t)(std::clamp(data[i * 3 + 1], 0.0f, 1.0f) * 255.0f); // G
            mat8.at<cv::Vec3b>(i / w, i % w)[2] = (uint8_t)(std::clamp(data[i * 3 + 0], 0.0f, 1.0f) * 255.0f); // R
        }
        
        // Convert to Lab, apply CLAHE to L, convert back
        cv::Mat lab;
        cv::cvtColor(mat8, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> lab_planes;
        cv::split(lab, lab_planes);
        
        clahe->apply(lab_planes[0], lab_planes[0]);
        
        cv::merge(lab_planes, lab);
        cv::Mat res;
        cv::cvtColor(lab, res, cv::COLOR_Lab2BGR);
        
        // 8-bit -> Float
        #pragma omp parallel for
        for (int i = 0; i < w * h; ++i) {
            outData[i * 3 + 0] = res.at<cv::Vec3b>(i / w, i % w)[2] / 255.0f; // R
            outData[i * 3 + 1] = res.at<cv::Vec3b>(i / w, i % w)[1] / 255.0f; // G
            outData[i * 3 + 2] = res.at<cv::Vec3b>(i / w, i % w)[0] / 255.0f; // B
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
    float clip = m_clipSlider->value() / 10.0f;
    int grid = m_tileSlider->value();
    
    m_mainWindow->pushUndo();
    
    // Apply directly to current image
    ImageViewer* viewer = m_mainWindow->currentViewer();
    if (viewer && viewer->getBuffer().isValid()) {
        ImageBuffer& buffer = viewer->getBuffer();
        
        // Save original for mask blending
        ImageBuffer original;
        if (buffer.hasMask()) {
            original = buffer;
        }
        
        // Re-run logic on full image
        createPreview(buffer, clip, grid);
        buffer = m_previewImage;
        
        // Blend with mask if present
        if (original.isValid() && buffer.hasMask()) {
            buffer.blendResult(original);
        }
        
        viewer->refreshDisplay();
        m_mainWindow->statusBar()->showMessage(tr("CLAHE applied."));
    }
    
    accept();
}
