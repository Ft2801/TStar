#include "NarrowbandNormalizationDialog.h"
#include "../ImageViewer.h"
#include "../MainWindowCallbacks.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../io/FitsLoader.h"
#include "../io/XISFReader.h"
#include "../io/SimpleTiffReader.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QApplication>
#include <QSplitter>
#include <cmath>

namespace {
// Universal image loader from file path → ImageBuffer
bool loadImageFromPath(const QString& path, ImageBuffer& buffer, QString& error) {
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();

    if (ext == "fits" || ext == "fit") {
        return FitsLoader::load(path, buffer, &error);
    } else if (ext == "xisf") {
        return XISFReader::read(path, buffer, &error);
    } else if (ext == "tif" || ext == "tiff") {
        int w, h, c;
        std::vector<float> data;
        if (!SimpleTiffReader::readFloat32(path, w, h, c, data, &error))
            return false;
        buffer.setData(w, h, c, data);
        return true;
    } else {
        QImage img(path);
        if (img.isNull()) { error = "Failed to load image."; return false; }
        img = img.convertToFormat(QImage::Format_RGB888);
        int w = img.width(), h = img.height();
        std::vector<float> data(w * h * 3);
        for (int y = 0; y < h; ++y) {
            const uchar* line = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                data[(y * w + x) * 3 + 0] = line[x * 3 + 0] / 255.0f;
                data[(y * w + x) * 3 + 1] = line[x * 3 + 1] / 255.0f;
                data[(y * w + x) * 3 + 2] = line[x * 3 + 2] / 255.0f;
            }
        }
        buffer.setData(w, h, 3, data);
        return true;
    }
}
} // anonymous namespace

// ============================================================================
// Constructor / Destructor
// ============================================================================

NarrowbandNormalizationDialog::NarrowbandNormalizationDialog(QWidget* parent)
    : DialogBase(parent, tr("Narrowband Normalization"))
{
    m_mainWindow = dynamic_cast<MainWindowCallbacks*>(parent);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_debounce, &QTimer::timeout, this, &NarrowbandNormalizationDialog::onPreview);

    buildUI();
    resize(1100, 600);
}

NarrowbandNormalizationDialog::~NarrowbandNormalizationDialog() = default;

// ============================================================================
// Public API
// ============================================================================

void NarrowbandNormalizationDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

void NarrowbandNormalizationDialog::refreshImageList() {
    // Nothing specific to refresh — channels are loaded individually
}

// ============================================================================
// Slider + SpinBox Row Helper
// ============================================================================

NarrowbandNormalizationDialog::SliderSpinRow
NarrowbandNormalizationDialog::createSliderSpinRow(double lo, double hi, double step,
                                                    double val, int decimals) {
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    auto* sp = new QDoubleSpinBox(this);
    sp->setRange(lo, hi);
    sp->setDecimals(decimals);
    sp->setSingleStep(step);
    sp->setValue(val);

    auto* s = new QSlider(Qt::Horizontal, this);
    int imin = (int)std::round(lo / step);
    int imax = (int)std::round(hi / step);
    s->setRange(imin, imax);
    s->setValue((int)std::round(val / step));

    // Sync both ways
    double stepCopy = step;
    connect(s, &QSlider::valueChanged, this, [sp, stepCopy](int iv) {
        sp->blockSignals(true);
        sp->setValue(iv * stepCopy);
        sp->blockSignals(false);
    });
    connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [s, stepCopy](double v) {
        s->blockSignals(true);
        s->setValue((int)std::round(v / stepCopy));
        s->blockSignals(false);
    });

    lay->addWidget(s, 1);
    lay->addWidget(sp, 0);

    return { w, sp, s };
}

// ============================================================================
// UI Construction
// ============================================================================

