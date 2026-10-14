#pragma once
#include "Matrix.h"
#include <array>

namespace bvr {
struct ReferenceVertex {
    float position[4]; // Homogeneous D3D clip coordinates, NOT screen pixels.
    float color[4];
};
using ReferenceVertices = std::array<ReferenceVertex, 64>;

// A recenter-space plane three metres ahead, independent of Bayonetta's
// camera, world units, shaders, culling, and post-processing.
inline ReferenceVertices BuildReferenceGrid(const Mat4& clip) {
    ReferenceVertices vertices{};
    unsigned next = 0;
    auto point = [&](float x, float y, bool cross) {
        auto& v = vertices[next++];
        for (int r = 0; r < 4; ++r)
            v.position[r] = clip.m[r][0]*x + clip.m[r][1]*y +
                            clip.m[r][2]*3.0f + clip.m[r][3];
        v.color[0] = cross ? 1.0f : 0.0f;
        v.color[1] = 1.0f;
        v.color[2] = cross ? 0.0f : 1.0f;
        v.color[3] = 1.0f;
    };
    for (int i = -8; i <= 8; ++i) {
        point(i*0.5f, -3.0f, false); point(i*0.5f, 3.0f, false);
    }
    for (int i = -6; i <= 6; ++i) {
        point(-4.0f, i*0.5f, false); point(4.0f, i*0.5f, false);
    }
    point(-0.15f, 0, true); point(0.15f, 0, true);
    point(0, -0.15f, true); point(0, 0.15f, true);
    return vertices;
}
}
