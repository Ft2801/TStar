#ifndef RESOURCEMONITORWIDGET_H
#define RESOURCEMONITORWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <d3d11.h>
#include <dxgi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#endif

class ResourceMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit ResourceMonitorWidget(QWidget* parent = nullptr);
    ~ResourceMonitorWidget() override;

private slots:
    void updateStats();

private:
    QLabel* m_label;
    QTimer* m_timer;
    
    // CPU tracking (delta-based)
#ifdef Q_OS_WIN
    ULARGE_INTEGER m_prevIdleTime{};
    ULARGE_INTEGER m_prevKernelTime{};
    ULARGE_INTEGER m_prevUserTime{};
    
    // PDH (Performance Data Helper) for generic GPU usage (Intel/AMD/NVIDIA)
    PDH_HQUERY m_pdhQuery = nullptr;
    PDH_HCOUNTER m_pdhGpuCounter = nullptr;
    bool m_pdhAvailable = false;
    
    // DXGI for generic GPU name detection
    QString getGpuNameDxgi();
    
#elif defined(Q_OS_MACOS)
    uint64_t m_prevIdleTicks = 0;
    uint64_t m_prevTotalTicks = 0;
#endif
    
    // GPU (NVIDIA NVML) - dynamically loaded
    void* m_nvmlLib = nullptr;
    void* m_nvmlDevice = nullptr;
    bool m_nvmlAvailable = false;
    QString m_gpuName;
    
    // Platform queries
    float queryCpuUsage();
    void queryRamUsage(float& usedGB, float& totalGB, float& percent);
    float queryGpuUsage();
    float queryPdhGpuUsage(); // Windows only fallback
    
    // Init helpers
    void initNvml();
    void cleanupNvml();
    void initPdh();   // Windows only
    void cleanupPdh();// Windows only
    void initCpuBaseline();
};

#endif // RESOURCEMONITORWIDGET_H
