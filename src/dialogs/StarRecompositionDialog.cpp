// =============================================================================
// StarRecompositionDialog.cpp
// Implements the star recomposition dialog, allowing the user to select a
// starless and a stars-only view, adjust GHS stretch parameters, preview
// the blended result, and apply it as a new image window.
// =============================================================================

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

// =============================================================================
// Shared combo-box stylesheet applied to all drop-down selectors in this dialog
// =============================================================================
static const char* kComboStyle =
    "QComboBox { color: white; background-color: #2a2a2a; "
    "  border: 1px solid #555; padding: 2px; border-radius: 3px; }"
    "QComboBox:focus { border: 2px solid #4a9eff; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
    "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
    "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
    "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
    "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }";

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
StarRecompositionDialog::StarRecompositionDialog(QWidget* parent)
    : DialogBase(parent, tr("Star Recomposition"), 900, 500)
{
    setModal(true);
    createUI();
    populateCombos();
    setMinimumWidth(400);
    m_initializing = false;
}

// -----------------------------------------------------------------------------
// setViewer  --  Refreshes the combo boxes when the active viewer changes.
// -----------------------------------------------------------------------------
void StarRecompositionDialog::setViewer(ImageViewer* v)
{
    Q_UNUSED(v);
    populateCombos();
}

