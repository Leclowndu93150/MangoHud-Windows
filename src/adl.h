#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "gpu_metrics_util.h"

// ADL defines
#define ADL_OK 0
#define ADL_MAX_PATH 256

// ADL fan speed type constants
#define ADL_DL_FANCTRL_SPEED_TYPE_PERCENT 1
#define ADL_DL_FANCTRL_SPEED_TYPE_RPM     2

// Minimal ADL structures we need
typedef struct AdapterInfo {
    int iSize;
    int iAdapterIndex;
    char strUDID[ADL_MAX_PATH];
    int iBusNumber;
    int iDeviceNumber;
    int iFunctionNumber;
    int iVendorID;
    char strAdapterName[ADL_MAX_PATH];
    char strDisplayName[ADL_MAX_PATH];
    int iPresent;
    int iExist;
    char strDriverPath[ADL_MAX_PATH];
    char strDriverPathExt[ADL_MAX_PATH];
    char strPNPString[ADL_MAX_PATH];
    int iOSDisplayIndex;
} AdapterInfo;

typedef struct ADLTemperature {
    int iSize;
    int iTemperature; // in millidegrees Celsius
} ADLTemperature;

typedef struct ADLFanSpeedValue {
    int iSize;
    int iSpeedType; // 1 = percent, 2 = RPM
    int iFanSpeed;
    int iFlags;
} ADLFanSpeedValue;

typedef struct ADLPMActivity {
    int iSize;
    int iEngineClock;        // in 10kHz
    int iMemoryClock;        // in 10kHz
    int iVddc;               // in millivolts
    int iActivityPercent;    // GPU usage in percent
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iReserved;
} ADLPMActivity;

typedef struct ADLMemoryInfo {
    long long iMemorySize;
    char strMemoryType[ADL_MAX_PATH];
    long long iMemoryBandwidth;
} ADLMemoryInfo;

// ADL function pointer typedefs
typedef int (*ADL_MAIN_CONTROL_CREATE)(void* (*callback)(int), int);
typedef int (*ADL_MAIN_CONTROL_DESTROY)();
typedef int (*ADL_ADAPTER_NUMBEROFADAPTERS_GET)(int*);
typedef int (*ADL_ADAPTER_ADAPTERINFO_GET)(AdapterInfo*, int);
typedef int (*ADL_ADAPTER_ACTIVE_GET)(int, int*);
typedef int (*ADL_OVERDRIVE5_TEMPERATURE_GET)(int, int, ADLTemperature*);
typedef int (*ADL_OVERDRIVE5_FANSPEED_GET)(int, int, ADLFanSpeedValue*);
typedef int (*ADL_OVERDRIVE5_CURRENTACTIVITY_GET)(int, ADLPMActivity*);
typedef int (*ADL_ADAPTER_MEMORYINFO_GET)(int, ADLMemoryInfo*);

class AMDGPU_ADL {
public:
    AMDGPU_ADL();
    ~AMDGPU_ADL();

    bool init(int adapter_index);

    gpu_metrics copy_metrics() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        return metrics;
    }

    void pause() {
        paused = true;
        cond_var.notify_one();
    }

    void resume() {
        paused = false;
        cond_var.notify_one();
    }

    bool is_available() const { return adl_available; }

    std::shared_ptr<Throttling> throttling;

private:
    HMODULE hDLL = nullptr;
    bool adl_available = false;
    int adapter_idx = -1;

    std::mutex metrics_mutex;
    gpu_metrics metrics;
    std::thread thread;
    std::condition_variable cond_var;
    std::atomic<bool> stop_thread{false};
    std::atomic<bool> paused{false};

    // ADL function pointers
    ADL_MAIN_CONTROL_CREATE ADL_Main_Control_Create = nullptr;
    ADL_MAIN_CONTROL_DESTROY ADL_Main_Control_Destroy = nullptr;
    ADL_ADAPTER_NUMBEROFADAPTERS_GET ADL_Adapter_NumberOfAdapters_Get = nullptr;
    ADL_ADAPTER_ADAPTERINFO_GET ADL_Adapter_AdapterInfo_Get = nullptr;
    ADL_ADAPTER_ACTIVE_GET ADL_Adapter_Active_Get = nullptr;
    ADL_OVERDRIVE5_TEMPERATURE_GET ADL_Overdrive5_Temperature_Get = nullptr;
    ADL_OVERDRIVE5_FANSPEED_GET ADL_Overdrive5_FanSpeed_Get = nullptr;
    ADL_OVERDRIVE5_CURRENTACTIVITY_GET ADL_Overdrive5_CurrentActivity_Get = nullptr;
    ADL_ADAPTER_MEMORYINFO_GET ADL_Adapter_MemoryInfo_Get = nullptr;

    bool load_adl();
    void get_instant_metrics(gpu_metrics* m);
    void metrics_polling_thread();
};
