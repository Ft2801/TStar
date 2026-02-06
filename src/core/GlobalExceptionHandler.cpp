#include "GlobalExceptionHandler.h"
#include "Logger.h"
#include <QMessageBox>
#include <QApplication>
#include <QThread>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDialog>
#include <QLabel>
#include <QStyle>
#include <QClipboard>

void GlobalExceptionHandler::handle(const std::exception& e)
{
    QString msg = QString::fromStdString(e.what());
    Logger::critical("Caught unhandled exception: " + msg, "ExceptionHandler");
    showDialog(msg);
}

void GlobalExceptionHandler::handle(const QString& errorMessage)
{
    Logger::critical("Caught unhandled error: " + errorMessage, "ExceptionHandler");
    showDialog(errorMessage);
}

void GlobalExceptionHandler::showDialog(const QString& message)
{
    // Guard: Don't show dialog if app is closing
    if (!QApplication::instance() || QApplication::closingDown()) {
        return;
    }
    
    // Ensure we are in the main thread for UI
    if (QThread::currentThread() != QApplication::instance()->thread()) {
        QMetaObject::invokeMethod(QApplication::instance(), [message]() {
            showDialog(message);
        }, Qt::QueuedConnection);
        return;
    }

    QDialog dialog;
    dialog.setWindowTitle(QObject::tr("Application Error"));
    dialog.setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical));
    dialog.setMinimumWidth(500);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    // Icon and Message
    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* iconLabel = new QLabel();
    iconLabel->setPixmap(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(48, 48));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    
    QVBoxLayout* textLayout = new QVBoxLayout();
    QLabel* titleLabel = new QLabel(QObject::tr("<h3>An unexpected error occurred</h3>"));
    textLayout->addWidget(titleLabel);
    
    QLabel* msgLabel = new QLabel(message);
    msgLabel->setWordWrap(true);
    textLayout->addWidget(msgLabel);
    
    headerLayout->addLayout(textLayout, 1);
    layout->addLayout(headerLayout);
    
    // Details area (collapsed by default)
    QLabel* detailsLabel = new QLabel(QObject::tr("Log Context:"));
    layout->addWidget(detailsLabel);
    
    QTextEdit* detailsText = new QTextEdit();
    detailsText->setReadOnly(true);
    detailsText->setPlainText(Logger::getRecentLogs(50));
    detailsText->setFont(QFont("Consolas", 9));
    detailsText->setFixedHeight(200);
    layout->addWidget(detailsText);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    QPushButton* copyBtn = new QPushButton(QObject::tr("Copy to Clipboard"));
    QObject::connect(copyBtn, &QPushButton::clicked, [message, detailsText]() {
        QApplication::clipboard()->setText(message + "\n\nLog Context:\n" + detailsText->toPlainText());
    });
    
    QPushButton* closeBtn = new QPushButton(QObject::tr("Close"));
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    
    layout->addLayout(btnLayout);
    
    dialog.exec();
}
