#include "TaskManager.h"
#include "ResourceManager.h"
#include "Logger.h"

#include <QMutexLocker>
#include <QThread>

namespace Threading {

// ── Singleton ─────────────────────────────────────────────────────────────────
TaskManager& TaskManager::instance()
{
    static TaskManager _instance;
    return _instance;
}

TaskManager::TaskManager(QObject* parent)
    : QObject(parent)
{
    // Sensible default before init() is called.
    m_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
}

TaskManager::~TaskManager()
{
    // Best-effort: cancel pending work and wait for running tasks.
    cancelAll();
    m_pool.waitForDone(5000);
}

// ── Initialization ────────────────────────────────────────────────────────────
void TaskManager::init()
{
    const int n = ResourceManager::instance().maxThreads();
    m_pool.setMaxThreadCount(n);
    Logger::info(QString("TaskManager: pool size = %1 thread(s)").arg(n), "Threading");
}

// ── Submit ────────────────────────────────────────────────────────────────────
Task::Id TaskManager::submit(std::shared_ptr<Task> task)
{
    Q_ASSERT(task);

    const Task::Id id = task->id();

    // ── Register under lock ───────────────────────────────────────────────────
    {
        QMutexLocker lk(&m_mutex);
        m_tasks.insert(id, task);
    }

    // ── Wire task signals → manager signals ──────────────────────────────────
    // Connections use Qt::AutoConnection; since the Task QObject was created in
    // the GUI thread and run() fires in a pool thread, Qt routes these as
    // queued connections automatically.

    connect(task.get(), &Task::started,
            this, [this](quint64 tid) { emit taskStarted(tid); });

    connect(task.get(), &Task::progress,
            this, [this](quint64 tid, int pct, const QString& msg) {
                emit taskProgress(tid, pct, msg);
            });

    connect(task.get(), &Task::finished,
            this, [this](quint64 tid) {
                emit taskFinished(tid);
                onTaskDone(tid);
            });

    connect(task.get(), &Task::failed,
            this, [this](quint64 tid, const QString& err) {
                emit taskFailed(tid, err);
                onTaskDone(tid);
            });

    connect(task.get(), &Task::cancelled,
            this, [this](quint64 tid) {
                emit taskCancelled(tid);
                onTaskDone(tid);
            });

    // ── Submit to pool – higher enum value = higher QThreadPool priority ──────
    m_pool.start(task.get(), static_cast<int>(task->priority()));

    return id;
}

// ── Cancel one task ───────────────────────────────────────────────────────────
void TaskManager::cancel(Task::Id id)
{
    QMutexLocker lk(&m_mutex);
    auto it = m_tasks.find(id);
    if (it != m_tasks.end()) {
        it.value()->cancel();
    }
}

// ── Cancel all tasks ──────────────────────────────────────────────────────────
void TaskManager::cancelAll()
{
    // Collect snapshot under lock, then cancel without holding the lock
    // (cancel() itself is lock-free).
    QList<std::shared_ptr<Task>> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot.reserve(m_tasks.size());
        for (auto& t : m_tasks) {
            snapshot.append(t);
        }
    }
    for (auto& t : snapshot) {
        t->cancel();
    }
}

// ── Housekeeping: remove a completed/failed/cancelled task ────────────────────
void TaskManager::onTaskDone(quint64 id)
{
    bool empty = false;
    {
        QMutexLocker lk(&m_mutex);
        m_tasks.remove(id);
        empty = m_tasks.isEmpty();
    }
    if (empty) {
        emit allTasksDone();
    }
}

// ── Status queries ────────────────────────────────────────────────────────────
int TaskManager::activeCount() const
{
    return m_pool.activeThreadCount();
}

int TaskManager::trackedCount() const
{
    QMutexLocker lk(&m_mutex);
    return m_tasks.size();
}

bool TaskManager::waitForAll(int msTimeout)
{
    return m_pool.waitForDone(msTimeout);
}

int TaskManager::maxThreads() const
{
    return m_pool.maxThreadCount();
}

} // namespace Threading
