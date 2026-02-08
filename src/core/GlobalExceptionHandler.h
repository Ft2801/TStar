#ifndef GLOBALEXCEPTIONHANDLER_H
#define GLOBALEXCEPTIONHANDLER_H

#include <QString>
#include <exception>

class GlobalExceptionHandler
{
public:
    static void init();
    static void handle(const std::exception& e);
    static void handle(const QString& errorMessage);
    
    // Platform-specific handlers
#ifdef Q_OS_WIN
    static long __stdcall handleSEH(struct _EXCEPTION_POINTERS* exceptionInfo);
#else
    static void handlePosixSignal(int sig, siginfo_t* info, void* context);
#endif
    static void handleTerminate();
    static void handleSignal(int sig); // Standard signal handler (fallback)

private:
    static void showDialog(const QString& message, bool isFatal = false);
};

#endif // GLOBALEXCEPTIONHANDLER_H
