#pragma once
#include "Matrix.h"
namespace bvr {
void UpdateFirstPersonTracking(const Mat4& relativeGameHead,bool valid);
bool InstallFirstPerson(unsigned char* image,bool (*enabled)());
void RemoveFirstPerson();
bool FirstPersonAvailable();
void ToggleFirstPerson();
// Commit the previously queued model and head mask together for both eyes.
void BeginFirstPersonFrame();
bool GetFirstPersonCorrection(const Mat4& inverseProjection,Mat4& correction);
}