void NarrowbandNormalizationDialog::buildUI() {
    initWidgets();
    connectSignals();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* root = new QHBoxLayout();
    root->setSpacing(10);
    outer->addLayout(root, 1);

    // ---- LEFT PANEL (scrollable) ----
    auto* leftScroll = new QScrollArea(this);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* leftHost = new QWidget(this);
    leftScroll->setWidget(leftHost);

    auto* leftRow = new QHBoxLayout(leftHost);
    leftRow->setContentsMargins(0, 0, 0, 0);
    leftRow->setSpacing(10);

    auto* colA = new QVBoxLayout();
    colA->setSpacing(8);
    auto* colB = new QVBoxLayout();
    colB->setSpacing(8);

    // Column A: Import buttons + load channels
    auto* grpImport = new QGroupBox(tr("Import mapped RGB view"), this);
    auto* impLayout = new QVBoxLayout(grpImport);
    impLayout->setSpacing(6);
    for (auto* btn : { m_btnImpSHO, m_btnImpHSO, m_btnImpHOS, m_btnImpHOO }) {
        btn->setMinimumHeight(28);
        impLayout->addWidget(btn);
    }
    colA->addWidget(grpImport);

    colA->addWidget(new QLabel("<b>" + tr("Load channels") + "</b>"));

    auto* grpNB = new QGroupBox(tr("Narrowband channels"), this);
    auto* nbLayout = new QVBoxLayout(grpNB);
    nbLayout->setSpacing(4);
    nbLayout->addWidget(m_btnHa);
    nbLayout->addWidget(m_lblHa);
    nbLayout->addWidget(m_btnOIII);
    nbLayout->addWidget(m_lblOIII);
    nbLayout->addWidget(m_btnSII);
    nbLayout->addWidget(m_lblSII);
    colA->addWidget(grpNB);

    auto* grpOSC = new QGroupBox(tr("OSC extractions"), this);
    auto* oscLayout = new QVBoxLayout(grpOSC);
    oscLayout->setSpacing(4);
    oscLayout->addWidget(m_btnOSC1);
    oscLayout->addWidget(m_lblOSC1);
    oscLayout->addWidget(m_btnOSC2);
    oscLayout->addWidget(m_lblOSC2);
    colA->addWidget(grpOSC);
    colA->addStretch(1);

    // Column B: Normalization + Actions
    auto* grpNorm = new QGroupBox(tr("Normalization"), this);
    m_normForm = new QFormLayout();
    m_normForm->setLabelAlignment(Qt::AlignRight);
    m_normForm->setFormAlignment(Qt::AlignTop);

    m_normForm->addRow(tr("Scenario:"), m_cmbScenario);
    m_normForm->addRow(tr("Mode:"), m_cmbMode);
    m_normForm->addRow(tr("Lightness:"), m_cmbLightness);

    m_lblBlackpoint = new QLabel(tr("Blackpoint\n(Min → Med):"), this);
    m_lblHLrecover = new QLabel(tr("HL Recover:"), this);
    m_lblHLreduct = new QLabel(tr("HL Reduction:"), this);
    m_lblBrightness = new QLabel(tr("Brightness:"), this);
    m_lblBlendmode = new QLabel(tr("Blend Mode:"), this);
    m_lblHaBlend = new QLabel(tr("Ha Blend:"), this);
    m_lblOIIIboostLabel = new QLabel(tr("OIII Boost:"), this);
    m_lblSIIboostLabel = new QLabel(tr("SII Boost:"), this);
    m_lblOIIIboost2Label = new QLabel(tr("OIII Boost:"), this);

    m_normForm->addRow(m_lblBlackpoint, m_rowBlackpoint.widget);
    m_normForm->addRow(m_lblHLrecover, m_rowHLrecover.widget);
    m_normForm->addRow(m_lblHLreduct, m_rowHLreduct.widget);
    m_normForm->addRow(m_lblBrightness, m_rowBrightness.widget);
    m_normForm->addRow(m_lblBlendmode, m_cmbBlendmode);
    m_normForm->addRow(m_lblHaBlend, m_rowHaBlend.widget);
    m_normForm->addRow(m_lblOIIIboostLabel, m_rowOIIIboost.widget);
    m_normForm->addRow(m_lblSIIboostLabel, m_rowSIIboost.widget);
    m_normForm->addRow(m_lblOIIIboost2Label, m_rowOIIIboost2.widget);

    m_normForm->addRow("", m_chkSCNR);

    grpNorm->setLayout(m_normForm);
    colB->addWidget(grpNorm);

    auto* grpActions = new QGroupBox(tr("Actions"), this);
    auto* actLayout = new QVBoxLayout(grpActions);
    actLayout->setSpacing(6);
    for (auto* btn : { m_btnClear, m_btnPreview, m_btnApply, m_btnPush }) {
        btn->setMinimumHeight(28);
        actLayout->addWidget(btn);
    }
    colB->addWidget(grpActions);
    colB->addStretch(1);

    leftRow->addLayout(colA, 1);
    leftRow->addLayout(colB, 1);

    leftScroll->setMinimumWidth(480);
    root->addWidget(leftScroll, 0);

    // ---- RIGHT PANEL (Preview) ----
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setSpacing(8);

    // Zoom toolbar
    auto* tools = new QHBoxLayout();
    tools->setSpacing(6);

    auto* btnZoomIn = new QPushButton("+");
    auto* btnZoomOut = new QPushButton("-");
    auto* btnFit = new QPushButton(tr("Fit"));

    btnZoomIn->setFixedWidth(30);
    btnZoomOut->setFixedWidth(30);
    btnFit->setFixedWidth(40);

    connect(btnZoomIn, &QPushButton::clicked, this, [this]() { m_view->scale(1.25, 1.25); });
    connect(btnZoomOut, &QPushButton::clicked, this, [this]() { m_view->scale(0.8, 0.8); });
    connect(btnFit, &QPushButton::clicked, this, [this]() {
        if (m_pixBase && !m_pixBase->pixmap().isNull()) {
            m_view->resetTransform();
            m_view->fitInView(m_pixBase, Qt::KeepAspectRatio);
        }
    });

    tools->addStretch(1);
    tools->addWidget(btnZoomOut);
    tools->addWidget(btnZoomIn);
    tools->addSpacing(10);
    tools->addWidget(btnFit);
    tools->addStretch(1);
    rightLayout->addLayout(tools);

    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setAlignment(Qt::AlignCenter);

    m_pixBase = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixBase);

    rightLayout->addWidget(m_view, 1);

    m_status = new QLabel("", this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet("color:#888;");
    rightLayout->addWidget(m_status, 0);

    root->addWidget(rightWidget, 1);

    refreshVisibility();
}

