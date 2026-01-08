#include "MaskGenerationDialog.h"
#include "MaskCanvas.h"
#include "LivePreviewDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QtMath>
#include <QtConcurrent/QtConcurrent>
#include <opencv2/opencv.hpp>
#include <QMessageBox>
#include <QUuid>

MaskGenerationDialog::MaskGenerationDialog(const ImageBuffer& image, QWidget* parent) 
    : DialogBase(parent, "Mask Generation", 1024, 800), m_sourceImage(image) 
{
    setWindowFlag(Qt::Window); // Allow minimize/maximize
    
    m_livePreview = nullptr; // Create only when Preview Mask is clicked
    
    setupUI();

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

void MaskGenerationDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // --- 1. Mode Toolbar ---
    QHBoxLayout* modeBar = new QHBoxLayout();
    m_freeBtn = new QPushButton(tr("Freehand"));
    m_freeBtn->setCheckable(true);
    m_freeBtn->setChecked(true);
    
    m_ellipseBtn = new QPushButton(tr("Ellipse"));
    m_ellipseBtn->setCheckable(true);
    
    m_selectBtn = new QPushButton(tr("Select Entire Image"));
    m_selectBtn->setCheckable(true);
    
    m_moveBtn = new QPushButton(tr("Move/Select"));
    m_moveBtn->setCheckable(true);
    
    QButtonGroup* group = new QButtonGroup(this);
    group->addButton(m_freeBtn);
    group->addButton(m_ellipseBtn);
    group->addButton(m_selectBtn);
    group->addButton(m_moveBtn);
    group->setExclusive(true);
    
    QString btnStyle = "QPushButton { padding:6px; border:1px solid #888; border-radius:4px; background:transparent; color: #e0e0e0; } "
                       "QPushButton:checked { background-color:#0078d4; color:white; border-color:#005a9e; }";
    
    m_freeBtn->setStyleSheet(btnStyle);
    m_ellipseBtn->setStyleSheet(btnStyle);
    m_selectBtn->setStyleSheet(btnStyle);
    m_moveBtn->setStyleSheet(btnStyle);
    
    connect(m_freeBtn, &QPushButton::clicked, [this](){ setMode("polygon"); });
    connect(m_ellipseBtn, &QPushButton::clicked, [this](){ setMode("ellipse"); });
    connect(m_selectBtn, &QPushButton::clicked, [this](){ setMode("select"); });
    connect(m_moveBtn, &QPushButton::clicked, [this](){ setMode("move"); });
    
    modeBar->addWidget(m_freeBtn);
    modeBar->addWidget(m_ellipseBtn);
    modeBar->addWidget(m_selectBtn);
    modeBar->addWidget(m_moveBtn);
    modeBar->addStretch();
    
    // Zoom Controls
    QPushButton* zOut = new QPushButton(tr("Zoom Out"));
    QPushButton* zIn = new QPushButton(tr("Zoom In"));
    QPushButton* zFit = new QPushButton(tr("Fit"));
    
    connect(zOut, &QPushButton::clicked, [this](){ m_canvas->zoomOut(); });
    connect(zIn, &QPushButton::clicked, [this](){ m_canvas->zoomIn(); });
    connect(zFit, &QPushButton::clicked, [this](){ m_canvas->fitToView(); });
    
    modeBar->addWidget(zOut);
    modeBar->addWidget(zIn);
    modeBar->addWidget(zFit);
    
    mainLayout->addLayout(modeBar);
    
    // --- 2. Canvas ---
    // Generate preview image for background
    QImage bg = m_sourceImage.getDisplayImage(ImageBuffer::Display_AutoStretch, true); // AutoStretch by default for visibility
    m_canvas = new MaskCanvas(bg, this);
    connect(m_canvas, &MaskCanvas::maskContentChanged, this, &MaskGenerationDialog::updateLivePreview);
    mainLayout->addWidget(m_canvas, 1);
    
    // --- 3. Controls ---
    QHBoxLayout* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(tr("Mask Type:")));
    m_typeCombo = new QComboBox();
    m_typeCombo = new QComboBox();
    m_typeCombo->addItem(tr("Binary"), "Binary");
    m_typeCombo->addItem(tr("Range Selection"), "Range Selection");
    m_typeCombo->addItem(tr("Lightness"), "Lightness");
    m_typeCombo->addItem(tr("Chrominance"), "Chrominance");
    m_typeCombo->addItem(tr("Star Mask"), "Star Mask");
    m_typeCombo->addItem(tr("Color: Red"), "Color: Red");
    m_typeCombo->addItem(tr("Color: Orange"), "Color: Orange");
    m_typeCombo->addItem(tr("Color: Yellow"), "Color: Yellow");
    m_typeCombo->addItem(tr("Color: Green"), "Color: Green");
    m_typeCombo->addItem(tr("Color: Cyan"), "Color: Cyan");
    m_typeCombo->addItem(tr("Color: Blue"), "Color: Blue");
    m_typeCombo->addItem(tr("Color: Magenta"), "Color: Magenta");
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, &MaskGenerationDialog::onTypeChanged);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, &MaskGenerationDialog::updateLivePreview);
    
    // Add logic to update background when stretch mode changes (using the preview stretch combo which acts as visualization control)
    // Actually, m_previewStretchCombo is for "Preview Visualization" of the MASK. 
    // BUT the user complains the background image is always AutoStretch'ed. 
    // We should probably add a dedicated control for the BACKGROUND image display mode or reuse the same if it makes sense.
    // Given the user request, let's reuse m_previewStretchCombo to ALSO update the background canvas if "Preview Visualization" usually implies seeing the image context.
    // However, usually "Preview Visualization" controls how the MASK comes out in the LivePreviewDialog.
    // Let's add a connection to update the canvas background too.
    controls->addWidget(m_typeCombo);
    
    controls->addWidget(new QLabel(tr("Edge Blur (px):")));
    m_blurSlider = new QSlider(Qt::Horizontal);
    m_blurSlider->setRange(0, 300);
    m_blurLabel = new QLabel("0");
    // Update label during drag, but update preview only on release (blur is expensive)
    // Update label and preview during drag (now fast due to downsampling)
    connect(m_blurSlider, &QSlider::valueChanged, [this](int v){ 
        m_blurLabel->setText(QString::number(v)); 
        updateLivePreview();
    });
    controls->addWidget(m_blurSlider);
    controls->addWidget(m_blurLabel);
    
    mainLayout->addLayout(controls);
    
    // --- 4. Range Selection Group ---
    m_rangeGroup = new QGroupBox(tr("Range Selection"));
    QGridLayout* rangeGrid = new QGridLayout(m_rangeGroup);
    
    auto addSlider = [&](int row, const QString& name, int max, QLabel*& lblInfo) -> QSlider* {
        rangeGrid->addWidget(new QLabel(name + ":"), row, 0);
        QSlider* s = new QSlider(Qt::Horizontal);
        s->setRange(0, max);
        lblInfo = new QLabel("0.00");
        rangeGrid->addWidget(s, row, 1);
        rangeGrid->addWidget(lblInfo, row, 2);
        connect(s, &QSlider::valueChanged, [this, max, lblInfo](int v){
            lblInfo->setText(QString::number((double)v/max, 'f', 2));
            updateLivePreview();
        });
        return s;
    };
    
    m_lowerSl = addSlider(0, tr("Lower Limit"), 100, m_lowerLbl);
    m_upperSl = addSlider(1, tr("Upper Limit"), 100, m_upperLbl);
    m_fuzzSl = addSlider(2, tr("Fuzziness"), 100, m_fuzzLbl);
    
    // Smoothness (Sigma)
    rangeGrid->addWidget(new QLabel(tr("Internal Blur:")), 3, 0);
    m_smoothSl = new QSlider(Qt::Horizontal);
    m_smoothSl->setRange(0, 200);
    m_smoothSl->setValue(3);
    m_smoothLbl = new QLabel("3 px");
    rangeGrid->addWidget(m_smoothSl, 3, 1);
    rangeGrid->addWidget(m_smoothLbl, 3, 2);
    connect(m_smoothSl, &QSlider::valueChanged, [this](int v){
        m_smoothLbl->setText(QString("%1 px").arg(v));
        updateLivePreview();
    });
    
    m_linkCb = new QCheckBox(tr("Link Limits"));
    rangeGrid->addWidget(m_linkCb, 0, 3);
    connect(m_linkCb, &QCheckBox::toggled, [this](bool c){ 
        if(c) m_upperSl->setValue(m_lowerSl->value()); 
    });
    connect(m_lowerSl, &QSlider::valueChanged, [this](int v){
        if(m_linkCb->isChecked()) m_upperSl->setValue(v);
    });
    
    m_screenCb = new QCheckBox(tr("Screening"));
    rangeGrid->addWidget(m_screenCb, 1, 3);
    connect(m_screenCb, &QCheckBox::toggled, this, &MaskGenerationDialog::updateLivePreview);
    
    m_lightCb = new QCheckBox(tr("Use Lightness"));
    m_lightCb->setChecked(true); 
    rangeGrid->addWidget(m_lightCb, 2, 3);
    connect(m_lightCb, &QCheckBox::toggled, this, &MaskGenerationDialog::updateLivePreview);
    
    m_invertCb = new QCheckBox(tr("Invert Range"));
    rangeGrid->addWidget(m_invertCb, 3, 3);
    connect(m_invertCb, &QCheckBox::toggled, this, &MaskGenerationDialog::updateLivePreview);
    
    m_rangeGroup->setVisible(false);
    mainLayout->addWidget(m_rangeGroup);
    
    // Defaults
    m_upperSl->setValue(100); // 1.0 default
    
    // --- 5. Preview Visualization ---
    QGroupBox* previewGroup = new QGroupBox(tr("Preview Visualization"));
    QHBoxLayout* previewLayout = new QHBoxLayout(previewGroup);
    
    previewLayout->addWidget(new QLabel(tr("Stretch:")));
    m_previewStretchCombo = new QComboBox();
    m_previewStretchCombo->addItem(tr("Linear"), ImageBuffer::Display_Linear);
    m_previewStretchCombo->addItem(tr("Auto Stretch"), ImageBuffer::Display_AutoStretch);
    m_previewStretchCombo->addItem(tr("Histogram"), ImageBuffer::Display_Histogram);
    m_previewStretchCombo->addItem(tr("ArcSinh"), ImageBuffer::Display_ArcSinh);
    m_previewStretchCombo->addItem(tr("Sqrt"), ImageBuffer::Display_Sqrt);
    m_previewStretchCombo->addItem(tr("Log"), ImageBuffer::Display_Log);
    m_previewStretchCombo->setCurrentIndex(1); // Default to AutoStretch
    // Generate downsampled data for preview
    // Max dimension ~ 256px for ultra fast preview (blur is O(R^2) or O(N), but smaller N is better)
    // 512px is good balance.
    m_maxPreviewDim = 1024; // Default per user request
    int w = m_sourceImage.width();
    int h = m_sourceImage.height();
    float scale = 1.0f;
    if (w > m_maxPreviewDim || h > m_maxPreviewDim) {
        scale = std::min((float)m_maxPreviewDim/w, (float)m_maxPreviewDim/h);
    }
    m_smallW = static_cast<int>(w * scale);
    m_smallH = static_cast<int>(h * scale);
    
    // Cache lightness
    std::vector<float> L = getComponentLightness();
    cv::Mat matL(h, w, CV_32FC1, L.data());
    cv::Mat smallMatL;
    cv::resize(matL, smallMatL, cv::Size(m_smallW, m_smallH), 0, 0, cv::INTER_AREA);
    
    m_smallLuma.resize(m_smallW * m_smallH);
    memcpy(m_smallLuma.data(), smallMatL.data, m_smallLuma.size() * sizeof(float));

    connect(m_previewStretchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](){
        // Update Live Preview (Mask)
        updateLivePreview();
        
        // ALSO Update Canvas Background (using full res logic)
        if (m_canvas) {
            ImageBuffer::DisplayMode mode = static_cast<ImageBuffer::DisplayMode>(m_previewStretchCombo->currentData().toInt());
            QImage bg = m_sourceImage.getDisplayImage(mode, true);
            m_canvas->setBackgroundImage(bg);
        }
    });
    previewLayout->addWidget(m_previewStretchCombo);

    previewLayout->addWidget(new QLabel(tr("Size:")));
    m_previewSizeCombo = new QComboBox();
    m_previewSizeCombo->addItem("1024px", 1024);
    m_previewSizeCombo->addItem("2048px", 2048);
    m_previewSizeCombo->addItem(tr("Full"), 0);
    m_previewSizeCombo->setCurrentIndex(0); // Default 1024
    
    connect(m_previewSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](){
        int dim = m_previewSizeCombo->currentData().toInt();
        m_maxPreviewDim = (dim == 0) ? 99999 : dim;
        
        // Regenerate cache
        int w = m_sourceImage.width();
        int h = m_sourceImage.height();
        float scale = 1.0f;
        if (w > m_maxPreviewDim || h > m_maxPreviewDim) {
            scale = std::min((float)m_maxPreviewDim/w, (float)m_maxPreviewDim/h);
        }
        m_smallW = static_cast<int>(w * scale);
        m_smallH = static_cast<int>(h * scale);
        
        std::vector<float> L = getComponentLightness();
        cv::Mat matL(h, w, CV_32FC1, L.data());
        cv::Mat smallMatL;
        cv::resize(matL, smallMatL, cv::Size(m_smallW, m_smallH), 0, 0, cv::INTER_AREA);
        m_smallLuma.resize(m_smallW * m_smallH);
        memcpy(m_smallLuma.data(), smallMatL.data, m_smallLuma.size() * sizeof(float));
        
        updateLivePreview();
    });
    previewLayout->addWidget(m_previewSizeCombo);
    
    m_previewInvertCb = new QCheckBox(tr("Invert"));
    connect(m_previewInvertCb, &QCheckBox::toggled, this, &MaskGenerationDialog::updateLivePreview);
    previewLayout->addWidget(m_previewInvertCb);
    
    m_previewFalseColorCb = new QCheckBox(tr("False Color"));
    connect(m_previewFalseColorCb, &QCheckBox::toggled, this, &MaskGenerationDialog::updateLivePreview);
    previewLayout->addWidget(m_previewFalseColorCb);
    
    mainLayout->addWidget(previewGroup);

    // --- 6. Buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* previewBtn = new QPushButton(tr("Preview Mask"));
    QPushButton* clearBtn = new QPushButton(tr("Clear Shapes"));
    connect(previewBtn, &QPushButton::clicked, this, &MaskGenerationDialog::generatePreview);
    connect(clearBtn, &QPushButton::clicked, this, &MaskGenerationDialog::clearShapes);
    btnLayout->addWidget(previewBtn);
    btnLayout->addWidget(clearBtn);
    
    mainLayout->addLayout(btnLayout);
    
    QDialogButtonBox* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(bbox);
}

