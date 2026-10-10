#pragma once
#include "Matrix.h"

namespace bvr {
// Keep the render orientation at the recenter orientation while putting this
// eye at its fully tracked optical center. SteamVR applies rotation afterwards.
// S*E and H*E must have the SAME translation, including the eye's movement
// around the head during yaw/pitch/roll. Just using H's position misses that.
inline Mat4 CompositorRotationHead(const Mat4& origin, const Mat4& head,
                                  const Mat4& eyeToHead) {
    Mat4 renderHead = origin;
    for (int row = 0; row < 3; ++row) {
        renderHead.m[row][3] = head.m[row][3];
        for (int col = 0; col < 3; ++col)
            renderHead.m[row][3] +=
                (head.m[row][col] - origin.m[row][col]) * eyeToHead.m[col][3];
    }
    return renderHead;
}
}
