#include "SwapManager.h"
#include "../ImageBuffer.h"
#include <QDebug>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

SwapManager& SwapManager::instance() {
    static SwapManager s_instance;
    return s_instance;
}

SwapManager::SwapManager() {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &SwapManager::checkMemoryPressure);
    m_timer->start(DEFAULT_CHECK_INTERVAL_MS);
}

SwapManager::~SwapManager() {
    m_timer->stop();
    // No need to delete buffers, we don't own them
}

void SwapManager::registerBuffer(ImageBuffer* buffer) {
    QMutexLocker lock(&m_listMutex);
    if (!m_buffers.contains(buffer)) {
        m_buffers.append(buffer);
    }
}

void SwapManager::unregisterBuffer(ImageBuffer* buffer) {
    QMutexLocker lock(&m_listMutex);
    m_buffers.removeAll(buffer);
}

double SwapManager::getMemoryUsagePercent() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
    DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
    return (static_cast<double>(physMemUsed) / totalPhysMem) * 100.0;
#else
    // Minimal Linux/Mac stub - assuming okay for now or use sysconf
    // Todo: Impl for Mac
    return 0.0; 
#endif
}

void SwapManager::checkMemoryPressure() {
    double usage = getMemoryUsagePercent();
    
    if (usage < m_maxRamUsagePercent) return;
    
    // RAM is high! Find candidates to swap.
    // Strategy: Sort buffers by Last Access Time (LRU)
    
    QMutexLocker lock(&m_listMutex);
    
    // Copy list to sort
    std::vector<ImageBuffer*> candidates;
    for (ImageBuffer* buf : m_buffers) {
        if (!buf->isSwapped() && buf->canSwap()) {
             candidates.push_back(buf);
        }
    }
    
    if (candidates.empty()) return;
    
    // Sort: Smallest timestamp (oldest) first
    std::sort(candidates.begin(), candidates.end(), [](ImageBuffer* a, ImageBuffer* b){
        return a->getLastAccessTime() < b->getLastAccessTime();
    });
    
    // Swap out until usage drops? 
    // Or just swap standard batch (e.g. 1 at a time to avoid lag spike)
    // Let's swap the oldest one.
    
    for (ImageBuffer* buf : candidates) {
        if (buf->trySwapOut()) {
            qInfo() << "[SwapManager] Swapped out:" << buf->name() << "due to RAM pressure:" << usage << "%";
            // Check pressure again? If we cleared big chunk, maybe stop.
            // For now, swap one per cycle to avoid freezing UI.
            break;
        }
    }
}
