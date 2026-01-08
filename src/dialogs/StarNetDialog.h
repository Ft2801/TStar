#ifndef STARNETDIALOG_H
#define STARNETDIALOG_H

#include "DialogBase.h"
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>

class MainWindowCallbacks;

class StarNetDialog : public DialogBase {
    Q_OBJECT
public:
    explicit StarNetDialog(QWidget* parent = nullptr);

private slots:
    void onRun();

private:
    QCheckBox* m_chkLinear;
    QCheckBox* m_chkGenerateMask;
    QCheckBox* m_chkGpu;
    QSpinBox* m_spinStride;
    QPushButton* m_btnRun;
};

#endif // STARNETDIALOG_H
