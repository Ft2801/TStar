#include "WavescaleHDRDialog.h"
#include "../ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QMessageBox>
#include <QGroupBox>
#include <cmath>
#include <algorithm>
#include <QShowEvent>

// ------ Color Space Helpers (Local) ------

struct LabPixel { float L, a, b; };
struct RGBPixel { float r, g, b; };

// Constants
static const float D65_Xn = 0.95047f;
static const float D65_Yn = 1.00000f;
static const float D65_Zn = 1.08883f;

static float pivot_xyz(float n) {
    return (n > 0.008856f) ? std::cbrt(n) : (7.787f * n + 16.0f/116.0f);
}

static LabPixel rgb2lab(float r, float g, float b) {
    // 1. Linearize sRGB (assuming input is sRGB)
    auto lin = [](float c) {
        return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    float R = lin(r);
    float G = lin(g);
    float B = lin(b);

    // 2. RGB -> XYZ
    float X = 0.4124564f * R + 0.3575761f * G + 0.1804375f * B;
    float Y = 0.2126729f * R + 0.7151522f * G + 0.0721750f * B;
    float Z = 0.0193339f * R + 0.1191920f * G + 0.9503041f * B;

    // 3. XYZ -> Lab
    float fx = pivot_xyz(X / D65_Xn);
    float fy = pivot_xyz(Y / D65_Yn);
    float fz = pivot_xyz(Z / D65_Zn);

    float L_val = 116.0f * fy - 16.0f;
    float a_val = 500.0f * (fx - fy);
    float b_val = 200.0f * (fy - fz);

    return { std::max(0.0f, L_val), a_val, b_val };
}

static float pivot_inv(float n) {
    float n3 = n * n * n;
    return (n3 > 0.008856f) ? n3 : ((n - 16.0f/116.0f) / 7.787f);
}

static RGBPixel lab2rgb(float L, float a, float b) {
    float fy = (L + 16.0f) / 116.0f;
    float fx = a / 500.0f + fy;
    float fz = fy - b / 200.0f;

    float X = D65_Xn * pivot_inv(fx);
    float Y = D65_Yn * pivot_inv(fy);
    float Z = D65_Zn * pivot_inv(fz);

    // XYZ -> Linear RGB
    float R =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    float B =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

    // Linear -> sRGB
    auto gam = [](float c) {
        return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f/2.4f) - 0.055f);
    };
    return { std::max(0.0f, std::min(1.0f, gam(R))),
             std::max(0.0f, std::min(1.0f, gam(G))),
             std::max(0.0f, std::min(1.0f, gam(B))) };
}


// ------ Worker ------

WavescaleHDRWorker::WavescaleHDRWorker(QObject* parent) : QThread(parent) {}

void WavescaleHDRWorker::setup(const ImageBuffer& src, int scales, float compression, float maskGamma, float dimmingGamma) {
    m_src = src; // Copy
    m_scales = scales;
    m_compression = compression;
    m_maskGamma = maskGamma;
    m_dimmingGamma = dimmingGamma;
}

