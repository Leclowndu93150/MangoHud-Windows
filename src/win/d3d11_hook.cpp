#include "kiero.h"

#if KIERO_INCLUDE_D3D11

#include "d3d11_hook.h"
#include <assert.h>
#include <intrin.h>
#include <d3d11.h>
#include <imgui.h>
#include <implot.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "d3d_shared.h"
#include "../overlay.h"
#include "../hud_elements.h"

typedef long(__stdcall* Present)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* ResizeBuffers)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static Present oPresent = NULL;
static ResizeBuffers oResizeBuffers = NULL;

static ID3D11Device* pDevice = nullptr;
static ID3D11DeviceContext* pContext = nullptr;
static ID3D11RenderTargetView* pRenderTargetView = nullptr;
static bool imgui_inited = false;

static void create_render_target(IDXGISwapChain* pSwapChain) {
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer) {
        pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
        pBackBuffer->Release();
    }
}

static void cleanup_render_target() {
    if (pRenderTargetView) {
        pRenderTargetView->Release();
        pRenderTargetView = nullptr;
    }
}

static void init_imgui(IDXGISwapChain* pSwapChain) {
    pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice);
    if (!pDevice)
        return;
    pDevice->GetImmediateContext(&pContext);
    if (!pContext)
        return;

    DXGI_SWAP_CHAIN_DESC sd;
    pSwapChain->GetDesc(&sd);

    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)sd.BufferDesc.Width, (float)sd.BufferDesc.Height);

    ImGui::StyleColorsDark();
    HUDElements.convert_colors(false, params);

    ImGui_ImplWin32_Init(sd.OutputWindow);
    ImGui_ImplDX11_Init(pDevice, pContext);

    create_fonts(nullptr, params, sw_stats.font_small, sw_stats.font_text, sw_stats.font_secondary);
    sw_stats.font_params_hash = params.font_params_hash;

    create_render_target(pSwapChain);
    imgui_inited = true;
}

long __stdcall hkPresent11(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    dx_version = kiero::RenderType::D3D11;

    if (!imgui_inited)
        init_imgui(pSwapChain);

    if (imgui_inited) {
        d3d_run();

        // Update display size from current swap chain dimensions
        DXGI_SWAP_CHAIN_DESC sd;
        pSwapChain->GetDesc(&sd);
        ImGui::GetIO().DisplaySize = ImVec2((float)sd.BufferDesc.Width, (float)sd.BufferDesc.Height);

        if (HUDElements.colors.update)
            HUDElements.convert_colors(params);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        overlay_new_frame(params);
        position_layer(sw_stats, params, window_size);
        render_imgui(sw_stats, params, window_size, false);
        overlay_end_frame();

        ImGui::Render();
        pContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (sw_stats.font_params_hash != params.font_params_hash) {
            sw_stats.font_params_hash = params.font_params_hash;
            create_fonts(nullptr, params, sw_stats.font_small, sw_stats.font_text, sw_stats.font_secondary);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        }
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT __stdcall hkResizeBuffers11(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    cleanup_render_target();
    HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    create_render_target(pSwapChain);
    return hr;
}

void impl::d3d11::init()
{
    printf("init d3d11\n");
    auto ret = kiero::bind(8, (void**)&oPresent, reinterpret_cast<void*>(hkPresent11));
    assert(ret == kiero::Status::Success);
    ret = kiero::bind(13, (void**)&oResizeBuffers, reinterpret_cast<void*>(hkResizeBuffers11));
    assert(ret == kiero::Status::Success);
    init_d3d_shared();
}

#endif // KIERO_INCLUDE_D3D11
