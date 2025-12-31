#ifndef STARNETDIALOG_H
#define STARNETDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>

class MainWindow;

class StarNetDialog : public QDialog {
    Q_OBJECT
public:
    explicit StarNetDialog(MainWindow* parent = nullptr);

private slots:
    void onRun();

private:
    MainWindow* m_mainWin;
    QCheckBox* m_chkLinear;
    QCheckBox* m_chkGenerateMask;
    QCheckBox* m_chkGpu;
    QSpinBox* m_spinStride;
    QPushButton* m_btnRun;
};

#endif // STARNETDIALOG_H
