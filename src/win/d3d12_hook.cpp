#include "kiero.h"

#if KIERO_INCLUDE_D3D12

#include <cstdio>
#include <cassert>
#include "d3d12_hook.h"
#include "d3d_shared.h"
#include "../overlay.h"
#include "../hud_elements.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <implot.h>
#include "imgui_impl_win32.h"

// ImGui DX12 backend is only available on MSVC builds (MinGW lacks required headers)
#ifdef _MSC_VER
#include "imgui_impl_dx12.h"
#define HAS_IMGUI_DX12 1
#else
#define HAS_IMGUI_DX12 0
#endif

static const int NUM_BACK_BUFFERS = 3;

// Kiero D3D12 vtable layout (from kiero.cpp):
//   [0..43]   ID3D12Device          (44 methods)
//   [44..62]  ID3D12CommandQueue     (19 methods)
//   [63..71]  ID3D12CommandAllocator  (9 methods)
//   [72..131] ID3D12GraphicsCommandList (60 methods)
//   [132..149] IDXGISwapChain        (18 methods)
//
// Present          = swapchain vtable index 8  -> kiero index 132 + 8  = 140
// ResizeBuffers    = swapchain vtable index 13 -> kiero index 132 + 13 = 145
// ExecuteCommandLists = commandQueue vtable index 10 -> kiero index 44 + 10 = 54
static const int KIERO_IDX_PRESENT             = 140;
static const int KIERO_IDX_RESIZE_BUFFERS      = 145;
static const int KIERO_IDX_EXECUTE_CMD_LISTS   = 54;

typedef long(__fastcall* PresentD3D12)(IDXGISwapChain*, UINT, UINT);
typedef void(__fastcall* ExecuteCommandListsFn)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
typedef HRESULT(__stdcall* ResizeBuffers12Fn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static PresentD3D12 oPresentD3D12 = nullptr;
static ExecuteCommandListsFn oExecuteCommandLists = nullptr;
static ResizeBuffers12Fn oResizeBuffers12 = nullptr;

#if HAS_IMGUI_DX12
static ID3D12Device* d3d12Device = nullptr;
static ID3D12CommandQueue* d3d12CommandQueue = nullptr;
static ID3D12DescriptorHeap* d3d12SrvDescHeap = nullptr;
static ID3D12DescriptorHeap* d3d12RtvDescHeap = nullptr;
static ID3D12CommandAllocator* d3d12CommandAllocators[NUM_BACK_BUFFERS] = {};
static ID3D12GraphicsCommandList* d3d12CommandList = nullptr;
static ID3D12Resource* d3d12BackBuffers[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE d3d12RtvHandles[NUM_BACK_BUFFERS] = {};
static bool d3d12_imgui_inited = false;
static UINT numBackBuffers = 0;

static void cleanup_render_targets() {
    for (int i = 0; i < NUM_BACK_BUFFERS; i++) {
        if (d3d12BackBuffers[i]) {
            d3d12BackBuffers[i]->Release();
            d3d12BackBuffers[i] = nullptr;
        }
    }
}

static void cleanup_d3d12() {
    if (d3d12CommandList) { d3d12CommandList->Release(); d3d12CommandList = nullptr; }
    for (int i = 0; i < NUM_BACK_BUFFERS; i++) {
        if (d3d12CommandAllocators[i]) {
            d3d12CommandAllocators[i]->Release();
            d3d12CommandAllocators[i] = nullptr;
        }
    }
    cleanup_render_targets();
    if (d3d12RtvDescHeap) { d3d12RtvDescHeap->Release(); d3d12RtvDescHeap = nullptr; }
    if (d3d12SrvDescHeap) { d3d12SrvDescHeap->Release(); d3d12SrvDescHeap = nullptr; }
    if (d3d12Device) { d3d12Device->Release(); d3d12Device = nullptr; }
}

static bool create_render_targets(IDXGISwapChain* pSwapChain) {
    for (UINT i = 0; i < numBackBuffers; i++) {
        if (FAILED(pSwapChain->GetBuffer(i, IID_PPV_ARGS(&d3d12BackBuffers[i])))) {
            return false;
        }
        d3d12Device->CreateRenderTargetView(d3d12BackBuffers[i], nullptr, d3d12RtvHandles[i]);
    }
    return true;
}

static bool init_d3d12_imgui(IDXGISwapChain* pSwapChain) {
    if (!d3d12CommandQueue)
        return false;

    HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&d3d12Device);
    if (FAILED(hr) || !d3d12Device)
        return false;

    DXGI_SWAP_CHAIN_DESC sd;
    pSwapChain->GetDesc(&sd);
    numBackBuffers = sd.BufferCount;
    if (numBackBuffers > NUM_BACK_BUFFERS)
        numBackBuffers = NUM_BACK_BUFFERS;

    // Create SRV descriptor heap (for ImGui font texture)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(d3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&d3d12SrvDescHeap)))) {
            cleanup_d3d12();
            return false;
        }
    }

    // Create RTV descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = numBackBuffers;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(d3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&d3d12RtvDescHeap)))) {
            cleanup_d3d12();
            return false;
        }

        SIZE_T rtvDescriptorSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = d3d12RtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < numBackBuffers; i++) {
            d3d12RtvHandles[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    // Create command allocators
    for (UINT i = 0; i < numBackBuffers; i++) {
        if (FAILED(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&d3d12CommandAllocators[i])))) {
            cleanup_d3d12();
            return false;
        }
    }

    if (!create_render_targets(pSwapChain)) {
        cleanup_d3d12();
        return false;
    }

    if (FAILED(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            d3d12CommandAllocators[0], nullptr, IID_PPV_ARGS(&d3d12CommandList)))) {
        cleanup_d3d12();
        return false;
    }
    d3d12CommandList->Close();

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)sd.BufferDesc.Width, (float)sd.BufferDesc.Height);
    ImGui::StyleColorsDark();
    HUDElements.convert_colors(false, params);

    ImGui_ImplWin32_Init(sd.OutputWindow);
    ImGui_ImplDX12_Init(d3d12Device, numBackBuffers, sd.BufferDesc.Format,
        d3d12SrvDescHeap,
        d3d12SrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        d3d12SrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    create_fonts(nullptr, params, sw_stats.font_small, sw_stats.font_text, sw_stats.font_secondary);
    sw_stats.font_params_hash = params.font_params_hash;

    d3d12_imgui_inited = true;
    return true;
}
#else
// MinGW: no ImGui DX12 backend, just capture the command queue for future use
static ID3D12CommandQueue* d3d12CommandQueue = nullptr;
#endif // HAS_IMGUI_DX12