// -----------------------------------------------------------------------------
// createUI  --  Builds the complete dialog layout:
//   Left column  : source selectors, GHS controls, action buttons
//   Right column : live preview viewer
// -----------------------------------------------------------------------------
void StarRecompositionDialog::createUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    // =========================================================================
    // Left column: Controls
    // =========================================================================
    QVBoxLayout* ctrlLayout = new QVBoxLayout();
    QGridLayout* grid       = new QGridLayout();

    // --- Source selection ---
    grid->addWidget(new QLabel(tr("Starless View:")), 0, 0);
    m_cmbStarless = new QComboBox();
    m_cmbStarless->setStyleSheet(kComboStyle);
    grid->addWidget(m_cmbStarless, 0, 1);

    grid->addWidget(new QLabel(tr("Stars-Only View:")), 1, 0);
    m_cmbStars = new QComboBox();
    m_cmbStars->setStyleSheet(kComboStyle);
    grid->addWidget(m_cmbStars, 1, 1);

    // --- GHS stretch parameters group ---
    QGroupBox*   stretchGroup = new QGroupBox(tr("Stars Stretch Parameters"));
    QGridLayout* sg           = new QGridLayout(stretchGroup);

    // Stretch mode
    sg->addWidget(new QLabel(tr("Stretch Mode:")), 0, 0);
    m_cmbStretchMode = new QComboBox();
    m_cmbStretchMode->addItem(tr("Generalized Hyperbolic Stretch"),
                              ImageBuffer::GHS_GeneralizedHyperbolic);
    m_cmbStretchMode->addItem(tr("Inverse GHS"),
                              ImageBuffer::GHS_InverseGeneralizedHyperbolic);
    m_cmbStretchMode->addItem(tr("ArcSinh Stretch"),
                              ImageBuffer::GHS_ArcSinh);
    m_cmbStretchMode->addItem(tr("Inverse ArcSinh"),
                              ImageBuffer::GHS_InverseArcSinh);
    m_cmbStretchMode->setStyleSheet(kComboStyle);
    sg->addWidget(m_cmbStretchMode, 0, 1, 1, 2);

    // Color method
    sg->addWidget(new QLabel(tr("Color Method:")), 1, 0);
    m_cmbColorMode = new QComboBox();
    m_cmbColorMode->addItem(tr("RGB (Independent)"),         ImageBuffer::GHS_Independent);
    m_cmbColorMode->addItem(tr("Human Weighted Luminance"),  ImageBuffer::GHS_WeightedLuminance);
    m_cmbColorMode->addItem(tr("Even Weighted Luminance"),   ImageBuffer::GHS_EvenWeightedLuminance);
    m_cmbColorMode->addItem(tr("Saturation"),                ImageBuffer::GHS_Saturation);
    m_cmbColorMode->setStyleSheet(kComboStyle);
    sg->addWidget(m_cmbColorMode, 1, 1, 1, 2);

    // Color blending / clip mode
    sg->addWidget(new QLabel(tr("Color Blending:")), 2, 0);
    m_cmbClipMode = new QComboBox();
    m_cmbClipMode->addItem(tr("Clip"),            ImageBuffer::GHS_Clip);
    m_cmbClipMode->addItem(tr("Rescale"),         ImageBuffer::GHS_Rescale);
    m_cmbClipMode->addItem(tr("RGB Blend"),       ImageBuffer::GHS_ClipRGBBlend);
    m_cmbClipMode->addItem(tr("Global Rescale"),  ImageBuffer::GHS_RescaleGlobal);
    m_cmbClipMode->setStyleSheet(kComboStyle);
    sg->addWidget(m_cmbClipMode, 2, 1, 1, 2);

    // Helper lambda: creates a labelled slider + spin-box row and wires
    // bidirectional synchronization plus preview updates.
    auto addSlider = [&](QGridLayout* g, int row, const QString& label,
                         QSlider*& slider, QDoubleSpinBox*& spin,
                         double min, double max, double step, double def) {
        g->addWidget(new QLabel(label), row, 0);

        slider = new QSlider(Qt::Horizontal);
        slider->setRange(static_cast<int>(min * 100), static_cast<int>(max * 100));
        slider->setValue(static_cast<int>(def * 100));

        spin = new QDoubleSpinBox();
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setValue(def);
        spin->setDecimals(3);

        g->addWidget(slider, row, 1);
        g->addWidget(spin,   row, 2);

        // Slider -> spin box synchronization
        connect(slider, &QSlider::valueChanged, this, [spin, this](int v) {
            spin->blockSignals(true);
            spin->setValue(v / 100.0);
            spin->blockSignals(false);
            onUpdatePreview();
        });

        // Spin box -> slider synchronization
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [slider, this](double v) {
            slider->blockSignals(true);
            slider->setValue(static_cast<int>(std::round(v * 100)));
            slider->blockSignals(false);
            onUpdatePreview();
        });
    };

    addSlider(sg, 3, tr("Stretch Factor (D):"),  m_sliderD,  m_spinD,  0.0, 10.0, 0.01, 0.0);
    addSlider(sg, 4, tr("Local Intensity (B):"),  m_sliderB,  m_spinB,  0.0, 15.0, 0.01, 0.0);
    addSlider(sg, 5, tr("Symmetry Point (SP):"), m_sliderSP, m_spinSP, 0.0,  1.0, 0.001, 0.0);

    ctrlLayout->addLayout(grid);
    ctrlLayout->addWidget(stretchGroup);

    // --- Action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnApply  = new QPushButton(tr("Apply"));
    QPushButton* btnCancel = new QPushButton(tr("Cancel"));

    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnApply);
    ctrlLayout->addLayout(btnLayout);

    mainLayout->addLayout(ctrlLayout, 1);

    // =========================================================================
    // Right column: Preview
    // =========================================================================
    QVBoxLayout* previewLayout = new QVBoxLayout();

    // Preview toolbar
    QHBoxLayout* pToolbar = new QHBoxLayout();
    pToolbar->addWidget(new QLabel(tr("Preview:")));

    m_btnFit = new QPushButton(tr("Fit"));
    m_btnFit->setFixedWidth(40);
    pToolbar->addWidget(m_btnFit);
    pToolbar->addStretch();
    previewLayout->addLayout(pToolbar);

    // Preview image viewer
    m_previewViewer = new ImageViewer(this);
    m_previewViewer->setProperty("isPreview", true);
    m_previewViewer->setMinimumSize(400, 400);
    previewLayout->addWidget(m_previewViewer);

    mainLayout->addLayout(previewLayout, 3);

    // --- Signal connections ---
    connect(btnApply,  &QPushButton::clicked, this, &StarRecompositionDialog::onApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_btnFit, &QPushButton::clicked,
            m_previewViewer, &ImageViewer::fitToWindow);

    connect(m_cmbStarless, &QComboBox::currentIndexChanged,
            this, [this](int) { onUpdatePreview(); });
    connect(m_cmbStars, &QComboBox::currentIndexChanged,
            this, [this](int) { onUpdatePreview(); });

    connect(m_cmbStretchMode, &QComboBox::currentIndexChanged,
            this, [this](int) { onUpdatePreview(); });
    connect(m_cmbColorMode, &QComboBox::currentIndexChanged,
            this, [this](int) { onUpdatePreview(); });
    connect(m_cmbClipMode, &QComboBox::currentIndexChanged,
            this, [this](int) { onUpdatePreview(); });
}

