#include "VR.h"
#include "IndependentGrid.h"
#include "CompanionClient.h"
#include "ControllerHooks.h"
#include "TouchControllers.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;
#include "Config.h"
#include "Log.h"
#include "TrackingMath.h"
#include "WorldHud.h"
#include <cmath>

namespace bvr {
namespace {
using InitFn = uint32_t (VR_CALLTYPE*)(vr::EVRInitError*, vr::EVRApplicationType);
using ShutdownFn = void (VR_CALLTYPE*)();
using InterfaceFn = void* (VR_CALLTYPE*)(const char*, vr::EVRInitError*);
using ErrorFn = const char* (VR_CALLTYPE*)(vr::EVRInitError);
ShutdownFn g_shutdown = nullptr;
VRRuntime g_vr;

Mat4 FromVR(const vr::HmdMatrix34_t& v) {
    Mat4 m = Identity();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) m.m[r][c] = v.m[r][c];
    return m;
}

vr::HmdMatrix34_t ToVR34(const Mat4& m) {
    vr::HmdMatrix34_t result{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) result.m[r][c] = m.m[r][c];
    return result;
}

Mat4 ToGameCoordinates(const Mat4& v, float units) {
    // OpenVR looks down -Z; Bayonetta/D3D view space looks down +Z.
    Mat4 flip = Identity();
    flip.m[2][2] = -1.0f;
    Mat4 o = Mul(flip, Mul(v, flip));
    o.m[0][3] *= units;
    o.m[1][3] *= units;
    o.m[2][3] *= units;
    return o;
}

bool GetBayonettaProjection(Mat4& projection) {
    // BayoHook independently identifies 0x00F2DCE0 as the Steam build's live
    // row-vector view-projection matrix. For V*P, the lengths of columns 0 and
    // 1 (excluding translation) are the perspective X/Y scales because the
    // view rotation is orthonormal. This remains valid as the game's FOV
    // changes between gameplay and cutscenes.
    const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return false;
    constexpr uintptr_t viewProjectionRva = 0x00B2DCE0; // VA 0x00F2DCE0
    const float* m = reinterpret_cast<const float*>(image + viewProjectionRva);
    const float forwardSq = m[3] * m[3] + m[7] * m[7] + m[11] * m[11];
    const float forward = std::sqrt(forwardSq);
    const float x = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    const float y = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    const float depth = (m[2] * m[3] + m[6] * m[7] + m[10] * m[11]) /
                        forwardSq;
    const float depthOffset = m[14] - depth * m[15];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(forward) ||
        !std::isfinite(depth) || !std::isfinite(depthOffset) ||
        x < 0.05f || x > 20.0f || y < 0.05f || y > 20.0f ||
        forward < 0.5f || forward > 1.5f || std::fabs(depth) < 0.1f ||
        std::fabs(depth) > 10.0f || std::fabs(depthOffset) < 1e-6f ||
        std::fabs(depthOffset) > 10000.0f) return false;
    projection = {};
    projection.m[0][0] = x / forward;
    projection.m[1][1] = y / forward;
    projection.m[2][2] = depth;
    projection.m[2][3] = depthOffset;
    projection.m[3][2] = 1.0f;
    return true;
}
}

VRRuntime& VR() { return g_vr; }

