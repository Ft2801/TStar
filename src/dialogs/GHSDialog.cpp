#include "GHSDialog.h"
#include "widgets/HistogramWidget.h"
#include "algos/GHSAlgo.h"
#include "../ImageViewer.h"
#include <QDebug> 
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QToolButton>
#include <QScrollArea>
#include <QIcon>
#include <QTimer>
#include <QShortcut>
#include <QKeySequence>


GHSDialog::GHSDialog(QWidget *parent) : DialogBase(parent, tr("Generalized Hyperbolic Stretch (GHT)"), 440, 600), m_previewPending(false), m_activeViewer(nullptr), m_applied(false) {
    // Interaction
    m_interactionEnabled = false;
    setModal(false);
    setupUI();
    connectSignals();
}

GHSDialog::~GHSDialog() {
    // Cleanup on destruction if not already handled by reject()
    if (m_activeViewer && !m_applied) {
        m_activeViewer->setBuffer(m_originalBuffer, m_activeViewer->windowTitle(), true);
    }
    // Always reset interaction mode to prevent lingering selection cursor
    if (m_activeViewer) {
        m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
        m_activeViewer->setCursor(Qt::ArrowCursor);
    }
}

void GHSDialog::reject() {
    // Restore original if we have an active viewer and were previewing
    if (m_activeViewer) {
        // Disconnect first
        m_activeViewer->setRegionSelectedCallback(nullptr);
        disconnect(m_activeViewer, &ImageViewer::destroyed, this, nullptr);
        disconnect(m_activeViewer, &ImageViewer::bufferChanged, this, nullptr);
        
        m_selfUpdating = true;
        m_activeViewer->setBuffer(m_originalBuffer, m_activeViewer->windowTitle(), true);
        
        // Reset Interaction
        m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
        m_activeViewer->setCursor(Qt::ArrowCursor);
        m_selfUpdating = false;
        
        // Clear active pointer
        m_activeViewer = nullptr;
    }
    
    QDialog::reject();
}

void GHSDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    
    // === Histogram Toolbar ===
    QHBoxLayout *histoToolbar = new QHBoxLayout();
    
    // Zoom controls with + / - buttons
    histoToolbar->addWidget(new QLabel(tr("Zoom:")));
    
    m_zoomOutBtn = new QToolButton();
    m_zoomOutBtn->setText("-");
    m_zoomOutBtn->setFixedWidth(24);
    m_zoomOutBtn->setToolTip(tr("Zoom Out"));
    histoToolbar->addWidget(m_zoomOutBtn);
    
    m_zoomLabel = new QLabel("1x");
    m_zoomLabel->setMinimumWidth(35);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    histoToolbar->addWidget(m_zoomLabel);
    
    m_zoomInBtn = new QToolButton();
    m_zoomInBtn->setText("+");
    m_zoomInBtn->setFixedWidth(24);
    m_zoomInBtn->setToolTip(tr("Zoom In"));
    histoToolbar->addWidget(m_zoomInBtn);
    
    // Zoom Reset
    m_zoomResetBtn = new QToolButton();
    m_zoomResetBtn->setText("1:1");
    m_zoomResetBtn->setToolTip(tr("Reset zoom to 1"));
    histoToolbar->addWidget(m_zoomResetBtn);
    
    histoToolbar->addStretch();
    
    // Log Scale
    m_logScaleCheck = new QCheckBox(tr("Logarithmic"));
    m_logScaleCheck->setChecked(false); // Off by default
    m_logScaleCheck->setToolTip(tr("Logarithmic histogram scale"));
    histoToolbar->addWidget(m_logScaleCheck);
    
    mainLayout->addLayout(histoToolbar);
    
    // === Histogram Scroll Area ===
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true); // Allow widget to be smaller than area, but we will force size when zoomed
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    // Style the scrollbar to be thin and rounded
    m_scrollArea->setStyleSheet(
        "QScrollArea { border: none; background: #1a1a1a; }"
        "QScrollBar:horizontal { border: none; background: #2b2b2b; height: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 4px; }"
        "QScrollBar::handle:horizontal:hover { background: #666; }"
        "QScrollBar::add-line:horizontal { width: 0px; }"
        "QScrollBar::sub-line:horizontal { width: 0px; }"
    );
    
    m_histWidget = new HistogramWidget(this);
    m_histWidget->setMinimumHeight(100); // Reduced to prevent overflow, let layout/stretch handle it
    m_histWidget->setLogScale(false); 
    
    m_scrollArea->setWidget(m_histWidget);
    mainLayout->addWidget(m_scrollArea, 1); // Give it stretch factor

    // Separate Horizontal ScrollBar below the scroll area
    m_histScrollBar = new QScrollBar(Qt::Horizontal);
    m_histScrollBar->setVisible(false);
    m_histScrollBar->setFixedHeight(10);
    m_histScrollBar->setStyleSheet(
        "QScrollBar:horizontal { border: none; background: #2b2b2b; height: 10px; margin: 0px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal:hover { background: #666; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
    );
    mainLayout->addWidget(m_histScrollBar);

    // Sync scrollbar with scroll area
    connect(m_histScrollBar, &QScrollBar::valueChanged, [this](int v){
        m_scrollArea->horizontalScrollBar()->setValue(v);
    });
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::rangeChanged, [this](int min, int max){
        m_histScrollBar->setRange(min, max);
    });
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, [this](int v){
        m_histScrollBar->setValue(v);
    });

    // Add Undo/Redo Shortcuts to Dialog
    // (Managed globally by MainWindow)


    
    // === Channel / Display Toolbar ===
    QHBoxLayout *channelToolbar = new QHBoxLayout();
    
    // Channel buttons
    // Helper for styled toggles
    auto createToggle = [&](const QString& text, const QString& col, const QString& tip) {
        QToolButton* btn = new QToolButton();
        btn->setText(text);
        btn->setCheckable(true);
        btn->setChecked(true);
        btn->setFixedSize(30, 30);
        btn->setStyleSheet(QString("QToolButton:checked { background-color: %1; color: black; font-weight: bold; }").arg(col));
        btn->setToolTip(tip);
        return btn;
    };
    
    m_redBtn = createToggle("R", "#ff0000", tr("Toggle Red channel"));
    m_greenBtn = createToggle("G", "#00ff00", tr("Toggle Green channel"));
    m_blueBtn = createToggle("B", "#0000ff", tr("Toggle Blue channel"));
    
    channelToolbar->addWidget(m_redBtn);
    channelToolbar->addWidget(m_greenBtn);
    channelToolbar->addWidget(m_blueBtn);
    
    channelToolbar->addSpacing(10);
    
    m_gridBtn = new QToolButton();
    m_gridBtn->setText(tr("Grid"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setChecked(true);
    m_gridBtn->setToolTip(tr("Show grid overlay"));
    channelToolbar->addWidget(m_gridBtn);
    
    m_curveBtn = new QToolButton();
    m_curveBtn->setText(tr("Curve"));
    m_curveBtn->setCheckable(true);
    m_curveBtn->setChecked(true);
    m_curveBtn->setToolTip(tr("Show transfer curve"));
    channelToolbar->addWidget(m_curveBtn);
    
    channelToolbar->addStretch();
    
    mainLayout->addLayout(channelToolbar);
    
    // === Mode Selection ===
    QGroupBox *modeGroup = new QGroupBox(tr("Stretch Mode"));
    QVBoxLayout *modeLayout = new QVBoxLayout(modeGroup);
    
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem(tr("Generalized Hyperbolic Stretch"), ImageBuffer::GHS_GeneralizedHyperbolic);
    m_modeCombo->addItem(tr("Inverse GHS"), ImageBuffer::GHS_InverseGeneralizedHyperbolic);
    m_modeCombo->addItem(tr("ArcSinh Stretch"), ImageBuffer::GHS_ArcSinh);
    m_modeCombo->addItem(tr("Inverse ArcSinh"), ImageBuffer::GHS_InverseArcSinh);
    m_modeCombo->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    modeLayout->addWidget(m_modeCombo);
    
    m_colorCombo = new QComboBox();
    m_colorCombo->addItem(tr("RGB (Independent)"), ImageBuffer::GHS_Independent);
    m_colorCombo->addItem(tr("Human Weighted Luminance"), ImageBuffer::GHS_WeightedLuminance);
    m_colorCombo->addItem(tr("Even Weighted Luminance"), ImageBuffer::GHS_EvenWeightedLuminance);
    m_colorCombo->addItem(tr("Saturation"), ImageBuffer::GHS_Saturation);
    m_colorCombo->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    modeLayout->addWidget(m_colorCombo);
    
    // === Clipping Stats ===
    QHBoxLayout* clipLayout = new QHBoxLayout();
    m_lowClipLabel = new QLabel(tr("Low: 0.00%"));
    m_highClipLabel = new QLabel(tr("High: 0.00%"));
    m_lowClipLabel->setStyleSheet("color: #ff8888; margin-right: 10px;");
    m_highClipLabel->setStyleSheet("color: #8888ff;");
    clipLayout->addWidget(m_lowClipLabel);
    clipLayout->addWidget(m_highClipLabel);
    clipLayout->addStretch();
    mainLayout->addLayout(clipLayout);

    mainLayout->addWidget(modeGroup);
    
    // === Parameters Grid ===
    QGroupBox *paramsGroup = new QGroupBox(tr("Parameters"));
    QGridLayout *paramsGrid = new QGridLayout(paramsGroup);
    paramsGrid->setColumnStretch(1, 1);
    
    // Helper lambda for slider+spin
    auto makeSliderRow = [&](int row, const QString& label, QDoubleSpinBox*& spin, QSlider*& slider, 
                             double min, double max, double val, double step, int decimals, int sliderMax = 10000) {
        paramsGrid->addWidget(new QLabel(label), row, 0);
        
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, sliderMax);
        double t = (val - min) / (max - min);
        slider->setValue(static_cast<int>(t * sliderMax));
        paramsGrid->addWidget(slider, row, 1);
        
        spin = new QDoubleSpinBox();
        spin->setRange(min, max);
        spin->setValue(val);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setMinimumWidth(90);
        paramsGrid->addWidget(spin, row, 2);
    };
    
    // D: Stretch Factor - range: 0-10, default 0
    makeSliderRow(0, tr("Stretch (D):"), m_dSpin, m_dSlider, 0.0, 10.0, 0.0, 0.01, 2);
    
    // B: Local Intensity - range: -5 to 15, default 0.0 (was 0.5)
    makeSliderRow(1, tr("Intensity (B):"), m_bSpin, m_bSlider, -5.0, 15.0, 0.0, 0.01, 2);
    
    // SP: Symmetry Point - range: 0-1, default 0
    QHBoxLayout* spRow = new QHBoxLayout();
    paramsGrid->addWidget(new QLabel(tr("Symmetry (SP):")), 2, 0);
    
    m_spSlider = new QSlider(Qt::Horizontal);
    m_spSlider->setRange(0, 10000);
    m_spSlider->setValue(0);
    spRow->addWidget(m_spSlider);
    
    m_spSpin = new QDoubleSpinBox();
    m_spSpin->setRange(0.0, 1.0);
    m_spSpin->setValue(0.0);
    m_spSpin->setSingleStep(0.001);
    m_spSpin->setDecimals(4);
    m_spSpin->setMinimumWidth(90);
    spRow->addWidget(m_spSpin);
    
    
    QWidget* spWidget = new QWidget();
    spWidget->setLayout(spRow);
    paramsGrid->addWidget(spWidget, 2, 1, 1, 2);
    
    // LP: Low Protection - range: 0-1, default 0
    makeSliderRow(3, tr("Shadow (LP):"), m_lpSpin, m_lpSlider, 0.0, 1.0, 0.0, 0.001, 4);
    
    // HP: High Protection - range: 0-1, default 1
    makeSliderRow(4, tr("Highlight (HP):"), m_hpSpin, m_hpSlider, 0.0, 1.0, 1.0, 0.001, 4);
    
    // BP: Black Point - range: 0-0.2, default 0
    // Increased precision as requested 
    makeSliderRow(5, tr("Black Point (BP):"), m_bpSpin, m_bpSlider, 0.0, 0.2, 0.0, 0.0001, 5);
    
    mainLayout->addWidget(paramsGroup);
    
    // === Clip Mode ===
    QHBoxLayout *clipRow = new QHBoxLayout();
    clipRow->addWidget(new QLabel(tr("Clip Mode:")));
    m_clipModeCombo = new QComboBox();
    m_clipModeCombo->addItem(tr("Clip"), ImageBuffer::GHS_Clip);
    m_clipModeCombo->addItem(tr("Rescale"), ImageBuffer::GHS_Rescale);
    m_clipModeCombo->addItem(tr("RGB Blend"), ImageBuffer::GHS_ClipRGBBlend);
    m_clipModeCombo->addItem(tr("Global rescale"), ImageBuffer::GHS_RescaleGlobal);
    m_clipModeCombo->setToolTip(tr("How to handle values outside 0-1 range"));
    m_clipModeCombo->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    clipRow->addWidget(m_clipModeCombo);
    clipRow->addStretch();
    
    m_previewCheck = new QCheckBox(tr("Preview"));
    m_previewCheck->setChecked(true);
    m_previewCheck->setToolTip(tr("Enable live preview"));
    clipRow->addWidget(m_previewCheck);
    
    mainLayout->addLayout(clipRow);
    
    // === Buttons ===
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *resetBtn = new QPushButton(tr("Reset"));
    QPushButton *applyBtn = new QPushButton(tr("Apply"));
    QPushButton *closeBtn = new QPushButton(tr("Close"));
    
    applyBtn->setStyleSheet("QPushButton { background-color: #3a7d44; }");
    
    btnLayout->addWidget(resetBtn);
    
    QLabel* copyLabel = new QLabel(tr("© 2026 Mike Cranfield"));
    copyLabel->setStyleSheet("color: #888; font-size: 10px; margin-left: 10px;");
    btnLayout->addWidget(copyLabel);
    
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);
    
    connect(resetBtn, &QPushButton::clicked, this, &GHSDialog::onReset);
    connect(applyBtn, &QPushButton::clicked, this, &GHSDialog::onApply);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Initialize Throttle Timer
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(100); // 100ms throttle
    
    connect(m_previewTimer, &QTimer::timeout, this, [this](){
        if (m_previewPending) {
            m_previewPending = false;
            onPreviewTrigger(); 
        }
    });

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

