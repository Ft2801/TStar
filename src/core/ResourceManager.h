#ifndef RESOURCEMANAGER_H
#define RESOURCEMANAGER_H

#include <QObject>
#include <QtGlobal>

/**
 * @brief Manages system resource limits (CPU/RAM)
 * 
 * Enforces user requirement: Max 90% CPU and RAM usage.
 */
class ResourceManager : public QObject {
    Q_OBJECT
public:
    static ResourceManager& instance();

    /**
     * @brief Initialize resource limits
     * Calculates max threads based on 90% rule.
     */
    void init();

    /**
     * @brief Get the maximum number of threads allowed
     * @return 90% of available logical cores, minimum 1
     */
    int maxThreads() const;

    /**
     * @brief Check if specific amount of memory can be allocated without exceeding 90% system load
     * @param estimatedBytes Bytes intended to be allocated
     * @return true if safe to proceed
     */
    bool isMemorySafe(size_t estimatedBytes = 0) const;

    /**
     * @brief Get current system memory usage percentage
     */
    double getMemoryUsagePercent() const;

private:
    ResourceManager();
    ~ResourceManager() = default;
    
    int m_maxThreads = 1;
};

#endif // RESOURCEMANAGER_H
