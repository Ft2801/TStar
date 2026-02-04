#ifndef SWAPMANAGER_H
#define SWAPMANAGER_H

#include <QObject>
#include <QList>
#include <QMutex>
#include <QTimer>
#include <vector>

class ImageBuffer;

/**
 * @brief Singleton class to manage memory pressure and offload inactive ImageBuffers to disk.
 */
class SwapManager : public QObject {
    Q_OBJECT
public:
    static SwapManager& instance();
    
    // Register/Unregister buffers
    void registerBuffer(ImageBuffer* buffer);
    void unregisterBuffer(ImageBuffer* buffer);
    
    // Check memory and swap if needed
    void checkMemoryPressure();
    
    // Settings
    void setMaxRamUsagePercent(int percent) { m_maxRamUsagePercent = percent; }

    // Constants
    static const int DEFAULT_CHECK_INTERVAL_MS = 2000; // Check every 2s

private:
    SwapManager();
    ~SwapManager();
    
    QList<ImageBuffer*> m_buffers;
    QMutex m_listMutex;
    QTimer* m_timer;
    
    int m_maxRamUsagePercent = 80;
    
    // Helper to get global memory status
    double getMemoryUsagePercent();
};

#endif // SWAPMANAGER_H