void GHSDialog::connectSignals() {
    // Helper for slider<->spin sync with Split Updates
    auto syncSliderSpin = [this](QSlider* slider, QDoubleSpinBox* spin, double min, double max, int sliderMax) {
        // SpinBox: Discrete changes -> Full Preview
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [=](double d){
            double ratio = (d - min) / (max - min);
            int sVal = static_cast<int>(ratio * sliderMax);
            if (slider->value() != sVal) {
                slider->blockSignals(true);
                slider->setValue(sVal);
                slider->blockSignals(false);
            }
            onPreviewTrigger(); // Full Update
        });
        
        // Slider Drag: Real-time Curve Update Only
        connect(slider, &QSlider::valueChanged, [=](int i){
            double ratio = static_cast<double>(i) / sliderMax;
            double d = min + ratio * (max - min);
            if (std::abs(spin->value() - d) > 1e-6) {
                spin->blockSignals(true);
                spin->setValue(d);
                spin->blockSignals(false);
            }
            onValueChange(); // Curve Update Only (Histogram widget)
            updateHistogram(); // Redraw histogram with new curve
            // Removed onPreviewTrigger() here to prevent lag. Updates only on release.
        });
        
        // Slider Release: Trigger Full Preview
        connect(slider, &QSlider::sliderReleased, this, &GHSDialog::onPreviewTrigger);
    };
    
    syncSliderSpin(m_dSlider, m_dSpin, 0.0, 10.0, 10000);
    syncSliderSpin(m_bSlider, m_bSpin, -5.0, 15.0, 10000);
    syncSliderSpin(m_lpSlider, m_lpSpin, 0.0, 1.0, 10000);
    syncSliderSpin(m_hpSlider, m_hpSpin, 0.0, 1.0, 10000);
    syncSliderSpin(m_bpSlider, m_bpSpin, 0.0, 0.2, 10000);
    
    // BP Special: Enforce real-time update during drag (it's fast and critical for visual feedback)
    connect(m_bpSlider, &QSlider::valueChanged, this, [this](int){
        if(m_previewCheck->isChecked()) onPreviewTrigger();
    });
    
    // SP special (0-1 range)
    connect(m_spSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double d){
        int sVal = static_cast<int>(d * 10000);
        if (m_spSlider->value() != sVal) {
            m_spSlider->blockSignals(true);
            m_spSlider->setValue(sVal);
            m_spSlider->blockSignals(false);
        }
        onPreviewTrigger(); // Full Update
    });
    connect(m_spSlider, &QSlider::valueChanged, [this](int i){
        double d = static_cast<double>(i) / 10000.0;
        if (std::abs(m_spSpin->value() - d) > 1e-6) {
            m_spSpin->blockSignals(true);
            m_spSpin->setValue(d);
            m_spSpin->blockSignals(false);
        }
        onValueChange(); // Curve Update Only
    });
    connect(m_spSlider, &QSlider::sliderReleased, this, &GHSDialog::onPreviewTrigger);
    
    // Mode changes
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int){ onPreviewTrigger(); });
    connect(m_colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int){ onPreviewTrigger(); });
    connect(m_clipModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int){ onPreviewTrigger(); });
    
    // Histogram controls - Zoom buttons
    connect(m_zoomInBtn, &QToolButton::clicked, [this](){
        if (m_zoomLevel < 10) {
            m_zoomLevel++;
            m_zoomLabel->setText(QString("%1x").arg(m_zoomLevel));
            onZoomChanged();
        }
    });
    connect(m_zoomOutBtn, &QToolButton::clicked, [this](){
        if (m_zoomLevel > 1) {
            m_zoomLevel--;
            m_zoomLabel->setText(QString("%1x").arg(m_zoomLevel));
            onZoomChanged();
        }
    });
    connect(m_zoomResetBtn, &QToolButton::clicked, [this](){ 
        m_zoomLevel = 1;
        m_zoomLabel->setText("1x");
        onZoomChanged();
    });
    connect(m_logScaleCheck, &QCheckBox::toggled, [this](bool checked){
        m_histWidget->setLogScale(checked);
    });
    
    // Channel toggles
    connect(m_redBtn, &QToolButton::toggled, this, &GHSDialog::onChannelToggled);
    connect(m_greenBtn, &QToolButton::toggled, this, &GHSDialog::onChannelToggled);
    connect(m_blueBtn, &QToolButton::toggled, this, &GHSDialog::onChannelToggled);
    connect(m_gridBtn, &QToolButton::toggled, [this](bool){ updateHistogram(); });
    connect(m_curveBtn, &QToolButton::toggled, [this](bool){ updateHistogram(); });
    
    connect(m_previewCheck, &QCheckBox::toggled, this, &GHSDialog::onPreviewTrigger);
}

