#pragma once
#include "Matrix.h"
#include <windows.h>
#include <openvr.h>
#include <d3d11.h>

namespace bvr {
class VRRuntime {
public:
    bool Init();
    void Shutdown();
    void WaitForPose();
    void PostPresentHandoff();
    void Recenter();
    void EnsureOrigin();
    void CycleStereoScale();
    void ToggleEyeSwap();
    void CycleProjectionMode();
    bool Active() const { return system_ && compositor_; }
    int Eye() const { return eye_; }
    void SetEye(int eye) { eye_ = eye & 1; }
    int DXGIAdapterIndex() const;
    void AdvanceEye() { eye_ ^= 1; }
    void MarkEyeRendered(int eye);
    Mat4 ClipTransform(float gameAspect) const;
    vr::EVRCompositorError Submit(int eye, ID3D11Texture2D* texture);

private:
    vr::IVRSystem* system_ = nullptr;
    vr::IVRCompositor* compositor_ = nullptr;
    HMODULE dll_ = nullptr;
    Mat4 origin_ = Identity();
    Mat4 headPose_ = Identity();
    vr::HmdMatrix34_t renderPose_[2]{};
    Mat4 eyeToHead_[2] = {Identity(), Identity()};
    float rawProjection_[2][4]{};
    bool originValid_ = false;
    bool poseValid_ = false;
    bool renderPoseValid_[2]{};
    int eye_ = 0;
    float stereoScale_ = 1.0f;
    bool swapEyes_ = false;
    int projectionMode_ = 0;
};

VRRuntime& VR();
}
