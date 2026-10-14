#pragma once
#include "Matrix.h"

namespace bvr {
inline bool LooksOrthographic(const Mat4& m) {
    return std::fabs(m.m[3][0]) < 1e-5f && std::fabs(m.m[3][1]) < 1e-5f &&
           std::fabs(m.m[3][2]) < 1e-5f && std::fabs(m.m[3][3] - 1.0f) < 1e-4f;
}

// Original UI clip coordinates span [-1,+1]. Place that rectangle in metres
// in the recenter coordinate system (+Z forward), discarding its screen depth.
// The panel does not depend on the current HMD pose or the game's world scale.
inline Mat4 HudPanelFromClip(float aspect, float width, float distance, float height) {
    Mat4 panel{};
    panel.m[0][0] = width * 0.5f;
    panel.m[1][1] = width / (2.0f * aspect);
    panel.m[1][3] = height;
    panel.m[2][3] = distance;
    panel.m[3][3] = 1.0f;
    return panel;
}

// Conservative screen bounds of a scissor rectangle after projection onto
// the panel. Inputs/outputs are normalized clip coordinates, with y up.
// A rectangle crossing the eye plane cannot be bounded by its four corners;
// use the full viewport there and let homogeneous clipping handle the draw.
inline void ProjectHudScissor(const Mat4& clip, float left, float bottom,
                             float right, float top, float (&bounds)[4]) {
    bounds[0] = bounds[1] = 1.0f;
    bounds[2] = bounds[3] = -1.0f;
    for (int i = 0; i < 4; ++i) {
        const float x = (i & 1) ? right : left;
        const float y = (i & 2) ? top : bottom;
        const float w = clip.m[3][0]*x + clip.m[3][1]*y + clip.m[3][3];
        if (w <= 1e-5f) {
            bounds[0] = bounds[1] = -1.0f;
            bounds[2] = bounds[3] = 1.0f;
            return;
        }
        const float px = (clip.m[0][0]*x + clip.m[0][1]*y + clip.m[0][3]) / w;
        const float py = (clip.m[1][0]*x + clip.m[1][1]*y + clip.m[1][3]) / w;
        if (px < bounds[0]) bounds[0] = px;
        if (py < bounds[1]) bounds[1] = py;
        if (px > bounds[2]) bounds[2] = px;
        if (py > bounds[3]) bounds[3] = py;
    }
    for (float& value : bounds) {
        if (value < -1.0f) value = -1.0f;
        if (value > 1.0f) value = 1.0f;
    }
}
}
