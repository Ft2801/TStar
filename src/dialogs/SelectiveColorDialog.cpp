#include "SelectiveColorDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QMessageBox>
#include <QTimer>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>

SelectiveColorDialog::SelectiveColorDialog(QWidget* parent)
    : DialogBase(parent, tr("Selective Color Correction"), 950, 700), m_updatingPreset(false)
{
    
    // Define hue presets (degrees 0-360)
    m_presets = {
        {"Custom", 0, 360},
        {"Red", 340, 20},       // Wraps around
        {"Orange", 15, 45},
        {"Yellow", 45, 70},
        {"Green", 70, 170},
        {"Cyan", 170, 200},
        {"Blue", 200, 270},
        {"Magenta", 270, 340}
    };
    
    // Grab current image
    if (MainWindowCallbacks* mw = getCallbacks()) {
        if (mw->getCurrentViewer() && mw->getCurrentViewer()->getBuffer().isValid()) {
            m_sourceImage = mw->getCurrentViewer()->getBuffer();
        }
    }
    
    setupUi();
    onReset();

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

SelectiveColorDialog::~SelectiveColorDialog() {}

void SelectiveColorDialog::setSource(const ImageBuffer& img) {
    m_sourceImage = img;
    updateMask();
}

void SelectiveColorDialog::setupUi() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    // Left: Controls
    QVBoxLayout* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(8);
    
    // Mask Group
    QGroupBox* maskGroup = new QGroupBox(tr("Hue Selection"));
    QGridLayout* maskLayout = new QGridLayout(maskGroup);
    
    // Preset
    maskLayout->addWidget(new QLabel(tr("Preset:")), 0, 0);
    m_presetCombo = new QComboBox();
    for (const auto& p : m_presets) {
        m_presetCombo->addItem(tr(p.name.toUtf8().constData())); 
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SelectiveColorDialog::onPresetChanged);
    maskLayout->addWidget(m_presetCombo, 0, 1, 1, 3);
    
    // Hue Start/End
    maskLayout->addWidget(new QLabel(tr("Hue Start (°):")), 1, 0);
    m_hueStartSpin = new QDoubleSpinBox();
    m_hueStartSpin->setRange(0, 360);
    m_hueStartSpin->setDecimals(0);
    m_hueStartSpin->setValue(0);
    connect(m_hueStartSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_hueStartSpin, 1, 1);
    
    maskLayout->addWidget(new QLabel(tr("Hue End (°):")), 1, 2);
    m_hueEndSpin = new QDoubleSpinBox();
    m_hueEndSpin->setRange(0, 360);
    m_hueEndSpin->setDecimals(0);
    m_hueEndSpin->setValue(360);
    connect(m_hueEndSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_hueEndSpin, 1, 3);
    
    // Smoothness & Min Chroma
    maskLayout->addWidget(new QLabel(tr("Smoothness (°):")), 2, 0);
    m_smoothnessSpin = new QDoubleSpinBox();
    m_smoothnessSpin->setRange(0, 60);
    m_smoothnessSpin->setValue(10);
    connect(m_smoothnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_smoothnessSpin, 2, 1);
    
    maskLayout->addWidget(new QLabel(tr("Min Chroma:")), 2, 2);
    m_minChromaSpin = new QDoubleSpinBox();
    m_minChromaSpin->setRange(0, 1);
    m_minChromaSpin->setSingleStep(0.05);
    m_minChromaSpin->setValue(0.05);
    connect(m_minChromaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_minChromaSpin, 2, 3);
    
    // Intensity & Invert
    maskLayout->addWidget(new QLabel(tr("Intensity:")), 3, 0);
    m_intensitySpin = new QDoubleSpinBox();
    m_intensitySpin->setRange(0, 2);
    m_intensitySpin->setSingleStep(0.1);
    m_intensitySpin->setValue(1.0);
    connect(m_intensitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SelectiveColorDialog::updatePreview);
    maskLayout->addWidget(m_intensitySpin, 3, 1);
    
    m_invertCheck = new QCheckBox(tr("Invert"));
    connect(m_invertCheck, &QCheckBox::toggled, this, &SelectiveColorDialog::updateMask);
    maskLayout->addWidget(m_invertCheck, 3, 2);
    
    m_showMaskCheck = new QCheckBox(tr("Show Mask"));
    connect(m_showMaskCheck, &QCheckBox::toggled, this, &SelectiveColorDialog::updatePreview);
    maskLayout->addWidget(m_showMaskCheck, 3, 3);
    
    leftLayout->addWidget(maskGroup);
    
    // CMY Group
    QGroupBox* cmyGroup = new QGroupBox(tr("CMY Adjustments"));
    QFormLayout* cmyLayout = new QFormLayout(cmyGroup);
    
    auto createSlider = [](int min, int max, int val) {
        QSlider* s = new QSlider(Qt::Horizontal);
        s->setRange(min, max);
        s->setValue(val);
        return s;
    };
    
    m_cyanSlider = createSlider(-100, 100, 0);
    connect(m_cyanSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    cmyLayout->addRow(tr("Cyan:"), m_cyanSlider);
    
    m_magentaSlider = createSlider(-100, 100, 0);
    connect(m_magentaSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    cmyLayout->addRow(tr("Magenta:"), m_magentaSlider);
    
    m_yellowSlider = createSlider(-100, 100, 0);
    connect(m_yellowSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    cmyLayout->addRow(tr("Yellow:"), m_yellowSlider);
    
    leftLayout->addWidget(cmyGroup);
    
    // RGB Group
    QGroupBox* rgbGroup = new QGroupBox(tr("RGB Adjustments"));
    QFormLayout* rgbLayout = new QFormLayout(rgbGroup);
    
    m_redSlider = createSlider(-100, 100, 0);
    connect(m_redSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    rgbLayout->addRow(tr("Red:"), m_redSlider);
    
    m_greenSlider = createSlider(-100, 100, 0);
    connect(m_greenSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    rgbLayout->addRow(tr("Green:"), m_greenSlider);
    
    m_blueSlider = createSlider(-100, 100, 0);
    connect(m_blueSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    rgbLayout->addRow(tr("Blue:"), m_blueSlider);
    
    leftLayout->addWidget(rgbGroup);
    
    // LSC Group
    QGroupBox* lscGroup = new QGroupBox(tr("Luminance / Saturation / Contrast"));
    QFormLayout* lscLayout = new QFormLayout(lscGroup);
    
    m_luminanceSlider = createSlider(-100, 100, 0);
    connect(m_luminanceSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    lscLayout->addRow(tr("Luminance:"), m_luminanceSlider);
    
    m_saturationSlider = createSlider(-100, 100, 0);
    connect(m_saturationSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    lscLayout->addRow(tr("Saturation:"), m_saturationSlider);
    
    m_contrastSlider = createSlider(-100, 100, 0);
    connect(m_contrastSlider, &QSlider::valueChanged, this, &SelectiveColorDialog::updatePreview);
    lscLayout->addRow(tr("Contrast:"), m_contrastSlider);
    
    leftLayout->addWidget(lscGroup);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* resetBtn = new QPushButton(tr("Reset"));
    connect(resetBtn, &QPushButton::clicked, this, &SelectiveColorDialog::onReset);
    btnLayout->addWidget(resetBtn);
    
    btnLayout->addStretch();
    
    QPushButton* applyBtn = new QPushButton(tr("Apply"));
    connect(applyBtn, &QPushButton::clicked, this, &SelectiveColorDialog::onApply);
    btnLayout->addWidget(applyBtn);
    
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);
    
    leftLayout->addLayout(btnLayout);
    
    mainLayout->addLayout(leftLayout);
    
    // Right: Preview
    QVBoxLayout* rightLayout = new QVBoxLayout();
    
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setMinimumSize(400, 400);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
    
    rightLayout->addWidget(m_view);
    mainLayout->addLayout(rightLayout, 1);
}

void SelectiveColorDialog::onPresetChanged(int index) {
    if (index < 0 || index >= (int)m_presets.size()) return;
    
    m_updatingPreset = true;
    const auto& p = m_presets[index];
    m_hueStartSpin->setValue(p.hueStart);
    m_hueEndSpin->setValue(p.hueEnd);
    m_updatingPreset = false;
    
    updateMask();
}

std::vector<float> SelectiveColorDialog::computeHueMask(const ImageBuffer& src) {
    if (!src.isValid() || src.channels() < 3) {
        return std::vector<float>(src.width() * src.height(), 1.0f);
    }
    
    int w = src.width();
    int h = src.height();
    int c = src.channels();
    
    float hueStart = (float)m_hueStartSpin->value();
    float hueEnd = (float)m_hueEndSpin->value();
    float smooth = (float)m_smoothnessSpin->value();
    float minChroma = (float)m_minChromaSpin->value();
    bool invert = m_invertCheck->isChecked();
    
    // Convert to OpenCV for HSV
    cv::Mat rgb(h, w, CV_8UC3);
    const float* srcData = src.data().data();
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * c;
            rgb.at<cv::Vec3b>(y, x)[2] = (uint8_t)(std::clamp(srcData[idx], 0.0f, 1.0f) * 255);     // R -> B in BGR
            rgb.at<cv::Vec3b>(y, x)[1] = (uint8_t)(std::clamp(srcData[idx+1], 0.0f, 1.0f) * 255); // G
            rgb.at<cv::Vec3b>(y, x)[0] = (uint8_t)(std::clamp(srcData[idx+2], 0.0f, 1.0f) * 255); // B -> R in BGR
        }
    }
    
    cv::Mat hsv;
    cv::cvtColor(rgb, hsv, cv::COLOR_BGR2HSV);
    
    std::vector<float> mask(w * h, 0.0f);
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            cv::Vec3b pixel = hsv.at<cv::Vec3b>(y, x);
            float H = pixel[0] * 2.0f; // OpenCV H is 0-180, convert to 0-360
            float S = pixel[1] / 255.0f;
            float V = pixel[2] / 255.0f;
            
            // Chroma gate
            float chroma = S * V;
            if (chroma < minChroma) {
                mask[y * w + x] = invert ? 1.0f : 0.0f;
                continue;
            }
            
            // Hue band calculation with wrap-around
            float bandWeight = 0.0f;
            
            // Calculate forward arc length
            float L = std::fmod(hueEnd - hueStart + 360.0f, 360.0f);
            if (L <= 0.0f) L = 360.0f;
            
            // Forward distance from start
            float fwd = std::fmod(H - hueStart + 360.0f, 360.0f);
            
            if (fwd <= L) {
                bandWeight = 1.0f;
            } else if (smooth > 0) {
                // Feather after end
                if (fwd < L + smooth) {
                    bandWeight = 1.0f - (fwd - L) / smooth;
                }
                // Feather before start
                float bwd = std::fmod(hueStart - H + 360.0f, 360.0f);
                if (bwd < smooth) {
                    bandWeight = std::max(bandWeight, 1.0f - bwd / smooth);
                }
            }
            
            bandWeight = std::clamp(bandWeight, 0.0f, 1.0f);
            
            if (invert) bandWeight = 1.0f - bandWeight;
            
            mask[y * w + x] = bandWeight;
        }
    }
    
    return mask;
}

void SelectiveColorDialog::updateMask() {
    if (!m_sourceImage.isValid()) return;
    
    m_mask = computeHueMask(m_sourceImage);
    updatePreview();
}

ImageBuffer SelectiveColorDialog::applyAdjustments(const ImageBuffer& src, const std::vector<float>& mask) {
    if (!src.isValid()) return src;
    
    int w = src.width();
    int h = src.height();
    int c = src.channels();
    
    float intensity = (float)m_intensitySpin->value();
    
    // Get slider values as -1 to +1 range
    float cyan = m_cyanSlider->value() / 100.0f;
    float magenta = m_magentaSlider->value() / 100.0f;
    float yellow = m_yellowSlider->value() / 100.0f;
    float red = m_redSlider->value() / 100.0f;
    float green = m_greenSlider->value() / 100.0f;
    float blue = m_blueSlider->value() / 100.0f;
    float lum = m_luminanceSlider->value() / 100.0f;
    float sat = m_saturationSlider->value() / 100.0f;
    float con = m_contrastSlider->value() / 100.0f;
    
    ImageBuffer result = src;
    float* dstData = result.data().data();
    const float* srcData = src.data().data();
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * c;
            float m = std::clamp(mask[y * w + x] * intensity, 0.0f, 1.0f);
            
            if (m < 0.001f) continue; // Skip unmasked pixels
            
            float R = srcData[idx];
            float G = srcData[idx + 1];
            float B = srcData[idx + 2];
            
            // CMY adjustments (reduce complementary)
            R = std::clamp(R - cyan * m, 0.0f, 1.0f);
            G = std::clamp(G - magenta * m, 0.0f, 1.0f);
            B = std::clamp(B - yellow * m, 0.0f, 1.0f);
            
            // RGB boosts
            R = std::clamp(R + red * m, 0.0f, 1.0f);
            G = std::clamp(G + green * m, 0.0f, 1.0f);
            B = std::clamp(B + blue * m, 0.0f, 1.0f);
            
            // Luminance
            if (std::abs(lum) > 0.001f) {
                R = std::clamp(R + lum * m, 0.0f, 1.0f);
                G = std::clamp(G + lum * m, 0.0f, 1.0f);
                B = std::clamp(B + lum * m, 0.0f, 1.0f);
            }
            
            // Contrast
            if (std::abs(con) > 0.001f) {
                float factor = 1.0f + con * m;
                R = std::clamp((R - 0.5f) * factor + 0.5f, 0.0f, 1.0f);
                G = std::clamp((G - 0.5f) * factor + 0.5f, 0.0f, 1.0f);
                B = std::clamp((B - 0.5f) * factor + 0.5f, 0.0f, 1.0f);
            }
            
            // Saturation (simple approach: adjust distance from gray)
            if (std::abs(sat) > 0.001f) {
                float gray = 0.299f * R + 0.587f * G + 0.114f * B;
                float factor = 1.0f + sat * m;
                R = std::clamp(gray + (R - gray) * factor, 0.0f, 1.0f);
                G = std::clamp(gray + (G - gray) * factor, 0.0f, 1.0f);
                B = std::clamp(gray + (B - gray) * factor, 0.0f, 1.0f);
            }
            
            dstData[idx] = R;
            dstData[idx + 1] = G;
            dstData[idx + 2] = B;
        }
    }
    
    return result;
}

void SelectiveColorDialog::updatePreview() {
    if (!m_sourceImage.isValid()) return;
    
    bool showMask = m_showMaskCheck->isChecked();
    
    if (showMask) {
        // Show mask as grayscale overlay
        int w = m_sourceImage.width();
        int h = m_sourceImage.height();
        
        QImage qimg(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float m = m_mask[y * w + x];
                uint8_t v = (uint8_t)(m * 255);
                qimg.setPixel(x, y, qRgb(v, v, v));
            }
        }
        
        m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    } else {
        // Show adjusted image
        m_previewImage = applyAdjustments(m_sourceImage, m_mask);
        QImage qimg = m_previewImage.getDisplayImage();
        m_pixmapItem->setPixmap(QPixmap::fromImage(qimg));
    }
    
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
}

