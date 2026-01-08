#include "StarNetDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../algos/StarNetRunner.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QGroupBox>
#include <QDebug>
#include <QIcon>
#include <QThread>
#include <QProgressDialog>
#include <QLibrary>
#include <QSettings>

// Helper to detect best GPU provider
static bool detectGpuAvailable() {
    // Check for CUDA (NVIDIA) - look for nvcuda.dll
    bool hasCuda = QLibrary("nvcuda").load();
    // Check for DirectML (AMD/Intel) - available on Windows 10+
    bool hasDirectML = QLibrary("DirectML").load();
    return hasCuda || hasDirectML;
}

StarNetDialog::StarNetDialog(QWidget* parent) : DialogBase(parent, tr("StarNet++ Star Removal"), 300, 200) {


    QVBoxLayout* layout = new QVBoxLayout(this);

    QGroupBox* grp = new QGroupBox(tr("Parameters"), this);
    QVBoxLayout* grpLayout = new QVBoxLayout(grp);

    m_chkLinear = new QCheckBox(tr("Linear Data (Pre-stretch)"), this);
    m_chkLinear->setChecked(true);
    m_chkLinear->setToolTip(tr("If checked, applies an auto-stretch before StarNet and inverts it afterwards.\nEssential for linear images."));
    
    m_chkGenerateMask = new QCheckBox(tr("Generate Star Mask"), this);
    m_chkGenerateMask->setChecked(false);
    
    m_chkGpu = new QCheckBox(tr("Use GPU"), this);
    m_chkGpu->setChecked(QSettings().value("StarNet/useGpu", detectGpuAvailable()).toBool());
    m_chkGpu->setToolTip(tr("Highly recommended if compatible hardware is detected."));
    
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

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_btnRun = new QPushButton(tr("Run"), this);
    QPushButton* btnClose = new QPushButton(tr("Close"), this);
    
    connect(m_btnRun, &QPushButton::clicked, this, &StarNetDialog::onRun);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);

    btnLayout->addStretch();
    btnLayout->addWidget(m_btnRun);
    btnLayout->addWidget(btnClose);

    layout->addLayout(btnLayout);
}

void StarNetDialog::onRun() {
    MainWindowCallbacks* mw = getCallbacks();
    if (!mw || !mw->getCurrentViewer() || !mw->getCurrentViewer()->getBuffer().isValid()) {
        QMessageBox::warning(this, tr("Error"), tr("No image loaded (StarNet)."));
        return;
    }

    StarNetParams params;
    params.isLinear = m_chkLinear->isChecked();
    params.generateMask = m_chkGenerateMask->isChecked();
    params.stride = m_spinStride->value();
    params.useGpu = m_chkGpu->isChecked();

    QSettings().setValue("StarNet/useGpu", params.useGpu);

    mw->startLongProcess();
    mw->logMessage(tr("Starting StarNet++..."), 0, false);

    QThread* thread = new QThread;
    StarNetRunner* runner = new StarNetRunner;
    runner->moveToThread(thread);

    connect(runner, &StarNetRunner::processOutput, this, [this](const QString& msg){
        if (MainWindowCallbacks* mw = getCallbacks()) {
            mw->logMessage(msg, 0, false);
        }
    });

    ImageViewer* viewer = mw->getCurrentViewer();
    if (!viewer) {
        mw->endLongProcess();
        runner->deleteLater();
        thread->deleteLater();
        return;
    }
    
    QProgressDialog* pd = new QProgressDialog(tr("Running StarNet++..."), tr("Cancel"), 0, 0, this);
    pd->setWindowModality(Qt::WindowModal);
    pd->setMinimumDuration(0);
    pd->show();
             
    connect(pd, &QProgressDialog::canceled, runner, &StarNetRunner::cancel, Qt::DirectConnection);
    
    // Capture state
    ImageBuffer input = viewer->getBuffer();

    connect(thread, &QThread::started, runner, [=]() mutable {
        ImageBuffer starless;
        QString errorMsg;
        bool ok = runner->run(input, starless, params, &errorMsg);
        
        QMetaObject::invokeMethod(this, [=]() {
            pd->close();
            pd->deleteLater();
            
            thread->quit();
            thread->wait();
            thread->deleteLater();
            runner->deleteLater();
            
            if (MainWindowCallbacks* mw = getCallbacks()) {
                mw->endLongProcess();
            }
            
            if (ok) {
                if (MainWindowCallbacks* mw = getCallbacks()) {
                    mw->logMessage(tr("StarNet completed successfully. Validating result..."), 1, true);
                    if (starless.width() <= 0 || starless.height() <= 0 || starless.data().empty()) {
                         mw->logMessage(tr("ERR: StarNet result is empty!"), 3, true);
                         QMessageBox::critical(this, tr("Error"), tr("StarNet produced an empty image."));
                    } else {
                        mw->createResultWindow(starless, tr("Starless"));
                        if (params.generateMask) {
                            mw->logMessage(tr("Generating Star Mask..."), 0, false);
                            if (input.width() != starless.width() || input.height() != starless.height()) {
                                 mw->logMessage(tr("ERR: Dimension mismatch for mask."), 3, true);
                                 QMessageBox::warning(this, tr("Warning"), tr("Could not generate mask due to size mismatch."));
                            } else {
                                ImageBuffer mask;
                                size_t inputSize = input.data().size();
                                std::vector<float> maskData(inputSize);
                                const auto& od = input.data();
                                const auto& sd = starless.data();
                                for(size_t i=0; i<inputSize; ++i) {
                                    float val = od[i] - sd[i]; 
                                    maskData[i] = std::max(0.0f, val);
                                }
                                mask.setData(input.width(), input.height(), input.channels(), maskData);
                                mw->createResultWindow(mask, tr("Star Mask"));
                            }
                        }
                    }
                }
                accept();
            } else if (!errorMsg.isEmpty() && errorMsg != tr("StarNet process cancelled by user.")) {
                if (MainWindowCallbacks* mw = getCallbacks()) {
                    mw->logMessage(tr("ERR: StarNet failed: %1").arg(errorMsg), 3, true);
                }
                QMessageBox::critical(this, tr("StarNet Error"), errorMsg);
            } else if (errorMsg == tr("StarNet process cancelled by user.")) {
                if (MainWindowCallbacks* mw = getCallbacks()) {
                    mw->logMessage(tr("StarNet cancelled."), 0, true);
                }
            }
        });
    });
    
    thread->start();
}
