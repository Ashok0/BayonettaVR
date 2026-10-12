#include "FloorCalibration.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include "GameCameraFixture.h"
using namespace bvr;
static Mat4 Pose(float yaw,float pitch,float roll,float x,float y,float z) {
    Mat4 ry=Identity(),rx=Identity(),rz=Identity();
    ry.m[0][0]=ry.m[2][2]=std::cos(yaw);ry.m[0][2]=std::sin(yaw);ry.m[2][0]=-std::sin(yaw);
    rx.m[1][1]=rx.m[2][2]=std::cos(pitch);rx.m[2][1]=std::sin(pitch);rx.m[1][2]=-std::sin(pitch);
    rz.m[0][0]=rz.m[1][1]=std::cos(roll);rz.m[1][0]=std::sin(roll);rz.m[0][1]=-std::sin(roll);
    Mat4 result=Mul(ry,Mul(rx,rz));result.m[0][3]=x;result.m[1][3]=y;result.m[2][3]=z;return result;
}
static void Near(float a,float b,float tolerance=0.002f){assert(std::fabs(a-b)<tolerance);}
static void Near(const Mat4& a,const Mat4& b){for(int r=0;r<4;++r)for(int c=0;c<4;++c)Near(a.m[r][c],b.m[r][c]);}
int main() {
    int cases=0;
    for(float handedness:{-1.0f,1.0f})for(float units:{0.5f,1.0f,10.0f})for(float height:{0.8f,1.65f,2.1f})
    for(int yaw=-3;yaw<=3;++yaw)for(int pitch=-2;pitch<=2;++pitch)for(int roll=-2;roll<=2;++roll) {
        Mat4 camera=Pose(yaw*.9f,pitch*.5f,roll*.3f,20,8,-12);
        for(int r=0;r<3;++r)camera.m[r][0]*=handedness;
        const Mat4 head=Pose(yaw*.7f,pitch*.4f,roll*.35f,2,height,-3);
        Mat4 origin{};assert(LevelPose(head,origin));
        Near(origin.m[1][0],0);Near(origin.m[1][1],1);Near(origin.m[1][2],0);
        const float ground=2.0f;
        FloorCalibration floor;assert(floor.Calibrate(camera,ground,height,units));
        const Mat4 correction=floor.Correction(camera);
        Near(BasisDeterminant(correction),1);
        Mat4 levelCamera{};assert(LevelPose(camera,levelCamera));
        levelCamera.m[1][3]=ground+height*units;
        Near(Mul(correction,InvertRigid(camera)),InvertRigid(levelCamera));
        // Convert the entire virtual floor back into standing tracking space.
        // Its height and normal must match the physical y=0 plane, even when
        // calibration was invoked with head AND game camera pitched/rolled.
        Mat4 metres=Identity();metres.m[0][0]=metres.m[1][1]=1/units;metres.m[2][2]=-1/units;
        const Mat4 worldToTracking=Mul(origin,Mul(metres,Mul(correction,InvertRigid(camera))));
        Near(worldToTracking.m[1][0],0);Near(worldToTracking.m[1][2],0);
        Near(worldToTracking.m[1][1]*ground+worldToTracking.m[1][3],0);
        // Eye positions and crouches must move relative to that plane, not
        // recalibrate it implicitly. Retain actual head roll/pitch in rendering.
        for(float crouch:{0.0f,-0.4f})for(float eyeX:{-0.032f,0.032f}) {
            Mat4 current=head;current.m[1][3]+=crouch;Mat4 eye=Identity();eye.m[0][3]=eyeX;
            const Mat4 eyeTracking=Mul(current,eye);
            // Use an actual inverse for scaling, not InvertRigid.
            Mat4 invMetres{};assert(Invert(metres,invMetres));
            const Mat4 correctEye=Mul(levelCamera,Mul(invMetres,Mul(InvertRigid(origin),eyeTracking)));
            const Mat4 mappedBack=Mul(worldToTracking,correctEye);
            Near(mappedBack,eyeTracking);
        }
        // Recover the camera from the same P*V data as the live game reader.
        const Mat4 p=PerspectiveFovLH(1.0f,1.7777778f,0.1f,10000);
        Mat4 recovered{};assert(RecoverGameCamera(p,Mul(p,InvertRigid(camera)),recovered));Near(recovered,camera);
        ++cases;
    }
    Mat4 vertical=Pose(.7f,1.57079633f,0,0,1.7f,0),level{};
    assert(LevelPose(vertical,level));Near(level.m[1][1],1);
    // Run the original engine's look-at and perspective code. This is the
    // coordinate convention .55 never exercised: recovered determinant -1.
    GameCameraFixture game;
    for(float yaw:{-2.0f,0.0f,1.5f})for(float pitch:{-.6f,0.0f,.6f}) {
        float eye[]={10,-39.935f,20};float up[]={0,1,0};
        float target[]={eye[0]+std::sin(yaw)*std::cos(pitch),eye[1]+std::sin(pitch),eye[2]+std::cos(yaw)*std::cos(pitch)};
        const Mat4 rowView=game.View(eye,target,up);
        const Mat4 rowProjection=game.Projection(.872664626f,1.7777778f,.1f,1000000);
        Near(rowProjection.m[2][3],-1);
        Mat4 projection{},recovered{};
        assert(GameProjectionFromViewProjection(Mul(rowView,rowProjection),projection));
        assert(RecoverGameCamera(projection,Transpose(Mul(rowView,rowProjection)),recovered));
        Near(BasisDeterminant(recovered),-1);
        for(int i=0;i<3;++i)Near(recovered.m[i][3],eye[i]);
        FloorCalibration native;assert(native.Calibrate(recovered,-42,1.262f,1));
        const Mat4 fixed=Mul(native.Correction(recovered),Transpose(rowView));
        // Unprojecting camera +X must still point along the original game +X.
        Mat4 newCamera=InvertRigid(fixed);
        float rightDot=0;for(int r=0;r<3;++r)rightDot+=newCamera.m[r][0]*rowView.m[r][0];
        assert(rightDot>.99f);
        Near(BasisDeterminant(native.Correction(recovered)),1);
    }
    FloorCalibration floor;Mat4 camera=Pose(.5f,.7f,.2f,0,5,0);
    Near(floor.Correction(camera),Identity());assert(floor.Calibrate(camera,0,1.7f,1));
    const float offset=floor.HeightOffset();
    assert(!floor.Calibrate(camera,0,0,1));assert(!floor.Calibrate(camera,0,1.7f,0));
    assert(!floor.Calibrate(camera,std::numeric_limits<float>::quiet_NaN(),1.7f,1));Near(offset,floor.HeightOffset());
    floor.Reanchor(-.5f,1);Near(floor.HeightOffset(),offset-.5f);
    Mat4 bad{};assert(!RecoverGameCamera(bad,bad,level));
    bad=Identity();bad.m[0][0]=2;assert(!RecoverGameCamera(Identity(),bad,level));
    printf("Floor calibration: %d handedness/tilt/height/scale cases plus actual Steam camera code; floor plane, stereo, right direction and non-reflecting correction passed.\n",cases);
}
