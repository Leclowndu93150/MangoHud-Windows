// MangoHud Windowed Overlay
// Standalone transparent window that renders the MangoHud HUD on top of games.
// Uses the exact same rendering/monitoring code as the DLL hooks.
// This is the default mode ("windowed mode"). No injection, no admin, no proxy DLLs.

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <tlhelp32.h>
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
#include "fps_etw.h"

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

// Target game window tracking
static HWND g_target_hwnd = nullptr;
static std::string g_target_process;
static RECT g_target_rect = {};

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

// Get the process name for a given window handle
static std::string get_window_process_name(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return "";

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return "";

    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);

    std::string name;
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                name = pe.szExeFile;
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return name;
}

// Check if a window looks like a game (visible, sizable, has a title, not a system window)
static bool is_game_window(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) return false;
    if (hwnd == g_hwnd) return false;

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);

    // Skip tool windows, tooltips, etc
    if (exStyle & WS_EX_TOOLWINDOW) return false;

    // Must have a title
    char title[256];
    if (GetWindowTextA(hwnd, title, sizeof(title)) == 0) return false;

    // Skip tiny windows (probably UI elements)
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w < 400 || h < 300) return false;

    // Skip known non-game processes
    std::string proc = get_window_process_name(hwnd);
    if (proc == "explorer.exe" || proc == "MangoHud.exe" ||
        proc == "MangoJuice.exe" || proc == "mangojuice.exe" ||
        proc == "SearchHost.exe" || proc == "ShellExperienceHost.exe" ||
        proc == "SystemSettings.exe" || proc == "ApplicationFrameHost.exe" ||
        proc == "TextInputHost.exe" || proc == "cmd.exe" ||
        proc == "powershell.exe" || proc == "WindowsTerminal.exe" ||
        proc == "conhost.exe" || proc == "Code.exe")
        return false;

    return true;
}

// Find the target game window
// If g_target_process is set, find a window belonging to that process
// Otherwise find the foreground window if it looks like a game
static HWND find_target_window()
{
    if (!g_target_process.empty()) {
        // Enumerate all windows looking for one from this process
        struct EnumData {
            std::string target;
            HWND result;
            HWND overlay;
        } data = { g_target_process, nullptr, g_hwnd };

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* d = reinterpret_cast<EnumData*>(lParam);
            if (hwnd == d->overlay) return TRUE;
            if (!IsWindowVisible(hwnd)) return TRUE;

            std::string proc = get_window_process_name(hwnd);
            if (_stricmp(proc.c_str(), d->target.c_str()) == 0) {
                RECT r;
                GetClientRect(hwnd, &r);
                if ((r.right - r.left) >= 400 && (r.bottom - r.top) >= 300) {
                    d->result = hwnd;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&data));

        return data.result;
    }

    // Auto-detect: use the foreground window if it looks like a game
    HWND fg = GetForegroundWindow();
    if (fg && is_game_window(fg))
        return fg;

    return nullptr;
}

// Update the overlay window position/size to match the target game window
static bool track_target_window()
{
    HWND target = find_target_window();

    if (!target || !IsWindow(target)) {
        g_target_hwnd = nullptr;
        return false;
    }

    if (target != g_target_hwnd) {
        g_target_hwnd = target;
        char title[256] = {};
        GetWindowTextA(target, title, sizeof(title));
        std::string proc = get_window_process_name(target);
        SPDLOG_INFO("Targeting window: \"{}\" ({})", title, proc);

        // Start ETW FPS tracing for this process
        DWORD pid = 0;
        GetWindowThreadProcessId(target, &pid);
        if (pid) {
            etw_fps::stop();
            if (!etw_fps::start(pid)) {
                SPDLOG_WARN("ETW FPS tracing unavailable (need admin). FPS will not be shown.");
            }
        }
    }

    // Hide overlay if game window is minimized
    if (IsIconic(target))
        return false;

    // Hide overlay if game is not the foreground window (user alt-tabbed away)
    HWND fg = GetForegroundWindow();
    if (fg != target && fg != g_hwnd) {
        // Check if the foreground window is a child/popup of the game
        DWORD fg_pid = 0, target_pid = 0;
        GetWindowThreadProcessId(fg, &fg_pid);
        GetWindowThreadProcessId(target, &target_pid);
        if (fg_pid != target_pid)
            return false;
    }

    RECT rect;
    if (!GetWindowRect(target, &rect))
        return false;

    int x = rect.left;
    int y = rect.top;
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    if (w < 1 || h < 1) return false;

    // Only reposition if something changed
    if (memcmp(&rect, &g_target_rect, sizeof(RECT)) != 0) {
        g_target_rect = rect;
        SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);

        // Resize D3D swapchain if size changed
        static int last_w = 0, last_h = 0;
        if (w != last_w || h != last_h) {
            last_w = w;
            last_h = h;
            cleanup_rtv();
            g_swapchain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
            create_rtv();
        }
    }

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
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static HWND create_overlay_window(int x, int y, int w, int h)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MangoHudOverlay";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"MangoHudOverlay",
        L"MangoHud",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hwnd) return nullptr;

    // Use a very specific magenta as the transparent color key.
    // ImGui will never produce this exact color naturally.
    SetLayeredWindowAttributes(hwnd, RGB(1, 0, 1), 0, LWA_COLORKEY);

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    return hwnd;
}

