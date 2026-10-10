#include "Config.h"
#include "Log.h"
#include <windows.h>
#include <cwchar>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace bvr {
namespace { Config g_cfg; }

static int ReadInt(const wchar_t* path, const wchar_t* key, int value) {
    return GetPrivateProfileIntW(L"VR", key, value, path);
}

static float ReadFloat(const wchar_t* path, const wchar_t* key, float value) {
    wchar_t fallback[64]{}, text[64]{};
    swprintf_s(fallback, L"%.7g", value);
    GetPrivateProfileStringW(L"VR", key, fallback, text, 64, path);
    wchar_t* end = nullptr;
    const float parsed = wcstof(text, &end);
    return end != text ? parsed : value;
}

void LoadConfig() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *(slash + 1) = L'\0';
    wcscat_s(path, L"BayonettaVR.ini");
    g_cfg.enabled = ReadInt(path, L"Enabled", 1) != 0;
    g_cfg.nativeStereo = ReadInt(path, L"NativeStereo", 1) != 0;
    g_cfg.forceHalfRate = ReadInt(path, L"ForceHalfRate", 0) != 0;
    g_cfg.uncapPresent = ReadInt(path, L"UncapPresent", 1) != 0;
    g_cfg.positionalTracking = ReadInt(path, L"PositionalTracking", 1) != 0;
    g_cfg.applyToUi = ReadInt(path, L"ApplyToUI", 0) != 0;
    g_cfg.swapEyes = ReadInt(path, L"SwapEyes", 0) != 0;
    g_cfg.mirrorSideBySide = ReadInt(path, L"MirrorSideBySide", 0) != 0;
    g_cfg.compositorPoseDiagnostic = ReadInt(path, L"CompositorPoseDiagnostic", 0) != 0;
    g_cfg.worldUnitsPerMetre = ReadFloat(path, L"WorldUnitsPerMetre", 1.0f);
    g_cfg.stereoScale = ReadFloat(path, L"StereoScale", 1.0f);
    g_cfg.gameVerticalFovDegrees = ReadFloat(path, L"GameVerticalFovDegrees", 60.0f);
    g_cfg.nearClip = ReadFloat(path, L"NearClip", 0.1f);
    g_cfg.farClip = ReadFloat(path, L"FarClip", 100000.0f);
    g_cfg.projectionMode = ReadInt(path, L"ProjectionMode", 0);
    g_cfg.matrixRegisterOverride = ReadInt(path, L"MatrixRegisterOverride", -1);
    Log("config: build=20260915.23 enabled=%d native=%d halfRate=%d uncapPresent=%d positional=%d compositorPoseDiagnostic=%d units/m=%.3f stereo=%.3f fov=%.2f projection=%d matrixOverride=%d",
        g_cfg.enabled, g_cfg.nativeStereo, g_cfg.forceHalfRate, g_cfg.uncapPresent,
        g_cfg.positionalTracking, g_cfg.compositorPoseDiagnostic,
        g_cfg.worldUnitsPerMetre,
        g_cfg.stereoScale, g_cfg.gameVerticalFovDegrees, g_cfg.projectionMode,
        g_cfg.matrixRegisterOverride);
}

const Config& Cfg() { return g_cfg; }
}