bool VRRuntime::Init() {
    if (!Cfg().enabled) return false;
    external_ = Cfg().externalCompositor;
    if (external_) {
        if (!Cfg().nativeStereo) { Log("companion: NativeStereo=1 is required"); return false; }
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
        if (wchar_t* slash = wcsrchr(path,L'\\')) *slash=L'\0';
        if (!Companion().Start(path)) return false;
        ipc::Control control{};
        if (!Companion().Poll(control)) { Companion().Stop(); return false; }
        externalAdapter_ = control.calibration.adapter;
        for (int e=0;e<2;++e) {
            eyeToHead_[e] = FromVR(control.calibration.eyes[e]);
            for(int n=0;n<4;++n)rawProjection_[e][n]=control.calibration.tangents[e][n];
        }
        stereoScale_=Cfg().stereoScale;swapEyes_=Cfg().swapEyes;projectionMode_=Cfg().projectionMode;
        automaticTurning_=true;poseMode_=TrackingMode();
        WaitForPose();
        Log("companion: full native tracking active; game process did not initialize OpenVR");
        return true;
    }
    dll_ = LoadLibraryW(L"openvr_api.dll");
    if (!dll_) { Log("vr: openvr_api.dll was not found"); return false; }
    const auto init = reinterpret_cast<InitFn>(GetProcAddress(dll_, "VR_InitInternal"));
    const auto getInterface = reinterpret_cast<InterfaceFn>(GetProcAddress(dll_, "VR_GetGenericInterface"));
    g_shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(dll_, "VR_ShutdownInternal"));
    const auto errorText = reinterpret_cast<ErrorFn>(GetProcAddress(dll_, "VR_GetVRInitErrorAsEnglishDescription"));
    if (!init || !getInterface || !g_shutdown) { Log("vr: required OpenVR exports are absent"); return false; }

    vr::EVRInitError error = vr::VRInitError_None;
    init(&error, vr::VRApplication_Scene);
    if (error != vr::VRInitError_None) {
        Log("vr: initialization failed (%d): %s", static_cast<int>(error),
            errorText ? errorText(error) : "unknown error");
        return false;
    }
    system_ = static_cast<vr::IVRSystem*>(getInterface(vr::IVRSystem_Version, &error));
    compositor_ = static_cast<vr::IVRCompositor*>(getInterface(vr::IVRCompositor_Version, &error));
    if (!system_ || !compositor_) {
        Log("vr: IVRSystem/IVRCompositor interface unavailable (%d)", static_cast<int>(error));
        Shutdown();
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        const vr::EVREye e = i ? vr::Eye_Right : vr::Eye_Left;
        eyeToHead_[i] = FromVR(system_->GetEyeToHeadTransform(e));
        system_->GetProjectionRaw(e, &rawProjection_[i][0], &rawProjection_[i][1],
                                  &rawProjection_[i][2], &rawProjection_[i][3]);
        Log("vr: eye %d tangents %.4f %.4f %.4f %.4f offset %.4f %.4f %.4f m", i,
            rawProjection_[i][0], rawProjection_[i][1], rawProjection_[i][2], rawProjection_[i][3],
            eyeToHead_[i].m[0][3], eyeToHead_[i].m[1][3], eyeToHead_[i].m[2][3]);
    }
    stereoScale_ = Cfg().stereoScale;
    swapEyes_ = Cfg().swapEyes;
    projectionMode_ = Cfg().projectionMode;
    automaticTurning_ = Cfg().automaticTurning;
    poseMode_ = Cfg().compositorPoseDiagnostic ? 0 : TrackingMode();
    Log("vr: tracking mode %d/%S (F7 toggles fixed baseline)", poseMode_, PoseModeLabel());
    Log("vr: calibration stereo scale %.3f, effective eye spacing %.4f world units, swapped=%d",
        stereoScale_,
        std::fabs(eyeToHead_[1].m[0][3] - eyeToHead_[0].m[0][3]) *
            Cfg().worldUnitsPerMetre * stereoScale_,
        swapEyes_);
    Log("vr: OpenVR ready (%s, %s)", vr::IVRSystem_Version, vr::IVRCompositor_Version);
    vr::ETrackedPropertyError frequencyError = vr::TrackedProp_Success;
    const float displayFrequency = system_->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &frequencyError);
    Log("vr: HMD display frequency %.2f Hz (property error %d)", displayFrequency,
        static_cast<int>(frequencyError));
    if (Cfg().forceHalfRate) {
        compositor_->ForceInterleavedReprojectionOn(true);
        Log("vr: consistent half-rate reprojection requested (target %.2f Hz)",
            displayFrequency > 0.0f ? displayFrequency * 0.5f : 0.0f);
    } else {
        compositor_->ForceInterleavedReprojectionOn(false);
    }
    WaitForPose();
    Log("vr: origin will recenter on the first perspective draw");
    return true;
}

bool VRRuntime::Active() const {
    return external_ ? Companion().Ready() : system_ && compositor_;
}
void VRRuntime::SetExternalGrid(bool enabled) { if (external_) Companion().SetGrid(enabled); }
bool VRRuntime::SubmitPair(ID3D11Device* device, ID3D11DeviceContext* context,
                           ID3D11Texture2D* left, ID3D11Texture2D* right) {
    if (!external_ || !renderPoseValid_[0] || !renderPoseValid_[1] ||
        capturedStamp_[0].sample != capturedStamp_[1].sample) return false;
    ipc::Frame frame{};
    frame.sample=capturedStamp_[0].sample;
    for(int e=0;e<2;++e){frame.pose[e]=renderPose_[e];frame.revision[e]=capturedRevision_[e];}
    return Companion().Publish(device,context,left,right,frame);
}
bool VRRuntime::StartIndependentGrid() {
    return IndependentVRGrid().Start(system_, compositor_);
}