// -----------------------------------------------------------------------------
// populateCombos  --  Scans all open ImageViewer instances and fills both
// combo boxes, preserving previous selections when possible.
// -----------------------------------------------------------------------------
void StarRecompositionDialog::populateCombos()
{
    // Remember current selections before clearing
    ImageViewer* currSll = static_cast<ImageViewer*>(
        m_cmbStarless->currentData().value<void*>());
    ImageViewer* currStr = static_cast<ImageViewer*>(
        m_cmbStars->currentData().value<void*>());

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

    QList<ImageViewer*> viewers =
        mw->getCurrentViewer()->window()->findChildren<ImageViewer*>();

    for (ImageViewer* v : viewers) {
        // Exclude our own preview viewer
        if (v == m_previewViewer) continue;

        QString title = v->windowTitle();
        if (title.isEmpty()) continue;

        m_cmbStarless->addItem(title, QVariant::fromValue(static_cast<void*>(v)));
        m_cmbStars->addItem(title,    QVariant::fromValue(static_cast<void*>(v)));

        if (v == currSll) sllIdx = m_cmbStarless->count() - 1;
        if (v == currStr) strIdx = m_cmbStars->count() - 1;
    }

    // Restore previous selections if the viewers still exist
    if (sllIdx >= 0) m_cmbStarless->setCurrentIndex(sllIdx);
    if (strIdx >= 0) m_cmbStars->setCurrentIndex(strIdx);

    m_cmbStarless->blockSignals(false);
    m_cmbStars->blockSignals(false);
}

// -----------------------------------------------------------------------------
// onRefreshViews  --  Slot to re-scan available viewers.
// -----------------------------------------------------------------------------
void StarRecompositionDialog::onRefreshViews()
{
    populateCombos();
}

