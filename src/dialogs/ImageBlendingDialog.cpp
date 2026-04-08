// =============================================================================
// ImageBlendingDialog.cpp
// Implementation of the image blending dialog. Supports multiple blend modes,
// range masking, per-channel blending for mono-on-RGB, and a live preview
// with configurable resolution.
// =============================================================================

#include "ImageBlendingDialog.h"
#include "MainWindowCallbacks.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QMdiSubWindow>
#include <QGroupBox>
#include <QPushButton>
#include <QTimer>

// =============================================================================
// Construction
// =============================================================================

ImageBlendingDialog::ImageBlendingDialog(QWidget* parent)
    : DialogBase(parent, tr("Image Blending"), 1000, 600)
{
    setModal(true);
    createUI();
    populateCombos();
    m_initializing = false;
    m_firstPreview = true;
}

// =============================================================================
// UI construction
// =============================================================================

void ImageBlendingDialog::createUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // -- Left panel: controls -------------------------------------------------
    QVBoxLayout* ctrlLayout = new QVBoxLayout();
    ctrlLayout->setSpacing(10);

    // -- 1. Image selection group ---------------------------------------------
    QGroupBox*   imgGroup = new QGroupBox(tr("Selection"));
    QGridLayout* iGrid    = new QGridLayout(imgGroup);

    iGrid->addWidget(new QLabel(tr("Base Image:")), 0, 0);
    m_cmbBase = new QComboBox();
    iGrid->addWidget(m_cmbBase, 0, 1);

    iGrid->addWidget(new QLabel(tr("Top Image:")), 1, 0);
    m_cmbTop = new QComboBox();
    iGrid->addWidget(m_cmbTop, 1, 1);

    m_lblTargetChannel = new QLabel(tr("Channel (Mono Top):"));
    m_cmbTargetChannel = new QComboBox();
    m_cmbTargetChannel->addItem(tr("Red"),       0);
    m_cmbTargetChannel->addItem(tr("Green"),     1);
    m_cmbTargetChannel->addItem(tr("Blue"),      2);
    m_cmbTargetChannel->addItem(tr("All / RGB"), 3);
    m_cmbTargetChannel->setCurrentIndex(3);
    iGrid->addWidget(m_lblTargetChannel, 2, 0);
    iGrid->addWidget(m_cmbTargetChannel, 2, 1);

    ctrlLayout->addWidget(imgGroup);

    // -- 2. Blending settings group -------------------------------------------
    QGroupBox*   blendGroup = new QGroupBox(tr("Blending"));
    QGridLayout* bGrid      = new QGridLayout(blendGroup);

    bGrid->addWidget(new QLabel(tr("Mode:")), 0, 0);
    m_cmbMode = new QComboBox();
    m_cmbMode->addItem(tr("Normal"),     ImageBlendingParams::Normal);
    m_cmbMode->addItem(tr("Multiply"),   ImageBlendingParams::Multiply);
    m_cmbMode->addItem(tr("Screen"),     ImageBlendingParams::Screen);
    m_cmbMode->addItem(tr("Overlay"),    ImageBlendingParams::Overlay);
    m_cmbMode->addItem(tr("Add"),        ImageBlendingParams::Add);
    m_cmbMode->addItem(tr("Subtract"),   ImageBlendingParams::Subtract);
    m_cmbMode->addItem(tr("Difference"), ImageBlendingParams::Difference);
    m_cmbMode->addItem(tr("Soft Light"), ImageBlendingParams::SoftLight);
    m_cmbMode->addItem(tr("Hard Light"), ImageBlendingParams::HardLight);
    bGrid->addWidget(m_cmbMode, 0, 1);

    bGrid->addWidget(new QLabel(tr("Opacity:")), 1, 0);
    m_sldOpacity = new QSlider(Qt::Horizontal);
    m_sldOpacity->setRange(0, 100);
    m_sldOpacity->setValue(100);
    m_lblOpacity = new QLabel("100%");
    bGrid->addWidget(m_sldOpacity, 1, 1);
    bGrid->addWidget(m_lblOpacity, 1, 2);

    ctrlLayout->addWidget(blendGroup);

    // -- 3. Range mask group --------------------------------------------------
    QGroupBox*   rangeGroup = new QGroupBox(tr("Range Mask"));
    QGridLayout* rGrid      = new QGridLayout(rangeGroup);

    rGrid->addWidget(new QLabel(tr("Low Range:")), 0, 0);
    m_sldLow = new QSlider(Qt::Horizontal);
    m_sldLow->setRange(0, 1000);
    m_sldLow->setValue(0);
    m_lblLow = new QLabel("0.00");
    rGrid->addWidget(m_sldLow, 0, 1);
    rGrid->addWidget(m_lblLow, 0, 2);

    rGrid->addWidget(new QLabel(tr("High Range:")), 1, 0);
    m_sldHigh = new QSlider(Qt::Horizontal);
    m_sldHigh->setRange(0, 1000);
    m_sldHigh->setValue(1000);
    m_lblHigh = new QLabel("1.00");
    rGrid->addWidget(m_sldHigh, 1, 1);
    rGrid->addWidget(m_lblHigh, 1, 2);

    rGrid->addWidget(new QLabel(tr("Feathering:")), 2, 0);
    m_sldFeather = new QSlider(Qt::Horizontal);
    m_sldFeather->setRange(0, 500);
    m_sldFeather->setValue(0);
    m_lblFeather = new QLabel("0.00");
    rGrid->addWidget(m_sldFeather, 2, 1);
    rGrid->addWidget(m_lblFeather, 2, 2);

    ctrlLayout->addWidget(rangeGroup);

    // -- 4. Preview options group ---------------------------------------------
    QGroupBox*   previewGroup = new QGroupBox(tr("Preview Options"));
    QVBoxLayout* pLayout      = new QVBoxLayout(previewGroup);
    m_chkShowPreview = new QCheckBox(tr("Show Preview"));
    m_chkShowPreview->setChecked(true);
    m_chkHighRes = new QCheckBox(tr("High Res (2048px)"));
    m_chkHighRes->setChecked(true);
    pLayout->addWidget(m_chkShowPreview);
    pLayout->addWidget(m_chkHighRes);
    ctrlLayout->addWidget(previewGroup);

    ctrlLayout->addStretch();

    // -- Action buttons -------------------------------------------------------
    QHBoxLayout* btnLayout  = new QHBoxLayout();
    QPushButton* btnApply   = new QPushButton(tr("Apply"));
    QPushButton* btnCancel  = new QPushButton(tr("Cancel"));
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnApply);
    ctrlLayout->addLayout(btnLayout);

    mainLayout->addLayout(ctrlLayout, 0);

    // -- Right panel: preview viewer ------------------------------------------
    m_previewViewer = new ImageViewer(this);
    m_previewViewer->setProperty("isPreview", true);
    m_previewViewer->setMinimumSize(400, 400);
    mainLayout->addWidget(m_previewViewer, 1);

    // -- ComboBox styling -----------------------------------------------------
    QString cmbStyle =
        "QComboBox { color: white; background-color: #2a2a2a; "
        "  border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox QAbstractItemView { background-color: #2a2a2a; "
        "  color: white; selection-background-color: #4a7ba7; }";
    m_cmbBase->setStyleSheet(cmbStyle);
    m_cmbTop->setStyleSheet(cmbStyle);
    m_cmbMode->setStyleSheet(cmbStyle);
    m_cmbTargetChannel->setStyleSheet(cmbStyle);

    // =========================================================================
    // Signal/slot connections
    // =========================================================================

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnApply,  &QPushButton::clicked,
            this, &ImageBlendingDialog::onApply);

    connect(m_cmbBase,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageBlendingDialog::updatePreview);
    connect(m_cmbTop,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageBlendingDialog::onTopImageChanged);
    connect(m_cmbMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageBlendingDialog::updatePreview);
    connect(m_cmbTargetChannel,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageBlendingDialog::updatePreview);

    // Update labels and preview whenever any slider changes.
    auto updateLabels = [this]() {
        m_lblOpacity->setText(
            QString("%1%").arg(m_sldOpacity->value()));
        m_lblLow->setText(
            QString::number(m_sldLow->value() / 1000.0, 'f', 2));
        m_lblHigh->setText(
            QString::number(m_sldHigh->value() / 1000.0, 'f', 2));
        m_lblFeather->setText(
            QString::number(m_sldFeather->value() / 1000.0, 'f', 2));
        updatePreview();
    };

    connect(m_sldOpacity, &QSlider::valueChanged, updateLabels);
    connect(m_sldLow,     &QSlider::valueChanged, updateLabels);
    connect(m_sldHigh,    &QSlider::valueChanged, updateLabels);
    connect(m_sldFeather, &QSlider::valueChanged, updateLabels);

    connect(m_chkShowPreview, &QCheckBox::toggled,
            this, &ImageBlendingDialog::updatePreview);
    connect(m_chkHighRes,     &QCheckBox::toggled,
            this, &ImageBlendingDialog::updatePreview);
}

