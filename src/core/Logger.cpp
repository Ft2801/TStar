#include "Logger.h"
#include "Version.h"
#include <QStandardPaths>
#include <QFileInfo>
#include <QStringConverter>
#include <iostream>
#include <csignal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif
#pragma GCC diagnostic pop
#endif

// Static member initialization
QFile* Logger::s_logFile = nullptr;
QTextStream* Logger::s_logStream = nullptr;
QRecursiveMutex Logger::s_mutex;
QString Logger::s_logDirPath;
QString Logger::s_currentLogPath;
int Logger::s_maxLogFiles = 5;
bool Logger::s_initialized = false;
QtMessageHandler Logger::s_previousHandler = nullptr;

// Signal handler for crashes
static void crashSignalHandler(int signal)
{
    QString reason;
    switch (signal) {
        case SIGSEGV: reason = "Segmentation Fault (SIGSEGV)"; break;
        case SIGABRT: reason = "Abort Signal (SIGABRT)"; break;
        case SIGFPE:  reason = "Floating Point Exception (SIGFPE)"; break;
        case SIGILL:  reason = "Illegal Instruction (SIGILL)"; break;
        default:      reason = QString("Signal %1").arg(signal); break;
    }
    
    Logger::critical("=== APPLICATION CRASH ===");
    Logger::critical("Reason: " + reason);
    Logger::critical("The application has encountered a fatal error and will now exit.");
    Logger::shutdown();
    
    // Re-raise to get default behavior (core dump, etc.)
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void Logger::init(const QString& logDirPath, int maxLogFiles)
{
    QMutexLocker locker(&s_mutex);
    
    if (s_initialized) return;
    
    s_maxLogFiles = maxLogFiles;
    
    // Determine log directory
    if (logDirPath.isEmpty()) {
        // Default: app directory/logs
        QString appDir = QCoreApplication::applicationDirPath();
        s_logDirPath = appDir + "/logs";
    } else {
        s_logDirPath = logDirPath;
    }
    
    // Create log directory if needed
    QDir dir(s_logDirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // Create new log file
    s_currentLogPath = s_logDirPath + "/TStar_debug.log";
    
    // Rotate old logs disabled for now to avoid deleting our debug log
    // rotateLogFiles();
    
    s_logFile = new QFile(s_currentLogPath);
    if (!s_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        std::cerr << "Failed to open log file: " << s_currentLogPath.toStdString() << std::endl;
        delete s_logFile;
        s_logFile = nullptr;
        return;
    }
    
    s_logStream = new QTextStream(s_logFile);
    s_logStream->setEncoding(QStringConverter::Utf8);
    
    // Write header
    *s_logStream << "================================================================================\n";
    *s_logStream << "TStar Log - Started " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    *s_logStream << "Version: " << TStar::getVersion() << "\n";
#ifdef Q_OS_WIN
    *s_logStream << "Platform: Windows\n";
#elif defined(Q_OS_MAC)
    *s_logStream << "Platform: macOS\n";
#else
    *s_logStream << "Platform: Linux/Other\n";
#endif
    *s_logStream << "================================================================================\n\n";
    s_logStream->flush();
    
    // Install Qt message handler
    s_previousHandler = qInstallMessageHandler(qtMessageHandler);
    
    // Install crash handlers
    std::signal(SIGSEGV, crashSignalHandler);
    std::signal(SIGABRT, crashSignalHandler);
    std::signal(SIGFPE, crashSignalHandler);
    std::signal(SIGILL, crashSignalHandler);
    
#ifdef Q_OS_WIN
    // Windows-specific: handle unhandled exceptions
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exInfo) -> LONG {
        Logger::critical("=== UNHANDLED EXCEPTION ===");
        Logger::critical(QString("Exception Code: 0x%1").arg(exInfo->ExceptionRecord->ExceptionCode, 8, 16, QChar('0')));
        Logger::shutdown();
        return EXCEPTION_CONTINUE_SEARCH;
    });
#endif
    
    s_initialized = true;
    
    // Log initialization
    log(Info, "Logging system initialized", "Logger");
    log(Info, QString("Log file: %1").arg(s_currentLogPath), "Logger");
}

void Logger::shutdown()
{
    QMutexLocker locker(&s_mutex);
    
    if (!s_initialized) return;
    
    // Restore previous handler
    if (s_previousHandler) {
        qInstallMessageHandler(s_previousHandler);
    }
    
    // Write footer
    if (s_logStream) {
        *s_logStream << "\n================================================================================\n";
        *s_logStream << "TStar Log - Ended " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        *s_logStream << "================================================================================\n";
        s_logStream->flush();
        delete s_logStream;
        s_logStream = nullptr;
    }
    
    if (s_logFile) {
        s_logFile->close();
        delete s_logFile;
        s_logFile = nullptr;
    }
    
    s_initialized = false;
}

void Logger::log(Level level, const QString& message, const QString& category)
{
    QMutexLocker locker(&s_mutex);
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr = levelToString(level);
    QString categoryStr = category.isEmpty() ? "" : QString("[%1] ").arg(category);
    
    QString formattedMsg = QString("[%1] [%2] %3%4")
                              .arg(timestamp)
                              .arg(levelStr, -8)
                              .arg(categoryStr)
                              .arg(message);
    
    // Write to file
    if (s_logStream) {
        *s_logStream << formattedMsg << "\n";
        s_logStream->flush(); // ALWAYS flush for debugging
    }
    
    // Also output to console in debug builds
#ifdef QT_DEBUG
    // std::cerr << formattedMsg.toStdString() << std::endl;
#endif
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Level level;
    switch (type) {
        case QtDebugMsg:    level = Debug; break;
        case QtInfoMsg:     level = Info; break;
        case QtWarningMsg:  level = Warning; break;
        case QtCriticalMsg: level = Critical; break;
        case QtFatalMsg:    level = Fatal; break;
        default:            level = Info; break;
    }
    
    QString category;
    if (context.category && strcmp(context.category, "default") != 0) {
        category = QString::fromUtf8(context.category);
    }
    
    log(level, msg, category);
    
    // For fatal messages, ensure we flush and potentially abort
    if (type == QtFatalMsg) {
        shutdown();
        std::abort();
    }
}

void Logger::rotateLogFiles()
{
    QDir dir(s_logDirPath);
    QStringList filters;
    filters << "TStar_*.log";
    
    QFileInfoList logFiles = dir.entryInfoList(filters, QDir::Files, QDir::Time);
    
    // Remove oldest files if we have too many
    while (logFiles.size() >= s_maxLogFiles) {
        QFile::remove(logFiles.last().absoluteFilePath());
        logFiles.removeLast();
    }
}

QString Logger::levelToString(Level level)
{
    switch (level) {
        case Debug:    return "DEBUG";
        case Info:     return "INFO";
        case Warning:  return "WARNING";
        case Error:    return "ERROR";
        case Critical: return "CRITICAL";
        case Fatal:    return "FATAL";
        default:       return "UNKNOWN";
    }
}

QString Logger::currentLogFile()
{
    return s_currentLogPath;
}
