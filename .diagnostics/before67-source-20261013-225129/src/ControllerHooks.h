#pragma once
#include "ControllerInput.h"
namespace bvr {
bool InstallControllerHooks();
void UpdateControllerInput(const ControllerInput& input);
bool PollGamepadRecenter(bool active,uint64_t now,bool* cameraToggle=nullptr,bool cameraShortcut=false);
}
