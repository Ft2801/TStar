#ifndef TSTARAPPLICATION_H
#define TSTARAPPLICATION_H

#include <QApplication>

class TStarApplication : public QApplication
{
public:
    TStarApplication(int& argc, char** argv);
    virtual ~TStarApplication();

    bool notify(QObject* receiver, QEvent* event) override;
};

#endif // TSTARAPPLICATION_H
