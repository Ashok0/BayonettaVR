#pragma once
#include "Matrix.h"
#include <cstdint>
#include "FirstPersonCostumes.h"
namespace bvr {
void UpdateFirstPersonTracking(const Mat4& relativeGameHead,bool valid);
void UpdateFirstPersonTurn(float horizontal,uint64_t now);
bool InstallFirstPerson(unsigned char* image,bool (*enabled)());
void RemoveFirstPerson();
bool FirstPersonAvailable();
void ToggleFirstPerson();
// Commit the previously queued model and head mask together for both eyes.
void BeginFirstPersonFrame();
bool GetFirstPersonCorrection(const Mat4& inverseProjection,Mat4& correction);
// Immutable draw ranges from the same player queue committed for both eyes.
bool FirstPersonCostumeDrawsActive();
const FirstPersonBodyRule* GetFirstPersonCostumeDraw(void* indexBuffer,unsigned type,unsigned start,unsigned primitives);
}
