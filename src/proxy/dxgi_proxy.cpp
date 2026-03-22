#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../win/kiero.h"
#include "../win/win_shared.h"
#include "../blacklist.h"

#if KIERO_INCLUDE_D3D11
#include "../win/d3d11_hook.h"
#endif
#if KIERO_INCLUDE_D3D12
#include "../win/d3d12_hook.h"
#endif

static HMODULE s_realDXGI = nullptr;
static bool s_hooksInited = false;
static bool s_initFailed = false;

static void ensureRealDXGI() {
    if (s_realDXGI) return;
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    std::string path = std::string(sysDir) + "\\dxgi.dll";
    s_realDXGI = LoadLibraryA(path.c_str());
}

static DWORD WINAPI initOverlayThread(LPVOID) {
    // Wait a bit for the game to fully initialize its graphics
    Sleep(2000);

    if (is_blacklisted()) return 0;

    try {
        std::vector<kiero::RenderType::Enum> types;
        if (GetModuleHandleA("d3d11.dll"))
            types.push_back(kiero::RenderType::D3D11);
        if (GetModuleHandleA("d3d12.dll"))
            types.push_back(kiero::RenderType::D3D12);

        for (auto& t : types)
            kiero::init(t);

        if (!types.empty()) {
#if KIERO_INCLUDE_D3D11
            impl::d3d11::init();
#endif
#if KIERO_INCLUDE_D3D12
            impl::d3d12::init();
#endif
        }
    } catch (...) {
        s_initFailed = true;
    }

    return 0;
}

static void initOverlayHooks() {
    if (s_hooksInited || s_initFailed) return;

    const char* disable_hooks = getenv("MANGOHUD_DISABLE_DXGI_PROXY_HOOKS");
    if (disable_hooks && strcmp(disable_hooks, "1") == 0)
        return;

    s_hooksInited = true;

    // Do the actual hooking on a background thread so we don't block the game's
    // renderer initialization. The game needs its DXGI factory returned ASAP.
    CreateThread(NULL, 0, initOverlayThread, NULL, 0, NULL);
}

template<typename T>
static T getRealProc(const char* name) {
    ensureRealDXGI();
    if (!s_realDXGI) return nullptr;
    return (T)GetProcAddress(s_realDXGI, name);
}

typedef HRESULT (WINAPI *PFN_CreateDXGIFactory)(REFIID, void**);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory2)(UINT, REFIID, void**);
typedef HRESULT (WINAPI *PFN_DXGIGetDebugInterface1)(UINT, REFIID, void**);
typedef HRESULT (WINAPI *PFN_DXGIDeclareAdapterRemovalSupport)(void);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-declarations"

extern "C" {

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    auto real = getRealProc<PFN_CreateDXGIFactory>("CreateDXGIFactory");
    if (!real) return E_FAIL;
    HRESULT hr = real(riid, ppFactory);
    if (SUCCEEDED(hr)) initOverlayHooks();
    return hr;
}

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    auto real = getRealProc<PFN_CreateDXGIFactory1>("CreateDXGIFactory1");
    if (!real) return E_FAIL;
    HRESULT hr = real(riid, ppFactory);
    if (SUCCEEDED(hr)) initOverlayHooks();
    return hr;
}

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    auto real = getRealProc<PFN_CreateDXGIFactory2>("CreateDXGIFactory2");
    if (!real) return E_FAIL;
    HRESULT hr = real(Flags, riid, ppFactory);
    if (SUCCEEDED(hr)) initOverlayHooks();
    return hr;
}

__declspec(dllexport) HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    auto real = getRealProc<PFN_DXGIGetDebugInterface1>("DXGIGetDebugInterface1");
    if (!real) return E_FAIL;
    return real(Flags, riid, pDebug);
}

__declspec(dllexport) HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void) {
    auto real = getRealProc<PFN_DXGIDeclareAdapterRemovalSupport>("DXGIDeclareAdapterRemovalSupport");
    if (!real) return E_FAIL;
    return real();
}

} // extern "C"

#pragma GCC diagnostic pop

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInstance);
        ensureRealDXGI();
    }
    return TRUE;
}
