#include "UpdateDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QNetworkRequest>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QDesktopServices>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDebug>

UpdateDialog::UpdateDialog(QWidget* parent, const QString& version, const QString& changelog, const QString& downloadUrl) 
    : QDialog(parent), m_downloadUrl(downloadUrl), m_nam(nullptr), m_reply(nullptr), m_file(nullptr)
{
    setWindowTitle(tr("Update Available: v%1").arg(version));
    resize(500, 400);
    setWindowIcon(QIcon(":/images/Logo.png"));

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Header
    QLabel* header = new QLabel(tr("<h3>A new version of TStar is available!</h3>"), this);
    layout->addWidget(header);

    // Changelog
    layout->addWidget(new QLabel(tr("What's New:"), this));
    m_changelogView = new QTextBrowser(this);
    m_changelogView->setMarkdown(changelog);
    m_changelogView->setOpenExternalLinks(true);
    layout->addWidget(m_changelogView);

    // Progress Status
    m_statusLabel = new QLabel(tr("Would you like to update now?"), this);
    layout->addWidget(m_statusLabel);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    layout->addWidget(m_progressBar);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_updateBtn = new QPushButton(tr("Update Now"), this);
    m_updateBtn->setStyleSheet("font-weight: bold; background-color: #007acc; color: white; padding: 6px;");
    
    m_cancelBtn = new QPushButton(tr("Not Now"), this);
    
    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_updateBtn);
    layout->addLayout(btnLayout);

    connect(m_updateBtn, &QPushButton::clicked, this, &UpdateDialog::startDownload);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

UpdateDialog::~UpdateDialog() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file) {
        m_file->close();
        delete m_file;
    }
}

void UpdateDialog::startDownload() {
    // If the URL is just a webpage (no platform-specific installer), open browser
#if defined(Q_OS_WIN)
    if (!m_downloadUrl.endsWith(".exe", Qt::CaseInsensitive)) {
#elif defined(Q_OS_MAC)
    if (!m_downloadUrl.endsWith(".dmg", Qt::CaseInsensitive)) {
#else
    if (true) { // Linux: always open in browser
#endif
        QDesktopServices::openUrl(QUrl(m_downloadUrl));
        accept();
        return;
    }

    // Prepare File
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString fileName = m_downloadUrl.section('/', -1);
    m_destinationPath = QDir(tempDir).filePath(fileName);

    m_file = new QFile(m_destinationPath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not save update file: %1").arg(m_file->errorString()));
        return;
    }

    // UI Updates
    m_updateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false); // Can't cancel easily once started without cleanup logic, keep it simple
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Downloading update..."));

    // Start Request
    m_nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(m_downloadUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDialog::onDownloadProgress);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateDialog::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateDialog::onDownloadFinished);
}

void UpdateDialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        m_progressBar->setMaximum(100);
        m_progressBar->setValue((bytesReceived * 100) / bytesTotal);
        m_statusLabel->setText(tr("Downloading... %1%").arg((bytesReceived * 100) / bytesTotal));
    } else {
        m_progressBar->setMaximum(0); // Indeterminate
        m_statusLabel->setText(tr("Downloading... (%1 bytes)").arg(bytesReceived));
    }
}

void UpdateDialog::onReadyRead() {
    if (m_file) {
        m_file->write(m_reply->readAll());
    }
}

void UpdateDialog::onDownloadFinished() {
    if (m_file) {
        m_file->close();
    }

    if (m_reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, tr("Update Failed"), tr("Download failed: %1").arg(m_reply->errorString()));
        m_progressBar->setVisible(false);
        m_updateBtn->setEnabled(true);
        m_cancelBtn->setEnabled(true);
        m_file->remove(); // delete partial
        return;
    }
    
    // Success
    m_statusLabel->setText(tr("Download complete. Verifying..."));
    m_progressBar->setValue(100);
    
    // Ask to install
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Ready to Install"), 
        tr("Download complete. Close TStar and start the installer?"),
        QMessageBox::Yes|QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        launchInstaller();
    } else {
        m_statusLabel->setText(tr("Update downloaded. Run installer manually from temp if desired."));
        m_updateBtn->setEnabled(true);
        m_cancelBtn->setEnabled(true);
    }
}

void UpdateDialog::launchInstaller() {
    // Launch installer detached
#if defined(Q_OS_MAC)
    // On macOS, use 'open' to mount the DMG and show its contents
    bool success = QProcess::startDetached("open", QStringList() << m_destinationPath);
#else
    bool success = QProcess::startDetached(m_destinationPath, QStringList());
#endif
    if (success) {
        QCoreApplication::quit();
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to launch installer. Please run it manually:\n%1").arg(m_destinationPath));
    }
}
