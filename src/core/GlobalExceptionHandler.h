#ifndef GLOBALEXCEPTIONHANDLER_H
#define GLOBALEXCEPTIONHANDLER_H

#include <QString>
#include <exception>

class GlobalExceptionHandler
{
public:
    static void handle(const std::exception& e);
    static void handle(const QString& errorMessage);
    
private:
    static void showDialog(const QString& message);
};

#endif // GLOBALEXCEPTIONHANDLER_H