void NarrowbandNormalizationDialog::initWidgets() {
    // Import buttons
    m_btnImpSHO = new QPushButton(tr("Load SHO View..."), this);
    m_btnImpHSO = new QPushButton(tr("Load HSO View..."), this);
    m_btnImpHOS = new QPushButton(tr("Load HOS View..."), this);
    m_btnImpHOO = new QPushButton(tr("Load HOO View..."), this);

    // Channel buttons + labels
    m_btnHa = new QPushButton(tr("Load Ha..."), this);
    m_btnOIII = new QPushButton(tr("Load OIII..."), this);
    m_btnSII = new QPushButton(tr("Load SII..."), this);
    m_btnOSC1 = new QPushButton(tr("Load OSC1 (Ha/OIII)..."), this);
    m_btnOSC2 = new QPushButton(tr("Load OSC2 (SII/OIII)..."), this);

    m_lblHa = new QLabel(tr("No Ha loaded."), this);
    m_lblOIII = new QLabel(tr("No OIII loaded."), this);
    m_lblSII = new QLabel(tr("No SII loaded."), this);
    m_lblOSC1 = new QLabel(tr("No OSC1 loaded."), this);
    m_lblOSC2 = new QLabel(tr("No OSC2 loaded."), this);

    for (auto* lab : { m_lblHa, m_lblOIII, m_lblSII, m_lblOSC1, m_lblOSC2 }) {
        lab->setWordWrap(false);
        lab->setStyleSheet("color:#888; margin-left:8px;");
    }

    // Normalization combos
    m_cmbScenario = new QComboBox(this);
    m_cmbScenario->addItems({"SHO", "HSO", "HOS", "HOO"});

    m_cmbMode = new QComboBox(this);
    m_cmbMode->addItems({tr("Non-linear (Mode=1)"), tr("Linear (Mode=0)")});

    m_cmbLightness = new QComboBox(this);
    m_cmbLightness->addItems({tr("Off (0)"), tr("Original (1)"), tr("Ha (2)"),
                                tr("SII (3)"), tr("OIII (4)")});

    // Slider/Spin rows
    m_rowBlackpoint = createSliderSpinRow(0.0, 1.0, 0.01, 0.25, 3);
    m_rowHLrecover = createSliderSpinRow(0.5, 2.0, 0.01, 1.0, 3);
    m_rowHLreduct = createSliderSpinRow(0.5, 2.0, 0.01, 1.0, 3);
    m_rowBrightness = createSliderSpinRow(0.5, 2.0, 0.01, 1.0, 3);
    m_rowHaBlend = createSliderSpinRow(0.0, 1.0, 0.01, 0.6, 3);
    m_rowOIIIboost = createSliderSpinRow(0.5, 2.0, 0.01, 1.0, 3);
    m_rowSIIboost = createSliderSpinRow(0.5, 2.0, 0.01, 1.0, 3);
    m_rowOIIIboost2 = createSliderSpinRow(0.5, 2.0, 0.01, 1.0, 3);

    m_cmbBlendmode = new QComboBox(this);
    m_cmbBlendmode->addItems({tr("Screen"), tr("Add"), tr("Linear Dodge"), tr("Normal")});

    m_chkSCNR = new QCheckBox(tr("SCNR (reduce green cast)"), this);
    m_chkSCNR->setChecked(true);

    // Action buttons
    m_btnClear = new QPushButton(tr("Clear"), this);
    m_btnPreview = new QPushButton(tr("Preview"), this);
    m_btnApply = new QPushButton(tr("Apply to Current View"), this);
    m_btnPush = new QPushButton(tr("Push as New View"), this);
}

