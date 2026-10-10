#pragma once
#include "ControllerInput.h"
namespace bvr {
bool InstallControllerHooks();
void UpdateControllerInput(const ControllerInput& input);
}
