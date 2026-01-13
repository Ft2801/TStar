#ifndef NEW_PROJECT_DIALOG_H
#define NEW_PROJECT_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include "../stacking/StackingProject.h"

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);
    
    QString projectPath() const;
    QString projectName() const;
    
    Stacking::StackingProject* createProject();
    
private slots:
    void onBrowse();
    void onAccept();
    void validateInputs();
    
private:
    void setupUI();
    
    QLineEdit* m_pathEdit;
    QLineEdit* m_nameEdit;
    QPushButton* m_browseBtn;
    QPushButton* m_createBtn;
    QPushButton* m_cancelBtn;
};

#endif // NEW_PROJECT_DIALOG_H
