#include "RARDialog.h"
#include "../MainWindow.h"
#include "../algos/RARRunner.h"
#include <QThread>
#include <QProgressDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <QSettings>
#include <QIcon>
#include <QCoreApplication>
#include <QLibrary>

// Helper to detect best GPU provider
static QString detectBestProvider() {
    // Check for CUDA (NVIDIA) - look for nvcuda.dll
    bool hasCuda = QLibrary("nvcuda").load();
    
    // Check for DirectML (AMD/Intel) - available on Windows 10+
    bool hasDirectML = QLibrary("DirectML").load();
    
    // Prefer CUDA if available (usually faster), then DirectML
    if (hasCuda) return "CUDA";
    if (hasDirectML) return "DirectML";
    return "CPU";
}

RARDialog::RARDialog(MainWindow* parent) : QDialog(parent), m_mainWin(parent) {
    setWindowTitle(tr("Aberration Remover"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    resize(400, 300);
    
    QSettings s;

    QVBoxLayout* layout = new QVBoxLayout(this);
    
    // Model Section
    QGroupBox* grpModel = new QGroupBox(tr("Model"), this);
    QVBoxLayout* modLayout = new QVBoxLayout(grpModel);
    
    QHBoxLayout* pathLayout = new QHBoxLayout();
    m_editModelPath = new QLineEdit(this);
    m_editModelPath->setText(s.value("RAR/lastModel", "").toString());
    QPushButton* btnBrowse = new QPushButton(tr("Browse..."), this);
    connect(btnBrowse, &QPushButton::clicked, this, &RARDialog::onBrowseModel);
    pathLayout->addWidget(m_editModelPath);
    pathLayout->addWidget(btnBrowse);
    
    QPushButton* btnDownload = new QPushButton(tr("Download Latest Model (GitHub)"), this);
    connect(btnDownload, &QPushButton::clicked, this, &RARDialog::onDownloadModel);
    
    modLayout->addLayout(pathLayout);
    modLayout->addWidget(btnDownload);
    layout->addWidget(grpModel);
    
    // Params
    QGroupBox* grpParams = new QGroupBox(tr("Parameters"), this);
    QGridLayout* pLayout = new QGridLayout(grpParams);
    
    pLayout->addWidget(new QLabel(tr("Patch Size:")), 0, 0);
    m_spinPatch = new QSpinBox(this);
    m_spinPatch->setRange(128, 2048);
    m_spinPatch->setValue(512);
    pLayout->addWidget(m_spinPatch, 0, 1);
    
    pLayout->addWidget(new QLabel(tr("Overlap:")), 1, 0);
    m_spinOverlap = new QSpinBox(this);
    m_spinOverlap->setRange(16, 512);
    m_spinOverlap->setValue(64);
    pLayout->addWidget(m_spinOverlap, 1, 1);
    
    pLayout->addWidget(new QLabel(tr("Provider:")), 2, 0);
    m_comboProvider = new QComboBox(this);
    m_comboProvider->addItems({"CPU", "DirectML", "CUDA"});
    m_comboProvider->setStyleSheet(
        "QComboBox { color: white; background-color: #2a2a2a; border: 1px solid #555; padding: 2px; border-radius: 3px; }"
        "QComboBox:focus { border: 2px solid #4a9eff; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/images/dropdown.png); }"
        "QComboBox QAbstractItemView { color: white; background-color: #2a2a2a; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 3px; margin: 0px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7 !important; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );
    
    // Auto-detect best provider: Use saved setting if exists, otherwise detect
    QString savedProvider = s.value("RAR/lastProvider", "").toString();
    if (savedProvider.isEmpty()) {
        QString detected = detectBestProvider();
        m_comboProvider->setCurrentText(detected);
    } else {
        m_comboProvider->setCurrentText(savedProvider);
    }
    pLayout->addWidget(m_comboProvider, 2, 1);
    
    layout->addWidget(grpParams);
    
    // Run
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QLabel* copyright = new QLabel(tr("© 2026 Riccardo Alberghi"), this);
    copyright->setStyleSheet("color: gray; font-size: 10px;");
    btnLayout->addWidget(copyright);
    m_lblStatus = new QLabel(this);
    QPushButton* btnRun = new QPushButton(tr("Run"), this);
    QPushButton* btnClose = new QPushButton(tr("Close"), this);
    
    connect(btnRun, &QPushButton::clicked, this, &RARDialog::onRun);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);
    
    btnLayout->addWidget(m_lblStatus, 1);
    btnLayout->addWidget(btnRun);
    btnLayout->addWidget(btnClose);
    layout->addLayout(btnLayout);
}

void RARDialog::onBrowseModel() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select ONNX Model"), "", tr("ONNX Models (*.onnx)"));
    if (!path.isEmpty()) m_editModelPath->setText(path);
}

void RARDialog::onDownloadModel() {
    m_lblStatus->setText(tr("Checking GitHub..."));
    QNetworkAccessManager* net = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://api.github.com/repos/riccardoalberghi/abberation_models/releases/latest"));
    
    QNetworkReply* reply = net->get(req);
    connect(reply, &QNetworkReply::finished, [this, reply, net](){
        reply->deleteLater();
        net->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Error"), tr("GitHub Check Failed: %1").arg(reply->errorString()));
            m_lblStatus->setText("");
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        QJsonArray assets = root["assets"].toArray();
        
        QString downloadUrl, fileName;
        for(const auto& val : assets) {
            QJsonObject asset = val.toObject();
            if (asset["name"].toString().endsWith(".onnx")) {
                fileName = asset["name"].toString();
                downloadUrl = asset["browser_download_url"].toString();
                break;
            }
        }
        
        if (downloadUrl.isEmpty()) {
            QMessageBox::warning(this, tr("Error"), tr("No ONNX model found in latest release."));
            m_lblStatus->setText("");
            return;
        }
        
        // Use AppData/Local/TStar (Standardized user path)
        QString destDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/TStar/models/rar";
        QDir().mkpath(destDir);
        QString destPath = destDir + "/" + fileName;

        // Check if already exists
        if (QFile::exists(destPath)) {
            m_editModelPath->setText(destPath);
            m_lblStatus->setText(tr("Model found locally."));
            QMessageBox::information(this, tr("Model Found"), tr("The model is already downloaded:\n%1").arg(destPath));
            return;
        }
        
        // Start Download
        m_lblStatus->setText(tr("Downloading %1...").arg(fileName));
        QNetworkAccessManager* dlNet = new QNetworkAccessManager(this);
        QNetworkReply* dlReply = dlNet->get(QNetworkRequest(QUrl(downloadUrl)));
        
        connect(dlReply, &QNetworkReply::downloadProgress, [this](qint64 got, qint64 total){
            if(total > 0) m_lblStatus->setText(tr("Downloading: %1%").arg(got*100/total));
        });
        
        connect(dlReply, &QNetworkReply::finished, [this, dlReply, dlNet, destPath](){
            dlReply->deleteLater();
            dlNet->deleteLater();
            
            if (dlReply->error() != QNetworkReply::NoError) {
                QMessageBox::warning(this, tr("Download Error"), dlReply->errorString());
                m_lblStatus->setText("");
                return;
            }
            
            QFile f(destPath);
            if(f.open(QIODevice::WriteOnly)) {
                f.write(dlReply->readAll());
                f.close();
                m_editModelPath->setText(destPath);
                m_lblStatus->setText(tr("Download Complete."));
                QMessageBox::information(this, tr("Success"), tr("Model downloaded to:\n%1").arg(destPath));
            } else {
                 QMessageBox::warning(this, tr("File Error"), tr("Could not save model file."));
            }
        });
    });
}

void RARDialog::setViewer(ImageViewer* v) {
    m_viewer = v;
}

void RARDialog::onRun() {
    if (m_editModelPath->text().isEmpty()) {
         QMessageBox::warning(this, tr("No Image"), tr("Please select an image first."));
         return;
    }
    
    QSettings s;
    s.setValue("RAR/lastModel", m_editModelPath->text());
    s.setValue("RAR/lastProvider", m_comboProvider->currentText());

    RARParams params;
    params.modelPath = m_editModelPath->text();
    params.patchSize = m_spinPatch->value();
    params.overlap = m_spinOverlap->value();
    params.provider = m_comboProvider->currentText();
    
    m_mainWin->startLongProcess();
    m_mainWin->log(tr("Starting Aberration Removal..."));
    
    QThread* thread = new QThread;
    RARRunner* runner = new RARRunner;
    runner->moveToThread(thread);
    
    connect(runner, &RARRunner::processOutput, m_mainWin, [this](const QString& msg){
        m_mainWin->log(msg);
    });

    ImageViewer* viewer = m_viewer; // Use tracked viewer
    if (!viewer) {
        m_mainWin->endLongProcess();
        runner->deleteLater();
        thread->deleteLater();
        QMessageBox::warning(this, tr("No Target"), tr("No active image viewer found."));
        return;
    }
    
    QProgressDialog* pd = new QProgressDialog(tr("Running Aberration Removal..."), tr("Cancel"), 0, 0, this);
    pd->setWindowModality(Qt::WindowModal);
    pd->setMinimumDuration(0);
    pd->show();
             
    connect(pd, &QProgressDialog::canceled, runner, &RARRunner::cancel, Qt::DirectConnection);
    
    ImageBuffer input = viewer->getBuffer();
    
    connect(thread, &QThread::started, runner, [runner, input, params, thread, pd, this]() mutable {
        ImageBuffer output;
        QString err;
        bool success = runner->run(input, output, params, &err);
        
        QMetaObject::invokeMethod(this, [=]() {
            pd->close();
            pd->deleteLater();
            
            thread->quit();
            thread->wait();
            thread->deleteLater();
            runner->deleteLater();
            
            m_mainWin->endLongProcess();
            
            if (success) {
                m_mainWin->createNewImageWindow(output, tr("RAR Result"), m_mainWin->displayMode());
                m_mainWin->log(tr("Aberration Removal Complete."));
                accept();
            } else if (!err.isEmpty() && err != tr("Aborted by user.")) {
                 m_mainWin->log(tr("ERR: RAR Failed: %1").arg(err));
                 QMessageBox::critical(this, tr("Error"), err);
            } else if (err == tr("Aborted by user.")) {
                 m_mainWin->log(tr("RAR cancelled."));
            }
        });
    });
    
    thread->start();
}
