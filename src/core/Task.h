#ifndef TSTAR_TASK_H
#define TSTAR_TASK_H

#include <QObject>
#include <QRunnable>
#include <QString>
#include <atomic>

namespace Threading {

/**
 * @brief Abstract task with unique ID, per-task cancellation, priority and progress reporting.
 *
 * Usage:
 * @code
 *   class MyTask : public Threading::Task {
 *   protected:
 *       void execute() override {
 *           for (int i = 0; i < 100 && shouldContinue(); ++i) {
 *               doWork(i);
 *               reportProgress(i + 1, tr("Processing %1 / 100").arg(i + 1));
 *           }
 *       }
 *   };
 *
 *   auto t = std::make_shared<MyTask>();
 *   connect(t.get(), &Task::progress, this, &MyDialog::onProgress);
 *   TaskManager::instance().submit(t);
 * @endcode
 *
 * Key properties
 * --------------
 *  - Every task has a unique quint64 ID assigned on construction.
 *  - Cancellation is per-task: calling cancel() only stops *this* task.
 *  - shouldContinue() also respects the global ThreadState flag, so a blanket
 *    ThreadState::requestCancel() still halts all running tasks.
 *  - Signals are always delivered on the thread where the Task QObject was
 *    created (normally the main / GUI thread) via Qt::AutoConnection.
 *  - setAutoDelete(false): lifetime is managed by std::shared_ptr inside TaskManager.
 */
class Task : public QObject, public QRunnable
{
    Q_OBJECT
    Q_DISABLE_COPY(Task)

public:
    using Id = quint64;

    /**
     * @brief Priority, mapped to QThreadPool's int priority (higher = sooner).
     */
    enum class Priority : int {
        Background = 0,   ///< Housekeeping, model downloads
        Low        = 1,   ///< Bulk batch work
        Normal     = 2,   ///< Standard user-triggered operations
        High       = 3,   ///< Interactive / time-critical operations
        Critical   = 4    ///< Must run immediately (e.g. cancel confirmations)
    };
    Q_ENUM(Priority)

    enum class Status {
        Pending,      ///< Submitted to pool, not yet started
        Running,      ///< execute() is in progress
        Cancelled,    ///< cancel() was called; execute() returned early
        Failed,       ///< execute() threw an exception
        Completed     ///< execute() returned normally
    };
    Q_ENUM(Status)

    explicit Task(Priority priority = Priority::Normal, QObject* parent = nullptr);
    ~Task() override = default;

    /** Unique ID assigned on construction. Never reused within an application run. */
    Id id() const noexcept { return m_id; }

    Priority priority() const noexcept { return m_priority; }

    Status status() const noexcept { return m_status.load(std::memory_order_acquire); }

    /** True while status is Pending or Running. */
    bool isActive() const noexcept;

    /** True once cancel() has been called (status may still be Running). */
    bool isCancelled() const noexcept { return m_cancelRequested.load(std::memory_order_acquire); }

    /**
     * @brief Request graceful cancellation.
     * Thread-safe; sets isCancelled() and signals the task to stop.
     * Does NOT force-terminate the thread.
     */
    void cancel();

signals:
    /** Emitted just before execute() is called. */
    void started(quint64 id);

    /**
     * @brief Emitted periodically from execute() via reportProgress().
     * @param id      Task ID
     * @param percent 0–100 completion estimate
     * @param message Human-readable status string (may be empty)
     */
    void progress(quint64 id, int percent, const QString& message);

    /** Emitted after execute() returns successfully. */
    void finished(quint64 id);

    /** Emitted when execute() threw an exception. */
    void failed(quint64 id, const QString& errorMessage);

    /** Emitted when the task stopped early due to cancel(). */
    void cancelled(quint64 id);

protected:
    /**
     * @brief Override this to implement the task body.
     * Run in the thread pool thread.  Check shouldContinue() frequently.
     */
    virtual void execute() = 0;

    /**
     * @brief Emit a progress update. Call from within execute().
     * @param percent 0–100
     * @param message Optional status string
     */
    void reportProgress(int percent, const QString& message = {});

    /**
     * @brief Returns false when cancel() has been called or when the global
     *        ThreadState::shouldRun() flag is cleared.  Call this in tight loops.
     */
    bool shouldContinue() const noexcept;

private:
    /** Called by QThreadPool; manages state transitions and delegates to execute(). */
    void run() override final;

    const Id       m_id;
    const Priority m_priority;
    std::atomic<Status> m_status { Status::Pending };
    std::atomic<bool>   m_cancelRequested { false };

    static std::atomic<Id> s_nextId;
};

} // namespace Threading

#endif // TSTAR_TASK_H