void WavescaleHDRWorker::run() {
    if (!m_src.isValid()) return;

    emit progress(5);

    int w = m_src.width();
    int h = m_src.height();
    int ch = m_src.channels();
    const std::vector<float>& srcData = m_src.data();

    // 1. Convert to simple Lab planes (only Need L really, but need to reconstruct color)
    std::vector<float> L(w * h);
    std::vector<float> A(w * h);
    std::vector<float> B(w * h);

    for (int i = 0; i < w * h; ++i) {
        float r = srcData[i * ch + 0];
        float g = (ch > 1) ? srcData[i * ch + 1] : r;
        float b = (ch > 2) ? srcData[i * ch + 2] : r;
        
        LabPixel lab = rgb2lab(r, g, b);
        L[i] = lab.L;
        A[i] = lab.a;
        B[i] = lab.b;
    }

    emit progress(20);

    // 2. Decompose L
    // Need to scale L to 0..100? rgb2lab returns 0..100.
    // atrousDecompose expects float buffer.
    std::vector<std::vector<float>> planes = ImageBuffer::atrousDecompose(L, w, h, m_scales);

    emit progress(50);

    // 3. Process
    // Mask from L0
    std::vector<float> mask(w * h);
    for (int i = 0; i < w * h; ++i) {
        float val = std::clamp(L[i] / 100.0f, 0.0f, 1.0f);
        if (m_maskGamma != 1.0f) val = std::pow(val, m_maskGamma);
        mask[i] = val;
    }

    // Modulate planes
    // Residual (planes.back()) is untouched
    for (int s = 0; s < (int)planes.size() - 1; ++s) {
        float decay = std::pow(0.5f, s);
        // scale = (1 + (comp - 1) * mask * decay) * 2
        
        for (int i = 0; i < w * h; ++i) {
            float scaleFactor = (1.0f + (m_compression - 1.0f) * mask[i] * decay) * 2.0f;
            planes[s][i] *= scaleFactor;
        }
    }

    // 4. Reconstruct
    std::vector<float> Lr = ImageBuffer::atrousReconstruct(planes, w, h);

    // Midtones Alignment
    // Median of L0 vs Lr
    auto getMedian = [](std::vector<float>& d) {
        if (d.empty()) return 0.0f;
        std::vector<float> sub; 
        int step = d.size() / 10000 + 1;
        for(size_t i=0; i<d.size(); i+=step) sub.push_back(d[i]);
        size_t n = sub.size() / 2;
        std::nth_element(sub.begin(), sub.begin() + n, sub.end());
        return sub[n];
    };
    
    float med0 = getMedian(L);
    float med1 = getMedian(Lr);
    if (med1 < 1e-5f) med1 = 1.0f;
    float ratio = med0 / med1;
    
    for (float& v : Lr) v = std::clamp(v * ratio, 0.0f, 100.0f);

    emit progress(80);

    // 5. Convert back to RGB
    // Dimming Curve: Now user controlled.
    float dimGamma = m_dimmingGamma;

    ImageBuffer outBuf;
    outBuf.resize(w, h, 3); // output is always RGB
    std::vector<float>& outData = outBuf.data();

    for (int i = 0; i < w * h; ++i) {
        RGBPixel rgb = lab2rgb(Lr[i], A[i], B[i]);
        // Dimming
        outData[i * 3 + 0] = std::pow(std::clamp(rgb.r, 0.0f, 1.0f), dimGamma);
        outData[i * 3 + 1] = std::pow(std::clamp(rgb.g, 0.0f, 1.0f), dimGamma);
        outData[i * 3 + 2] = std::pow(std::clamp(rgb.b, 0.0f, 1.0f), dimGamma);
    }
    
    // Create Mask Buffer for preview
    ImageBuffer maskBuf;
    maskBuf.resize(w, h, 3);
    std::vector<float>& mData = maskBuf.data();
    for (int i = 0; i < w * h; ++i) {
        float v = mask[i];
        mData[i*3+0] = v;
        mData[i*3+1] = v;
        mData[i*3+2] = v;
    }

    emit progress(100);
    emit finished(outBuf, maskBuf);
}


// ------ Dialog ------
 
