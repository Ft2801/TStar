#include "ResourceMonitorWidget.h"
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <IOKit/IOKitLib.h>
#endif

// ============================================================================
// NVML typedefs for dynamic loading (NVIDIA GPU monitoring)
// ============================================================================
typedef enum {
    NVML_SUCCESS_ = 0
} nvmlReturn_t_;

typedef struct { unsigned int gpu; unsigned int memory; } nvmlUtilization_t_;

typedef nvmlReturn_t_ (*nvmlInit_t)();
typedef nvmlReturn_t_ (*nvmlShutdown_t)();
typedef nvmlReturn_t_ (*nvmlDeviceGetCount_t)(unsigned int*);
typedef nvmlReturn_t_ (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
typedef nvmlReturn_t_ (*nvmlDeviceGetUtilizationRates_t)(void*, nvmlUtilization_t_*);
typedef nvmlReturn_t_ (*nvmlDeviceGetName_t)(void*, char*, unsigned int);

// Cached function pointers
static nvmlInit_t                       s_nvmlInit = nullptr;
static nvmlShutdown_t                   s_nvmlShutdown = nullptr;
static nvmlDeviceGetCount_t             s_nvmlDeviceGetCount = nullptr;
static nvmlDeviceGetHandleByIndex_t     s_nvmlDeviceGetHandleByIndex = nullptr;
static nvmlDeviceGetUtilizationRates_t  s_nvmlDeviceGetUtilizationRates = nullptr;
static nvmlDeviceGetName_t              s_nvmlDeviceGetName = nullptr;

// ============================================================================
// Constructor / Destructor
// ============================================================================

ResourceMonitorWidget::ResourceMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(0);
    
    m_label = new QLabel(this);
    m_label->setStyleSheet(
        "color: #999; font-size: 11px; font-family: 'Consolas', 'Monaco', 'Courier New', monospace;"
    );
    m_label->setText("CPU: --  |  RAM: --  |  GPU: --");
    layout->addWidget(m_label);
    
    // Init platform-specific baselines
    initCpuBaseline();
    initNvml();
    
#ifdef Q_OS_WIN
    initPdh();
    if (m_gpuName.isEmpty()) {
        m_gpuName = getGpuNameDxgi();
    }
#endif
    
    // Timer for periodic updates
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ResourceMonitorWidget::updateStats);
    m_timer->start(500); // 500ms = 0.5s
    
    // First update immediately
    QTimer::singleShot(100, this, &ResourceMonitorWidget::updateStats);
}

ResourceMonitorWidget::~ResourceMonitorWidget() {
    cleanupNvml();
#ifdef Q_OS_WIN
    cleanupPdh();
#endif
}

// ============================================================================
// Periodic update
// ============================================================================

void ResourceMonitorWidget::updateStats() {
    float cpu = queryCpuUsage();
    
    float ramUsed, ramTotal, ramPercent;
    queryRamUsage(ramUsed, ramTotal, ramPercent);
    
    float gpu = queryGpuUsage();
    
    QString text;
    
    // CPU
    if (cpu >= 0)
        text += QString("CPU: %1%").arg(QString::number(cpu, 'f', 0));
    else
        text += "CPU: --";
    
    text += "  |  ";
    
    // GPU
    if (gpu >= 0)
        text += QString("GPU: %1%").arg(QString::number(gpu, 'f', 0));
    else
        text += "GPU: N/A";
    
    text += "  |  ";

    // RAM
    if (ramTotal > 0)
        text += QString("RAM: %1 / %2 GB (%3%)")
            .arg(QString::number(ramUsed, 'f', 1))
            .arg(QString::number(ramTotal, 'f', 1))
            .arg(QString::number(ramPercent, 'f', 0));
    else
        text += "RAM: --";
    
    m_label->setText(text);
}

// ============================================================================
// CPU Usage (delta-based)
// ============================================================================

void ResourceMonitorWidget::initCpuBaseline() {
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    GetSystemTimes(&idle, &kernel, &user);
    m_prevIdleTime.LowPart = idle.dwLowDateTime;
    m_prevIdleTime.HighPart = idle.dwHighDateTime;
    m_prevKernelTime.LowPart = kernel.dwLowDateTime;
    m_prevKernelTime.HighPart = kernel.dwHighDateTime;
    m_prevUserTime.LowPart = user.dwLowDateTime;
    m_prevUserTime.HighPart = user.dwHighDateTime;
#elif defined(Q_OS_MACOS)
    host_cpu_load_info_data_t cpuInfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpuInfo, &count) == KERN_SUCCESS) {
        m_prevIdleTicks = cpuInfo.cpu_ticks[CPU_STATE_IDLE];
        m_prevTotalTicks = cpuInfo.cpu_ticks[CPU_STATE_USER]
                         + cpuInfo.cpu_ticks[CPU_STATE_SYSTEM]
                         + cpuInfo.cpu_ticks[CPU_STATE_IDLE]
                         + cpuInfo.cpu_ticks[CPU_STATE_NICE];
    }