long __fastcall hkPresent12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
{
    dx_version = kiero::RenderType::D3D12;

#if HAS_IMGUI_DX12
    if (!d3d12_imgui_inited)
        init_d3d12_imgui(pSwapChain);

    if (d3d12_imgui_inited && d3d12CommandQueue) {
        d3d_run();

        UINT backBufferIdx = pSwapChain->GetCurrentBackBufferIndex();
        if (backBufferIdx >= numBackBuffers)
            backBufferIdx = 0;

        DXGI_SWAP_CHAIN_DESC sd;
        pSwapChain->GetDesc(&sd);
        ImGui::GetIO().DisplaySize = ImVec2((float)sd.BufferDesc.Width, (float)sd.BufferDesc.Height);

        if (HUDElements.colors.update)
            HUDElements.convert_colors(params);

        d3d12CommandAllocators[backBufferIdx]->Reset();
        d3d12CommandList->Reset(d3d12CommandAllocators[backBufferIdx], nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = d3d12BackBuffers[backBufferIdx];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        d3d12CommandList->ResourceBarrier(1, &barrier);

        d3d12CommandList->OMSetRenderTargets(1, &d3d12RtvHandles[backBufferIdx], FALSE, nullptr);
        d3d12CommandList->SetDescriptorHeaps(1, &d3d12SrvDescHeap);

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        overlay_new_frame(params);
        position_layer(sw_stats, params, window_size);
        render_imgui(sw_stats, params, window_size, false);
        overlay_end_frame();

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d12CommandList);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        d3d12CommandList->ResourceBarrier(1, &barrier);

        d3d12CommandList->Close();
        ID3D12CommandList* ppCommandLists[] = { d3d12CommandList };
        d3d12CommandQueue->ExecuteCommandLists(1, ppCommandLists);

        if (sw_stats.font_params_hash != params.font_params_hash) {
            sw_stats.font_params_hash = params.font_params_hash;
            create_fonts(nullptr, params, sw_stats.font_small, sw_stats.font_text, sw_stats.font_secondary);
            ImGui_ImplDX12_InvalidateDeviceObjects();
            ImGui_ImplDX12_CreateDeviceObjects();
        }
    }
#else
    // MinGW fallback: stats collection only, no overlay rendering for D3D12
    d3d_run();
#endif

    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
}

void __fastcall hkExecuteCommandLists12(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
    if (!d3d12CommandQueue) {
        D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            d3d12CommandQueue = queue;
    }
    oExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

HRESULT __stdcall hkResizeBuffers12(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
#if HAS_IMGUI_DX12
    cleanup_render_targets();
#endif

    HRESULT hr = oResizeBuffers12(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

#if HAS_IMGUI_DX12
    if (SUCCEEDED(hr) && d3d12Device) {
        numBackBuffers = BufferCount;
        if (numBackBuffers > NUM_BACK_BUFFERS)
            numBackBuffers = NUM_BACK_BUFFERS;
        create_render_targets(pSwapChain);
    }
#endif
    return hr;
}

void impl::d3d12::init()
{
    auto ret = kiero::bind(KIERO_IDX_PRESENT, (void**)&oPresentD3D12, reinterpret_cast<void*>(hkPresent12));
    assert(ret == kiero::Status::Success);

    ret = kiero::bind(KIERO_IDX_EXECUTE_CMD_LISTS, (void**)&oExecuteCommandLists, reinterpret_cast<void*>(hkExecuteCommandLists12));
    assert(ret == kiero::Status::Success);

    ret = kiero::bind(KIERO_IDX_RESIZE_BUFFERS, (void**)&oResizeBuffers12, reinterpret_cast<void*>(hkResizeBuffers12));
    assert(ret == kiero::Status::Success);

    init_d3d_shared();
}

#endif // KIERO_INCLUDE_D3D12
