#include "D3D9Hooks.h"
#include "Config.h"
#include "Log.h"
#include <windows.h>
#include <d3d9.h>

namespace {
HMODULE g_real = nullptr;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n >= MAX_PATH) return TRUE;
    wcscat_s(path, L"\\d3d9.dll");
    g_real = LoadLibraryW(path);
    bvr::LoadConfig();
    bvr::Log("proxy: real d3d9 %s", g_real ? "loaded" : "FAILED to load");
    return TRUE;
}

template<class T> T Resolve(const char* name) {
    InitOnceExecuteOnce(&g_once, LoadReal, nullptr, nullptr);
    return g_real ? reinterpret_cast<T>(GetProcAddress(g_real, name)) : nullptr;
}
}

extern "C" HRESULT WINAPI ProxyDirect3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
    using Fn = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);
    const auto fn = Resolve<Fn>("Direct3DCreate9Ex");
    if (!fn) return D3DERR_NOTAVAILABLE;
    const HRESULT hr = fn(sdk, out);
    bvr::Log("proxy: Direct3DCreate9Ex result=0x%08X", hr);
    if (SUCCEEDED(hr) && out) bvr::HookDirect3D9Ex(*out);
    return hr;
}

extern "C" IDirect3D9* WINAPI ProxyDirect3DCreate9(UINT sdk) {
    using Fn = IDirect3D9* (WINAPI*)(UINT);
    const auto fn = Resolve<Fn>("Direct3DCreate9");
    return fn ? fn(sdk) : nullptr;
}

extern "C" int WINAPI ProxyD3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name) {
    using Fn = int (WINAPI*)(D3DCOLOR, LPCWSTR);
    const auto fn = Resolve<Fn>("D3DPERF_BeginEvent");
    return fn ? fn(color, name) : -1;
}

extern "C" int WINAPI ProxyD3DPERF_EndEvent() {
    using Fn = int (WINAPI*)();
    const auto fn = Resolve<Fn>("D3DPERF_EndEvent");
    return fn ? fn() : -1;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
