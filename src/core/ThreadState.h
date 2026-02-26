
#ifndef THREAD_STATE_H
#define THREAD_STATE_H

#include <atomic>

namespace Threading {

/**
 * @brief Application-wide "keep running" flag.
 *
 * ⚠️  This is a GLOBAL flag – calling requestCancel() cancels ALL tasks at once.
 *     For per-task cancellation use:
 *       • Threading::CancelToken  (for existing QThread-based workers)
 *       • Threading::Task + TaskManager  (for new work; preferred)
 *
 * Typical use: only call requestCancel() on application shutdown or when the
 * user wants to stop *all* background activity simultaneously.
 */
class ThreadState {
public:
    /** @return false if a global cancellation was requested. */
    static bool shouldRun() {
        return s_shouldRun.load(std::memory_order_acquire);
    }

    static void setRun(bool run) {
        s_shouldRun.store(run, std::memory_order_release);
    }

    /** Stop ALL running tasks (application-wide). Prefer CancelToken for per-task control. */
    static void requestCancel() { setRun(false); }

    /** Re-arm the global flag (call after cancellation resolved). */
    static void reset() { setRun(true); }

private:
    static std::atomic<bool> s_shouldRun;
};


/**
 * @brief Lightweight per-task cancellation token.
 *
 * Drop-in upgrade for `std::atomic<bool> m_cancelled` in QThread workers:
 *
 * @code
 *   // Old pattern (problematic – no global flag awareness):
 *   std::atomic<bool> m_cancelled{false};
 *   ...
 *   if (m_cancelled) return;
 *
 *   // New pattern:
 *   Threading::CancelToken m_cancelToken;
 *   ...
 *   if (!m_cancelToken.shouldContinue()) return;
 *
 *   // Request cancellation from the dialog destructor / cancel button:
 *   m_cancelToken.cancel();
 * @endcode
 *
 * shouldContinue() returns false when EITHER:
 *  - cancel() has been called on this token, OR
 *  - ThreadState::shouldRun() is false (global shutdown).
 */
class CancelToken {
public:
    /** Request cancellation of this specific operation. Thread-safe. */
    void cancel()  { m_cancelled.store(true,  std::memory_order_release); }

    /** Re-arm for a fresh operation on the same token. */
    void reset()   { m_cancelled.store(false, std::memory_order_release); }

    /** True once cancel() has been called. */
    bool isCancelled() const { return m_cancelled.load(std::memory_order_acquire); }

    /**
     * @return false when cancel() was called OR when the global ThreadState
     *         requestCancel() was called.  Use this in inner loops.
     */
    bool shouldContinue() const {
        return !isCancelled() && ThreadState::shouldRun();
    }

private:
    std::atomic<bool> m_cancelled{false};
};


// ── Backward-compatible free functions ───────────────────────────────────────
inline bool getThreadRun() { return ThreadState::shouldRun(); }
inline void setThreadRun(bool run) { ThreadState::setRun(run); }

} // namespace Threading

#endif // THREAD_STATE_H
