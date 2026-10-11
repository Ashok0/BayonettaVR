#include "Config.h"
#include "Log.h"
#include <windows.h>
#include <cwchar>
#include <cmath>

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

static float ReadBoundedFloat(const wchar_t* path, const wchar_t* key,
                              float fallback, float minimum, float maximum) {
    const float value = ReadFloat(path, key, fallback);
    if (!std::isfinite(value)) return fallback;
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

void LoadConfig() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *(slash + 1) = L'\0';
    wcscat_s(path, L"BayonettaVR.ini");
    g_cfg.enabled = ReadInt(path, L"Enabled", 1) != 0;
    g_cfg.disableDepthOfField = ReadInt(path, L"DisableDepthOfField", 1) != 0;
    g_cfg.disableMotionBlur = ReadInt(path, L"DisableMotionBlur", 1) != 0;
    g_cfg.disableAmbientOcclusion = ReadInt(path, L"DisableAmbientOcclusion", 1) != 0;
    g_cfg.fixSoftParticleDepth = ReadInt(path, L"FixSoftParticleDepth", 1) != 0;
    g_cfg.worldLockedMovies = ReadInt(path, L"WorldLockedMovies", 1) != 0;
    g_cfg.movieDistanceMetres = ReadBoundedFloat(path,L"MovieDistanceMetres",3.0f,1.0f,10.0f);
    g_cfg.movieWidthMetres = ReadBoundedFloat(path,L"MovieWidthMetres",4.0f,1.0f,10.0f);
    Log("movies: worldLocked=%d width=%.2fm distance=%.2fm",g_cfg.worldLockedMovies,
        g_cfg.movieWidthMetres,g_cfg.movieDistanceMetres);
    Log("soft particle depth: FixSoftParticleDepth=%d (F1 toggles for comparison)",g_cfg.fixSoftParticleDepth);
    Log("ambient shadows: DisableAmbientOcclusion=%d (Ctrl+F1 toggles for comparison)",g_cfg.disableAmbientOcclusion);
    Log("motion blur: DisableMotionBlur=%d (Shift+F1 toggles for comparison)",g_cfg.disableMotionBlur);
    Log("dof: DisableDepthOfField=%d (four verified cutscene shaders)",g_cfg.disableDepthOfField);
    g_cfg.vrControllers = ReadInt(path, L"VRControllers", 1) != 0;
    g_cfg.externalCompositor = ReadInt(path, L"ExternalCompositor", 0) != 0;
    Log("config: external compositor=%d", g_cfg.externalCompositor);
    g_cfg.nativeStereo = ReadInt(path, L"NativeStereo", 1) != 0;
    g_cfg.forceHalfRate = ReadInt(path, L"ForceHalfRate", 0) != 0;
    g_cfg.uncapPresent = ReadInt(path, L"UncapPresent", 1) != 0;
    g_cfg.positionalTracking = ReadInt(path, L"PositionalTracking", 1) != 0;
    g_cfg.compositorRotation = ReadInt(path, L"CompositorRotation", 1) != 0;
    g_cfg.automaticTurning = ReadInt(path, L"AutomaticTurning", 0) != 0;
    g_cfg.directPoseTurning = ReadInt(path, L"DirectPoseTurning", 1) != 0;
    g_cfg.nativeEarlySubmit = ReadInt(path, L"NativeEarlySubmit", 0) != 0;
    g_cfg.nativeExplicitPose = ReadInt(path, L"NativeExplicitPose", 1) != 0;
    g_cfg.compositorTurnDegrees = ReadFloat(path, L"CompositorTurnDegrees", 12.0f);
    if (!std::isfinite(g_cfg.compositorTurnDegrees)) g_cfg.compositorTurnDegrees = 12.0f;
    if (g_cfg.compositorTurnDegrees < 0.0f) g_cfg.compositorTurnDegrees = 0.0f;
    if (g_cfg.compositorTurnDegrees > 0.0f && g_cfg.compositorTurnDegrees < 2.0f)
        g_cfg.compositorTurnDegrees = 2.0f;
    if (g_cfg.compositorTurnDegrees > 45.0f) g_cfg.compositorTurnDegrees = 45.0f;
    g_cfg.applyToUi = ReadInt(path, L"ApplyToUI", 0) != 0;
    g_cfg.worldLockedHud = ReadInt(path, L"WorldLockedHUD", 1) != 0;
    g_cfg.hudDistanceMetres = ReadBoundedFloat(path, L"HUDDistanceMetres", 2.0f, 0.5f, 10.0f);
    g_cfg.hudWidthMetres = ReadBoundedFloat(path, L"HUDWidthMetres", 3.2f, 0.5f, 10.0f);
    g_cfg.hudVerticalOffsetMetres = ReadBoundedFloat(path, L"HUDVerticalOffsetMetres", 0.0f, -5.0f, 5.0f);
    Log("hud: worldLocked=%d width=%.2fm distance=%.2fm verticalOffset=%.2fm; F8 reanchors",
        g_cfg.worldLockedHud, g_cfg.hudWidthMetres, g_cfg.hudDistanceMetres,
        g_cfg.hudVerticalOffsetMetres);
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
    Log("config: build=20261011.49 enabled=%d native=%d halfRate=%d uncapPresent=%d positional=%d compositorPoseDiagnostic=%d units/m=%.3f stereo=%.3f fov=%.2f projection=%d matrixOverride=%d compositorRotation=%d turnDegrees=%.1f autoTurn=%d directPoseTurning=%d earlySubmit=%d explicitNativePose=%d",
        g_cfg.enabled, g_cfg.nativeStereo, g_cfg.forceHalfRate, g_cfg.uncapPresent,
        g_cfg.positionalTracking, g_cfg.compositorPoseDiagnostic,
        g_cfg.worldUnitsPerMetre,
        g_cfg.stereoScale, g_cfg.gameVerticalFovDegrees, g_cfg.projectionMode,
        g_cfg.matrixRegisterOverride, g_cfg.compositorRotation, g_cfg.compositorTurnDegrees,
        g_cfg.automaticTurning, g_cfg.directPoseTurning, g_cfg.nativeEarlySubmit,
        g_cfg.nativeExplicitPose);
}

const Config& Cfg() { return g_cfg; }
}