void GHSDialog::setHistogramData(const std::vector<std::vector<int>>& bins, int channels) {
    m_origBins = bins;
    m_channels = channels;
    m_histWidget->setGhostData(bins, channels);
    updateHistogram();
}

void GHSDialog::setSymmetryPoint(double sp) {
    m_spSpin->blockSignals(true);
    m_spSlider->blockSignals(true);
    m_spSpin->setValue(sp);
    m_spSlider->setValue(static_cast<int>(sp * 10000));
    m_spSpin->blockSignals(false);
    m_spSlider->blockSignals(false);
    onPreviewTrigger(); // Pick should update preview immediately
}

void GHSDialog::setClippingStats(float lowPct, float highPct) {
    if (m_lowClipLabel) m_lowClipLabel->setText(tr("Low: %1%").arg(lowPct, 0, 'f', 4));
    if (m_highClipLabel) m_highClipLabel->setText(tr("High: %1%").arg(highPct, 0, 'f', 4));
}

ImageBuffer::GHSParams GHSDialog::getParams() const {
    ImageBuffer::GHSParams params;
    params.D = m_dSpin->value();
    params.B = m_bSpin->value();
    params.SP = m_spSpin->value();
    params.LP = m_lpSpin->value();
    params.HP = m_hpSpin->value();
    params.BP = m_bpSpin->value();
    params.mode = static_cast<ImageBuffer::GHSMode>(m_modeCombo->currentData().toInt());
    params.colorMode = static_cast<ImageBuffer::GHSColorMode>(m_colorCombo->currentData().toInt());
    params.clipMode = static_cast<ImageBuffer::GHSClipMode>(m_clipModeCombo->currentData().toInt());
    params.inverse = (params.mode == ImageBuffer::GHS_InverseGeneralizedHyperbolic || 
                      params.mode == ImageBuffer::GHS_InverseArcSinh);
    params.channels[0] = m_redBtn->isChecked();
    params.channels[1] = m_greenBtn->isChecked();
    params.channels[2] = m_blueBtn->isChecked();
    return params;
}

