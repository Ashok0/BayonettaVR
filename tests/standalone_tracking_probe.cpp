#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <openvr.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include "Matrix.h"
#include "ReferenceGridRenderer.h"
#include <cstdio>

using Microsoft::WRL::ComPtr;
using namespace bvr;

namespace {
using InitFn = uint32_t (VR_CALLTYPE*)(vr::EVRInitError*, vr::EVRApplicationType);
using ShutdownFn = void (VR_CALLTYPE*)();
using InterfaceFn = void* (VR_CALLTYPE*)(const char*, vr::EVRInitError*);
struct Runtime {
    HMODULE dll = nullptr;
    ShutdownFn shutdown = nullptr;
    bool initialized = false;
    ~Runtime() { Stop(); if (dll) FreeLibrary(dll); }
    void Stop() { if (initialized && shutdown) shutdown(); initialized = false; }
};
Mat4 Matrix(const vr::HmdMatrix34_t& pose) {
    Mat4 m = Identity();
    for (int r=0; r<3; ++r) for (int c=0; c<4; ++c) m.m[r][c] = pose.m[r][c];
    return m;
}
double Seconds() {
    static const double frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    return double(now.QuadPart)/frequency;
}
void PaceUntil(double deadline) {
    for (;;) {
        const double remaining = deadline-Seconds();
        if (remaining <= 0) return;
        if (remaining > 0.002) Sleep(static_cast<DWORD>((remaining-0.001)*1000));
        else SwitchToThread();
    }
}
bool GameClosed() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool closed = true;
    if (!Process32FirstW(snapshot, &entry)) closed = false;
    else do {
        if (_wcsicmp(entry.szExeFile, L"Bayonetta.exe") == 0) { closed = false; break; }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return closed;
}
}

