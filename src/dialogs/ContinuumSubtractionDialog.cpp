/**
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is inspired by or incorporates code from Seti Astro Suite
 * (hosted at https://github.com/setiastro/setiastrosuitepro), which is licensed under GPLv3.
 * Modified by Ft2801 in 2026.
 * Original copyright (c) 2026 SetiAstro.
 * -------------------------------------------------------------------
 */

// ============================================================================
// ContinuumSubtractionDialog.cpp
//
// Full-pipeline continuum subtraction dialog.
// Implements the SAS Pro continuum_subtract.py methodology:
//   BG Neutralization -> Red-to-Green Normalization -> Star-based White Balance
//   -> Linear Subtraction -> Optional non-linear finalization
//
// Supported narrowband filters : Ha, SII, OIII
// Supported continuum sources  : Red, Green, OSC
// Composite inputs             : HaO3 (R=Ha, G=OIII), S2O3 (R=SII, G=OIII)
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

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QVBoxLayout>

// ============================================================================
// ContinuumSubtractWorker
// ============================================================================

ContinuumSubtractWorker::ContinuumSubtractWorker(
    const std::vector<Task>&       tasks,
    const ContinuumSubtractParams& params,
    bool                           denoiseWithCC,
    const QString&                 cosmicClarityPath,
    QObject*                       parent)
    : QThread(parent)
    , m_tasks(tasks)
    , m_params(params)
    , m_denoiseCC(denoiseWithCC)
    , m_ccPath(cosmicClarityPath)
{
}

void ContinuumSubtractWorker::run()
{
    for (const auto& task : m_tasks)
    {
        const bool hasStarry   = task.nbStarry.isValid()   && task.contStarry.isValid();
        const bool hasStarless = task.nbStarless.isValid() && task.contStarless.isValid();

        ContinuumSubtractRecipe recipe;

        // --- Starry pass: run the full pipeline and capture the calibration recipe ---
        if (hasStarry)
        {
            emit statusUpdate(tr("Processing %1 (starry)...").arg(task.name));

            ImageBuffer result = ChannelOps::continuumSubtractFull(
                task.nbStarry, task.contStarry, m_params, &recipe);

            if (result.isValid())
            {
                if (m_denoiseCC && !m_ccPath.isEmpty())
                {
                    emit statusUpdate(
                        tr("Denoising %1 with Cosmic Clarity...").arg(task.name));

                    CosmicClarityRunner runner;
                    CosmicClarityParams ccParams;
                    ccParams.mode        = CosmicClarityParams::Mode_Denoise;
                    ccParams.denoiseLum  = 0.9f;
                    ccParams.denoiseColor = 0.9f;
                    ccParams.denoiseMode  = "full";

                    ImageBuffer denoised;
                    QString     ccErr;
                    if (runner.run(result, denoised, ccParams, &ccErr) && denoised.isValid())
                        result = std::move(denoised);
                }

                emit resultReady(task.name + " (starry)", result);
            }
        }

        // --- Starless pass: apply the learned recipe or run standalone ---
        if (hasStarless)
        {
            emit statusUpdate(tr("Processing %1 (starless)...").arg(task.name));

            ImageBuffer result;

            if (recipe.valid)
            {
                // Apply the white-balance recipe learned from the starry pass.
                result = ChannelOps::continuumSubtractWithRecipe(
                    task.nbStarless, task.contStarless, recipe,
                    m_params.outputLinear,
                    m_params.targetMedian,
                    m_params.curvesBoost);
            }
            else
            {
                // Standalone starless path (no star-based WB, matches Python starless-only path).
                ContinuumSubtractParams slParams = m_params;
                if (task.starlessOnly)
                    slParams.qFactor = 0.9f; // Python equivalent Q for starless-only

                result = ChannelOps::continuumSubtractFull(
                    task.nbStarless, task.contStarless, slParams, nullptr);
            }

            if (result.isValid())
            {
                if (m_denoiseCC && !m_ccPath.isEmpty())
                {
                    emit statusUpdate(
                        tr("Denoising %1 starless with Cosmic Clarity...").arg(task.name));

                    CosmicClarityRunner runner;
                    CosmicClarityParams ccParams;
                    ccParams.mode         = CosmicClarityParams::Mode_Denoise;
                    ccParams.denoiseLum   = 0.9f;
                    ccParams.denoiseColor = 0.9f;
                    ccParams.denoiseMode  = "full";

                    ImageBuffer denoised;
                    QString     ccErr;
                    if (runner.run(result, denoised, ccParams, &ccErr) && denoised.isValid())
                        result = std::move(denoised);
                }

                emit resultReady(task.name + " (starless)", result);
            }
        }
    }

    emit allDone();
}