void VRRuntime::Shutdown() {
    UpdateControllerInput({});
    IndependentVRGrid().StopAndJoin();
    if (external_) Companion().Stop();
    if (system_ && g_shutdown) g_shutdown();
    system_ = nullptr;
    compositor_ = nullptr;
    if (dll_) { FreeLibrary(dll_); dll_ = nullptr; }
}

void VRRuntime::Recenter() {
    origin_ = headPose_;
    renderAnchor_ = origin_;
    originValid_ = poseValid_;
    ++renderRevision_;
    Log("vr: tracking origin recentered");
}

void VRRuntime::EnsureOrigin() {
    if (!originValid_ && poseValid_) Recenter();
}

void VRRuntime::CycleStereoScale() {
    // Start by reducing separation because excessive IPD is the common cause
    // of an image pair that cannot be fused. Zero is a useful mono diagnostic.
    static const float values[] = {1.0f, 0.5f, 0.25f, 0.0f, 2.0f};
    size_t nearest = 0;
    float best = std::fabs(stereoScale_ - values[0]);
    for (size_t i = 1; i < _countof(values); ++i) {
        const float distance = std::fabs(stereoScale_ - values[i]);
        if (distance < best) { best = distance; nearest = i; }
    }
    stereoScale_ = values[(nearest + 1) % _countof(values)];
    Log("vr: F9 stereo scale %.3f, effective eye spacing %.4f world units",
        stereoScale_,
        std::fabs(eyeToHead_[1].m[0][3] - eyeToHead_[0].m[0][3]) *
            Cfg().worldUnitsPerMetre * stereoScale_);
}

void VRRuntime::ToggleEyeSwap() {
    swapEyes_ = !swapEyes_;
    Log("vr: F10 eye swap %s", swapEyes_ ? "on" : "off");
}

void VRRuntime::CycleProjectionMode() {
    projectionMode_ = (projectionMode_ + 1) % 3;
    const char* names[] = {"native lens center", "reversed lens center", "centered diagnostic"};
    Log("vr: F11 projection mode %d (%s)", projectionMode_, names[projectionMode_]);
}

void VRRuntime::ToggleCompositorPoseDiagnostic() {
    poseMode_ = poseMode_ == 0 ? TrackingMode() : 0;
    ++renderRevision_;
    static const char* names[] = {
        "compositor-only fixed baseline",
        "native rotation-only exact pose",
        "native full-6DOF exact pose",
        "tracked eye positions with compositor rotation",
    };
    Log("vr: F7 pose mode %d (%s)", poseMode_, names[poseMode_]);
}

void VRRuntime::ToggleAutomaticTurning() {
    if(external_){Log("companion: F6 inactive; native tracking is already enabled (F7 toggles fixed mode)");return;}
    automaticTurning_ = !automaticTurning_;
    if (!automaticTurning_) renderAnchor_ = origin_;
    if (poseMode_ != 0) poseMode_ = TrackingMode();
    ++renderRevision_;
    Log("vr: F6 turning %s; mode=%S (F7 baseline override=%d)",
        automaticTurning_ ? (Cfg().directPoseTurning ? "on: native WaitGetPoses + ordinary submit" :
            "on: legacy stepped synthetic pose") : "off: smooth fixed-direction positional mode",
        PoseModeLabel(), poseMode_ == 0);
}

const wchar_t* VRRuntime::PoseModeLabel() const {
    static const wchar_t* names[] = {L"fixed", L"rotation", L"full6dof", L"compositor6dof"};
    return names[poseMode_];
}

