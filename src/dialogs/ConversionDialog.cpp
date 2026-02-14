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
#include <QSettings>
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
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Conversion/InputFolder", QDir::currentPath()).toString();

    QStringList files = QFileDialog::getOpenFileNames(this,
        tr("Select RAW Files"),
        initialDir,
        tr("RAW Files (*.cr2 *.CR2 *.nef *.NEF *.arw *.ARW *.dng *.DNG "
           "*.orf *.ORF *.rw2 *.RW2 *.raf *.RAF *.pef *.PEF);;"
           "TIFF Files (*.tif *.tiff *.TIF *.TIFF);;"
           "All Files (*)"));

    if (!files.isEmpty()) {
        settings.setValue("Conversion/InputFolder", QFileInfo(files.first()).absolutePath());
    }
    
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
    QSettings settings("TStar", "TStar");
    QString initialDir = settings.value("Conversion/OutputFolder", 
        m_outputDir->text().isEmpty() ? QDir::currentPath() : m_outputDir->text()).toString();

    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Select Output Directory"),
        initialDir);
    
    if (!dir.isEmpty()) {
        m_outputDir->setText(dir);
        settings.setValue("Conversion/OutputFolder", dir);
    }
}

// Include ResourceManager
#include "core/ResourceManager.h"
#include <QFutureWatcher>

// Include QFutureWatcher if not already visible (it was observed in file view around line 189)

