#pragma once
#include <cmath>
#include <cstring>

namespace bvr {
struct Mat4 { float m[4][4]{}; };

inline Mat4 Identity() {
    Mat4 a{};
    for (int i = 0; i < 4; ++i) a.m[i][i] = 1.0f;
    return a;
}

inline Mat4 Mul(const Mat4& a, const Mat4& b) {
    Mat4 o{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) o.m[r][c] += a.m[r][k] * b.m[k][c];
    return o;
}

inline Mat4 Transpose(const Mat4& a) {
    Mat4 o{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) o.m[r][c] = a.m[c][r];
    return o;
}

inline Mat4 InvertRigid(const Mat4& a) {
    Mat4 o = Identity();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) o.m[r][c] = a.m[c][r];
    for (int r = 0; r < 3; ++r)
        o.m[r][3] = -(o.m[r][0] * a.m[0][3] + o.m[r][1] * a.m[1][3] + o.m[r][2] * a.m[2][3]);
    return o;
}

inline bool Invert(const Mat4& a, Mat4& out) {
    float aug[4][8]{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) aug[r][c] = a.m[r][c];
        aug[r][r + 4] = 1.0f;
    }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(aug[r][c]) > std::fabs(aug[pivot][c])) pivot = r;
        if (std::fabs(aug[pivot][c]) < 1e-8f) return false;
        if (pivot != c) for (int k = 0; k < 8; ++k) { float t=aug[c][k]; aug[c][k]=aug[pivot][k]; aug[pivot][k]=t; }
        const float d = aug[c][c];
        for (int k = 0; k < 8; ++k) aug[c][k] /= d;
        for (int r = 0; r < 4; ++r) if (r != c) {
            const float f = aug[r][c];
            for (int k = 0; k < 8; ++k) aug[r][k] -= f * aug[c][k];
        }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out.m[r][c] = aug[r][c+4];
    return true;
}

inline Mat4 PerspectiveFovLH(float fovy, float aspect, float zn, float zf) {
    Mat4 p{};
    const float y = 1.0f / std::tan(fovy * 0.5f);
    p.m[0][0] = y / aspect;
    p.m[1][1] = y;
    p.m[2][2] = zf / (zf - zn);
    p.m[2][3] = -zn * zf / (zf - zn);
    p.m[3][2] = 1.0f;
    return p;
}

inline Mat4 PerspectiveRawLH(float l, float r, float t, float b, float zn, float zf) {
    Mat4 p{};
    p.m[0][0] = 2.0f / (r - l);
    p.m[0][2] = -(r + l) / (r - l);
    p.m[1][1] = 2.0f / (b - t);
    p.m[1][2] = -(b + t) / (b - t);
    p.m[2][2] = zf / (zf - zn);
    p.m[2][3] = -zn * zf / (zf - zn);
    p.m[3][2] = 1.0f;
    return p;
}

// Keep the game's depth encoding: soft particles reconstruct view Z using
// its projection constants. Replacing the FOV must not change that encoding.
inline Mat4 WithSceneDepth(Mat4 eye, const Mat4& scene) {
    for(int c=0;c<4;++c){eye.m[2][c]=scene.m[2][c];eye.m[3][c]=scene.m[3][c];}
    return eye;
}
}
