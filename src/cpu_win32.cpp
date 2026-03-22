#include <windows.h>
#include <thread>
#include <string.h>
#include <vector>
#include "cpu.h"
#include <spdlog/spdlog.h>

// NtQuerySystemInformation info class for per-core CPU times
#define SystemProcessorPerformanceInformation 0x8

// Processor power information struct returned by CallNtPowerInformation
typedef struct _PROCESSOR_POWER_INFORMATION {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION;

// Per-core performance information from NtQuerySystemInformation
typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

// Function pointer types for dynamically loaded APIs
typedef LONG (NTAPI *NtQuerySystemInformation_t)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

typedef LONG (WINAPI *CallNtPowerInformation_t)(
    int InformationLevel,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength
);

// ProcessorInformation power information level
#define ProcessorInformation 11

// Cached function pointers (loaded once)
static NtQuerySystemInformation_t pNtQuerySystemInformation = nullptr;
static CallNtPowerInformation_t pCallNtPowerInformation = nullptr;
static bool s_apisLoaded = false;

static void loadApis()
{
    if (s_apisLoaded)
        return;

    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        pNtQuerySystemInformation = (NtQuerySystemInformation_t)
            GetProcAddress(hNtdll, "NtQuerySystemInformation");
    }

    HMODULE hPowrProf = LoadLibraryA("PowrProf.dll");
    if (hPowrProf) {
        pCallNtPowerInformation = (CallNtPowerInformation_t)
            GetProcAddress(hPowrProf, "CallNtPowerInformation");
        // Intentionally not calling FreeLibrary -- keep it loaded for the process lifetime
    }

    s_apisLoaded = true;
}

// Previous per-core idle/kernel/user times for delta calculation
static std::vector<LARGE_INTEGER> s_prevIdleTimes;
static std::vector<LARGE_INTEGER> s_prevKernelTimes;
static std::vector<LARGE_INTEGER> s_prevUserTimes;

uint64_t FileTimeToInt64(const FILETIME& ft)
{
    ULARGE_INTEGER uli = {};
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

CPUStats::CPUStats()
{
}

CPUStats::~CPUStats()
{
}

bool CPUStats::Init()
{
    loadApis();

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCpus = sysInfo.dwNumberOfProcessors;

    m_cpuData.resize(numCpus);
    for (int i = 0; i < numCpus; i++) {
        m_cpuData[i].cpu_id = i;
        m_cpuData[i].percent = 0.0f;
        m_cpuData[i].mhz = 0;
        m_cpuData[i].temp = 0;
        m_cpuData[i].cpu_mhz = 0;
        m_cpuData[i].power = 0.0f;
    }

    // Initialize per-core tracking arrays
    s_prevIdleTimes.resize(numCpus, LARGE_INTEGER{});
    s_prevKernelTimes.resize(numCpus, LARGE_INTEGER{});
    s_prevUserTimes.resize(numCpus, LARGE_INTEGER{});

    m_inited = true;
    return true;
}

bool CPUStats::Reinit()
{
    m_inited = false;
    return Init();
}

bool CPUStats::UpdateCPUData()
{
    if (!m_inited)
        Init();

    // --- Total CPU usage via GetSystemTimes ---
    FILETIME IdleTime, KernelTime, UserTime;
    static unsigned long long PrevTotal = 0;
    static unsigned long long PrevIdle  = 0;

    GetSystemTimes(&IdleTime, &KernelTime, &UserTime);

    unsigned long long ThisIdle   = FileTimeToInt64(IdleTime);
    unsigned long long ThisKernel = FileTimeToInt64(KernelTime);
    unsigned long long ThisUser   = FileTimeToInt64(UserTime);

    unsigned long long ThisTotal      = ThisKernel + ThisUser;
    unsigned long long TotalSinceLast = ThisTotal - PrevTotal;
    unsigned long long IdleSinceLast  = ThisIdle  - PrevIdle;

    if (TotalSinceLast > 0) {
        double Headroom = (double)IdleSinceLast / (double)TotalSinceLast;
        m_cpuDataTotal.percent = (float)((1.0 - Headroom) * 100.0);
    }

    PrevTotal = ThisTotal;
    PrevIdle  = ThisIdle;

    // --- Per-core CPU usage via NtQuerySystemInformation ---
    if (pNtQuerySystemInformation && !m_cpuData.empty()) {
        int numCpus = (int)m_cpuData.size();
        std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> perfInfo(numCpus);
        ULONG returnLength = 0;

        LONG status = pNtQuerySystemInformation(
            SystemProcessorPerformanceInformation,
            perfInfo.data(),
            (ULONG)(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * numCpus),
            &returnLength
        );

        if (status == 0) { // STATUS_SUCCESS
            int coresReturned = (int)(returnLength / sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION));
            for (int i = 0; i < coresReturned && i < numCpus; i++) {
                // KernelTime includes IdleTime on Windows
                long long idle   = perfInfo[i].IdleTime.QuadPart;
                long long kernel = perfInfo[i].KernelTime.QuadPart;
                long long user   = perfInfo[i].UserTime.QuadPart;

                long long prevIdle   = s_prevIdleTimes[i].QuadPart;
                long long prevKernel = s_prevKernelTimes[i].QuadPart;
                long long prevUser   = s_prevUserTimes[i].QuadPart;

                long long totalDelta = (kernel + user) - (prevKernel + prevUser);
                long long idleDelta  = idle - prevIdle;

                if (totalDelta > 0) {
                    double headroom = (double)idleDelta / (double)totalDelta;
                    m_cpuData[i].percent = (float)((1.0 - headroom) * 100.0);
                }

                s_prevIdleTimes[i]   = perfInfo[i].IdleTime;
                s_prevKernelTimes[i] = perfInfo[i].KernelTime;
                s_prevUserTimes[i]   = perfInfo[i].UserTime;
            }
        }
    }

    m_updatedCPUs = true;
    return true;
}

