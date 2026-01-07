#include "HeaderViewerDialog.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QTimer>
#include <QTableWidgetItem>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QPainter>

// Delegate to ensure word wrap works correctly
class HeaderWordWrapDelegate : public QStyledItemDelegate {
public:
    explicit HeaderWordWrapDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        
        painter->save();
        
        // Draw background (selection etc)
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, opt.palette.highlight());
        } else {
            painter->fillRect(opt.rect, opt.palette.base());
        }
        
        // Draw Text
        QRect rect = opt.rect.adjusted(4, 4, -4, -4); // Padding
        painter->setPen(opt.state & QStyle::State_Selected ? opt.palette.highlightedText().color() : opt.palette.text().color());
        
        // Force wrap
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, opt.text);
        
        painter->restore();
    }
    
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        
        // Get actual column width from the view
        const QTableWidget* table = qobject_cast<const QTableWidget*>(option.widget);
        int w = 200; // Fallback
        if (table) {
            w = table->columnWidth(index.column());
        }
        w -= 8; // Padding
        
        QFontMetrics fm(opt.font);
        QRect bound = fm.boundingRect(QRect(0, 0, w, 10000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, opt.text);
        
        return QSize(w, bound.height() + 10);
    }
};

HeaderViewerDialog::HeaderViewerDialog(const ImageBuffer::Metadata& meta, QWidget* parent) 
    : QDialog(parent), m_meta(meta) {
    setWindowTitle(tr("FITS/XISF Header Explorer"));
    resize(600, 500);
    setupUI();

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

void HeaderViewerDialog::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(5);
    
    // Search Box
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Filter keys..."));
    m_searchBox->setStyleSheet("padding: 5px; border: 1px solid #555; background: #333; color: white;");
    connect(m_searchBox, &QLineEdit::textChanged, this, &HeaderViewerDialog::filterRows);
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
        "QTableWidget { background: #222; color: #ddd; gridline-color: #444; outline: 0; }"
        "QHeaderView::section { background: #333; color: #eee; padding: 4px; border: 1px solid #444; }"
        "QTableWidget::item { padding: 6px; border-bottom: 1px solid #333; }"
        "QTableWidget::item:selected { background: #0055aa; }"
    );
    
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    
    
    // Populate
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
    
    // Set Delegate for Value Column (Index 1)
    m_table->setItemDelegateForColumn(1, new HeaderWordWrapDelegate(m_table));
    // Also for comment? Maybe
    m_table->setItemDelegateForColumn(2, new HeaderWordWrapDelegate(m_table));
    
    // Set column widths before resizing rows
    m_table->setColumnWidth(0, 150);
    // Force the value column to take available space. 
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    
    // Ensure word wrap works by triggering resize after layout
    QTimer::singleShot(50, this, [this](){
        m_table->resizeRowsToContents();
    });
    
    // Layout is handled by Stretch mode now
}

void HeaderViewerDialog::filterRows(const QString& text) {
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

void HeaderViewerDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // Trigger a resize once visible to ensure layout is calculated
    QTimer::singleShot(100, this, [this](){
        m_table->resizeRowsToContents();
    });
}

#include "../ImageViewer.h"

void HeaderViewerDialog::setViewer(ImageViewer* v) {
    if (!v || !v->getBuffer().isValid()) return;
    
    m_meta = v->getBuffer().metadata();
    
    QString title = v->windowTitle();
    if (title.isEmpty()) title = tr("Image");
    setWindowTitle(tr("FITS/XISF Header: %1").arg(title));
    
    m_table->setRowCount(0);
    m_table->setRowCount(m_meta.rawHeaders.size());
    
    for(size_t i=0; i<m_meta.rawHeaders.size(); ++i) {
        const auto& card = m_meta.rawHeaders[i];
        
        QTableWidgetItem* k = new QTableWidgetItem(card.key);
        QTableWidgetItem* val = new QTableWidgetItem(card.value);
        QTableWidgetItem* c = new QTableWidgetItem(card.comment);
        
        k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        val->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        c->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        
        m_table->setItem(i, 0, k);
        m_table->setItem(i, 1, val);
        m_table->setItem(i, 2, c);
    }
    
    // Re-apply filter if any
    filterRows(m_searchBox->text());
    
    // Resize
    m_table->resizeRowsToContents();
}