int main() {
    // This is a separate, short-lived Scene app. Never run alongside the game:
    // it would take VR scene focus. It does not install or change any settings.
    std::puts("Standalone tracking probe: grid only, no Bayonetta hooks or D3D9 bridge.\n"
              "24 seconds: dark BLUE = display-rate target (8s), dark GREEN = half-rate target (8s), BLUE again (8s).\n"
              "Use small head turns. Escape exits early. No runtime settings are changed.");
    if (!GameClosed()) { std::puts("Close Bayonetta before running the standalone probe."); return 16; }
    Runtime runtime;
    runtime.dll = LoadLibraryW(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Bayonetta\\openvr_api.dll");
    if (!runtime.dll) { std::puts("OpenVR DLL unavailable"); return 1; }
    auto init = reinterpret_cast<InitFn>(GetProcAddress(runtime.dll, "VR_InitInternal"));
    auto iface = reinterpret_cast<InterfaceFn>(GetProcAddress(runtime.dll, "VR_GetGenericInterface"));
    runtime.shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(runtime.dll, "VR_ShutdownInternal"));
    if (!init || !iface || !runtime.shutdown) return 2;
    vr::EVRInitError error{};
    init(&error, vr::VRApplication_Scene);
    if (error) { std::printf("OpenVR init failed: %d\n", int(error)); return 3; }
    runtime.initialized = true;
    auto* system = static_cast<vr::IVRSystem*>(iface(vr::IVRSystem_Version, &error));
    auto* compositor = static_cast<vr::IVRCompositor*>(iface(vr::IVRCompositor_Version, &error));
    if (!system || !compositor) return 4;
    const float hz = system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                           vr::Prop_DisplayFrequency_Float);
    if (!(hz >= 30 && hz <= 240)) { std::printf("Invalid display frequency: %f\n", hz); return 5; }
    std::printf("Display %.1f Hz; targets %.1f / %.1f / %.1f fps\n", hz,hz,hz*0.5f,hz);
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    int32_t adapterIndex = -1;
    system->GetDXGIOutputInfo(&adapterIndex);
    if (adapterIndex < 0 || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapters1(static_cast<UINT>(adapterIndex), &adapter))) return 6;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 7;
    constexpr unsigned ringSize = 4;
    ComPtr<ID3D11Texture2D> textures[2][ringSize];
    ComPtr<ID3D11RenderTargetView> targets[2][ringSize];
    Mat4 projections[2]{}, eyeTransforms[2]{};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 1024;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    for (int e=0; e<2; ++e) {
        const auto p = system->GetProjectionMatrix(static_cast<vr::EVREye>(e), 0.1f, 1000.0f);
        for (int r=0; r<4; ++r) for (int c=0; c<4; ++c) projections[e].m[r][c] = p.m[r][c];
        eyeTransforms[e] = Matrix(system->GetEyeToHeadTransform(static_cast<vr::EVREye>(e)));
        for (unsigned slot=0; slot<ringSize; ++slot)
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &textures[e][slot])) ||
                FAILED(device->CreateRenderTargetView(textures[e][slot].Get(), nullptr, &targets[e][slot]))) return 8;
    }
    ComPtr<ID3D11Query> fence;
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
    if (FAILED(device->CreateQuery(&query, &fence))) return 9;
    ReferenceGridRenderer grid;
    // Warm up shader compilation before the timed test.
    for (int e=0; e<2; ++e)
        if (FAILED(grid.Draw(device.Get(), context.Get(), textures[e][0].Get(), Identity()))) return 10;

    Mat4 origin = Identity(), flip = Identity();
    flip.m[2][2] = -1; // Only maps the grid's +Z vertices to OpenVR -Z world space.
    bool originValid = false;
    int lastPhase = -1, result = 0;
    unsigned frame = 0, frames[3]{}, failedSubmits = 0;
    double start = 0, phaseStart = 0, previousFrame = 0;
    double worstGap[3]{};
    const double trackingDeadline = Seconds()+15;
    while (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
        const double frameStart = Seconds();
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
        const auto wait = compositor->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
        if (wait != vr::VRCompositorError_None || !poses[0].bPoseIsValid) {
            if (Seconds()>trackingDeadline || originValid) {
                std::printf("Tracking/focus unavailable: wait=%d valid=%d\n",int(wait),poses[0].bPoseIsValid);
                result = 11; break;
            }
            continue;
        }
        const Mat4 head = Matrix(poses[0].mDeviceToAbsoluteTracking);
        if (!originValid) { origin = head; originValid = true; start = Seconds(); }
        const double now = Seconds();
        const int phase = static_cast<int>((now-start)/8);
        if (phase >= 3) break;
        if (phase != lastPhase) {
            if (lastPhase >= 0) std::printf("Phase %d: measured %.1f fps, worst pose-to-pose interval %.2fms\n",
                lastPhase+1, frames[lastPhase]/(now-phaseStart), worstGap[lastPhase]*1000);
            phaseStart = now;
            previousFrame = 0;
            lastPhase = phase;
            std::printf("Phase %d: %s background, target %.1f fps\n", phase+1,
                phase==1 ? "GREEN" : "BLUE", phase==1 ? hz*0.5f : hz);
            fflush(stdout);
        }
        if (previousFrame && now-previousFrame > worstGap[phase]) worstGap[phase] = now-previousFrame;
        previousFrame = now;
        const unsigned slot = frame%ringSize;
        const float background[4]{0.004f, phase==1 ? 0.035f : 0.008f, phase==1 ? 0.008f : 0.045f, 1};
        for (int e=0; e<2; ++e) {
            // Direct OpenVR RH projection and eye/head view, independent of
            // VRRuntime::EyeViewProjection, inverse-game projection, and units.
            const Mat4 clip = Mul(projections[e], Mul(InvertRigid(eyeTransforms[e]),
                Mul(InvertRigid(head), Mul(origin, flip))));
            context->ClearRenderTargetView(targets[e][slot].Get(), background);
            if (FAILED(grid.Draw(device.Get(), context.Get(), textures[e][slot].Get(), clip))) { result=12; break; }
        }
        if (result) break;
        context->End(fence.Get()); context->Flush();
        HRESULT ready;
        while ((ready = context->GetData(fence.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE)
            SwitchToThread();
        if (FAILED(ready)) { result=13; break; }
        for (int e=0; e<2; ++e) {
            vr::Texture_t texture{textures[e][slot].Get(), vr::TextureType_DirectX, vr::ColorSpace_Gamma};
            const auto submitted = compositor->Submit(static_cast<vr::EVREye>(e), &texture);
            if (submitted != vr::VRCompositorError_None) {
                ++failedSubmits;
                if (failedSubmits < 5) std::printf("Submit eye %d failed: %d\n",e,int(submitted));
            }
        }
        compositor->PostPresentHandoff();
        ++frames[phase]; ++frame;
        // Deliberately limit only the green phase. Full-rate phases are paced
        // by WaitGetPoses, not an independent game/desktop presentation loop.
        if (phase == 1) PaceUntil(frameStart+2.0/hz);
    }
    if (lastPhase >= 0) std::printf("Phase %d: measured %.1f fps, worst pose-to-pose interval %.2fms\n",
        lastPhase+1, frames[lastPhase]/(Seconds()-phaseStart), worstGap[lastPhase]*1000);
    std::printf("Finished: submitted pairs=%u failures=%u status=%d. Settings unchanged.\n",frame,failedSubmits,result);
    context->ClearState(); context->Flush();
    runtime.Stop();
    return result ? result : failedSubmits ? 14 : originValid ? 0 : 15;
}
