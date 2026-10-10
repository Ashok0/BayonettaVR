#include "IndependentGrid.h"
#include "Matrix.h"
#include "ReferenceGridRenderer.h"
#include "Log.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>

namespace bvr {
using Microsoft::WRL::ComPtr;
namespace {
double Seconds() {
    static const double frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return double(now.QuadPart)/frequency;
}
Mat4 Matrix(const vr::HmdMatrix34_t& pose) {
    Mat4 m = Identity();
    for (int r=0; r<3; ++r) for (int c=0; c<4; ++c) m.m[r][c] = pose.m[r][c];
    return m;
}
}
IndependentGrid& IndependentVRGrid() {
    // Process lifetime; no thread join or D3D teardown under the loader lock.
    static auto* grid = new IndependentGrid;
    return *grid;
}
bool IndependentGrid::Start(vr::IVRSystem* system, vr::IVRCompositor* compositor) {
    if (OwnsFrames() || !system || !compositor) return false;
    system_ = system; compositor_ = compositor; recenter_.store(false); gameParked_.store(false);
    return worker_.Start(Run, this);
}
void IndependentGrid::Run(void* self, const std::atomic<bool>& stop) {
    try { static_cast<IndependentGrid*>(self)->Render(stop); }
    catch (...) { Log("independent grid: exception; returning ownership to game"); }
    Log("independent grid: worker exited");
}
void IndependentGrid::Render(const std::atomic<bool>& stop) {
    // Direct RH matrices, format, size, ring and ordinary submission match the
    // successful standalone probe. No game-frame pose, context or pacing.
    int32_t adapterIndex = -1;
    system_->GetDXGIOutputInfo(&adapterIndex);
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (adapterIndex < 0 || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapters1(static_cast<UINT>(adapterIndex), &adapter)) ||
        FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &device, nullptr, &context))) {
        Log("independent grid: device initialization failed"); return;
    }
    constexpr unsigned ringSize = 4;
    ComPtr<ID3D11Texture2D> textures[2][ringSize];
    ComPtr<ID3D11RenderTargetView> targets[2][ringSize];
    Mat4 projections[2]{}, eyes[2]{};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 1024;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    for (int e=0; e<2; ++e) {
        const auto p = system_->GetProjectionMatrix(static_cast<vr::EVREye>(e), 0.1f, 1000.0f);
        for (int r=0; r<4; ++r) for (int c=0; c<4; ++c) projections[e].m[r][c] = p.m[r][c];
        eyes[e] = Matrix(system_->GetEyeToHeadTransform(static_cast<vr::EVREye>(e)));
        for (unsigned slot=0; slot<ringSize; ++slot)
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &textures[e][slot])) ||
                FAILED(device->CreateRenderTargetView(textures[e][slot].Get(), nullptr, &targets[e][slot]))) {
                Log("independent grid: texture initialization failed"); return;
            }
    }
    ComPtr<ID3D11Query> fence;
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
    if (FAILED(device->CreateQuery(&query, &fence))) { Log("independent grid: fence initialization failed"); return; }
    ReferenceGridRenderer grid;
    for (int e=0; e<2; ++e)
        if (FAILED(grid.Draw(device.Get(), context.Get(), textures[e][0].Get(), Identity()))) {
            Log("independent grid: shader initialization failed"); return;
        }
    Log("independent grid: RUNNING thread=%lu adapter=%d; F3 returns, F8 recenters, hold F2 parks game for up to 4 seconds", GetCurrentThreadId(), adapterIndex);
    Mat4 origin = Identity(), flip = Identity(); flip.m[2][2] = -1;
    bool originValid = false;
    unsigned frame = 0, reportFrames = 0, failures = 0;
    double reportStart = Seconds(), previousPose = 0, worstGap = 0, maxAge = 0;
    bool lastParked = false;
    const double trackingDeadline = Seconds()+15;
    while (!stop.load()) {
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
        const auto wait = compositor_->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
        const double poseTime = Seconds();
        const bool parked = gameParked_.load();
        if (parked != lastParked) {
            Log("independent grid: phase=%s", parked ? "GAME-PARKED GREEN" : "GAME-RUNNING PURPLE");
            lastParked = parked;
            reportStart = poseTime; reportFrames = 0; previousPose = 0;
            worstGap = maxAge = 0;
        }
        if (wait != vr::VRCompositorError_None || !poses[0].bPoseIsValid) {
            if (poseTime > trackingDeadline || originValid) {
                Log("independent grid: tracking/focus lost wait=%d valid=%d", int(wait), poses[0].bPoseIsValid); break;
            }
            Sleep(10); continue;
        }
        const Mat4 head = Matrix(poses[0].mDeviceToAbsoluteTracking);
        if (!originValid || recenter_.exchange(false)) { origin = head; originValid = true; }
        if (previousPose) worstGap = std::max(worstGap, poseTime-previousPose);
        previousPose = poseTime;
        const unsigned slot = frame%ringSize;
        // Bright green makes the load-off comparison unmistakable.
        const float background[4]{parked ? 0.0f : 0.035f, parked ? 0.18f : 0.004f, parked ? 0.0f : 0.035f, 1};
        for (int e=0; e<2; ++e) {
            const Mat4 clip = Mul(projections[e], Mul(InvertRigid(eyes[e]),
                Mul(InvertRigid(head), Mul(origin, flip))));
            context->ClearRenderTargetView(targets[e][slot].Get(), background);
            if (FAILED(grid.Draw(device.Get(), context.Get(), textures[e][slot].Get(), clip))) {
                Log("independent grid: draw failed"); return;
            }
        }
        context->End(fence.Get()); context->Flush();
        HRESULT ready;
        const double fenceDeadline = Seconds()+2;
        while ((ready = context->GetData(fence.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
            if (Seconds() > fenceDeadline) { Log("independent grid: GPU fence timeout"); return; }
            SwitchToThread();
        }
        if (FAILED(ready)) { Log("independent grid: GPU fence failed 0x%08lX", ready); break; }
        maxAge = std::max(maxAge, Seconds()-poseTime);
        for (int e=0; e<2; ++e) {
            vr::Texture_t texture{textures[e][slot].Get(), vr::TextureType_DirectX, vr::ColorSpace_Gamma};
            const auto submitted = compositor_->Submit(static_cast<vr::EVREye>(e), &texture);
            if (submitted != vr::VRCompositorError_None) {
                ++failures; Log("independent grid: submit eye=%d error=%d", e, int(submitted));
            }
        }
        compositor_->PostPresentHandoff();
        ++frame; ++reportFrames;
        const double now = Seconds();
        if (now-reportStart >= 2) {
            vr::Compositor_FrameTiming timing{}; timing.m_nSize = sizeof(timing);
            const bool timingValid = compositor_->GetFrameTiming(&timing);
            Log("independent grid: parked=%d pairs=%u rate=%.1fHz worstPoseGap=%.2fms maxPoseToSubmit=%.2fms failures=%u timingValid=%d frameIndex=%u predictionExtra=%u throttle=%u presents=%u mispresented=%u flags=0x%X",
                parked, frame, reportFrames/(now-reportStart), worstGap*1000, maxAge*1000, failures,
                timingValid, timing.m_nFrameIndex, VR_COMPOSITOR_ADDITIONAL_PREDICTED_FRAMES(timing),
                VR_COMPOSITOR_NUMBER_OF_THROTTLED_FRAMES(timing), timing.m_nNumFramePresents,
                timing.m_nNumMisPresented, timing.m_nReprojectionFlags);
            reportStart = now; reportFrames = 0; worstGap = maxAge = 0;
        }
        if (failures) break;
    }
    context->ClearState(); context->Flush();
    Log("independent grid: finished pairs=%u failures=%u", frame, failures);
}
}
