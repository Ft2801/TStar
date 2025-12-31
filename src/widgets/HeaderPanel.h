#ifndef HEADERPANEL_H
#define HEADERPANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include "../ImageBuffer.h"

// Refactored from HeaderViewerDialog to be a reusable panel
class HeaderPanel : public QWidget {
    Q_OBJECT
public:
    explicit HeaderPanel(QWidget* parent = nullptr);
    
    // Updates the view with new metadata
    void setMetadata(const ImageBuffer::Metadata& meta);
    
    // Clears the view
    void clear();

private:
    void setupUI();
    void filterRows(const QString& text);
    
    ImageBuffer::Metadata m_meta;
    QTableWidget* m_table;
    QLineEdit* m_searchBox;
};

#endif // HEADERPANEL_H
