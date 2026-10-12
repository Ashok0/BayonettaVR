#pragma once
#include "Matrix.h"

namespace bvr {
// Recenter the tracking space around the headset's horizontal heading.
// Pitch and roll remain live tracking inputs, never part of the stored origin.
// This function has no game-camera, terrain, or floor-height inputs.
inline bool LevelTrackingOrigin(const Mat4& head,Mat4& origin) {
    for(const auto& row:head.m)for(float value:row)if(!std::isfinite(value))return false;
    float x=head.m[0][2],z=head.m[2][2];
    float length=std::sqrt(x*x+z*z);
    if(length<0.01f) {
        // At vertical gaze the forward heading is undefined; use the right axis.
        x=-head.m[2][0];z=head.m[0][0];length=std::sqrt(x*x+z*z);
    }
    if(length<0.01f)return false;
    x/=length;z/=length;
    Mat4 level=Identity();
    level.m[0][0]=z;level.m[2][0]=-x;level.m[0][2]=x;level.m[2][2]=z;
    for(int row=0;row<3;++row)level.m[row][3]=head.m[row][3];
    origin=level;return true;
}

// Keep the render orientation at the selected anchor orientation while putting this
// eye at its fully tracked optical center. SteamVR applies rotation afterwards.
// S*E and H*E must have the SAME translation, including the eye's movement
// around the head during yaw/pitch/roll. Just using H's position misses that.
inline Mat4 CompositorRotationHead(const Mat4& anchor, const Mat4& head,
                                  const Mat4& eyeToHead) {
    Mat4 renderHead = anchor;
    for (int row = 0; row < 3; ++row) {
        renderHead.m[row][3] = head.m[row][3];
        for (int col = 0; col < 3; ++col)
            renderHead.m[row][3] +=
                (head.m[row][col] - anchor.m[row][col]) * eyeToHead.m[col][3];
    }
    return renderHead;
}

inline float RotationDistanceDegrees(const Mat4& a, const Mat4& b) {
    float trace = 0.0f;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) trace += a.m[row][col] * b.m[row][col];
    float cosine = (trace - 1.0f) * 0.5f;
    if (cosine > 1.0f) cosine = 1.0f;
    if (cosine < -1.0f) cosine = -1.0f;
    return std::acos(cosine) * (180.0f / 3.14159265358979323846f);
}

// Update only orientation; this must never reset the world/recenter origin.
// Matrix distance handles yaw wraparound, straight-up views and roll without
// Euler angle discontinuities. Call once before rendering either eye.
inline bool FollowRenderAnchor(Mat4& anchor, const Mat4& head, float thresholdDegrees) {
    if (thresholdDegrees <= 0.0f ||
        RotationDistanceDegrees(anchor, head) <= thresholdDegrees) return false;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) anchor.m[row][col] = head.m[row][col];
    return true;
}
}
