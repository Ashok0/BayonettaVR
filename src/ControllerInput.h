#pragma once
#include <windows.h>
#include <Xinput.h>
#include <cstdint>
#include <cmath>
#include <cstring>

namespace bvr {
// Shared timing policy for real gamepads and Touch. Interruptions cannot
// finish a previous hold; a held chord fires once until released.
class RecenterHold {
public:
    bool Update(bool both,bool active,uint64_t now) {
        if(!active || !both || (holding_&&(now<last_ || now-last_>250))) {
            holding_=fired_=false;
        }
        last_=now;
        if(!active || !both)return false;
        if(!holding_){holding_=true;start_=now;}
        if(!fired_&&now-start_>=1000){fired_=true;return true;}
        return false;
    }
private:
    uint64_t start_=0,last_=0;
    bool holding_=false,fired_=false;
};
// R3 release toggles; either ordering of L3+R3 reserves the recenter chord.
class CameraTogglePress {
public:
    bool Update(bool right,bool left,bool active,uint64_t now) {
        const bool interrupted=!active || (seen_&&(now<last_ || now-last_>250));
        last_=now;seen_=true;
        if(interrupted){armed_=false;blocked_=right;}
        if(!active)return false;
        if(left){armed_=false;blocked_=right;}
        if(right) {
            if(!down_&&!blocked_&&!left)armed_=true;
            down_=true;return false;
        }
        const bool fire=down_&&armed_&&!left&&!interrupted;
        down_=armed_=blocked_=false;return fire;
    }
private:
    uint64_t last_=0;
    bool seen_=false,down_=false,armed_=false,blocked_=false;
};
struct TouchHand {
    bool connected=false, primary=false, secondary=false, stickClick=false, system=false;
    float x=0,y=0,trigger=0,grip=0;
};
struct ControllerInput {
    uint64_t tick=0,recenterSequence=0,cameraToggleSequence=0;
    uint32_t connected=0,active=0;
    XINPUT_GAMEPAD pad{};
};
class TouchMapper {
public:
    ControllerInput Map(const TouchHand& left,const TouchHand& right,bool focused,uint64_t now) {
        ControllerInput output{};output.tick=now;output.recenterSequence=recenterSequence_;
        output.cameraToggleSequence=cameraToggleSequence_;
        output.connected=left.connected||right.connected;
        output.active=output.connected&&focused;
        if(!output.active) {hold_.Update(false,false,now);toggle_.Update(right.stickClick,left.stickClick,false,now);systemDown_=false;return output;}
        const bool both=left.connected&&right.connected&&left.stickClick&&right.stickClick;
        const bool system=right.connected&&right.system;
        if(system&&!systemDown_)++recenterSequence_;
        systemDown_=system;
        if(hold_.Update(both,true,now))++recenterSequence_;
        if(toggle_.Update(right.stickClick,left.stickClick||right.grip>.55f,
            right.connected,now))++cameraToggleSequence_;
        output.recenterSequence=recenterSequence_;
        output.cameraToggleSequence=cameraToggleSequence_;
        auto& p=output.pad;
        if(left.connected) {
            Stick(left.x,left.y,p.sThumbLX,p.sThumbLY);
            p.bLeftTrigger=Trigger(left.trigger);
            if(left.primary)p.wButtons|=XINPUT_GAMEPAD_X;
            if(left.secondary)p.wButtons|=XINPUT_GAMEPAD_Y;
            if(left.grip>.55f)p.wButtons|=XINPUT_GAMEPAD_LEFT_SHOULDER;
            if(left.stickClick&&!both) {
                if(left.grip>.55f){p.wButtons&=~XINPUT_GAMEPAD_LEFT_SHOULDER;p.wButtons|=XINPUT_GAMEPAD_START;}
                else p.wButtons|=XINPUT_GAMEPAD_LEFT_THUMB;
            }
        }
        if(right.connected) {
            Stick(right.x,right.y,p.sThumbRX,p.sThumbRY);
            p.bRightTrigger=Trigger(right.trigger);
            if(right.primary)p.wButtons|=XINPUT_GAMEPAD_A;
            if(right.secondary)p.wButtons|=XINPUT_GAMEPAD_B;
            if(right.grip>.55f)p.wButtons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if(right.stickClick&&!both) {
                if(right.grip>.55f){p.wButtons&=~XINPUT_GAMEPAD_RIGHT_SHOULDER;p.wButtons|=XINPUT_GAMEPAD_BACK;}
                else p.wButtons|=XINPUT_GAMEPAD_RIGHT_THUMB;
            }
        }
        // Both grips form a deliberate D-pad modifier for the right stick.
        if(left.connected&&right.connected&&left.grip>.8f&&right.grip>.8f&&!both&&
           (std::fabs(right.x)>.5f||std::fabs(right.y)>.5f)) {
            p.wButtons&=~(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER);
            p.sThumbRX=p.sThumbRY=0;
            if(right.x>.5f)p.wButtons|=XINPUT_GAMEPAD_DPAD_RIGHT;
            if(right.x<-.5f)p.wButtons|=XINPUT_GAMEPAD_DPAD_LEFT;
            if(right.y>.5f)p.wButtons|=XINPUT_GAMEPAD_DPAD_UP;
            if(right.y<-.5f)p.wButtons|=XINPUT_GAMEPAD_DPAD_DOWN;
        }
        return output;
    }
private:
    static float Finite(float value){return std::isfinite(value)?value:0;}
    static BYTE Trigger(float value){value=Finite(value);return BYTE(value<=0?0:value>=1?255:value*255+.5f);}
    static void Stick(float x,float y,SHORT& outX,SHORT& outY) {
        x=Finite(x);y=Finite(y);x=x<-1?-1:x>1?1:x;y=y<-1?-1:y>1?1:y;
        if(x*x+y*y<.12f*.12f){outX=outY=0;return;}
        outX=SHORT(x*(x<0?32768:32767));outY=SHORT(y*(y<0?32768:32767));
    }
    uint64_t recenterSequence_=0;
    RecenterHold hold_;
    CameraTogglePress toggle_;
    uint64_t cameraToggleSequence_=0;
    bool systemDown_=false;
};
// Called under the input hook's lock; monotonic packets describe the merged pad.
class XboxInputBridge {
public:
    void Update(const ControllerInput& input){input_=input;}
    bool Available(uint64_t now) const {return input_.connected&&now>=input_.tick&&now-input_.tick<=250;}
    bool Read(DWORD index,XINPUT_STATE& state,bool physical,uint64_t now) {
        if(index!=0||!Available(now))return false;
        XINPUT_GAMEPAD result=physical?state.Gamepad:XINPUT_GAMEPAD{};
        if(input_.active) {
            const auto& v=input_.pad;result.wButtons|=v.wButtons;
            if(v.bLeftTrigger>result.bLeftTrigger)result.bLeftTrigger=v.bLeftTrigger;
            if(v.bRightTrigger>result.bRightTrigger)result.bRightTrigger=v.bRightTrigger;
            if(v.sThumbLX||v.sThumbLY){result.sThumbLX=v.sThumbLX;result.sThumbLY=v.sThumbLY;}
            if(v.sThumbRX||v.sThumbRY){result.sThumbRX=v.sThumbRX;result.sThumbRY=v.sThumbRY;}
        }
        if(!seen_||std::memcmp(&result,&previous_,sizeof(result))){++packet_;previous_=result;seen_=true;}
        state.dwPacketNumber=packet_;state.Gamepad=result;return true;
    }
private:
    ControllerInput input_{};
    XINPUT_GAMEPAD previous_{};
    DWORD packet_=0;
    bool seen_=false;
};
}
