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
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QProcess>
#include <csignal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#else
#include <signal.h>
#include <execinfo.h> // For backtrace
#include <cxxabi.h>   // For demangling
#include <unistd.h>
#endif

// Forward declare for the signal handler to access
void GlobalExceptionHandler::init()
{
#ifdef Q_OS_WIN
    // 1. Install Windows SEH Handler (Access Violations, etc.)
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)GlobalExceptionHandler::handleSEH);
#else
    // 1. Install POSIX Signal Handlers (SIGSEGV, SIGABRT, etc.)
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = SA_SIGINFO; // Use extended signal handling
    sa.sa_sigaction = GlobalExceptionHandler::handlePosixSignal;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
#endif

    // 2. Install C++ Terminate Handler (std::terminate, unhandled exceptions)
    std::set_terminate(GlobalExceptionHandler::handleTerminate);
}

#ifdef Q_OS_WIN
long __stdcall GlobalExceptionHandler::handleSEH(struct _EXCEPTION_POINTERS* exceptionInfo)
{
    DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    PVOID addr = exceptionInfo->ExceptionRecord->ExceptionAddress;
    
    QString errorMsg = QString("Critical System Error (SEH)\nCode: 0x%1\nAddress: 0x%2")
        .arg(code, 8, 16, QChar('0')).arg(reinterpret_cast<quintptr>(addr), 16, 16, QChar('0'));
        
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        errorMsg += "\nType: Access Violation";
        // detail: 0=read, 1=write
        if (exceptionInfo->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR op = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR target = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
            errorMsg += QString("\nOperation: %1 at Address: 0x%2")
                .arg(op == 0 ? "Read" : (op == 1 ? "Write" : "Data Execution"))
                .arg(target, 16, 16, QChar('0'));
        }
    }
    
    Logger::critical(errorMsg, "GlobalExceptionHandler");
    showDialog(errorMsg, true);
    
    return EXCEPTION_EXECUTE_HANDLER; // Or EXCEPTION_CONTINUE_SEARCH
}
#else
void GlobalExceptionHandler::handlePosixSignal(int sig, siginfo_t* info, void* context)
{
    Q_UNUSED(context);
    QString sigName;
    switch(sig) {
        case SIGSEGV: sigName = "SIGSEGV (Segmentation Fault)"; break;
        case SIGABRT: sigName = "SIGABRT (Aborted)"; break;
        case SIGFPE:  sigName = "SIGFPE (Floating Point Exception)"; break;
        case SIGILL:  sigName = "SIGILL (Illegal Instruction)"; break;
        case SIGBUS:  sigName = "SIGBUS (Bus Error)"; break;
        default:      sigName = QString("Signal %1").arg(sig); break;
    }
    
    QString msg = QString("Critical System Error (Signal)\nType: %1").arg(sigName);
    
    if (info && info->si_addr) {
        msg += QString("\nMemory Address: 0x%1").arg(reinterpret_cast<quintptr>(info->si_addr), 16, 16, QChar('0'));
    }

    // Attempt Stack Trace
    void* array[20];
    size_t size = backtrace(array, 20);
    char** strings = backtrace_symbols(array, size);
    
    if (strings) {
        msg += "\n\nStack Trace:\n";
        for (size_t i = 0; i < size; ++i) {
            msg += QString::fromLatin1(strings[i]) + "\n";
        }
        free(strings);
    }
    
    Logger::critical(msg, "GlobalExceptionHandler");
    showDialog(msg, true);
    
    // Restore default handler and raise to exit properly
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

void GlobalExceptionHandler::handleTerminate()
{
    // Try to rethrow to get the exception message if possible
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& e) {
        handle(e);
    } catch (...) {
        Logger::critical("Application terminated abnormally (std::terminate called)", "GlobalExceptionHandler");
        showDialog("Critical Error: Application terminated improperly.", true);
    }
    std::abort();
}

