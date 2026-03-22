// MangoHud Windowed Overlay
// Standalone transparent window that renders the MangoHud HUD on top of games.
// Uses the exact same rendering/monitoring code as the DLL hooks.
// This is the default mode ("windowed mode"). No injection, no admin, no proxy DLLs.

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <implot.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "../overlay.h"
#include "../overlay_params.h"
#include "../hud_elements.h"
#include "../cpu.h"
#include "../gpu.h"
#include "../keybinds.h"
#include "../notify.h"
#include "../file_utils.h"
#include "../blacklist.h"

extern overlay_params *_params;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapchain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND g_hwnd = nullptr;
static bool g_running = true;

static swapchain_stats g_sw_stats {};
static ImVec2 g_window_size;
static notify_thread g_notifier {};

static void cleanup_rtv()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static void create_rtv()
{
    ID3D11Texture2D* backBuffer;
    g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (backBuffer) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        backBuffer->Release();
    }
}

static void cleanup_d3d()
{
    cleanup_rtv();
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

static bool create_d3d(HWND hwnd, int w, int h)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = w;
    sd.BufferDesc.Height = h;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_swapchain, &g_device, &featureLevel, &g_context
    );

    if (FAILED(hr))
        return false;

    create_rtv();
    return true;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED) {
            cleanup_rtv();
            g_swapchain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            create_rtv();
        }
        return 0;
    case WM_DISPLAYCHANGE:
        // Monitor resolution changed, resize overlay to match
        if (g_hwnd) {
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_NOACTIVATE);
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static HWND create_overlay_window()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MangoHudOverlay";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    // WS_EX_TOPMOST:     always on top
    // WS_EX_TRANSPARENT:  clicks pass through to the window below
    // WS_EX_LAYERED:      needed for per-pixel alpha
    // WS_EX_TOOLWINDOW:   doesn't show in taskbar or alt-tab
    // WS_EX_NOACTIVATE:   never steals focus
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"MangoHudOverlay",
        L"MangoHud",
        WS_POPUP,
        0, 0, sw, sh,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hwnd) return nullptr;

    // Set color key: pure black (0,0,0) becomes fully transparent
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Extend DWM frame for proper composition
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    return hwnd;
}

int main(int argc, char* argv[])
{
    overlay_params params {};

    // If this EXE lives next to MangoHud's dxgi proxy, keep that proxy in
    // pure-forwarding mode while the standalone overlay creates its own D3D device.
    SetEnvironmentVariableA("MANGOHUD_DISABLE_DXGI_PROXY_HOOKS", "1");

    // Prevent multiple instances
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"MangoHudOverlayMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    init_spdlog();

    // Ensure config dir exists
    std::string config_dir = get_config_dir();
    if (!config_dir.empty()) {
        CreateDirectoryA(config_dir.c_str(), nullptr);
    }

    _params = &params;

    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);

    gpus = std::make_unique<GPUS>(&params);

    init_cpu_stats(params);

    init_system_info();

    g_notifier.params = &params;
    start_notifier(g_notifier);

    g_hwnd = create_overlay_window();
    if (!g_hwnd) {
        SPDLOG_ERROR("Failed to create overlay window");
        stop_notifier(g_notifier);
        CloseHandle(mutex);
        return 1;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    if (!create_d3d(g_hwnd, sw, sh)) {
        SPDLOG_ERROR("Failed to create D3D11 device");
        stop_notifier(g_notifier);
        cleanup_d3d();
        DestroyWindow(g_hwnd);
        CloseHandle(mutex);
        return 1;
    }

    // Init ImGui
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)sw, (float)sh);

    ImGui::StyleColorsDark();
    HUDElements.convert_colors(false, params);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    create_fonts(nullptr, params, g_sw_stats.font_small, g_sw_stats.font_text, g_sw_stats.font_secondary);
    g_sw_stats.font_params_hash = params.font_params_hash;

    if (!logger) logger = std::make_unique<Logger>(&params);

    // Main loop
    MSG msg;
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                g_running = false;
        }
        if (!g_running) break;

        // Keybinds (toggle visibility, logging, etc)
        check_keybinds(params);

        // Update stats
        update_hud_info(g_sw_stats, params, 0);

        // Hot-reload colors
        if (HUDElements.colors.update)
            HUDElements.convert_colors(params);

        // Hot-reload fonts
        if (g_sw_stats.font_params_hash != params.font_params_hash) {
            g_sw_stats.font_params_hash = params.font_params_hash;
            create_fonts(nullptr, params, g_sw_stats.font_small, g_sw_stats.font_text, g_sw_stats.font_secondary);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        // Update display size if resolution changed
        int curW = GetSystemMetrics(SM_CXSCREEN);
        int curH = GetSystemMetrics(SM_CYSCREEN);
        io.DisplaySize = ImVec2((float)curW, (float)curH);

        // Render
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        overlay_new_frame(params);
        position_layer(g_sw_stats, params, g_window_size);
        render_imgui(g_sw_stats, params, g_window_size, false);
        overlay_end_frame();

        ImGui::Render();

        float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapchain->Present(1, 0); // vsync to avoid wasting GPU

        Sleep(1); // yield
    }

    // Cleanup
    stop_notifier(g_notifier);
    stop_hw_updater();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    cleanup_d3d();
    DestroyWindow(g_hwnd);
    ReleaseMutex(mutex);
    CloseHandle(mutex);

    return 0;
}
