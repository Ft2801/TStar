#include "ConversionDialog.h"
#include "../ImageBuffer.h"
#include "../io/FitsWrapper.h"
#include "../io/TiffIO.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QAtomicInt>
#include <mutex>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

ConversionDialog::ConversionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Convert RAW Files to FITS"));
    setMinimumSize(600, 500);
    setupUI();
}

void ConversionDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Info
    QLabel* infoLabel = new QLabel(tr(
        "Convert RAW camera files (CR2, NEF, ARW, DNG, etc.) to FITS format "
        "for astrophotography processing."
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #aaa; padding: 5px;");
    mainLayout->addWidget(infoLabel);
    
    // File List Group
    QGroupBox* filesGroup = new QGroupBox(tr("Input Files"));
    QVBoxLayout* filesLayout = new QVBoxLayout(filesGroup);
    
    m_fileList = new QListWidget();
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    filesLayout->addWidget(m_fileList);
    
    QHBoxLayout* fileButtonsLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("Add Files..."));
    m_removeBtn = new QPushButton(tr("Remove"));
    m_clearBtn = new QPushButton(tr("Clear All"));
    fileButtonsLayout->addWidget(m_addBtn);
    fileButtonsLayout->addWidget(m_removeBtn);
    fileButtonsLayout->addWidget(m_clearBtn);
    fileButtonsLayout->addStretch();
    filesLayout->addLayout(fileButtonsLayout);
    mainLayout->addWidget(filesGroup);
    
    // Output Settings Group
    QGroupBox* outputGroup = new QGroupBox(tr("Output Settings"));
    QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);
    
    // Output Directory
    QHBoxLayout* dirLayout = new QHBoxLayout();
    dirLayout->addWidget(new QLabel(tr("Output Directory:")));
    m_outputDir = new QLineEdit();
    m_outputDir->setPlaceholderText(tr("Same as input files"));
    dirLayout->addWidget(m_outputDir);
    m_browseBtn = new QPushButton(tr("Browse..."));
    dirLayout->addWidget(m_browseBtn);
    outputLayout->addLayout(dirLayout);
    
    // Format Options
    QHBoxLayout* optionsLayout = new QHBoxLayout();
    
    optionsLayout->addWidget(new QLabel(tr("Output Format:")));
    m_outputFormat = new QComboBox();
    m_outputFormat->addItems({"FITS", "XISF", "TIFF"});
    optionsLayout->addWidget(m_outputFormat);
    
    optionsLayout->addWidget(new QLabel(tr("Bit Depth:")));
    m_bitDepth = new QComboBox();
    m_bitDepth->addItems({"16-bit", "32-bit float"});
    m_bitDepth->setCurrentIndex(1);  // Default to 32-bit float for astrophotography
    optionsLayout->addWidget(m_bitDepth);
    
    optionsLayout->addStretch();
    outputLayout->addLayout(optionsLayout);
    
    // Debayer Option
    m_debayerCheck = new QCheckBox(tr("Apply debayering (for color cameras)"));
    m_debayerCheck->setChecked(true);
    outputLayout->addWidget(m_debayerCheck);
    
    mainLayout->addWidget(outputGroup);
    
    // Progress
    m_progress = new QProgressBar();
    m_progress->setTextVisible(true);
    m_progress->setValue(0);
    mainLayout->addWidget(m_progress);
    
    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(m_statusLabel);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_convertBtn = new QPushButton(tr("Convert"));
    m_convertBtn->setDefault(true);
    buttonLayout->addWidget(m_convertBtn);
    
    m_closeBtn = new QPushButton(tr("Close"));
    buttonLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(buttonLayout);
    
    // Connections
    connect(m_addBtn, &QPushButton::clicked, this, &ConversionDialog::onAddFiles);
    connect(m_removeBtn, &QPushButton::clicked, this, &ConversionDialog::onRemoveFiles);
    connect(m_clearBtn, &QPushButton::clicked, this, &ConversionDialog::onClearList);
    connect(m_browseBtn, &QPushButton::clicked, this, &ConversionDialog::onBrowseOutput);
    connect(m_convertBtn, &QPushButton::clicked, this, &ConversionDialog::onConvert);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_fileList, &QListWidget::itemSelectionChanged, this, &ConversionDialog::updateStatus);
}

void ConversionDialog::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this,
        tr("Select RAW Files"),
        QString(),
        tr("RAW Files (*.cr2 *.CR2 *.nef *.NEF *.arw *.ARW *.dng *.DNG "
           "*.orf *.ORF *.rw2 *.RW2 *.raf *.RAF *.pef *.PEF);;"
           "TIFF Files (*.tif *.tiff *.TIF *.TIFF);;"
           "All Files (*)"));
    
    for (const QString& file : files) {
        // Check if already in list
        bool exists = false;
        for (int i = 0; i < m_fileList->count(); ++i) {
            if (m_fileList->item(i)->data(Qt::UserRole).toString() == file) {
                exists = true;
                break;
            }
        }
        
        if (!exists) {
            QListWidgetItem* item = new QListWidgetItem(QFileInfo(file).fileName());
            item->setData(Qt::UserRole, file);
            m_fileList->addItem(item);
        }
    }
    
    updateStatus();
}

