#include "StarRecompositionDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QMdiSubWindow>
#include <QLabel>
#include <QPushButton>
#include <QIcon>

#include <QDoubleSpinBox>
#include <QGroupBox>

StarRecompositionDialog::StarRecompositionDialog(QWidget* parent)
    : DialogBase(parent, tr("Star Recomposition"), 900, 500)
{
    setModal(true);
    createUI();
    populateCombos();
    setMinimumWidth(400);
    m_initializing = false;
}

void StarRecompositionDialog::setViewer(ImageViewer* v)
{
    Q_UNUSED(v);
    populateCombos(); // Refresh list to ensure new viewer is available
}

void StarRecompositionDialog::createUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    // Left: Controls
    QVBoxLayout* ctrlLayout = new QVBoxLayout();
    
    QGridLayout* grid = new QGridLayout();
    
    // Starless Source
    grid->addWidget(new QLabel(tr("Starless View:")), 0, 0);
    m_cmbStarless = new QComboBox();
    m_cmbStarless->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    grid->addWidget(m_cmbStarless, 0, 1);
    
    // Stars Source
    grid->addWidget(new QLabel(tr("Stars-Only View:")), 1, 0);
    m_cmbStars = new QComboBox();
    m_cmbStars->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    grid->addWidget(m_cmbStars, 1, 1);
    
    // --- Advanced Stretch Controls for Stars ---
    QGroupBox *stretchGroup = new QGroupBox(tr("Stars Stretch Parameters"));
    QGridLayout *sg = new QGridLayout(stretchGroup);
    
    // Stretch Mode
    sg->addWidget(new QLabel(tr("Stretch Mode:")), 0, 0);
    m_cmbStretchMode = new QComboBox();
    m_cmbStretchMode->addItem(tr("Generalized Hyperbolic Stretch"), ImageBuffer::GHS_GeneralizedHyperbolic);
    m_cmbStretchMode->addItem(tr("Inverse GHS"), ImageBuffer::GHS_InverseGeneralizedHyperbolic);
    m_cmbStretchMode->addItem(tr("ArcSinh Stretch"), ImageBuffer::GHS_ArcSinh);
    m_cmbStretchMode->addItem(tr("Inverse ArcSinh"), ImageBuffer::GHS_InverseArcSinh);
    m_cmbStretchMode->setStyleSheet(m_cmbStarless->styleSheet());
    sg->addWidget(m_cmbStretchMode, 0, 1, 1, 2);

    // Color Mode
    sg->addWidget(new QLabel(tr("Color Method:")), 1, 0);
    m_cmbColorMode = new QComboBox();
    m_cmbColorMode->addItem(tr("RGB (Independent)"), ImageBuffer::GHS_Independent);
    m_cmbColorMode->addItem(tr("Human Weighted Luminance"), ImageBuffer::GHS_WeightedLuminance);
    m_cmbColorMode->addItem(tr("Even Weighted Luminance"), ImageBuffer::GHS_EvenWeightedLuminance);
    m_cmbColorMode->addItem(tr("Saturation"), ImageBuffer::GHS_Saturation);
    m_cmbColorMode->setStyleSheet(m_cmbStarless->styleSheet());
    sg->addWidget(m_cmbColorMode, 1, 1, 1, 2);

    // Clip / Blending Mode
    sg->addWidget(new QLabel(tr("Color Blending:")), 2, 0);
    m_cmbClipMode = new QComboBox();
    m_cmbClipMode->addItem(tr("Clip"), ImageBuffer::GHS_Clip);
    m_cmbClipMode->addItem(tr("Rescale"), ImageBuffer::GHS_Rescale);
    m_cmbClipMode->addItem(tr("RGB Blend"), ImageBuffer::GHS_ClipRGBBlend);
    m_cmbClipMode->addItem(tr("Global Rescale"), ImageBuffer::GHS_RescaleGlobal);
    m_cmbClipMode->setStyleSheet(m_cmbStarless->styleSheet());
    sg->addWidget(m_cmbClipMode, 2, 1, 1, 2);

    // Helper macro for creating slider row
    auto addSlider = [&](QGridLayout* g, int row, const QString& label, QSlider*& slider, QDoubleSpinBox*& spin, double min, double max, double step, double def) {
        g->addWidget(new QLabel(label), row, 0);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min * 100, max * 100);
        slider->setValue(def * 100);
        spin = new QDoubleSpinBox();
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setValue(def);
        spin->setDecimals(3);
        g->addWidget(slider, row, 1);
        g->addWidget(spin, row, 2);

        connect(slider, &QSlider::valueChanged, this, [spin, this](int v){ 
            spin->blockSignals(true); spin->setValue(v / 100.0); spin->blockSignals(false); 
            onUpdatePreview();
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [slider, this](double v){
            slider->blockSignals(true); slider->setValue(std::round(v * 100)); slider->blockSignals(false); 
            onUpdatePreview();
        });
    };

    addSlider(sg, 3, tr("Stretch Factor (D):"), m_sliderD, m_spinD, 0.0, 10.0, 0.01, 0.0);
    addSlider(sg, 4, tr("Local Intensity (B):"), m_sliderB, m_spinB, 0.0, 15.0, 0.01, 0.0);
    addSlider(sg, 5, tr("Symmetry Point (SP):"), m_sliderSP, m_spinSP, 0.0, 1.0, 0.001, 0.0);
    
    ctrlLayout->addLayout(grid);
    ctrlLayout->addWidget(stretchGroup);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnApply = new QPushButton(tr("Apply"));
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));
    
    btnLayout->addStretch();
    btnLayout->addWidget(btnApply);
    btnLayout->addWidget(btnCancel);
    ctrlLayout->addLayout(btnLayout);
    
    mainLayout->addLayout(ctrlLayout, 1);

    // Right: Preview
    QVBoxLayout* previewLayout = new QVBoxLayout();
    
    // Preview Toolbar
    QHBoxLayout* pToolbar = new QHBoxLayout();
    pToolbar->addWidget(new QLabel(tr("Preview:")));
    m_btnFit = new QPushButton(tr("Fit"));
    // Make them small
    m_btnFit->setFixedWidth(40);
    pToolbar->addWidget(m_btnFit);
    pToolbar->addStretch();
    previewLayout->addLayout(pToolbar);

    m_previewViewer = new ImageViewer(this);
    m_previewViewer->setProperty("isPreview", true);
    m_previewViewer->setMinimumSize(400, 400);
    previewLayout->addWidget(m_previewViewer);
    
    mainLayout->addLayout(previewLayout, 3);
    
    
    // Connect signals
    connect(btnApply, &QPushButton::clicked, this, &StarRecompositionDialog::onApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    connect(m_btnFit, &QPushButton::clicked, m_previewViewer, &ImageViewer::fitToWindow);

    connect(m_cmbStarless, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
    connect(m_cmbStars, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
    
    connect(m_cmbStretchMode, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
    connect(m_cmbColorMode, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
    connect(m_cmbClipMode, &QComboBox::currentIndexChanged, this, [this](int){ onUpdatePreview(); });
}

void StarRecompositionDialog::populateCombos() {
    // Store current selections before clearing
    ImageViewer* currSll = (ImageViewer*)m_cmbStarless->currentData().value<void*>();
    ImageViewer* currStr = (ImageViewer*)m_cmbStars->currentData().value<void*>();

    m_cmbStarless->blockSignals(true);
    m_cmbStars->blockSignals(true);

    m_cmbStarless->clear();
    m_cmbStars->clear();
    
    MainWindowCallbacks* mw = getCallbacks();
    if (!mw) {
        m_cmbStarless->blockSignals(false);
        m_cmbStars->blockSignals(false);
        return;
    }
    
    int sllIdx = -1;
    int strIdx = -1;
    
    QList<ImageViewer*> viewers = mw->getCurrentViewer()->window()->findChildren<ImageViewer*>();
    for (ImageViewer* v : viewers) {
        if (v == m_previewViewer) continue; // Skip our own preview window
        QString title = v->windowTitle();
        if (title.isEmpty()) continue;
        
        m_cmbStarless->addItem(title, QVariant::fromValue((void*)v));
        m_cmbStars->addItem(title, QVariant::fromValue((void*)v));
        
        if (v == currSll) sllIdx = m_cmbStarless->count() - 1;
        if (v == currStr) strIdx = m_cmbStars->count() - 1;
    }
    
    // Restore selections if the viewers still exist
    if (sllIdx >= 0) m_cmbStarless->setCurrentIndex(sllIdx);
    if (strIdx >= 0) m_cmbStars->setCurrentIndex(strIdx);
    
    m_cmbStarless->blockSignals(false);
    m_cmbStars->blockSignals(false);
}

void StarRecompositionDialog::onRefreshViews() {
    populateCombos();
}

void StarRecompositionDialog::onUpdatePreview() {
    if (m_initializing) return;
    
    ImageViewer* starlessViewer = (ImageViewer*)m_cmbStarless->currentData().value<void*>();
    ImageViewer* starsViewer = (ImageViewer*)m_cmbStars->currentData().value<void*>();
    
    if (!starlessViewer || !starsViewer) return;
    
    // Validate buffers first
    if (starlessViewer->getBuffer().width() == 0 || starsViewer->getBuffer().width() == 0) return;
    
    QImage qSll = starlessViewer->getCurrentDisplayImage();
    QImage qStr = starsViewer->getCurrentDisplayImage();
    
    if (qSll.isNull() || qStr.isNull()) return;
    if (qSll.width() <= 0 || qSll.height() <= 0 || qStr.width() <= 0 || qStr.height() <= 0) return;

    // Ensure sizes match logic (resize stars to starless)
    if (qStr.size() != qSll.size()) {
       qStr = qStr.scaled(qSll.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
       if (qStr.isNull()) return;  // Check after scaling
    }
    
    int qw = qSll.width();
    int qh = qSll.height();
    
    // Create temp buffers from display images
    ImageBuffer bufSll, bufStr;
    bufSll.setData(qw, qh, 3, {}); 
    bufStr.setData(qw, qh, 3, {});
    
    qSll = qSll.convertToFormat(QImage::Format_RGB888);
    qStr = qStr.convertToFormat(QImage::Format_RGB888);
    
    float* fSll = bufSll.data().data();
    float* fStr = bufStr.data().data();
    
    // Use scanLine to respect padding/stride
    for (int y = 0; y < qh; ++y) {
        const uchar* lineS = qSll.constScanLine(y);
        const uchar* lineT = qStr.constScanLine(y);
        for (int x = 0; x < qw; ++x) {
            size_t idx = (static_cast<size_t>(y) * qw + x) * 3;
            fSll[idx + 0] = lineS[x*3+0] / 255.0f;
            fSll[idx + 1] = lineS[x*3+1] / 255.0f;
            fSll[idx + 2] = lineS[x*3+2] / 255.0f;
            
            fStr[idx + 0] = lineT[x*3+0] / 255.0f;
            fStr[idx + 1] = lineT[x*3+1] / 255.0f;
            fStr[idx + 2] = lineT[x*3+2] / 255.0f;
        }
    }
    
    // Run Runner logic
    ImageBuffer result;
    StarRecompositionParams params;
    
    // Build GHS Params from UI
    ImageBuffer::GHSParams ghs;
    ghs.mode = (ImageBuffer::GHSMode)m_cmbStretchMode->currentData().toInt();
    ghs.colorMode = (ImageBuffer::GHSColorMode)m_cmbColorMode->currentData().toInt();
    ghs.clipMode = (ImageBuffer::GHSClipMode)m_cmbClipMode->currentData().toInt();
    ghs.D = m_spinD->value();
    ghs.B = m_spinB->value();
    ghs.SP = m_spinSP->value();
    
    // Inverse flags
    ghs.inverse = (ghs.mode == ImageBuffer::GHS_InverseGeneralizedHyperbolic || ghs.mode == ImageBuffer::GHS_InverseArcSinh);
    
    // We only process stars, not strictly log scale for this specific blend UI
    ghs.applyLog = false;
    
    params.ghs = ghs;
    
    QString err;
    if (m_runner.run(bufSll, bufStr, result, params, &err)) {
        // Set result to preview viewer
        // Important: Force Linear display state so we see the exact output of the blend (0-1)
        // without auto-stretch interfering.
        bool firstPreview = m_previewViewer->getBuffer().width() == 0;
        m_previewViewer->setBuffer(result, "Preview", true); 
        m_previewViewer->setDisplayState(ImageBuffer::Display_Linear, false);
        // Only fit-to-window on the first preview; subsequent slider changes preserve zoom
        if (firstPreview) {
            m_previewViewer->fitToWindow();
        }
    }
}

void StarRecompositionDialog::onApply() {
    ImageViewer* starlessViewer = (ImageViewer*)m_cmbStarless->currentData().value<void*>();
    ImageViewer* starsViewer = (ImageViewer*)m_cmbStars->currentData().value<void*>();
    
    if (!starlessViewer || !starsViewer) {
        QMessageBox::warning(this, tr("No Image"), tr("Please select both Starless and Stars-Only views."));
        return;
    }
    
    // Validate buffers
    if (starlessViewer->getBuffer().width() == 0 || starsViewer->getBuffer().width() == 0) {
        QMessageBox::warning(this, tr("Invalid Images"), tr("Selected views contain invalid image data."));
        return;
    }
    
    StarRecompositionParams params;
    ImageBuffer::GHSParams ghs;
    ghs.mode = (ImageBuffer::GHSMode)m_cmbStretchMode->currentData().toInt();
    ghs.colorMode = (ImageBuffer::GHSColorMode)m_cmbColorMode->currentData().toInt();
    ghs.clipMode = (ImageBuffer::GHSClipMode)m_cmbClipMode->currentData().toInt();
    ghs.D = m_spinD->value();
    ghs.B = m_spinB->value();
    ghs.SP = m_spinSP->value();
    ghs.inverse = (ghs.mode == ImageBuffer::GHS_InverseGeneralizedHyperbolic || ghs.mode == ImageBuffer::GHS_InverseArcSinh);
    ghs.applyLog = false;
    
    params.ghs = ghs;
    
    ImageBuffer result;
    QString err;
    if (m_runner.run(starlessViewer->getBuffer(), starsViewer->getBuffer(), result, params, &err)) {
        // Create a new image view for the result instead of modifying the starless image
        MainWindowCallbacks* mw = getCallbacks();
        if (mw) {
            QString newName = m_cmbStarless->currentText() + "_recomposed";
            mw->createResultWindow(result, newName);
        }
        accept();
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to process image: %1").arg(err));
    }
}

bool StarRecompositionDialog::isUsingViewer(ImageViewer* v) const {
    if (!v) return false;
    ImageViewer* sll = m_cmbStarless ? (ImageViewer*)m_cmbStarless->currentData().value<void*>() : nullptr;
    ImageViewer* str = m_cmbStars ? (ImageViewer*)m_cmbStars->currentData().value<void*>() : nullptr;
    return (v == sll || v == str);
}