// -----------------------------------------------------------------------------
// onUpdatePreview  --  Rebuilds the preview from display images.
// Converts QImages to float buffers, applies GHS stretch to the stars layer,
// and composites the result.
// -----------------------------------------------------------------------------
void StarRecompositionDialog::onUpdatePreview()
{
    if (m_initializing) return;

    ImageViewer* starlessViewer = static_cast<ImageViewer*>(
        m_cmbStarless->currentData().value<void*>());
    ImageViewer* starsViewer = static_cast<ImageViewer*>(
        m_cmbStars->currentData().value<void*>());

    if (!starlessViewer || !starsViewer) return;

    // Validate that both viewers have loaded image data
    if (starlessViewer->getBuffer().width() == 0 ||
        starsViewer->getBuffer().width() == 0) return;

    QImage qSll = starlessViewer->getCurrentDisplayImage();
    QImage qStr = starsViewer->getCurrentDisplayImage();

    if (qSll.isNull() || qStr.isNull()) return;
    if (qSll.width() <= 0 || qSll.height() <= 0 ||
        qStr.width() <= 0 || qStr.height() <= 0) return;

    // Resize the stars image to match the starless dimensions
    if (qStr.size() != qSll.size()) {
        qStr = qStr.scaled(qSll.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        if (qStr.isNull()) return;
    }

    int qw = qSll.width();
    int qh = qSll.height();

    // Create temporary float buffers from the display images
    ImageBuffer bufSll, bufStr;
    bufSll.setData(qw, qh, 3, {});
    bufStr.setData(qw, qh, 3, {});

    qSll = qSll.convertToFormat(QImage::Format_RGB888);
    qStr = qStr.convertToFormat(QImage::Format_RGB888);

    float* fSll = bufSll.data().data();
    float* fStr = bufStr.data().data();

    // Convert 8-bit scanlines to normalized [0,1] float data, respecting
    // QImage row padding via constScanLine().
    for (int y = 0; y < qh; ++y) {
        const uchar* lineS = qSll.constScanLine(y);
        const uchar* lineT = qStr.constScanLine(y);
        for (int x = 0; x < qw; ++x) {
            size_t idx = (static_cast<size_t>(y) * qw + x) * 3;

            fSll[idx + 0] = lineS[x * 3 + 0] / 255.0f;
            fSll[idx + 1] = lineS[x * 3 + 1] / 255.0f;
            fSll[idx + 2] = lineS[x * 3 + 2] / 255.0f;

            fStr[idx + 0] = lineT[x * 3 + 0] / 255.0f;
            fStr[idx + 1] = lineT[x * 3 + 1] / 255.0f;
            fStr[idx + 2] = lineT[x * 3 + 2] / 255.0f;
        }
    }

    // Build GHS parameters from the UI controls
    ImageBuffer::GHSParams ghs;
    ghs.mode      = static_cast<ImageBuffer::GHSMode>(
        m_cmbStretchMode->currentData().toInt());
    ghs.colorMode = static_cast<ImageBuffer::GHSColorMode>(
        m_cmbColorMode->currentData().toInt());
    ghs.clipMode  = static_cast<ImageBuffer::GHSClipMode>(
        m_cmbClipMode->currentData().toInt());
    ghs.D  = m_spinD->value();
    ghs.B  = m_spinB->value();
    ghs.SP = m_spinSP->value();

    // Determine inverse flag from the selected mode
    ghs.inverse = (ghs.mode == ImageBuffer::GHS_InverseGeneralizedHyperbolic ||
                   ghs.mode == ImageBuffer::GHS_InverseArcSinh);

    // Log scale is not used for the recomposition blend
    ghs.applyLog = false;

    StarRecompositionParams params;
    params.ghs = ghs;

    // Execute the recomposition
    ImageBuffer result;
    QString     err;
    if (m_runner.run(bufSll, bufStr, result, params, &err)) {
        // Force linear display so the preview shows the exact blend output
        // without auto-stretch interference
        bool firstPreview = (m_previewViewer->getBuffer().width() == 0);
        m_previewViewer->setBuffer(result, "Preview", true);
        m_previewViewer->setDisplayState(ImageBuffer::Display_Linear, false);

        // Only fit-to-window on the initial preview; subsequent updates
        // preserve the current zoom/pan state
        if (firstPreview) {
            m_previewViewer->fitToWindow();
        }
    }
}

// -----------------------------------------------------------------------------
// onApply  --  Runs the recomposition on full-resolution source buffers and
// creates a new result window.
// -----------------------------------------------------------------------------
void StarRecompositionDialog::onApply()
{
    ImageViewer* starlessViewer = static_cast<ImageViewer*>(
        m_cmbStarless->currentData().value<void*>());
    ImageViewer* starsViewer = static_cast<ImageViewer*>(
        m_cmbStars->currentData().value<void*>());

    if (!starlessViewer || !starsViewer) {
        QMessageBox::warning(this, tr("No Image"),
            tr("Please select both Starless and Stars-Only views."));
        return;
    }

    // Validate source buffers
    if (starlessViewer->getBuffer().width() == 0 ||
        starsViewer->getBuffer().width() == 0) {
        QMessageBox::warning(this, tr("Invalid Images"),
            tr("Selected views contain invalid image data."));
        return;
    }

    // Collect GHS parameters
    StarRecompositionParams params;
    ImageBuffer::GHSParams  ghs;
    ghs.mode      = static_cast<ImageBuffer::GHSMode>(
        m_cmbStretchMode->currentData().toInt());
    ghs.colorMode = static_cast<ImageBuffer::GHSColorMode>(
        m_cmbColorMode->currentData().toInt());
    ghs.clipMode  = static_cast<ImageBuffer::GHSClipMode>(
        m_cmbClipMode->currentData().toInt());
    ghs.D       = m_spinD->value();
    ghs.B       = m_spinB->value();
    ghs.SP      = m_spinSP->value();
    ghs.inverse = (ghs.mode == ImageBuffer::GHS_InverseGeneralizedHyperbolic ||
                   ghs.mode == ImageBuffer::GHS_InverseArcSinh);
    ghs.applyLog = false;
    params.ghs   = ghs;

    // Process at full resolution
    ImageBuffer result;
    QString     err;
    if (m_runner.run(starlessViewer->getBuffer(), starsViewer->getBuffer(),
                     result, params, &err)) {
        MainWindowCallbacks* mw = getCallbacks();
        if (mw) {
            // Inherit metadata from starless image and merge history from stars image
            result.setMetadata(starlessViewer->getBuffer().metadata());
            result.mergeHistory(starsViewer->getBuffer().metadata());
            result.addHistoryAction(tr("Star Recomposition from %1 and %2")
                                    .arg(m_cmbStarless->currentText())
                                    .arg(m_cmbStars->currentText()));

            QString newName = m_cmbStarless->currentText() + "_recomposed";
            mw->createResultWindow(result, newName);
            mw->logMessage(
                tr("Star Recomposition completed: %1").arg(newName), 1, true);
        }
        accept();
    } else {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to process image: %1").arg(err));
    }
}

// -----------------------------------------------------------------------------
// isUsingViewer  --  Returns true if the given viewer is currently selected
// in either the starless or stars-only combo box.
// -----------------------------------------------------------------------------
bool StarRecompositionDialog::isUsingViewer(ImageViewer* v) const
{
    if (!v) return false;

    ImageViewer* sll = m_cmbStarless
        ? static_cast<ImageViewer*>(m_cmbStarless->currentData().value<void*>())
        : nullptr;
    ImageViewer* str = m_cmbStars
        ? static_cast<ImageViewer*>(m_cmbStars->currentData().value<void*>())
        : nullptr;

    return (v == sll || v == str);
}