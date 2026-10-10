#include "Matrix.h"
#include <cmath>
#include <cstdio>

using namespace bvr;

static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a-b) < e; }

int main() {
    Mat4 rigid = Identity();
    rigid.m[0][0]=0; rigid.m[0][2]=1; rigid.m[2][0]=-1; rigid.m[2][2]=0;
    rigid.m[0][3]=3; rigid.m[1][3]=-2; rigid.m[2][3]=7;
    Mat4 ri = InvertRigid(rigid);
    Mat4 unit = Mul(rigid,ri);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        if(!Near(unit.m[r][c],r==c?1.0f:0.0f)) return 1;

    Mat4 p = PerspectiveFovLH(1.1f, 16.0f/9.0f, 0.1f, 1000.0f), inv{};
    if(!Invert(p,inv)) return 2;
    unit=Mul(p,inv);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        if(!Near(unit.m[r][c],r==c?1.0f:0.0f)) return 3;

    Mat4 raw=PerspectiveRawLH(-1,1,-1,1,0.1f,1000.0f);
    if(!Near(raw.m[0][0],1)||!Near(raw.m[1][1],1)||!Near(raw.m[3][2],1)) return 4;
    std::puts("matrix tests passed");
    return 0;
}
