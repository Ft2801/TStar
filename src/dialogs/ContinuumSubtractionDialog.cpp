// ============================================================================
// ContinuumSubtractionDialog — Full-pipeline continuum subtraction
// Matches SAS Pro continuum_subtract.py methodology:
//   BG neutralization → Red-to-green normalization → Star-based WB →
//   Linear subtraction → Optional non-linear finalization
// Supports Ha, SII, OIII NB filters + Red/Green/OSC continuum sources
// with starry and starless variants, plus HaO3/S2O3 composite extraction.
// ============================================================================

#include "ContinuumSubtractionDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"
#include "../algos/CosmicClarityRunner.h"
#include "../widgets/CustomMdiSubWindow.h"
#include "../io/FitsLoader.h"
#include "../io/SimpleTiffReader.h"
#include "../io/XISFReader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QApplication>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QFileInfo>

// ============================================================================
// Worker Thread
// ============================================================================

ContinuumSubtractWorker::ContinuumSubtractWorker(
    const std::vector<Task>& tasks,
    const ContinuumSubtractParams& params,
    bool denoiseWithCC,
    const QString& cosmicClarityPath,
    QObject* parent)
    : QThread(parent)
    , m_tasks(tasks)
    , m_params(params)
    , m_denoiseCC(denoiseWithCC)
    , m_ccPath(cosmicClarityPath)
{
}

void ContinuumSubtractWorker::run() {
    for (const auto& task : m_tasks) {
        bool hasStarry   = task.nbStarry.isValid()   && task.contStarry.isValid();
        bool hasStarless = task.nbStarless.isValid()  && task.contStarless.isValid();

        ContinuumSubtractRecipe recipe;

        // ---- Starry pass (learn recipe) ----
        if (hasStarry) {
            emit statusUpdate(tr("Processing %1 (starry)...").arg(task.name));

            ImageBuffer result = ChannelOps::continuumSubtractFull(
                task.nbStarry, task.contStarry, m_params, &recipe);

            if (result.isValid()) {
                // Optional denoising with Cosmic Clarity
                if (m_denoiseCC && !m_ccPath.isEmpty()) {
                    emit statusUpdate(tr("Denoising %1 with Cosmic Clarity...").arg(task.name));
                    CosmicClarityRunner runner;
                    CosmicClarityParams ccParams;
                    ccParams.mode = CosmicClarityParams::Mode_Denoise;
                    ccParams.denoiseLum = 0.9f;
                    ccParams.denoiseColor = 0.9f;
                    ccParams.denoiseMode = "full";
                    ImageBuffer denoised;
                    QString ccErr;
                    if (runner.run(result, denoised, ccParams, &ccErr) && denoised.isValid()) {
                        result = std::move(denoised);
                    }
                }

                emit resultReady(task.name + " (starry)", result);
            }
        }

        // ---- Starless pass (apply recipe or standalone) ----
        if (hasStarless) {
            emit statusUpdate(tr("Processing %1 (starless)...").arg(task.name));

            ImageBuffer result;
            if (recipe.valid) {
                // Apply learned recipe from starry pass
                result = ChannelOps::continuumSubtractWithRecipe(
                    task.nbStarless, task.contStarless, recipe,
                    m_params.outputLinear, m_params.targetMedian, m_params.curvesBoost);
            } else {
                // Standalone starless processing (no WB — same as Python starless-only path)
                ContinuumSubtractParams slParams = m_params;
                // Use Q=0.9 for starless-only path (matches Python)
                if (task.starlessOnly) slParams.qFactor = 0.9f;
                result = ChannelOps::continuumSubtractFull(
                    task.nbStarless, task.contStarless, slParams, nullptr);
            }

            if (result.isValid()) {
                if (m_denoiseCC && !m_ccPath.isEmpty()) {
                    emit statusUpdate(tr("Denoising %1 starless with Cosmic Clarity...").arg(task.name));
                    CosmicClarityRunner runner;
                    CosmicClarityParams ccParams;
                    ccParams.mode = CosmicClarityParams::Mode_Denoise;
                    ccParams.denoiseLum = 0.9f;
                    ccParams.denoiseColor = 0.9f;
                    ccParams.denoiseMode = "full";
                    ImageBuffer denoised;
                    QString ccErr;
                    if (runner.run(result, denoised, ccParams, &ccErr) && denoised.isValid()) {
                        result = std::move(denoised);
                    }
                }

                emit resultReady(task.name + " (starless)", result);
            }
        }
    }

    emit allDone();
}