void NarrowbandNormalizationDialog::connectSignals() {
    // Import
    connect(m_btnImpSHO, &QPushButton::clicked, this, [this]() { onImportMappedView("SHO"); });
    connect(m_btnImpHSO, &QPushButton::clicked, this, [this]() { onImportMappedView("HSO"); });
    connect(m_btnImpHOS, &QPushButton::clicked, this, [this]() { onImportMappedView("HOS"); });
    connect(m_btnImpHOO, &QPushButton::clicked, this, [this]() { onImportMappedView("HOO"); });

    // Channel load
    connect(m_btnHa, &QPushButton::clicked, this, [this]() { onLoadChannel("Ha"); });
    connect(m_btnOIII, &QPushButton::clicked, this, [this]() { onLoadChannel("OIII"); });
    connect(m_btnSII, &QPushButton::clicked, this, [this]() { onLoadChannel("SII"); });
    connect(m_btnOSC1, &QPushButton::clicked, this, [this]() { onLoadChannel("OSC1"); });
    connect(m_btnOSC2, &QPushButton::clicked, this, [this]() { onLoadChannel("OSC2"); });

    // Actions
    connect(m_btnClear, &QPushButton::clicked, this, &NarrowbandNormalizationDialog::onClear);
    connect(m_btnPreview, &QPushButton::clicked, this, &NarrowbandNormalizationDialog::onPreview);
    connect(m_btnApply, &QPushButton::clicked, this, &NarrowbandNormalizationDialog::onApply);
    connect(m_btnPush, &QPushButton::clicked, this, &NarrowbandNormalizationDialog::onPushNew);

    // Scenario/mode changes
    connect(m_cmbScenario, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NarrowbandNormalizationDialog::onScenarioChanged);
    connect(m_cmbMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NarrowbandNormalizationDialog::onModeChanged);

    // All parameter changes → schedule preview
    connect(m_cmbLightness, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NarrowbandNormalizationDialog::schedulePreview);
    connect(m_cmbBlendmode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NarrowbandNormalizationDialog::schedulePreview);
    connect(m_chkSCNR, &QCheckBox::toggled,
            this, &NarrowbandNormalizationDialog::schedulePreview);

    for (auto* sp : { m_rowBlackpoint.spin, m_rowHLrecover.spin, m_rowHLreduct.spin,
                      m_rowBrightness.spin, m_rowHaBlend.spin, m_rowOIIIboost.spin,
                      m_rowSIIboost.spin, m_rowOIIIboost2.spin }) {
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &NarrowbandNormalizationDialog::schedulePreview);
    }

    for (auto* sl : { m_rowBlackpoint.slider, m_rowHLrecover.slider, m_rowHLreduct.slider,
                      m_rowBrightness.slider, m_rowHaBlend.slider, m_rowOIIIboost.slider,
                      m_rowSIIboost.slider, m_rowOIIIboost2.slider }) {
        connect(sl, &QSlider::valueChanged,
                this, &NarrowbandNormalizationDialog::schedulePreview);
    }
}

// ============================================================================
// Visibility Management
// ============================================================================

