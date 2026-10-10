#include "VR.h"
#include "Config.h"
#include "Log.h"
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
}

VRRuntime& VR() { return g_vr; }

bool VRRuntime::Init() {
    if (!Cfg().enabled) return false;
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

void VRRuntime::Shutdown() {
    if (system_ && g_shutdown) g_shutdown();
    system_ = nullptr;
    compositor_ = nullptr;
    if (dll_) { FreeLibrary(dll_); dll_ = nullptr; }
}

void VRRuntime::Recenter() {
    origin_ = headPose_;
    originValid_ = poseValid_;
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

void VRRuntime::WaitForPose() {
    if (!compositor_) return;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    const vr::EVRCompositorError e = compositor_->WaitGetPoses(
        poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    if (e != vr::VRCompositorError_None) { Log("vr: WaitGetPoses error %d", static_cast<int>(e)); return; }
    const auto& h = poses[vr::k_unTrackedDeviceIndex_Hmd];
    poseValid_ = h.bPoseIsValid;
    if (!poseValid_) return;
    headPose_ = FromVR(h.mDeviceToAbsoluteTracking);
}

void VRRuntime::PostPresentHandoff() {
    if (compositor_) compositor_->PostPresentHandoff();
}

Mat4 VRRuntime::ClipTransform(float gameAspect) const {
    if (!poseValid_ || !originValid_) return Identity();
    int e = swapEyes_ ? (eye_ ^ 1) : eye_;
    // Relative eye pose: eye -> current head -> tracking -> initial head space.
    // Diagnostic mode leaves the scene at the recentered render pose. SteamVR
    // then owns all late head rotation for this texture, which cleanly tells us
    // whether duplicated motion originates before or after compositor submit.
    Mat4 relativeHead = Cfg().compositorPoseDiagnostic
        ? Identity() : Mul(InvertRigid(origin_), headPose_);
    if (!Cfg().positionalTracking) {
        relativeHead.m[0][3] = relativeHead.m[1][3] = relativeHead.m[2][3] = 0.0f;
    }
    Mat4 scaledEye = eyeToHead_[e];
    scaledEye.m[0][3] *= stereoScale_;
    scaledEye.m[1][3] *= stereoScale_;
    scaledEye.m[2][3] *= stereoScale_;
    Mat4 eyePose = Mul(relativeHead, scaledEye);
    Mat4 eyeView = ToGameCoordinates(InvertRigid(eyePose), Cfg().worldUnitsPerMetre);

    const float pi = 3.14159265358979323846f;
    Mat4 gameProjection = PerspectiveFovLH(Cfg().gameVerticalFovDegrees * pi / 180.0f,
                                           gameAspect, Cfg().nearClip, Cfg().farClip);
    Mat4 invGame{};
    if (!Invert(gameProjection, invGame)) return Identity();
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
    // Existing clip = Pgame * modelView * vertex. Replace the camera/projection
    // portion without needing Bayonetta's model or view matrices separately.
    return Mul(eyeProjection, Mul(eyeView, invGame));
}

int VRRuntime::DXGIAdapterIndex() const {
    int32_t index = -1;
    if (system_) system_->GetDXGIOutputInfo(&index);
    return static_cast<int>(index);
}

void VRRuntime::MarkEyeRendered(int eye) {
    if (eye < 0 || eye > 1) return;
    const Mat4& pose = Cfg().compositorPoseDiagnostic ? origin_ : headPose_;
    renderPose_[eye] = ToVR34(pose);
    renderPoseValid_[eye] = poseValid_ && (!Cfg().compositorPoseDiagnostic || originValid_);
}

vr::EVRCompositorError VRRuntime::Submit(int eye, ID3D11Texture2D* texture) {
    if (!compositor_ || !texture) return vr::VRCompositorError_RequestFailed;
    if (Cfg().compositorPoseDiagnostic && renderPoseValid_[eye]) {
        vr::VRTextureWithPose_t t{};
        t.handle = texture;
        t.eType = vr::TextureType_DirectX;
        t.eColorSpace = vr::ColorSpace_Gamma;
        t.mDeviceToAbsoluteTracking = renderPose_[eye];
        return compositor_->Submit(eye ? vr::Eye_Right : vr::Eye_Left, &t,
                                   nullptr, vr::Submit_TextureWithPose);
    }
    // Both native eyes use the pose returned by the immediately preceding
    // WaitGetPoses, which is OpenVR's standard render path. Supplying the same
    // pose again as a custom TextureWithPose can interfere with compositor
    // frame timing and motion smoothing on some runtimes.
    vr::Texture_t t{texture, vr::TextureType_DirectX, vr::ColorSpace_Gamma};
    return compositor_->Submit(eye ? vr::Eye_Right : vr::Eye_Left, &t);
}
}
