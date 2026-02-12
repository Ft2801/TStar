#include "HeaderEditorDialog.h"
#include "../ImageViewer.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QGroupBox>

HeaderEditorDialog::HeaderEditorDialog(ImageViewer* viewer, QWidget* parent) 
    : DialogBase(parent, tr("Header Editor"), 700, 600), m_viewer(viewer) {
    if (viewer) {
        m_meta = viewer->getBuffer().metadata();
        setWindowTitle(tr("FITS Header Editor - %1").arg(viewer->windowTitle()));
    }
    setupUI();
    loadMetadata();

}

void HeaderEditorDialog::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    // Top: Add New Key
    QGroupBox* addGroup = new QGroupBox(tr("Add/Update Key"), this);
    QHBoxLayout* addLayout = new QHBoxLayout(addGroup);
    
    m_keyInput = new QLineEdit(this);
    m_keyInput->setPlaceholderText(tr("KEYWORD"));
    m_keyInput->setMaxLength(8); // FITS standard
    
    m_valInput = new QLineEdit(this);
    m_valInput->setPlaceholderText(tr("Value"));
    
    m_commentInput = new QLineEdit(this);
    m_commentInput->setPlaceholderText(tr("Comment"));
    
    m_addBtn = new QPushButton(tr("Set"), this);
    connect(m_addBtn, &QPushButton::clicked, this, &HeaderEditorDialog::onAdd);
    
    addLayout->addWidget(m_keyInput, 1);
    addLayout->addWidget(m_valInput, 2);
    addLayout->addWidget(m_commentInput, 2);
    addLayout->addWidget(m_addBtn);
    
    layout->addWidget(addGroup);
    
    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Key"), tr("Value"), tr("Comment")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    
    // Connect selection to populate inputs
    connect(m_table, &QTableWidget::itemSelectionChanged, [this]() {
        auto items = m_table->selectedItems();
        if (items.size() >= 3) {
            int row = items[0]->row();
            m_keyInput->setText(m_table->item(row, 0)->text());
            m_valInput->setText(m_table->item(row, 1)->text());
            m_commentInput->setText(m_table->item(row, 2)->text());
        }
    });

    layout->addWidget(m_table);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    m_delBtn = new QPushButton(tr("Delete Selected"), this);
    connect(m_delBtn, &QPushButton::clicked, this, &HeaderEditorDialog::onDelete);
    
    m_saveBtn = new QPushButton(tr("Save Changes"), this);
    connect(m_saveBtn, &QPushButton::clicked, this, &HeaderEditorDialog::onSave);
    
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    
    btnLayout->addWidget(m_delBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(closeBtn);
    
    layout->addLayout(btnLayout);
}

void HeaderEditorDialog::loadMetadata() {
    m_table->setRowCount(0);
    m_table->setRowCount(m_meta.rawHeaders.size());
    for(size_t i=0; i<m_meta.rawHeaders.size(); ++i) {
        const auto& card = m_meta.rawHeaders[i];
        m_table->setItem(i, 0, new QTableWidgetItem(card.key));
        m_table->setItem(i, 1, new QTableWidgetItem(card.value));
        m_table->setItem(i, 2, new QTableWidgetItem(card.comment));
    }
}

void HeaderEditorDialog::onAdd() {
    QString key = m_keyInput->text().trimmed().toUpper();
    QString val = m_valInput->text();
    QString comment = m_commentInput->text();
    
    if (key.isEmpty()) return;
    
    // Check if exists
    bool found = false;
    for(auto& card : m_meta.rawHeaders) {
        if (card.key == key) {
            card.value = val;
            card.comment = comment;
            found = true;
            break;
        }
    }
    
    if (!found) {
        m_meta.rawHeaders.push_back({key, val, comment});
    }
    
    loadMetadata();
    // Re-select?
}

void HeaderEditorDialog::onDelete() {
    int row = m_table->currentRow();
    if (row < 0) return;
    
    QString key = m_table->item(row, 0)->text();
    
    // Remove from vector
    for(auto it = m_meta.rawHeaders.begin(); it != m_meta.rawHeaders.end(); ++it) {
        if (it->key == key) {
            m_meta.rawHeaders.erase(it);
            break;
        }
    }
    loadMetadata();
}

void HeaderEditorDialog::onSave() {
    if (!m_viewer) return;
    
    // 1. Update Viewer Metadata
    ImageBuffer& buf = m_viewer->getBuffer();
    buf.setMetadata(m_meta);
    
    // 2. Ask to save file
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Save File"), 
        tr("Apply changes and save file?\nWarning: This overwrites the existing file if you choose Yes.\nChoose No to only apply to memory (you can Save As later)."),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        
    if (reply == QMessageBox::Cancel) return;
    
    if (reply == QMessageBox::Yes) {
        QString path = QFileDialog::getSaveFileName(this, tr("Save FITS"), "", "FITS Files (*.fits)");
        if (!path.isEmpty()) {
            QString err;
            // Default to 32-bit Float as that is the internal representation
            if (!buf.save(path, "FITS", ImageBuffer::Depth_32Float, &err)) { 
               QMessageBox::critical(this, tr("Error"), tr("Failed to save: %1").arg(err));
            } else {
               QMessageBox::information(this, tr("Success"), tr("File saved."));
            }
        }
    } else {
       // Just memory apply
    }
    
    m_viewer->setModified(true);
    accept();
}
