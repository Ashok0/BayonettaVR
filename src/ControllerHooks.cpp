#include "ControllerHooks.h"
#include "Log.h"
#include <cstring>
namespace bvr {
namespace {
using GetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
using SetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
GetStateFn originalGet=nullptr;
SetStateFn originalSet=nullptr;
SRWLOCK inputLock=SRWLOCK_INIT;
XboxInputBridge bridge;
RecenterHold gamepadHold;
CameraTogglePress gamepadToggle;
bool calibrationActive=false;
bool cameraShortcutActive=false;
constexpr WORD stickChord=XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_RIGHT_THUMB;
DWORD WINAPI GetState(DWORD index,XINPUT_STATE* state) {
    if(!state)return ERROR_BAD_ARGUMENTS;
    const DWORD result=originalGet(index,state);
    if(result!=ERROR_SUCCESS)*state={};
    AcquireSRWLockExclusive(&inputLock);
    if(index==0 && calibrationActive && (state->Gamepad.wButtons&stickChord)==stickChord)
        state->Gamepad.wButtons&=~stickChord;
    const bool supplied=bridge.Read(index,*state,result==ERROR_SUCCESS,GetTickCount64());
    if(index==0&&cameraShortcutActive)state->Gamepad.wButtons&=~XINPUT_GAMEPAD_RIGHT_THUMB;
    ReleaseSRWLockExclusive(&inputLock);
    return supplied?ERROR_SUCCESS:result;
}
DWORD WINAPI SetState(DWORD index,XINPUT_VIBRATION* vibration) {
    if(!vibration)return ERROR_BAD_ARGUMENTS;
    const DWORD result=originalSet(index,vibration);
    // Physical-pad rumble remains intact. Touch rumble is not emulated yet.
    AcquireSRWLockExclusive(&inputLock);
    const bool supplied=index==0&&bridge.Available(GetTickCount64());
    ReleaseSRWLockExclusive(&inputLock);
    return supplied?ERROR_SUCCESS:result;
}
template<class T> bool Patch(ULONG_PTR* slot,T hook,T& original) {
    if(*slot==reinterpret_cast<ULONG_PTR>(hook))return true;
    DWORD protection=0;if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection))return false;
    original=reinterpret_cast<T>(*slot);
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),reinterpret_cast<void*>(hook));
    DWORD ignored=0;VirtualProtect(slot,sizeof(*slot),protection,&ignored);return true;
}
}
void UpdateControllerInput(const ControllerInput& input) {
    AcquireSRWLockExclusive(&inputLock);bridge.Update(input);ReleaseSRWLockExclusive(&inputLock);
}
bool PollGamepadRecenter(bool active,uint64_t now,bool* cameraToggle,bool cameraShortcut) {
    // Call the unhooked export, so the consumed game buttons and virtual pad
    // cannot hide a physical chord or produce duplicate Touch events.
    XINPUT_STATE state{};
    const bool connected=originalGet && originalGet(0,&state)==ERROR_SUCCESS;
    AcquireSRWLockExclusive(&inputLock);
    calibrationActive=active;
    cameraShortcutActive=active&&cameraShortcut;
    const bool toggled=gamepadToggle.Update(connected&&(state.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB),
        connected&&(state.Gamepad.wButtons&XINPUT_GAMEPAD_LEFT_THUMB),active&&connected&&cameraShortcut,now);
    if(cameraToggle)*cameraToggle=toggled;
    const bool fired=gamepadHold.Update(connected&&(state.Gamepad.wButtons&stickChord)==stickChord,
        active&&connected,now);
    ReleaseSRWLockExclusive(&inputLock);return fired;
}
bool InstallControllerHooks() {
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress)return false;
    bool get=false,set=false;
    for(auto* entry=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);entry->Name;++entry) {
        if(_stricmp(reinterpret_cast<const char*>(base+entry->Name),"XINPUT1_3.dll")||!entry->OriginalFirstThunk)continue;
        auto* names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+entry->OriginalFirstThunk);
        auto* slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+entry->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            const bool ordinal=IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)!=0;
            const auto number=ordinal?IMAGE_ORDINAL(names->u1.Ordinal):0;
            const char* name=ordinal?"":reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData)->Name;
            if(number==2||!strcmp(name,"XInputGetState"))get=Patch(&slots->u1.Function,&GetState,originalGet);
            if(number==3||!strcmp(name,"XInputSetState"))set=Patch(&slots->u1.Function,&SetState,originalSet);
        }
    }
    Log("controllers: XInput 1.3 import hooks get=%d set=%d; virtual Xbox slot=0",get,set);
    return get;
}
}
