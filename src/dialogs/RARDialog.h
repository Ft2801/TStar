#ifndef RARDIALOG_H
#define RARDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPointer>

class MainWindow;

class RARDialog : public QDialog {
    Q_OBJECT
public:
    explicit RARDialog(MainWindow* parent = nullptr);

    void setViewer(class ImageViewer* v);

private slots:
    void onBrowseModel();
    void onDownloadModel();
    void onRun();

private:
    MainWindow* m_mainWin;
    QLineEdit* m_editModelPath;
    QSpinBox* m_spinPatch;
    QSpinBox* m_spinOverlap;
    QComboBox* m_comboProvider;
    QLabel* m_lblStatus;
    
    QPointer<class ImageViewer> m_viewer;
};

#endif // RARDIALOG_H
