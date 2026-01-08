#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

#include "DialogBase.h"

class AboutDialog : public DialogBase {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr, const QString& version = "", const QString& buildTimestamp = "");
};

#endif // ABOUTDIALOG_H
