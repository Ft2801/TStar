#include "SwapManager.h"
#include "../ImageBuffer.h"
#include <QDebug>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <unistd.h>
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
#elif defined(__APPLE__)
    // macOS implementation
    int64_t totalPhysMem = 0;
    size_t len = sizeof(totalPhysMem);
    if (sysctlbyname("hw.memsize", &totalPhysMem, &len, NULL, 0) != 0) return 0.0;
    
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vmstat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vmstat, &count) != KERN_SUCCESS) {
        return 0.0;
    }
    
    long page_size = sysconf(_SC_PAGESIZE);
    // Available = free + inactive + speculative
    double availPhysMem = (double)(vmstat.free_count + vmstat.inactive_count + vmstat.speculative_count) * page_size;
    double physMemUsed = (double)totalPhysMem - availPhysMem;
    return (physMemUsed / (double)totalPhysMem) * 100.0;
#else
    // Linux implementation 
    long pages = sysconf(_SC_PHYS_PAGES);
    long avail_pages = sysconf(_SC_AV_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0) return 0.0;
    
    double totalPhysMem = (double)pages * page_size;
    double physMemUsed = (double)(pages - avail_pages) * page_size;
    return (physMemUsed / totalPhysMem) * 100.0;
#endif
}

void SwapManager::checkMemoryPressure() {
    double usage = getMemoryUsagePercent();
    
    if (usage < m_maxRamUsagePercent) return;
    
    qDebug() << "[SwapManager] RAM pressure detected:" << usage << "% (threshold:" << m_maxRamUsagePercent << "%)";
    
    // RAM is high! Find candidates to swap.
    // Strategy: Sort buffers by Last Access Time (LRU)
    
    QMutexLocker lock(&m_listMutex);
    
    qDebug() << "[SwapManager] Registered buffers:" << m_buffers.size();
    
    // Copy list to sort
    std::vector<ImageBuffer*> candidates;
    for (int i = 0; i < m_buffers.size(); ++i) {
        ImageBuffer* buf = m_buffers[i];
        qDebug() << "[SwapManager] Buffer" << i << ":" << (void*)buf << "name:" << buf->name() 
                 << "swapped:" << buf->isSwapped() << "canSwap:" << buf->canSwap();
        if (!buf->isSwapped() && buf->canSwap()) {
             candidates.push_back(buf);
        }
    }
    
    if (candidates.empty()) {
        qDebug() << "[SwapManager] No swap candidates.";
        return;
    }
    
    // Sort: Smallest timestamp (oldest) first
    std::sort(candidates.begin(), candidates.end(), [](ImageBuffer* a, ImageBuffer* b){
        return a->getLastAccessTime() < b->getLastAccessTime();
    });
    
    // Evict buffers until memory pressure reduces 
    // Or just swap standard batch (e.g. 1 at a time to avoid lag spike)
    // Swap the oldest buffer first.
    
    for (ImageBuffer* buf : candidates) {
        qDebug() << "[SwapManager] Attempting swap out:" << buf->name() << (void*)buf;
        if (buf->trySwapOut()) {
            qInfo() << "[SwapManager] Swapped out:" << buf->name() << "due to RAM pressure:" << usage << "%";
            // Swap one buffer per cycle to maintain UI responsiveness
            break;
        }
    }
}
