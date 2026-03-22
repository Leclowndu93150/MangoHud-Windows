#include "dxgi_gpu.h"
#include <dxgi.h>
#include <dxgi1_4.h>
#include <spdlog/spdlog.h>

DXGI_GPU::DXGI_GPU() {}

DXGI_GPU::~DXGI_GPU() {
    stop_thread = true;
    cond_var.notify_one();
    if (thread.joinable())
        thread.join();

    if (adapter3) {
        adapter3->Release();
        adapter3 = nullptr;
    }
}

bool DXGI_GPU::init(uint32_t vendor_id, uint32_t device_id) {
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        SPDLOG_WARN("DXGI_GPU: CreateDXGIFactory1 failed (HRESULT: 0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    IDXGIAdapter1* adapter1 = nullptr;
    bool found = false;

    for (UINT i = 0; factory->EnumAdapters1(i, &adapter1) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        hr = adapter1->GetDesc1(&desc);
        if (FAILED(hr)) {
            adapter1->Release();
            continue;
        }

        // Match the adapter by vendor and device ID
        if (desc.VendorId == vendor_id && desc.DeviceId == device_id) {
            // Try to get IDXGIAdapter3 (available on Windows 10+, WDDM 2.0+)
            hr = adapter1->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3));
            if (SUCCEEDED(hr) && adapter3) {
                vram_total_gb = static_cast<float>(desc.DedicatedVideoMemory) / (1024.f * 1024.f * 1024.f);
                found = true;
                SPDLOG_INFO(
                    "DXGI_GPU: Found adapter {:04X}:{:04X} with {:.2f} GB VRAM",
                    vendor_id, device_id, vram_total_gb
                );
            } else {
                SPDLOG_WARN(
                    "DXGI_GPU: IDXGIAdapter3 not available for {:04X}:{:04X} (HRESULT: 0x{:08X})",
                    vendor_id, device_id, static_cast<unsigned>(hr)
                );
            }
            adapter1->Release();
            break;
        }

        adapter1->Release();
    }

    factory->Release();

    if (!found || !adapter3) {
        SPDLOG_WARN("DXGI_GPU: Could not find adapter {:04X}:{:04X} with IDXGIAdapter3 support", vendor_id, device_id);
        return false;
    }

    dxgi_available = true;

    // Start the polling thread
    thread = std::thread(&DXGI_GPU::metrics_polling_thread, this);

    return true;
}

float DXGI_GPU::get_vram_used_gb() {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    return metrics.sys_vram_used;
}

float DXGI_GPU::get_vram_total_gb() {
    // vram_total_gb is set once during init and never changes, so no lock needed
    return vram_total_gb;
}

void DXGI_GPU::get_instant_metrics(gpu_metrics* m) {
    if (!adapter3)
        return;

    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    HRESULT hr = adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (SUCCEEDED(hr)) {
        m->sys_vram_used = static_cast<float>(info.CurrentUsage) / (1024.f * 1024.f * 1024.f);
        m->memoryTotal = vram_total_gb;
    }
}

void DXGI_GPU::metrics_polling_thread() {
    gpu_metrics metrics_buffer[METRICS_SAMPLE_COUNT] {};

    while (!stop_thread) {
        for (size_t cur_sample_id = 0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
            get_instant_metrics(&metrics_buffer[cur_sample_id]);
            std::this_thread::sleep_for(std::chrono::milliseconds(METRICS_POLLING_PERIOD_MS));

            if (stop_thread) break;
        }

        if (stop_thread) break;

        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

        // Average the VRAM metrics over the sample period
        GPU_UPDATE_METRIC_AVERAGE_FLOAT(memoryTotal);
        GPU_UPDATE_METRIC_AVERAGE_FLOAT(sys_vram_used);
    }
}
