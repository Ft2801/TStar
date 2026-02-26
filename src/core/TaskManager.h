#ifndef TSTAR_TASKMANAGER_H
#define TSTAR_TASKMANAGER_H

#include "Task.h"

#include <QObject>
#include <QMutex>
#include <QThreadPool>
#include <QHash>
#include <memory>

namespace Threading {

/**
 * @brief Central manager for all background tasks.
 *
 * TaskManager owns a private QThreadPool sized at ResourceManager::maxThreads().
 * All long-running background work should be submitted here rather than creating
 * raw QThread objects, so that:
 *  - Thread count is bounded by the 90 % CPU rule.
 *  - Tasks are prioritised (Critical > High > Normal > Low > Background).
 *  - Individual tasks can be cancelled by ID without disturbing others.
 *  - Global progress / completion signals are available for a status-bar widget.
 *
 * Initialization
 * --------------
 * Call TaskManager::instance().init() once after ResourceManager::instance().init().
 *
 * Submitting a task
 * -----------------
 * @code
 *   auto task = std::make_shared<MyTask>();
 *   // Optional: connect per-task signals directly
 *   connect(task.get(), &Task::progress, this, &MyDialog::onProgress);
 *   // Submit – TaskManager keeps the shared_ptr alive until completion
 *   Task::Id id = TaskManager::instance().submit(task);
 *   // Store id if you need to cancel later
 *   m_taskId = id;
 * @endcode
 *
 * Cancelling a specific task
 * --------------------------
 * @code
 *   TaskManager::instance().cancel(m_taskId);
 * @endcode
 *
 * Cancelling everything (e.g. on application quit)
 * -------------------------------------------------
 * @code
 *   TaskManager::instance().cancelAll();
 * @endcode
 *
 * Thread safety
 * -------------
 * All public methods are thread-safe.
 */
class TaskManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(TaskManager)

public:
    static TaskManager& instance();

    /**
     * @brief Must be called once after ResourceManager::instance().init().
     * Sets the pool's thread limit to ResourceManager::maxThreads().
     */
    void init();

    /**
     * @brief Submit a task for execution.
     * The task is started on the private thread pool with its declared priority.
     * TaskManager holds a shared_ptr reference until the task finishes.
     *
     * @param task  Shared task to run (must not be null).
     * @return The task's unique ID for later cancellation.
     */
    Task::Id submit(std::shared_ptr<Task> task);

    /**
     * @brief Request cancellation of one task by ID.
     * Safe to call even after the task has already completed; no-op in that case.
     */
    void cancel(Task::Id id);

    /**
     * @brief Request cancellation of ALL tracked tasks.
     * Pending tasks in the queue are also removed before they start.
     */
    void cancelAll();

    /** @brief Number of tasks currently executing in the pool. */
    int activeCount() const;

    /** @brief Total number of tasks known to the manager (running + pending registration). */
    int trackedCount() const;

    /**
     * @brief Block until all running tasks finish (or timeout expires).
     * @param msTimeout  Maximum wait in milliseconds; -1 means wait forever.
     * @return true if all tasks finished within the timeout.
     */
    bool waitForAll(int msTimeout = -1);

    /** @brief Maximum threads used by the internal pool. */
    int maxThreads() const;

signals:
    // ── Mirrors of Task signals, re-emitted globally ──────────────────────────

    /** Emitted when any task starts executing. */
    void taskStarted(quint64 id);

    /** Emitted periodically by any running task. */
    void taskProgress(quint64 id, int percent, const QString& message);

    /** Emitted when any task completes successfully. */
    void taskFinished(quint64 id);

    /** Emitted when any task fails with an exception. */
    void taskFailed(quint64 id, const QString& errorMessage);

    /** Emitted when any task is cancelled. */
    void taskCancelled(quint64 id);

    /**
     * Emitted when the last tracked task finishes / cancels / fails.
     * Useful for hiding a global busy indicator.
     */
    void allTasksDone();

private:
    explicit TaskManager(QObject* parent = nullptr);
    ~TaskManager() override;

    /** Called via signal from each task; removes it from the registry. */
    void onTaskDone(quint64 id);

    QThreadPool         m_pool;          ///< Private pool – NOT QThreadPool::globalInstance()
    mutable QMutex      m_mutex;         ///< Guards m_tasks
    QHash<Task::Id, std::shared_ptr<Task>> m_tasks; ///< All live tasks
};

} // namespace Threading

#endif // TSTAR_TASKMANAGER_H
