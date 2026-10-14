#include "TrackingMath.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>
using namespace bvr;
static Mat4 Pose(float yaw,float pitch,float roll,float x,float y,float z) {
    Mat4 ry=Identity(),rx=Identity(),rz=Identity();
    ry.m[0][0]=ry.m[2][2]=std::cos(yaw);ry.m[0][2]=std::sin(yaw);ry.m[2][0]=-std::sin(yaw);
    rx.m[1][1]=rx.m[2][2]=std::cos(pitch);rx.m[2][1]=std::sin(pitch);rx.m[1][2]=-std::sin(pitch);
    rz.m[0][0]=rz.m[1][1]=std::cos(roll);rz.m[1][0]=std::sin(roll);rz.m[0][1]=-std::sin(roll);
    Mat4 result=Mul(ry,Mul(rx,rz));result.m[0][3]=x;result.m[1][3]=y;result.m[2][3]=z;return result;
}
static void Near(float a,float b,float e=.0002f){assert(std::fabs(a-b)<e);}
static void Near(const Mat4& a,const Mat4& b,float e=.0002f){for(int r=0;r<4;++r)for(int c=0;c<4;++c)Near(a.m[r][c],b.m[r][c],e);}
int main() {
    unsigned poses=0,orbits=0,oldTilt=0;
    for(float yaw:{-3.f,-1.5f,0.f,1.5f,3.f})for(float pitch:{-.8f,0.f,.8f})
    for(float roll:{-.6f,0.f,.6f})for(float height:{.9f,1.8f}) {
        const auto launch=Pose(yaw,pitch,roll,1,height,-2);
        Mat4 origin{};assert(LevelTrackingOrigin(launch,origin));
        Near(origin,Pose(yaw,0,0,1,height,-2));
        // After a tilted launch, looking level produces a neutral view.
        const auto upright=Pose(yaw,0,0,1,height,-2);
        Near(Mul(InvertRigid(origin),upright),Identity());
        if(pitch!=0 || roll!=0){assert(RotationDistanceDegrees(launch,upright)>20);++oldTilt;}
        // Current head tilt still tracks normally; it is not frozen into the world.
        Near(Mul(InvertRigid(origin),launch),Pose(0,pitch,roll,0,0,0));
        Mat4 crouched=upright;crouched.m[1][3]-=.25f;
        const auto relative=Mul(InvertRigid(origin),crouched);Near(relative.m[1][3],-.25f);
        // Repeated presses reset heading to the new horizontal direction.
        const auto turned=Pose(yaw+.4f,-.3f,.4f,1,height,-2);
        assert(LevelTrackingOrigin(turned,origin));Near(origin,Pose(yaw+.4f,0,0,1,height,-2));
        ++poses;
    }
    // The production scene path is restored to .54: Peye * Heye^-1 *
    // Pgame^-1 * originalClip. No camera leveling or floor offset is inserted.
    // At neutral tracking, every game-controlled orbit retains its original
    // center, height, pitch and yaw, with only the expected two-eye separation.
    for(float yaw:{-3.f,-1.f,0.f,1.f,3.f})for(float pitch:{-1.3f,-.6f,0.f,.6f,1.3f})
    for(float cameraY:{-40.f,0.f,5.f,100.f}) {
        const auto camera=Pose(yaw,pitch,.1f,10,cameraY,-15);
        const auto view=InvertRigid(camera),p=PerspectiveFovLH(.9f,1.777778f,.1f,10000);
        Mat4 invP{};assert(Invert(p,invP));
        float center[3]{};
        for(float eyeX:{-.032f,.032f}) {
            Mat4 eyeView=Identity();eyeView.m[0][3]=-eyeX;
            const auto clip=Mul(Mul(p,Mul(eyeView,invP)),Mul(p,view));
            Near(clip,Mul(p,Mul(eyeView,view)),.0003f);
            const auto finalView=Mul(invP,clip);
            const auto finalCamera=InvertRigid(finalView);
            for(int r=0;r<3;++r){center[r]+=finalCamera.m[r][3]*.5f;for(int c=0;c<3;++c)Near(finalCamera.m[r][c],camera.m[r][c]);}
        }
        for(int r=0;r<3;++r)Near(center[r],camera.m[r][3],.002f);
        ++orbits;
    }
    Mat4 origin=Identity(),bad=Identity();bad.m[1][3]=std::numeric_limits<float>::quiet_NaN();
    assert(!LevelTrackingOrigin(bad,origin));Near(origin,Identity());
    bad={};assert(!LevelTrackingOrigin(bad,origin));Near(origin,Identity());
    assert(LevelTrackingOrigin(Pose(.7f,1.57079633f,0,0,1.7f,0),origin));
    Near(origin,Pose(.7f,0,0,0,1.7f,0));
    printf("Recenter: %u tilted-launch/repeat/height cases (%u reproduce old persistent tilt), %u stereo camera orbits preserve original position and orientation; invalid/vertical poses passed.\n",poses,oldTilt,orbits);
}
