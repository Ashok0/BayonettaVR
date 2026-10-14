#include "ControllerHooks.h"
#include <cstdio>
#include <cstdarg>
using namespace bvr;
namespace bvr { void Log(const char* format,...) {va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");} }
int main() {
    XINPUT_STATE untouched{};const DWORD before=XInputGetState(3,&untouched);
    if(!InstallControllerHooks()||!InstallControllerHooks())return 1;
    ControllerInput input{};input.connected=input.active=1;input.tick=GetTickCount64();
    input.pad.wButtons=XINPUT_GAMEPAD_A;input.pad.sThumbLX=23456;
    UpdateControllerInput(input);
    XINPUT_STATE state{};
    if(XInputGetState(0,&state)!=ERROR_SUCCESS||!(state.Gamepad.wButtons&XINPUT_GAMEPAD_A)||state.Gamepad.sThumbLX!=23456)return 2;
    const DWORD packet=state.dwPacketNumber;
    if(XInputGetState(0,&state)!=ERROR_SUCCESS||state.dwPacketNumber!=packet)return 3;
    XINPUT_VIBRATION vibration{};if(XInputSetState(0,&vibration)!=ERROR_SUCCESS)return 4;
    if(XInputGetState(0,&state)!=ERROR_SUCCESS||state.dwPacketNumber!=packet)return 5;
    if(XInputGetState(3,&untouched)!=before)return 6;
    if(XInputGetState(0,nullptr)!=ERROR_BAD_ARGUMENTS)return 7;
    input.tick=GetTickCount64();input.pad.wButtons=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_RIGHT_THUMB;
    input.pad.sThumbLX=12001;input.pad.sThumbLY=-21002;input.pad.sThumbRX=-8001;input.pad.sThumbRY=3002;
    UpdateControllerInput(input);bool toggle=false;
    float turnX=0;
    PollGamepadRecenter(true,input.tick,&toggle,true,&turnX);
    if(std::fabs(turnX-(-8001.f/32768.f))>.0001f)return 11;
    if(XInputGetState(0,&state)!=ERROR_SUCCESS||(state.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB)||
       !(state.Gamepad.wButtons&XINPUT_GAMEPAD_A)||state.Gamepad.sThumbLX!=12001||state.Gamepad.sThumbLY!=-21002||
       state.Gamepad.sThumbRX!=-8001||state.Gamepad.sThumbRY!=3002)return 9;
    PollGamepadRecenter(false,GetTickCount64(),nullptr,false,&turnX);
    if(turnX!=0)return 12;
    if(XInputGetState(0,&state)!=ERROR_SUCCESS||!(state.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB))return 10;
    input.pad.sThumbRX=32767;input.tick=GetTickCount64();UpdateControllerInput(input);
    PollGamepadRecenter(true,input.tick,nullptr,true,&turnX);if(turnX!=1.f)return 13;
    UpdateControllerInput({});
    // Verify disconnect follows the original DLL's real result.
    auto real=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(GetModuleHandleW(L"xinput1_3.dll"),"XInputGetState"));
    if(!real||XInputGetState(0,&state)!=real(0,&untouched))return 8;
    puts("controller hooks: real ordinal-import interception, duplicate install, virtual slot, rumble acknowledgement, packet stability and passthrough passed");
}
