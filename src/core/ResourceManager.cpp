#include "ResourceManager.h"
#include <QThread>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <QThreadPool>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_MAC
#include <mach/mach.h>
#endif

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#endif

ResourceManager& ResourceManager::instance() {
    static ResourceManager _instance;
    return _instance;
}

ResourceManager::ResourceManager() {
    // Default safe fallback
    m_maxThreads = std::max(1, QThread::idealThreadCount() - 1); 
}

void ResourceManager::init() {
    int totalCores = QThread::idealThreadCount();
    
    // User request: Limit to 90% CPU
    // We floor the result to be safe, but ensure at least 1 thread.
    // Example: 16 cores * 0.9 = 14.4 -> 14 threads.
    // Example: 8 cores * 0.9 = 7.2 -> 7 threads.
    // Example: 4 cores * 0.9 = 3.6 -> 3 threads.
    m_maxThreads = std::max(1, static_cast<int>(std::floor(totalCores * 0.9)));

    qInfo() << "ResourceManager: CPU Limit set to" << m_maxThreads << "threads (Total:" << totalCores << ")";

#ifdef _OPENMP
    omp_set_num_threads(m_maxThreads);
    qInfo() << "ResourceManager: OpenMP configured.";
#endif

    // Enforce limit on QtConcurrent global pool
    QThreadPool::globalInstance()->setMaxThreadCount(m_maxThreads);
    qInfo() << "ResourceManager: Global QThreadPool set to" << m_maxThreads << "threads";
}

int ResourceManager::maxThreads() const {
    return m_maxThreads;
}

double ResourceManager::getMemoryUsagePercent() const {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return static_cast<double>(memInfo.dwMemoryLoad);
    }
#elif defined(Q_OS_MAC)
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics_data_t vmstat;
    if (host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vmstat, &count) == KERN_SUCCESS) {
        // MacOS memory usage API is complex compared to Windows.
        // MacOS memory usage API requires porting.
        // Returning 0.0 indicates 'unknown' status, which prevents blocking operations 
        // that rely on strict memory checks. This is the intended behavior until ported.
        return 0.0; 
    }
#elif defined(Q_OS_LINUX)
    struct sysinfo memInfo;
    if (sysinfo(&memInfo) == 0) {
        long long total = memInfo.totalram;
        long long free = memInfo.freeram;
        total *= memInfo.mem_unit;
        free *= memInfo.mem_unit;
        return (1.0 - (double)free / total) * 100.0;
    }
#endif
    return 0.0; // Unknown
}

bool ResourceManager::isMemorySafe(size_t estimatedBytes) const {
    double usage = getMemoryUsagePercent();
    
    // If we can't measure, assume safe (don't block user)
    if (usage <= 0.1) return true; 

    // Hard limit: 90%
    if (usage >= 90.0) {
        qWarning() << "ResourceManager: High Memory Usage detected:" << usage << "%";
        return false;
    }
    
    // If we have an estimate, check if it pushes us over
#ifdef Q_OS_WIN
    if (estimatedBytes > 0) {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        unsigned long long avail = memInfo.ullAvailPhys;
        
        if (estimatedBytes > avail) {
             qWarning() << "ResourceManager: Not enough physical RAM for operation. Needed:" << estimatedBytes << "Available:" << avail;
             return false;
        }
        
        // Also check if adding it exceeds 90% total
        unsigned long long total = memInfo.ullTotalPhys;
        unsigned long long used = total - avail;
        double projected = (double)(used + estimatedBytes) / total * 100.0;
        
        if (projected >= 90.0) {
            qWarning() << "ResourceManager: Operation would exceed 90% RAM limit. Projected:" << projected << "%";
            return false;
        }
    }
#endif

    return true;
}