// =============================================================================
// Populate combo boxes with available viewers
// =============================================================================

void ImageBlendingDialog::populateCombos()
{
    m_cmbBase->clear();
    m_cmbTop->clear();

    MainWindowCallbacks* mw = getCallbacks();
    if (!mw) return;

    ImageViewer* active = mw->getCurrentViewer();
    if (!active) return;

    QList<ImageViewer*> viewers =
        active->window()->findChildren<ImageViewer*>();

    for (auto* v : viewers) {
        // Skip the embedded preview viewer.
        if (v->property("isPreview").toBool()) continue;
        if (!v->getBuffer().isValid()) continue;

        QIcon icon = QIcon(":/images/Logo.png");
        m_cmbBase->addItem(icon, v->windowTitle(),
                           QVariant::fromValue((void*)v));
        m_cmbTop->addItem(icon, v->windowTitle(),
                          QVariant::fromValue((void*)v));
    }
}

// =============================================================================
// Top image change handler
// =============================================================================

void ImageBlendingDialog::onTopImageChanged()
{
    ImageViewer* v =
        (ImageViewer*)m_cmbTop->currentData().value<void*>();
    bool isMono = v && v->getBuffer().channels() == 1;

    // Show the channel selector only when blending a mono image onto RGB.
    m_lblTargetChannel->setVisible(isMono);
    m_cmbTargetChannel->setVisible(isMono);
    updatePreview();
}

