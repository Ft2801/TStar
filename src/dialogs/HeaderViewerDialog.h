#ifndef HEADERVIEWERDIALOG_H
#define HEADERVIEWERDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include "ImageBuffer.h"

class HeaderViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit HeaderViewerDialog(const ImageBuffer::Metadata& meta, QWidget* parent = nullptr);
    void setViewer(class ImageViewer* v);
    
protected:
    void showEvent(QShowEvent* event) override;

private:
    void setupUI();
    void filterRows(const QString& text);
    
    ImageBuffer::Metadata m_meta;
    QTableWidget* m_table;
    QLineEdit* m_searchBox;
};

#endif // HEADERVIEWERDIALOG_H
