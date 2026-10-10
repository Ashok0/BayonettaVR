#include "ControllerInput.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bvr;
static int checks=0;
static void Require(bool value,const char* message){++checks;if(!value){printf("controller test failed: %s\n",message);exit(1);}}
int main() {
    TouchMapper mapper;TouchHand l{},r{};
    Require(!mapper.Map(l,r,true,0).connected,"no phantom controller");
    l.connected=r.connected=true;l.primary=l.secondary=r.primary=r.secondary=true;
    l.x=-1;l.y=1;r.x=1;r.y=-1;l.trigger=.5f;r.trigger=1;l.grip=r.grip=.6f;
    auto input=mapper.Map(l,r,true,10);auto p=input.pad;
    Require(p.wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER),"face buttons and bumpers");
    Require(p.sThumbLX==-32768&&p.sThumbLY==32767&&p.sThumbRX==32767&&p.sThumbRY==-32768,"stick hand/sign/full range");
    Require(p.bLeftTrigger==128&&p.bRightTrigger==255,"analog triggers");
    l.primary=l.secondary=r.primary=r.secondary=false;l.grip=r.grip=0;
    l.x=.02f;l.y=-.03f;r.x=std::numeric_limits<float>::quiet_NaN();r.y=0;
    l.trigger=-1;r.trigger=3;
    p=mapper.Map(l,r,true,20).pad;
    Require(!p.sThumbLX&&!p.sThumbLY&&!p.sThumbRX&&!p.sThumbRY,"dead zone and invalid axes");
    Require(!p.bLeftTrigger&&p.bRightTrigger==255,"trigger clamping");
    l.stickClick=true;l.grip=.7f;
    Require(mapper.Map(l,r,true,30).pad.wButtons==XINPUT_GAMEPAD_START,"left grip + click is Start only");
    l.stickClick=false;l.grip=0;r.stickClick=true;r.grip=.7f;
    Require(mapper.Map(l,r,true,40).pad.wButtons==XINPUT_GAMEPAD_BACK,"right grip + click is Back only");
    r.grip=0;Require(mapper.Map(l,r,true,50).pad.wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"ordinary stick click preserved");
    r.stickClick=false;l.grip=r.grip=1;r.x=-1;r.y=1;
    p=mapper.Map(l,r,true,60).pad;
    Require(p.wButtons==(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_UP)&&!p.sThumbRX&&!p.sThumbRY,"D-pad modifier consumes camera and bumpers");
    l.grip=r.grip=0;l.stickClick=r.stickClick=true;
    Require(mapper.Map(l,r,true,100).recenterSequence==0,"hold starts without recenter");
    Require(mapper.Map(l,r,true,1099).recenterSequence==0,"one-second threshold");
    input=mapper.Map(l,r,true,1100);
    Require(input.recenterSequence==1&&!(input.pad.wButtons&(XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_RIGHT_THUMB)),"one recenter consumes stick clicks");
    Require(mapper.Map(l,r,true,5000).recenterSequence==1,"long hold does not repeat");
    l.stickClick=r.stickClick=false;mapper.Map(l,r,true,5010);r.system=true;
    Require(mapper.Map(l,r,true,5020).recenterSequence==2,"right System if runtime exposes it");
    Require(mapper.Map(l,r,true,5030).recenterSequence==2,"System edge only");
    r.system=false;l.stickClick=r.stickClick=true;mapper.Map(l,r,true,5100);
    auto unfocused=mapper.Map(l,r,false,6100);
    Require(!unfocused.active&&unfocused.connected&&!unfocused.pad.wButtons&&unfocused.recenterSequence==2,"focus loss releases buttons/cancels hold");
    Require(mapper.Map(l,r,true,7000).recenterSequence==2,"focus return cannot complete old hold");
    Require(mapper.Map(l,r,true,8000).recenterSequence==3,"new full hold after focus returns");
    r.connected=false;input=mapper.Map(l,r,true,8100);
    Require(input.connected&&!input.pad.sThumbRX&&!input.pad.bRightTrigger,"one hand disconnect clears that hand");

    XboxInputBridge bridge;XINPUT_STATE state{};
    Require(!bridge.Read(0,state,false,10),"bridge defaults disconnected");
    input={};input.connected=input.active=1;input.tick=100;input.pad.wButtons=XINPUT_GAMEPAD_A;
    bridge.Update(input);Require(bridge.Read(0,state,false,100)&&state.Gamepad.wButtons==XINPUT_GAMEPAD_A,"virtual pad with no physical gamepad");
    const DWORD packet=state.dwPacketNumber;bridge.Read(0,state,false,101);
    Require(state.dwPacketNumber==packet,"unchanged input stable packet");
    state.Gamepad={};state.Gamepad.wButtons=XINPUT_GAMEPAD_B;state.Gamepad.sThumbLX=1234;state.Gamepad.bRightTrigger=200;
    bridge.Read(0,state,true,101);
    Require(state.Gamepad.wButtons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)&&state.Gamepad.sThumbLX==1234&&state.Gamepad.bRightTrigger==200,"physical gamepad merges without loss");
    Require(state.dwPacketNumber>packet,"merged changes advance packet");
    Require(!bridge.Read(1,state,false,100)&&!bridge.Read(3,state,false,100),"only player one supplied");
    Require(!bridge.Read(0,state,false,351)&&!bridge.Read(0,state,false,99),"stale and invalid timestamps fail closed");
    input.active=0;bridge.Update(input);state={};Require(bridge.Read(0,state,false,110)&&!state.Gamepad.wButtons,"dashboard supplies neutral releases");
    input.connected=0;bridge.Update(input);Require(!bridge.Read(0,state,false,110),"disconnect returns to physical path");
    printf("controller input: %d mapping, merge, focus, stale, disconnect and recenter checks passed\n",checks);
}
