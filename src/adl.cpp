#include "adl.h"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

// ADL memory allocation callback - required by ADL_Main_Control_Create
static void* __stdcall ADL_Main_Memory_Alloc(int iSize) {
    void* lpBuffer = malloc(iSize);
    if (lpBuffer)
        memset(lpBuffer, 0, iSize);
    return lpBuffer;
}

AMDGPU_ADL::AMDGPU_ADL() {
}

AMDGPU_ADL::~AMDGPU_ADL() {
    stop_thread = true;
    cond_var.notify_one();
    if (thread.joinable())
        thread.join();

    if (ADL_Main_Control_Destroy)
        ADL_Main_Control_Destroy();

    if (hDLL)
        FreeLibrary(hDLL);
}

bool AMDGPU_ADL::load_adl() {
    // Try 64-bit DLL first, then 32-bit
#ifdef _WIN64
    hDLL = LoadLibraryA("atiadlxx.dll");
#else
    hDLL = LoadLibraryA("atiadlxy.dll");
#endif

    if (!hDLL) {
        // Try the other variant as fallback
#ifdef _WIN64
        hDLL = LoadLibraryA("atiadlxy.dll");
#else
        hDLL = LoadLibraryA("atiadlxx.dll");
#endif
    }

    if (!hDLL) {
        SPDLOG_DEBUG("ADL: Could not load AMD Display Library DLL");
        return false;
    }

    // Load all function pointers
    ADL_Main_Control_Create = (ADL_MAIN_CONTROL_CREATE)
        GetProcAddress(hDLL, "ADL_Main_Control_Create");
    ADL_Main_Control_Destroy = (ADL_MAIN_CONTROL_DESTROY)
        GetProcAddress(hDLL, "ADL_Main_Control_Destroy");
    ADL_Adapter_NumberOfAdapters_Get = (ADL_ADAPTER_NUMBEROFADAPTERS_GET)
        GetProcAddress(hDLL, "ADL_Adapter_NumberOfAdapters_Get");
    ADL_Adapter_AdapterInfo_Get = (ADL_ADAPTER_ADAPTERINFO_GET)
        GetProcAddress(hDLL, "ADL_Adapter_AdapterInfo_Get");
    ADL_Adapter_Active_Get = (ADL_ADAPTER_ACTIVE_GET)
        GetProcAddress(hDLL, "ADL_Adapter_Active_Get");
    ADL_Overdrive5_Temperature_Get = (ADL_OVERDRIVE5_TEMPERATURE_GET)
        GetProcAddress(hDLL, "ADL_Overdrive5_Temperature_Get");
    ADL_Overdrive5_FanSpeed_Get = (ADL_OVERDRIVE5_FANSPEED_GET)
        GetProcAddress(hDLL, "ADL_Overdrive5_FanSpeed_Get");
    ADL_Overdrive5_CurrentActivity_Get = (ADL_OVERDRIVE5_CURRENTACTIVITY_GET)
        GetProcAddress(hDLL, "ADL_Overdrive5_CurrentActivity_Get");
    ADL_Adapter_MemoryInfo_Get = (ADL_ADAPTER_MEMORYINFO_GET)
        GetProcAddress(hDLL, "ADL_Adapter_MemoryInfo_Get");

    // Check required functions
    if (!ADL_Main_Control_Create || !ADL_Main_Control_Destroy ||
        !ADL_Adapter_NumberOfAdapters_Get || !ADL_Adapter_AdapterInfo_Get) {
        SPDLOG_ERROR("ADL: Failed to load required ADL functions");
        FreeLibrary(hDLL);
        hDLL = nullptr;
        return false;
    }

    return true;
}