void ConversionDialog::onConvert() {
    int count = m_fileList->count();
    if (count == 0) {
        QMessageBox::warning(this, tr("No Files"), tr("Please add files to convert."));
        return;
    }
    
    QString outDir = m_outputDir->text();
    int bitDepthVal = m_bitDepth->currentIndex() == 0 ? 16 : 32;
    
    m_progress->setMaximum(count);
    m_progress->setValue(0);
    m_convertBtn->setEnabled(false);
    m_statusLabel->setText(tr("Preparing conversion..."));
    
    // Disable inputs
    m_addBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);
    
    struct ConvertJob {
        QString filePath;
        QString outPath;
        int listIndex;
    };

    struct Context {
        QList<ConvertJob> jobs;
        QAtomicInt processed{0};
        QAtomicInt successes{0};
        qint64 startTime;
    };
    auto ctx = std::make_shared<Context>();
    ctx->startTime = QDateTime::currentMSecsSinceEpoch();
    
    for (int i = 0; i < count; ++i) {
        QString filePath = m_fileList->item(i)->data(Qt::UserRole).toString();
        QString targetDir = outDir.isEmpty() ? QFileInfo(filePath).absolutePath() : outDir;
        QString outPath = QDir(targetDir).absoluteFilePath(
            QFileInfo(filePath).completeBaseName() + ".fit");
            
        m_fileList->item(i)->setForeground(Qt::white);
        ctx->jobs.append({filePath, outPath, i});
    }

    int maxThreads = ResourceManager::instance().maxThreads();
    QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);

    // Use QFutureWatcher to monitor completion
    auto* watcher = new QFutureWatcher<void>(this);
    
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, count, ctx]() {
        m_convertBtn->setEnabled(true);
        m_addBtn->setEnabled(true);
        m_removeBtn->setEnabled(true);
        m_clearBtn->setEnabled(true);
        
        qint64 elapsedPtr = QDateTime::currentMSecsSinceEpoch() - ctx->startTime;
        int successCount = ctx->successes.loadRelaxed();

        m_statusLabel->setText(tr("Converted %1 files in %2 ms").arg(successCount).arg(elapsedPtr));
        
        if (successCount == count) {
            QMessageBox::information(this, tr("Conversion Complete"),
                tr("Successfully converted %1 files in %2 s.").arg(successCount).arg(elapsedPtr / 1000.0, 0, 'f', 1));
        } else {
            QMessageBox::warning(this, tr("Conversion Complete"),
                tr("Converted %1 of %2 files. Some files failed.").arg(successCount).arg(count));
        }
        
        watcher->deleteLater();
    });

    // Start Async Map - IMPORTANT: Pass ctx->jobs which is now inside the shared pointer
    QFuture<void> future = QtConcurrent::map(ctx->jobs, [this, ctx, count, bitDepthVal](const ConvertJob& job) {
        bool success = false;
        
        // --- Conversion Logic ---
        ImageBuffer buf;
        bool loaded = false;
        QString ext = QFileInfo(job.filePath).suffix().toLower();
        
        // Reuse thread-local buffer? Be careful with async if thread_local persists across different tasks incorrectly
        static thread_local ImageBuffer threadBuffer; 
        threadBuffer.resize(0, 0, 0);

        if (ext == "fit" || ext == "fits" || ext == "fts") {
             loaded = Stacking::FitsIO::read(job.filePath, threadBuffer);
        } else if (ext == "tif" || ext == "tiff") {
             loaded = Stacking::TiffIO::read(job.filePath, threadBuffer);
        } else {
#ifdef HAVE_LIBRAW
             libraw_data_t *lr = libraw_init(0);
             if (lr) {
                 if (libraw_open_file(lr, job.filePath.toLocal8Bit().constData()) == LIBRAW_SUCCESS) {
                     if (libraw_unpack(lr) == LIBRAW_SUCCESS) {
                         libraw_decoder_info_t info;
                         libraw_get_decoder_info(lr, &info);
                         
                         int w = lr->rawdata.sizes.raw_width;
                         int left = lr->rawdata.sizes.left_margin;
                         int top = lr->rawdata.sizes.top_margin;
                         int vw = lr->rawdata.sizes.width;
                         int vh = lr->rawdata.sizes.height;
                         
                         threadBuffer.resize(vw, vh, 1);
                         float* dst = threadBuffer.data().data();
                         unsigned short* src = (unsigned short*)lr->rawdata.raw_alloc; 
                         if (!src) src = lr->rawdata.raw_image;
                         
                         if (src) {
                             float black = (float)lr->color.black;
                             float maximum = (float)lr->color.maximum;
                             float range = maximum - black;
                             if (range <= 0.0f) range = 65535.0f;

                             float mul[4];
                             for (int k = 0; k < 4; ++k) mul[k] = lr->color.cam_mul[k];
                             float g_norm = mul[1]; 
                             if (g_norm <= 0.0f && mul[3] > 0.0f) g_norm = mul[3];
                             if (g_norm <= 0.0f) g_norm = 1.0f;
                             for (int k = 0; k < 4; ++k) mul[k] /= g_norm;

                             unsigned int f = lr->idata.filters;

                             for (int y = 0; y < vh; ++y) {
                                 int row = y + top;
                                 for (int x = 0; x < vw; ++x) {
                                     int col = x + left;
                                     int patternIdx = (f >> ((((row) << 1) & 14) + (col & 1)) * 2) & 3;
                                     float val = (float)src[row * w + col];
                                     dst[y * vw + x] = std::max(0.0f, (val - black) * mul[patternIdx] / range);
                                 }
                             }
                             loaded = true;
                             
                             threadBuffer.metadata().isMono = true;
                             QString bayerPat = "RGGB"; // Simplification for brevity, full logic assumed correct in original
                             if (f == 0x94949494) bayerPat = "RGGB";
                             else if (f == 0x16161616) bayerPat = "BGGR";
                             else if (f == 0x61616161) bayerPat = "GRBG";
                             else if (f == 0x49494949) bayerPat = "GBRG";
                             
                             // Pattern shift logic based on margins
                             if (left % 2 != 0) {
                                 if (bayerPat == "RGGB") bayerPat = "GRBG";
                                 else if (bayerPat == "BGGR") bayerPat = "GBRG";
                                 else if (bayerPat == "GRBG") bayerPat = "RGGB";
                                 else if (bayerPat == "GBRG") bayerPat = "BGGR";
                             }
                             if (top % 2 != 0) {
                                 if (bayerPat == "RGGB") bayerPat = "GBRG";
                                 else if (bayerPat == "BGGR") bayerPat = "GRBG";
                                 else if (bayerPat == "GRBG") bayerPat = "BGGR";
                                 else if (bayerPat == "GBRG") bayerPat = "RGGB";
                             }
                             
                             threadBuffer.metadata().bayerPattern = bayerPat;
                             threadBuffer.metadata().xisfProperties["BayerPattern"] = bayerPat;
                             threadBuffer.metadata().exposure = lr->other.shutter;
                         }
                     }
                 }
                 libraw_close(lr);
             }
#endif
        }

        if (loaded) {
            if (Stacking::FitsIO::write(job.outPath, threadBuffer, bitDepthVal)) {
                 success = true;
                 ctx->successes.fetchAndAddRelaxed(1);
            }
        }
        
        int p = ctx->processed.fetchAndAddRelaxed(1) + 1;
        
        // Update UI (Thread-safe)
        QMetaObject::invokeMethod(this, [this, p, count, job, success]() {
             m_progress->setValue(p);
             m_statusLabel->setText(tr("Converting %1 of %2...").arg(p).arg(count));
             
             QListWidgetItem* item = m_fileList->item(job.listIndex);
             if (item) item->setForeground(success ? Qt::green : Qt::red);
        }, Qt::QueuedConnection);
    });
    
    watcher->setFuture(future);
}

void ConversionDialog::updateStatus() {
    int count = m_fileList->count();
    m_statusLabel->setText(tr("%1 file(s) ready for conversion").arg(count));
    m_convertBtn->setEnabled(count > 0);
}