void MaskGenerationDialog::setMode(const QString& mode) {
    if (m_canvas) m_canvas->setMode(mode);
}

void MaskGenerationDialog::clearShapes() {
    if (m_canvas) m_canvas->clearShapes();
}

void MaskGenerationDialog::onTypeChanged(const QString& type) {
    Q_UNUSED(type);
    QString id = m_typeCombo->currentData().toString();
    bool isRange = (id == "Range Selection");
    m_rangeGroup->setVisible(isRange);
}

MaskLayer MaskGenerationDialog::getGeneratedMask(int requestedW, int requestedH) const {
    QString t = m_typeCombo->currentData().toString();
    
    // Determine target size
    int targetW = (requestedW > 0) ? requestedW : m_sourceImage.width();
    int targetH = (requestedH > 0) ? requestedH : m_sourceImage.height();
    bool isPreview = (requestedW > 0);

    // Get base mask from canvas
    // If preview, use MaskCanvas scaled generation
    std::vector<float> base;
    if (isPreview) {
        base = m_canvas->createMask(targetW, targetH);
    } else {
        base = const_cast<MaskGenerationDialog*>(this)->generateBaseMask();
    }
    
    bool baseIsEmpty = std::all_of(base.begin(), base.end(), [](float v) { return v == 0.0f; });
    
    const QString ALL_IMAGE_TYPES[] = {
        "Range Selection", "Lightness", "Chrominance", "Star Mask",
        "Color: Red", "Color: Orange", "Color: Yellow", "Color: Green",
        "Color: Cyan", "Color: Blue", "Color: Magenta"
    };

    bool useAll = false;
    if (baseIsEmpty) {
        for (const QString& ait : ALL_IMAGE_TYPES) {
            if (t == ait || t.startsWith("Color:")) {
                useAll = true;
                break;
            }
        }
    }

    if (useAll) {
        base.assign(targetW * targetH, 1.0f);
    }
    
    std::vector<float> finalMask;
    
    if (t == "Binary") {
        finalMask = base;
    } else if (t == "Range Selection") {
        // Need Range Mask at target size
        std::vector<float> comp = getLightness(targetW, targetH);
        
        // Inline generateRangeMask logic adapted for resizing
        // Or reuse generateRangeMask but modifying it to take input buffer??
        // Let's copy logic here or refactor. Inline is safer given const constraints.
        
        float L = m_lowerSl->value() / 100.0f;
        float U = m_upperSl->value() / 100.0f;
        float fuzz = m_fuzzSl->value() / 100.0f;
        int smooth = m_smoothSl->value();
        bool screen = m_screenCb->isChecked();
        bool inv = m_invertCb->isChecked();
        
        // If preview, scale smooth (blur) significantly down?
        // Smooth is in pixels. If we scale down image 10x, blur should be 10x smaller.
        if (isPreview) {
            float scale = (float)targetW / m_sourceImage.width();
            smooth = std::max(0, (int)(smooth * scale));
        }

        finalMask.resize(base.size());
        
        #pragma omp parallel for
        for (size_t i=0; i<comp.size(); ++i) {
            float v = comp[i];
            float val = 0.0f;
            float lowerBound = L - fuzz;
            float upperBound = U + fuzz;
            
            if (v >= L && v <= U) val = 1.0f;
            else if (fuzz > 1e-6) {
                if (v >= lowerBound && v < L) val = (v - lowerBound) / fuzz;
                else if (v > U && v <= upperBound) val = (upperBound - v) / fuzz;
            }
            val = std::clamp(val, 0.0f, 1.0f);
            if (screen) val *= v;
            
            // Combine with base
            finalMask[i] = base[i] * val;
        }
        
        // Blur
        if (smooth > 0) {
            cv::Mat mat(targetH, targetW, CV_32FC1, finalMask.data());
            cv::GaussianBlur(mat, mat, cv::Size(0,0), smooth);
        }
        
        // Invert
        if (inv) {
            for(float& v : finalMask) v = 1.0f - v;
        }

    } else if (t == "Lightness") {
        std::vector<float> L = getLightness(targetW, targetH);
        finalMask.resize(base.size());
        for(size_t i=0; i<base.size(); ++i) finalMask[i] = (base[i] > 0) ? L[i] : 0.0f;
    } else if (t == "Chrominance") {
        std::vector<float> C = getChrominance(targetW, targetH);
        finalMask.resize(base.size());
        for(size_t i=0; i<base.size(); ++i) finalMask[i] = (base[i] > 0) ? C[i] : 0.0f;
    } else if (t == "Star Mask") {
        std::vector<float> S = getStarMask(targetW, targetH);
        finalMask.resize(base.size());
        for(size_t i=0; i<base.size(); ++i) finalMask[i] = (base[i] > 0) ? S[i] : 0.0f;
    } else if (t.startsWith("Color:")) {
        QString color = t.mid(7);
        std::vector<float> C = getColorMask(color, targetW, targetH);
        finalMask.resize(base.size());
        for(size_t i=0; i<base.size(); ++i) finalMask[i] = (base[i] > 0) ? C[i] : 0.0f;
    }
    
    // Apply Global blur
    int blur = m_blurSlider->value();
    if (isPreview) {
        float scale = (float)targetW / m_sourceImage.width();
        blur = std::max(0, (int)(blur * scale));
    }
    
    if (blur > 0 && finalMask.size() > 0) {
        cv::Mat m(targetH, targetW, CV_32FC1, finalMask.data());
        cv::GaussianBlur(m, m, cv::Size(0,0), blur);
    }
    
    MaskLayer layer;
    layer.data = finalMask;
    layer.width = targetW;
    layer.height = targetH;
    layer.name = m_typeCombo->currentText();
    layer.visible = true;
    layer.id = QUuid::createUuid().toString();
    layer.mode = "replace";
    layer.opacity = 1.0f;
    
    return layer;
}