// =============================================================================
// Preview update
// =============================================================================

void ImageBlendingDialog::updatePreview()
{
    if (m_initializing) return;

    ImageViewer* baseV =
        (ImageViewer*)m_cmbBase->currentData().value<void*>();
    ImageViewer* topV =
        (ImageViewer*)m_cmbTop->currentData().value<void*>();
    if (!baseV || !topV) return;

    const ImageBuffer& base = baseV->getBuffer();
    const ImageBuffer& top  = topV->getBuffer();

    // Validate dimension compatibility.
    if (base.width() != top.width() || base.height() != top.height()) {
        m_previewViewer->setBuffer(ImageBuffer(),
                                   tr("Dimensions Mismatch"));
        return;
    }

    // Gather current blending parameters.
    ImageBlendingParams params;
    params.mode          = (ImageBlendingParams::BlendMode)
                           m_cmbMode->currentIndex();
    params.opacity       = m_sldOpacity->value() / 100.0f;
    params.lowRange      = m_sldLow->value()     / 1000.0f;
    params.highRange     = m_sldHigh->value()    / 1000.0f;
    params.feather       = m_sldFeather->value() / 1000.0f;
    params.targetChannel = m_cmbTargetChannel->currentData().toInt();

    // If preview is disabled, show the base image unblended.
    if (!m_chkShowPreview->isChecked()) {
        bool needsFit = (m_firstPreview
                         || base.width()  != m_lastPreviewWidth
                         || base.height() != m_lastPreviewHeight);
        m_previewViewer->setBuffer(base, "Base Image (No Preview)",
                                   !needsFit);
        if (needsFit) {
            m_firstPreview      = false;
            m_lastPreviewWidth  = base.width();
            m_lastPreviewHeight = base.height();
        }
        return;
    }

    // -- Downsample for preview performance -----------------------------------
    const int maxDim = m_chkHighRes->isChecked() ? 2048 : 1024;
    const int w = base.width();
    const int h = base.height();
    float scale = 1.0f;

    ImageBuffer baseSmall, topSmall;
    if (w > maxDim || h > maxDim) {
        scale    = std::min((float)maxDim / w, (float)maxDim / h);
        int nw   = std::max(1, (int)(w * scale));
        int nh   = std::max(1, (int)(h * scale));

        // Downsample base image (nearest-neighbour for speed).
        int nc = base.channels();
        baseSmall.resize(nw, nh, nc);
        const float* sData = base.data().data();
        float*       dData = baseSmall.data().data();
#pragma omp parallel for
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                int sx = std::clamp((int)(x / scale), 0, w - 1);
                int sy = std::clamp((int)(y / scale), 0, h - 1);
                for (int c = 0; c < nc; ++c)
                    dData[(y * nw + x) * nc + c] =
                        sData[(sy * w + sx) * nc + c];
            }
        }

        // Downsample top image.
        nc = top.channels();
        topSmall.resize(nw, nh, nc);
        sData = top.data().data();
        dData = topSmall.data().data();