WavescaleHDRDialog::WavescaleHDRDialog(QWidget* parent, ImageViewer* targetViewer)
    : QDialog(parent), m_targetViewer(targetViewer)
{
    setWindowTitle(tr("Wavescale HDR"));
    resize(1000, 700);
    
    // Copy for preview
    if (m_targetViewer && m_targetViewer->getBuffer().isValid()) {
        m_originalBuffer = m_targetViewer->getBuffer();
    }
    m_previewBuffer = m_originalBuffer;

    createUI();
    m_worker = new WavescaleHDRWorker(this);
    connect(m_worker, &WavescaleHDRWorker::progress, m_progressBar, &QProgressBar::setValue);
    connect(m_worker, &WavescaleHDRWorker::finished, this, &WavescaleHDRDialog::onWorkerFinished);
    
    // Cache a small version of L channel for fast mask preview (e.g. max 400px)
    if (m_originalBuffer.isValid()) {
        int w = m_originalBuffer.width();
        int h = m_originalBuffer.height();
        
        float scale = std::min(1.0f, 400.0f / static_cast<float>(std::max(w, h)));
        m_cacheW = std::max(1, static_cast<int>(w * scale));
        m_cacheH = std::max(1, static_cast<int>(h * scale));
        
        m_L_channel_cache.resize(m_cacheW * m_cacheH);
        int ch = m_originalBuffer.channels();
        const float* d = m_originalBuffer.data().data();
        
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static)
        #endif
        for (int y = 0; y < m_cacheH; ++y) {
            for (int x = 0; x < m_cacheW; ++x) {
                // Nearest neighbor sampling for speed
                int sx = static_cast<int>(x / scale);
                int sy = static_cast<int>(y / scale);
                sx = std::min(sx, w - 1);
                sy = std::min(sy, h - 1);
                
                size_t idx = (static_cast<size_t>(sy) * w + sx) * ch;
                float r = d[idx + 0];
                float g = (ch > 1) ? d[idx + 1] : r;
                float b = (ch > 2) ? d[idx + 2] : r;
                // Fast Luminance for preview (avoiding expensive Lab conversion)
                m_L_channel_cache[y * m_cacheW + x] = (0.2126f * r + 0.7152f * g + 0.0722f * b) * 100.0f;
            }
        }
    }
    
    // Set initial buffer so view is not empty and can be fitted
    m_viewer->setBuffer(m_originalBuffer, tr("Original"), false);
    
    // Deferred initial mask update (after dialog is fully constructed and shown)
    QTimer::singleShot(50, this, &WavescaleHDRDialog::updateQuickMask);
    
    startPreview();
}

void WavescaleHDRDialog::setViewer(ImageViewer* v) {
    if (m_targetViewer == v) return;
    m_targetViewer = v;
    
    if (m_targetViewer && m_targetViewer->getBuffer().isValid()) {
        m_originalBuffer = m_targetViewer->getBuffer();
        
        // Regenerate cache for mask
        int w = m_originalBuffer.width();
        int h = m_originalBuffer.height();
        float scale = std::min(1.0f, 400.0f / static_cast<float>(std::max(w, h)));
        m_cacheW = std::max(1, static_cast<int>(w * scale));
        m_cacheH = std::max(1, static_cast<int>(h * scale));
        
        m_L_channel_cache.resize(m_cacheW * m_cacheH);
        int ch = m_originalBuffer.channels();
        const float* d = m_originalBuffer.data().data();
        
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static)
        #endif
        for (int y = 0; y < m_cacheH; ++y) {
            for (int x = 0; x < m_cacheW; ++x) {
                int sx = static_cast<int>(x / scale);
                int sy = static_cast<int>(y / scale);
                sx = std::min(sx, w - 1);
                sy = std::min(sy, h - 1);
                size_t idx = (static_cast<size_t>(sy) * w + sx) * ch;
                float r = d[idx + 0];
                float g = (ch > 1) ? d[idx + 1] : r;
                float b = (ch > 2) ? d[idx + 2] : r;
                m_L_channel_cache[y * m_cacheW + x] = (0.2126f * r + 0.7152f * g + 0.0722f * b) * 100.0f;
            }
        }
        
        updateQuickMask();
        startPreview();
    }
}

WavescaleHDRDialog::~WavescaleHDRDialog() {
    if (m_worker->isRunning()) {
        m_worker->terminate();
        m_worker->wait();
    }
}

