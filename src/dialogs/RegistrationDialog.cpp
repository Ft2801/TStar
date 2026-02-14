
#include "RegistrationDialog.h"
#include "../MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QSettings>

RegistrationDialog::RegistrationDialog(MainWindow* parent)
    : QDialog(parent)
    , m_mainWindow(parent)
{
    setWindowTitle(tr("Image Registration"));
    setMinimumSize(800, 600);
    resize(900, 700);
    
    setupUI();
    
    if (parent) {
        move(parent->geometry().center() - rect().center());
    }
}

RegistrationDialog::~RegistrationDialog() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
        m_worker->wait();
    }
}

void RegistrationDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Sequence group
    m_sequenceGroup = new QGroupBox(tr("Image Sequence"), this);
    QVBoxLayout* seqLayout = new QVBoxLayout(m_sequenceGroup);
    
    QHBoxLayout* toolbar = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load Folder..."), this);
    connect(m_loadBtn, &QPushButton::clicked, this, &RegistrationDialog::onLoadSequence);
    toolbar->addWidget(m_loadBtn);
    
    toolbar->addStretch();
    
    m_referenceLbl = new QLabel(tr("Reference: Not set"), this);
    toolbar->addWidget(m_referenceLbl);
    
    m_setRefBtn = new QPushButton(tr("Set Reference"), this);
    connect(m_setRefBtn, &QPushButton::clicked, this, &RegistrationDialog::onSetReference);
    toolbar->addWidget(m_setRefBtn);
    
    m_autoRefBtn = new QPushButton(tr("Auto"), this);
    connect(m_autoRefBtn, &QPushButton::clicked, this, &RegistrationDialog::onAutoFindReference);
    toolbar->addWidget(m_autoRefBtn);
    
    seqLayout->addLayout(toolbar);
    
    m_imageTable = new QTableWidget(this);
    m_imageTable->setColumnCount(5);
    m_imageTable->setHorizontalHeaderLabels({
        tr("Filename"), tr("Status"), tr("Shift X"), tr("Shift Y"), tr("Stars")
    });
    m_imageTable->horizontalHeader()->setStretchLastSection(true);
    m_imageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_imageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_imageTable->setAlternatingRowColors(true);
    seqLayout->addWidget(m_imageTable);
    
    mainLayout->addWidget(m_sequenceGroup, 1);
    
    // Parameters group
    m_paramsGroup = new QGroupBox(tr("Registration Parameters"), this);
    QGridLayout* paramLayout = new QGridLayout(m_paramsGroup);
    
    // Row 0: Detection σ and Min Stars
    paramLayout->addWidget(new QLabel(tr("Detection σ:"), this), 0, 0);
    m_detectionSigma = new QDoubleSpinBox(this);
    m_detectionSigma->setRange(1.0, 10.0);
    m_detectionSigma->setValue(4.0);
    m_detectionSigma->setDecimals(1);
    paramLayout->addWidget(m_detectionSigma, 0, 1);
    
    paramLayout->addWidget(new QLabel(tr("Min Stars:"), this), 0, 2);
    m_minStars = new QSpinBox(this);
    m_minStars->setRange(5, 100);
    m_minStars->setValue(20);
    paramLayout->addWidget(m_minStars, 0, 3);
    
    // Row 1: Max Stars and Match Tolerance
    paramLayout->addWidget(new QLabel(tr("Max Stars:"), this), 1, 0);
    m_maxStars = new QSpinBox(this);
    m_maxStars->setRange(50, 2000);
    m_maxStars->setValue(500);
    paramLayout->addWidget(m_maxStars, 1, 1);
    
    paramLayout->addWidget(new QLabel(tr("Match Tolerance:"), this), 1, 2);
    m_matchTolerance = new QDoubleSpinBox(this);
    m_matchTolerance->setRange(0.0001, 0.01);
    m_matchTolerance->setValue(0.002);
    m_matchTolerance->setDecimals(4);
    paramLayout->addWidget(m_matchTolerance, 1, 3);
    
    // Row 2: Checkboxes
    m_allowRotation = new QCheckBox(tr("Allow Rotation"), this);
    m_allowRotation->setChecked(true);
    paramLayout->addWidget(m_allowRotation, 2, 0, 1, 2);
    
    m_highPrecision = new QCheckBox(tr("High Precision (subpixel)"), this);
    m_highPrecision->setChecked(true);
    paramLayout->addWidget(m_highPrecision, 2, 2, 1, 2);
    
    // Row 3: Output Directory
    paramLayout->addWidget(new QLabel(tr("Output Directory:"), this), 3, 0);
    m_outputDir = new QLineEdit(this);
    m_outputDir->setPlaceholderText(tr("Leave empty for source folder"));
    paramLayout->addWidget(m_outputDir, 3, 1, 1, 2);
    
    QPushButton* browseDirBtn = new QPushButton(tr("..."), this);
    connect(browseDirBtn, &QPushButton::clicked, this, [this](){
        QSettings settings("TStar", "TStar");
        QString initialDir = settings.value("Registration/OutputFolder", m_outputDir->text()).toString();
        
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), initialDir);
        if (!dir.isEmpty()) {
            m_outputDir->setText(dir);
            settings.setValue("Registration/OutputFolder", dir);
        }
    });
    paramLayout->addWidget(browseDirBtn, 3, 3);
    
    mainLayout->addWidget(m_paramsGroup);
    
    // Progress group
    m_progressGroup = new QGroupBox(tr("Progress"), this);
    QVBoxLayout* progLayout = new QVBoxLayout(m_progressGroup);
    
    m_progressBar = new QProgressBar(this);
    progLayout->addWidget(m_progressBar);
    
    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    m_logText->setMaximumHeight(100);
    progLayout->addWidget(m_logText);
    
    m_summaryLabel = new QLabel(this);
    progLayout->addWidget(m_summaryLabel);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_startBtn = new QPushButton(tr("Start Registration"), this);
    connect(m_startBtn, &QPushButton::clicked, this, &RegistrationDialog::onStartRegistration);
    buttonLayout->addWidget(m_startBtn);
    
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &RegistrationDialog::onCancel);
    buttonLayout->addWidget(m_cancelBtn);
    
    progLayout->addLayout(buttonLayout);
    
    mainLayout->addWidget(m_progressGroup);
}