GHSDialog::State GHSDialog::getState() const {
    State s;
    s.d = m_dSpin->value();
    s.b = m_bSpin->value();
    s.sp = m_spSpin->value();
    s.lp = m_lpSpin->value();
    s.hp = m_hpSpin->value();
    s.bp = m_bpSpin->value();
    
    s.mode = m_modeCombo->currentIndex(); // Save index, not data, for easier restoration
    s.colorMode = m_colorCombo->currentIndex();
    s.clipMode = m_clipModeCombo->currentIndex();
    
    s.channels[0] = m_redBtn->isChecked();
    s.channels[1] = m_greenBtn->isChecked();
    s.channels[2] = m_blueBtn->isChecked();
    
    s.logScale = m_logScaleCheck->isChecked();
    
    s.sliderD = m_dSlider->value();
    s.sliderB = m_bSlider->value();
    s.sliderSP = m_spSlider->value();
    s.sliderLP = m_lpSlider->value();
    s.sliderHP = m_hpSlider->value();
    s.sliderBP = m_bpSlider->value();
    return s;
}

void GHSDialog::setState(const State& s) {
    // Block signals to prevent massive intermediate updates
    bool wasBlocked = signalsBlocked();
    blockSignals(true);
    m_dSpin->blockSignals(true); m_dSlider->blockSignals(true);
    m_bSpin->blockSignals(true); m_bSlider->blockSignals(true);
    m_spSpin->blockSignals(true); m_spSlider->blockSignals(true);
    m_lpSpin->blockSignals(true); m_lpSlider->blockSignals(true);
    m_hpSpin->blockSignals(true); m_hpSlider->blockSignals(true);
    m_bpSpin->blockSignals(true); m_bpSlider->blockSignals(true);
    m_modeCombo->blockSignals(true);
    m_colorCombo->blockSignals(true);
    m_clipModeCombo->blockSignals(true);
    m_redBtn->blockSignals(true);
    m_greenBtn->blockSignals(true);
    m_blueBtn->blockSignals(true);

    m_dSpin->setValue(s.d);
    m_bSpin->setValue(s.b);
    m_spSpin->setValue(s.sp);
    m_lpSpin->setValue(s.lp);
    m_hpSpin->setValue(s.hp);
    m_bpSpin->setValue(s.bp);

    m_dSlider->setValue(s.sliderD);
    m_bSlider->setValue(s.sliderB);
    m_spSlider->setValue(s.sliderSP);
    m_lpSlider->setValue(s.sliderLP);
    m_hpSlider->setValue(s.sliderHP);
    m_bpSlider->setValue(s.sliderBP);

    m_modeCombo->setCurrentIndex(s.mode);
    m_colorCombo->setCurrentIndex(s.colorMode);
    m_clipModeCombo->setCurrentIndex(s.clipMode);

    m_redBtn->setChecked(s.channels[0]);
    m_greenBtn->setChecked(s.channels[1]);
    m_blueBtn->setChecked(s.channels[2]);
    
    m_logScaleCheck->setChecked(s.logScale);

    // Unblock
    m_dSpin->blockSignals(false); m_dSlider->blockSignals(false);
    m_bSpin->blockSignals(false); m_bSlider->blockSignals(false);
    m_spSpin->blockSignals(false); m_spSlider->blockSignals(false);
    m_lpSpin->blockSignals(false); m_lpSlider->blockSignals(false);
    m_hpSpin->blockSignals(false); m_hpSlider->blockSignals(false);
    m_bpSpin->blockSignals(false); m_bpSlider->blockSignals(false);
    m_modeCombo->blockSignals(false);
    m_colorCombo->blockSignals(false);
    m_clipModeCombo->blockSignals(false);
    m_redBtn->blockSignals(false);
    m_greenBtn->blockSignals(false);
    m_blueBtn->blockSignals(false);
    
    // Update internal state vars
    m_showRed = s.channels[0];
    m_showGreen = s.channels[1];
    m_showBlue = s.channels[2];
    m_histWidget->setLogScale(s.logScale);

    blockSignals(wasBlocked);

    // Trigger update? 
    // Usually we set state then TRIGGER preview if needed.
    // But caller might just want to restore UI.
}

void GHSDialog::resetState() {
    onReset();
}

void GHSDialog::onApply() {
    ImageBuffer::GHSParams params = getParams();
    
    // Internal Apply Logic (replaces MainWindow connection)
    if (!m_activeViewer) return;
    
    m_selfUpdating = true;
    // 1. Restore the viewer to the CLEAN state (before preview)
    m_activeViewer->setBuffer(m_originalBuffer, m_activeViewer->windowTitle(), true);
    
    // 2. Push history of the CLEAN state
    m_activeViewer->pushUndo();
    
    // 3. Apply the stretch to our base buffer
    m_originalBuffer.applyGHS(params); 
    
    // 4. Update Viewer Permanently with the stretched image
    m_activeViewer->setBuffer(m_originalBuffer, m_activeViewer->windowTitle(), true);
    m_selfUpdating = false;
    
    m_applied = true;
    
    // Reset Intensity for iterative use
    resetIntensity();
    
    // Re-compute Histogram
    setHistogramData(m_originalBuffer.computeHistogram(), m_originalBuffer.channels());
    
    emit applied(tr("GHS Transformation applied to %1").arg(m_activeViewer->windowTitle()));
}

