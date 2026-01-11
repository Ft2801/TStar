
#ifndef THREAD_STATE_H
#define THREAD_STATE_H

#include <atomic>
#include <QMutex>

namespace Threading {

class ThreadState {
public:
    static bool shouldRun() {
        return s_shouldRun.load(std::memory_order_acquire);
    }
    
    static void setRun(bool run) {
        s_shouldRun.store(run, std::memory_order_release);
    }
    
    static void requestCancel() {
        setRun(false);
    }
    
    static void reset() {
        setRun(true);
    }

private:
    static std::atomic<bool> s_shouldRun;
};

inline bool getThreadRun() { return ThreadState::shouldRun(); }
inline void setThreadRun(bool run) { ThreadState::setRun(run); }

}

#endif
