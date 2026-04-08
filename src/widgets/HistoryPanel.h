#ifndef HISTORYPANEL_H
#define HISTORYPANEL_H

#include <QWidget>
#include <QListWidget>

class ImageViewer;

/**
 * @class HistoryPanel
 * @brief A panel widget that displays the undo/redo history of an ImageViewer instance.
 *
 * HistoryPanel renders a list of state descriptions representing the full editing
 * history of the currently active ImageViewer. The current state is visually
 * highlighted, while past and future (redoable) states are listed above and below
 * it respectively. Clicking an entry emits a signal that can be used to restore
 * the application to that specific state.
 */
class HistoryPanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the HistoryPanel widget.
     * @param parent Optional parent widget.
     */
    explicit HistoryPanel(QWidget* parent = nullptr);

    /**
     * @brief Populates the history list with the undo/redo stack of the given viewer.
     *
     * Displays all past states (undo stack) followed by future states (redo stack).
     * The current state entry is rendered in bold. Passing nullptr clears the panel.
     *
     * @param viewer Pointer to the ImageViewer whose history should be displayed.
     */
    void updateHistory(ImageViewer* viewer);

    /**
     * @brief Clears the history list and releases the reference to the current viewer.
     */
    void clear();

signals:
    /**
     * @brief Emitted when the user clicks a history entry.
     *
     * The index corresponds to the row in the list, where 0 represents the
     * initial state and each subsequent index represents an applied operation.
     *
     * @param index The row index of the selected history entry.
     */
    void historyStateSelected(int index);

private slots:
    /**
     * @brief Handles a click on a list item and emits historyStateSelected.
     * @param item The list widget item that was clicked.
     */
    void onItemClicked(QListWidgetItem* item);

private:
    /**
     * @brief Initializes the widget layout and internal list widget.
     */
    void setupUI();

    QListWidget* m_list;          ///< The list widget used to display history entries.
    ImageViewer* m_currentViewer; ///< Non-owning pointer to the currently tracked viewer.
};

#endif // HISTORYPANEL_H