#include "Task.h"
#include "ThreadState.h"

#include <QMetaObject>
#include <stdexcept>

namespace Threading {

// ── Static ID counter ─────────────────────────────────────────────────────────
std::atomic<Task::Id> Task::s_nextId { 1 };

// ── Constructor ───────────────────────────────────────────────────────────────
Task::Task(Priority priority, QObject* parent)
    : QObject(parent)
    , QRunnable()
    , m_id(s_nextId.fetch_add(1, std::memory_order_relaxed))
    , m_priority(priority)
{
    // The TaskManager keeps us alive via shared_ptr; we must not self-delete.
    setAutoDelete(false);
}

// ── Cancellation ──────────────────────────────────────────────────────────────
void Task::cancel()
{
    // Mark cancel first; run() will detect this and emit cancelled().
    m_cancelRequested.store(true, std::memory_order_release);
}

// ── Continuation guard ────────────────────────────────────────────────────────
bool Task::shouldContinue() const noexcept
{
    // Per-task cancel takes priority; fall back to global ThreadState flag.
    if (m_cancelRequested.load(std::memory_order_acquire))
        return false;
    return ThreadState::shouldRun();
}

// ── Active test ───────────────────────────────────────────────────────────────
bool Task::isActive() const noexcept
{
    const Status s = status();
    return s == Status::Pending || s == Status::Running;
}

// ── Progress helper ───────────────────────────────────────────────────────────
void Task::reportProgress(int percent, const QString& message)
{
    // percent is clamped to [0, 100] defensively
    emit progress(m_id, qBound(0, percent, 100), message);
}

// ── Core run() ────────────────────────────────────────────────────────────────
//   Called by the QThreadPool in a pool thread.
//   Manages status state-machine and routes signals.
void Task::run()
{
    // ── Transition: Pending → Running ────────────────────────────────────────
    {
        Status expected = Status::Pending;
        if (!m_status.compare_exchange_strong(expected, Status::Running,
                                              std::memory_order_acq_rel)) {
            // Already cancelled before the pool got to start us – bail out.
            emit cancelled(m_id);
            return;
        }
    }

    emit started(m_id);

    // ── Execute ──────────────────────────────────────────────────────────────
    try {
        execute();
    }
    catch (const std::exception& ex) {
        m_status.store(Status::Failed, std::memory_order_release);
        emit failed(m_id, QString::fromUtf8(ex.what()));
        return;
    }
    catch (...) {
        m_status.store(Status::Failed, std::memory_order_release);
        emit failed(m_id, QStringLiteral("Unknown exception in task %1").arg(m_id));
        return;
    }

    // ── Post-execute: decide final status ────────────────────────────────────
    if (m_cancelRequested.load(std::memory_order_acquire)) {
        m_status.store(Status::Cancelled, std::memory_order_release);
        emit cancelled(m_id);
    } else {
        m_status.store(Status::Completed, std::memory_order_release);
        emit finished(m_id);
    }
}

} // namespace Threading