void NarrowbandNormalizationDialog::refreshVisibility() {
    bool isHOO = (m_cmbScenario->currentIndex() == 3);
    bool nonLinear = (m_cmbMode->currentIndex() == 0); // Mode=1 is non-linear

    // SII button/label visibility
    m_btnSII->setVisible(!isHOO);
    m_lblSII->setVisible(!isHOO);

    // HOO-only rows
    m_lblBlendmode->setVisible(isHOO);
    m_cmbBlendmode->setVisible(isHOO);
    m_lblHaBlend->setVisible(isHOO);
    m_rowHaBlend.widget->setVisible(isHOO);
    m_lblOIIIboostLabel->setVisible(isHOO);
    m_rowOIIIboost.widget->setVisible(isHOO);

    // SHO-family rows
    m_lblSIIboostLabel->setVisible(!isHOO);
    m_rowSIIboost.widget->setVisible(!isHOO);
    m_lblOIIIboost2Label->setVisible(!isHOO);
    m_rowOIIIboost2.widget->setVisible(!isHOO);

    // SCNR for SHO family only
    m_chkSCNR->setVisible(!isHOO);

    // Lightness only for non-linear
    m_cmbLightness->setEnabled(nonLinear);

    // Update lightness items for scenario
    if (nonLinear) {
        m_cmbLightness->blockSignals(true);
        QString cur = m_cmbLightness->currentText();
        m_cmbLightness->clear();
        if (isHOO) {
            m_cmbLightness->addItems({tr("Off (0)"), tr("Original (1)"),
                                       tr("Ha (2)"), tr("OIII (3)")});
        } else {
            m_cmbLightness->addItems({tr("Off (0)"), tr("Original (1)"),
                                       tr("Ha (2)"), tr("SII (3)"), tr("OIII (4)")});
        }
        int idx = m_cmbLightness->findText(cur);
        if (idx >= 0) m_cmbLightness->setCurrentIndex(idx);
        m_cmbLightness->blockSignals(false);
    }
}

void NarrowbandNormalizationDialog::onScenarioChanged() {
    refreshVisibility();
    schedulePreview();
}

void NarrowbandNormalizationDialog::onModeChanged() {
    refreshVisibility();
    schedulePreview();
}

void NarrowbandNormalizationDialog::schedulePreview() {
    m_status->setText(tr("Updating preview..."));
    m_debounce->start();
}

// ============================================================================
// Gather Parameters
// ============================================================================

ChannelOps::NBNParams NarrowbandNormalizationDialog::gatherParams() const {
    ChannelOps::NBNParams p;
    p.scenario = m_cmbScenario->currentIndex();   // 0=SHO, 1=HSO, 2=HOS, 3=HOO
    p.mode = (m_cmbMode->currentIndex() == 0) ? 1 : 0; // Non-linear=1, Linear=0
    p.lightness = m_cmbLightness->currentIndex();
    p.blackpoint = (float)m_rowBlackpoint.spin->value();
    p.hlrecover = std::max((float)m_rowHLrecover.spin->value(), 0.25f);
    p.hlreduct = std::max((float)m_rowHLreduct.spin->value(), 0.25f);
    p.brightness = std::max((float)m_rowBrightness.spin->value(), 0.25f);
    p.blendmode = m_cmbBlendmode->currentIndex();
    p.hablend = (float)m_rowHaBlend.spin->value();
    p.oiiiboost = (float)m_rowOIIIboost.spin->value();
    p.siiboost = (float)m_rowSIIboost.spin->value();
    p.oiiiboost2 = (float)m_rowOIIIboost2.spin->value();
    p.scnr = m_chkSCNR->isChecked();
    return p;
}

// ============================================================================
// Channel Loading
// ============================================================================

void NarrowbandNormalizationDialog::onLoadChannel(const QString& which) {
    QStringList options = { tr("From View"), tr("From File") };
    bool ok;
    QString src = QInputDialog::getItem(this, tr("Load %1").arg(which),
                                         tr("Source:"), options, 0, false, &ok);
    if (!ok) return;

    if (src == tr("From View")) {
        loadFromViewer(which);
    } else {
        loadFromFile(which);
    }
}