// === Context Switching ===
void GHSDialog::setTarget(ImageViewer* viewer) {
    qDebug() << "[GHSDialog::setTarget] New:" << viewer << "Old:" << m_activeViewer;
    if (m_activeViewer == viewer) return;
    
    // Disconnect and Restore old
    if (m_activeViewer) {
        // DISCONNECT FIRST to prevent signals (especially bufferChanged) during restoration
        m_activeViewer->setRegionSelectedCallback(nullptr);
        disconnect(m_activeViewer, &ImageViewer::destroyed, this, nullptr);
        disconnect(m_activeViewer, &ImageViewer::bufferChanged, this, nullptr);

        // Restore previous viewer to its clean state (remove preview)
        m_selfUpdating = true;
        qDebug() << "[GHSDialog::setTarget] Restoring buffer on Old Viewer. Visible?" << m_activeViewer->isVisible();
        if (m_activeViewer->isVisible()) { // Tentative fix/check
            m_activeViewer->setBuffer(m_originalBuffer, m_activeViewer->windowTitle(), true);
        } else {
             qDebug() << "[GHSDialog::setTarget] Skipping restore because viewer is not visible.";
        }
        
        // Reset Interaction Mode
        m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
        m_activeViewer->setCursor(Qt::ArrowCursor);
        m_selfUpdating = false;
    }
    
    m_activeViewer = viewer;
    m_applied = false;
    
    if (m_activeViewer) {
        // Connect New
        // SYNC: Listen for external buffer changes (Undo/Redo from Main Menu)
        connect(m_activeViewer, &ImageViewer::bufferChanged, this, [this](){
            if (m_selfUpdating) return; // Warning: recursive loop prevention
            
            // External Update Detected (e.g. Undo/Redo)
            // Re-sync base state
            m_originalBuffer = m_activeViewer->getBuffer();
            setHistogramData(m_originalBuffer.computeHistogram(), m_originalBuffer.channels());
            
            // Re-trigger preview (applies current params to NEW base)
            onPreviewTrigger();
        });

        // SAFETY: Handle Viewer Destruction
        connect(m_activeViewer, &ImageViewer::destroyed, this, [this](QObject* obj){
            if (obj == m_activeViewer) {
               m_activeViewer = nullptr; 
               if (m_previewTimer) m_previewTimer->stop();
               m_previewPending = false;
            }
        });
        
        // Selection for SP (Only if interaction enabled)
        if (m_interactionEnabled) {
            m_activeViewer->setInteractionMode(ImageViewer::Mode_Selection);
            m_activeViewer->setCursor(Qt::CrossCursor);
        }
        
        m_activeViewer->setRegionSelectedCallback([this](QRectF r){
            qDebug() << "[GHSDialog] rectSelected lambda called for rect:" << r;
            try {
                 if (!m_activeViewer) return; // Safety check
                 
                 r = r.normalized();
                 if (r.isEmpty()) return;
                 QRect rect = r.toRect();
                 
                 // Validate rect against buffer dimensions
                 if (rect.right() < 0 || rect.bottom() < 0 || 
                     rect.left() >= m_originalBuffer.width() || rect.top() >= m_originalBuffer.height()) {
                     return; // Completely outside
                 }
                 
                 float val = 0.0f;
                 if (m_originalBuffer.channels() == 3) {
                     float sumMeans = 0.0f;
                     int activeCount = 0;
                     if (m_redBtn->isChecked()) { sumMeans += m_originalBuffer.getAreaMean(rect.x(), rect.y(), rect.width(), rect.height(), 0); activeCount++; }
                     if (m_greenBtn->isChecked()) { sumMeans += m_originalBuffer.getAreaMean(rect.x(), rect.y(), rect.width(), rect.height(), 1); activeCount++; }
                     if (m_blueBtn->isChecked()) { sumMeans += m_originalBuffer.getAreaMean(rect.x(), rect.y(), rect.width(), rect.height(), 2); activeCount++; }
                     val = (activeCount > 0) ? (sumMeans / activeCount) : 0.0f;
                 } else {
                     val = m_originalBuffer.getAreaMean(rect.x(), rect.y(), rect.width(), rect.height(), 0);
                 }
                 setSymmetryPoint(static_cast<double>(val));
            } catch (const std::exception& e) {
                qWarning() << "GHSDialog selection error:" << e.what();
            } catch (...) {
                qWarning() << "GHSDialog selection error: unknown exception";
            }
        });
        
        // Sync State
        m_originalBuffer = m_activeViewer->getBuffer();
        setHistogramData(m_originalBuffer.computeHistogram(65536), m_originalBuffer.channels());
        
        // Defer preview until dialog is fully shown to avoid fade-in lag
        QTimer::singleShot(300, this, &GHSDialog::onPreviewTrigger);
        
    } else {
        // Reset/Clear UI
        m_originalBuffer = ImageBuffer();
        if (m_histWidget) m_histWidget->clear();
    }
}