// ============================================================================
// ContinuumSubtractionDialog - construction
// ============================================================================

ContinuumSubtractionDialog::ContinuumSubtractionDialog(QWidget* parent)
    : DialogBase(parent, tr("Continuum Subtraction"))
{
    m_mainWindow = getCallbacks();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Three-column layout: Narrowband | Continuum | Parameters ---
    QHBoxLayout* columnsLayout = new QHBoxLayout();

    // Column 1: Narrowband filter inputs
    QGroupBox*   nbGroup  = new QGroupBox(tr("Narrowband Filters"), this);
    QVBoxLayout* nbLayout = new QVBoxLayout(nbGroup);

    createSlotUI(nbLayout, m_haStarry,     tr("Ha"),               "Ha");
    createSlotUI(nbLayout, m_haStarless,   tr("Ha (Starless)"),    "Ha (Starless)");
    createSlotUI(nbLayout, m_siiStarry,    tr("SII"),              "SII");
    createSlotUI(nbLayout, m_siiStarless,  tr("SII (Starless)"),   "SII (Starless)");
    createSlotUI(nbLayout, m_oiiiStarry,   tr("OIII"),             "OIII");
    createSlotUI(nbLayout, m_oiiiStarless, tr("OIII (Starless)"),  "OIII (Starless)");
    createSlotUI(nbLayout, m_hao3Starry,   tr("HaO3"),             "HaO3");
    createSlotUI(nbLayout, m_hao3Starless, tr("HaO3 (Starless)"),  "HaO3 (Starless)");
    createSlotUI(nbLayout, m_s2o3Starry,   tr("S2O3"),             "S2O3");
    createSlotUI(nbLayout, m_s2o3Starless, tr("S2O3 (Starless)"),  "S2O3 (Starless)");

    // Column 2: Continuum source inputs
    QGroupBox*   contGroup  = new QGroupBox(tr("Continuum Sources"), this);
    QVBoxLayout* contLayout = new QVBoxLayout(contGroup);

    createSlotUI(contLayout, m_redStarry,    tr("Red"),              "Red");
    createSlotUI(contLayout, m_redStarless,  tr("Red (Starless)"),   "Red (Starless)");
    createSlotUI(contLayout, m_greenStarry,  tr("Green"),            "Green");
    createSlotUI(contLayout, m_greenStarless,tr("Green (Starless)"), "Green (Starless)");
    createSlotUI(contLayout, m_oscStarry,    tr("OSC"),              "OSC");
    createSlotUI(contLayout, m_oscStarless,  tr("OSC (Starless)"),   "OSC (Starless)");

    // Column 3: Processing parameters
    QGroupBox*   paramGroup  = new QGroupBox(tr("Parameters"), this);
    QVBoxLayout* paramLayout = new QVBoxLayout(paramGroup);

    // Q-Factor - controls the continuum subtraction strength
    QHBoxLayout* qRow   = new QHBoxLayout();
    QLabel*      qLabel = new QLabel(tr("Q-Factor:"), this);

    m_qFactorSpin = new QDoubleSpinBox(this);
    m_qFactorSpin->setRange(0.10, 2.00);
    m_qFactorSpin->setSingleStep(0.05);
    m_qFactorSpin->setDecimals(2);
    m_qFactorSpin->setValue(0.80);

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

    // Output mode
    m_outputLinearCheck = new QCheckBox(tr("Output Linear Image Only"), this);
    m_outputLinearCheck->setChecked(true);
    paramLayout->addWidget(m_outputLinearCheck);

    // Optional post-subtraction denoising via Cosmic Clarity
    m_denoiseCheck = new QCheckBox(tr("Denoise with Cosmic Clarity (0.9)"), this);
    m_denoiseCheck->setToolTip(
        tr("Runs Cosmic Clarity denoise on the linear continuum-subtracted image "
           "before any non-linear stretch."));
    m_denoiseCheck->setChecked(false);
    paramLayout->addWidget(m_denoiseCheck);

    // Advanced settings sub-group
    QGroupBox*   settingsGroup = new QGroupBox(tr("Settings"), this);
    QFormLayout* advLayout     = new QFormLayout(settingsGroup);

    m_thresholdSpin = new QDoubleSpinBox(this);
    m_thresholdSpin->setRange(0.5, 50.0);
    m_thresholdSpin->setDecimals(1);
    m_thresholdSpin->setValue(5.0);
    advLayout->addRow(tr("WB Star Threshold:"), m_thresholdSpin);

    m_curvesSpin = new QDoubleSpinBox(this);
    m_curvesSpin->setRange(0.0, 1.0);
    m_curvesSpin->setDecimals(2);
    m_curvesSpin->setSingleStep(0.05);
    m_curvesSpin->setValue(0.50);
    advLayout->addRow(tr("Curves Boost:"), m_curvesSpin);

    paramLayout->addWidget(settingsGroup);

    // Pipeline description label
    QLabel* infoLabel = new QLabel(
        tr("Pipeline: BG Neutralization > Red-to-Green Normalization > "
           "Star WB > Linear Subtraction (NB - Q*(Cont - median)) > Stretch"),
        this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-style: italic;");
    paramLayout->addWidget(infoLabel);
    paramLayout->addStretch(1);

    // Assemble columns
    columnsLayout->addWidget(nbGroup,    1);
    columnsLayout->addWidget(contGroup,  1);
    columnsLayout->addWidget(paramGroup, 1);
    mainLayout->addLayout(columnsLayout);

    // --- Status row ---
    m_statusLabel = new QLabel("", this);
    m_progress    = new QProgressBar(this);
    m_progress->setRange(0, 0); // Indeterminate spinner during processing
    m_progress->setVisible(false);

    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_progress);

    // --- Bottom action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* clearBtn   = new QPushButton(tr("Clear All"), this);
    QPushButton* refreshBtn = new QPushButton(tr("Refresh"),   this);
    m_executeBtn            = new QPushButton(tr("Execute"),   this);
    QPushButton* closeBtn   = new QPushButton(tr("Close"),     this);

    connect(clearBtn,    &QPushButton::clicked, this, &ContinuumSubtractionDialog::onClear);
    connect(refreshBtn,  &QPushButton::clicked, this, &ContinuumSubtractionDialog::refreshImageList);
    connect(m_executeBtn,&QPushButton::clicked, this, &ContinuumSubtractionDialog::onExecute);
    connect(closeBtn,    &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(clearBtn);
    btnLayout->addWidget(refreshBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(m_executeBtn);
    mainLayout->addLayout(btnLayout);

    // Center dialog relative to parent
    adjustSize();
    if (parent)
    {
        if (QWidget* parentWidget = qobject_cast<QWidget*>(parent))
        {
            move(parentWidget->x() + (parentWidget->width()  - width())  / 2,
                 parentWidget->y() + (parentWidget->height() - height()) / 2);
        }
    }
}

ContinuumSubtractionDialog::~ContinuumSubtractionDialog()
{
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}

// ----------------------------------------------------------------------------
// Public interface
// ----------------------------------------------------------------------------

void ContinuumSubtractionDialog::setViewer(ImageViewer* v)
{
    m_viewer = v;
}

// ============================================================================
// UI helpers
// ============================================================================

void ContinuumSubtractionDialog::createSlotUI(QVBoxLayout*   layout,
                                               ImageSlot&     slot,
                                               const QString& label,
                                               const QString& channel)
{
    slot.button = new QPushButton(tr("Load %1").arg(label), this);

    // Em dash as placeholder until an image is loaded
    slot.label = new QLabel(QString::fromUtf8("\xe2\x80\x94"), this);
    slot.label->setStyleSheet("color: gray;");

    connect(slot.button, &QPushButton::clicked, [this, channel]() {
        loadImage(channel);
    });

    layout->addWidget(slot.button);
    layout->addWidget(slot.label);
}

void ContinuumSubtractionDialog::populateCombo(QComboBox* combo)
{
    combo->clear();
    if (!m_mainWindow) return;

    ImageViewer* currentViewer = m_mainWindow->getCurrentViewer();
    if (!currentViewer) return;

    const auto subList = currentViewer->window()->findChildren<CustomMdiSubWindow*>();
    for (CustomMdiSubWindow* csw : subList)
    {
        ImageViewer* v = csw->viewer();
        if (!v || !v->getBuffer().isValid()) continue;
        if (csw->isToolWindow()) continue;

        combo->addItem(csw->windowTitle(),
                       QVariant::fromValue(reinterpret_cast<quintptr>(v)));
    }
}

void ContinuumSubtractionDialog::refreshImageList()
{
    // No per-slot combos exist in the current design; retained for API compatibility.
}

// ============================================================================
// Image loading
// ============================================================================

void ContinuumSubtractionDialog::loadImage(const QString& channel)
{
    // Prompt the user to choose the image source.
    bool ok = false;
    const QStringList sources = { tr("From View"), tr("From File") };

    const QString source = QInputDialog::getItem(
        this,
        tr("Select %1 Image Source").arg(channel),
        tr("Load image from:"),
        sources, 0, false, &ok);

    if (!ok) return;

    ImageBuffer buffer;
    QString     labelText;

    if (source == tr("From View"))
    {
        if (!m_mainWindow) return;

        ImageViewer* currentViewer = m_mainWindow->getCurrentViewer();
        if (!currentViewer)
        {
            QMessageBox::information(this, tr("Load"), tr("No open images."));
            return;
        }

        // Build a list of all valid, non-tool MDI sub-windows.
        const auto subList = currentViewer->window()->findChildren<CustomMdiSubWindow*>();

        QStringList          names;
        QList<ImageViewer*>  viewers;

        for (CustomMdiSubWindow* csw : subList)
        {
            ImageViewer* v = csw->viewer();
            if (!v || !v->getBuffer().isValid()) continue;
            if (csw->isToolWindow()) continue;

            names   << csw->windowTitle();
            viewers << v;
        }

        if (names.isEmpty())
        {
            QMessageBox::information(this, tr("Load"), tr("No open images."));
            return;
        }

        QString choice;
        if (names.size() == 1)
        {
            choice = names[0];
        }
        else
        {
            choice = QInputDialog::getItem(
                this,
                tr("Select View - %1").arg(channel),
                tr("Choose:"),
                names, 0, false, &ok);
            if (!ok) return;
        }

        const int idx = names.indexOf(choice);
        if (idx < 0) return;

        buffer    = viewers[idx]->getBuffer();
        labelText = choice;
    }
    else
    {
        // Load from disk
        const QString fileFilter =
            tr("Images (*.png *.tif *.tiff *.fits *.fit *.xisf)");

        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select %1 Image").arg(channel), QString(), fileFilter);

        if (path.isEmpty()) return;

        QString         err;
        const QFileInfo fi(path);
        const QString   ext = fi.suffix().toLower();

        if (ext == "fits" || ext == "fit")
        {
            if (!FitsLoader::load(path, buffer, &err))
            {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to load FITS: %1").arg(err));
                return;
            }
        }
        else if (ext == "xisf")
        {
            if (!XISFReader::read(path, buffer, &err))
            {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to load XISF: %1").arg(err));
                return;
            }
        }
        else if (ext == "tif" || ext == "tiff")
        {
            int                tw, th, tc;
            std::vector<float> tdata;
            if (!SimpleTiffReader::readFloat32(path, tw, th, tc, tdata, &err))
            {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to load TIFF: %1").arg(err));
                return;
            }
            buffer.setData(tw, th, tc, tdata);
        }
        else
        {
            // Fallback: use Qt image loader (PNG, etc.)
            QImage img(path);
            if (img.isNull())
            {
                QMessageBox::critical(this, tr("Error"), tr("Failed to load image."));
                return;
            }

            img = img.convertToFormat(QImage::Format_RGB888);
            const int iw = img.width();
            const int ih = img.height();

            std::vector<float> idata(iw * ih * 3);
            for (int y = 0; y < ih; ++y)
            {
                const uchar* line = img.scanLine(y);
                for (int x = 0; x < iw; ++x)
                {
                    idata[(y * iw + x) * 3 + 0] = line[x * 3 + 0] / 255.0f;
                    idata[(y * iw + x) * 3 + 1] = line[x * 3 + 1] / 255.0f;
                    idata[(y * iw + x) * 3 + 2] = line[x * 3 + 2] / 255.0f;
                }
            }
            buffer.setData(iw, ih, 3, idata);
        }

        labelText = fi.fileName();
    }

    if (!buffer.isValid())
    {
        QMessageBox::warning(this, tr("Load"), tr("Image is empty or invalid."));
        return;
    }

    // Map the channel string to the corresponding ImageSlot.
    struct SlotMapping { const char* key; ImageSlot* slot; };

    SlotMapping mappings[] = {
        { "Ha",               &m_haStarry    },
        { "Ha (Starless)",    &m_haStarless  },
        { "SII",              &m_siiStarry   },
        { "SII (Starless)",   &m_siiStarless },
        { "OIII",             &m_oiiiStarry  },
        { "OIII (Starless)",  &m_oiiiStarless},
        { "Red",              &m_redStarry   },
        { "Red (Starless)",   &m_redStarless },
        { "Green",            &m_greenStarry },
        { "Green (Starless)", &m_greenStarless},
        { "OSC",              &m_oscStarry   },
        { "OSC (Starless)",   &m_oscStarless },
        { "HaO3",             &m_hao3Starry  },
        { "HaO3 (Starless)",  &m_hao3Starless},
        { "S2O3",             &m_s2o3Starry  },
        { "S2O3 (Starless)",  &m_s2o3Starless},
    };

    bool found = false;
    for (const auto& m : mappings)
    {
        if (channel == m.key)
        {
            m.slot->buffer = buffer;
            m.slot->label->setText(labelText);
            m.slot->label->setStyleSheet("");

            // Trigger composite extraction for HaO3 / S2O3 inputs.
            const bool starless = channel.contains("Starless");
            if      (channel.startsWith("HaO3")) extractFromComposite("HaO3", starless, buffer);
            else if (channel.startsWith("S2O3")) extractFromComposite("S2O3", starless, buffer);

            m_statusLabel->setText(tr("Loaded %1: %2").arg(channel, labelText));
            found = true;
            break;
        }
    }

    if (!found)
        QMessageBox::critical(this, tr("Error"), tr("Unknown channel: %1").arg(channel));
}

// ============================================================================
// Composite extraction  (HaO3 -> Ha + OIII,  S2O3 -> SII + OIII)
// ============================================================================

void ContinuumSubtractionDialog::extractFromComposite(
    const QString&     composite,
    bool               starless,
    const ImageBuffer& img)
{
    if (!img.isValid() || img.channels() < 3)
    {
        QMessageBox::warning(
            this, tr("Composite Load"),
            tr("%1 expects a 3-channel color image (R=emission, G=OIII). "
               "Cannot extract channels.").arg(composite));
        return;
    }

    auto channels = ChannelOps::extractChannels(img);
    if (channels.size() < 2) return;

    const ImageBuffer rCh = channels[0]; // R = Ha or SII
    const ImageBuffer gCh = channels[1]; // G = OIII

    // Store the emission channel (R) into the appropriate NB slot.
    if (composite == "HaO3")
    {
        ImageSlot& slot = starless ? m_haStarless : m_haStarry;
        slot.buffer = rCh;
        slot.label->setText(starless ? tr("Ha from HaO3 (starless)") : tr("Ha from HaO3 [R]"));
        slot.label->setStyleSheet("");
    }
    else if (composite == "S2O3")
    {
        ImageSlot& slot = starless ? m_siiStarless : m_siiStarry;
        slot.buffer = rCh;
        slot.label->setText(starless ? tr("SII from S2O3 (starless)") : tr("SII from S2O3 [R]"));
        slot.label->setStyleSheet("");
    }

    // Merge the OIII channel (G) with any previously extracted OIII data.
    // Averaging matches the Python _update_oiii_from_composites behaviour.
    ImageSlot& oiiiSlot = starless ? m_oiiiStarless : m_oiiiStarry;
    const QString suffix = starless ? tr(" (starless)") : QString();

    if (oiiiSlot.loaded())
    {
        const auto& existData = oiiiSlot.buffer.data();
        const auto& newData   = gCh.data();
        const int   ow = oiiiSlot.buffer.width();
        const int   oh = oiiiSlot.buffer.height();

        if (ow == gCh.width() && oh == gCh.height())
        {
            std::vector<float> avg(existData.size());
            for (size_t i = 0; i < avg.size(); ++i)
                avg[i] = (existData[i] + newData[i]) * 0.5f;

            oiiiSlot.buffer.setData(ow, oh, 1, avg);
            oiiiSlot.label->setText(
                tr("OIII from %1 (averaged)%2").arg(composite, suffix));
        }
        else
        {
            oiiiSlot.buffer = gCh;
            oiiiSlot.label->setText(
                tr("OIII from %1%2").arg(composite, suffix));
        }
    }
    else
    {
        oiiiSlot.buffer = gCh;
        oiiiSlot.label->setText(tr("OIII from %1%2").arg(composite, suffix));
    }

    oiiiSlot.label->setStyleSheet("");
}

// ============================================================================
// Continuum resolution
// ============================================================================

bool ContinuumSubtractionDialog::getContinuumForFilter(
    const QString& filter,
    bool           starless,
    ImageBuffer&   outCont)
{
    // Ha / SII use the Red channel; OIII uses the Green channel.
    const bool useGreen = (filter == "OIII");

    // Try the dedicated Red or Green slot first.
    const ImageSlot& directSlot = useGreen
        ? (starless ? m_greenStarless : m_greenStarry)
        : (starless ? m_redStarless   : m_redStarry);

    if (directSlot.loaded())
    {
        outCont = directSlot.buffer;
        return true;
    }

    // Fallback: extract the appropriate channel from an OSC image.
    const ImageSlot& oscSlot = starless ? m_oscStarless : m_oscStarry;
    if (oscSlot.loaded() && oscSlot.buffer.channels() >= 3)
    {
        auto chans = ChannelOps::extractChannels(oscSlot.buffer);
        if (chans.size() >= 2)
        {
            outCont = useGreen ? chans[1] : chans[0];
            return true;
        }
    }

    return false;
}

// ============================================================================
// Q-Factor slider / spinbox synchronization
// ============================================================================

void ContinuumSubtractionDialog::onQFactorChanged(double val)
{
    QSignalBlocker b(m_qFactorSlider);
    m_qFactorSlider->setValue(static_cast<int>(val * 100));
}

// ============================================================================
// Clear all loaded images
// ============================================================================

void ContinuumSubtractionDialog::onClear()
{
    ImageSlot* allSlots[] = {
        &m_haStarry,   &m_haStarless,
        &m_siiStarry,  &m_siiStarless,
        &m_oiiiStarry, &m_oiiiStarless,
        &m_hao3Starry, &m_hao3Starless,
        &m_s2o3Starry, &m_s2o3Starless,
        &m_redStarry,  &m_redStarless,
        &m_greenStarry,&m_greenStarless,
        &m_oscStarry,  &m_oscStarless,
    };

    for (auto* s : allSlots)
        s->clear();

    m_statusLabel->setText(tr("All loaded images cleared."));
}

// ============================================================================
// Execute - build task list and launch the worker thread
// ============================================================================

void ContinuumSubtractionDialog::onExecute()
{
    std::vector<ContinuumSubtractWorker::Task> tasks;

    struct FilterDef
    {
        QString    name;
        ImageSlot* nbStarry;
        ImageSlot* nbStarless;
    };

    const FilterDef filters[] = {
        { "Ha",   &m_haStarry,   &m_haStarless   },
        { "SII",  &m_siiStarry,  &m_siiStarless  },
        { "OIII", &m_oiiiStarry, &m_oiiiStarless },
    };

    for (const auto& f : filters)
    {
        ContinuumSubtractWorker::Task task;
        task.name = f.name;

        ImageBuffer contStarry, contStarless;
        const bool hasContStarry   = getContinuumForFilter(f.name, false, contStarry);
        const bool hasContStarless = getContinuumForFilter(f.name, true,  contStarless);
        const bool hasNBStarry     = f.nbStarry->loaded();
        const bool hasNBStarless   = f.nbStarless->loaded();
        const bool hasStarryPair   = hasNBStarry   && hasContStarry;
        const bool hasStarlessPair = hasNBStarless && hasContStarless;

        if (!hasStarryPair && !hasStarlessPair)
            continue; // Skip this filter - no complete image pair available

        if (hasStarryPair)
        {
            task.nbStarry   = f.nbStarry->buffer;
            task.contStarry = contStarry;
        }

        if (hasStarlessPair)
        {
            task.nbStarless   = f.nbStarless->buffer;
            task.contStarless = contStarless;
        }

        // Validate that NB and continuum images share the same dimensions.
        if (hasStarryPair &&
            (task.nbStarry.width()  != task.contStarry.width() ||
             task.nbStarry.height() != task.contStarry.height()))
        {
            QMessageBox::warning(
                this, tr("Dimension Mismatch"),
                tr("%1 starry: NB (%2x%3) and Continuum (%4x%5) dimensions must match.")
                    .arg(f.name)
                    .arg(task.nbStarry.width()).arg(task.nbStarry.height())
                    .arg(task.contStarry.width()).arg(task.contStarry.height()));
            continue;
        }

        if (hasStarlessPair &&
            (task.nbStarless.width()  != task.contStarless.width() ||
             task.nbStarless.height() != task.contStarless.height()))
        {
            QMessageBox::warning(
                this, tr("Dimension Mismatch"),
                tr("%1 starless: NB (%2x%3) and Continuum (%4x%5) dimensions must match.")
                    .arg(f.name)
                    .arg(task.nbStarless.width()).arg(task.nbStarless.height())
                    .arg(task.contStarless.width()).arg(task.contStarless.height()));
            continue;
        }

        task.starlessOnly = (!hasStarryPair && hasStarlessPair);
        tasks.push_back(std::move(task));
    }

    if (tasks.empty())
    {
        m_statusLabel->setText(
            tr("Load at least one NB filter + matching continuum (or OSC)."));
        return;
    }

    // Assemble processing parameters from the UI.
    ContinuumSubtractParams params;
    params.qFactor       = static_cast<float>(m_qFactorSpin->value());
    params.starThreshold = static_cast<float>(m_thresholdSpin->value());
    params.outputLinear  = m_outputLinearCheck->isChecked();
    params.targetMedian  = 0.25f;
    params.curvesBoost   = static_cast<float>(m_curvesSpin->value());

    const bool denoiseCC = m_denoiseCheck->isChecked();

    // Disable controls and show progress during processing.
    m_executeBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_statusLabel->setText(tr("Processing..."));

    // Clean up any previous worker before starting a new one.
    if (m_worker)
    {
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
// Worker callbacks
// ============================================================================

void ContinuumSubtractionDialog::onResultReady(const QString&     name,
                                                const ImageBuffer& result)
{
    const QString title = QString("%1_ContSub").arg(name);

    if (m_mainWindow)
    {
        m_mainWindow->createResultWindow(result, title);
        m_mainWindow->logMessage(
            tr("Continuum Subtraction: created '%1'").arg(title), 0, false);
    }
}

void ContinuumSubtractionDialog::onWorkerStatus(const QString& msg)
{
    m_statusLabel->setText(msg);
}

void ContinuumSubtractionDialog::onWorkerDone()
{
    m_progress->setVisible(false);
    m_executeBtn->setEnabled(true);
    m_statusLabel->setText(tr("Done."));
}