void VRRuntime::LogPoseDiagnostic() const {
    const Mat4 relative = Mul(InvertRigid(origin_), headPose_);
    static const char* names[] = {"fixed", "rotation-only", "full-6dof", "compositor-6dof"};
    Log("pose capture: mode=%d/%s valid=%d originValid=%d eye=%d",
        poseMode_, names[poseMode_], poseValid_, originValid_, eye_);
    for (int row = 0; row < 3; ++row) {
        Log("pose capture: head[%d]=%.7f %.7f %.7f %.7f origin[%d]=%.7f %.7f %.7f %.7f relative[%d]=%.7f %.7f %.7f %.7f",
            row, headPose_.m[row][0], headPose_.m[row][1], headPose_.m[row][2], headPose_.m[row][3],
            row, origin_.m[row][0], origin_.m[row][1], origin_.m[row][2], origin_.m[row][3],
            row, relative.m[row][0], relative.m[row][1], relative.m[row][2], relative.m[row][3]);
    }
}

void VRRuntime::WaitForPose() {
    if (external_) {
        ipc::Control control{};
        if (!Companion().Poll(control)) { poseValid_=false;UpdateControllerInput({});return; }
        poseSample_=control.tracking.sample;
        poseValid_=control.tracking.valid && GetTickCount64()-control.tracking.tick<250;
        if(poseValid_)headPose_=FromVR(control.tracking.head);
        ApplyControllers(control.controllers);
        return;
    }
    if (!compositor_) return;
    // Advance even if the wait fails so old images cannot inherit a new
    // implicit compositor pose when tracking resumes.
    ++poseSample_;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    const vr::EVRCompositorError e = compositor_->WaitGetPoses(
        poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    if (e != vr::VRCompositorError_None) {
        poseValid_ = false;
        UpdateControllerInput({});
        Log("vr: WaitGetPoses error %d", static_cast<int>(e));
        return;
    }
    const auto& predicted = poses[vr::k_unTrackedDeviceIndex_Hmd];
    poseValid_ = predicted.bPoseIsValid;
    if (poseValid_) headPose_ = FromVR(predicted.mDeviceToAbsoluteTracking);
    if(Cfg().vrControllers) {
        static TouchControllers controllers;
        DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        ApplyControllers(controllers.Poll(system_,poseValid_&&foreground==GetCurrentProcessId(),GetTickCount64()));
    }
    if (poseValid_ && originValid_ && poseMode_ == 3 && automaticTurning_ &&
        FollowRenderAnchor(renderAnchor_, headPose_, Cfg().compositorTurnDegrees)) {
        ++renderRevision_;
        ++anchorTurns_;
    }
}

void VRRuntime::ApplyControllers(const ControllerInput& input) {
    if(!Cfg().vrControllers)return;
    UpdateControllerInput(input);
    if(input.recenterSequence!=controllerRecenterSequence_) {
        controllerRecenterSequence_=input.recenterSequence;
        const auto now=GetTickCount64();
        if(poseValid_&&input.active&&now>=input.tick&&now-input.tick<=250) {
            Recenter();Log("controllers: camera and HUD recentered");
        }
    }
}

void VRRuntime::PostPresentHandoff() {
    if (compositor_) compositor_->PostPresentHandoff();
}

Mat4 VRRuntime::EyeViewProjection(int eye, float units, const Mat4& input) const {
    if (!poseValid_ || !originValid_) return Identity();
    int e = swapEyes_ ? (eye ^ 1) : eye;
    // The SAME synthetic head pose is used for rendering and texture submit.
    // Mode 3 keeps native rotation stable between anchor updates. The anchor
    // can follow large turns without changing origin_ or the tracked position.
    Mat4 relativeHead = Mul(InvertRigid(origin_), RenderHeadForEye(e));
    Mat4 scaledEye = eyeToHead_[e];
    scaledEye.m[0][3] *= stereoScale_;
    scaledEye.m[1][3] *= stereoScale_;
    scaledEye.m[2][3] *= stereoScale_;
    Mat4 eyePose = Mul(relativeHead, scaledEye);
    Mat4 eyeView = ToGameCoordinates(InvertRigid(eyePose), units);
    const float* q = rawProjection_[e];
    float l = q[0], r = q[1], t = q[2], b = q[3];
    if (projectionMode_ == 1) {
        // Diagnostic for a clip-space handedness/sign mismatch: preserve each
        // eye's FOV span while reflecting its optical center horizontally.
        l = -q[1];
        r = -q[0];
    } else if (projectionMode_ == 2) {
        // Remove the per-eye optical-center offset. This is not intended as a
        // final headset projection, but clean fusion here isolates the fault.
        const float halfWidth = (q[1] - q[0]) * 0.5f;
        const float halfHeight = (q[3] - q[2]) * 0.5f;
        l = -halfWidth;
        r = halfWidth;
        t = -halfHeight;
        b = halfHeight;
    }
    Mat4 eyeProjection = PerspectiveRawLH(l, r, t, b,
                                          Cfg().nearClip, Cfg().farClip);
    // Preserve the game path's original multiplication order as well as its
    // values. The independent reference uses the default identity input.
    return Mul(eyeProjection, Mul(eyeView, input));
}

Mat4 VRRuntime::ClipTransform(float gameAspect) const {
    if (!poseValid_ || !originValid_) return Identity();
    const float pi = 3.14159265358979323846f;
    Mat4 gameProjection = PerspectiveFovLH(Cfg().gameVerticalFovDegrees * pi / 180.0f,
                                           gameAspect, Cfg().nearClip, Cfg().farClip);
    Mat4 liveGameProjection{};
    if (GetBayonettaProjection(liveGameProjection)) {
        gameProjection = liveGameProjection;
        static float lastLoggedY = 0.0f;
        if (std::fabs(gameProjection.m[1][1] - lastLoggedY) > 0.02f) {
            const float detectedFov = 2.0f * std::atan(1.0f / gameProjection.m[1][1]) * 180.0f / pi;
            Log("camera: exact projection x=%.4f y=%.4f z=%.6f zw=%.6f aspect=%.4f verticalFov=%.2f",
                gameProjection.m[0][0], gameProjection.m[1][1],
                gameProjection.m[2][2], gameProjection.m[2][3],
                gameProjection.m[1][1] / gameProjection.m[0][0], detectedFov);
            lastLoggedY = gameProjection.m[1][1];
        }
    }
    Mat4 invGame{};
    if (!Invert(gameProjection, invGame)) return Identity();
    // Existing clip = Pgame * modelView * vertex. Replace the camera/projection
    // portion without needing Bayonetta's model or view matrices separately.
    return EyeViewProjection(eye_, Cfg().worldUnitsPerMetre, invGame);
}

Mat4 VRRuntime::HudClipTransform(float gameAspect) const {
    if (!poseValid_ || !originValid_) return Identity();
    const Mat4 panel = HudPanelFromClip(gameAspect, Cfg().hudWidthMetres,
        Cfg().hudDistanceMetres, Cfg().hudVerticalOffsetMetres);
    // Use the same eye pose as the scene and its submission metadata. The
    // fixed panel is measured in metres, independent of game FOV/world scale.
    return EyeViewProjection(eye_, 1.0f, panel);
}

int VRRuntime::DXGIAdapterIndex() const {
    if (external_) return externalAdapter_;
    int32_t index = -1;
    if (system_) system_->GetDXGIOutputInfo(&index);
    return static_cast<int>(index);
}

int VRRuntime::TrackingMode() const {
    if (external_) return Cfg().positionalTracking ? 2 : 1;
    return SelectTrackingMode(Cfg().compositorRotation, Cfg().positionalTracking,
                              automaticTurning_, Cfg().directPoseTurning);
}

Mat4 VRRuntime::RenderHeadForEye(int eye) const {
    Mat4 pose = headPose_;
    if (poseMode_ == 0) {
        pose = origin_;
    } else if (poseMode_ == 3) {
        Mat4 scaledEye = eyeToHead_[eye];
        for (int row = 0; row < 3; ++row) scaledEye.m[row][3] *= stereoScale_;
        pose = CompositorRotationHead(renderAnchor_, headPose_, scaledEye);
    } else if (poseMode_ == 1) {
        // The rendered camera uses current orientation at the recentered
        // position. Submit that same synthetic pose so compositor correction
        // exactly matches the image instead of reintroducing tracked motion.
        pose.m[0][3] = origin_.m[0][3];
        pose.m[1][3] = origin_.m[1][3];
        pose.m[2][3] = origin_.m[2][3];
    }
    return pose;
}

void VRRuntime::MarkEyeRendered(int eye) {
    if (eye < 0 || eye > 1) return;
    const Mat4 pose = RenderHeadForEye(swapEyes_ ? (eye ^ 1) : eye);
    renderPose_[eye] = ToVR34(pose);
    renderPoseValid_[eye] = poseValid_ && originValid_;
    capturedStamp_[eye] = {poseSample_, renderPoseValid_[eye],
                          poseMode_ == 2 && Cfg().directPoseTurning};
    // Capture WITH the eye pixels, before Present handles F6/F7/F8. The grid
    // must use the same pose as the submitted image, not the next frame's pose.
    referenceClip_[eye] = EyeViewProjection(eye, 1.0f);
    capturedRevision_[eye] = renderRevision_;
}

vr::EVRCompositorError VRRuntime::Submit(int eye, ID3D11Texture2D* texture) {
    if (!compositor_ || !texture || eye < 0 || eye > 1) return vr::VRCompositorError_RequestFailed;
    if (UseOrdinaryPoseSubmit(capturedStamp_[eye], poseSample_, poseValid_,
                              !Cfg().nativeExplicitPose)) {
        // No synthetic pose or discontinuity flag: these pixels were rendered
        // from this exact WaitGetPoses sample. Both native eyes share it.
        vr::Texture_t t{texture, vr::TextureType_DirectX, vr::ColorSpace_Gamma};
        lastSubmitFlags_[eye] = vr::Submit_Default;
        const auto result = compositor_->Submit(eye ? vr::Eye_Right : vr::Eye_Left, &t);
        if (result == vr::VRCompositorError_None) {
            ++ordinarySubmits_;
            submittedRevision_[eye] = capturedRevision_[eye];
        }
        return result;
    }
    if (renderPoseValid_[eye]) {
        vr::VRTextureWithPose_t t{};
        t.handle = texture;
        t.eType = vr::TextureType_DirectX;
        t.eColorSpace = vr::ColorSpace_Gamma;
        t.mDeviceToAbsoluteTracking = renderPose_[eye];
        // Suppress motion extrapolation across a render-direction change.
        // Use the revision captured WITH this eye, since F7/F8 run after
        // capture and must not relabel an image that was already rendered.
        const bool discontinuity = capturedRevision_[eye] != submittedRevision_[eye];
        const auto flags = static_cast<vr::EVRSubmitFlags>(vr::Submit_TextureWithPose |
            (discontinuity ? vr::Submit_FrameDiscontinuity : 0));
        lastSubmitFlags_[eye] = flags;
        const auto result = compositor_->Submit(eye ? vr::Eye_Right : vr::Eye_Left,
                                                &t, nullptr, flags);
        if (result == vr::VRCompositorError_None) {
            ++explicitSubmits_;
            submittedRevision_[eye] = capturedRevision_[eye];
        }
        return result;
    }
    // Invalid tracking during startup falls back to the ordinary compositor
    // pose association until a valid exact render pose is available.
    vr::Texture_t t{texture, vr::TextureType_DirectX, vr::ColorSpace_Gamma};
    lastSubmitFlags_[eye] = vr::Submit_Default;
    return compositor_->Submit(eye ? vr::Eye_Right : vr::Eye_Left, &t);
}

void VRRuntime::LogFrameTiming() const {
    if(external_){Log("companion: tracking sample=%llu valid=%d queueDrops=%u",poseSample_,poseValid_,Companion().Dropped());return;}
    if (!compositor_) return;
    vr::Compositor_FrameTiming timing{};
    timing.m_nSize = sizeof(timing);
    if (!compositor_->GetFrameTiming(&timing)) return;
    const Mat4 relative = Mul(InvertRigid(origin_), headPose_);
    Log("vr timing: mode=%S interval=%.2fms presents=%u dropped=%u mispresented=%u flags=0x%X headDelta=%.4f/%.4f/%.4fm autoTurn=%d anchorTurns=%llu anchorGap=%.2fdeg",
        PoseModeLabel(), timing.m_flClientFrameIntervalMs, timing.m_nNumFramePresents,
        timing.m_nNumDroppedFrames, timing.m_nNumMisPresented, timing.m_nReprojectionFlags,
        relative.m[0][3], relative.m[1][3], relative.m[2][3], automaticTurning_, anchorTurns_,
        RotationDistanceDegrees(renderAnchor_, headPose_));
    Log("vr submit audit: sample=%llu captured=%llu/%llu flags=0x%X/0x%X ordinary=%llu explicit=%llu",
        poseSample_, capturedStamp_[0].sample, capturedStamp_[1].sample,
        lastSubmitFlags_[0], lastSubmitFlags_[1], ordinarySubmits_, explicitSubmits_);
}
}
