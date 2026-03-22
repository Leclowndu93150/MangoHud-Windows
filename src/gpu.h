#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <cstdio>
#include <cstdint>
#include "overlay_params.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <regex>
#include <iostream>
#include <array>
#include "nvidia.h"
#include "adl.h"
#include "dxgi_gpu.h"
#include "gpu_metrics_util.h"

class GPU {
    public:
        gpu_metrics metrics;
        std::string drm_node;
        std::unique_ptr<NVIDIA> nvidia = nullptr;
        std::unique_ptr<AMDGPU_ADL> amd_adl = nullptr;
        std::unique_ptr<DXGI_GPU> dxgi_monitor = nullptr;
        bool is_active = false;
        std::string pci_dev;
        uint32_t vendor_id = 0;
        uint32_t device_id = 0;
        const std::string driver;

        GPU(
            std::string drm_node, uint32_t vendor_id, uint32_t device_id, const char* pci_dev,
            std::string driver, int dxgi_index = 0
        )
            : drm_node(drm_node), pci_dev(pci_dev), vendor_id(vendor_id), device_id(device_id),
            driver(driver) {
                if (vendor_id == 0x10de)
                    nvidia = std::make_unique<NVIDIA>(pci_dev);
                else if (vendor_id == 0x1002) {
                    amd_adl = std::make_unique<AMDGPU_ADL>();
                    if (!amd_adl->init(dxgi_index))
                        amd_adl.reset();
                }

                // Always create a DXGI monitor for VRAM info (works for all vendors)
                dxgi_monitor = std::make_unique<DXGI_GPU>();
                if (!dxgi_monitor->init(vendor_id, device_id))
                    dxgi_monitor.reset();
        }

        gpu_metrics get_metrics() {
            if (nvidia)
                this->metrics = nvidia->copy_metrics();
            else if (amd_adl && amd_adl->is_available())
                this->metrics = amd_adl->copy_metrics();

            // DXGI always supplements VRAM data if available
            if (dxgi_monitor && dxgi_monitor->is_available()) {
                if (metrics.memoryTotal <= 0)
                    metrics.memoryTotal = dxgi_monitor->get_vram_total_gb();
                if (metrics.sys_vram_used <= 0)
                    metrics.sys_vram_used = dxgi_monitor->get_vram_used_gb();
            }

            return metrics;
        };

        std::vector<int> nvidia_pids() {
#ifdef HAVE_NVML
            if (nvidia)
                return nvidia->pids();
#endif
            return std::vector<int>();
        }

        void pause() {
            if (nvidia)
                nvidia->pause();
            if (amd_adl)
                amd_adl->pause();
            if (dxgi_monitor)
                dxgi_monitor->pause();
        }

        void resume() {
            if (nvidia)
                nvidia->resume();
            if (amd_adl)
                amd_adl->resume();
            if (dxgi_monitor)
                dxgi_monitor->resume();
        }

        bool is_apu() {
            return false;
        }

        std::shared_ptr<Throttling> throttling() {
            if (nvidia)
                return nvidia->throttling;

            if (amd_adl && amd_adl->is_available())
                return amd_adl->throttling;

            return nullptr;
        }

        std::string gpu_text();
        std::string vram_text();

    private:
        std::thread thread;

        int index_in_selected_gpus();
};

class GPUS {
    public:
        std::vector<std::shared_ptr<GPU>> available_gpus;
        std::mutex mutex;

        explicit GPUS(const overlay_params* early_params = nullptr);

        std::shared_ptr<const overlay_params> params();

        void pause() {
            for (auto& gpu : available_gpus)
                gpu->pause();
        }

        void resume() {
            for (auto& gpu : available_gpus)
                gpu->resume();
        }

        std::shared_ptr<GPU> active_gpu() {
            if (available_gpus.empty())
                return nullptr;

            for (auto gpu : available_gpus) {
                if (gpu->is_active)
                    return gpu;
            }

            // if no GPU is marked as active, just set it to the last one
            // because integrated gpus are usually first
            return available_gpus.back();
        }

        void update_throttling() {
            for (auto gpu : available_gpus)
                if (gpu->throttling())
                    gpu->throttling()->update();
        }

        void get_metrics() {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto gpu : available_gpus)
                gpu->get_metrics();
        }

        std::vector<std::shared_ptr<GPU>> selected_gpus() {
            std::lock_guard<std::mutex> lock(mutex);
            std::vector<std::shared_ptr<GPU>> vec;

            if (params()->gpu_list.empty() && params()->pci_dev.empty())
                return available_gpus;

            if (!params()->gpu_list.empty()) {
                for (unsigned index : params()->gpu_list) {
                    if (index < available_gpus.size()) {
                        if (available_gpus[index])
                            vec.push_back(available_gpus[index]);
                    }
                }

                return vec;
            }

            if (!params()->pci_dev.empty()) {
                for (auto &gpu : available_gpus) {
                    if (gpu->pci_dev == params()->pci_dev) {
                        vec.push_back(gpu);
                        return vec;
                    }
                }

                return vec;
            }

            return vec;
        }

    private:
        const std::array<std::string, 2> supported_drivers = {
            "nvidia", "amd"
        };
};

extern std::unique_ptr<GPUS> gpus;

void getNvidiaGpuInfo(const struct overlay_params& params);
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
