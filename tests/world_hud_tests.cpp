#include "WorldHud.h"
#include <cstdio>
#include <cstdlib>

using namespace bvr;
static void Require(bool ok, const char* message) {
    if (!ok) { std::printf("HUD failure: %s\n", message); std::exit(1); }
}
static bool Near(float a, float b) { return std::fabs(a-b) < 1e-4f; }
static void Project(const Mat4& m, float x, float y, float z, float (&out)[3]) {
    float v[4]{x,y,z,1}, p[4]{};
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) p[r]+=m.m[r][c]*v[c];
    for (int r=0;r<3;++r) out[r]=p[r]/p[3];
}
static Mat4 Yaw(float angle) {
    Mat4 m=Identity();
    m.m[0][0]=m.m[2][2]=std::cos(angle);
    m.m[0][2]=std::sin(angle); m.m[2][0]=-std::sin(angle);
    return m;
}
int main() {
    const Mat4 panel=HudPanelFromClip(16.0f/9,3.2f,2.0f,0.0f);
    const Mat4 projection=PerspectiveFovLH(1.57079632679f,16.0f/9,0.1f,1000);
    const Mat4 initial=Mul(projection,panel);
    float center[3]{}, corner[3]{}, deep[3]{};
    Project(initial,0,0,0,center); Project(initial,1,1,0,corner);
    Project(initial,1,1,0.99f,deep);
    Require(Near(center[0],0) && Near(center[1],0), "panel centered at recenter");
    Require(Near(corner[0],0.45f) && Near(corner[1],0.45f), "aspect-correct corners within view");
    for(int i=0;i<3;++i) Require(Near(corner[i],deep[i]), "UI depth flattened onto panel");
    Require(corner[2]>0 && corner[2]<1, "panel inside depth clip range");

    // Look directly at the panel's right edge. That fixed world point must
    // reach the optical center, while the panel center moves to the left.
    const Mat4 turned=Mul(projection,Mul(InvertRigid(Yaw(std::atan(1.6f/2))),panel));
    Project(turned,1,0,0,corner); Project(turned,0,0,0,center);
    Require(Near(corner[0],0) && center[0]<-0.4f, "turning lets the user look at a fixed HUD edge");
    Mat4 head=Identity(); head.m[0][3]=0.3f;
    Project(Mul(projection,Mul(InvertRigid(head),panel)),0,0,0,center);
    Require(Near(center[0],-0.084375f), "lean produces world-locked parallax");
    head=Identity(); head.m[2][3]=0.5f;
    Project(Mul(projection,Mul(InvertRigid(head),panel)),1,1,0,corner);
    Require(corner[0]>0.45f && corner[1]>0.45f, "leaning closer enlarges HUD");

    float eyes[2][3]{};
    for(int eye=0;eye<2;++eye) {
        Mat4 eyePose=Identity(); eyePose.m[0][3]=eye ? 0.032f : -0.032f;
        Project(Mul(projection,Mul(InvertRigid(eyePose),panel)),0,0,0,eyes[eye]);
    }
    Require(eyes[0][0]>0 && Near(eyes[0][0],-eyes[1][0]), "correct stereo disparity at finite depth");
    Mat4 origin=Yaw(0.7f); origin.m[0][3]=2; origin.m[1][3]=1.6f;
    const Mat4 relative=Mul(InvertRigid(origin),origin);
    Project(Mul(projection,Mul(InvertRigid(relative),panel)),0,0,0,center);
    Require(Near(center[0],0) && Near(center[1],0), "recenter at a new orientation/position restores panel center");
    Project(Mul(projection,HudPanelFromClip(16.0f/9,3.2f,2,0.3f)),0,0,0,center);
    Require(Near(center[1],0.15f), "positive height raises panel");

    float bounds[4]{};
    ProjectHudScissor(initial,-1,-1,1,1,bounds);
    Require(Near(bounds[0],-0.45f) && Near(bounds[1],-0.45f) &&
        Near(bounds[2],0.45f) && Near(bounds[3],0.45f), "scissor follows projected panel");
    ProjectHudScissor(turned,0,-1,1,1,bounds);
    Project(turned,0,-1,0,corner);
    Require(bounds[0]<=corner[0]+1e-4f && Near(bounds[2],0), "scissor follows head turn");
    Mat4 crossing=Identity(); crossing.m[3][0]=2;
    ProjectHudScissor(crossing,-1,-1,1,1,bounds);
    Require(bounds[0]==-1 && bounds[1]==-1 && bounds[2]==1 && bounds[3]==1,
        "eye-plane crossing uses conservative bounds");
    Mat4 offscreen=Identity(); offscreen.m[0][3]=5;
    ProjectHudScissor(offscreen,-1,-1,1,1,bounds);
    Require(bounds[0]==1 && bounds[2]==1, "fully offscreen scissor is empty");
    Require(LooksOrthographic(Identity()) && !LooksOrthographic(projection), "UI/perspective matrix classification");
    std::puts("world HUD: fixed anchor, head turns, lean, stereo, recenter, size, depth and scissor checks passed");
    return 0;
}
