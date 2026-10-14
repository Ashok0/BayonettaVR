#pragma once
#include <cstddef>
#include <cmath>

namespace bvr {
// Steam cViewFrustum, verified against check/setFov and the reference PDB.
struct SceneFrustum {
    float position[4];
    float aspect, cosX, sinX, cosY, sinY, nearZ, farZ;
    float padding;
    float direction[4], normals[4][4], corners[8][4];
};
static_assert(sizeof(SceneFrustum)==256 && offsetof(SceneFrustum,farZ)==0x28);

// A sphere enclosing the original expanded frustum in EVERY orientation.
// Keep a finite range, including its far corners and object bounding radius.
// The original check is still used first, so this can only add visibility.
inline bool SurroundVisible(const SceneFrustum& f,const float* position,float radius) {
    if(!position || !std::isfinite(radius) || radius<0 ||
       !std::isfinite(f.farZ) || f.farZ<=0 ||
       !std::isfinite(f.cosX) || !std::isfinite(f.cosY) ||
       !std::isfinite(f.sinX) || !std::isfinite(f.sinY) ||
       f.cosX<0.001f || f.cosX>1 || f.cosY<0.001f || f.cosY>1 ||
       f.sinX<0 || f.sinX>1 || f.sinY<0 || f.sinY>1)return false;
    const double z=double(f.farZ)+radius;
    const double x=(z*f.sinX+radius)/f.cosX;
    const double y=(z*f.sinY+radius)/f.cosY;
    double distanceSquared=0;
    for(int i=0;i<3;++i) {
        const double delta=double(position[i])-f.position[i];
        if(!std::isfinite(delta))return false;
        distanceSquared+=delta*delta;
    }
    return distanceSquared<=x*x+y*y+z*z;
}

bool InstallCameraCullingHook(unsigned char* image,bool (*enabled)());
void RemoveCameraCullingHook();
}
