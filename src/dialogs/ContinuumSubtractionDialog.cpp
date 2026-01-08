#include "ContinuumSubtractionDialog.h"
#include "MainWindowCallbacks.h"
#include "DialogBase.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"
#include "../widgets/CustomMdiSubWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QApplication>
#include <QMdiArea>
#include <QMdiSubWindow>

ContinuumSubtractionDialog::ContinuumSubtractionDialog(QWidget* parent)
    : DialogBase(parent, tr("Continuum Subtraction")) {
    m_mainWindow = getCallbacks();
    setMinimumWidth(400);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Image selection group
    QGroupBox* selGroup = new QGroupBox(tr("Image Selection"), this);
    QFormLayout* selLayout = new QFormLayout(selGroup);
    
    m_narrowbandCombo = new QComboBox(this);
    m_continuumCombo = new QComboBox(this);
    
    selLayout->addRow(tr("Narrowband:"), m_narrowbandCombo);
    selLayout->addRow(tr("Continuum:"), m_continuumCombo);
    mainLayout->addWidget(selGroup);
    
    // Parameters group
    QGroupBox* paramGroup = new QGroupBox(tr("Parameters"), this);
    QVBoxLayout* paramLayout = new QVBoxLayout(paramGroup);
    
    QHBoxLayout* qRow = new QHBoxLayout();
    QLabel* qLabel = new QLabel(tr("Q-Factor:"), this);
    m_qFactorSpin = new QDoubleSpinBox(this);
    m_qFactorSpin->setRange(0.1, 2.0);
    m_qFactorSpin->setSingleStep(0.05);
    m_qFactorSpin->setValue(0.80);
    m_qFactorSpin->setDecimals(2);
    
    m_qFactorSlider = new QSlider(Qt::Horizontal, this);
    m_qFactorSlider->setRange(10, 200);
    m_qFactorSlider->setValue(80);
    
    connect(m_qFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ContinuumSubtractionDialog::onQFactorChanged);
    connect(m_qFactorSlider, &QSlider::valueChanged, [this](int val){
        m_qFactorSpin->setValue(val / 100.0);
    });
    
    qRow->addWidget(qLabel);
    qRow->addWidget(m_qFactorSlider, 1);
    qRow->addWidget(m_qFactorSpin);
    paramLayout->addLayout(qRow);
    
    m_outputLinearCheck = new QCheckBox(tr("Output Linear Only (skip stretch)"), this);
    m_outputLinearCheck->setChecked(true);
    paramLayout->addWidget(m_outputLinearCheck);
    
    mainLayout->addWidget(paramGroup);
    
    // Info label
    QLabel* infoLabel = new QLabel(tr("Formula: Result = NB - Q × (Continuum - median)"), this);
    infoLabel->setStyleSheet("color: gray; font-style: italic;");
    mainLayout->addWidget(infoLabel);
    
    // Status and progress
    m_statusLabel = new QLabel("", this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_progress);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* refreshBtn = new QPushButton(tr("Refresh"), this);
    m_applyBtn = new QPushButton(tr("Apply"), this);
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    
    connect(refreshBtn, &QPushButton::clicked, this, &ContinuumSubtractionDialog::refreshImageList);
    connect(m_applyBtn, &QPushButton::clicked, this, &ContinuumSubtractionDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addWidget(refreshBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(m_applyBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
    
    refreshImageList();

}

void ContinuumSubtractionDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

void ContinuumSubtractionDialog::onQFactorChanged(double val) {
    QSignalBlocker b(m_qFactorSlider);
    m_qFactorSlider->setValue(static_cast<int>(val * 100));
}

void ContinuumSubtractionDialog::refreshImageList() {
    m_narrowbandCombo->clear();
    m_continuumCombo->clear();
    
    if (!m_mainWindow) return;
    
    // Find MDI area and populate with open images
    ImageViewer* v_cur = m_mainWindow ? m_mainWindow->getCurrentViewer() : nullptr;
    if (!v_cur) return;
    auto sublist = v_cur->window()->findChildren<CustomMdiSubWindow*>();
    for (CustomMdiSubWindow* csw : sublist) {
        ImageViewer* v = csw->viewer();
        if (!v) continue;
        
        QString title = csw->windowTitle();
        m_narrowbandCombo->addItem(title, QVariant::fromValue(reinterpret_cast<quintptr>(v)));
        m_continuumCombo->addItem(title, QVariant::fromValue(reinterpret_cast<quintptr>(v)));
    }
    
    // Add channel extraction options for continuum
    m_continuumCombo->addItem(tr("-- Use Red Channel --"), QVariant(-1));
    m_continuumCombo->addItem(tr("-- Use Green Channel --"), QVariant(-2));
}

void ContinuumSubtractionDialog::onApply() {
    if (m_narrowbandCombo->count() == 0) {
        QMessageBox::warning(this, tr("Continuum Subtraction"), tr("No images available."));
        return;
    }
    
    // Get narrowband image
    quintptr nbPtr = m_narrowbandCombo->currentData().value<quintptr>();
    ImageViewer* nbViewer = reinterpret_cast<ImageViewer*>(nbPtr);
    if (!nbViewer) {
        QMessageBox::warning(this, tr("Continuum Subtraction"), tr("Invalid narrowband image."));
        return;
    }
    
    const ImageBuffer& nbBuf = nbViewer->getBuffer();
    if (!nbBuf.isValid()) {
        QMessageBox::warning(this, tr("Continuum Subtraction"), tr("Narrowband image is empty."));
        return;
    }
    
    // Get continuum image
    ImageBuffer contBuf;
    QVariant contData = m_continuumCombo->currentData();
    
    if (contData.toInt() == -1) {
        // Use red channel from narrowband
        if (nbBuf.channels() >= 3) {
            auto channels = ChannelOps::extractChannels(nbBuf);
            if (!channels.empty()) contBuf = channels[0];
        } else {
            contBuf = nbBuf;
        }
    } else if (contData.toInt() == -2) {
        // Use green channel from narrowband
        if (nbBuf.channels() >= 3) {
            auto channels = ChannelOps::extractChannels(nbBuf);
            if (channels.size() >= 2) contBuf = channels[1];
        } else {
            contBuf = nbBuf;
        }
    } else {
        quintptr contPtr = contData.value<quintptr>();
        ImageViewer* contViewer = reinterpret_cast<ImageViewer*>(contPtr);
        if (contViewer) {
            contBuf = contViewer->getBuffer();
        }
    }
    
    if (!contBuf.isValid()) {
        QMessageBox::warning(this, tr("Continuum Subtraction"), tr("Invalid continuum image."));
        return;
    }
    
    // Check dimensions
    if (nbBuf.width() != contBuf.width() || nbBuf.height() != contBuf.height()) {
        QMessageBox::warning(this, tr("Continuum Subtraction"), 
            tr("Image dimensions must match.\nNB: %1x%2, Cont: %3x%4")
            .arg(nbBuf.width()).arg(nbBuf.height())
            .arg(contBuf.width()).arg(contBuf.height()));
        return;
    }
    
    float qFactor = static_cast<float>(m_qFactorSpin->value());
    
    m_statusLabel->setText(tr("Processing (Q=%1)...").arg(qFactor, 0, 'f', 2));
    m_progress->setVisible(true);
    m_progress->setValue(30);
    m_applyBtn->setEnabled(false);
    QApplication::processEvents();
    
    // Perform subtraction
    ImageBuffer result = ChannelOps::continuumSubtract(nbBuf, contBuf, qFactor);
    
    m_progress->setValue(80);
    QApplication::processEvents();
    
    if (!result.isValid()) {
        QMessageBox::critical(this, tr("Continuum Subtraction"), tr("Processing failed."));
        m_statusLabel->setText(tr("Failed."));
        m_progress->setVisible(false);
        m_applyBtn->setEnabled(true);
        return;
    }
    
    // Create new window with result
    QString title = QString("%1_ContinuumSub").arg(m_narrowbandCombo->currentText());
    if (m_mainWindow) m_mainWindow->createResultWindow(result, title);
    
    m_progress->setValue(100);
    m_statusLabel->setText(tr("Done."));
    m_applyBtn->setEnabled(true);
    
    accept();
}