std::vector<float> MaskGenerationDialog::generateBaseMask() {
    return m_canvas->createMask();
}

std::vector<float> MaskGenerationDialog::getComponentLightness() const {
    const std::vector<float>& d = m_sourceImage.data();
    std::vector<float> L(m_sourceImage.width() * m_sourceImage.height());
    int ch = m_sourceImage.channels();
    
    #pragma omp parallel for
    for (int i = 0; i < (int)L.size(); ++i) {
        if (ch == 1) L[i] = d[i];
        else {
            float r = d[i*3];
            float g = d[i*3+1];
            float b = d[i*3+2];
            L[i] = 0.2989f*r + 0.5870f*g + 0.1140f*b;
        }
    }
    return L;
}

std::vector<float> MaskGenerationDialog::getChrominance(int w, int h) const {
     // If full resolution
     if (w == m_sourceImage.width() && h == m_sourceImage.height()) {
         return getComponentChrominance();
     }
     
     // For preview, we should really compute on the resized image for speed
     // But getComponentChrominance computes on full.
     // Let's create a resized RGB image and compute on that.
     cv::Mat src(h, w, CV_32FC3);
     
     // Resize raw data
     cv::Mat fullSrc(m_sourceImage.height(), m_sourceImage.width(), CV_32FC3, const_cast<float*>(m_sourceImage.data().data()));
     cv::resize(fullSrc, src, cv::Size(w, h), 0, 0, cv::INTER_AREA);
     
     cv::Mat ycrcb;
     cv::cvtColor(src, ycrcb, cv::COLOR_RGB2YCrCb);
     
     std::vector<cv::Mat> chans;
     cv::split(ycrcb, chans);
     
     cv::Scalar meanCr = cv::mean(chans[1]);
     cv::Scalar meanCb = cv::mean(chans[2]);
     
     cv::Mat diffCr = chans[1] - meanCr;
     cv::Mat diffCb = chans[2] - meanCb;
     
     cv::Mat dist;
     cv::sqrt(diffCr.mul(diffCr) + diffCb.mul(diffCb), dist);
     
     cv::normalize(dist, dist, 0, 1, cv::NORM_MINMAX);
     
     std::vector<float> res(w*h);
     memcpy(res.data(), dist.data, res.size()*sizeof(float));
     return res;
}

