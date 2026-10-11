#include "CameraCulling.h"
#include "Log.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace bvr {
namespace {
using CheckFn=int(__thiscall*)(void*,const float*,float);
unsigned char* target=nullptr;
unsigned char* trampoline=nullptr;
unsigned char* mainFrustum=nullptr;
unsigned char** activeCamera=nullptr;
CheckFn original=nullptr;
bool (*isEnabled)()=nullptr;
bool loggedRescue=false;
// cCameraFrustum::VfrustumCk, Steam VA AAA720. Its only relative
// instruction is after the overwritten six-byte movss instruction.
constexpr unsigned char signature[]={
    0xF3,0x0F,0x10,0x44,0x24,0x08,0x8B,0x44,0x24,0x04,0x51,
    0xF3,0x0F,0x11,0x04,0x24,0x50,0x83,0xC1,0x10,
    0xE8,0x07,0x08,0xA7,0xFF,0x0F,0xB6,0xC0,0xC2,0x08,0x00};
int __fastcall Check(void* self,void*,const float* position,float radius) {
    const int visible=original(self,position,radius);
    if(visible || !isEnabled())return visible;
    const auto camera=*activeCamera;
    if(self!=mainFrustum && (!camera || self!=camera+0x5F0))return visible;
    const auto& f=*reinterpret_cast<const SceneFrustum*>(static_cast<unsigned char*>(self)+0x10);
    if(!SurroundVisible(f,position,radius))return visible;
    if(!loggedRescue) {
        loggedRescue=true;
        Log("camera culling: retained off-camera geometry; frustum=%p far=%.2f radius=%.2f",self,f.farZ,radius);
    }
    return 1;
}
}

bool InstallCameraCullingHook(unsigned char* image,bool (*enabled)()) {
    if(target)return true;
    if(!image || !enabled)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt->FileHeader.TimeDateStamp!=0x591AD330 || nt->OptionalHeader.SizeOfImage<0x572905C) {
        Log("camera culling: unsupported executable; original visibility retained");return false;
    }
    auto* entry=image+0x6AA720;
    // Also verify the active camera getter before relying on manager+8.
    constexpr unsigned char getter[]={0x8B,0x41,0x08,0xC3};
    if(memcmp(entry,signature,sizeof(signature)) || memcmp(image+0x6AA2C0,getter,sizeof(getter))) {
        Log("camera culling: signature mismatch; original visibility retained");return false;
    }
    auto* relay=static_cast<unsigned char*>(VirtualAlloc(nullptr,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!relay)return false;
    memcpy(relay,entry,6);
    relay[6]=0xE9;
    *reinterpret_cast<int32_t*>(relay+7)=static_cast<int32_t>((entry+6)-(relay+11));
    FlushInstructionCache(GetCurrentProcess(),relay,11);
    DWORD protection=0;
    if(!VirtualProtect(entry,6,PAGE_EXECUTE_READWRITE,&protection)) {
        VirtualFree(relay,0,MEM_RELEASE);return false;
    }
    trampoline=relay;original=reinterpret_cast<CheckFn>(relay);
    mainFrustum=image+0xB2E0D0;
    activeCamera=reinterpret_cast<unsigned char**>(image+0x5729058);
    isEnabled=enabled;loggedRescue=false;target=entry;
    unsigned char patch[6]={0xE9,0,0,0,0,0x90};
    *reinterpret_cast<int32_t*>(patch+1)=static_cast<int32_t>(
        reinterpret_cast<unsigned char*>(&Check)-(entry+5));
    memcpy(entry,patch,6);
    DWORD ignored=0;VirtualProtect(entry,6,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),entry,6);
    Log("camera culling: 360-degree scene visibility installed at %p; shadow check unchanged",entry);
    return true;
}

void RemoveCameraCullingHook() {
    if(!target)return;
    DWORD protection=0;
    if(!VirtualProtect(target,6,PAGE_EXECUTE_READWRITE,&protection)) {
        Log("camera culling: could not restore hook; retaining trampoline");return;
    }
    memcpy(target,signature,6);
    DWORD ignored=0;VirtualProtect(target,6,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),target,6);
    VirtualFree(trampoline,0,MEM_RELEASE);
    target=nullptr;trampoline=nullptr;original=nullptr;isEnabled=nullptr;
    mainFrustum=nullptr;activeCamera=nullptr;
}
}
