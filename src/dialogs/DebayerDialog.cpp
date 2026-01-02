#include "DebayerDialog.h"
#include "../ImageViewer.h"
#include "../ImageBuffer.h"
#include "../algos/ChannelOps.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QThread>

DebayerDialog::DebayerDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Debayer"));
    setMinimumWidth(350);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Pattern selection group
    QGroupBox* patternGroup = new QGroupBox(tr("Bayer Pattern"), this);
    QHBoxLayout* patternLayout = new QHBoxLayout(patternGroup);
    
    m_patternCombo = new QComboBox(this);
    m_patternCombo->addItem(tr("Auto (from header)"), QString("auto"));
    m_patternCombo->addItem("RGGB", QString("RGGB"));
    m_patternCombo->addItem("BGGR", QString("BGGR"));
    m_patternCombo->addItem("GRBG", QString("GRBG"));
    m_patternCombo->addItem("GBRG", QString("GBRG"));
    connect(m_patternCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &DebayerDialog::onPatternChanged);
    
    m_detectedLabel = new QLabel(tr("Detected: (none)"), this);
    m_detectedLabel->setStyleSheet("color: gray;");
    
    patternLayout->addWidget(m_patternCombo, 1);
    patternLayout->addWidget(m_detectedLabel);
    mainLayout->addWidget(patternGroup);
    
    // Method selection group
    QGroupBox* methodGroup = new QGroupBox(tr("Interpolation Method"), this);
    QHBoxLayout* methodLayout = new QHBoxLayout(methodGroup);
    
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Edge-aware"), QString("edge"));
    m_methodCombo->addItem(tr("Bilinear"), QString("bilinear"));
    
    methodLayout->addWidget(m_methodCombo);
    methodLayout->addStretch(1);
    mainLayout->addWidget(methodGroup);
    
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
    m_applyBtn = new QPushButton(tr("Apply"), this);
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    
    connect(m_applyBtn, &QPushButton::clicked, this, &DebayerDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch(1);
    btnLayout->addWidget(m_applyBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void DebayerDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
    updatePatternLabel();
}

void DebayerDialog::onPatternChanged(int) {
    updatePatternLabel();
}

void DebayerDialog::updatePatternLabel() {
    if (!m_viewer) {
        m_detectedLabel->setText(tr("Detected: (no image)"));
        return;
    }
    
    QString detected = detectPatternFromHeader();
    if (detected.isEmpty()) {
        m_detectedLabel->setText(tr("Detected: (unknown)"));
    } else {
        m_detectedLabel->setText(tr("Detected: %1").arg(detected));
    }
}

QString DebayerDialog::detectPatternFromHeader() {
    if (!m_viewer) return QString();
    
    const auto& meta = m_viewer->getBuffer().metadata();
    
    // Check common header keywords for Bayer pattern in rawHeaders
    QStringList keys = {"BAYERPAT", "BAYERPATN", "BAYER_PATTERN", "BAYERPATTERN", 
                        "CFAPATTERN", "CFA_PATTERN", "COLORTYPE"};
    
    for (const auto& hdr : meta.rawHeaders) {
        if (keys.contains(hdr.key)) {
            QString val = hdr.value.toUpper().trimmed();
            val.remove(',').remove(' ').remove('/').remove('|');
            
            // Check for valid patterns
            if (val == "RGGB" || val == "BGGR" || val == "GRBG" || val == "GBRG") {
                return val;
            }
            // Check if pattern is embedded in value
            for (const QString& pat : {"RGGB", "BGGR", "GRBG", "GBRG"}) {
                if (val.contains(pat)) return pat;
            }
        }
    }
    return QString();
}

QString DebayerDialog::autoDetectByScoring() {
    if (!m_viewer) return "RGGB";
    
    const ImageBuffer& buf = m_viewer->getBuffer();
    if (buf.channels() != 1) return "RGGB";
    
    // Use scoring algorithm to find best pattern
    QString bestPattern = "RGGB";
    float bestScore = std::numeric_limits<float>::max();
    
    for (const QString& pat : {"RGGB", "BGGR", "GRBG", "GBRG"}) {
        ImageBuffer rgb = ChannelOps::debayer(buf, pat.toStdString(), "bilinear");
        if (!rgb.isValid()) continue;
        
        float score = ChannelOps::computeDebayerScore(rgb);
        if (score < bestScore) {
            bestScore = score;
            bestPattern = pat;
        }
    }
    
    return bestPattern;
}

void DebayerDialog::onApply() {
    if (!m_viewer) {
        QMessageBox::warning(this, tr("Debayer"), tr("No image selected."));
        return;
    }
    
    const ImageBuffer& buf = m_viewer->getBuffer();
    
    // Check if already RGB
    if (buf.channels() >= 3) {
        QMessageBox::information(this, tr("Debayer"), tr("Image already has 3 channels."));
        return;
    }
    
    if (buf.channels() != 1) {
        QMessageBox::warning(this, tr("Debayer"), tr("Only single-channel mosaic images can be debayered."));
        return;
    }
    
    // Determine pattern
    QString pattern = m_patternCombo->currentData().toString();
    if (pattern == "auto") {
        pattern = detectPatternFromHeader();
        if (pattern.isEmpty()) {
            m_statusLabel->setText(tr("Auto-detecting pattern..."));
            m_progress->setVisible(true);
            m_progress->setValue(10);
            QApplication::processEvents();
            
            pattern = autoDetectByScoring();
            m_statusLabel->setText(tr("Detected: %1").arg(pattern));
        }
    }
    
    QString method = m_methodCombo->currentData().toString();
    
    m_statusLabel->setText(tr("Debayering (%1, %2)...").arg(pattern, method));
    m_progress->setVisible(true);
    m_progress->setValue(30);
    m_applyBtn->setEnabled(false);
    QApplication::processEvents();
    
    // Perform debayer
    ImageBuffer rgb = ChannelOps::debayer(buf, pattern.toStdString(), method.toStdString());
    
    m_progress->setValue(90);
    QApplication::processEvents();
    
    if (!rgb.isValid()) {
        QMessageBox::critical(this, tr("Debayer"), tr("Debayer failed."));
        m_statusLabel->setText(tr("Failed."));
        m_progress->setVisible(false);
        m_applyBtn->setEnabled(true);
        return;
    }
    
    // Apply result
    m_viewer->pushUndo();
    m_viewer->setBuffer(rgb);
    
    m_progress->setValue(100);
    m_statusLabel->setText(tr("Done."));
    m_applyBtn->setEnabled(true);
    
    accept();
}