void NarrowbandNormalizationDialog::loadFromViewer(const QString& which) {
    if (!m_mainWindow) return;

    ImageViewer* v_cur = m_mainWindow->getCurrentViewer();
    if (!v_cur) {
        QMessageBox::warning(this, tr("No Image"), tr("No active image view found."));
        return;
    }

    auto sublist = v_cur->window()->findChildren<CustomMdiSubWindow*>();
    QStringList names;
    QList<ImageViewer*> viewers;
    for (CustomMdiSubWindow* csw : sublist) {
        ImageViewer* v = csw->viewer();
        if (!v || !v->getBuffer().isValid()) continue;
        if (csw->isToolWindow()) continue;
        names << csw->windowTitle();
        viewers << v;
    }

    if (names.isEmpty()) {
        QMessageBox::warning(this, tr("No Image"), tr("No active image view found."));
        return;
    }

    bool ok = true;
    QString choice;
    if (names.size() == 1) {
        choice = names[0];
    } else {
        choice = QInputDialog::getItem(
            this, tr("Select View - %1").arg(which),
            tr("Choose:"), names, 0, false, &ok);
        if (!ok) return;
    }

    int idx = names.indexOf(choice);
    if (idx < 0) return;

    const ImageBuffer& buf = viewers[idx]->getBuffer();
    ImageBuffer::ReadLock lock(&buf);

    int w = buf.width();
    int h = buf.height();
    int ch = buf.channels();
    const float* data = buf.data().data();
    size_t n = (size_t)w * h;

    if (which == "Ha" || which == "OIII" || which == "SII") {
        // Extract mono (use R channel if RGB)
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; i++) {
            mono[i] = data[i * ch]; // channel 0
        }

        // Validate dimensions
        if (!m_ha.empty() && (w != m_chW || h != m_chH)) {
            QMessageBox::warning(this, tr("Size Mismatch"),
                                 tr("Channel dimensions don't match previously loaded channels."));
            return;
        }

        m_chW = w;
        m_chH = h;

        if (which == "Ha") m_ha = std::move(mono);
        else if (which == "OIII") m_oiii = std::move(mono);
        else m_sii = std::move(mono);

        setStatusLabel(which, tr("From View (%1x%2)").arg(w).arg(h));
    } else if (which == "OSC1" || which == "OSC2") {
        // OSC: need RGB, extract Ha/OIII or SII/OIII
        if (ch < 3) {
            QMessageBox::warning(this, tr("Not RGB"),
                                 tr("OSC requires an RGB image."));
            return;
        }

        if (!m_ha.empty() && (w != m_chW || h != m_chH)) {
            QMessageBox::warning(this, tr("Size Mismatch"),
                                 tr("Channel dimensions don't match."));
            return;
        }

        m_chW = w;
        m_chH = h;

        // OSC1 = Ha/OIII → Ha from R, OIII from G+B average
        // OSC2 = SII/OIII → SII from R, OIII from G+B average
        std::vector<float> chR(n), chGB(n);
        for (size_t i = 0; i < n; i++) {
            chR[i] = data[i * ch + 0];
            chGB[i] = (data[i * ch + 1] + data[i * ch + 2]) * 0.5f;
        }

        if (which == "OSC1") {
            m_ha = std::move(chR);
            m_oiii = std::move(chGB);
            setStatusLabel("Ha", tr("From OSC1 R"));
            setStatusLabel("OIII", tr("From OSC1 G+B"));
        } else {
            m_sii = std::move(chR);
            m_oiii = std::move(chGB);
            setStatusLabel("SII", tr("From OSC2 R"));
            setStatusLabel("OIII", tr("From OSC2 G+B"));
        }

        setStatusLabel(which, tr("Extracted from View (%1x%2)").arg(w).arg(h));
    }

    m_status->setText(tr("%1 loaded.").arg(which));
    schedulePreview();
}

void NarrowbandNormalizationDialog::loadFromFile(const QString& which) {
    QString filter = tr("Images (*.png *.tif *.tiff *.fits *.fit *.xisf)");
    QString path = QFileDialog::getOpenFileName(this, tr("Select %1 File").arg(which),
                                                 "", filter);
    if (path.isEmpty()) return;

    ImageBuffer buf;
    QString err;
    if (!loadImageFromPath(path, buf, err)) {
        QMessageBox::critical(this, tr("Load Error"),
                              tr("Could not load %1: %2").arg(QFileInfo(path).fileName(), err));
        return;
    }

    int w = buf.width();
    int h = buf.height();
    int ch = buf.channels();
    const float* data = buf.data().data();
    size_t n = (size_t)w * h;

    if (which == "Ha" || which == "OIII" || which == "SII") {
        std::vector<float> mono(n);
        for (size_t i = 0; i < n; i++) {
            mono[i] = data[i * ch];
        }

        if (!m_ha.empty() && (w != m_chW || h != m_chH)) {
            QMessageBox::warning(this, tr("Size Mismatch"),
                                 tr("Channel dimensions don't match."));
            return;
        }

        m_chW = w;
        m_chH = h;

        if (which == "Ha") m_ha = std::move(mono);
        else if (which == "OIII") m_oiii = std::move(mono);
        else m_sii = std::move(mono);

        setStatusLabel(which, QFileInfo(path).fileName());
    }

    m_status->setText(tr("%1 loaded from file.").arg(which));
    schedulePreview();
}