void RegistrationDialog::onLoadSequence() {
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Registration/InputFolder", QDir::currentPath()).toString();

    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Select Image Folder"),
        initialDir);
    
    if (dir.isEmpty()) return;
    
    settings.setValue("Registration/InputFolder", dir);
    
    m_sequence = std::make_unique<Stacking::ImageSequence>();
    
    QStringList filters;
    filters << "*.fit" << "*.fits" << "*.fts" << "*.tif" << "*.tiff";
    
    bool success = m_sequence->loadFromDirectory(dir, filters,
        [this]([[maybe_unused]] const QString& msg, double pct) {
            m_progressBar->setValue(static_cast<int>(pct * 100));
            QApplication::processEvents();
        });
    
    if (success) {
        updateTable();
        updateReferenceLabel();
        m_logText->append(tr("Loaded %1 images").arg(m_sequence->count()));
    } else {
        m_sequence.reset();
        m_logText->append(tr("<span style='color:red'>Failed to load sequence</span>"));
    }
    
    m_progressBar->setValue(0);
}

void RegistrationDialog::setSequence(std::unique_ptr<Stacking::ImageSequence> sequence) {
    m_sequence = std::move(sequence);
    updateTable();
    updateReferenceLabel();
}

void RegistrationDialog::updateTable() {
    m_imageTable->setRowCount(0);
    if (!m_sequence) return;
    
    m_imageTable->setRowCount(m_sequence->count());
    
    for (int i = 0; i < m_sequence->count(); ++i) {
        const auto& img = m_sequence->image(i);
        
        QString filename = img.fileName();
        if (i == m_sequence->referenceImage()) {
            filename += " ★";
        }
        m_imageTable->setItem(i, 0, new QTableWidgetItem(filename));
        
        QString status = img.registration.hasRegistration ? tr("OK") : tr("-");
        m_imageTable->setItem(i, 1, new QTableWidgetItem(status));
        
        if (img.registration.hasRegistration) {
            m_imageTable->setItem(i, 2, new QTableWidgetItem(
                QString::number(img.registration.shiftX, 'f', 1)));
            m_imageTable->setItem(i, 3, new QTableWidgetItem(
                QString::number(img.registration.shiftY, 'f', 1)));
        } else {
            m_imageTable->setItem(i, 2, new QTableWidgetItem("-"));
            m_imageTable->setItem(i, 3, new QTableWidgetItem("-"));
        }
        
        m_imageTable->setItem(i, 4, new QTableWidgetItem("-"));
    }
}