int main(int argc, char* argv[])
{
    overlay_params params {};

    SetEnvironmentVariableA("MANGOHUD_DISABLE_DXGI_PROXY_HOOKS", "1");

    // Parse command line: MangoHud.exe [process_name.exe]
    if (argc > 1) {
        g_target_process = argv[1];
        printf("MangoHud: targeting process \"%s\"\n", g_target_process.c_str());
    } else {
        printf("MangoHud: auto-detecting game window (foreground)\n");
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"MangoHudOverlayMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    init_spdlog();

    std::string config_dir = get_config_dir();
    if (!config_dir.empty())
        CreateDirectoryA(config_dir.c_str(), nullptr);

    _params = &params;
    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);

    if (!gpus)
        gpus = std::make_unique<GPUS>(&params);

    init_cpu_stats(params);
    init_system_info();

    g_notifier.params = &params;
    start_notifier(g_notifier);

    // Start with screen-sized window, will resize once we find a target
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = create_overlay_window(0, 0, sw, sh);
    if (!g_hwnd) {
        SPDLOG_ERROR("Failed to create overlay window");
        stop_notifier(g_notifier);
        CloseHandle(mutex);
        return 1;
    }

    if (!create_d3d(g_hwnd, sw, sh)) {
        SPDLOG_ERROR("Failed to create D3D11 device");
        stop_notifier(g_notifier);
        cleanup_d3d();
        DestroyWindow(g_hwnd);
        CloseHandle(mutex);
        return 1;
    }

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

    printf("MangoHud overlay running. Waiting for game window...\n");

    MSG msg;
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                g_running = false;
        }
        if (!g_running) break;

        check_keybinds(params);

        // Throttle HW stats updates to ~2x per second (every 500ms)
        {
            static DWORD last_hw_update = 0;
            DWORD now = GetTickCount();
            if (now - last_hw_update >= 500) {
                last_hw_update = now;
                update_hw_info(params, 0);
            }
        }

        // Override FPS/frametime with real game data from ETW
        if (etw_fps::is_running()) {
            double real_fps = etw_fps::get_fps();
            uint64_t real_ft = etw_fps::get_frametime_ns();
            extern float frametime;
            extern double fps;
            extern std::vector<float> frametime_data;

            if (real_fps > 0) {
                g_sw_stats.fps = real_fps;
                fps = real_fps;
            }
            if (real_ft > 0) {
                float ft_ms = (float)real_ft / 1000000.0f;
                frametime = ft_ms;
                frametime_data.push_back(ft_ms);
                if (frametime_data.size() > 200)
                    frametime_data.erase(frametime_data.begin());
            }
        }

        if (HUDElements.colors.update)
            HUDElements.convert_colors(params);

        if (g_sw_stats.font_params_hash != params.font_params_hash) {
            g_sw_stats.font_params_hash = params.font_params_hash;
            create_fonts(nullptr, params, g_sw_stats.font_small, g_sw_stats.font_text, g_sw_stats.font_secondary);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        // Track target game window
        bool has_target = track_target_window();

        if (!has_target) {
            // No game window found, hide overlay and wait
            ShowWindow(g_hwnd, SW_HIDE);
            Sleep(500);
            continue;
        }

        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

        // Set display size to target window size
        int tw = g_target_rect.right - g_target_rect.left;
        int th = g_target_rect.bottom - g_target_rect.top;
        io.DisplaySize = ImVec2((float)tw, (float)th);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        overlay_new_frame(params);
        position_layer(g_sw_stats, params, g_window_size);
        render_imgui(g_sw_stats, params, g_window_size, false);
        overlay_end_frame();

        ImGui::Render();

        // Clear to the color key (RGB 1,0,1 = very dark magenta, becomes transparent)
        float clear[4] = { 1.0f/255.0f, 0.0f, 1.0f/255.0f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapchain->Present(1, 0);
        Sleep(1);
    }

    etw_fps::stop();
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