void NarrowbandNormalizationDialog::setStatusLabel(const QString& which, const QString& text) {
    QLabel* lab = nullptr;
    if (which == "Ha") lab = m_lblHa;
    else if (which == "OIII") lab = m_lblOIII;
    else if (which == "SII") lab = m_lblSII;
    else if (which == "OSC1") lab = m_lblOSC1;
    else if (which == "OSC2") lab = m_lblOSC2;

    if (!lab) return;

    if (text.isEmpty()) {
        lab->setText(tr("No %1 loaded.").arg(which));
        lab->setStyleSheet("color:#888; margin-left:8px;");
    } else {
        lab->setText(text);
        lab->setStyleSheet("color:#2a7; font-weight:600; margin-left:8px;");
    }
}

// ============================================================================
// Import Mapped View (Split RGB → NB channels by scenario)
// ============================================================================

void NarrowbandNormalizationDialog::onImportMappedView(const QString& scenario) {
    // Set scenario combo
    int idx = m_cmbScenario->findText(scenario);
    if (idx >= 0) m_cmbScenario->setCurrentIndex(idx);

    if (!m_mainWindow) return;
    auto* viewer = m_mainWindow->getCurrentViewer();
    if (!viewer || !viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"),
                             tr("Select an RGB mapped view first."));
        return;
    }

    const ImageBuffer& buf = viewer->getBuffer();
    ImageBuffer::ReadLock lock(&buf);

    if (buf.channels() < 3) {
        QMessageBox::warning(this, tr("Not RGB"),
                             tr("Import requires an RGB mapped composite (3-channel)."));
        return;
    }

    int w = buf.width();
    int h = buf.height();
    size_t n = (size_t)w * h;
    const float* data = buf.data().data();
    int ch = buf.channels();

    std::vector<float> R(n), G(n), B(n);
    for (size_t i = 0; i < n; i++) {
        R[i] = data[i * ch + 0];
        G[i] = data[i * ch + 1];
        B[i] = data[i * ch + 2];
    }

    m_chW = w;
    m_chH = h;

    // SHO: R=SII, G=Ha, B=OIII
    // HSO: R=Ha, G=SII, B=OIII
    // HOS: R=Ha, G=OIII, B=SII
    // HOO: R=Ha, G=OIII, B=OIII
    if (scenario == "SHO") {
        m_sii = R; m_ha = G; m_oiii = B;
    } else if (scenario == "HSO") {
        m_ha = R; m_sii = G; m_oiii = B;
    } else if (scenario == "HOS") {
        m_ha = R; m_oiii = G; m_sii = B;
    } else { // HOO
        m_ha = R;
        // OIII = average of G and B
        m_oiii.resize(n);
        for (size_t i = 0; i < n; i++)
            m_oiii[i] = (G[i] + B[i]) * 0.5f;
        m_sii.clear();
    }

    setStatusLabel("Ha", tr("From %1 import").arg(scenario));
    setStatusLabel("OIII", tr("From %1 import").arg(scenario));
    if (scenario != "HOO")
        setStatusLabel("SII", tr("From %1 import").arg(scenario));
    else
        setStatusLabel("SII", "");

    m_status->setText(tr("Imported %1 view → channels split.").arg(scenario));
    schedulePreview();
}

// ============================================================================
// Clear
// ============================================================================

void NarrowbandNormalizationDialog::onClear() {
    m_ha.clear();
    m_oiii.clear();
    m_sii.clear();
    m_result.clear();
    m_chW = m_chH = 0;

    for (auto* lab : { m_lblHa, m_lblOIII, m_lblSII, m_lblOSC1, m_lblOSC2 }) {
        lab->setStyleSheet("color:#888; margin-left:8px;");
    }
    m_lblHa->setText(tr("No Ha loaded."));
    m_lblOIII->setText(tr("No OIII loaded."));
    m_lblSII->setText(tr("No SII loaded."));
    m_lblOSC1->setText(tr("No OSC1 loaded."));
    m_lblOSC2->setText(tr("No OSC2 loaded."));

    m_pixBase->setPixmap(QPixmap());
    m_status->setText(tr("Cleared."));
}

