#include "CompanionClient.h"
#include "Log.h"
namespace bvr {
CompanionClient& Companion(){static auto* client=new CompanionClient;return *client;}
bool CompanionClient::Start(const std::wstring& directory, bool test) {
    if(process_)return false;
    const auto name=ipc::UniqueName();
    if(!session_.Open(name,true)){Log("companion: IPC creation failed %lu",GetLastError());return false;}
    const auto executable=directory+L"\\BayonettaVRHost.exe";
    std::wstring command=L"\""+executable+L"\" "+(test?L"--transport-test ":L"--game ")+name+L" "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&startup,&process)) {
        Log("companion: launch failed %lu",GetLastError());session_.Close();return false;
    }
    CloseHandle(process.hThread);process_=process.hProcess;
    const ULONGLONG deadline=GetTickCount64()+20000;
    while(GetTickCount64()<deadline) {
        ipc::Control control{};
        if(WaitForSingleObject(process_,0)==WAIT_OBJECT_0)break;
        if(session_.Read(control)){
            if(control.error)break;
            if(control.ready){lastControl_=control;ready_=true;Log("companion: ready pid=%lu adapter=%d",process.dwProcessId,control.calibration.adapter);return true;}
        }
        Sleep(10);
    }
    Log("companion: initialization failed or timed out; desktop remains available");Stop();return false;
}
bool CompanionClient::Poll(ipc::Control& control) {
    if(!ready_||!process_)return false;
    if(WaitForSingleObject(process_,0)==WAIT_OBJECT_0){ready_=false;Log("companion: host exited; stopped VR capture");return false;}
    ipc::Control current{};
    if(session_.Read(current))lastControl_=current;
    control=lastControl_;
    if(control.error||GetTickCount64()-control.heartbeat>5000){ready_=false;Log("companion: host failure/stall error=%u",control.error);return false;}
    return true;
}
bool CompanionClient::Publish(ID3D11Device* device,ID3D11DeviceContext* context,
                              ID3D11Texture2D* left,ID3D11Texture2D* right,ipc::Frame frame) {
    if(!ready_)return false;
    frame.sequence=++sequence_;frame.tick=GetTickCount64();
    const bool published=publisher_.Publish(*session_.Data(),device,context,left,right,frame);
    if(FAILED(publisher_.Error())){
        ready_=false;Log("companion: producer GPU failure 0x%08lX; stopped VR capture",publisher_.Error());
        if(session_.Lock()){session_.Data()->control.stop=1;session_.Unlock();}
    }
    return published;
}
void CompanionClient::SetGrid(bool enabled){if(session_.Lock()){session_.Data()->control.grid=enabled;session_.Unlock();}}
void CompanionClient::Stop(){
    ready_=false;
    if(session_.Lock(100)){session_.Data()->control.stop=1;session_.Unlock();}
    if(process_){WaitForSingleObject(process_,3000);CloseHandle(process_);process_=nullptr;}
    // A blocked host also observes parent death. Never terminate an unrelated
    // VR process, and never wait/join from DllMain.
    session_.Close();
}
}
