// proxyvrtest.cpp -- reproduces the game's exact situation outside the game.
//
// vrprobe.exe succeeds while the game fails, and the only relevant difference is
// that the game has our winmm proxy loaded under the base name "winmm.dll".
// vrclient_x64.dll imports WINMM.dll, so the loader binds its imports against
// whatever module already holds that name -- ours. If the proxy does not export
// everything vrclient needs, the bind fails and LoadLibrary(vrclient) fails,
// which OpenVR reports as VRInitError_Init_VRClientDLLNotFound (102).
//
// So: load the proxy first, exactly as the game does, then ask OpenVR to start.
//
//   proxyvrtest.exe <folder containing the proxy winmm.dll and openvr_api.dll>

#include <windows.h>
#include <cstdio>
#include <openvr.h>

typedef uint32_t (VR_CALLTYPE* PFN_Init)(vr::EVRInitError*, vr::EVRApplicationType);
typedef void     (VR_CALLTYPE* PFN_Shutdown)();
typedef const char* (VR_CALLTYPE* PFN_ErrDesc)(vr::EVRInitError);
typedef const char* (VR_CALLTYPE* PFN_ErrSym)(vr::EVRInitError);

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printf("usage: proxyvrtest <folder with proxy winmm.dll + openvr_api.dll>\n");
        return 2;
    }

    printf("proxy + OpenVR interaction test\n===============================\n\n");

    wchar_t proxy[MAX_PATH]{}, ovr[MAX_PATH]{};
    swprintf_s(proxy, L"%s\\winmm.dll", argv[1]);
    swprintf_s(ovr,   L"%s\\openvr_api.dll", argv[1]);

    // 1. Load the proxy first -- this is what the game's import table does.
    HMODULE p = LoadLibraryW(proxy);
    if (!p) {
        printf("FAIL: could not load the proxy (err %lu)\n", GetLastError());
        return 1;
    }
    wprintf(L"loaded proxy: %s\n", proxy);

    // Confirm it really is the module now owning the winmm name.
    HMODULE named = GetModuleHandleW(L"winmm.dll");
    printf("GetModuleHandle(\"winmm.dll\") == proxy: %s\n\n",
           (named == p) ? "YES (this is the game's situation)" : "no");

    // 2. Now bring up OpenVR, which will try to load vrclient_x64.dll.
    HMODULE dll = LoadLibraryW(ovr);
    if (!dll) {
        printf("FAIL: could not load openvr_api.dll (err %lu)\n", GetLastError());
        return 1;
    }

    auto init     = reinterpret_cast<PFN_Init>(GetProcAddress(dll, "VR_InitInternal"));
    auto shutdown = reinterpret_cast<PFN_Shutdown>(GetProcAddress(dll, "VR_ShutdownInternal"));
    auto desc     = reinterpret_cast<PFN_ErrDesc>(
                        GetProcAddress(dll, "VR_GetVRInitErrorAsEnglishDescription"));
    auto sym      = reinterpret_cast<PFN_ErrSym>(
                        GetProcAddress(dll, "VR_GetVRInitErrorAsSymbol"));
    if (!init || !shutdown) {
        printf("FAIL: openvr_api.dll missing exports\n");
        return 1;
    }

    vr::EVRInitError e = vr::VRInitError_None;
    init(&e, vr::VRApplication_Background);

    if (e == vr::VRInitError_None) {
        printf("VR_InitInternal(Background): OK\n");
        shutdown();
        printf("\nRESULT: the proxy does NOT break OpenVR.\n");
        return 0;
    }

    printf("VR_InitInternal(Background): %s (%d): %s\n",
           sym ? sym(e) : "?", static_cast<int>(e), desc ? desc(e) : "");

    if (e == vr::VRInitError_Init_VRClientDLLNotFound) {
        printf("\nRESULT: REPRODUCED. The proxy is shadowing winmm for vrclient_x64.dll.\n"
               "        vrclient imports timeSetEvent/timeKillEvent among others; any\n"
               "        export the proxy is missing fails the import bind and the whole\n"
               "        vrclient load is rejected.\n");
    } else {
        printf("\nRESULT: failed, but not with 102 -- something else is wrong.\n");
    }
    return 1;
}