std::vector<float> MaskGenerationDialog::getComponentChrominance() const {
     int w = m_sourceImage.width();
     int h = m_sourceImage.height();
     cv::Mat src(h, w, CV_32FC3, const_cast<float*>(m_sourceImage.data().data()));
     
     cv::Mat ycrcb;
     cv::cvtColor(src, ycrcb, cv::COLOR_RGB2YCrCb);
     
     std::vector<cv::Mat> chans;
     cv::split(ycrcb, chans);
     
     cv::Scalar meanCr = cv::mean(chans[1]);
     cv::Scalar meanCb = cv::mean(chans[2]);
     
     cv::Mat diffCr = chans[1] - meanCr;
     cv::Mat diffCb = chans[2] - meanCb;
     
     cv::Mat dist;
     cv::sqrt(diffCr.mul(diffCr) + diffCb.mul(diffCb), dist);
     
     cv::normalize(dist, dist, 0, 1, cv::NORM_MINMAX);
     
     std::vector<float> res(w*h);
     memcpy(res.data(), dist.data, res.size()*sizeof(float));
     return res;
}

std::vector<float> MaskGenerationDialog::getStarMask(int w, int h) const {
     // Get Star Mask at resolution
     // 1. Get Lightness at resolution
     std::vector<float> L = getLightness(w, h);
     
     // 2. Extract stars
     // If resolution is lower, we might need to adjust detection threshold?
     // Actually extractStars works on pixels.
     auto stars = ImageBuffer::extractStars(L, w, h, 3.0f, 5);
     
     // 3. Draw
     cv::Mat mask = cv::Mat::zeros(h, w, CV_32FC1);
     
     // Scale variable removed - not used

     for (const auto& s : stars) {
         int r = std::max(1, (int)(s.hfr * 2.0f)); 
         cv::circle(mask, cv::Point((int)s.x, (int)s.y), r, cv::Scalar(1.0), -1); 
     }
     
     if (mask.empty()) return std::vector<float>(w*h, 0.0f);

     cv::GaussianBlur(mask, mask, cv::Size(0,0), 2.0);
     
     std::vector<float> res(w*h);
     memcpy(res.data(), mask.data, res.size()*sizeof(float));
     return res;
}