#pragma omp parallel for
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                int sx = std::clamp((int)(x / scale), 0, w - 1);
                int sy = std::clamp((int)(y / scale), 0, h - 1);
                for (int c = 0; c < nc; ++c)
                    dData[(y * nw + x) * nc + c] =
                        sData[(sy * w + sx) * nc + c];
            }
        }
    } else {
        baseSmall = base;
        topSmall  = top;
    }

    // -- Run the blend and display the result ---------------------------------
    ImageBlendingRunner runner;
    ImageBuffer result;
    if (runner.run(baseSmall, topSmall, result, params)) {
        bool needsFit = (m_firstPreview
                         || result.width()  != m_lastPreviewWidth
                         || result.height() != m_lastPreviewHeight);
        m_previewViewer->setBuffer(result, "Preview", !needsFit);
        if (needsFit) {
            m_firstPreview      = false;
            m_lastPreviewWidth  = result.width();
            m_lastPreviewHeight = result.height();
        }
    }
}

// =============================================================================
// Show event
// =============================================================================

void ImageBlendingDialog::showEvent(QShowEvent* event)
{
    DialogBase::showEvent(event);

    // Ensure the preview viewer fits its content after layout is complete.
    if (m_previewViewer->getBuffer().isValid()) {
        m_previewViewer->fitToWindow();
        m_firstPreview      = false;
        m_lastPreviewWidth  = m_previewViewer->getBuffer().width();
        m_lastPreviewHeight = m_previewViewer->getBuffer().height();
    }
}

// =============================================================================
// Apply blending to a new result window
// =============================================================================

void ImageBlendingDialog::onApply()
{
    ImageViewer* baseV =
        (ImageViewer*)m_cmbBase->currentData().value<void*>();
    ImageViewer* topV =
        (ImageViewer*)m_cmbTop->currentData().value<void*>();
    if (!baseV || !topV) return;

    ImageBlendingParams params;
    params.mode          = (ImageBlendingParams::BlendMode)
                           m_cmbMode->currentIndex();
    params.opacity       = m_sldOpacity->value() / 100.0f;
    params.lowRange      = m_sldLow->value()     / 1000.0f;
    params.highRange     = m_sldHigh->value()    / 1000.0f;
    params.feather       = m_sldFeather->value() / 1000.0f;
    params.targetChannel = m_cmbTargetChannel->currentData().toInt();

    ImageBlendingRunner runner;
    ImageBuffer result;
    QString err;

    if (runner.run(baseV->getBuffer(), topV->getBuffer(),
                   result, params, &err)) {
        if (MainWindowCallbacks* mw = getCallbacks()) {
            // Inherit metadata from base image and merge history from top image
            result.setMetadata(baseV->getBuffer().metadata());
            result.mergeHistory(topV->getBuffer().metadata());
            result.addHistoryAction(tr("Image Blended with %1").arg(topV->windowTitle()));

            // Propagate the base image's display settings to the result.
            auto  mode   = static_cast<int>(baseV->getDisplayMode());
            float median = baseV->getAutoStretchMedian();
            bool  linked = baseV->isDisplayLinked();

            const QString blendTitle =
                MainWindowCallbacks::buildChildTitle(
                    baseV->windowTitle(), "_blended");
            mw->createResultWindow(result, blendTitle,
                                   mode, median, linked);
            mw->logMessage(
                tr("Image Blending completed: %1").arg(blendTitle),
                1, true);
        }
        accept();
    } else {
        QMessageBox::critical(this, tr("Error"), err);
    }
}

// =============================================================================
// Viewer binding
// =============================================================================

void ImageBlendingDialog::setViewer(ImageViewer* v)
{
    populateCombos();
    if (!v) return;

    // Select the specified viewer as the base image.
    for (int i = 0; i < m_cmbBase->count(); ++i) {
        if (m_cmbBase->itemData(i).value<void*>() == (void*)v) {
            m_cmbBase->setCurrentIndex(i);
            break;
        }
    }
}