#endif
}

float ResourceMonitorWidget::queryCpuUsage() {
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user))
        return -1.0f;
    
    ULARGE_INTEGER nowIdle, nowKernel, nowUser;
    nowIdle.LowPart = idle.dwLowDateTime;
    nowIdle.HighPart = idle.dwHighDateTime;
    nowKernel.LowPart = kernel.dwLowDateTime;
    nowKernel.HighPart = kernel.dwHighDateTime;
    nowUser.LowPart = user.dwLowDateTime;
    nowUser.HighPart = user.dwHighDateTime;
    
    uint64_t idleDelta = nowIdle.QuadPart - m_prevIdleTime.QuadPart;
    uint64_t kernelDelta = nowKernel.QuadPart - m_prevKernelTime.QuadPart;
    uint64_t userDelta = nowUser.QuadPart - m_prevUserTime.QuadPart;
    
    m_prevIdleTime = nowIdle;
    m_prevKernelTime = nowKernel;
    m_prevUserTime = nowUser;
    
    uint64_t totalDelta = kernelDelta + userDelta;
    if (totalDelta == 0) return 0.0f;
    
    // kernel time includes idle time
    float usage = 100.0f * (1.0f - (float)idleDelta / (float)totalDelta);
    return std::clamp(usage, 0.0f, 100.0f);
    
#elif defined(Q_OS_MACOS)
    host_cpu_load_info_data_t cpuInfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpuInfo, &count) != KERN_SUCCESS)
        return -1.0f;
    
    uint64_t idle = cpuInfo.cpu_ticks[CPU_STATE_IDLE];
    uint64_t total = cpuInfo.cpu_ticks[CPU_STATE_USER]
                   + cpuInfo.cpu_ticks[CPU_STATE_SYSTEM]
                   + cpuInfo.cpu_ticks[CPU_STATE_IDLE]
                   + cpuInfo.cpu_ticks[CPU_STATE_NICE];
    
    uint64_t idleDelta = idle - m_prevIdleTicks;
    uint64_t totalDelta = total - m_prevTotalTicks;
    
    m_prevIdleTicks = idle;
    m_prevTotalTicks = total;
    
    if (totalDelta == 0) return 0.0f;
    
    float usage = 100.0f * (1.0f - (float)idleDelta / (float)totalDelta);
    return std::clamp(usage, 0.0f, 100.0f);
#else
    return -1.0f;
#endif
}

// ============================================================================
// RAM Usage
// ============================================================================

void ResourceMonitorWidget::queryRamUsage(float& usedGB, float& totalGB, float& percent) {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        totalGB = memInfo.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f);
        float availGB = memInfo.ullAvailPhys / (1024.0f * 1024.0f * 1024.0f);
        usedGB = totalGB - availGB;
        percent = (totalGB > 0) ? (usedGB / totalGB * 100.0f) : 0;
    } else {
        usedGB = totalGB = percent = 0;
    }
#elif defined(Q_OS_MACOS)
    // Total RAM via sysctl
    int64_t totalMem = 0;
    size_t size = sizeof(totalMem);
    sysctlbyname("hw.memsize", &totalMem, &size, nullptr, 0);
    totalGB = totalMem / (1024.0f * 1024.0f * 1024.0f);
    
    // Used RAM via Mach VM statistics
    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t pageSize;
    host_page_size(mach_host_self(), &pageSize);
    
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vmStats, &count) == KERN_SUCCESS) {
        uint64_t active = vmStats.active_count * pageSize;
        uint64_t wired = vmStats.wire_count * pageSize;
        uint64_t compressed = vmStats.compressor_page_count * pageSize;
        // "Used" = active + wired + compressed (matching Activity Monitor)
        usedGB = (active + wired + compressed) / (1024.0f * 1024.0f * 1024.0f);
        percent = (totalGB > 0) ? (usedGB / totalGB * 100.0f) : 0;
    } else {
        usedGB = percent = 0;
    }
#else
    usedGB = totalGB = percent = 0;
#endif
}

// ============================================================================
// GPU Usage (NVIDIA NVML, dynamically loaded)
// ============================================================================