void GHSDialog::onReset() {
    // Reset ALL parameters to defaults
    m_dSpin->blockSignals(true);
    m_bSpin->blockSignals(true);
    m_spSpin->blockSignals(true);
    m_lpSpin->blockSignals(true);
    m_hpSpin->blockSignals(true);
    m_bpSpin->blockSignals(true);
    
    m_dSpin->setValue(0.0);
    m_bSpin->setValue(0.0);
    m_spSpin->setValue(0.0);
    m_lpSpin->setValue(0.0);
    m_hpSpin->setValue(1.0);
    m_bpSpin->setValue(0.0);
    m_modeCombo->setCurrentIndex(0);
    m_colorCombo->setCurrentIndex(0);
    m_clipModeCombo->setCurrentIndex(0);
    
    m_dSpin->blockSignals(false);
    m_bSpin->blockSignals(false);
    m_spSpin->blockSignals(false);
    m_lpSpin->blockSignals(false);
    m_hpSpin->blockSignals(false);
    m_bpSpin->blockSignals(false);
    
    // Sync sliders
    m_dSlider->setValue(0);
    m_bSlider->setValue(2500); // 0.0 mapped to -5..15 range
    m_spSlider->setValue(0);
    m_lpSlider->setValue(0);
    m_hpSlider->setValue(10000);
    m_bpSlider->setValue(0);
    
    onPreviewTrigger(); // Full Reset
}

void GHSDialog::resetIntensity() {
    // Partial Reset: Clear D, B (Intensity), and BP but keep SP, Mode, Color settings
    m_dSpin->blockSignals(true);
    m_bSpin->blockSignals(true);
    m_bpSpin->blockSignals(true);
    
    m_dSpin->setValue(0.0);
    m_bSpin->setValue(0.0); // Reset B to neutral (0.0)
    m_bpSpin->setValue(0.0); // Reset BP as requested
    
    m_dSpin->blockSignals(false);
    m_bSpin->blockSignals(false);
    m_bpSpin->blockSignals(false);
    
    m_dSlider->setValue(0);
    m_bSlider->setValue(2500); // Sync with B=0.0
    m_bpSlider->setValue(0);
    
    updateHistogram(); // Update curve visualization
}

void GHSDialog::onValueChange() {
    // FAST UPDATE: Curve Only
    ImageBuffer::GHSParams params = getParams();
    
    // Compute LUT for Curve Visualization
    ImageBuffer ib;
    std::vector<float> lut = ib.computeGHSLUT(params);
    
    // Update histogram display with Curve
    if (m_histWidget) {
        m_histWidget->setTransformCurve(lut);
        updateHistogram(); // Redraw widget
    }
}

void GHSDialog::onPreviewTrigger() {
    // Throttling Check
    if (m_previewTimer->isActive()) {
        m_previewPending = true;
        return;
    }

    // Ensure curve is up to date (redundant if called from slider, but safe/needed for others)
    onValueChange();
    
    ImageBuffer::GHSParams params;
    
    if (m_previewCheck->isChecked()) {
        params = getParams();
    } else {
        // Identity Params (No Stretch)
        params.D = 0.0;
        params.B = 0.0;
        params.SP = 0.0;
        params.LP = 0.0; 
        params.HP = 1.0;
        params.BP = 0.0;
        params.mode = ImageBuffer::GHS_GeneralizedHyperbolic;
        params.colorMode = ImageBuffer::GHS_Independent; 
        params.inverse = false;
        params.channels[0] = true; 
        params.channels[1] = true; 
        params.channels[2] = true;
    }
    
    if (m_activeViewer) {
        // Internal Apply Preview
        // Identity Check: Must account for BP if we allow BP in GHS mode
        bool isIdentity = (std::abs(params.D) < 1e-6 && 
                          std::abs(params.BP) < 1e-6 && 
                          params.mode == ImageBuffer::GHS_GeneralizedHyperbolic);
        if (isIdentity) {
            m_selfUpdating = true;
            m_activeViewer->setBuffer(m_originalBuffer, m_activeViewer->windowTitle(), true);
            m_selfUpdating = false;
            setClippingStats(0, 0);
        } else {
            ImageBuffer temp = m_originalBuffer;
            temp.applyGHS(params);
            
            long lowClip = 0, highClip = 0;
            temp.computeClippingStats(lowClip, highClip);
            long total = static_cast<long>(temp.width()) * temp.height() * temp.channels();
            if(total > 0) {
                setClippingStats((100.0f * lowClip) / total, (100.0f * highClip) / total);
            }
            m_selfUpdating = true;
            m_activeViewer->setBuffer(temp, m_activeViewer->windowTitle(), true);
            m_selfUpdating = false;
        }
    } else {
        emit previewRequested(params);
    }
    
    // Start Throttle Timer
    m_previewTimer->start();
}

