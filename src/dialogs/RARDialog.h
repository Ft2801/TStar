#ifndef RARDIALOG_H
#define RARDIALOG_H

#include "DialogBase.h"
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPointer>
#include "../ImageViewer.h"

class MainWindowCallbacks;

class RARDialog : public DialogBase {
    Q_OBJECT
public:
    explicit RARDialog(QWidget* parent = nullptr);

    void setViewer(class ImageViewer* v);

private slots:
    void onBrowseModel();
    void onDownloadModel();
    void onRun();

private:
    QLineEdit* m_editModelPath;
    QSpinBox* m_spinPatch;
    QSpinBox* m_spinOverlap;
    QComboBox* m_comboProvider;
    QLabel* m_lblStatus;
    
    QPointer<class ImageViewer> m_viewer;
};

#endif // RARDIALOG_H