void ResourceMonitorWidget::initNvml() {
#ifdef Q_OS_WIN
    // Try to load NVML from standard NVIDIA locations
    HMODULE lib = LoadLibraryA("nvml.dll");
    if (!lib) {
        // Try standard NVIDIA path
        const char* sysRoot = getenv("SYSTEMROOT");
        if (sysRoot) {
            std::string path = std::string(sysRoot) + "\\System32\\nvml.dll";
            lib = LoadLibraryA(path.c_str());
        }
    }
    if (!lib) {
        // Try program files path
        const char* progFiles = getenv("ProgramFiles");
        if (progFiles) {
            std::string path = std::string(progFiles) + "\\NVIDIA Corporation\\NVSMI\\nvml.dll";
            lib = LoadLibraryA(path.c_str());
        }
    }
    
    if (!lib) {
        m_nvmlAvailable = false;
        return;
    }
    
    m_nvmlLib = (void*)lib;
    
    // Resolve function pointers
    // Suppress MinGW warning: GetProcAddress returns FARPROC which has incompatible signature
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    s_nvmlInit = (nvmlInit_t)GetProcAddress(lib, "nvmlInit_v2");
    if (!s_nvmlInit) s_nvmlInit = (nvmlInit_t)GetProcAddress(lib, "nvmlInit");
    s_nvmlShutdown = (nvmlShutdown_t)GetProcAddress(lib, "nvmlShutdown");
    s_nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress(lib, "nvmlDeviceGetCount_v2");
    if (!s_nvmlDeviceGetCount) s_nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress(lib, "nvmlDeviceGetCount");
    s_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(lib, "nvmlDeviceGetHandleByIndex_v2");
    if (!s_nvmlDeviceGetHandleByIndex) s_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(lib, "nvmlDeviceGetHandleByIndex");
    s_nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)GetProcAddress(lib, "nvmlDeviceGetUtilizationRates");
    s_nvmlDeviceGetName = (nvmlDeviceGetName_t)GetProcAddress(lib, "nvmlDeviceGetName");
#pragma GCC diagnostic pop
    
    if (!s_nvmlInit || !s_nvmlDeviceGetCount || !s_nvmlDeviceGetHandleByIndex || !s_nvmlDeviceGetUtilizationRates) {
        m_nvmlAvailable = false;
        return;
    }
    
    if (s_nvmlInit() != NVML_SUCCESS_) {
        m_nvmlAvailable = false;
        return;
    }
    
    unsigned int deviceCount = 0;
    s_nvmlDeviceGetCount(&deviceCount);
    if (deviceCount == 0) {
        m_nvmlAvailable = false;
        return;
    }
    
    // Use first GPU
    void* device = nullptr;
    if (s_nvmlDeviceGetHandleByIndex(0, &device) != NVML_SUCCESS_) {
        m_nvmlAvailable = false;
        return;
    }
    
    m_nvmlDevice = device;
    m_nvmlAvailable = true;
    
    // Get GPU name
    if (s_nvmlDeviceGetName) {
        char name[128] = {};
        if (s_nvmlDeviceGetName(device, name, sizeof(name)) == NVML_SUCCESS_) {
            m_gpuName = QString::fromLatin1(name);
            // Shorten common prefixes
            m_gpuName.replace("NVIDIA ", "");
            m_gpuName.replace("GeForce ", "");
        }
    }
    
#elif defined(Q_OS_MACOS)
    // On macOS, we can detect the GPU name via IOKit
    // Apple Silicon GPU utilization is not easily accessible without private APIs
    // We'll just report the GPU name and mark usage as unavailable
    io_iterator_t iterator;
    CFMutableDictionaryRef matchDict = IOServiceMatching("IOPCIDevice");
    if (matchDict && IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, &iterator) == KERN_SUCCESS) {
        io_service_t service;
        while ((service = IOIteratorNext(iterator)) != 0) {
            CFTypeRef model = IORegistryEntryCreateCFProperty(service, CFSTR("model"), kCFAllocatorDefault, 0);
            if (model) {
                if (CFGetTypeID(model) == CFDataGetTypeID()) {
                    const char* modelStr = (const char*)CFDataGetBytePtr((CFDataRef)model);
                    if (modelStr && strlen(modelStr) > 0) {
                        m_gpuName = QString::fromUtf8(modelStr);
                    }
                }
                CFRelease(model);
            }
            IOObjectRelease(service);
            if (!m_gpuName.isEmpty()) break;
        }
        IOObjectRelease(iterator);
    }
    
    // Fallback: get GPU from system profiler info via sysctl
    if (m_gpuName.isEmpty()) {
        // Check if Apple Silicon
        char brand[256] = {};
        size_t sz = sizeof(brand);
        if (sysctlbyname("machdep.cpu.brand_string", brand, &sz, nullptr, 0) == 0) {
            if (QString::fromLatin1(brand).contains("Apple")) {
                m_gpuName = "Apple GPU";
            }
        }
    }
    
    m_nvmlAvailable = false; // No NVML on macOS
#endif
}

void ResourceMonitorWidget::cleanupNvml() {
#ifdef Q_OS_WIN
    if (m_nvmlAvailable && s_nvmlShutdown) {
        s_nvmlShutdown();
    }
    if (m_nvmlLib) {
        FreeLibrary((HMODULE)m_nvmlLib);
        m_nvmlLib = nullptr;
    }
#endif
}