// ============================================================================
// Dialog Implementation
// ============================================================================

ContinuumSubtractionDialog::ContinuumSubtractionDialog(QWidget* parent)
    : DialogBase(parent, tr("Continuum Subtraction")) {
    m_mainWindow = getCallbacks();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // ---- Three-column layout ----
    QHBoxLayout* columnsLayout = new QHBoxLayout();

    // ============ Column 1: Narrowband Filters ============
    QGroupBox* nbGroup = new QGroupBox(tr("Narrowband Filters"), this);
    QVBoxLayout* nbLayout = new QVBoxLayout(nbGroup);

    createSlotUI(nbLayout, m_haStarry,     tr("Ha"),               "Ha");
    createSlotUI(nbLayout, m_haStarless,   tr("Ha (Starless)"),    "Ha (Starless)");
    createSlotUI(nbLayout, m_siiStarry,    tr("SII"),              "SII");
    createSlotUI(nbLayout, m_siiStarless,  tr("SII (Starless)"),   "SII (Starless)");
    createSlotUI(nbLayout, m_oiiiStarry,   tr("OIII"),             "OIII");
    createSlotUI(nbLayout, m_oiiiStarless, tr("OIII (Starless)"),  "OIII (Starless)");

    // Composites
    createSlotUI(nbLayout, m_hao3Starry,   tr("HaO3"),             "HaO3");
    createSlotUI(nbLayout, m_hao3Starless, tr("HaO3 (Starless)"),  "HaO3 (Starless)");
    createSlotUI(nbLayout, m_s2o3Starry,   tr("S2O3"),             "S2O3");
    createSlotUI(nbLayout, m_s2o3Starless, tr("S2O3 (Starless)"),  "S2O3 (Starless)");

    // ============ Column 2: Continuum Sources ============
    QGroupBox* contGroup = new QGroupBox(tr("Continuum Sources"), this);
    QVBoxLayout* contLayout = new QVBoxLayout(contGroup);

    createSlotUI(contLayout, m_redStarry,    tr("Red"),              "Red");
    createSlotUI(contLayout, m_redStarless,  tr("Red (Starless)"),   "Red (Starless)");
    createSlotUI(contLayout, m_greenStarry,  tr("Green"),            "Green");
    createSlotUI(contLayout, m_greenStarless,tr("Green (Starless)"), "Green (Starless)");
    createSlotUI(contLayout, m_oscStarry,    tr("OSC"),              "OSC");
    createSlotUI(contLayout, m_oscStarless,  tr("OSC (Starless)"),   "OSC (Starless)");

    // ============ Column 3: Parameters ============
    QGroupBox* paramGroup = new QGroupBox(tr("Parameters"), this);
    QVBoxLayout* paramLayout = new QVBoxLayout(paramGroup);

    // Q-Factor
    QHBoxLayout* qRow = new QHBoxLayout();
    QLabel* qLabel = new QLabel(tr("Q-Factor:"), this);
    m_qFactorSpin = new QDoubleSpinBox(this);
    m_qFactorSpin->setRange(0.10, 2.00);
    m_qFactorSpin->setSingleStep(0.05);
    m_qFactorSpin->setValue(0.80);
    m_qFactorSpin->setDecimals(2);

    m_qFactorSlider = new QSlider(Qt::Horizontal, this);
    m_qFactorSlider->setRange(10, 200);
    m_qFactorSlider->setValue(80);

    connect(m_qFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ContinuumSubtractionDialog::onQFactorChanged);
    connect(m_qFactorSlider, &QSlider::valueChanged, [this](int val) {
        m_qFactorSpin->setValue(val / 100.0);
    });

    qRow->addWidget(qLabel);
    qRow->addWidget(m_qFactorSlider, 1);
    qRow->addWidget(m_qFactorSpin);
    paramLayout->addLayout(qRow);

    // Output Linear
    m_outputLinearCheck = new QCheckBox(tr("Output Linear Image Only"), this);
    m_outputLinearCheck->setChecked(true);
    paramLayout->addWidget(m_outputLinearCheck);

    // Denoise with Cosmic Clarity
    m_denoiseCheck = new QCheckBox(tr("Denoise with Cosmic Clarity (0.9)"), this);
    m_denoiseCheck->setToolTip(
        tr("Runs Cosmic Clarity denoise on the linear continuum-subtracted image "
           "before any non-linear stretch."));
    m_denoiseCheck->setChecked(false);
    paramLayout->addWidget(m_denoiseCheck);

    // ---- Settings section ----
    QGroupBox* settingsGroup = new QGroupBox(tr("Settings"), this);
    QFormLayout* advLayout = new QFormLayout(settingsGroup);

    m_thresholdSpin = new QDoubleSpinBox(this);
    m_thresholdSpin->setRange(0.5, 50.0);
    m_thresholdSpin->setValue(5.0);
    m_thresholdSpin->setDecimals(1);
    advLayout->addRow(tr("WB Star Threshold (σ):"), m_thresholdSpin);

    m_curvesSpin = new QDoubleSpinBox(this);
    m_curvesSpin->setRange(0.0, 1.0);
    m_curvesSpin->setValue(0.50);
    m_curvesSpin->setDecimals(2);
    m_curvesSpin->setSingleStep(0.05);
    advLayout->addRow(tr("Curves Boost:"), m_curvesSpin);

    paramLayout->addWidget(settingsGroup);

    // Info label
    QLabel* infoLabel = new QLabel(
        tr("Pipeline: BG Neutralization > Red-to-Green Normalization > "
           "Star WB > Linear Subtraction (NB - Q*(Cont - median)) > Stretch"), this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-style: italic;");
    paramLayout->addWidget(infoLabel);

    paramLayout->addStretch(1);

    // ---- Assemble columns ----
    columnsLayout->addWidget(nbGroup,   1);
    columnsLayout->addWidget(contGroup, 1);
    columnsLayout->addWidget(paramGroup, 1);
    mainLayout->addLayout(columnsLayout);

    // ---- Status row ----
    m_statusLabel = new QLabel("", this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0); // indeterminate
    m_progress->setVisible(false);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_progress);

    // ---- Bottom buttons ----
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* clearBtn = new QPushButton(tr("Clear All"), this);
    connect(clearBtn, &QPushButton::clicked, this, &ContinuumSubtractionDialog::onClear);

    QPushButton* refreshBtn = new QPushButton(tr("Refresh"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &ContinuumSubtractionDialog::refreshImageList);

    m_executeBtn = new QPushButton(tr("Execute"), this);
    connect(m_executeBtn, &QPushButton::clicked, this, &ContinuumSubtractionDialog::onExecute);

    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(clearBtn);
    btnLayout->addWidget(refreshBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(m_executeBtn);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    // Adjust to natural size then center within parent
    adjustSize();
    if (parent) {
        QWidget* parentWidget = qobject_cast<QWidget*>(parent);
        if (parentWidget) {
            move(parentWidget->x() + (parentWidget->width()  - width())  / 2,
                 parentWidget->y() + (parentWidget->height() - height()) / 2);
        }
    }
}

ContinuumSubtractionDialog::~ContinuumSubtractionDialog() {
    if (m_worker) {
        m_worker->wait();
        delete m_worker;
    }
}

void ContinuumSubtractionDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

// ============================================================================
// UI Helpers
// ============================================================================

void ContinuumSubtractionDialog::createSlotUI(QVBoxLayout* layout, ImageSlot& slot,
                                               const QString& label, const QString& channel) {
    slot.button = new QPushButton(tr("Load %1").arg(label), this);
    slot.label  = new QLabel(QString::fromUtf8("\xe2\x80\x94"), this);  // em dash
    slot.label->setStyleSheet("color: gray;");

    connect(slot.button, &QPushButton::clicked, [this, channel]() {
        loadImage(channel);
    });

    layout->addWidget(slot.button);
    layout->addWidget(slot.label);
}

void ContinuumSubtractionDialog::populateCombo(QComboBox* combo) {
    combo->clear();
    if (!m_mainWindow) return;
    ImageViewer* v_cur = m_mainWindow->getCurrentViewer();
    if (!v_cur) return;

    auto sublist = v_cur->window()->findChildren<CustomMdiSubWindow*>();
    for (CustomMdiSubWindow* csw : sublist) {
        ImageViewer* v = csw->viewer();
        if (!v || !v->getBuffer().isValid()) continue;
        if (csw->isToolWindow()) continue;
        combo->addItem(csw->windowTitle(),
                       QVariant::fromValue(reinterpret_cast<quintptr>(v)));
    }
}

void ContinuumSubtractionDialog::refreshImageList() {
    // Nothing specific to refresh in multi-load design;
    // kept as public slot for MainWindow compatibility.
}

// ============================================================================
// Image Loading
// ============================================================================

void ContinuumSubtractionDialog::loadImage(const QString& channel) {
    // Ask user: load from open view or from file
    QStringList sources;
    sources << tr("From View") << tr("From File");
    bool ok = false;
    QString source = QInputDialog::getItem(
        this, tr("Select %1 Image Source").arg(channel),
        tr("Load image from:"), sources, 0, false, &ok);
    if (!ok) return;

    ImageBuffer buffer;
    QString labelText;

    if (source == tr("From View")) {
        // Build list of open views
        if (!m_mainWindow) return;
        ImageViewer* v_cur = m_mainWindow->getCurrentViewer();
        if (!v_cur) {
            QMessageBox::information(this, tr("Load"), tr("No open images."));
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
            QMessageBox::information(this, tr("Load"), tr("No open images."));
            return;
        }

        QString choice;
        if (names.size() == 1) {
            choice = names[0];
        } else {
            choice = QInputDialog::getItem(
                this, tr("Select View - %1").arg(channel),
                tr("Choose:"), names, 0, false, &ok);
            if (!ok) return;
        }

        int idx = names.indexOf(choice);
        if (idx < 0) return;
        buffer = viewers[idx]->getBuffer();
        labelText = choice;

    } else {
        // From file
        QString fileFilter = tr("Images (*.png *.tif *.tiff *.fits *.fit *.xisf)");
        QString path = QFileDialog::getOpenFileName(
            this, tr("Select %1 Image").arg(channel), QString(), fileFilter);
        if (path.isEmpty()) return;

        QString err;
        QFileInfo fi(path);
        QString ext = fi.suffix().toLower();

        if (ext == "fits" || ext == "fit") {
            if (!FitsLoader::load(path, buffer, &err)) {
                QMessageBox::critical(this, tr("Error"),
                    tr("Failed to load FITS: %1").arg(err));
                return;
            }
        } else if (ext == "xisf") {
            if (!XISFReader::read(path, buffer, &err)) {
                QMessageBox::critical(this, tr("Error"),
                    tr("Failed to load XISF: %1").arg(err));
                return;
            }
        } else if (ext == "tif" || ext == "tiff") {
            int tw, th, tc;
            std::vector<float> tdata;
            if (!SimpleTiffReader::readFloat32(path, tw, th, tc, tdata, &err)) {
                QMessageBox::critical(this, tr("Error"),
                    tr("Failed to load TIFF: %1").arg(err));
                return;
            }
            buffer.setData(tw, th, tc, tdata);
        } else {
            // Try QImage for PNG etc.
            QImage img(path);
            if (img.isNull()) {
                QMessageBox::critical(this, tr("Error"), tr("Failed to load image."));
                return;
            }
            img = img.convertToFormat(QImage::Format_RGB888);
            int iw = img.width(), ih = img.height();
            std::vector<float> idata(iw * ih * 3);
            for (int y = 0; y < ih; ++y) {
                const uchar* line = img.scanLine(y);
                for (int x = 0; x < iw; ++x) {
                    idata[(y * iw + x) * 3 + 0] = line[x * 3 + 0] / 255.0f;
                    idata[(y * iw + x) * 3 + 1] = line[x * 3 + 1] / 255.0f;
                    idata[(y * iw + x) * 3 + 2] = line[x * 3 + 2] / 255.0f;
                }
            }
            buffer.setData(iw, ih, 3, idata);
        }

        labelText = fi.fileName();
    }

    if (!buffer.isValid()) {
        QMessageBox::warning(this, tr("Load"), tr("Image is empty or invalid."));
        return;
    }

    // Store into the correct slot
    // Map channel string to the correct slot
    struct SlotMapping { const char* key; ImageSlot* slot; };

    SlotMapping mappings[] = {
        {"Ha",               &m_haStarry},
        {"Ha (Starless)",    &m_haStarless},
        {"SII",              &m_siiStarry},
        {"SII (Starless)",   &m_siiStarless},
        {"OIII",             &m_oiiiStarry},
        {"OIII (Starless)",  &m_oiiiStarless},
        {"Red",              &m_redStarry},
        {"Red (Starless)",   &m_redStarless},
        {"Green",            &m_greenStarry},
        {"Green (Starless)", &m_greenStarless},
        {"OSC",              &m_oscStarry},
        {"OSC (Starless)",   &m_oscStarless},
        {"HaO3",             &m_hao3Starry},
        {"HaO3 (Starless)",  &m_hao3Starless},
        {"S2O3",             &m_s2o3Starry},
        {"S2O3 (Starless)",  &m_s2o3Starless},
    };

    bool found = false;
    for (const auto& m : mappings) {
        if (channel == m.key) {
            m.slot->buffer = buffer;
            m.slot->label->setText(labelText);
            m.slot->label->setStyleSheet("");

            // Handle composite extraction for HaO3 and S2O3
            bool starless = channel.contains("Starless");
            if (channel.startsWith("HaO3"))
                extractFromComposite("HaO3", starless, buffer);
            else if (channel.startsWith("S2O3"))
                extractFromComposite("S2O3", starless, buffer);

            m_statusLabel->setText(tr("Loaded %1: %2").arg(channel, labelText));
            found = true;
            break;
        }
    }

    if (!found) {
        QMessageBox::critical(this, tr("Error"), tr("Unknown channel: %1").arg(channel));
    }
}

// ============================================================================
// Composite Extraction (HaO3 -> Ha + OIII, S2O3 -> SII + OIII)
// ============================================================================

void ContinuumSubtractionDialog::extractFromComposite(
    const QString& composite, bool starless, const ImageBuffer& img) {

    if (!img.isValid() || img.channels() < 3) {
        QMessageBox::warning(this, tr("Composite Load"),
            tr("%1 expects a 3-channel color image (R=emission, G=OIII). "
               "Cannot extract channels.").arg(composite));
        return;
    }

    // Extract R and G channels
    auto channels = ChannelOps::extractChannels(img);
    if (channels.size() < 2) return;

    ImageBuffer rCh = channels[0]; // R = Ha or SII
    ImageBuffer gCh = channels[1]; // G = OIII

    if (composite == "HaO3") {
        if (starless) {
            m_haStarless.buffer = rCh;
            m_haStarless.label->setText(tr("Ha from HaO3 (starless)"));
            m_haStarless.label->setStyleSheet("");
        } else {
            m_haStarry.buffer = rCh;
            m_haStarry.label->setText(tr("Ha from HaO3 [R]"));
            m_haStarry.label->setStyleSheet("");
        }
    } else if (composite == "S2O3") {
        if (starless) {
            m_siiStarless.buffer = rCh;
            m_siiStarless.label->setText(tr("SII from S2O3 (starless)"));
            m_siiStarless.label->setStyleSheet("");
        } else {
            m_siiStarry.buffer = rCh;
            m_siiStarry.label->setText(tr("SII from S2O3 [R]"));
            m_siiStarry.label->setStyleSheet("");
        }
    }

    // OIII: average with any existing OIII from other composite
    // (matches Python _update_oiii_from_composites behavior)
    ImageSlot& oiiiSlot = starless ? m_oiiiStarless : m_oiiiStarry;

    if (oiiiSlot.loaded()) {
        // Average with existing OIII
        const auto& existData = oiiiSlot.buffer.data();
        const auto& newData   = gCh.data();
        int ow = oiiiSlot.buffer.width(), oh = oiiiSlot.buffer.height();
        if (ow == gCh.width() && oh == gCh.height()) {
            std::vector<float> avg(existData.size());
            for (size_t i = 0; i < avg.size(); ++i)
                avg[i] = (existData[i] + newData[i]) * 0.5f;
            oiiiSlot.buffer.setData(ow, oh, 1, avg);
            oiiiSlot.label->setText(tr("OIII from %1 (averaged)%2")
                .arg(composite, starless ? " (starless)" : ""));
        } else {
            oiiiSlot.buffer = gCh;
            oiiiSlot.label->setText(tr("OIII from %1%2")
                .arg(composite, starless ? " (starless)" : ""));
        }
    } else {
        oiiiSlot.buffer = gCh;
        oiiiSlot.label->setText(tr("OIII from %1%2")
            .arg(composite, starless ? " (starless)" : ""));
    }
    oiiiSlot.label->setStyleSheet("");
}

// ============================================================================
// Continuum Resolution
// ============================================================================

bool ContinuumSubtractionDialog::getContinuumForFilter(
    const QString& filter, bool starless, ImageBuffer& outCont) {

    // Ha, SII -> Red channel (or OSC Red)
    // OIII    -> Green channel (or OSC Green)
    bool useGreen = (filter == "OIII");

    // Try dedicated Red/Green first
    const ImageSlot& directSlot = useGreen
        ? (starless ? m_greenStarless : m_greenStarry)
        : (starless ? m_redStarless   : m_redStarry);

    if (directSlot.loaded()) {
        outCont = directSlot.buffer;
        return true;
    }

    // Fallback: extract from OSC
    const ImageSlot& oscSlot = starless ? m_oscStarless : m_oscStarry;
    if (oscSlot.loaded() && oscSlot.buffer.channels() >= 3) {
        auto chans = ChannelOps::extractChannels(oscSlot.buffer);
        if (chans.size() >= 2) {
            outCont = useGreen ? chans[1] : chans[0];
            return true;
        }
    }

    return false;
}

// ============================================================================
// Q-Factor Sync
// ============================================================================

void ContinuumSubtractionDialog::onQFactorChanged(double val) {
    QSignalBlocker b(m_qFactorSlider);
    m_qFactorSlider->setValue(static_cast<int>(val * 100));
}

// ============================================================================
// Clear All
// ============================================================================

void ContinuumSubtractionDialog::onClear() {
    ImageSlot* allSlots[] = {
        &m_haStarry, &m_haStarless, &m_siiStarry, &m_siiStarless,
        &m_oiiiStarry, &m_oiiiStarless,
        &m_hao3Starry, &m_hao3Starless, &m_s2o3Starry, &m_s2o3Starless,
        &m_redStarry, &m_redStarless, &m_greenStarry, &m_greenStarless,
        &m_oscStarry, &m_oscStarless,
    };
    for (auto* s : allSlots) s->clear();
    m_statusLabel->setText(tr("All loaded images cleared."));
}

// ============================================================================
// Execute — Build tasks and launch worker
// ============================================================================

void ContinuumSubtractionDialog::onExecute() {
    std::vector<ContinuumSubtractWorker::Task> tasks;

    // Build tasks for each NB filter
    struct FilterDef {
        QString name;
        ImageSlot* nbStarry;
        ImageSlot* nbStarless;
    };

    FilterDef filters[] = {
        { "Ha",   &m_haStarry,   &m_haStarless },
        { "SII",  &m_siiStarry,  &m_siiStarless },
        { "OIII", &m_oiiiStarry, &m_oiiiStarless },
    };

    for (const auto& f : filters) {
        ContinuumSubtractWorker::Task task;
        task.name = f.name;

        // Resolve continuum for starry
        ImageBuffer contStarry, contStarless;
        bool hasContStarry   = getContinuumForFilter(f.name, false, contStarry);
        bool hasContStarless = getContinuumForFilter(f.name, true,  contStarless);

        bool hasNBStarry   = f.nbStarry->loaded();
        bool hasNBStarless = f.nbStarless->loaded();

        bool hasStarryPair   = hasNBStarry   && hasContStarry;
        bool hasStarlessPair = hasNBStarless && hasContStarless;

        if (!hasStarryPair && !hasStarlessPair)
            continue; // Skip this filter — no complete pair

        if (hasStarryPair) {
            task.nbStarry   = f.nbStarry->buffer;
            task.contStarry = contStarry;
        }
        if (hasStarlessPair) {
            task.nbStarless   = f.nbStarless->buffer;
            task.contStarless = contStarless;
        }

        // Check dimensions match
        if (hasStarryPair &&
            (task.nbStarry.width() != task.contStarry.width() ||
             task.nbStarry.height() != task.contStarry.height())) {
            QMessageBox::warning(this, tr("Dimension Mismatch"),
                tr("%1 starry: NB (%2x%3) and Continuum (%4x%5) dimensions must match.")
                .arg(f.name)
                .arg(task.nbStarry.width()).arg(task.nbStarry.height())
                .arg(task.contStarry.width()).arg(task.contStarry.height()));
            continue;
        }
        if (hasStarlessPair &&
            (task.nbStarless.width() != task.contStarless.width() ||
             task.nbStarless.height() != task.contStarless.height())) {
            QMessageBox::warning(this, tr("Dimension Mismatch"),
                tr("%1 starless: NB (%2x%3) and Continuum (%4x%5) dimensions must match.")
                .arg(f.name)
                .arg(task.nbStarless.width()).arg(task.nbStarless.height())
                .arg(task.contStarless.width()).arg(task.contStarless.height()));
            continue;
        }

        task.starlessOnly = (!hasStarryPair && hasStarlessPair);
        tasks.push_back(std::move(task));
    }

    if (tasks.empty()) {
        m_statusLabel->setText(
            tr("Load at least one NB filter + matching continuum (or OSC)."));
        return;
    }

    // Build params
    ContinuumSubtractParams params;
    params.qFactor       = static_cast<float>(m_qFactorSpin->value());
    params.starThreshold = static_cast<float>(m_thresholdSpin->value());
    params.outputLinear  = m_outputLinearCheck->isChecked();
    params.targetMedian  = 0.25f;
    params.curvesBoost   = static_cast<float>(m_curvesSpin->value());

    bool denoiseCC = m_denoiseCheck->isChecked();

    // Launch worker
    m_executeBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_statusLabel->setText(tr("Processing..."));

    if (m_worker) {
        m_worker->wait();
        delete m_worker;
    }

    m_worker = new ContinuumSubtractWorker(tasks, params, denoiseCC, QString(), this);
    connect(m_worker, &ContinuumSubtractWorker::resultReady,
            this, &ContinuumSubtractionDialog::onResultReady, Qt::QueuedConnection);
    connect(m_worker, &ContinuumSubtractWorker::statusUpdate,
            this, &ContinuumSubtractionDialog::onWorkerStatus, Qt::QueuedConnection);
    connect(m_worker, &ContinuumSubtractWorker::allDone,
            this, &ContinuumSubtractionDialog::onWorkerDone, Qt::QueuedConnection);
    m_worker->start();
}

// ============================================================================
// Worker Callbacks
// ============================================================================

void ContinuumSubtractionDialog::onResultReady(const QString& name, const ImageBuffer& result) {
    QString title = QString("%1_ContSub").arg(name);
    if (m_mainWindow) {
        m_mainWindow->createResultWindow(result, title);
        m_mainWindow->logMessage(
            tr("Continuum Subtraction: created '%1'").arg(title), 0, false);
    }
}

void ContinuumSubtractionDialog::onWorkerStatus(const QString& msg) {
    m_statusLabel->setText(msg);
}

void ContinuumSubtractionDialog::onWorkerDone() {
    m_progress->setVisible(false);
    m_executeBtn->setEnabled(true);
    m_statusLabel->setText(tr("Done."));
}
