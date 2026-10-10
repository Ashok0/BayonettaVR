#pragma once
#include "Matrix.h"
#include "PoseSubmission.h"
#include <windows.h>
#include <openvr.h>
#include <d3d11.h>

namespace bvr {
class VRRuntime {
public:
    bool Init();
    bool StartIndependentGrid();
    void Shutdown();
    void WaitForPose();
    void PostPresentHandoff();
    void Recenter();
    void EnsureOrigin();
    void CycleStereoScale();
    void ToggleEyeSwap();
    void CycleProjectionMode();
    void ToggleCompositorPoseDiagnostic();
    void ToggleAutomaticTurning();
    void LogPoseDiagnostic() const;
    void LogFrameTiming() const;
    bool Active() const;
    bool External() const { return external_; }
    bool SubmitPair(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*, ID3D11Texture2D*);
    void SetExternalGrid(bool enabled);
    bool CompositorPoseDiagnostic() const { return poseMode_ == 0; }
    bool NativeFullTracking() const { return poseMode_ == 2; }
    const wchar_t* PoseModeLabel() const;
    int Eye() const { return eye_; }
    void SetEye(int eye) { eye_ = eye & 1; }
    int DXGIAdapterIndex() const;
    void AdvanceEye() { eye_ ^= 1; }
    void MarkEyeRendered(int eye);
    bool ReferencePoseValid(int eye) const { return eye >= 0 && eye < 2 && renderPoseValid_[eye]; }
    Mat4 CapturedReferenceClip(int eye) const { return referenceClip_[eye & 1]; }
    Mat4 ClipTransform(float gameAspect) const;
    Mat4 HudClipTransform(float gameAspect) const;
    vr::EVRCompositorError Submit(int eye, ID3D11Texture2D* texture);

private:
    Mat4 EyeViewProjection(int eye, float units, const Mat4& input = Identity()) const;
    Mat4 RenderHeadForEye(int eye) const;
    int TrackingMode() const;
    bool external_ = false;
    int externalAdapter_ = -1;
    vr::IVRSystem* system_ = nullptr;
    vr::IVRCompositor* compositor_ = nullptr;
    HMODULE dll_ = nullptr;
    Mat4 origin_ = Identity();
    Mat4 renderAnchor_ = Identity();
    Mat4 headPose_ = Identity();
    vr::HmdMatrix34_t renderPose_[2]{};
    Mat4 referenceClip_[2]{};
    CapturedPoseStamp capturedStamp_[2]{};
    std::uint64_t poseSample_ = 0;
    unsigned long long ordinarySubmits_ = 0;
    unsigned long long explicitSubmits_ = 0;
    unsigned lastSubmitFlags_[2]{};
    Mat4 eyeToHead_[2] = {Identity(), Identity()};
    float rawProjection_[2][4]{};
    bool originValid_ = false;
    bool poseValid_ = false;
    bool renderPoseValid_[2]{};
    unsigned long long renderRevision_ = 1;
    unsigned long long capturedRevision_[2]{};
    unsigned long long submittedRevision_[2]{};
    unsigned long long anchorTurns_ = 0;
    bool automaticTurning_ = false;
    int eye_ = 0;
    float stereoScale_ = 1.0f;
    bool swapEyes_ = false;
    int projectionMode_ = 0;
    // 0=fixed baseline, 1=native rotation, 2=native 6DOF,
    // 3=tracked optical centers with compositor rotation.
    int poseMode_ = 3;
};

VRRuntime& VR();
}