void WavescaleHDRDialog::createUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QHBoxLayout* contentLayout = new QHBoxLayout();
    
    // Left: ImageViewer
    m_viewer = new ImageViewer(this);
    m_viewer->setMinimumWidth(600);
    contentLayout->addWidget(m_viewer, 1);
    
    // Right: Controls
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(10, 0, 0, 0);
    QGroupBox* grp = new QGroupBox(tr("HDR Controls"), this);
    QFormLayout* form = new QFormLayout(grp);
    
    m_scalesSlider = new QSlider(Qt::Horizontal);
    m_scalesSlider->setRange(2, 10);
    m_scalesSlider->setValue(5);
    m_scalesLabel = new QLabel("5");
    connect(m_scalesSlider, &QSlider::valueChanged, [=](int v){ m_scalesLabel->setText(QString::number(v)); });
    
    m_compSlider = new QSlider(Qt::Horizontal);
    m_compSlider->setRange(10, 500);
    m_compSlider->setValue(150);
    m_compLabel = new QLabel("1.50");
    connect(m_compSlider, &QSlider::valueChanged, [=](int v){ m_compLabel->setText(QString::number(v/100.0, 'f', 2)); });

    m_gammaSlider = new QSlider(Qt::Horizontal);
    m_gammaSlider->setRange(10, 1000);
    m_gammaSlider->setValue(500);
    m_gammaLabel = new QLabel("5.00");
    connect(m_gammaSlider, &QSlider::valueChanged, this, [this](int v){
        updateQuickMask();
    });

    form->addRow(tr("Scales:"), m_scalesSlider);
    form->addRow("", m_scalesLabel);
    form->addRow(tr("Compression:"), m_compSlider);
    form->addRow("", m_compLabel);
    form->addRow(tr("Mask Gamma:"), m_gammaSlider);
    form->addRow("", m_gammaLabel);

    m_dimmingSlider = new QSlider(Qt::Horizontal);
    m_dimmingSlider->setRange(50, 300); // 0.5 to 3.0
    m_dimmingSlider->setValue(100); // Default 1.0
    m_dimmingLabel = new QLabel("1.00");
    connect(m_dimmingSlider, &QSlider::valueChanged, [=](int v){ m_dimmingLabel->setText(QString::number(v/100.0, 'f', 2)); });
    
    form->addRow(tr("Dimming Gamma:"), m_dimmingSlider);
    form->addRow("", m_dimmingLabel);
    
    rightLayout->addWidget(grp);
    
    // Mask Preview
    QGroupBox* maskGrp = new QGroupBox(tr("Mask Preview"), this);
    QVBoxLayout* maskLay = new QVBoxLayout(maskGrp);
    m_maskLabel = new QLabel(this);
    m_maskLabel->setFixedSize(200, 200);
    m_maskLabel->setScaledContents(false); // Fix: Prevent squashing
    m_maskLabel->setAlignment(Qt::AlignCenter); 
    maskLay->addWidget(m_maskLabel);
    rightLayout->addWidget(maskGrp);
    
    rightLayout->addStretch();
    
    m_showOriginalCheck = new QCheckBox(tr("Compare (Show Original)"), this);
    connect(m_showOriginalCheck, &QCheckBox::toggled, this, &WavescaleHDRDialog::toggleOriginal);
    rightLayout->addWidget(m_showOriginalCheck);
    
    m_previewBtn = new QPushButton(tr("Update Preview"), this);
    connect(m_previewBtn, &QPushButton::clicked, this, &WavescaleHDRDialog::onPreviewClicked);
    rightLayout->addWidget(m_previewBtn);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    rightLayout->addWidget(m_progressBar);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_applyBtn = new QPushButton(tr("Apply"), this);
    connect(m_applyBtn, &QPushButton::clicked, this, &WavescaleHDRDialog::onApplyClicked);
    
    m_closeBtn = new QPushButton(tr("Close"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addWidget(m_applyBtn);
    btnLayout->addWidget(m_closeBtn);
    rightLayout->addLayout(btnLayout);
    
    contentLayout->addLayout(rightLayout);
    mainLayout->addLayout(contentLayout);
}

void WavescaleHDRDialog::startPreview() {
    if (m_worker->isRunning()) return;
    
    m_previewBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);
    m_progressBar->setValue(0);
    
    int scales = m_scalesSlider->value();
    float comp = m_compSlider->value() / 100.0f;
    float gamma = m_gammaSlider->value() / 100.0f;
    float dimGamma = m_dimmingSlider->value() / 100.0f;
    
    m_worker->setup(m_originalBuffer, scales, comp, gamma, dimGamma);
    m_worker->start();
}

