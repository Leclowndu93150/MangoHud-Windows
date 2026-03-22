#include "gpu.h"
#include "hud_elements.h"

#include <windows.h>
#include <dxgi.h>
#include <spdlog/spdlog.h>
#include <string>
#include <locale>
#include <codecvt>

// Helper: convert a wide string (WCHAR[]) to a std::string (UTF-8)
static std::string wchar_to_utf8(const WCHAR* wstr) {
    if (!wstr || wstr[0] == L'\0')
        return "";

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0)
        return "";

    std::string result(size_needed - 1, '\0'); // -1 to exclude null terminator
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size_needed, nullptr, nullptr);
    return result;
}

// Helper: build a PCI-style device string from DXGI adapter desc for identification
static std::string make_pci_dev_string(const DXGI_ADAPTER_DESC& desc) {
    char buf[64];
    snprintf(buf, sizeof(buf), "DXGI-%04X:%04X-%u",
             desc.VendorId, desc.DeviceId, desc.SubSysId);
    return std::string(buf);
}

GPUS::GPUS(const overlay_params* early_params) {
    overlay_params default_params {};
    std::shared_ptr<overlay_params> params_ptr;
    const overlay_params* params = early_params;
    if (!params) {
        params_ptr = get_params();
        params = params_ptr ? params_ptr.get() : &default_params;
    }

    IDXGIFactory* factory = nullptr;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        SPDLOG_ERROR("CreateDXGIFactory failed (HRESULT: 0x{:08X})", static_cast<unsigned>(hr));
        return;
    }

    IDXGIAdapter* adapter = nullptr;
    uint8_t idx = 0;
    uint8_t total_active = 0;

    for (UINT i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc;
        hr = adapter->GetDesc(&desc);
        if (FAILED(hr)) {
            SPDLOG_WARN("Failed to get DXGI_ADAPTER_DESC for adapter {}", i);
            adapter->Release();
            continue;
        }

        uint32_t vendor_id = desc.VendorId;
        uint32_t device_id = desc.DeviceId;

        // Skip Microsoft Basic Render Driver and similar software adapters
        if (vendor_id == 0x1414 && device_id == 0x008c) {
            SPDLOG_DEBUG("Skipping Microsoft Basic Render Driver (adapter {})", i);
            adapter->Release();
            continue;
        }

        std::string node_name = wchar_to_utf8(desc.Description);
        std::string pci_dev_str = make_pci_dev_string(desc);
        const char* pci_dev = pci_dev_str.c_str();

        // Determine driver string based on vendor
        std::string driver;
        if (vendor_id == 0x10de)
            driver = "nvidia";
        else if (vendor_id == 0x1002)
            driver = "amd";
        else if (vendor_id == 0x8086)
            driver = "intel";
        else
            driver = "unknown";

        std::shared_ptr<GPU> ptr =
            std::make_shared<GPU>(node_name, vendor_id, device_id, pci_dev, driver, static_cast<int>(i));

        if (params->gpu_list.size() == 1 && params->gpu_list[0] == idx++)
            ptr->is_active = true;

        if (!params->pci_dev.empty() && pci_dev_str == params->pci_dev)
            ptr->is_active = true;

        available_gpus.emplace_back(ptr);

        SPDLOG_DEBUG(
            "GPU Found: name: {}, driver: {}, vendor_id: {:04x} device_id: {:04x} pci_dev: {}",
            node_name, driver, vendor_id, device_id, pci_dev_str
        );

        if (ptr->is_active) {
            SPDLOG_INFO(
                "Set {} as active GPU (driver={} id={:04x}:{:04x} pci_dev={})",
                node_name, driver, vendor_id, device_id, pci_dev_str
            );
            total_active++;
        }

        adapter->Release();
    }

    factory->Release();

    if (total_active < 2)
        return;

    for (auto& gpu : available_gpus) {
        if (!gpu->is_active)
            continue;

        SPDLOG_WARN(
            "You have more than 1 active GPU, check if you use both pci_dev "
            "and gpu_list. If you use fps logging, MangoHud will log only "
            "this GPU: name = {}, driver = {}, vendor = {:04x}, pci_dev = {}",
            gpu->drm_node, gpu->driver, gpu->vendor_id, gpu->pci_dev
        );

        break;
    }
}

int GPU::index_in_selected_gpus() {
    auto selected_gpus = gpus->selected_gpus();
    auto it = std::find_if(selected_gpus.begin(), selected_gpus.end(),
                        [this](const std::shared_ptr<GPU>& gpu) {
                            return gpu.get() == this;
                        });
    if (it != selected_gpus.end()) {
        return std::distance(selected_gpus.begin(), it);
    }
    return -1;
}

std::string GPU::gpu_text() {
    std::string gpu_text;
    size_t index = this->index_in_selected_gpus();

    if (gpus->selected_gpus().size() == 1) {
        // When there's exactly one selected GPU, return "GPU" without index
        gpu_text = "GPU";
        if (gpus->params()->gpu_text.size() > 0) {
            gpu_text = gpus->params()->gpu_text[0];
        }
    } else if (gpus->selected_gpus().size() > 1) {
        // When there are multiple selected GPUs, use GPU+index or matching gpu_text
        gpu_text = "GPU" + std::to_string(index);
        if (gpus->params()->gpu_text.size() > index) {
            gpu_text = gpus->params()->gpu_text[index];
        }
    } else {
        // Default case for no selected GPUs
        gpu_text = "GPU";
    }

    return gpu_text;
}

std::string GPU::vram_text() {
    std::string vram_text;
    size_t index = this->index_in_selected_gpus();
    if (gpus->selected_gpus().size() > 1)
        vram_text = "VRAM" + std::to_string(index);
    else
        vram_text = "VRAM";
    return vram_text;
}

std::shared_ptr<const overlay_params> GPUS::params() {
    return get_params();
}

std::unique_ptr<GPUS> gpus = nullptr;
