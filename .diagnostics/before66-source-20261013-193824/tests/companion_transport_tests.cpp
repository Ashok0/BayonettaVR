#include "CompanionClient.h"
#include <cstdio>
#include <vector>
#include <cstdarg>
using namespace bvr;
using Microsoft::WRL::ComPtr;
// Keep the integration test's log out of the installed game's log.
namespace bvr { void Log(const char* format,...) {va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");} }
bool WaitCount(CompanionClient& client,uint64_t minimum) {
    const ULONGLONG deadline=GetTickCount64()+4000;
    while(GetTickCount64()<deadline){ipc::Control c{};if(client.Poll(c)&&c.received>=minimum)return c.testFailures==0;Sleep(5);}return false;
}
int wmain(int argc,wchar_t** argv){
    if(argc>1&&!wcscmp(argv[1],L"--temporary-parent")){Sleep(2500);return 0;}
    wchar_t cwd[MAX_PATH]{};GetCurrentDirectoryW(MAX_PATH,cwd);
    const std::wstring directory=std::wstring(cwd)+L"\\build\\Win32\\Release";
    if(argc>1&&!wcscmp(argv[1],L"--runtime-smoke")){
        CompanionClient live;if(!live.Start(directory,false))return 30;
        ipc::Control control{};const ULONGLONG end=GetTickCount64()+8000;
        bool alive=true;while(GetTickCount64()<end){if(!live.Poll(control)){alive=false;break;}Sleep(20);}
        const bool passed=alive&&control.tracking.valid&&control.submitted>=10;
        printf("runtime smoke: alive=%d validTracking=%u submittedPairs=%llu\n",alive,control.tracking.valid,control.submitted);
        live.Stop();return passed?0:31;
    }
    CompanionClient client;if(!client.Start(directory,true))return 1;
    ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapters1(0,&adapter))||
       FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
           nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 2;
    ComPtr<ID3D11Texture2D> eyes[2];unsigned width=0,published=0;uint64_t attempt=0;
    auto send=[&](unsigned size,bool corrupt=false){
        ++attempt;
        if(width!=size){
            for(auto& eye:eyes){eye.Reset();D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=size;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
                d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
                if(FAILED(device->CreateTexture2D(&d,nullptr,&eye)))return false;}
            width=size;
        }
        for(int e=0;e<2;++e){
            std::vector<unsigned char> bytes(size*size*4);
            for(size_t p=0;p<bytes.size();p+=4){bytes[p]=static_cast<unsigned char>(attempt%251);bytes[p+1]=(e?191:63);bytes[p+2]=127;bytes[p+3]=255;}
            context->UpdateSubresource(eyes[e].Get(),0,nullptr,bytes.data(),size*4,0);
        }
        ipc::Frame frame{};frame.sample=attempt*7;frame.pose[0].m[0][3]=float(attempt);frame.pose[1].m[0][3]=-float(attempt);
        frame.revision[0]=frame.revision[1]=attempt/20;
        if(corrupt)frame.pose[1].m[0][3]=0;
        bool ok=client.Publish(device.Get(),context.Get(),eyes[0].Get(),eyes[1].Get(),frame);if(ok)++published;return ok;
    };
    if(!send(64)||!WaitCount(client,1))return 3;
    // Actual cross-process readback after repeated size changes, slot reuse,
    // frame drops and independent producer/consumer pacing.
    for(int i=0;i<160;++i){
        if(i==80){
            for(auto& eye:eyes)eye.Reset();width=0;
            context->ClearState();context.Reset();device.Reset();
            if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 19;
        }
        send((i/10)%2?96:64);Sleep(i%3);
    }
    if(!WaitCount(client,10))return 4;
    ipc::Control before{};if(!client.Poll(before)||before.testFailures)return 5;
    // Ask test host to stop consuming while continuing its heartbeat. Fill the
    // bounded queue and prove a producer drops instead of waiting/overwriting.
    if(!client.SessionForTest().Lock(100))return 6;
    client.SessionForTest().Data()->control.grid=2;client.SessionForTest().Unlock();Sleep(50);
    int accepted=0;for(int i=0;i<8;++i)if(send(64))++accepted;
    if(accepted>3||client.Dropped()==0)return 7;
    if(!client.SessionForTest().Lock(100))return 8;
    client.SessionForTest().Data()->control.grid=0;client.SessionForTest().Unlock();
    if(!WaitCount(client,before.received+1))return 9;
    Sleep(50);ipc::Control after{};if(!client.Poll(after)||after.testFailures)return 10;
    printf("transport: published=%u consumed=%llu dropped=%u; pixel/eye/pose/resize/device-recreation checks passed\n",published,after.received,client.Dropped());
    // Deliberately corrupt one pose: verify the receiver can detect a mismatch.
    if(!send(64,true))return 11;
    const ULONGLONG mismatchDeadline=GetTickCount64()+4000;
    do { Sleep(5);client.Poll(after); } while(after.testFailures==0&&GetTickCount64()<mismatchDeadline);
    if(after.testFailures!=1)return 12;
    puts("transport: deliberate pose mismatch detected");
    client.Stop();if(client.Ready())return 13;
    // Recovery uses a new mapping; old slot generations cannot leak into it.
    CompanionClient restarted;if(!restarted.Start(directory,true))return 14;
    restarted.Stop();puts("transport: host shutdown and fresh-session restart passed");
    // Parent-death cleanup: give a host a short-lived, test-owned parent.
    ipc::Session orphanSession;const auto name=ipc::UniqueName();if(!orphanSession.Open(name,true))return 15;
    wchar_t self[MAX_PATH]{};GetModuleFileNameW(nullptr,self,MAX_PATH);
    std::wstring childCmd=L"\""+std::wstring(self)+L"\" --temporary-parent";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION child{};
    if(!CreateProcessW(self,childCmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&child))return 16;
    CloseHandle(child.hThread);
    const auto hostPath=directory+L"\\BayonettaVRHost.exe";
    std::wstring hostCmd=L"\""+hostPath+L"\" --transport-test "+name+L" "+std::to_wstring(child.dwProcessId);
    PROCESS_INFORMATION host{};
    if(!CreateProcessW(hostPath.c_str(),hostCmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&host))return 17;
    CloseHandle(host.hThread);
    const DWORD childWait=WaitForSingleObject(child.hProcess,6000);
    const DWORD hostWait=WaitForSingleObject(host.hProcess,4000);
    DWORD hostExit=99;GetExitCodeProcess(host.hProcess,&hostExit);
    CloseHandle(child.hProcess);CloseHandle(host.hProcess);
    if(childWait!=WAIT_OBJECT_0||hostWait!=WAIT_OBJECT_0||hostExit!=0)return 18;
    puts("transport: host exits automatically when its parent dies; all checks passed");return 0;
}