void WavescaleHDRDialog::updateQuickMask() {
    if (m_L_channel_cache.empty() || m_cacheW <= 0 || m_cacheH <= 0) {
        return;
    }
    
    float gamma = m_gammaSlider->value() / 100.0f;
    m_gammaLabel->setText(QString::number(gamma, 'f', 2));
    
    // Create QImage directly from cache data
    QImage img(m_cacheW, m_cacheH, QImage::Format_Grayscale8);
    
    for (int y = 0; y < m_cacheH; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < m_cacheW; ++x) {
            float raw = m_L_channel_cache[y * m_cacheW + x] / 100.0f;
            float val = std::clamp(raw, 0.0f, 1.0f);
            
            // Apply Gamma
            if (std::abs(gamma - 1.0f) > 0.01f) {
                val = std::pow(val, gamma);
            }
            
            line[x] = static_cast<uchar>(val * 255.0f);
        }
    }
    
    // Update the mask label directly
    QPixmap pix = QPixmap::fromImage(img);
    m_maskLabel->setPixmap(pix.scaled(m_maskLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_maskLabel->update();
}

void WavescaleHDRDialog::onPreviewClicked() {
    startPreview();
}

void WavescaleHDRDialog::onApplyClicked() {
    if (m_targetViewer) {
        emit applyInternal(m_previewBuffer);
        accept();
    }
}

void WavescaleHDRDialog::onWorkerFinished(ImageBuffer result, ImageBuffer mask) {
    m_previewBuffer = result;
    m_maskBuffer = mask;
    
    m_previewBtn->setEnabled(true);
    m_applyBtn->setEnabled(true);
    m_progressBar->setValue(100);
    
    if (!m_showOriginalCheck->isChecked()) {
        // After first process, we want to preserve view if user zooms in
        m_viewer->setBuffer(m_previewBuffer, tr("Preview"), true); 
    }
    
    // NOTE: We do NOT update the mask preview here anymore.
    // The Quick Mask (updateQuickMask) handles the mask preview in real-time
    // and is much faster/responsive. The worker's mask is just a side-effect.
}

void WavescaleHDRDialog::toggleOriginal(bool showOriginal) {
    if (showOriginal) {
        m_viewer->setBuffer(m_originalBuffer, tr("Original"), true);
    } else {
        m_viewer->setBuffer(m_previewBuffer, tr("Preview"), true);
    }
}

void WavescaleHDRDialog::updateMaskPreview(const ImageBuffer& mask) {
    if (!mask.isValid()) {
        // Fallback: show a solid gray placeholder
        QPixmap placeholder(m_maskLabel->size());
        placeholder.fill(Qt::gray);
        m_maskLabel->setPixmap(placeholder);
        m_maskLabel->repaint();
        return;
    }
    
    // Get display image
    QImage img = mask.getDisplayImage(ImageBuffer::Display_Linear, false, nullptr, 0, 0);
    
    if (img.isNull()) {
        return;
    }
    
    // Ensure the pixmap is correctly scaled to the label and updated
    QPixmap pix = QPixmap::fromImage(img);
    if (!pix.isNull()) {
        m_maskLabel->setPixmap(pix.scaled(m_maskLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_maskLabel->repaint(); // Force immediate repaint
    }
}

void WavescaleHDRDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // Ensure the image fits the space on first show
    if (m_isFirstShow && m_viewer) {
        m_viewer->fitToWindow();
        m_isFirstShow = false;
    }
}