std::vector<float> MaskGenerationDialog::getStarMask() const {
    return getStarMask(m_sourceImage.width(), m_sourceImage.height());
}

std::vector<float> MaskGenerationDialog::generateRangeMask([[maybe_unused]] const std::vector<float>& base) {

    std::vector<float> comp = getComponentLightness();
    
    float L = m_lowerSl->value() / 100.0f;
    float U = m_upperSl->value() / 100.0f;
    float fuzz = m_fuzzSl->value() / 100.0f;
    int smooth = m_smoothSl->value();
    bool screen = m_screenCb->isChecked();
    bool inv = m_invertCb->isChecked();
    
    int size = comp.size();
    std::vector<float> m(size, 0.0f);
    
    #pragma omp parallel for
    for (int i=0; i<size; ++i) {
        float v = comp[i];
        float val = 0.0f;
        
        // Hard limits logic with fuzziness
        float lowerBound = L - fuzz;
        float upperBound = U + fuzz;
        
        // Pure range
        if (v >= L && v <= U) val = 1.0f;
        else if (fuzz > 1e-6) {
            // Ramp up
            if (v >= lowerBound && v < L) {
                val = (v - lowerBound) / fuzz;
            }
            // Ramp down
            else if (v > U && v <= upperBound) {
                val = (upperBound - v) / fuzz;
            }
        }
        
        val = std::clamp(val, 0.0f, 1.0f);
        
        if (screen) val *= v;
        
        m[i] = val;
    }
    
    // Blur
    if (smooth > 0) {
        cv::Mat mat(m_sourceImage.height(), m_sourceImage.width(), CV_32FC1, m.data());
        cv::GaussianBlur(mat, mat, cv::Size(0,0), smooth);
    }
    
    // Invert
    if (inv) {
        for(float& v : m) v = 1.0f - v;
    }
    
    return m;
}