// ============================================================================
// Preview / Apply / Push
// ============================================================================

void NarrowbandNormalizationDialog::onPreview() {
    computeAndDisplay(true);
}

void NarrowbandNormalizationDialog::computeAndDisplay(bool fit) {
    bool isHOO = (m_cmbScenario->currentIndex() == 3);

    // Validate channels
    if (isHOO) {
        if (m_ha.empty() || m_oiii.empty()) {
            m_status->setText(tr("Load Ha + OIII to preview HOO."));
            return;
        }
    } else {
        QStringList missing;
        if (m_ha.empty()) missing << "Ha";
        if (m_oiii.empty()) missing << "OIII";
        if (m_sii.empty()) missing << "SII";
        if (!missing.isEmpty()) {
            m_status->setText(tr("Load %1 to preview %2.").arg(missing.join(", "),
                                                                 m_cmbScenario->currentText()));
            return;
        }
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_status->setText(tr("Computing..."));

    auto params = gatherParams();

    // Build SII channel (empty for HOO)
    std::vector<float> emptySii;
    const std::vector<float>& sii = isHOO ? emptySii : m_sii;

    m_result = ChannelOps::normalizeNarrowband(m_ha, m_oiii, sii,
                                                m_chW, m_chH, params);

    if (m_result.empty()) {
        m_status->setText(tr("Normalization failed."));
        QApplication::restoreOverrideCursor();
        return;
    }

    // Display
    QPixmap pm = floatToPixmap(m_result, m_chW, m_chH, 3);
    m_pixBase->setPixmap(pm);
    m_scene->setSceneRect(0, 0, m_chW, m_chH);

    if (fit) {
        m_view->resetTransform();
        m_view->fitInView(m_pixBase, Qt::KeepAspectRatio);
    }

    m_status->setText(tr("Done."));
    QApplication::restoreOverrideCursor();
}

void NarrowbandNormalizationDialog::onApply() {
    if (m_result.empty()) {
        onPreview();
        if (m_result.empty()) return;
    }

    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("No Image"),
                             tr("No active image to apply to."));
        return;
    }

    ImageBuffer newBuf;
    newBuf.setData(m_chW, m_chH, 3, m_result);
    newBuf.setMetadata(m_viewer->getBuffer().metadata());

    if (m_mainWindow) {
        m_mainWindow->startLongProcess();
        m_viewer->setBuffer(newBuf);
        m_mainWindow->endLongProcess();
    }
    accept();
}

void NarrowbandNormalizationDialog::onPushNew() {
    if (m_result.empty()) {
        onPreview();
        if (m_result.empty()) return;
    }

    ImageBuffer newBuf;
    newBuf.setData(m_chW, m_chH, 3, m_result);

    if (m_mainWindow) {
        m_mainWindow->createResultWindow(newBuf, tr("NB Normalized"));
    }
}

// ============================================================================
// Helper: Float to Pixmap
// ============================================================================

QPixmap NarrowbandNormalizationDialog::floatToPixmap(const std::vector<float>& img,
                                                      int w, int h, int ch) {
    QImage qimg(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; y++) {
        uchar* scan = qimg.scanLine(y);
        for (int x = 0; x < w; x++) {
            size_t idx = ((size_t)y * w + x) * ch;
            if (ch >= 3) {
                scan[x * 3 + 0] = (uchar)std::clamp((int)(img[idx + 0] * 255.0f), 0, 255);
                scan[x * 3 + 1] = (uchar)std::clamp((int)(img[idx + 1] * 255.0f), 0, 255);
                scan[x * 3 + 2] = (uchar)std::clamp((int)(img[idx + 2] * 255.0f), 0, 255);
            } else {
                uchar v = (uchar)std::clamp((int)(img[idx] * 255.0f), 0, 255);
                scan[x * 3 + 0] = v;
                scan[x * 3 + 1] = v;
                scan[x * 3 + 2] = v;
            }
        }
    }
    return QPixmap::fromImage(qimg);
}