void SelectiveColorDialog::onReset() {
    m_presetCombo->setCurrentIndex(1); // Red
    
    m_cyanSlider->setValue(0);
    m_magentaSlider->setValue(0);
    m_yellowSlider->setValue(0);
    m_redSlider->setValue(0);
    m_greenSlider->setValue(0);
    m_blueSlider->setValue(0);
    m_luminanceSlider->setValue(0);
    m_saturationSlider->setValue(0);
    m_contrastSlider->setValue(0);
    
    m_intensitySpin->setValue(1.0);
    m_smoothnessSpin->setValue(10.0);
    m_minChromaSpin->setValue(0.05);
    m_invertCheck->setChecked(false);
    m_showMaskCheck->setChecked(false);
    
    updateMask();
}

void SelectiveColorDialog::onApply() {
    MainWindowCallbacks* mw = getCallbacks();
    if (!mw) return;
    
    ImageViewer* v = mw->getCurrentViewer();
    if (!v) return;
    
    v->pushUndo();
    
    ImageBuffer& buffer = v->getBuffer();
    
    // Save original for mask blending
    ImageBuffer original;
    if (buffer.hasMask()) {
        original = buffer;
    }
    
    // Recompute mask based on current buffer (not cached source)
    std::vector<float> currentMask = computeHueMask(buffer);
    ImageBuffer result = applyAdjustments(buffer, currentMask);
    v->setBuffer(result, buffer.name());
    
    // Blend with mask if present
    ImageBuffer& newBuffer = v->getBuffer();
    if (original.isValid() && newBuffer.hasMask()) {
        newBuffer.blendResult(original);
    }
    
    accept();
}
