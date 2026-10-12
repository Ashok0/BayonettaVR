#pragma once
#include "Matrix.h"

namespace bvr {
// Preserve heading and position, but never bake head/camera pitch or roll
// into the tracking origin. Looking down while calibrating stays valid.
inline bool LevelPose(const Mat4& pose,Mat4& level) {
    for(const auto& row:pose.m)for(float value:row)if(!std::isfinite(value))return false;
    float x=pose.m[0][2],z=pose.m[2][2];
    float length=std::sqrt(x*x+z*z);
    if(length<0.01f) { // Looking vertically: recover heading from the right axis.
        x=-pose.m[2][0];z=pose.m[0][0];length=std::sqrt(x*x+z*z);
    }
    if(length<0.01f)return false;
    x/=length;z/=length;level=Identity();
    level.m[0][0]=z;level.m[2][0]=-x;level.m[0][2]=x;level.m[2][2]=z;
    for(int i=0;i<3;++i)level.m[i][3]=pose.m[i][3];
    return true;
}

inline bool RecoverGameCamera(const Mat4& projection,const Mat4& viewProjection,Mat4& camera) {
    Mat4 inverse{};if(!Invert(projection,inverse))return false;
    const Mat4 view=Mul(inverse,viewProjection);
    for(const auto& row:view.m)for(float value:row)if(!std::isfinite(value))return false;
    for(int r=0;r<3;++r)for(int s=0;s<3;++s) {
        float dot=0;for(int c=0;c<3;++c)dot+=view.m[r][c]*view.m[s][c];
        if(std::fabs(dot-(r==s?1.0f:0.0f))>0.02f)return false;
    }
    for(int c=0;c<4;++c)if(std::fabs(view.m[3][c]-(c==3?1.0f:0.0f))>0.02f)return false;
    camera=InvertRigid(view);return true;
}

class FloorCalibration {
public:
    bool Calibrate(const Mat4& camera,float groundY,float headHeight,float units) {
        Mat4 level{};
        if(!LevelPose(camera,level) || !std::isfinite(groundY) ||
           !std::isfinite(headHeight) || headHeight<0.2f || headHeight>3.0f ||
           !std::isfinite(units) || units<=0)return false;
        const float offset=groundY+headHeight*units-camera.m[1][3];
        if(!std::isfinite(offset))return false;
        heightOffset_=offset;active_=true;return true;
    }
    Mat4 Correction(const Mat4& camera) const {
        Mat4 level{};if(!active_ || !LevelPose(camera,level))return Identity();
        level.m[1][3]+=heightOffset_;
        return Mul(InvertRigid(level),camera);
    }
    // Reanchoring a menu/HUD without a grounded player must not undo the
    // existing floor relation when the user has crouched or stood up.
    void Reanchor(float heightDelta,float units) {
        if(active_)heightOffset_+=heightDelta*units;
    }
    bool Active() const {return active_;}
    float HeightOffset() const {return heightOffset_;}
private:
    bool active_=false;
    float heightOffset_=0;
};
}