float ResourceMonitorWidget::queryGpuUsage() {
#ifdef Q_OS_WIN
    // Prefer NVML if available (NVIDIA specific, usually accurate for dGPU)
    if (m_nvmlAvailable && m_nvmlDevice && s_nvmlDeviceGetUtilizationRates) {
        nvmlUtilization_t_ util;
        if (s_nvmlDeviceGetUtilizationRates(m_nvmlDevice, &util) == NVML_SUCCESS_) {
             // If NVML reports > 0 usage, return it. 
             // If 0, it might be idle or user is using iGPU. 
             // We could check PDH too and take max, but let's trust NVML if it's there.
             // Actually, hybrid graphics (Optimus) might have dGPU sleeping (0%) while iGPU works.
             // So if NVML is 0, we might want to peek at PDH?
             // For simplicity, if NVML works, we use it. 
             return (float)util.gpu; 
        }
    }
    
    // Fallback to PDH (Integrated Graphics / AMD / Intel)
    if (m_pdhAvailable) {
        return queryPdhGpuUsage();
    }
    
    return -1.0f;
    
#elif defined(Q_OS_MACOS)
    // Apple Silicon GPU utilization requires private IOKit APIs
    // Not reliably available without SPI, so return -1
    return -1.0f;
#else
    return -1.0f;
#endif
}

#ifdef Q_OS_WIN
void ResourceMonitorWidget::initPdh() {
    if (PdhOpenQuery(NULL, 0, &m_pdhQuery) != ERROR_SUCCESS) {
        m_pdhAvailable = false;
        return;
    }
    
    // Add counter for all GPU engines "GPU Engine(*)\Utilization Percentage"
    // English locale might be required or localized names. PdhAddCounter works with localized paths usually.
    // To be safe internationally, PdhAddEnglishCounter is better if available (Vista+).
    // Utilization Percentage is standard.
    if (PdhAddEnglishCounterW(m_pdhQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &m_pdhGpuCounter) != ERROR_SUCCESS) {
         // Fallback to localized if English fails (unlikely on modern Windows)
         if (PdhAddCounterW(m_pdhQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &m_pdhGpuCounter) != ERROR_SUCCESS) {
             m_pdhAvailable = false;
             PdhCloseQuery(m_pdhQuery);
             m_pdhQuery = nullptr;
             return;
         }
    }
    
    m_pdhAvailable = true;
}

void ResourceMonitorWidget::cleanupPdh() {
    if (m_pdhQuery) {
        PdhCloseQuery(m_pdhQuery);
        m_pdhQuery = nullptr;
    }
    m_pdhGpuCounter = nullptr;
}

float ResourceMonitorWidget::queryPdhGpuUsage() {
    if (!m_pdhQuery || !m_pdhGpuCounter) return -1.0f;
    
    // Collect data
    if (PdhCollectQueryData(m_pdhQuery) != ERROR_SUCCESS) return -1.0f;
    
    // Get array size
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArray(m_pdhGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    
    if (status != (PDH_STATUS)PDH_MORE_DATA) return -1.0f; // Expect MORE_DATA
    
    std::vector<BYTE> buffer(bufferSize);
    PDH_FMT_COUNTERVALUE_ITEM* items = (PDH_FMT_COUNTERVALUE_ITEM*)buffer.data();
    
    status = PdhGetFormattedCounterArray(m_pdhGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    
    if (status != ERROR_SUCCESS) return -1.0f;
    
    double maxUsage = 0.0;
    
    for (DWORD i = 0; i < itemCount; i++) {
        // GPU Engine instances are like "pid_X_luid_Y_phys_Z_eng_W"
        // We just want the max usage of any engine to represent "Busy-ness"
        // This covers 3D, Video Decode, Copy, etc.
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
            double val = items[i].FmtValue.doubleValue;
            if (val > maxUsage) maxUsage = val;
        }
    }
    
    return (float)maxUsage;
}

QString ResourceMonitorWidget::getGpuNameDxgi() {
    IDXGIFactory1* factory = nullptr;
    if (CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory) != S_OK) {
        return QString();
    }
    
    QString bestName;
    IDXGIAdapter1* adapter = nullptr;
    
    // Enumerate adapters
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        
        // Skip software adapter (Microsoft Basic Render Driver)
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }
        
        // Found a hardware adapter
        // Use the first one found (usually primary) or prioritize discrete?
        // Usually iGPU is index 0 or discrete is index 0 depending on BIOS.
        // We'll just take the first real HW GPU.
        bestName = QString::fromWCharArray(desc.Description);
        adapter->Release();
        break; 
        
        // Note: multiple GPUs? We just pick one for the name label for now.
    }
    
    factory->Release();
    return bestName;
}
#endif
