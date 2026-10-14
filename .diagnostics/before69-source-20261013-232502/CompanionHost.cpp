#include "CompanionProtocol.h"
#include "SharedFrameTransport.h"
#include "ReferenceGridRenderer.h"
#include "Matrix.h"
#include "TouchControllers.h"
#include <cstdio>
#include <cmath>

using namespace bvr;
using Microsoft::WRL::ComPtr;
namespace {
struct Runtime {
    HMODULE dll=nullptr;
    void (VR_CALLTYPE* shutdown)()=nullptr;
    bool initialized=false;
    ~Runtime(){if(initialized&&shutdown)shutdown();if(dll)FreeLibrary(dll);}
    bool Init(const std::wstring& directory,vr::IVRSystem*& system,vr::IVRCompositor*& compositor){
        dll=LoadLibraryW((directory+L"\\openvr_api.dll").c_str());if(!dll)return false;
        auto init=reinterpret_cast<uint32_t(VR_CALLTYPE*)(vr::EVRInitError*,vr::EVRApplicationType)>(GetProcAddress(dll,"VR_InitInternal"));
        auto iface=reinterpret_cast<void*(VR_CALLTYPE*)(const char*,vr::EVRInitError*)>(GetProcAddress(dll,"VR_GetGenericInterface"));
        shutdown=reinterpret_cast<void(VR_CALLTYPE*)()>(GetProcAddress(dll,"VR_ShutdownInternal"));
        if(!init||!iface||!shutdown)return false;
        vr::EVRInitError error{};init(&error,vr::VRApplication_Scene);if(error){printf("OpenVR init error=%d\n",int(error));return false;}
        initialized=true;
        system=static_cast<vr::IVRSystem*>(iface(vr::IVRSystem_Version,&error));
        compositor=static_cast<vr::IVRCompositor*>(iface(vr::IVRCompositor_Version,&error));
        if(compositor)compositor->SetTrackingSpace(vr::TrackingUniverseStanding);
        return system&&compositor;
    }
};
Mat4 Matrix(const vr::HmdMatrix34_t& pose){Mat4 m=Identity();for(int r=0;r<3;++r)for(int c=0;c<4;++c)m.m[r][c]=pose.m[r][c];return m;}
bool VerifyPixels(ID3D11Device* device,ID3D11DeviceContext* context,SharedFrameConsumer& consumer){
    const auto& frame=consumer.Frame();
    if(frame.sample!=frame.sequence*7||frame.pose[0].m[0][3]!=float(frame.sequence)||frame.pose[1].m[0][3]!=-float(frame.sequence))return false;
    for(int e=0;e<2;++e){
        D3D11_TEXTURE2D_DESC desc{};consumer.Eye(e)->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;if(FAILED(device->CreateTexture2D(&desc,nullptr,&readback)))return false;
        context->CopyResource(readback.Get(),consumer.Eye(e));
        D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
        bool valid=true;
        for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x){
            auto pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
            if(pixel[0]!=static_cast<unsigned char>(frame.sequence%251)||pixel[1]!=(e?191:63)||pixel[2]!=127||pixel[3]!=255)valid=false;
        }
        context->Unmap(readback.Get(),0);if(!valid)return false;
    }
    return true;
}
int Run(ipc::Session& session,HANDLE parent,DWORD parentId,const std::wstring& directory,bool test){
    ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> gridTextures[2][4];ComPtr<ID3D11RenderTargetView> gridTargets[2][4];ReferenceGridRenderer grid;
    SharedFrameConsumer consumer;
    TouchControllers controllers;
    const bool controllerInput=!test&&GetPrivateProfileIntW(L"VR",L"VRControllers",1,(directory+L"\\BayonettaVR.ini").c_str())!=0;
    printf("controllers: build=20261013.68 enabled=%d; hold both sticks 1s to recenter heading; standing tracking space\n",controllerInput);
    // Shutdown OpenVR before releasing any textures it may still reference.
    Runtime runtime;vr::IVRSystem* system=nullptr;vr::IVRCompositor* compositor=nullptr;
    if(!test&&!runtime.Init(directory,system,compositor))return 1;
    ipc::Calibration calibration{};calibration.adapter=0;calibration.hz=120;
    Mat4 projections[2]{},eyes[2]{};
    if(!test){
        system->GetDXGIOutputInfo(&calibration.adapter);
        calibration.hz=system->GetFloatTrackedDeviceProperty(0,vr::Prop_DisplayFrequency_Float);
        for(int e=0;e<2;++e){
            calibration.eyes[e]=system->GetEyeToHeadTransform(static_cast<vr::EVREye>(e));
            auto& q=calibration.tangents[e];system->GetProjectionRaw(static_cast<vr::EVREye>(e),&q[0],&q[1],&q[2],&q[3]);
            const auto p=system->GetProjectionMatrix(static_cast<vr::EVREye>(e),0.1f,1000.0f);
            for(int r=0;r<4;++r)for(int c=0;c<4;++c)projections[e].m[r][c]=p.m[r][c];
            eyes[e]=Matrix(calibration.eyes[e]);
        }
    }
    if(calibration.adapter<0||FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||
       FAILED(factory->EnumAdapters1(UINT(calibration.adapter),&adapter))||
       FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 2;
    if(!test){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=1024;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        for(int e=0;e<2;++e)for(int s=0;s<4;++s)
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&gridTextures[e][s]))||
               FAILED(device->CreateRenderTargetView(gridTextures[e][s].Get(),nullptr,&gridTargets[e][s])))return 3;
        if(FAILED(grid.Draw(device.Get(),context.Get(),gridTextures[0][0].Get(),Identity())))return 4;
    }
    if(!session.Lock(1000))return 5;
    session.Data()->control.calibration=calibration;session.Data()->control.ready=1;
    session.Data()->control.heartbeat=GetTickCount64();session.Unlock();
    printf("host ready parent alive, adapter=%d test=%d\n",calibration.adapter,test);
    uint64_t sample=0,pairs=0,received=0,failures=0,lastRevision[2]{},noFocus=0;
    Mat4 origin=Identity(),flip=Identity();flip.m[2][2]=-1;bool originValid=false,wasFallback=true;
    ULONGLONG report=GetTickCount64();ipc::Control control{};
    while(WaitForSingleObject(parent,0)==WAIT_TIMEOUT){
        if(session.Read(control)&&control.stop)break;
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
        bool trackingValid=true;
        if(!test){
            const auto error=compositor->WaitGetPoses(poses,vr::k_unMaxTrackedDeviceCount,nullptr,0);
            trackingValid=error==vr::VRCompositorError_None&&poses[0].bPoseIsValid;
        }
        ++sample;
        ControllerInput input{};
        if(controllerInput) {
            DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            input=controllers.Poll(system,trackingValid&&foreground==parentId,GetTickCount64());
        }
        if(session.Lock()){
            auto& c=session.Data()->control;c.heartbeat=GetTickCount64();c.tracking.sample=sample;
            c.tracking.tick=c.heartbeat;c.tracking.valid=trackingValid;c.tracking.head=poses[0].mDeviceToAbsoluteTracking;
            c.received=received;c.submitted=pairs;c.testFailures=failures;
            c.controllers=input;
            session.Unlock();
        }
        if(!(test&&control.grid==2)&&consumer.Consume(*session.Data(),device.Get(),context.Get())){
            ++received;
            if(test&&!VerifyPixels(device.Get(),context.Get(),consumer)){++failures;printf("PAIR/POSE/PIXEL MISMATCH sequence=%llu\n",consumer.Frame().sequence);}
            if(session.Lock()){session.Data()->control.received=received;session.Data()->control.testFailures=failures;session.Unlock();}
        }
        if(FAILED(consumer.Error())){printf("transport error=0x%08lX\n",consumer.Error());return 6;}
        if(test){Sleep(5);continue;}
        if(!trackingValid){Sleep(10);continue;}
        const Mat4 head=Matrix(poses[0].mDeviceToAbsoluteTracking);
        if(!originValid){origin=head;originValid=true;}
        const bool showGrid=control.grid!=0;
        const bool useFallback=showGrid||!consumer.HasFrame();
        const unsigned slot=unsigned(pairs%4);
        if(useFallback){
            // Submit black until the first game frame; the grid is opt-in via F5.
            const float black[4]{0,0,0,1};
            const float gridBackground[4]{0.004f,0.008f,0.045f,1};
            for(int e=0;e<2;++e){
                context->ClearRenderTargetView(gridTargets[e][slot].Get(),showGrid?gridBackground:black);
                if(showGrid){
                    auto clip=Mul(projections[e],Mul(InvertRigid(eyes[e]),Mul(InvertRigid(head),Mul(origin,flip))));
                    if(FAILED(grid.Draw(device.Get(),context.Get(),gridTextures[e][slot].Get(),clip)))return 7;
                }
            }
            if(FAILED(FinishGpu(device.Get(),context.Get())))return 8;
        }
        bool submitted=true;
        for(int e=0;e<2;++e){
            vr::EVRCompositorError result;
            if(useFallback){
                vr::Texture_t texture{gridTextures[e][slot].Get(),vr::TextureType_DirectX,vr::ColorSpace_Gamma};
                result=compositor->Submit(static_cast<vr::EVREye>(e),&texture);
            }else{
                vr::VRTextureWithPose_t texture{};texture.handle=consumer.Eye(e);texture.eType=vr::TextureType_DirectX;
                texture.eColorSpace=vr::ColorSpace_Gamma;texture.mDeviceToAbsoluteTracking=consumer.Frame().pose[e];
                const bool discontinuity=wasFallback||lastRevision[e]!=consumer.Frame().revision[e];
                const auto flags=static_cast<vr::EVRSubmitFlags>(vr::Submit_TextureWithPose|(discontinuity?vr::Submit_FrameDiscontinuity:0));
                result=compositor->Submit(static_cast<vr::EVREye>(e),&texture,nullptr,flags);
                if(result==vr::VRCompositorError_None)lastRevision[e]=consumer.Frame().revision[e];
            }
            if(result==vr::VRCompositorError_DoNotHaveFocus){submitted=false;++noFocus;continue;}
            if(result!=vr::VRCompositorError_None){printf("submit eye=%d error=%d\n",e,int(result));return 9;}
        }
        wasFallback=useFallback;compositor->PostPresentHandoff();if(submitted)++pairs;
        if(GetTickCount64()-report>=2000){
            printf("host pairs=%llu received=%llu sequence=%llu sample=%llu imageAge=%llums grid=%d noFocus=%llu\n",pairs,received,
                consumer.Frame().sequence,consumer.Frame().sample,consumer.HasFrame()?GetTickCount64()-consumer.Frame().tick:0,showGrid,noFocus);
            report=GetTickCount64();
        }
    }
    context->ClearState();context->Flush();printf("host stopped received=%llu failures=%llu\n",received,failures);return failures?10:0;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=4||(wcscmp(argv[1],L"--game")&&wcscmp(argv[1],L"--transport-test")))return 20;
    const bool test=wcscmp(argv[1],L"--transport-test")==0;
    const DWORD parentId=wcstoul(argv[3],nullptr,10);if(!parentId||parentId==GetCurrentProcessId())return 21;
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,parentId);if(!parent)return 22;
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);std::wstring directory=path;
    directory.resize(directory.find_last_of(L"\\/"));
    FILE* output=nullptr;_wfreopen_s(&output,(directory+L"\\BayonettaVRHost-"+std::to_wstring(parentId)+L".log").c_str(),L"w",stdout);
    setvbuf(stdout,nullptr,_IONBF,0);
    ipc::Session session;if(!session.Open(argv[2],false)){CloseHandle(parent);return 23;}
    int result=0;try{result=Run(session,parent,parentId,directory,test);}catch(...){result=24;}
    if(session.Lock(100)){session.Data()->control.error=result;session.Data()->control.ready=0;session.Unlock();}
    CloseHandle(parent);return result;
}
