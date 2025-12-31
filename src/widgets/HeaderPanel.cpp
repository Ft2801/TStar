#include "HeaderPanel.h"
#include <QHeaderView>

HeaderPanel::HeaderPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void HeaderPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);
    
    // Search Box
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Filter FITS keys..."));
    m_searchBox->setStyleSheet("padding: 4px; border: 1px solid #555; background: #333; color: white;");
    connect(m_searchBox, &QLineEdit::textChanged, this, &HeaderPanel::filterRows);
    layout->addWidget(m_searchBox);
    
    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Key"), tr("Value"), tr("Comment")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet(
        "QTableWidget { background: #222; color: #ddd; gridline-color: #444; }"
        "QHeaderView::section { background: #333; color: #eee; padding: 4px; border: 1px solid #444; }"
        "QTableWidget::item { padding: 4px; }"
        "QTableWidget::item:selected { background: #0055aa; }"
    );
    
    // Enable word wrap
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    
    layout->addWidget(m_table, 1);
}

void HeaderPanel::setMetadata(const ImageBuffer::Metadata& meta) {
    m_meta = meta;
    m_table->setRowCount(0); // Clear
    
    m_table->setRowCount(m_meta.rawHeaders.size());
    for(size_t i=0; i<m_meta.rawHeaders.size(); ++i) {
        const auto& card = m_meta.rawHeaders[i];
        
        QTableWidgetItem* k = new QTableWidgetItem(card.key);
        QTableWidgetItem* v = new QTableWidgetItem(card.value);
        QTableWidgetItem* c = new QTableWidgetItem(card.comment);
        
        k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        c->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        
        m_table->setItem(i, 0, k);
        m_table->setItem(i, 1, v);
        m_table->setItem(i, 2, c);
    }
    
    // Re-apply filter if text present
    if (!m_searchBox->text().isEmpty()) {
        filterRows(m_searchBox->text());
    }
}

void HeaderPanel::clear() {
    m_table->setRowCount(0);
}

void HeaderPanel::filterRows(const QString& text) {
    if (text.isEmpty()) {
        for(int i=0; i<m_table->rowCount(); ++i) m_table->setRowHidden(i, false);
        return;
    }
    
    QString lower = text.toLower();
    for(int i=0; i<m_table->rowCount(); ++i) {
        bool match = false;
        if (m_table->item(i, 0)->text().toLower().contains(lower)) match = true;
        else if (m_table->item(i, 1)->text().toLower().contains(lower)) match = true;
        else if (m_table->item(i, 2)->text().toLower().contains(lower)) match = true;
        m_table->setRowHidden(i, !match);
    }
}