bool CPUStats::UpdateCoreMhz()
{
    if (!m_inited)
        return false;

    if (!pCallNtPowerInformation)
        return false;

    int numCpus = (int)m_cpuData.size();
    if (numCpus <= 0)
        return false;

    std::vector<PROCESSOR_POWER_INFORMATION> powerInfo(numCpus);
    ULONG bufferSize = (ULONG)(sizeof(PROCESSOR_POWER_INFORMATION) * numCpus);

    LONG status = pCallNtPowerInformation(
        ProcessorInformation,
        NULL, 0,
        powerInfo.data(), bufferSize
    );

    if (status != 0) // STATUS_SUCCESS == 0
        return false;

    int maxMhz = 0;
    for (int i = 0; i < numCpus; i++) {
        m_cpuData[i].mhz = (int)powerInfo[i].CurrentMhz;
        if ((int)powerInfo[i].CurrentMhz > maxMhz)
            maxMhz = (int)powerInfo[i].CurrentMhz;
    }

    m_cpuDataTotal.cpu_mhz = maxMhz;
    return true;
}

bool CPUStats::UpdateCpuTemp()
{
    // No simple unprivileged API for CPU temperature on Windows.
    // Accessing CPU thermals requires either:
    //   - WMI query to MSAcpi_ThermalZoneTemperature (requires admin privileges)
    //   - An external library such as LibreHardwareMonitor / OpenHardwareMonitor
    // For now, report 0 (unknown).
    m_cpuDataTotal.temp = 0;
    return false;
}

bool CPUStats::UpdateCpuPower()
{
    // No standard Windows API for CPU package power consumption.
    // Would require an external library (LibreHardwareMonitor) or driver-level access.
    m_cpuDataTotal.power = 0.0f;
    return false;
}

bool CPUStats::ReadcpuTempFile(int&)
{
    return false;
}

bool CPUStats::GetCpuFile()
{
    // Linux-specific (reads from /sys/class/hwmon). Not applicable on Windows.
    return false;
}

bool CPUStats::InitCpuPowerData()
{
    // Linux-specific (reads from sysfs power files). Not applicable on Windows.
    return false;
}

void CPUStats::get_cpu_cores_types()
{
    // Hybrid core labeling (P-core / E-core) relies on Linux sysfs.
    // Not implemented on Windows.
}

void CPUStats::get_cpu_cores_types_intel()
{
    // Linux-specific (reads from /sys/devices/cpu_core/cpus etc.)
}

void CPUStats::get_cpu_cores_types_arm()
{
    // Linux-specific (reads from /sys/devices/system/cpu/cpuN/regs)
}

CPUStats cpuStats;
