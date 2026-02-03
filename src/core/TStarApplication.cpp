#include "TStarApplication.h"
#include "GlobalExceptionHandler.h"
#include "Logger.h"
#include <exception>

TStarApplication::TStarApplication(int& argc, char** argv)
    : QApplication(argc, argv)
{
}

TStarApplication::~TStarApplication()
{
}

bool TStarApplication::notify(QObject* receiver, QEvent* event)
{
    try {
        return QApplication::notify(receiver, event);
    } catch (const std::exception& e) {
        GlobalExceptionHandler::handle(e);
        return false;
    } catch (...) {
        GlobalExceptionHandler::handle(QString("Unknown exception caught in event loop."));
        return false;
    }
}