void MaskGenerationDialog::updateLivePreview() {
    if (!m_livePreview || !m_livePreview->isVisible()) return;
    
    // Use m_smallW, m_smallH for fast preview if available
    MaskLayer m;
    if (m_smallW > 0 && m_smallH > 0) {
        m = getGeneratedMask(m_smallW, m_smallH);
    } else {
        m = getGeneratedMask();
    }
    
    // Apply visualization settings
    ImageBuffer::DisplayMode mode = static_cast<ImageBuffer::DisplayMode>(m_previewStretchCombo->currentData().toInt());
    bool inverted = m_previewInvertCb->isChecked();
    bool falseColor = m_previewFalseColorCb->isChecked();
    
    m_livePreview->updateMask(m.data, m.width, m.height, mode, inverted, falseColor);
}

void MaskGenerationDialog::generatePreview() {
    // Create LivePreviewDialog if doesn't exist
    if (!m_livePreview) {
        m_livePreview = new LivePreviewDialog(m_sourceImage.width(), m_sourceImage.height(), this);
        // Do NOT nullify on finished, just hide if needed, or keep logic as is but handle close.
        connect(m_livePreview, &QDialog::finished, [this]() {
             // Optional: Handle cleanup or state
        });
    }
    
    // Generate and update mask
    MaskLayer m = getGeneratedMask();
    if (m.data.empty() || m.width == 0 || m.height == 0) {
        QMessageBox::warning(this, tr("Preview"), tr("No mask data generated. Please draw shapes or select a mask type."));
        return;
    }
    
    m_livePreview->updateMask(m.data, m.width, m.height,
                              static_cast<ImageBuffer::DisplayMode>(m_previewStretchCombo->currentData().toInt()),
                              m_previewInvertCb->isChecked(),
                              m_previewFalseColorCb->isChecked());
    
    // Show if hidden (reuse if already open)
    if (!m_livePreview->isVisible()) {
        m_livePreview->show();
    }
    m_livePreview->raise();
    m_livePreview->activateWindow();
}