void ConversionDialog::onRemoveFiles() {
    qDeleteAll(m_fileList->selectedItems());
    updateStatus();
}

void ConversionDialog::onClearList() {
    m_fileList->clear();
    updateStatus();
}

void ConversionDialog::onBrowseOutput() {
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Select Output Directory"),
        m_outputDir->text());
    
    if (!dir.isEmpty()) {
        m_outputDir->setText(dir);
    }
}

void ConversionDialog::onConvert() {
    int count = m_fileList->count();
    if (count == 0) {
        QMessageBox::warning(this, tr("No Files"), tr("Please add files to convert."));
        return;
    }
    
    QString outDir = m_outputDir->text();
    int bitDepthVal = m_bitDepth->currentIndex() == 0 ? 16 : 32;
    Q_UNUSED(bitDepthVal);
    
    m_progress->setMaximum(count);
    m_progress->setValue(0);
    m_convertBtn->setEnabled(false);
    
    int converted = 0;
    for (int i = 0; i < count; ++i) {
        QString filePath = m_fileList->item(i)->data(Qt::UserRole).toString();
        QString targetDir = outDir.isEmpty() ? QFileInfo(filePath).absolutePath() : outDir;
        QString outPath = QDir(targetDir).absoluteFilePath(
            QFileInfo(filePath).completeBaseName() + ".fit");
        
        m_statusLabel->setText(tr("Converting: %1").arg(QFileInfo(filePath).fileName()));
        QApplication::processEvents();
        
        ImageBuffer buf;
        bool loaded = false;
        QString ext = QFileInfo(filePath).suffix().toLower();
        
        // Try loading based on extension
        if (ext == "fit" || ext == "fits" || ext == "fts") {
            loaded = Stacking::FitsIO::read(filePath, buf);
        } else if (ext == "tif" || ext == "tiff") {
            loaded = Stacking::TiffIO::read(filePath, buf);
        } else {
#ifdef HAVE_LIBRAW
            libraw_data_t *lr = libraw_init(0);
            if (lr) {
                // We want RAW CFA data, NO demosaicing (much faster and correct for astro)
                if (libraw_open_file(lr, filePath.toLocal8Bit().constData()) == LIBRAW_SUCCESS) {
                    if (libraw_unpack(lr) == LIBRAW_SUCCESS) {
                        // Extract metadata
                        buf.metadata().exposure = lr->other.shutter;
                        if (lr->makernotes.common.SensorTemperature > -273.0f) {
                            buf.metadata().ccdTemp = lr->makernotes.common.SensorTemperature;
                        }
                        buf.metadata().isMono = true; // CFA is mono
                        
                        // Access raw data directly from LibRaw
                        int w = lr->sizes.width;
                        int h = lr->sizes.height;
                        int c = 1; // CFA
                        
                        std::vector<float> data(static_cast<size_t>(w) * h);
                        unsigned short* src = (unsigned short*)lr->rawdata.raw_alloc; // Raw data
                        if (!src) src = (unsigned short*)lr->rawdata.raw_image;
                        
                        if (src) {
                            const size_t total = (size_t)w * h;
                            const float norm = 1.0f / 65535.0f;
                            #pragma omp parallel for
                            for (size_t j = 0; j < total; ++j) {
                                data[j] = src[j] * norm;
                            }
                            buf.setData(w, h, c, data);
                            loaded = true;
                        }
                    }
                }
                libraw_close(lr);
            }
#endif
        }
        
        if (loaded) {
            if (buf.isValid()) {
                if (Stacking::FitsIO::write(outPath, buf, 32)) {
                    converted++;
                    m_fileList->item(i)->setForeground(Qt::green);
                } else {
                    m_fileList->item(i)->setForeground(Qt::red);
                }
            } else {
                converted++;
                m_fileList->item(i)->setForeground(Qt::green);
            }
        } else {
            m_fileList->item(i)->setForeground(Qt::red);
        }
        
        m_progress->setValue(i + 1);
        QApplication::processEvents();
    }
    
    m_convertBtn->setEnabled(true);
    m_statusLabel->setText(tr("Converted %1 of %2 files").arg(converted).arg(count));
    
    if (converted == count) {
        QMessageBox::information(this, tr("Conversion Complete"),
            tr("Successfully converted %1 files.").arg(converted));
    } else {
        QMessageBox::warning(this, tr("Conversion Complete"),
            tr("Converted %1 of %2 files. Some files failed.").arg(converted).arg(count));
    }
}

void ConversionDialog::updateStatus() {
    int count = m_fileList->count();
    m_statusLabel->setText(tr("%1 file(s) ready for conversion").arg(count));
    m_convertBtn->setEnabled(count > 0);
}
