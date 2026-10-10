// vrprobe.cpp -- asks OpenVR the same questions TombRaiderVR asks, in a plain
// x64 process, and prints every answer.
//
// The point is to separate two very different failures that look identical from
// inside the game:
//
//   * OpenVR itself is not healthy in this environment (runtime not found,
//     SteamVR not running, headset asleep, wrong openvr_api.dll)
//   * OpenVR is fine and the mod is at fault
//
// Run it from the game folder so it picks up the same openvr_api.dll the mod
// would:  vrprobe.exe

#include <windows.h>
#include <cstdio>
#include <openvr.h>

typedef uint32_t (VR_CALLTYPE* PFN_Init)(vr::EVRInitError*, vr::EVRApplicationType);
typedef void     (VR_CALLTYPE* PFN_Shutdown)();
typedef void*    (VR_CALLTYPE* PFN_GetIface)(const char*, vr::EVRInitError*);
typedef bool     (VR_CALLTYPE* PFN_IsHmdPresent)();
typedef bool     (VR_CALLTYPE* PFN_IsRuntimeInstalled)();
typedef const char* (VR_CALLTYPE* PFN_ErrDesc)(vr::EVRInitError);
typedef const char* (VR_CALLTYPE* PFN_ErrSym)(vr::EVRInitError);

static PFN_ErrDesc g_desc = nullptr;
static PFN_ErrSym  g_sym  = nullptr;

static void PrintErr(const char* what, vr::EVRInitError e) {
    printf("  %-34s %s (%d): %s\n", what,
           g_sym ? g_sym(e) : "?", static_cast<int>(e),
           g_desc ? g_desc(e) : "");
}

static bool TryInit(const char* label, vr::EVRApplicationType type, PFN_Init init,
                    PFN_Shutdown shutdown, PFN_GetIface getIface) {
    vr::EVRInitError e = vr::VRInitError_None;
    init(&e, type);
    if (e != vr::VRInitError_None) {
        PrintErr(label, e);
        return false;
    }
    printf("  %-34s OK\n", label);

    auto* sys = static_cast<vr::IVRSystem*>(getIface(vr::IVRSystem_Version, &e));
    if (!sys) {
        PrintErr("  IVRSystem", e);
        shutdown();
        return false;
    }
    printf("  %-34s OK (%s)\n", "  IVRSystem", vr::IVRSystem_Version);

    // Sample tracking a few times so you can move the headset and watch it.
    printf("\n  sampling head pose for 3 seconds -- move the headset:\n");
    for (int i = 0; i < 6; ++i) {
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
        sys->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseSeated, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);
        const auto& h = poses[vr::k_unTrackedDeviceIndex_Hmd];
        if (h.bPoseIsValid) {
            const auto& m = h.mDeviceToAbsoluteTracking.m;
            printf("    valid  pos %+.3f %+.3f %+.3f   fwd %+.2f %+.2f %+.2f\n",
                   m[0][3], m[1][3], m[2][3], -m[0][2], -m[1][2], -m[2][2]);
        } else {
            printf("    INVALID pose (connected=%d)\n", h.bDeviceIsConnected);
        }
        Sleep(500);
    }

    shutdown();
    return true;
}

int main() {
    printf("OpenVR probe\n============\n\n");

    HMODULE dll = LoadLibraryW(L"openvr_api.dll");
    if (!dll) {
        printf("FAIL: openvr_api.dll not found in this folder or on PATH (err %lu)\n",
               GetLastError());
        printf("      Copy it from SteamVR\\bin\\win64\\openvr_api.dll\n");
        return 1;
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(dll, path, MAX_PATH);
    wprintf(L"loaded: %s\n\n", path);

    auto init     = reinterpret_cast<PFN_Init>(GetProcAddress(dll, "VR_InitInternal"));
    auto shutdown = reinterpret_cast<PFN_Shutdown>(GetProcAddress(dll, "VR_ShutdownInternal"));
    auto getIface = reinterpret_cast<PFN_GetIface>(GetProcAddress(dll, "VR_GetGenericInterface"));
    auto hmdOk    = reinterpret_cast<PFN_IsHmdPresent>(GetProcAddress(dll, "VR_IsHmdPresent"));
    auto rtOk     = reinterpret_cast<PFN_IsRuntimeInstalled>(GetProcAddress(dll, "VR_IsRuntimeInstalled"));
    g_desc = reinterpret_cast<PFN_ErrDesc>(GetProcAddress(dll, "VR_GetVRInitErrorAsEnglishDescription"));
    g_sym  = reinterpret_cast<PFN_ErrSym>(GetProcAddress(dll, "VR_GetVRInitErrorAsSymbol"));

    if (!init || !shutdown || !getIface) {
        printf("FAIL: openvr_api.dll is missing expected exports\n");
        return 1;
    }

    printf("cheap checks (these are advisory, not verdicts):\n");
    printf("  %-34s %s\n", "VR_IsRuntimeInstalled",
           (rtOk && rtOk()) ? "yes" : "NO");
    printf("  %-34s %s\n", "VR_IsHmdPresent",
           (hmdOk && hmdOk()) ? "yes" : "NO  <- often a false negative");

    printf("\nBackground app (what mono mode asks for first):\n");
    if (TryInit("VR_InitInternal(Background)", vr::VRApplication_Background,
                init, shutdown, getIface)) {
        printf("\nRESULT: OpenVR is healthy. Mono head-tracking should work.\n");
        return 0;
    }

    printf("\nOverlay app (the fallback -- this one can start SteamVR):\n");
    if (TryInit("VR_InitInternal(Overlay)", vr::VRApplication_Overlay,
                init, shutdown, getIface)) {
        printf("\nRESULT: Background failed but Overlay works. The mod falls back\n"
               "        to Overlay automatically, so mono should still work.\n");
        return 0;
    }

    printf("\nRESULT: OpenVR could not initialise at all. Fix this before blaming\n"
           "        the mod -- start SteamVR, wake the headset, and re-run.\n");
    return 1;
}
