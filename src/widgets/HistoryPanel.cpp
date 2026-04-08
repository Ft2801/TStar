#include "HistoryPanel.h"
#include "../ImageViewer.h"

#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFont>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
    , m_list(nullptr)
    , m_currentViewer(nullptr)
{
    setupUI();
}

// -----------------------------------------------------------------------------
// Private methods
// -----------------------------------------------------------------------------

void HistoryPanel::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);

    // Use a slightly smaller font to keep the list compact.
    QFont listFont = m_list->font();
    listFont.setPointSize(10);
    m_list->setFont(listFont);

    connect(m_list, &QListWidget::itemClicked,
            this,   &HistoryPanel::onItemClicked);

    layout->addWidget(m_list);
}

// -----------------------------------------------------------------------------
// Public methods
// -----------------------------------------------------------------------------

void HistoryPanel::clear()
{
    m_list->clear();
    m_currentViewer = nullptr;
}

void HistoryPanel::updateHistory(ImageViewer* viewer)
{
    m_list->clear();
    m_currentViewer = viewer;

    if (!viewer)
        return;

    // The first entry always represents the unmodified initial state.
    m_list->addItem(tr("[Initial State]"));

    // Append one entry per operation recorded in the undo stack.
    // These represent states that have already been applied, in chronological order.
    for (const QString& description : viewer->undoDescriptions())
        m_list->addItem(description);

    // Highlight the current state (the last entry added so far) in bold
    // so the user can immediately identify where they are in the history.
    const int currentRow = m_list->count() - 1;
    QListWidgetItem* currentItem = m_list->item(currentRow);
    if (currentItem)
    {
        QFont font = currentItem->font();
        font.setBold(true);
        currentItem->setFont(font);
    }

    // Append redo entries in reverse order so that the next redoable action
    // appears immediately after the current state in the list.
    const std::vector<QString>& redoEntries = viewer->redoDescriptions();
    for (auto it = redoEntries.rbegin(); it != redoEntries.rend(); ++it)
        m_list->addItem(*it);

    m_list->setCurrentRow(currentRow);
}

// -----------------------------------------------------------------------------
// Private slots
// -----------------------------------------------------------------------------

void HistoryPanel::onItemClicked(QListWidgetItem* item)
{
    if (!m_currentViewer)
        return;

    const int index = m_list->row(item);
    if (index < 0)
        return;

    emit historyStateSelected(index);
}