std::vector<float> MaskGenerationDialog::getColorMask(const QString& color, int w, int h) const {
    // Return zeros if not RGB
    if (m_sourceImage.channels() < 3) {
        return std::vector<float>(w*h, 0.0f);
    }
    
    // Resize source if needed
    cv::Mat rgb8(h, w, CV_8UC3);
    if (w == m_sourceImage.width() && h == m_sourceImage.height()) {
        const std::vector<float>& data = m_sourceImage.data();
        #pragma omp parallel for
        for (int i = 0; i < h*w; ++i) {
            rgb8.at<cv::Vec3b>(i)[0] = static_cast<uchar>(std::clamp(data[i*3] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[1] = static_cast<uchar>(std::clamp(data[i*3+1] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[2] = static_cast<uchar>(std::clamp(data[i*3+2] * 255.0f, 0.0f, 255.0f));
        }
    } else {
        cv::Mat fullSrc(m_sourceImage.height(), m_sourceImage.width(), CV_32FC3, const_cast<float*>(m_sourceImage.data().data()));
        cv::Mat smallSrc;
        cv::resize(fullSrc, smallSrc, cv::Size(w, h), 0, 0, cv::INTER_AREA);
        #pragma omp parallel for
        for (int i = 0; i < h*w; ++i) {
            float* p = reinterpret_cast<float*>(smallSrc.data) + i*3;
            rgb8.at<cv::Vec3b>(i)[0] = static_cast<uchar>(std::clamp(p[0] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[1] = static_cast<uchar>(std::clamp(p[1] * 255.0f, 0.0f, 255.0f));
            rgb8.at<cv::Vec3b>(i)[2] = static_cast<uchar>(std::clamp(p[2] * 255.0f, 0.0f, 255.0f));
        }
    }
    
    // Convert RGB to HLS
    cv::Mat hls;
    cv::cvtColor(rgb8, hls, cv::COLOR_RGB2HLS);
    
    // Define color ranges (in degrees, 0-360)
    struct Range { float lo; float hi; };
    std::vector<Range> ranges;
    
    if (color == "Red") {
        ranges = {{0, 15}, {345, 360}}; // Widened a bit
    } else if (color == "Orange") {
        ranges = {{15, 45}};
    } else if (color == "Yellow") {
        ranges = {{45, 75}};
    } else if (color == "Green") {
        ranges = {{75, 165}};
    } else if (color == "Cyan") {
        ranges = {{165, 195}};
    } else if (color == "Blue") {
        ranges = {{195, 265}};
    } else if (color == "Magenta") {
        ranges = {{265, 345}};
    } else {
        return std::vector<float>(w*h, 0.0f);
    }
    
    // Create mask based on hue ranges
    std::vector<float> mask(w*h, 0.0f);
    
    #pragma omp parallel for
    for (int i = 0; i < h*w; ++i) {
        float hue = (hls.at<cv::Vec3b>(i)[0] / 180.0f) * 360.0f;
        float s = hls.at<cv::Vec3b>(i)[2] / 255.0f;
        
        if (s < 0.1f) continue; // Skip low saturation pixels (neutral/gray)

        for (const auto& r : ranges) {
            if (hue >= r.lo && hue <= r.hi) {
                mask[i] = 1.0f;
                break;
            }
        }
    }
    
    return mask;
}

std::vector<float> MaskGenerationDialog::getColorMask(const QString& color) const {
    return getColorMask(color, m_sourceImage.width(), m_sourceImage.height());
}

std::vector<float> MaskGenerationDialog::getLightness(int w, int h) const {
    if (w == m_smallW && h == m_smallH && !m_smallLuma.empty()) {
        return m_smallLuma;
    }
    // If requesting full size
    if (w == m_sourceImage.width() && h == m_sourceImage.height()) {
        return getComponentLightness();
    }
    // Arbitrary size - resize on the fly
    std::vector<float> L = getComponentLightness();
    std::vector<float> res(w * h);
    cv::Mat src(m_sourceImage.height(), m_sourceImage.width(), CV_32FC1, L.data());
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(w, h), 0, 0, cv::INTER_AREA);
    memcpy(res.data(), dst.data, res.size() * sizeof(float));
    return res;
}
