// =============================================================================
// StarNetDialog.cpp
// Implements the StarNet++ star removal dialog. Runs StarNet in a background
// thread, optionally generating a star mask via screen/descreen subtraction.
// =============================================================================

#include "StarNetDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../algos/StarNetRunner.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QGroupBox>
#include <QDebug>
#include <QIcon>
#include <QThread>
#include <QProgressDialog>
#include <QLibrary>
#include <QSettings>

// -----------------------------------------------------------------------------
// Utility: Detect whether a compatible GPU runtime (CUDA or DirectML) is
// available on the current system.
// -----------------------------------------------------------------------------
static bool detectGpuAvailable()
{
    bool hasCuda    = QLibrary("nvcuda").load();
    bool hasDirectML = QLibrary("DirectML").load();
    return hasCuda || hasDirectML;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
StarNetDialog::StarNetDialog(QWidget* parent)
    : DialogBase(parent, tr("StarNet++ Star Removal"), 300, 200)
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // --- Parameter group ---
    QGroupBox*   grp       = new QGroupBox(tr("Parameters"), this);
    QVBoxLayout* grpLayout = new QVBoxLayout(grp);

    // Linear data checkbox
    m_chkLinear = new QCheckBox(tr("Linear Data (Pre-stretch)"), this);
    m_chkLinear->setChecked(true);
    m_chkLinear->setToolTip(
        tr("If checked, applies an auto-stretch before StarNet and inverts it afterwards.\n"
           "Essential for linear images."));

    // Star mask generation checkbox
    m_chkGenerateMask = new QCheckBox(tr("Generate Star Mask"), this);
    m_chkGenerateMask->setChecked(false);

    // GPU acceleration checkbox (persisted in QSettings)
    m_chkGpu = new QCheckBox(tr("Use GPU"), this);
    m_chkGpu->setChecked(
        QSettings().value("StarNet/useGpu", detectGpuAvailable()).toBool());
    m_chkGpu->setToolTip(tr("Highly recommended if compatible hardware is detected."));

    // Stride spin box
    QHBoxLayout* strideLayout = new QHBoxLayout();
    strideLayout->addWidget(new QLabel(tr("Stride:")));

    m_spinStride = new QSpinBox(this);
    m_spinStride->setRange(16, 2048);
    m_spinStride->setValue(256);
    m_spinStride->setSingleStep(32);
    strideLayout->addWidget(m_spinStride);

    grpLayout->addWidget(m_chkLinear);
    grpLayout->addWidget(m_chkGenerateMask);
    grpLayout->addWidget(m_chkGpu);
    grpLayout->addLayout(strideLayout);

    layout->addWidget(grp);

    // --- Action buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_btnRun = new QPushButton(tr("Run"), this);
    QPushButton* btnClose = new QPushButton(tr("Close"), this);

    connect(m_btnRun, &QPushButton::clicked, this, &StarNetDialog::onRun);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);

    btnLayout->addStretch();
    btnLayout->addWidget(btnClose);
    btnLayout->addWidget(m_btnRun);

    layout->addLayout(btnLayout);
}

