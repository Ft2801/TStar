#ifndef HEADEREDITORDIALOG_H
#define HEADEREDITORDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include "ImageBuffer.h"

class ImageViewer;

class HeaderEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit HeaderEditorDialog(ImageViewer* viewer, QWidget* parent = nullptr);

private slots:
    void onAdd();
    void onDelete();
    void onSave(); // Updates ImageViewer and Saves File
    // void onSaveAs(); // Maybe later

private:
    void setupUI();
    void loadMetadata();
    
    ImageViewer* m_viewer;
    ImageBuffer::Metadata m_meta; // Local copy to edit
    
    QTableWidget* m_table;
    QPushButton* m_addBtn;
    QPushButton* m_delBtn;
    QPushButton* m_saveBtn;
    QLineEdit* m_keyInput;
    QLineEdit* m_valInput;
    QLineEdit* m_commentInput;
};

#endif // HEADEREDITORDIALOG_H
