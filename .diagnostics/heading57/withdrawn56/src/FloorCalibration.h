#pragma once
#include "Matrix.h"

namespace bvr {
inline float BasisDeterminant(const Mat4& a) {
    return a.m[0][0]*(a.m[1][1]*a.m[2][2]-a.m[1][2]*a.m[2][1])
          -a.m[0][1]*(a.m[1][0]*a.m[2][2]-a.m[1][2]*a.m[2][0])
          +a.m[0][2]*(a.m[1][0]*a.m[2][1]-a.m[1][1]*a.m[2][0]);
}
inline bool IsOrthogonalPose(const Mat4& pose) {
    for(const auto& row:pose.m)for(float value:row)if(!std::isfinite(value))return false;
    for(int r=0;r<3;++r)for(int s=0;s<3;++s) {
        float dot=0;for(int c=0;c<3;++c)dot+=pose.m[r][c]*pose.m[s][c];
        if(std::fabs(dot-(r==s?1.0f:0.0f))>0.02f)return false;
    }
    for(int c=0;c<4;++c)if(std::fabs(pose.m[3][c]-(c==3?1.0f:0.0f))>0.02f)return false;
    return std::fabs(std::fabs(BasisDeterminant(pose))-1)<0.03f;
}
// Preserve heading and position, but never bake head/camera pitch or roll
// into the tracking origin. Looking down while calibrating stays valid.
// The game's RH projection recovered through our LH projection has a reflected
// camera basis. Preserve that basis sign: changing it mirrors X and culls faces.
inline bool LevelPose(const Mat4& pose,Mat4& level) {
    if(!IsOrthogonalPose(pose))return false;
    const float sign=BasisDeterminant(pose)<0?-1.0f:1.0f;
    float x=pose.m[0][2],z=pose.m[2][2];
    float length=std::sqrt(x*x+z*z);
    if(length<0.01f) { // Looking vertically: recover heading from the right axis.
        x=-sign*pose.m[2][0];z=sign*pose.m[0][0];length=std::sqrt(x*x+z*z);
    }
    if(length<0.01f)return false;
    x/=length;z/=length;level=Identity();
    level.m[0][0]=sign*z;level.m[2][0]=-sign*x;level.m[0][2]=x;level.m[2][2]=z;
    for(int i=0;i<3;++i)level.m[i][3]=pose.m[i][3];
    return true;
}

inline bool GameProjectionFromViewProjection(const Mat4& rowViewProjection,Mat4& projection) {
    const float* m=&rowViewProjection.m[0][0];
    const float forwardSq = m[3] * m[3] + m[7] * m[7] + m[11] * m[11];
    const float forward = std::sqrt(forwardSq);
    const float x = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    const float y = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    const float depth = (m[2] * m[3] + m[6] * m[7] + m[10] * m[11]) /
                        forwardSq;
    const float depthOffset = m[14] - depth * m[15];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(forward) ||
        !std::isfinite(depth) || !std::isfinite(depthOffset) ||
        x < 0.05f || x > 20.0f || y < 0.05f || y > 20.0f ||
        forward < 0.5f || forward > 1.5f || std::fabs(depth) < 0.1f ||
        std::fabs(depth) > 10.0f || std::fabs(depthOffset) < 1e-6f ||
        std::fabs(depthOffset) > 10000.0f) return false;
    projection = {};
    projection.m[0][0] = x / forward;
    projection.m[1][1] = y / forward;
    projection.m[2][2] = depth;
    projection.m[2][3] = depthOffset;
    projection.m[3][2] = 1.0f;
    return true;
}

inline bool RecoverGameCamera(const Mat4& projection,const Mat4& viewProjection,Mat4& camera) {
    Mat4 inverse{};if(!Invert(projection,inverse))return false;
    const Mat4 view=Mul(inverse,viewProjection);
    if(!IsOrthogonalPose(view))return false;
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
        const Mat4 correction=Mul(InvertRigid(level),camera);
        // A calibration correction may rotate/translate, never reflect, scale
        // or reverse winding. Check the result too, before touching any draw.
        if(!IsOrthogonalPose(correction) || BasisDeterminant(correction)<0.97f)return Identity();
        return correction;
    }
    // Reanchoring a menu/HUD without a grounded player must not undo the
    // existing floor relation when the user has crouched or stood up.
    void Reanchor(float heightDelta,float units) {
        if(active_)heightOffset_+=heightDelta*units;
    }
    bool Active() const {return active_;}
    void Reset() {active_=false;heightOffset_=0;}
    float HeightOffset() const {return heightOffset_;}
private:
    bool active_=false;
    float heightOffset_=0;
};
}
