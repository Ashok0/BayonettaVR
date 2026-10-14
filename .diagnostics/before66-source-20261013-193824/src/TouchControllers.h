#pragma once
#include "ControllerInput.h"
#include <openvr.h>
#include <cstdio>
namespace bvr {
class TouchControllers {
public:
    ControllerInput Poll(vr::IVRSystem* system,bool focused,uint64_t now) {
        TouchHand hands[2]{};
        for(int hand=0;hand<2;++hand) {
            const auto id=system->GetTrackedDeviceIndexForControllerRole(hand?vr::TrackedControllerRole_RightHand:vr::TrackedControllerRole_LeftHand);
            if(id!=indices_[hand]||(!supported_[hand]&&id!=vr::k_unTrackedDeviceIndexInvalid&&now-lastCheck_[hand]>=1000)) {
                lastCheck_[hand]=now;
                indices_[hand]=id;supported_[hand]=false;
                if(id!=vr::k_unTrackedDeviceIndexInvalid) {
                    char type[128]{};system->GetStringTrackedDeviceProperty(id,vr::Prop_ControllerType_String,type,sizeof(type));
                    supported_[hand]=std::strcmp(type,"oculus_touch")==0;
                    if(!reported_[hand]||supported_[hand])std::printf("controllers: hand=%d device=%u type=%s supported=%d\n",hand,id,type,supported_[hand]);
                    reported_[hand]=true;
                }
            }
            vr::VRControllerState_t state{};
            if(!supported_[hand]||!system->IsTrackedDeviceConnected(id)||!system->GetControllerState(id,&state,sizeof(state)))continue;
            auto& out=hands[hand];out.connected=true;
            auto down=[&](vr::EVRButtonId button){return (state.ulButtonPressed&vr::ButtonMaskFromId(button))!=0;};
            // SteamVR's installed Oculus legacy profile: A/X -> A, B/Y -> ApplicationMenu.
            out.primary=down(vr::k_EButton_A);out.secondary=down(vr::k_EButton_ApplicationMenu);
            out.stickClick=down(vr::k_EButton_Axis0);out.system=down(vr::k_EButton_System);
            out.x=state.rAxis[0].x;out.y=state.rAxis[0].y;
            out.trigger=state.rAxis[1].x;out.grip=state.rAxis[2].x;
        }
        return mapper_.Map(hands[0],hands[1],focused&&system->IsInputAvailable(),now);
    }
private:
    TouchMapper mapper_;
    vr::TrackedDeviceIndex_t indices_[2]{vr::k_unTrackedDeviceIndexInvalid,vr::k_unTrackedDeviceIndexInvalid};
    bool supported_[2]{};
    bool reported_[2]{};
    uint64_t lastCheck_[2]{};
};
}
