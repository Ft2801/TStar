#include "ReferenceAlignDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QIcon>
#include <QApplication>
#include <opencv2/opencv.hpp>

ReferenceAlignDialog::ReferenceAlignDialog(QWidget* parent, const ImageBuffer& refBuffer)
    : DialogBase(parent), m_currentBuffer(refBuffer)
{
    setWindowTitle(tr("Align Reference Image"));
    setMinimumSize(600, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* infoLabel = new QLabel(tr("Check if the reference image pattern matches your image.\n"
                                      "Use the buttons below to flip or rotate it if necessary."), this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    // Preview area
    m_previewLabel = new QLabel(this);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel { background-color: #1e1e1e; border: 1px solid #444; }");
    m_previewLabel->setMinimumSize(400, 400);
    m_previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_previewLabel, 1);

    // Toolbar for transformations
    QHBoxLayout* toolLayout = new QHBoxLayout();
    
    QPushButton* btnFlipH = new QPushButton(tr("Flip Horizontal"), this);
    QPushButton* btnFlipV = new QPushButton(tr("Flip Vertical"), this);
    QPushButton* btnRotCW = new QPushButton(tr("Rotate 90° CW"), this);
    QPushButton* btnRotCCW = new QPushButton(tr("Rotate 90° CCW"), this);

    toolLayout->addWidget(btnFlipH);
    toolLayout->addWidget(btnFlipV);
    toolLayout->addWidget(btnRotCCW);
    toolLayout->addWidget(btnRotCW);
    toolLayout->addStretch();

    mainLayout->addLayout(toolLayout);

    // Dialog buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(btnFlipH, &QPushButton::clicked, this, &ReferenceAlignDialog::onFlipHorizontal);
    connect(btnFlipV, &QPushButton::clicked, this, &ReferenceAlignDialog::onFlipVertical);
    connect(btnRotCW, &QPushButton::clicked, this, &ReferenceAlignDialog::onRotateCW);
    connect(btnRotCCW, &QPushButton::clicked, this, &ReferenceAlignDialog::onRotateCCW);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updatePreview();
}

ReferenceAlignDialog::~ReferenceAlignDialog() {}

ImageBuffer ReferenceAlignDialog::getAlignedBuffer() const {
    return m_currentBuffer;
}

void ReferenceAlignDialog::onFlipHorizontal() {
    applyTransformationToBuffer(1, false); // flipCode 1 = horizontal
}

void ReferenceAlignDialog::onFlipVertical() {
    applyTransformationToBuffer(0, false); // flipCode 0 = vertical
}

void ReferenceAlignDialog::onRotateCW() {
    // Transpose then flip horizontally
    applyTransformationToBuffer(1, true);
}

void ReferenceAlignDialog::onRotateCCW() {
    // Transpose then flip vertically
    applyTransformationToBuffer(0, true);
}

void ReferenceAlignDialog::applyTransformationToBuffer(int flipCode, bool transpose) {
    if (!m_currentBuffer.isValid()) return;

    int w = m_currentBuffer.width();
    int h = m_currentBuffer.height();
    int c = m_currentBuffer.channels();
    int cvType = (c == 3) ? CV_32FC3 : CV_32FC1;

    cv::Mat mat(h, w, cvType, m_currentBuffer.data().data());
    cv::Mat dst;

    if (transpose) {
        cv::transpose(mat, dst);
        cv::flip(dst, dst, flipCode);
        m_currentBuffer.resize(h, w, c); // Swap w and h
    } else {
        cv::flip(mat, dst, flipCode);
    }

    std::memcpy(m_currentBuffer.data().data(), dst.ptr<float>(), m_currentBuffer.data().size() * sizeof(float));
    updatePreview();
}

QImage ReferenceAlignDialog::bufferToQImage(const ImageBuffer& buf) {
    if (!buf.isValid()) return QImage();

    int w = buf.width();
    int h = buf.height();
    int c = buf.channels();

    QImage img(w, h, QImage::Format_RGB888);
    const float* src = buf.data().data();
    
    // Auto-stretch for preview
    float minVal = 1.0f, maxVal = 0.0f;
    size_t totalPixels = (size_t)w * h * c;
    
    // Fast min/max approximation
    for (size_t i = 0; i < totalPixels; i += c) {
        float v = src[i];
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }
    
    if (maxVal <= minVal) maxVal = minVal + 1e-5f;
    float scale = 255.0f / (maxVal - minVal);

    for (int y = 0; y < h; ++y) {
        uchar* scanLine = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * c;
            int r, g, b;
            
            if (c == 1) {
                int v = static_cast<int>((src[idx] - minVal) * scale);
                v = std::clamp(v, 0, 255);
                r = g = b = v;
            } else {
                r = std::clamp(static_cast<int>((src[idx] - minVal) * scale), 0, 255);
                g = std::clamp(static_cast<int>((src[idx+1] - minVal) * scale), 0, 255);
                b = std::clamp(static_cast<int>((src[idx+2] - minVal) * scale), 0, 255);
            }
            
            int pxOut = x * 3;
            scanLine[pxOut] = r;
            scanLine[pxOut+1] = g;
            scanLine[pxOut+2] = b;
        }
    }
    return img;
}

void ReferenceAlignDialog::updatePreview() {
    QImage img = bufferToQImage(m_currentBuffer);
    if (!img.isNull()) {
        QPixmap pix = QPixmap::fromImage(img);
        // Scale to fit available QLabel space
        int w = m_previewLabel->width();
        int h = m_previewLabel->height();
        m_previewLabel->setPixmap(pix.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