void GlobalExceptionHandler::handleSignal(int sig)
{
    // Fallback handler if needed, though sigaction covers most
    QString msg = QString("Received signal: %1").arg(sig);
    Logger::critical(msg, "GlobalExceptionHandler");
    showDialog(msg, true);
    std::exit(sig);
}

void GlobalExceptionHandler::handle(const std::exception& e)
{
    QString msg = QString::fromStdString(e.what());
    Logger::critical("Caught unhandled exception: " + msg, "ExceptionHandler");
    showDialog(msg, false);
}

void GlobalExceptionHandler::handle(const QString& errorMessage)
{
    Logger::critical("Caught unhandled error: " + errorMessage, "ExceptionHandler");
    showDialog(errorMessage, false);
}

void GlobalExceptionHandler::showDialog(const QString& message, bool isFatal)
{
    // Last-ditch effort code
    
    // Ensure we are in the main thread for UI
    if (QApplication::instance() && QThread::currentThread() != QApplication::instance()->thread()) {
        QMetaObject::invokeMethod(QApplication::instance(), [message, isFatal]() {
            showDialog(message, isFatal);
        }, Qt::BlockingQueuedConnection);
        return;
    }

    // Checking if app is dying
    if (!QApplication::instance()) {
        // No Qt? Use MessageBox
        MessageBoxW(NULL, message.toStdWString().c_str(), L"Critical Error", MB_ICONERROR | MB_OK);
        if (isFatal) std::exit(-1);
        return;
    }
    
    QDialog dialog;
    dialog.setWindowTitle(isFatal ? QObject::tr("Critical Error") : QObject::tr("Application Error"));
    dialog.setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical));
    dialog.setMinimumWidth(500);
    dialog.setModal(true);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    // Icon and Message
    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* iconLabel = new QLabel();
    iconLabel->setPixmap(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(48, 48));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    
    QVBoxLayout* textLayout = new QVBoxLayout();
    QLabel* titleLabel = new QLabel(isFatal ? 
        QObject::tr("<h3>A critical error occurred</h3><p>The application must terminate.</p>") :
        QObject::tr("<h3>An unexpected error occurred</h3>"));
    textLayout->addWidget(titleLabel);
    
    QLabel* msgLabel = new QLabel(message);
    msgLabel->setWordWrap(true);
    msgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    textLayout->addWidget(msgLabel);
    
    headerLayout->addLayout(textLayout, 1);
    layout->addLayout(headerLayout);
    
    // Details area
    QLabel* detailsLabel = new QLabel(QObject::tr("Log Context:"));
    layout->addWidget(detailsLabel);
    
    QTextEdit* detailsText = new QTextEdit();
    detailsText->setReadOnly(true);
    detailsText->setPlainText(Logger::getRecentLogs(50));
    detailsText->setFont(QFont("Consolas", 9));
    detailsText->setFixedHeight(150);
    layout->addWidget(detailsText);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    QPushButton* copyBtn = new QPushButton(QObject::tr("Copy to Clipboard"));
    QObject::connect(copyBtn, &QPushButton::clicked, [message, detailsText]() {
        if (QApplication::clipboard())
            QApplication::clipboard()->setText(message + "\n\nLog Context:\n" + detailsText->toPlainText());
    });
    
    QPushButton* openLogBtn = new QPushButton(QObject::tr("Open Log Folder"));
    QObject::connect(openLogBtn, &QPushButton::clicked, []() {
        QString logPath = Logger::currentLogFile();
        if (!logPath.isEmpty()) {
            QFileInfo fi(logPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
        }
    });

    QPushButton* closeBtn = new QPushButton(isFatal ? QObject::tr("Exit") : QObject::tr("Close"));
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    
    btnLayout->addWidget(copyBtn);
    btnLayout->addWidget(openLogBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    
    layout->addLayout(btnLayout);
    
    dialog.exec();
    
    if (isFatal) {
        std::exit(-1);
    }
}