bool AMDGPU_ADL::init(int adapter_index) {
    if (!load_adl())
        return false;

    // Initialize ADL: iEnumConnectedAdapters=1 to enumerate only active adapters
    int adl_result = ADL_Main_Control_Create(ADL_Main_Memory_Alloc, 1);
    if (adl_result != ADL_OK) {
        SPDLOG_ERROR("ADL: ADL_Main_Control_Create failed with error {}", adl_result);
        FreeLibrary(hDLL);
        hDLL = nullptr;
        return false;
    }

    // Get the number of adapters
    int num_adapters = 0;
    adl_result = ADL_Adapter_NumberOfAdapters_Get(&num_adapters);
    if (adl_result != ADL_OK || num_adapters <= 0) {
        SPDLOG_ERROR("ADL: No adapters found (result={}, count={})", adl_result, num_adapters);
        ADL_Main_Control_Destroy();
        FreeLibrary(hDLL);
        hDLL = nullptr;
        return false;
    }

    SPDLOG_DEBUG("ADL: Found {} adapters", num_adapters);

    // Get adapter info for all adapters
    std::vector<AdapterInfo> adapter_info(num_adapters);
    memset(adapter_info.data(), 0, sizeof(AdapterInfo) * num_adapters);
    adl_result = ADL_Adapter_AdapterInfo_Get(adapter_info.data(),
        sizeof(AdapterInfo) * num_adapters);
    if (adl_result != ADL_OK) {
        SPDLOG_ERROR("ADL: ADL_Adapter_AdapterInfo_Get failed with error {}", adl_result);
        ADL_Main_Control_Destroy();
        FreeLibrary(hDLL);
        hDLL = nullptr;
        return false;
    }

    // Find an active AMD adapter.
    // ADL and DXGI enumerate adapters differently, so we look for the Nth
    // active AMD adapter where N matches adapter_index.
    int amd_adapter_count = 0;
    adapter_idx = -1;

    for (int i = 0; i < num_adapters; i++) {
        // Check if adapter is active
        int active = 0;
        if (ADL_Adapter_Active_Get) {
            ADL_Adapter_Active_Get(adapter_info[i].iAdapterIndex, &active);
        }

        if (!active && !adapter_info[i].iPresent)
            continue;

        // Check it's an AMD adapter (vendor 0x1002)
        if (adapter_info[i].iVendorID != 0x1002)
            continue;

        SPDLOG_DEBUG("ADL: Active AMD adapter [{}]: idx={} name='{}' bus={} dev={} func={}",
            amd_adapter_count,
            adapter_info[i].iAdapterIndex,
            adapter_info[i].strAdapterName,
            adapter_info[i].iBusNumber,
            adapter_info[i].iDeviceNumber,
            adapter_info[i].iFunctionNumber);

        if (amd_adapter_count == adapter_index) {
            adapter_idx = adapter_info[i].iAdapterIndex;
            SPDLOG_INFO("ADL: Using adapter '{}' (ADL index {})",
                adapter_info[i].strAdapterName, adapter_idx);
            break;
        }

        amd_adapter_count++;
    }

    // If no match by index, just use the first active AMD adapter found
    if (adapter_idx < 0) {
        for (int i = 0; i < num_adapters; i++) {
            int active = 0;
            if (ADL_Adapter_Active_Get)
                ADL_Adapter_Active_Get(adapter_info[i].iAdapterIndex, &active);

            if ((active || adapter_info[i].iPresent) && adapter_info[i].iVendorID == 0x1002) {
                adapter_idx = adapter_info[i].iAdapterIndex;
                SPDLOG_INFO("ADL: Falling back to first AMD adapter '{}' (ADL index {})",
                    adapter_info[i].strAdapterName, adapter_idx);
                break;
            }
        }
    }

    if (adapter_idx < 0) {
        SPDLOG_ERROR("ADL: No active AMD adapter found");
        ADL_Main_Control_Destroy();
        FreeLibrary(hDLL);
        hDLL = nullptr;
        return false;
    }

    // Query total VRAM once during init
    if (ADL_Adapter_MemoryInfo_Get) {
        ADLMemoryInfo mem_info = {};
        adl_result = ADL_Adapter_MemoryInfo_Get(adapter_idx, &mem_info);
        if (adl_result == ADL_OK) {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            metrics.memoryTotal = static_cast<float>(mem_info.iMemorySize) / (1024.f * 1024.f * 1024.f);
            SPDLOG_INFO("ADL: Total VRAM: {:.2f} GB", metrics.memoryTotal);
        } else {
            SPDLOG_WARN("ADL: Could not query memory info (error {})", adl_result);
        }
    }

    adl_available = true;
    throttling = std::make_shared<Throttling>(0x1002);

    // Start the polling thread
    thread = std::thread(&AMDGPU_ADL::metrics_polling_thread, this);

    SPDLOG_INFO("ADL: AMD GPU monitoring initialized successfully");
    return true;
}

