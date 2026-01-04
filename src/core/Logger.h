#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QRecursiveMutex>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>

/**
 * @brief Centralized logging system for TStar
 * 
 * Features:
 * - Automatic file logging to app directory
 * - Log rotation (keeps last N files)
 * - Thread-safe
 * - Captures qDebug, qWarning, qCritical, qFatal
 * - Crash handling with stack trace
 * 
 * Usage:
 *   Logger::init();  // Call before QApplication starts
 *   Logger::log(Logger::Info, "Message");
 *   Logger::shutdown();  // Call before app exit
 */
class Logger
{
public:
    enum Level {
        Debug,
        Info,
        Warning,
        Error,
        Critical,
        Fatal
    };

    /**
     * @brief Initialize the logging system
     * @param logDirPath Optional custom log directory (defaults to app dir/logs)
     * @param maxLogFiles Maximum number of log files to keep (default 5)
     */
    static void init(const QString& logDirPath = QString(), int maxLogFiles = 5);

    /**
     * @brief Shutdown the logging system
     */
    static void shutdown();

    /**
     * @brief Log a message
     */
    static void log(Level level, const QString& message, const QString& category = QString());

    /**
     * @brief Get the current log file path
     */
    static QString currentLogFile();

    /**
     * @brief Convenience methods
     */
    static void debug(const QString& msg, const QString& cat = QString())    { log(Debug, msg, cat); }
    static void info(const QString& msg, const QString& cat = QString())     { log(Info, msg, cat); }
    static void warning(const QString& msg, const QString& cat = QString())  { log(Warning, msg, cat); }
    static void error(const QString& msg, const QString& cat = QString())    { log(Error, msg, cat); }
    static void critical(const QString& msg, const QString& cat = QString()) { log(Critical, msg, cat); }

private:
    Logger() = default;
    ~Logger() = default;

    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    static void rotateLogFiles();
    static QString levelToString(Level level);
    static void writeCrashLog(const QString& reason);

    static QFile* s_logFile;
    static QTextStream* s_logStream;
    static QRecursiveMutex s_mutex;
    static QString s_logDirPath;
    static QString s_currentLogPath;
    static int s_maxLogFiles;
    static bool s_initialized;
    static QtMessageHandler s_previousHandler;
};

#endif // LOGGER_H