void RegistrationDialog::updateReferenceLabel() {
    if (m_sequence && m_sequence->count() > 0) {
        int ref = m_sequence->referenceImage();
        m_referenceLbl->setText(tr("Reference: %1 (%2)")
            .arg(m_sequence->image(ref).fileName())
            .arg(ref + 1));
    } else {
        m_referenceLbl->setText(tr("Reference: Not set"));
    }
}

void RegistrationDialog::onSetReference() {
    if (!m_sequence) return;
    
    int row = m_imageTable->currentRow();
    if (row >= 0) {
        m_sequence->setReferenceImage(row);
        updateTable();
        updateReferenceLabel();
    }
}

void RegistrationDialog::onAutoFindReference() {
    if (!m_sequence) return;
    
    int best = m_sequence->findBestReference();
    m_sequence->setReferenceImage(best);
    updateTable();
    updateReferenceLabel();
    m_logText->append(tr("Auto-selected reference: %1").arg(m_sequence->image(best).fileName()));
}

Stacking::RegistrationParams RegistrationDialog::gatherParams() const {
    Stacking::RegistrationParams params;
    params.detectionThreshold = static_cast<float>(m_detectionSigma->value());
    params.minStars = m_minStars->value();
    params.maxStars = m_maxStars->value();
    params.matchTolerance = static_cast<float>(m_matchTolerance->value());
    params.allowRotation = m_allowRotation->isChecked();
    params.highPrecision = m_highPrecision->isChecked();
    params.outputDirectory = m_outputDir->text();
    return params;
}

void RegistrationDialog::onStartRegistration() {
    if (!m_sequence || m_sequence->count() < 2) {
        QMessageBox::warning(this, tr("Cannot Register"),
            tr("Please load at least 2 images."));
        return;
    }
    
    m_isRunning = true;
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_logText->clear();
    
    Stacking::RegistrationParams params = gatherParams();
    
    m_worker = std::make_unique<Stacking::RegistrationWorker>(
        m_sequence.get(), params, m_sequence->referenceImage());
    
    connect(m_worker.get(), &Stacking::RegistrationWorker::progressChanged,
            this, &RegistrationDialog::onProgressChanged);
    connect(m_worker.get(), &Stacking::RegistrationWorker::logMessage,
            this, &RegistrationDialog::onLogMessage);
    connect(m_worker.get(), &Stacking::RegistrationWorker::imageRegistered,
            this, &RegistrationDialog::onImageRegistered);
    connect(m_worker.get(), &Stacking::RegistrationWorker::finished,
            this, &RegistrationDialog::onFinished);
    
    m_logText->append(tr("Starting registration..."));
    m_worker->start();
}

void RegistrationDialog::onCancel() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
    }
}

void RegistrationDialog::onProgressChanged([[maybe_unused]] const QString& message, double progress) {
    if (progress >= 0) {
        m_progressBar->setValue(static_cast<int>(progress * 100));
    }
}

void RegistrationDialog::onLogMessage(const QString& message, const QString& color) {
    QString finalColor = color;
    if (finalColor.toLower() == "neutral") finalColor = "";

    if (finalColor.isEmpty()) {
        m_logText->append(message);
    } else {
        m_logText->append(QString("<span style='color:%1'>%2</span>").arg(finalColor, message));
    }
}

void RegistrationDialog::onImageRegistered(int index, bool success) {
    if (index >= 0 && index < m_imageTable->rowCount()) {
        QTableWidgetItem* item = m_imageTable->item(index, 1);
        if (item) {
            item->setText(success ? tr("OK") : tr("FAIL"));
            item->setForeground(success ? Qt::green : Qt::red);
        }
        
        if (success && m_sequence) {
            const auto& img = m_sequence->image(index);
            if (img.registration.hasRegistration) {
                m_imageTable->item(index, 2)->setText(
                    QString::number(img.registration.shiftX, 'f', 1));
                m_imageTable->item(index, 3)->setText(
                    QString::number(img.registration.shiftY, 'f', 1));
            }
        }
    }
}

void RegistrationDialog::onFinished(int successCount) {
    m_isRunning = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setValue(100);
    
    int total = m_sequence ? m_sequence->count() : 0;
    m_summaryLabel->setText(tr("Registered %1 of %2 images successfully")
                           .arg(successCount).arg(total));
    
    if (successCount == total) {
        m_logText->append(tr("<span style='color:green'>Registration complete!</span>"));
    } else {
        m_logText->append(tr("<span style='color:salmon'>Registration finished with some failures</span>"));
    }
    
    m_worker.reset();
}
