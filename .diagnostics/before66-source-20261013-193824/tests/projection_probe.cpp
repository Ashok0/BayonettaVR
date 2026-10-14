#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <openvr.h>
#include "Matrix.h"
#include <cstdio>

int main() {
    HMODULE dll = LoadLibraryW(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Bayonetta\\openvr_api.dll");
    if (!dll) return 1;
    auto init = reinterpret_cast<uint32_t (VR_CALLTYPE*)(vr::EVRInitError*, vr::EVRApplicationType)>(GetProcAddress(dll, "VR_InitInternal"));
    auto shutdown = reinterpret_cast<void (VR_CALLTYPE*)()>(GetProcAddress(dll, "VR_ShutdownInternal"));
    auto iface = reinterpret_cast<void* (VR_CALLTYPE*)(const char*, vr::EVRInitError*)>(GetProcAddress(dll, "VR_GetGenericInterface"));
    if (!init || !shutdown || !iface) return 2;
    vr::EVRInitError error{};
    init(&error, vr::VRApplication_Background);
    if (error) { std::printf("background init error %d\n", int(error)); return 3; }
    auto* system = static_cast<vr::IVRSystem*>(iface(vr::IVRSystem_Version, &error));
    if (!system) { shutdown(); return 4; }
    for (int e=0; e<2; ++e) {
        float l,r,t,b;
        system->GetProjectionRaw(static_cast<vr::EVREye>(e), &l,&r,&t,&b);
        auto p = system->GetProjectionMatrix(static_cast<vr::EVREye>(e), 0.1f, 100000.0f);
        auto ours = bvr::PerspectiveRawLH(l,r,t,b,0.1f,100000.0f);
        std::printf("eye=%d raw=%.7f %.7f %.7f %.7f\n",e,l,r,t,b);
        for (int row=0;row<4;++row) {
            std::printf("runtime RH[%d]= %.7f %.7f %.7f %.7f\n",row,p.m[row][0],p.m[row][1],p.m[row][2],p.m[row][3]);
            std::printf("ours -> RH[%d]= %.7f %.7f %.7f %.7f\n",row,ours.m[row][0],ours.m[row][1],-ours.m[row][2],ours.m[row][3]);
        }
    }
    shutdown();
    FreeLibrary(dll);
    return 0;
}
