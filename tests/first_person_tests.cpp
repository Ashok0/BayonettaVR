#include "FirstPersonMath.h"
#include "ControllerInput.h"
#include "FirstPersonGameCameraFixture.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace bvr;
static void Near(float a,float b,float e=.003f){assert(std::fabs(a-b)<e);}
static Mat4 RecoverProjection(const Mat4& row) {
    Mat4 p{};for(int c=0;c<2;++c){float sq=0;for(int r=0;r<3;++r)sq+=row.m[r][c]*row.m[r][c];p.m[c][c]=std::sqrt(sq);}
    float depth=0;for(int r=0;r<3;++r)depth+=row.m[r][2]*row.m[r][3];
    p.m[2][2]=depth;p.m[2][3]=row.m[3][2]-depth*row.m[3][3];p.m[3][2]=1;return p;
}
int main() {
    for(int move:{0,7,9,10,11,12,13,14,16,18,22,24,25,26})assert(FirstPersonTraversal(move));
    for(int move:{-1,8,15,17,19,23,27,31,55,56,458,999})assert(!FirstPersonTraversal(move));
    Near(FirstPersonTurnDelta(1,1.f/60)*60,1.57079632679f);
    Near(FirstPersonTurnDelta(-1,1.f/90)*90,-1.57079632679f);
    Near(FirstPersonTurnDelta(.1f,.1f),0);Near(FirstPersonTurnDelta(1,1),0);
    Near(FirstPersonTurnDelta(std::numeric_limits<float>::quiet_NaN(),.1f),0);
    for(float sign:{-1.f,1.f}) {
        Mat4 heading=Identity();heading.m[0][0]=sign;
        FirstPersonTurnHeading(heading,1.57079632679f);
        Near(heading.m[0][2],sign);Near(heading.m[2][2],0);
        FirstPersonTurnHeading(heading,-1.57079632679f);
        Near(heading.m[0][2],0);Near(heading.m[2][2],1);
        for(int n=0;n<10000;++n)FirstPersonTurnHeading(heading,.01f);
        assert(ValidCameraBasis(heading));Near(CameraBasisDeterminant(heading),sign);
        Near(heading.m[1][1],1);Near(heading.m[1][2],0);Near(heading.m[1][0],0);
    }
    for(float x:{-1000.f,-350.f,0.f,350.f,1000.f})for(float y:{-1000.f,-350.f,0.f,350.f,1000.f}) {
        float a=x,b=y;FirstPersonWalkVector(a,b);
        assert(std::hypot(a,b)<=450.01f);
        Near(a*y-b*x,0,.1f); // Direction and diagonal speed preserved.
        if(std::hypot(x,y)<=450){Near(a,x);Near(b,y);}
    }
    float a=std::numeric_limits<float>::infinity(),b=1000;FirstPersonWalkVector(a,b);assert(a==0&&b==0);
    assert(FirstPersonEnemy(0x20,0,100));
    assert(!FirstPersonEnemy(0x20,1,100)&&!FirstPersonEnemy(0x20,0,0)&&!FirstPersonEnemy(8,0,100));
    for(float angle:{-3.13f,-1.57f,0.f,1.57f,3.13f}) {
        Mat4 tracked=Identity();tracked.m[0][0]=tracked.m[2][2]=std::cos(angle);
        tracked.m[0][2]=std::sin(angle);tracked.m[2][0]=-std::sin(angle);
        float yaw=0;assert(FirstPersonBodyYaw(Identity(),tracked,yaw));Near(yaw,angle);
        Mat4 reflected=Identity();reflected.m[0][0]=-1;
        assert(FirstPersonBodyYaw(reflected,tracked,yaw));Near(yaw,-angle);
    }
    GameCameraFixture game;unsigned cases=0;
    for(float yaw:{-2.8f,-1.3f,0.f,1.1f,2.8f})for(float pitch:{-1.3f,-.5f,0.f,.5f,1.3f})
    for(float height:{-40.f,1.7f,12.f})for(float eyeX:{-.032f,.032f}) {
        float camera[]={10,height+3,20},up[]={0,1,0};
        float aim[]={camera[0]+std::sin(yaw)*std::cos(pitch),camera[1]+std::sin(pitch),camera[2]+std::cos(yaw)*std::cos(pitch)};
        float head[]={camera[0]+3,height,camera[2]-2};
        float headAim[]={head[0]+std::sin(yaw),head[1],head[2]+std::cos(yaw)};
        const auto rowView=game.View(camera,aim,up),rowProjection=game.Projection(.9f,1.77778f,.1f,100000);
        const auto rowVP=Mul(rowView,rowProjection),p=RecoverProjection(rowVP);
        Mat4 invP{};assert(Invert(p,invP));
        Mat4 correction{};assert(FirstPersonCorrection(invP,Transpose(rowVP),head,correction));
        Near(CameraBasisDeterminant(correction),1);
        Mat4 eye=Identity();eye.m[0][3]=-eyeX;
        const auto actual=Mul(p,Mul(eye,Mul(correction,Mul(invP,Transpose(rowVP)))));
        const auto expected=Mul(p,Mul(eye,Mul(invP,Transpose(Mul(game.View(head,headAim,up),rowProjection)))));
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)Near(actual.m[r][c],expected.m[r][c],.005f);
        // Native automatic orbit must not feed a body turn back into the
        // first-person view. Change the actual engine camera underneath it.
        Mat4 fixed{};assert(FirstPersonCorrection(invP,Transpose(rowVP),head,correction,&fixed,true));
        float orbitAim[]={camera[0]+std::sin(yaw+.7f),camera[1]-.3f,camera[2]+std::cos(yaw+.7f)};
        const auto orbitVP=Transpose(Mul(game.View(camera,orbitAim,up),rowProjection));
        assert(FirstPersonCorrection(invP,orbitVP,head,correction,&fixed,false));
        const auto stable=Mul(p,Mul(eye,Mul(correction,Mul(invP,orbitVP))));
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)Near(stable.m[r][c],expected.m[r][c],.006f);
        const auto recovered=InvertRigid(Mul(invP,Mul(correction,Transpose(rowVP))));
        (void)recovered;++cases;
    }
    Mat4 bad{},out=Identity();float head[]={0,1.7f,0};
    assert(!FirstPersonCorrection(Identity(),bad,head,out));
    head[0]=std::numeric_limits<float>::quiet_NaN();assert(!FirstPersonCorrection(Identity(),Identity(),head,out));
    for(const char* s:{"body","boin_hair","boin_hair2","change_hair","boots","R_hand_0","L_hand_1","watch","juwel"})assert(!FirstPersonHeadMesh(s));
    for(const char* s:{"face_game","face_event2","eye_kage_game","glass","glass_renz","front_hair_omote","back_hair_ura","ribon1"})assert(FirstPersonHeadMesh(s));
    CameraTogglePress toggle;
    assert(!toggle.Update(true,false,true,0));assert(toggle.Update(false,false,true,10));
    assert(!toggle.Update(false,false,true,20));
    assert(!toggle.Update(true,false,true,30));
    for(uint64_t t=40;t<4040;t+=100)assert(!toggle.Update(true,false,true,t));
    assert(toggle.Update(false,false,true,4040)); // Long hold toggles once, too.
    for(bool leftFirst:{false,true})for(bool releaseLeftFirst:{false,true}) {
        CameraTogglePress chord;assert(!chord.Update(!leftFirst,leftFirst,true,0));
        for(uint64_t t=10;t<1300;t+=100)assert(!chord.Update(true,true,true,t));
        assert(!chord.Update(releaseLeftFirst,!releaseLeftFirst,true,1310));
        assert(!chord.Update(false,false,true,1320));
    }
    CameraTogglePress gap;gap.Update(true,false,true,0);assert(!gap.Update(false,false,true,300));
    gap.Update(true,false,true,310);gap.Update(true,false,false,320);assert(!gap.Update(false,false,true,330));
    TouchMapper mapper;TouchHand l{},r{};l.connected=r.connected=true;
    r.stickClick=true;assert(mapper.Map(l,r,true,0).cameraToggleSequence==0);
    r.stickClick=false;assert(mapper.Map(l,r,true,10).cameraToggleSequence==1);
    l.stickClick=r.stickClick=true;
    for(uint64_t t=20;t<1120;t+=100)mapper.Map(l,r,true,t);
    auto input=mapper.Map(l,r,true,1120);assert(input.recenterSequence==1&&input.cameraToggleSequence==1);
    l.stickClick=r.stickClick=false;assert(mapper.Map(l,r,true,1130).cameraToggleSequence==1);
    r.grip=.8f;r.stickClick=true;mapper.Map(l,r,true,1140);r.stickClick=false;
    assert(mapper.Map(l,r,true,1150).cameraToggleSequence==1);
    printf("First person: %u actual-engine stereo camera cases; head mesh/body selection, R3 tap/hold/release, both chord orders, modifiers and interruption tests passed.\n",cases);
}
