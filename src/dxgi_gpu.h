#pragma once
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdint>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "gpu_metrics_util.h"

// DXGI-based GPU monitoring
// Provides VRAM usage for any GPU vendor via IDXGIAdapter3::QueryVideoMemoryInfo
// Available on Windows 10+ with any GPU that supports WDDM 2.0+
class DXGI_GPU {
public:
    DXGI_GPU();
    ~DXGI_GPU();

    bool init(uint32_t vendor_id, uint32_t device_id);

    gpu_metrics copy_metrics() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        return metrics;
    }

    // Get VRAM info only (can be called by other GPU classes to supplement their data)
    float get_vram_used_gb();
    float get_vram_total_gb();

    void pause() { paused = true; cond_var.notify_one(); }
    void resume() { paused = false; cond_var.notify_one(); }

    bool is_available() const { return dxgi_available; }

private:
    bool dxgi_available = false;
    IDXGIAdapter3* adapter3 = nullptr;
    float vram_total_gb = 0;

    std::mutex metrics_mutex;
    gpu_metrics metrics;
    std::thread thread;
    std::condition_variable cond_var;
    std::atomic<bool> stop_thread{false};
    std::atomic<bool> paused{false};

    void get_instant_metrics(gpu_metrics* m);
    void metrics_polling_thread();
};