void GHSDialog::onZoomChanged() {
    // Zoom is handled by resizing the histogram widget inside the scroll area
    // The paintEvent draws to full width, effectively zooming the histogram
    
    // Get viewport width or a default base
    int baseWidth = m_scrollArea ? m_scrollArea->viewport()->width() : 400;
    if (baseWidth < 100) baseWidth = 400;
    
    int newWidth = baseWidth * m_zoomLevel;
    
    // If zoom is 1x, let it be resizable (fit to view)
    // If zoom > 1x, force fixed width
    if (m_zoomLevel == 1) {
        if (m_scrollArea) m_scrollArea->setWidgetResizable(true);
        m_histWidget->setMinimumWidth(0);
        m_histWidget->setMaximumWidth(16777215);
        m_histScrollBar->setVisible(false);
    } else {
        if (m_scrollArea) m_scrollArea->setWidgetResizable(true); // Keep resizable to allow dynamic height adjustment!
        m_histWidget->setMinimumWidth(newWidth);
        m_histWidget->setMaximumWidth(newWidth); // Lock width explicitly
        m_histScrollBar->setVisible(true);
        // Always keep the left side (shadows) in view when zooming
        if (m_scrollArea) m_scrollArea->horizontalScrollBar()->setValue(0);
    }
    
    updateHistogram();
}

void GHSDialog::onChannelToggled() {
    m_showRed = m_redBtn->isChecked();
    m_showGreen = m_greenBtn->isChecked();
    m_showBlue = m_blueBtn->isChecked();
    updateHistogram();
    onPreviewTrigger(); // Channel toggle is a discrete click, so update preview
}


void GHSDialog::updateHistogram() {
    if (m_origBins.empty() || !m_histWidget) return;
    
    // 1. Setup Algorithm for LUT/Curve display
    ImageBuffer::GHSParams params = getParams();
    int histSize = 65536;
    if (!m_origBins.empty() && !m_origBins[0].empty()) histSize = (int)m_origBins[0].size();
    
    // Curve LUT for visualization (keep at 4096 for smoothness)
    int lutSize = 4096;
    std::vector<float> lut(lutSize);
    
    GHSAlgo::GHSParams algoParams;
    algoParams.D = (float)params.D;
    algoParams.B = (float)params.B;
    algoParams.SP = (float)params.SP;
    algoParams.LP = (float)params.LP;
    algoParams.HP = (float)params.HP;
    algoParams.BP = (float)params.BP;

    switch(params.mode) {
        case ImageBuffer::GHS_GeneralizedHyperbolic: algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL; break;
        case ImageBuffer::GHS_InverseGeneralizedHyperbolic: algoParams.type = GHSAlgo::STRETCH_PAYNE_INVERSE; break;
        case ImageBuffer::GHS_Linear: algoParams.type = GHSAlgo::STRETCH_LINEAR; break;
        case ImageBuffer::GHS_ArcSinh: algoParams.type = GHSAlgo::STRETCH_ASINH; break;
        case ImageBuffer::GHS_InverseArcSinh: algoParams.type = GHSAlgo::STRETCH_INVASINH; break;
        default: algoParams.type = GHSAlgo::STRETCH_PAYNE_NORMAL; break;
    }

    GHSAlgo::GHSComputeParams cp;
    GHSAlgo::setup(cp, algoParams.B, algoParams.D, algoParams.LP, algoParams.SP, algoParams.HP, algoParams.type);

    for (int i = 0; i < lutSize; ++i) {
        lut[i] = GHSAlgo::compute((float)i / (lutSize - 1), algoParams, cp);
    }
    m_histWidget->setTransformCurve(lut);

    // 2. Real-time Histogram Recomputation (Transform Bins)
    // Optimization: Build a transform LUT once to avoid redundant expensive math per channel
    std::vector<int> transformLUT(histSize);
    for (int i = 0; i < histSize; ++i) {
        float normX = (float)i / (histSize - 1);
        float y = GHSAlgo::compute(normX, algoParams, cp);
        int outBin = static_cast<int>(y * (histSize - 1) + 0.5f);
        transformLUT[i] = std::clamp(outBin, 0, histSize - 1);
    }

    std::vector<std::vector<int>> transformedBins(m_channels, std::vector<int>(histSize, 0));
    for (int c = 0; c < m_channels && c < (int)m_origBins.size(); ++c) {
        const auto& chBins = m_origBins[c];
        auto& outBins = transformedBins[c];
        for (int i = 0; i < histSize; ++i) {
            int count = chBins[i];
            if (count > 0) {
                outBins[transformLUT[i]] += count;
            }
        }
    }

    // 3. Update Widget (Filter displayed channels)
    int displayChannels = 0;
    std::vector<std::vector<int>> displayBins;
    bool showChannels[3] = {m_showRed, m_showGreen, m_showBlue};

    for (int c = 0; c < m_channels && c < 3; ++c) {
        if (showChannels[c]) {
            displayBins.push_back(transformedBins[c]);
            displayChannels++;
        }
    }

    if (displayBins.empty()) {
        displayBins = transformedBins;
        displayChannels = m_channels;
    }

    m_histWidget->setData(displayBins, displayChannels);
    m_histWidget->setShowGrid(m_gridBtn->isChecked());
    m_histWidget->setShowCurve(m_curveBtn->isChecked());
}

void GHSDialog::setInteractionEnabled(bool enabled) {
    if (m_interactionEnabled == enabled) return;
    m_interactionEnabled = enabled;
    
    if (m_activeViewer) {
         if (enabled) {
             m_activeViewer->setInteractionMode(ImageViewer::Mode_Selection);
             m_activeViewer->setCursor(Qt::CrossCursor);
         } else {
             m_activeViewer->setInteractionMode(ImageViewer::Mode_PanZoom);
             m_activeViewer->setCursor(Qt::ArrowCursor);
             m_activeViewer->clearSelection();
         }
    }
}
