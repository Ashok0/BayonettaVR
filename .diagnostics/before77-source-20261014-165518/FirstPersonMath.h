#pragma once
#include "Matrix.h"
#include <cstring>

namespace bvr {
inline bool FirstPersonHideJumpBody(int move) {
    // Jump (including descent), dodge-limit jump, wind fall and special
    // jump. Landing/ledge/ground states restore the ordinary head-only mask.
    return move==11||move==18||move==22||move==25;
}
inline bool FirstPersonTraversal(int move) {
    // Steam pl0000 move IDs, named in the local BayoHook SDK. Keep the
    // ordinary jump/fall, landing and ledge traversal chain, not air attacks.
    if(move>=0&&move<=7)return true;
    switch(move) {
    case 9: case 10: case 11: case 12: case 13: case 14: case 16:
    case 18: case 22: case 24: case 25: case 26:return true;
    default:return false;
    }
}
inline float CameraBasisDeterminant(const Mat4& a) {
    return a.m[0][0]*(a.m[1][1]*a.m[2][2]-a.m[1][2]*a.m[2][1])
          -a.m[0][1]*(a.m[1][0]*a.m[2][2]-a.m[1][2]*a.m[2][0])
          +a.m[0][2]*(a.m[1][0]*a.m[2][1]-a.m[1][1]*a.m[2][0]);
}
inline bool ValidCameraBasis(const Mat4& a) {
    for(const auto& row:a.m)for(float v:row)if(!std::isfinite(v))return false;
    for(int r=0;r<3;++r)for(int s=0;s<3;++s) {
        float dot=0;for(int c=0;c<3;++c)dot+=a.m[r][c]*a.m[s][c];
        if(std::fabs(dot-(r==s?1.f:0.f))>.02f)return false;
    }
    for(int c=0;c<4;++c)if(std::fabs(a.m[3][c]-(c==3?1.f:0.f))>.02f)return false;
    return std::fabs(std::fabs(CameraBasisDeterminant(a))-1.f)<.03f;
}
// Both matrices use column vectors. Recover through the SAME projection used
// by the existing stereo path. Its camera basis can be reflected; preserve
// that sign or the scene gets mirrored and back-face culling reverses.
inline bool FirstPersonCorrection(const Mat4& inverseProjection,const Mat4& viewProjection,
                                  const float* eyes,Mat4& correction,Mat4* heading=nullptr,bool captureHeading=false) {
    const Mat4 view=Mul(inverseProjection,viewProjection);
    if(!ValidCameraBasis(view))return false;
    const Mat4 camera=InvertRigid(view);
    const float sign=CameraBasisDeterminant(camera)<0?-1.f:1.f;
    float x=camera.m[0][2],z=camera.m[2][2];
    float length=std::sqrt(x*x+z*z);
    if(length<.01f){x=-sign*camera.m[2][0];z=sign*camera.m[0][0];length=std::sqrt(x*x+z*z);}
    if(length<.01f)return false;
    x/=length;z/=length;
    Mat4 head=Identity();
    head.m[0][0]=sign*z;head.m[2][0]=-sign*x;head.m[0][2]=x;head.m[2][2]=z;
    if(heading) {
        if(captureHeading)*heading=head;
        else head=*heading;
    }
    for(int r=0;r<3;++r){if(!std::isfinite(eyes[r]))return false;head.m[r][3]=eyes[r];}
    const Mat4 result=Mul(InvertRigid(head),camera);
    if(!ValidCameraBasis(result)||CameraBasisDeterminant(result)<.97f)return false;
    correction=result;return true;
}
inline void FirstPersonWalkVector(float& x,float& y) {
    // Native axes range from -1000 to 1000. Movement deadzone is radius
    // 300; the run transition is 900 (squared comparisons in the Steam EXE).
    if(!std::isfinite(x)||!std::isfinite(y)){x=y=0;return;}
    const float length=std::hypot(x,y);
    if(length>450.f){x*=450.f/length;y*=450.f/length;}
}
inline bool FirstPersonEnemy(unsigned flags,unsigned alive,int health) {
    return (flags&0x20)!=0 && (alive&1)==0 && health>0;
}
inline bool FirstPersonBodyYaw(const Mat4& heading,const Mat4& tracked,float& yaw) {
    const Mat4 gaze=Mul(heading,tracked);
    const float x=gaze.m[0][2],z=gaze.m[2][2];
    if(!std::isfinite(x)||!std::isfinite(z)||x*x+z*z<.0001f)return false;
    yaw=std::atan2(x,z);return true;
}
inline float FirstPersonTurnDelta(float axis,float seconds) {
    if(!std::isfinite(axis)||!std::isfinite(seconds)||seconds<=0||seconds>.25f)return 0;
    const float magnitude=std::fmin(std::fabs(axis),1.f);
    if(magnitude<=.2f)return 0;
    return std::copysign((magnitude-.2f)/.8f,axis)*1.57079632679f*seconds;
}
inline void FirstPersonTurnHeading(Mat4& heading,float radians) {
    if(!std::isfinite(radians)||radians==0)return;
    const float sign=CameraBasisDeterminant(heading)<0?-1.f:1.f;
    // Positive stick X turns toward camera-right even for the native
    // reflected camera basis. Rebuild a level basis to avoid drift/roll.
    const float yaw=std::atan2(heading.m[0][2],heading.m[2][2])+sign*radians;
    const float x=std::sin(yaw),z=std::cos(yaw);
    heading=Identity();
    heading.m[0][0]=sign*z;heading.m[2][0]=-sign*x;
    heading.m[0][2]=x;heading.m[2][2]=z;
}
inline bool FirstPersonHeadMesh(const char* name) {
    // Names from the installed pl0010 WMB. In particular body, change_hair,
    // boin_hair, hands and boots must NOT be removed by a broad "hair" match.
    const char* const names[]={"face_game","face_event2","eye_kage_game","eye_kage_event1",
        "front_hair_ura","back_hair_ura","front_hair_omote","hair_drill","hair_drill3",
        "ribon1","glass","glass_renz","glass_renz1","hokuro_cover"};
    for(const char* candidate:names)if(!std::strcmp(name,candidate))return true;
    return false;
}
}