void AMDGPU_ADL::get_instant_metrics(gpu_metrics* m) {
    if (!adl_available || adapter_idx < 0)
        return;

    // Get GPU activity (load, clocks, voltage)
    if (ADL_Overdrive5_CurrentActivity_Get) {
        ADLPMActivity activity = {};
        activity.iSize = sizeof(ADLPMActivity);
        int result = ADL_Overdrive5_CurrentActivity_Get(adapter_idx, &activity);
        if (result == ADL_OK) {
            m->load = activity.iActivityPercent;
            m->CoreClock = activity.iEngineClock / 100;   // 10kHz -> MHz
            m->MemClock = activity.iMemoryClock / 100;    // 10kHz -> MHz
            m->voltage = activity.iVddc;                   // millivolts
        }
    }

    // Get temperature
    if (ADL_Overdrive5_Temperature_Get) {
        ADLTemperature temp = {};
        temp.iSize = sizeof(ADLTemperature);
        int result = ADL_Overdrive5_Temperature_Get(adapter_idx, 0, &temp);
        if (result == ADL_OK) {
            m->temp = temp.iTemperature / 1000;  // millidegrees -> degrees Celsius
        }
    }

    // Get fan speed (try RPM first, then percent)
    if (ADL_Overdrive5_FanSpeed_Get) {
        ADLFanSpeedValue fan = {};
        fan.iSize = sizeof(ADLFanSpeedValue);

        // Try RPM first
        fan.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
        int result = ADL_Overdrive5_FanSpeed_Get(adapter_idx, 0, &fan);
        if (result == ADL_OK && fan.iFanSpeed > 0) {
            m->fan_speed = fan.iFanSpeed;
            m->fan_rpm = true;
        } else {
            // Fall back to percentage
            fan.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_PERCENT;
            result = ADL_Overdrive5_FanSpeed_Get(adapter_idx, 0, &fan);
            if (result == ADL_OK) {
                m->fan_speed = fan.iFanSpeed;
                m->fan_rpm = false;
            }
        }
    }

    // Note: VRAM usage is not well-exposed through ADL Overdrive5.
    // Total VRAM is queried once during init.
    // Per-process and system VRAM usage should be obtained via DXGI
    // QueryVideoMemoryInfo (handled separately).
}

void AMDGPU_ADL::metrics_polling_thread() {
    gpu_metrics metrics_buffer[METRICS_SAMPLE_COUNT] {};

    while (!stop_thread) {
        // Collect samples
        for (size_t cur_sample_id = 0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
            get_instant_metrics(&metrics_buffer[cur_sample_id]);

            // Carry over the total VRAM from init (it doesn't change)
            {
                std::lock_guard<std::mutex> lock(metrics_mutex);
                metrics_buffer[cur_sample_id].memoryTotal = metrics.memoryTotal;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(METRICS_POLLING_PERIOD_MS));

            if (stop_thread)
                return;
        }

        // Average the samples and update the metrics
        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

        if (stop_thread)
            return;

        GPU_UPDATE_METRIC_AVERAGE(load);
        GPU_UPDATE_METRIC_AVERAGE(CoreClock);
        GPU_UPDATE_METRIC_AVERAGE(MemClock);
        GPU_UPDATE_METRIC_AVERAGE(temp);
        GPU_UPDATE_METRIC_AVERAGE(voltage);
        GPU_UPDATE_METRIC_MAX(fan_speed);
        GPU_UPDATE_METRIC_LAST(fan_rpm);
        GPU_UPDATE_METRIC_AVERAGE_FLOAT(memoryTotal);
    }
}