// -----------------------------------------------------------------------------
// onRun  --  Executes StarNet++ processing in a background thread.
// On completion, creates a starless result window and, optionally, a star mask.
// -----------------------------------------------------------------------------
void StarNetDialog::onRun()
{
    MainWindowCallbacks* mw = getCallbacks();

    // Validate that an image is loaded
    if (!mw || !mw->getCurrentViewer() ||
        !mw->getCurrentViewer()->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("Error"), tr("No image loaded (StarNet)."));
        return;
    }

    // Collect parameters from the UI
    StarNetParams params;
    params.isLinear     = m_chkLinear->isChecked();
    params.generateMask = m_chkGenerateMask->isChecked();
    params.stride       = m_spinStride->value();
    params.useGpu       = m_chkGpu->isChecked();

    // Persist the GPU preference
    QSettings().setValue("StarNet/useGpu", params.useGpu);

    // Notify the main window that a long-running process has started
    mw->startLongProcess();
    mw->logMessage(tr("Starting StarNet++..."), 0, false);

    // Set up background thread and runner
    QThread*       thread = new QThread;
    StarNetRunner* runner = new StarNetRunner;
    runner->moveToThread(thread);

    // Forward process output to the application log
    connect(runner, &StarNetRunner::processOutput, this, [this](const QString& msg) {
        if (MainWindowCallbacks* cb = getCallbacks()) {
            cb->logMessage(msg, 0, false);
        }
    });

    ImageViewer* viewer = mw->getCurrentViewer();
    if (!viewer) {
        mw->endLongProcess();
        runner->deleteLater();
        thread->deleteLater();
        return;
    }

    // Pre-capture window titles before the background run to avoid
    // platform-specific focus issues that could change the active title.
    const QString starlessTitle =
        MainWindowCallbacks::buildChildTitle(viewer->windowTitle(), "_starless");
    const QString starmaskTitle =
        MainWindowCallbacks::buildChildTitle(viewer->windowTitle(), "_starmask");

    // Modal progress dialog
    QProgressDialog* pd = new QProgressDialog(
        tr("Running StarNet++..."), tr("Cancel"), 0, 0, this);
    pd->setWindowModality(Qt::WindowModal);
    pd->setMinimumDuration(0);
    pd->show();

    connect(pd, &QProgressDialog::canceled,
            runner, &StarNetRunner::cancel, Qt::DirectConnection);

    // Capture the input buffer by value for thread safety
    ImageBuffer input = viewer->getBuffer();

    // --- Start processing when the thread begins ---
    connect(thread, &QThread::started, runner, [=]() mutable {
        ImageBuffer starless;
        QString     errorMsg;
        bool ok = runner->run(input, starless, params, &errorMsg);

        // Marshal results back to the GUI thread
        QMetaObject::invokeMethod(this, [=]() mutable {
            pd->close();
            pd->deleteLater();

            thread->quit();
            thread->wait();
            thread->deleteLater();
            runner->deleteLater();

            if (MainWindowCallbacks* cb = getCallbacks()) {
                cb->endLongProcess();
            }

            if (ok) {
                // --- Success path ---
                if (MainWindowCallbacks* cb = getCallbacks()) {
                    cb->logMessage(
                        tr("StarNet completed successfully. Validating result..."), 1, true);

                    // Validate the starless result
                    if (starless.width() <= 0 || starless.height() <= 0 ||
                        starless.data().empty()) {
                        cb->logMessage(tr("ERR: StarNet result is empty!"), 3, true);
                        QMessageBox::critical(
                            this, tr("Error"), tr("StarNet produced an empty image."));
                    } else {
                        // Create the starless result window
                        starless.addHistoryAction(tr("StarNet++ Star Removal (starless)"));
                        cb->createResultWindow(starless, starlessTitle);
                        cb->logMessage(
                            tr("Starless image created: %1").arg(starlessTitle), 1, true);

                        // Optionally generate a star mask
                        if (params.generateMask) {
                            cb->logMessage(tr("Generating Star Mask..."), 0, false);

                            // Verify dimension compatibility
                            if (input.width()  != starless.width() ||
                                input.height() != starless.height()) {
                                cb->logMessage(
                                    tr("ERR: Dimension mismatch for mask."), 3, true);
                                QMessageBox::warning(
                                    this, tr("Warning"),
                                    tr("Could not generate mask due to size mismatch."));
                            } else {
                                // Compute star mask using descreen formula:
                                //   mask = (original - starless) / (1 - starless)
                                // This produces a more vibrant and faithful mask
                                // than simple subtraction.
                                size_t             totalPixels = input.data().size();
                                std::vector<float> maskData(totalPixels);
                                const auto&        od = input.data();
                                const auto&        sd = starless.data();

                                for (size_t i = 0; i < totalPixels; ++i) {
                                    float sll        = sd[i];
                                    float orig       = od[i];
                                    float denom      = 1.0f - sll;
                                    float subtracted = orig - sll;

                                    if (denom > 1e-6f) {
                                        maskData[i] = std::max(0.0f,
                                            std::min(1.0f, subtracted / denom));
                                    } else {
                                        maskData[i] = std::max(0.0f,
                                            std::min(1.0f, subtracted));
                                    }
                                }

                                ImageBuffer mask;
                                mask.setData(input.width(), input.height(),
                                             input.channels(), maskData);
                                mask.addHistoryAction(tr("StarNet++ Star Mask generation"));
                                cb->createResultWindow(mask, starmaskTitle);
                                cb->logMessage(
                                    tr("Star mask created: %1").arg(starmaskTitle),
                                    1, true);
                            }
                        }
                    }
                }
                accept();

            } else if (!errorMsg.isEmpty() &&
                       errorMsg != tr("StarNet process cancelled by user.")) {
                // --- Error path ---
                if (MainWindowCallbacks* cb = getCallbacks()) {
                    cb->logMessage(
                        tr("ERR: StarNet failed: %1").arg(errorMsg), 3, true);
                }
                QMessageBox::critical(this, tr("StarNet Error"), errorMsg);

            } else if (errorMsg == tr("StarNet process cancelled by user.")) {
                // --- Cancellation path ---
                if (MainWindowCallbacks* cb = getCallbacks()) {
                    cb->logMessage(tr("StarNet cancelled."), 0, true);
                }
            }
        });
    });

    thread->start();
}